#pragma once
#if defined(TARGET_RX)

#include "device.h"
#include "common.h"

#if defined(PLATFORM_ESP32)
#include "DShotRMT.h"
#endif

extern device_t ServoOut_device;
extern bool servos_movedBlinkLed;
extern bool servo_initialized;

// Notify this unit that new channel data has arrived
void servoNewChannelsAvailable();
void servosUpdate(unsigned long now);
void servosFailsafe(bool no_pulse);
void servo_initializeEnable();
// Copy the current output values to the config's failsafe values
void servoCurrentToFailsafeConfig();

#endif
