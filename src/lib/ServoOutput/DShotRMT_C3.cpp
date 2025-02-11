#if (defined(GPIO_PIN_PWM_OUTPUTS) && defined(PLATFORM_ESP32) && defined(PLATFORM_ESP32_C3))

#include "config.h"
#include "logging.h"
#include "DShotRMT.h"

#define DSHOTC3_WAIT_US 200

static rmt_channel_t rmt_channel;

static bool dshotc3_has_inited = false;
static bool dshotc3_has_deinited = false;

static dshot_mode_t mode;
static rmt_config_t rmt_cfg_cache;

static uint16_t ticks_zero_high = 0;
static uint16_t ticks_zero_low = 0;
static uint16_t ticks_one_high = 0;
static uint16_t ticks_one_low = 0;
static uint16_t ticks_per_bit;

static DShotRMT* instances[PWM_MAX_CHANNELS];
static int inst_cnt = 0;
static int round_robin_idx = 0;
static int prev_pin = -1;

uint16_t dshotc3_calc_dshot_chksum(const dshot_packet_t& dshot_packet);
uint16_t dshotc3_prepare_rmt_data(const dshot_packet_t& dshot_packet);
static void dshotc3_output_rmt_data(int inst_idx);

bool dshotc3_init(dshot_mode_t dshot_mode, int pin_num)
{
    if (dshotc3_has_inited) {
        return true;
    }

    mode = dshot_mode;

    switch (mode) {
        case DSHOT150:
            ticks_per_bit = 64; // ...Bit Period Time 6.67 us
            ticks_zero_high = 24; // ...zero time 2.50 us
            ticks_one_high = 48; // ...one time 5.00 us
            break;

        case DSHOT300:
            ticks_per_bit = 32; // ...Bit Period Time 3.33 us
            ticks_zero_high = 12; // ...zero time 1.25 us
            ticks_one_high = 24; // ...one time 2.50 us
            break;

        case DSHOT600:
            ticks_per_bit = 16; // ...Bit Period Time 1.67 us
            ticks_zero_high = 6; // ...zero time 0.625 us
            ticks_one_high = 12; // ...one time 1.25 us
            break;

        case DSHOT1200:
            ticks_per_bit = 8; // ...Bit Period Time 0.83 us
            ticks_zero_high = 3; // ...zero time 0.313 us
            ticks_one_high = 6; // ...one time 0.625 us
            break;

        // ...because having a default is "good style"
        default:
            ticks_per_bit = 0; // ...Bit Period Time endless
            ticks_zero_high = 0; // ...no bits, no time
            ticks_one_high = 0; // ......no bits, no time
            break;
    }

    // ...calc low signal timing
    ticks_zero_low = (ticks_per_bit - ticks_zero_high);
    ticks_one_low = (ticks_per_bit - ticks_one_high);

    rmt_config_t dshot_tx_rmt_config = {
        .rmt_mode = RMT_MODE_TX,
        .channel = rmt_channel,
        .gpio_num = (gpio_num_t)pin_num,
        .clk_div = DSHOT_CLK_DIVIDER,
        .mem_block_num = uint8_t(RMT_CHANNEL_MAX - uint8_t(rmt_channel)),
        .tx_config = {
            .idle_level = RMT_IDLE_LEVEL_LOW,
            .carrier_en = false,
            .loop_en = false,
            .idle_output_en = true,
        },
    };
    memcpy(&rmt_cfg_cache, &dshot_tx_rmt_config, sizeof(rmt_config_t));

    // ...setup selected dshot mode
    rmt_config(&dshot_tx_rmt_config);
    // ...essential step, return the result
    int ret = rmt_driver_install(dshot_tx_rmt_config.channel, 0, 0);

    if (ret == ESP_OK) {
        #ifdef DSHOTC3_DEBUG_INIT
        DBGLN("dshotc3 init %d", rmt_channel);
        #endif
        dshotc3_has_inited = true;
        dshotc3_has_deinited = false;
    }
    else {
        DBGLN("dshotc3 init failed ch %d ret %d", rmt_channel, ret);
    }

    return (ret == ESP_OK);
}

void dshotc3_deinit()
{
    if (dshotc3_has_deinited) {
        return;
    }
    #ifdef DSHOTC3_DEBUG_INIT
    DBGLN("dshotc3_deinit");
    #endif
    round_robin_idx = 0;
    rmt_tx_stop(rmt_channel);
    rmt_driver_uninstall(rmt_channel);
    dshotc3_has_deinited = true;
    dshotc3_has_inited = false;
}

