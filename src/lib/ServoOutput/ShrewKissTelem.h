#pragma once

#include <Arduino.h>
#include "common.h"
#include "config.h"

// this structure matches the one sent by the ESC, which uses big-endian
typedef struct
{
    uint8_t temperature;
    uint8_t voltage_h;
    uint8_t voltage_l;
    uint8_t current_h;
    uint8_t current_l;
    uint8_t consumption_h;
    uint8_t consumption_l;
    uint8_t erpm_h;
    uint8_t erpm_l;
    uint8_t crc;
}
kiss_telem_pkt_t;

// convenient little-endian format of the data, including a channel marker and a millisecond timestamp
typedef struct
{
    uint8_t ch;
    uint32_t timestamp;
    uint8_t temperature;
    uint16_t voltage;
    uint16_t current;
    uint16_t consumption;
    uint16_t erpm;
}
kiss_telem_t;

extern kiss_telem_t* kiss_telem_data[PWM_MAX_CHANNELS]; // user custom code can get data out of here
extern bool has_new_kiss_telem; // use this to see if new telemetry data has arrived (mark it false please)

void shrew_kissTelemPoll(); 
void shrew_serRxIntercept(); // this should be called in devSerialIO

void shrew_kissTelemInit(); // use this if the serial ports need to be completely initialized
void shrew_kissTelemMarkInit(); // use this if the main code already initialize the ports to the correct settings
