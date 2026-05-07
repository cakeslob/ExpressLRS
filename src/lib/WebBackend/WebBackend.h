#pragma once

#include "common.h"
#include <stdint.h>

#include <ESPAsyncWebServer.h>

extern bool webbe_installed;
extern bool webbe_ws_started;

void webbe_tick();
void webbe_install(AsyncWebServer* srv);
uint8_t webbe_getRandomWifiChannel();
