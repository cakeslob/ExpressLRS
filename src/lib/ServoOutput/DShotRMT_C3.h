#if (defined(GPIO_PIN_PWM_OUTPUTS) && defined(PLATFORM_ESP32) && defined(PLATFORM_ESP32_C3))

#pragma once

#include "DShotRMT.h"

void dshotc3_poll(void);

class DShotRMT {
    public:
        DShotRMT(gpio_num_t gpio, rmt_channel_t rmtChannel);
        ~DShotRMT();
    
        // ...safety first ...no parameters, no DShot
        bool begin(dshot_mode_t dshot_mode = DSHOT_OFF, bool is_bidirectional = false);
        void send_dshot_value(uint16_t throttle_value, telemetric_request_t telemetric_request = NO_TELEMETRIC);
        void set_looping(bool x);
        void set_pin(void);
        inline rmt_item32_t* get_queued_item() { return (rmt_item32_t*)queued_msg; };
        inline int get_pin_num() { return gpio_num; };
        inline int get_inst_idx() { return my_idx; };
        bool new_data;

    private:
        gpio_num_t gpio_num;
        int my_idx;
        rmt_item32_t queued_msg[DSHOT_PACKET_LENGTH + 1];
        rmt_item32_t* encode_dshot_to_rmt(uint16_t parsed_packet);
};

#endif
