#ifdef BUILD_SHREW_GENERAL

#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/rtc_io.h>
#include <soc/rtc_cntl_reg.h>
#include <soc/soc.h>
#include <esp_intr_alloc.h>

#include "common.h"

extern void shrew_forceAllFailsafe();

volatile uint32_t shrew_hasBrownedOut;
static volatile uint8_t shrew_brownoutGoodCnt = 0;

static volatile RTC_DATA_ATTR uint32_t shrew_brownout_indicator = 0; // use RTC to indicate if a reset was from a brownout and not software triggered
static uint32_t shrew_reset_reason = 0xDEADBEEF;

static void IRAM_ATTR brownout_handler(void *arg) {
    shrew_hasBrownedOut = millis();
    shrew_brownoutGoodCnt = 0;
}

void shrew_brownoutSetup() {
    #ifdef BUILD_SHREW_BROWNOUT_DISABLE
    // Enable brownout detector and set it to generate an interrupt
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, RTC_CNTL_BROWN_OUT_INT_ENA | RTC_CNTL_BROWN_OUT_PD_RF_ENA | (2 << RTC_CNTL_BROWN_OUT_RST_WAIT_S));

    // Attach the interrupt handler
    esp_intr_alloc(ETS_RTC_CORE_INTR_SOURCE, 0, brownout_handler, (void*)0, NULL);
    #endif

    shrew_hasBrownedOut = 0;
}

void shrewbo_onDataHook() {
    if (shrew_hasBrownedOut > 1) {
        // if we get a few good packets then we can say the operation is sketchy but good enough
        shrew_brownoutGoodCnt++;
        if (shrew_brownoutGoodCnt > 3) {
            shrew_hasBrownedOut = 1;
            shrew_brownoutGoodCnt = 0;
        }
    }
    
}

void shrew_brownoutReset() {
    // reset but let the next session know that it was a brownout instead of software-triggered
    shrew_brownout_indicator = 0xDEADBEEF;
    esp_restart();
}

void shrewbo_onDisconnectHook() {
    if (shrew_hasBrownedOut > 0) {
        // if we disconnect then we assume the radio failed and we reset
        shrew_brownoutReset();
    }
}

esp_reset_reason_t shrew_reset_get_reason() {
    if (shrew_reset_reason != 0xDEADBEEF) {
        return (esp_reset_reason_t)shrew_reset_reason; // reason is cached, allows us to clear the indicator variable
    }
    auto real_reason = esp_reset_reason();
    shrew_reset_reason = real_reason;
    if (real_reason == ESP_RST_SW) {
        if (shrew_brownout_indicator == 0xDEADBEEF) {
            shrew_reset_reason = ESP_RST_BROWNOUT;
            shrew_brownout_indicator = 0;
        }
    }
    return (esp_reset_reason_t)shrew_reset_reason;
}

#endif
