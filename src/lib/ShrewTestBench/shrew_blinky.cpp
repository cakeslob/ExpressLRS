#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "driver/i2s.h"
#include "esp_sleep.h"
#include "nvs_flash.h"

#include <Arduino.h>

#define I2S_NUM i2s_port_t(0)

#if defined(CONFIG_IDF_TARGET_ESP32S2)
#define SAMPLE_RATE (360000)
#define MCLK 48000000
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define SAMPLE_RATE (800000)
#define MCLK 160000000
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
#define SAMPLE_RATE (800000)
#define MCLK 160000000
#elif defined(CONFIG_IDF_TARGET_ESP32)
#define SAMPLE_RATE (360000)
#define MCLK 48000000
#endif

#if defined(CONFIG_IDF_TARGET_ESP32S2)
static const int bitorder[] = {0x40, 0x80, 0x10, 0x20, 0x04, 0x08, 0x01, 0x02};
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
static const int bitorder[] = {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
static const int bitorder[] = {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};
#elif defined(CONFIG_IDF_TARGET_ESP32)
static const int bitorder[] = {0x40, 0x80, 0x10, 0x20, 0x04, 0x08, 0x01, 0x02};
#endif

static size_t out_buffer_size;
static uint16_t *out_buffer = nullptr;

//static QueueHandle_t i2s_queue = NULL;

void shrew_blinky_init(int gpio_pin, int num_leds)
{
    out_buffer_size = num_leds * 24 * sizeof(uint16_t);
    out_buffer = (uint16_t *)heap_caps_malloc(out_buffer_size, MALLOC_CAP_8BIT);

    i2s_config_t i2s_config = {
        .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = 0,
        .dma_buf_count = 4,
        .use_apll = true,
        .tx_desc_auto_clear = true,
        .fixed_mclk = MCLK,
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = -1,
        .ws_io_num = -1,
        .data_out_num = gpio_pin,
        .data_in_num = -1,
    };

    //i2s_queue = xQueueCreate(10, sizeof(i2s_event_t));
    //i2s_queue = xQueueGenericCreate(10, sizeof(i2s_event_t), queueQUEUE_TYPE_BASE);

    i2s_config.dma_buf_len = out_buffer_size;
    //i2s_driver_install(I2S_NUM, &i2s_config, 0, &i2s_queue);
    i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
    delay(1); // without this it fails to boot and gets stuck!
    i2s_set_pin(I2S_NUM, &pin_config);
    i2s_zero_dma_buffer(I2S_NUM);
    i2s_stop(I2S_NUM);
}

void shrew_blinky_once(uint8_t r, uint8_t g, uint8_t b, uint32_t dly)
{
    int loc = 0;
    for(int bitpos = 0 ; bitpos < 8 ; bitpos++)
    {
        int bit = bitorder[bitpos];
        out_buffer[loc + bitpos + 0]  = (g & bit) ? 0xFFE0 : 0xF000;
        out_buffer[loc + bitpos + 8]  = (b & bit) ? 0xFFE0 : 0xF000;
        out_buffer[loc + bitpos + 16] = (r & bit) ? 0xFFE0 : 0xF000;
    }

    //xQueueReset(&i2s_queue);
    size_t bytes_written = 0;
    i2s_write(I2S_NUM, out_buffer, out_buffer_size, &bytes_written, portMAX_DELAY);
    i2s_start(I2S_NUM);
    delay(1);
    dly--;
    esp_sleep_enable_timer_wakeup(dly * 1000);
    esp_light_sleep_start();
    //i2s_event_t evt;
    //if (xQueueReceive(i2s_queue, &evt, portMAX_DELAY) == pdPASS) {
    //    if (evt.type == I2S_EVENT_TX_DONE) {
    //        esp_sleep_enable_timer_wakeup(dly * 1000);
    //        esp_light_sleep_start();
    //    }
    //}
}
