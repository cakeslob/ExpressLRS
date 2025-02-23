#include "ShrewKissTelem.h"

#if defined(PLATFORM_ESP32)
#include "DShotRMT.h"
#endif

static bool has_init = false;

#if defined(PLATFORM_ESP32)
extern DShotRMT *dshotInstances[PWM_MAX_CHANNELS]; // null if non-existant
#endif

#define KISS_TELEM_RX_BUFF_SIZE 16
static unsigned char rx_buff_0[KISS_TELEM_RX_BUFF_SIZE];
static uint8_t rx_buff_0_idx = 0;
#if defined(PLATFORM_ESP32)
static unsigned char rx_buff_1[KISS_TELEM_RX_BUFF_SIZE];
static uint8_t rx_buff_1_idx = 0;
#endif

esc_telem_t* esc_telem_data[PWM_MAX_CHANNELS] = {nullptr};

static int inst_idx = 0;
static uint32_t last_time = 0;
bool has_new_kiss_telem = false;

static void read_ser(HardwareSerial* port, uint32_t timenow, uint8_t* buffer, uint8_t* buffer_idx);
static bool check_pkt(uint8_t* buffer, uint8_t* buffer_idx, uint32_t timenow);
static bool check_crc(uint8_t* buf);

void shrew_serRxIntercept() // TODO: move this to the dev hooks
{
    shrew_kissTelemPoll();
}

void shrew_kissTelemPoll()
{
    if (has_init == false) { // don't screw with the serial ports if KISS telemetry is not being used
        return;
    }

    uint32_t now = millis();

    bool has_new = false;

    if (config.GetSerialProtocol() == PROTOCOL_KISSTELEM) {
        read_ser(&Serial, now, (uint8_t*)rx_buff_0, &rx_buff_0_idx);
        has_new |= check_pkt((uint8_t*)rx_buff_0, &rx_buff_0_idx, now);
    }
    #if defined(PLATFORM_ESP32)
    if (config.GetSerial1Protocol() == PROTOCOL_SERIAL1_KISSTELEM) {
        read_ser(&Serial1, now, (uint8_t*)rx_buff_1, &rx_buff_1_idx);
        has_new |= check_pkt((uint8_t*)rx_buff_1, &rx_buff_1_idx, now);
    }
    #endif

    has_new_kiss_telem |= has_new;

    if ((now - last_time) < 50 || has_new == false) {
        return;
    }
    last_time = now;
    rx_buff_0_idx = 0; // the buffer is reset just in case we get stuck with half of a packet in the buffer, it should not take 50ms to transmit 10 bytes
    #if defined(PLATFORM_ESP32)
    rx_buff_1_idx = 0;
    // if enough time has passed, or if new data received, we can cycle to the next Dshot output (if existing)
    DShotRMT* inst = nullptr;
    for (int i = 0; i < PWM_MAX_CHANNELS + 4; i++) {
        inst_idx++;
        inst_idx %= PWM_MAX_CHANNELS;
        inst = dshotInstances[inst_idx];
        if (inst != NULL) { // instance available to use
            break;
        }
    }
    if (inst == NULL) { // quit if no instance of Dshot available
        inst_idx = 0; // this makes is so that all telemetry arrives to index 0
        return;
    }
    inst->set_telem_on_next();
    #endif
}

void shrew_kissTelemInit()
{
    if (has_init) {
        return;
    }
    if (config.GetSerialProtocol() == PROTOCOL_KISSTELEM
        #if defined(PLATFORM_ESP32)
        || config.GetSerial1Protocol() == PROTOCOL_SERIAL1_KISSTELEM
        #endif
    ) {
        if (config.GetSerialProtocol() == PROTOCOL_KISSTELEM) {
            Serial.begin(115200, SERIAL_8N1, GPIO_PIN_RCSIGNAL_RX, -1, false);
        }
        #if defined(PLATFORM_ESP32)
        if (config.GetSerial1Protocol() == PROTOCOL_SERIAL1_KISSTELEM) {
            Serial1.begin(115200, SERIAL_8N1, GPIO_PIN_SERIAL1_RX, -1, false);
        }
        #endif
        has_init = true;
    }
}

void shrew_kissTelemMarkInit() {
    has_init = true;
}

static uint8_t update_crc8(uint8_t crc, uint8_t crc_seed)
{
    uint8_t crc_u, i;
    crc_u = crc;
    crc_u ^= crc_seed;
    for (i = 0; i < 8; i++)
        crc_u = (crc_u & 0x80) ? 0x7 ^ (crc_u << 1) : (crc_u << 1);
    return (crc_u);
}

static uint8_t get_crc8(uint8_t* buf, uint8_t blen)
{
    uint8_t crc = 0, i;
    for (i = 0; i < blen; i++) {
        crc = update_crc8(buf[i], crc);
    }
    return (crc);
}

static bool check_crc(uint8_t* buf)
{
    uint8_t calced = get_crc8(buf, sizeof(kiss_telem_pkt_t) - 1);
    kiss_telem_pkt_t* p = (kiss_telem_pkt_t*)buf;
    return (p->crc == calced);
}

static void read_ser(HardwareSerial* port, uint32_t timenow, uint8_t* buffer, uint8_t* buffer_idx)
{
    while (port->available()) {
        last_time = timenow;
        unsigned char c = port->read();
        if ((*buffer_idx) < KISS_TELEM_RX_BUFF_SIZE) {
            buffer[(*buffer_idx)] = c;
            (*buffer_idx)++;
        }
    }
}

static bool check_pkt(uint8_t* buffer, uint8_t* buffer_idx, uint32_t timenow)
{
    if ((*buffer_idx) >= sizeof(kiss_telem_pkt_t)) { // enough data
        (*buffer_idx) = 0; // the buffer will not be reused later even if CRC fails
        if (check_crc((uint8_t*)buffer)) { // check if CRC is valid
            kiss_telem_pkt_t* ptr = (kiss_telem_pkt_t*)buffer;
            int i = inst_idx;

            // I put in a special backdoor method of packing a ESC identifier into AM32's KISS telemetry output
            // it uses the highest 4 bits of the current value
            // this limits usage to under 409.5A of current
            if ((ptr->current_h & 0xF0) != 0) {
                i = ((ptr->current_h & 0xF0) >> 4);
                ptr->current_h &= 0x0F;
            }

            // allocate memory for data if not already existing
            if (esc_telem_data[i] == nullptr) {
                esc_telem_data[i] = (esc_telem_t*)malloc(sizeof(esc_telem_t));
            }

            // repackage the data in a convenient format
            esc_telem_t* dst = esc_telem_data[i];
            dst->ch = i;
            dst->timestamp = timenow;
            dst->voltage = (ptr->voltage_h << 8) + ptr->voltage_l;
            dst->current = (ptr->current_h << 8) + ptr->current_l;
            dst->consumption = (ptr->consumption_h << 8) + ptr->consumption_l;
            dst->erpm = (ptr->erpm_h << 8) + ptr->erpm_l;
            return true;
        }
    }
    return false;
}
