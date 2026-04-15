#pragma once

#include "common.h"
#include <stdint.h>

#include <ESPAsyncWebServer.h>

void am32_setupServer(AsyncWebServer* srv);
void am32_tick(void);