DShotRMT::DShotRMT(gpio_num_t gpio, rmt_channel_t rmtChannel) : gpio_num(gpio), my_idx(rmtChannel) {
    // ...create clean packet
    encode_dshot_to_rmt(DSHOT_NULL_PACKET);
}

DShotRMT::~DShotRMT() {
    dshotc3_deinit();
}

void DShotRMT::set_pin() {
    #ifdef DSHOTC3_DEBUG_PINSWITCH
    DBGLN("dshotc3 set_pin c %d p %d", rmt_channel, gpio_num);
    #endif
    rmt_cfg_cache.gpio_num = gpio_num;
    rmt_tx_stop(rmt_channel);
    if (prev_pin >= 0) {
        pinMode(prev_pin, INPUT);
        digitalWrite(prev_pin, LOW);
        pinMode(prev_pin, OUTPUT);
        // this code looks weird, but it's because calling rmt_set_pin seems to add the GPIO to a list of pins connected to the RMT
        // so multiple pins are simultaneously outputting RMT data
        // I've discovered by setting the pin to input and then output again, the pin's mapping resets as expected
    }
    pinMode(gpio_num, OUTPUT);
    rmt_set_pin(rmt_channel, RMT_MODE_TX, gpio_num);
    prev_pin = gpio_num;
}

bool DShotRMT::begin(dshot_mode_t dshot_mode, bool is_bidirectional) {
    instances[my_idx] = this;
    if (my_idx >= inst_cnt) {
        inst_cnt = my_idx + 1;
        #ifdef DSHOTC3_DEBUG_INIT
        DBGLN("dshotc3 new inst %d", inst_cnt);
        #endif
    }
    if (dshotc3_has_inited == false) {
        rmt_channel = (rmt_channel_t)my_idx;
        #ifdef DSHOTC3_DEBUG_INIT
        DBGLN("dshotc3 new inst %d init ch %d", inst_cnt, rmt_channel);
        #endif
        return dshotc3_init(dshot_mode, gpio_num);
    }
    return true;
}

void DShotRMT::set_looping(bool x) {
    rmt_cfg_cache.tx_config.loop_en = x;
    rmt_cfg_cache.gpio_num = gpio_num;
    rmt_config(&rmt_cfg_cache);
}

// ...the config part is done, now the calculating and sending part
void DShotRMT::send_dshot_value(uint16_t throttle_value, telemetric_request_t telemetric_request) {
    dshot_packet_t dshot_rmt_packet = { };

    // ...packets are the same for bidirectional mode
    dshot_rmt_packet.throttle_value = throttle_value;
    dshot_rmt_packet.telemetric_request = telemetric_request;
    dshot_rmt_packet.checksum = dshotc3_calc_dshot_chksum(dshot_rmt_packet);

    encode_dshot_to_rmt(dshotc3_prepare_rmt_data(dshot_rmt_packet));
    new_data = true;
    dshotc3_poll();
}

rmt_item32_t* DShotRMT::encode_dshot_to_rmt(uint16_t parsed_packet) {
    rmt_item32_t* dshot_tx_rmt_item = this->get_queued_item();

    // ...pause "bit" added to each frame
    dshot_tx_rmt_item[DSHOT_PAUSE_BIT].level0 = LOW;
    dshot_tx_rmt_item[DSHOT_PAUSE_BIT].level1 = LOW;

    dshot_tx_rmt_item[DSHOT_PAUSE_BIT].duration1 = 0;
    dshot_tx_rmt_item[DSHOT_PAUSE_BIT].duration0 = 10000 - (16*ticks_per_bit) - 1;

    // setup the RMT end marker
    dshot_tx_rmt_item[DSHOT_PACKET_LENGTH-1].duration0 = 0;
    dshot_tx_rmt_item[DSHOT_PACKET_LENGTH-1].level0 = HIGH;
    dshot_tx_rmt_item[DSHOT_PACKET_LENGTH-1].duration1 = 0;
    dshot_tx_rmt_item[DSHOT_PACKET_LENGTH-1].level1 = LOW;

    for (int i = 0; i < DSHOT_PAUSE_BIT; i++, parsed_packet <<= 1) 	{
        if (parsed_packet & 0b1000000000000000) {
            // set one
            dshot_tx_rmt_item[i].duration0 = ticks_one_high;
            dshot_tx_rmt_item[i].duration1 = ticks_one_low;
        }
        else {
            // set zero
            dshot_tx_rmt_item[i].duration0 = ticks_zero_high;
            dshot_tx_rmt_item[i].duration1 = ticks_zero_low;
        }

        dshot_tx_rmt_item[i].level0 = HIGH;
        dshot_tx_rmt_item[i].level1 = LOW;
    }

    return dshot_tx_rmt_item;
}

