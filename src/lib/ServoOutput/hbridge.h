#pragma once
#include "common.h"
#include "devServoOutput.h"

#define HBRIDGE_PWM_FREQ 24000U

void hbridge_init(void);
void hbridge_failsafe(void);
void hbridge_update(unsigned long now);

void shrew_markServosInitialized(bool);
bool shrew_isWebCtrlActive(void);
