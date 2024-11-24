#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/rtc_io.h>
#include <soc/rtc_cntl_reg.h>
#include <soc/soc.h>
#include <esp_intr_alloc.h>

#ifdef BUILD_SHREW_GENERAL

extern void shrew_forceAllFailsafe();

static void IRAM_ATTR brownout_handler(void *arg) {
}

void shrew_brownoutSetup() {
    // Enable brownout detector and set it to generate an interrupt
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, RTC_CNTL_BROWN_OUT_INT_ENA | RTC_CNTL_BROWN_OUT_PD_RF_ENA | (2 << RTC_CNTL_BROWN_OUT_RST_WAIT_S));

    // Attach the interrupt handler
    esp_intr_alloc(ETS_RTC_CORE_INTR_SOURCE, 0, brownout_handler, (void*)0, NULL);
}

#endif
