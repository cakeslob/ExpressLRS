#pragma once

#include "common.h"
#include <stdint.h>

#include <ESPAsyncWebServer.h>

void shrew_startWifi();
void shrew_setupServer(AsyncWebServer* srv);
void shrew_handleWebUpdate(uint32_t now);
uint32_t shrew_getLastDataTime();
void shrew_markServosInitialized(bool);
bool shrew_isWebCtrlActive();
bool shrew_hasWifiStarted();
void shrew_restartRadio();
