#pragma once

#include "common.h"
#include <stdint.h>

#include <ESPAsyncWebServer.h>

#if defined(PLATFORM_ESP32) || defined(PLATFORM_ESP32_C3)
#define BUILD_AM32CONFIG
#endif

void am32_setupServer(AsyncWebServer* srv);
void am32_tick(void);