// ...just returns the checksum
// DOES NOT APPEND CHECKSUM!!!
uint16_t dshotc3_calc_dshot_chksum(const dshot_packet_t& dshot_packet) {
    // ...same initial 12bit data for bidirectional or "normal" mode
    uint16_t packet = (dshot_packet.throttle_value << 1) | dshot_packet.telemetric_request;
    // ...calc the checksum "normal" mode
    return (packet ^ (packet >> 4) ^ (packet >> 8)) & 0x0F;
}

uint16_t dshotc3_prepare_rmt_data(const dshot_packet_t& dshot_packet) {
    auto chksum = dshotc3_calc_dshot_chksum(dshot_packet);

    // ..."construct" the packet
    uint16_t prepared_to_encode = (dshot_packet.throttle_value << 1) | dshot_packet.telemetric_request;
    prepared_to_encode = (prepared_to_encode << 4) | chksum;

    return prepared_to_encode;
}

// ...finally output using ESP32 RMT
static void dshotc3_output_rmt_data(int inst_idx) {
    if (instances[inst_idx] == NULL) {
        return;
    }
    if (instances[inst_idx]->new_data) {
        #ifdef DSHOTC3_DEBUG_OUTPUT
        DBGLN("dshotc3 out %d", inst_idx);
        #endif
        instances[inst_idx]->new_data = false;
        instances[inst_idx]->set_pin();
        rmt_fill_tx_items(rmt_channel, instances[inst_idx]->get_queued_item(), DSHOT_PACKET_LENGTH, 0);
        rmt_tx_start(rmt_channel, true);
    }
}

void dshotc3_poll(void) {
    #define DSHOTC3_DEBUG_POLL
    #ifdef DSHOTC3_DEBUG_POLL
    static uint32_t no_init = 0;
    static uint32_t status_failed = 0;
    static uint32_t status_no_init = 0;
    static uint32_t status_idle = 0;
    static uint32_t status_busy = 0;
    static uint32_t status_else_cnt = 0;
    static uint32_t status_else = 0;
    static uint32_t last_time = 0;
    if ((millis() - last_time) >= 500) {
        last_time = millis();
        DBGLN("dshotc3 dbg %d %d %d %d %d %d %d", no_init, status_failed, status_no_init, status_idle, status_busy, status_else_cnt, status_else);
    }
    #endif
    if (dshotc3_has_inited == false) {
        #ifdef DSHOTC3_DEBUG_POLL
        no_init++;
        #endif
        return;
    }
    rmt_channel_status_result_t status;
    if (rmt_get_channel_status(&status) == ESP_OK) {
        if (status.status[rmt_channel] == RMT_CHANNEL_IDLE) {
            #if defined(DSHOTC3_WAIT_US)
            #if DSHOTC3_WAIT_US > 0
            // without an actual pause, the dshot packet appears cut off
            static uint32_t last_tx_time = 0;
            uint32_t now_us = micros();
            if ((now_us - last_tx_time) < DSHOTC3_WAIT_US) {
                return;
            }
            last_tx_time = now_us;
            #endif
            #endif
            dshotc3_output_rmt_data(round_robin_idx);
            round_robin_idx += 1;
            round_robin_idx %= inst_cnt;
            #ifdef DSHOTC3_DEBUG_POLL
            status_idle++;
            #endif
        }
        #ifdef DSHOTC3_DEBUG_POLL
        else {
            if (status.status[rmt_channel] == RMT_CHANNEL_UNINIT) {
                status_no_init++;
            }
            else if (status.status[rmt_channel] == RMT_CHANNEL_BUSY) {
                status_busy++;
            }
            else {
                status_else_cnt++;
                status_else = status.status[rmt_channel];
            }
        }
        #endif
    }
    #ifdef DSHOTC3_DEBUG_POLL
    else {
        status_failed++;
    }
    #endif
}

#endif
