#include "SerialVESC.h"
#include "common.h"
#include "OTA.h"
#include "crc.h"
#include "device.h"
#include "config.h"
#include "CustomMixer.h"

constexpr int32_t DUTY_RANGE_SNAP_TO_MAX = 99500; // 99.5%
constexpr int32_t POSITION_RANGE_SNAP_TO = 360000000; // 360 deg
constexpr int32_t POSITION_RANGE_SNAP_MIN = POSITION_RANGE_SNAP_TO - 500000;
constexpr int32_t POSITION_RANGE_SNAP_MAX = POSITION_RANGE_SNAP_TO + 500000;
// 1073741823 (0x3FFFFFFF) is the practical technical limit here because larger
// decoded values can overflow the signed int32 math used by i32map() in
// bidirectional mode. We intentionally cap at a cleaner 1000000000 so the
// limit is easier to read and reason about.
constexpr int32_t VESC_RANGE_MAX = 1000000000;
static int32_t i32map(int32_t x, int32_t in_min, int32_t in_max, int32_t out_min, int32_t out_max);
static int32_t decode_ufloat16(uint16_t v);
static int32_t xy_to_vesc_pos_offset(int16_t x, int16_t y, bool invert);
static int16_t xy_magnitude(int16_t x, int16_t y);

static Crc2Byte* vesc_crc = NULL; // we only need one instance even if two serial ports are used

void SerialVESC::begin(uint8_t idx, int8_t pin)
{
    DBGVLN("SerialVESC::begin %u %d", idx, pin);

    this->idx = idx;
    this->pin = pin;

    // cache the configuration, depending on which serial port we are
    const uint32_t* cfg_int_ptr = config.GetVescCfg();
    int copy_idx = (this->idx == 0) ? 0 : 3;
    memcpy((void*)this->cfg, (void*)&(cfg_int_ptr[copy_idx]), sizeof(vesc_cfg_t) * 3);
    for (int i = 0; i < 3; i++) {
        this->range[i] = decode_ufloat16(this->cfg[i].range);
    }

    if (vesc_crc == NULL) {
        // we only need one instance even if two serial ports are used
        vesc_crc = new Crc2Byte();
        vesc_crc->init(16, 0x1021);
    }

    this->configed = true;
}

