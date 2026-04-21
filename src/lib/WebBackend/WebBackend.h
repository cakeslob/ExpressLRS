#pragma once

#include "common.h"
#include <stdint.h>

#include <ESPAsyncWebServer.h>

void webbe_tick();
void webbe_install(AsyncWebServer* srv);
