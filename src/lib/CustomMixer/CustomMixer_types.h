#pragma once

#include <stdint.h>
#include <stdbool.h>

#define CUSTOMMIXER_SCALE_STEP    5

typedef struct __attribute__((packed))
{
    uint8_t deadzone;     // in CRSF units
    int8_t  curve;        // -100 to 100 maps to -10 to 10 in usage
    int8_t  scale;        // scale_percent = 100 + k * stored, where k is CUSTOMMIXER_SCALE_STEP
    uint8_t antideadzone; // in CRSF units
    int8_t  offset;       // in CRSF units
} mixercurve_t;

typedef struct __attribute__((packed))
{
    // arcade tank drive mixer settings
    uint8_t ch_throttle; // 0 means unassigned, must be assigned or else mixer is not active
    uint8_t ch_steering; // 0 means unassigned, must be assigned or else mixer is not active
    uint8_t ch_left;     // 0 means unassigned
    uint8_t ch_right;    // 0 means unassigned
    mixercurve_t curve_throttle;
    mixercurve_t curve_steering;
    mixercurve_t curve_left;
    mixercurve_t curve_right;
    // end of arcade tank drive mixer settings

    // allow two auxilliary channels to have curves in-place
    uint8_t      ch_aux1; // 0 means unassigned, not used
    mixercurve_t curve_aux1;
    uint8_t      ch_aux2; // 0 means unassigned, not used
    mixercurve_t curve_aux2;

    // custom arming switch
    uint8_t ch_arm;        // arming switch, 0 means unassigned
    uint8_t arming_range;  // bit 0 means low, bit 1 means mid, bit 2 means high
} custom_mixer_t;
