#pragma once
#include "common.h"
#include "devServoOutput.h"

void hbridge_init(void);
void hbridge_failsafe(void);
void hbridge_update(unsigned long now);