uint32_t SerialVESC::sendRCFrame(bool frameAvailable, bool frameMissed, uint32_t *channelData)
{
    if (!this->configed) {
        return DURATION_IMMEDIATELY;
    }

    // no data from transmitter, send nothing, the VESC must be configured with the right failsafe behavior
    if (!frameAvailable) {
        return DURATION_IMMEDIATELY;
    }

    for (int i = 0; i < 3; i++)
    {
        const vesc_cfg_t* pcfg = &(this->cfg[i]);

        // check if it is actually configured to do anything
        if (pcfg->channel_x == 0 || pcfg->cmd == 0) {
            continue;
        }

        vesc_i32_packet_t packet;
        packet.start_byte = 0x02;
        packet.stop_byte = 0x03;
        packet.payload_length = 5;
        packet.command_byte = pcfg->cmd;

        int32_t val = 0;
        int32_t range = this->range[i];

        // for duty cycle, limit the duty to a max of 100%, and also account for the loss of precision from user settings
        if (pcfg->cmd == COMM_SET_DUTY) {
            if (range >= DUTY_RANGE_SNAP_TO_MAX) {
                range = 100000;
            }
        }
        else if (pcfg->cmd == COMM_SET_POS) {
            // for position, because we can't represent 360 exactly, if it's close enough, snap to 360
            if (range >= POSITION_RANGE_SNAP_MIN && range <= POSITION_RANGE_SNAP_MAX) {
                range = POSITION_RANGE_SNAP_TO;
            }
        }

        if (pcfg->cmd != COMM_SET_POS || pcfg->channel_y == 0)
        {
            int32_t cval = ChannelDataMixed[pcfg->channel_x - 1];
            // we first check for specific inputs so that we are 100% certain that we are sending out a true zero, for safety reasons
            if (pcfg->cmd != COMM_SET_POS && pcfg->bidirectional && cval >= CRSF_CHANNEL_VALUE_MID - 1 && cval <= CRSF_CHANNEL_VALUE_MID + 1) {
                val = 0;
            }
            else if (pcfg->cmd != COMM_SET_POS && !pcfg->bidirectional && cval <= CRSF_CHANNEL_VALUE_MIN + 1) {
                val = 0;
            }
            // then consider the other deadzones
            else if (pcfg->cmd != COMM_SET_POS && cval >= CRSF_CHANNEL_VALUE_MAX - 1) {
                val = range;
            }
            else if (pcfg->cmd != COMM_SET_POS && pcfg->bidirectional && cval <= CRSF_CHANNEL_VALUE_MIN + 1) {
                val = -range;
            }
            else {
                // actually do calculations
                val = i32map(cval, CRSF_CHANNEL_VALUE_MIN, CRSF_CHANNEL_VALUE_MAX, pcfg->bidirectional ? -range : 0, range);
            }
        }
        else
        {
            int16_t x = ChannelDataMixed[pcfg->channel_x - 1] - CRSF_CHANNEL_VALUE_MID;
            int16_t y = ChannelDataMixed[pcfg->channel_y - 1] - CRSF_CHANNEL_VALUE_MID;
            if (xy_magnitude(x, y) <= ((CRSF_CHANNEL_VALUE_MAX - CRSF_CHANNEL_VALUE_MIN) / 4)) { // must exceed deadzone to actually be considered valid
                continue; // send nothing
            }
            val = xy_to_vesc_pos_offset(x, y, pcfg->bidirectional != 0);
        }

        packet.value = __builtin_bswap32((uint32_t)val);
        packet.crc = __builtin_bswap16(vesc_crc->calc(&(packet.command_byte), 5, 0));
        _outputPort->write((const uint8_t *)&packet, sizeof(vesc_i32_packet_t));
    }

    return DURATION_IMMEDIATELY;
}

void SerialVESC::forwardMessage(const crsf_header_t *message) {
}

void SerialVESC::processBytes(uint8_t *bytes, uint16_t size) {
}

static int32_t i32map(int32_t x, int32_t in_min, int32_t in_max, int32_t out_min, int32_t out_max)
{
    int32_t in_range = in_max - in_min;

    int32_t out_range = out_max - out_min;
    int32_t dx = x - in_min;

    // Split to avoid overflow: dx = q * in_range + r
    int32_t q = dx / in_range;
    int32_t r = dx % in_range;

    // Combine safely
    return out_min + q * out_range + (r * out_range) / in_range;
}

static int32_t decode_ufloat16(uint16_t v)
{
    const uint32_t exponent = (v >> 11) & 0x1F;
    const uint32_t mantissa = v & 0x7FF;

    if (mantissa == 0) {
        return 0;
    }

    const uint64_t decoded = (uint64_t)mantissa << exponent;
    if (decoded > VESC_RANGE_MAX) {
        return VESC_RANGE_MAX;
    }

    return (int32_t)decoded;
}

#if 0
static float xy_magnitude(int16_t x, int16_t y)
{
    return sqrtf((float)x * (float)x + (float)y * (float)y);
}
#else
static int16_t xy_magnitude(int16_t x, int16_t y)
{
    // we don't actually care about actual magnitude so don't waste float calculations
    return abs(x) + abs(y);
}
#endif

static int32_t xy_to_vesc_pos_offset(int16_t x, int16_t y, bool invert)
{
    if (x == 0 && y == 0) {
        return 0;
    }

    double angle_rad = atan2((double)y, (double)x);
    double angle_deg = angle_rad * (180.0 / M_PI);

    if (angle_deg < 0.0) {
        angle_deg += 360.0;
    }

    int32_t pos = (int32_t)(angle_deg * 1000000.0 + 0.5);

    if (invert) {
        pos = 360000000 - pos;
        if (pos == 360000000) {
            pos = 0;
        }
    }

    while (pos < 0) {
        pos += 360000000;
    }
    while (pos >= 360000000) {
        pos -= 360000000;
    }

    return pos;
}
