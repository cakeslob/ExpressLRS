#pragma once

#include <Arduino.h>

#include <stdint.h>
#include <stdbool.h>

#include "common.h"

#if defined(PLATFORM_ESP32)

void shrewdevhook_preSetup(void);
void shrewdevhook_postSetup(void);

void shrewdevhook_postServoStart(void);
void shrewdevhook_onNewData(void);
void shrewdevhook_tick(uint32_t now, bool new_data_avail);
void shrewdevhook_preServoUpdate(uint32_t now);
void shrewdevhook_postServoUpdate(void);
void shrewdevhook_postServoFailsafe(void);

#include "esp32rgb.h"

extern void WS281BsetLED(int index, uint32_t color);
extern void WS281BsetLED(uint32_t color);
extern void WS281BshowLEDs(void);
extern uint32_t HsvToRgb(const blinkyColor_t &blinkyColor);

bool shrewdevhook_onLedTick(void);
uint32_t shrewdevhook_getLedTickPeriod(void);
uint8_t shrewdevhook_onLedBootStatusTick(void);
int16_t shrewdevhook_getPixelCount(void);

#endif // PLATFORM_ESP32
