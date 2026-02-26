#include "led/activity_led.h"
#include "lmchan_config.h"

#include <stdint.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "esp_log.h"

static const char *TAG = "activity_led";

static led_strip_handle_t s_strip = NULL;
static TaskHandle_t s_led_task = NULL;
static volatile bool s_busy = false;
static bool s_led_cleared = false;
static uint16_t s_hue = 0;
static uint16_t s_phase = 0;
static uint16_t s_chase = 0;

static uint8_t scale8(uint8_t v, uint8_t scale)
{
    return (uint8_t)(((uint16_t)v * (uint16_t)scale) / 255U);
}

static uint8_t breathe_level(void)
{
    float x = (float)sinf((2.0f * 3.1415926f * (float)s_phase) / 256.0f);
    s_phase++;
    float n = (x + 1.0f) * 0.5f;
    float low = 0.12f;
    float high = 1.0f;
    float v = low + (high - low) * n;
    int out = (int)(v * (float)LMCHAN_LED_MAX_BRIGHTNESS);
    if (out < 1) out = 1;
    if (out > 255) out = 255;
    return (uint8_t)out;
}

static void hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint8_t region = (h / 60) % 6;
    uint8_t rem = (uint8_t)((h % 60) * 255 / 60);
    uint8_t p = (uint8_t)((uint16_t)v * (255 - s) / 255);
    uint8_t q = (uint8_t)((uint16_t)v * (255 - (uint16_t)s * rem / 255) / 255);
    uint8_t t = (uint8_t)((uint16_t)v * (255 - (uint16_t)s * (255 - rem) / 255) / 255);

    switch (region) {
        case 0: *r = v; *g = t; *b = p; break;
        case 1: *r = q; *g = v; *b = p; break;
        case 2: *r = p; *g = v; *b = t; break;
        case 3: *r = p; *g = q; *b = v; break;
        case 4: *r = t; *g = p; *b = v; break;
        default: *r = v; *g = p; *b = q; break;
    }
}

static void show_off(void)
{
    if (!s_strip) return;
    led_strip_clear(s_strip);
    s_led_cleared = true;
}

static void show_rainbow_busy(void)
{
    if (!s_strip) return;

    uint8_t amp = breathe_level();
    if (LMCHAN_LED_COUNT <= 1) {
        uint8_t r, g, b;
        hsv_to_rgb(s_hue, 255, amp, &r, &g, &b);
        led_strip_set_pixel(s_strip, 0, r, g, b);
    } else {
        for (int i = 0; i < LMCHAN_LED_COUNT; i++) {
            uint16_t local_h = (uint16_t)((s_hue + i * (360 / LMCHAN_LED_COUNT)) % 360);
            uint8_t r, g, b;
            hsv_to_rgb(local_h, 255, amp, &r, &g, &b);
            if (i != (int)s_chase) {
                r = scale8(r, 64);
                g = scale8(g, 64);
                b = scale8(b, 64);
            }
            led_strip_set_pixel(s_strip, i, r, g, b);
        }
        s_chase = (uint16_t)((s_chase + 1) % LMCHAN_LED_COUNT);
    }
    led_strip_refresh(s_strip);
    s_hue = (uint16_t)((s_hue + 3) % 360);
}

static void led_task(void *arg)
{
    (void)arg;
    while (1) {
        if (s_busy) {
            s_led_cleared = false;
            show_rainbow_busy();
            vTaskDelay(pdMS_TO_TICKS(45));
        } else {
            if (!s_led_cleared) {
                show_off();
            }
            vTaskDelay(pdMS_TO_TICKS(120));
        }
    }
}

esp_err_t activity_led_init(void)
{
#if !LMCHAN_LED_ENABLED
    ESP_LOGI(TAG, "Activity LED disabled by config");
    return ESP_OK;
#else
    led_strip_config_t strip_config = {
        .strip_gpio_num = LMCHAN_LED_GPIO,
        .max_leds = LMCHAN_LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 0,
        .flags.with_dma = false,
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (err != ESP_OK || !s_strip) {
        ESP_LOGW(TAG, "LED init failed on GPIO%d (%s), continue without LED",
                 LMCHAN_LED_GPIO, esp_err_to_name(err));
        s_strip = NULL;
        return ESP_OK;
    }

    show_off();
    if (xTaskCreatePinnedToCore(led_task, "activity_led", 4096, NULL, 2, &s_led_task, 0) != pdPASS) {
        ESP_LOGW(TAG, "LED task create failed");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Activity LED ready: gpio=%d count=%d max=%d",
             LMCHAN_LED_GPIO, LMCHAN_LED_COUNT, LMCHAN_LED_MAX_BRIGHTNESS);
    return ESP_OK;
#endif
}

void activity_led_set_busy(bool busy)
{
    s_busy = busy;
}
