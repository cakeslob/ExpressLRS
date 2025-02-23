#include "devAnalogVbat.h"

#if defined(USE_ANALOG_VBAT)
#include <Arduino.h>
#include "CRSF.h"
#include "telemetry.h"
#include "median.h"
#include "logging.h"

// Sample 5x samples over 500ms (unless SlowUpdate)
#define VBAT_SMOOTH_CNT         5
#if defined(DEBUG_VBAT_ADC)
#define VBAT_SAMPLE_INTERVAL    20U // faster updates in debug mode
#else
#define VBAT_SAMPLE_INTERVAL    100U
#endif

typedef uint16_t vbatAnalogStorage_t;
static MedianAvgFilter<vbatAnalogStorage_t, VBAT_SMOOTH_CNT>vbatSmooth;
static uint8_t vbatUpdateScale;

#if defined(PLATFORM_ESP32)
#include "esp_adc_cal.h"
static esp_adc_cal_characteristics_t *vbatAdcUnitCharacterics;
#endif

#ifdef BUILD_SHREW_ADCLUT
int32_t shrewvbat_get(uint32_t x);
static bool use_lut = false;
#endif

/* Shameful externs */
extern Telemetry telemetry;

extern void blackbox_log_vbat(int32_t x);

/**
 * @brief: Enable SlowUpdate mode to reduce the frequency Vbat telemetry is sent
 ***/
void Vbat_enableSlowUpdate(bool enable)
{
    vbatUpdateScale = enable ? 2 : 1;
}

static int start()
{
    if (GPIO_ANALOG_VBAT == UNDEF_PIN)
    {
        return DURATION_NEVER;
    }
    vbatUpdateScale = 1;
#if defined(PLATFORM_ESP32)
    analogReadResolution(12);

    int atten = hardware_int(HARDWARE_vbat_atten);
    if (atten != -1)
    {
        // if the configured value is higher than the max item (11dB, it indicates to use cal_characterize)
        bool useCal = atten > ADC_11db;
        if (useCal)
        {
            #ifndef BUILD_SHREW_ADCLUT
            atten -= (ADC_11db + 1);
            #else
            if (atten > 7) {
                use_lut = true;
                atten %= (ADC_11db + 1);
            }
            #endif

            vbatAdcUnitCharacterics = new esp_adc_cal_characteristics_t();
            int8_t channel = digitalPinToAnalogChannel(GPIO_ANALOG_VBAT);
            adc_unit_t unit = (channel > (SOC_ADC_MAX_CHANNEL_NUM - 1)) ? ADC_UNIT_2 : ADC_UNIT_1;
            esp_adc_cal_characterize(unit, (adc_atten_t)atten, ADC_WIDTH_BIT_12, 3300, vbatAdcUnitCharacterics);
        }
        analogSetPinAttenuation(GPIO_ANALOG_VBAT, (adc_attenuation_t)atten);
    }
#endif

    return VBAT_SAMPLE_INTERVAL;
}

static int32_t calcVbat()
{
    uint32_t adc = vbatSmooth.calc();
#if defined(PLATFORM_ESP32) && !defined(DEBUG_VBAT_ADC)
    if (vbatAdcUnitCharacterics)
        adc = esp_adc_cal_raw_to_voltage(adc, vbatAdcUnitCharacterics);
#endif

    int32_t vbat;

#ifdef BUILD_SHREW_ADCLUT
    if (use_lut == false)
#endif
    {
    // For negative offsets, anything between abs(OFFSET) and 0 is considered 0
    if (ANALOG_VBAT_OFFSET < 0 && adc <= -ANALOG_VBAT_OFFSET)
        vbat = 0;
    else
        vbat = ((int32_t)adc - ANALOG_VBAT_OFFSET) * 100 / ANALOG_VBAT_SCALE;
    }
#ifdef BUILD_SHREW_ADCLUT
    else
    {
        vbat = shrewvbat_get(adc);
    }
#endif
    blackbox_log_vbat(vbat);
    return vbat;
}

static void reportVbat()
{
    int32_t vbat = calcVbat();

    CRSF_MK_FRAME_T(crsf_sensor_battery_t) crsfbatt = { 0 };
    // Values are MSB first (BigEndian)
    crsfbatt.p.voltage = htobe16((uint16_t)vbat);
    // No sensors for current, capacity, or remaining available

    CRSF::SetHeaderAndCrc((uint8_t *)&crsfbatt, CRSF_FRAMETYPE_BATTERY_SENSOR, CRSF_FRAME_SIZE(sizeof(crsf_sensor_battery_t)), CRSF_ADDRESS_CRSF_TRANSMITTER);
    telemetry.AppendTelemetryPackage((uint8_t *)&crsfbatt);
}

static int timeout()
{
    if (GPIO_ANALOG_VBAT == UNDEF_PIN || telemetry.GetCrsfBatterySensorDetected())
    {
        return DURATION_NEVER;
    }

    #if defined(PLATFORM_ESP32)
    if (connectionState == wifiUpdate)
    {
        // on ESP32, ADC2 cannot be used when Wi-Fi is active
        int8_t channel = digitalPinToAnalogChannel(GPIO_ANALOG_VBAT);
        adc_unit_t unit = (channel > (SOC_ADC_MAX_CHANNEL_NUM - 1)) ? ADC_UNIT_2 : ADC_UNIT_1;
        if (unit == ADC_UNIT_2) {
            return DURATION_NEVER;
        }
    }
    #endif

    uint32_t adc = analogRead(GPIO_ANALOG_VBAT);
#if defined(PLATFORM_ESP32) && defined(DEBUG_VBAT_ADC)
    // When doing DEBUG_VBAT_ADC, every value is adjusted (for logging)
    // in normal mode only the final value is adjusted to save CPU cycles
    if (vbatAdcUnitCharacterics)
        adc = esp_adc_cal_raw_to_voltage(adc, vbatAdcUnitCharacterics);
    DBGLN("$ADC,%u,%d", adc, calcVbat());
#endif

    unsigned int idx = vbatSmooth.add(adc);
    if (idx == 0 && connectionState == connected) {
        reportVbat();
    }
    else {
        calcVbat();
    }

    return VBAT_SAMPLE_INTERVAL * vbatUpdateScale;
}

device_t AnalogVbat_device = {
    .initialize = nullptr,
    .start = start,
    .event = nullptr,
    .timeout = timeout,
};

#endif /* if USE_ANALOG_VCC */