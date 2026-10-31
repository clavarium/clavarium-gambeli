// The ring of 7 RGB LEDs: one pattern at a time, animated at 25 frames a second.
#include "box.h"
#include "led_strip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

#define LED_PIN 38
#define LED_COUNT 7

static led_strip_handle_t strip;
static volatile led_pattern_t pattern = LED_OFF;
static volatile uint32_t color;
static volatile uint32_t since;   // tick when the pattern was set

static void put(int i, uint32_t rgb, float k)
{
    led_strip_set_pixel(strip, i, ((rgb >> 16) & 255) * k, ((rgb >> 8) & 255) * k, (rgb & 255) * k);
}

static void animate(void *arg)
{
    for (;;) {
        float t = (xTaskGetTickCount() - since) * portTICK_PERIOD_MS / 1000.0f;
        uint32_t c = color;
        for (int i = 0; i < LED_COUNT; i++) {
            float k = 0;
            switch (pattern) {
            case LED_OFF: k = 0; break;
            case LED_SOLID: k = 0.5f; break;
            case LED_PULSE: k = 0.05f + 0.45f * (0.5f + 0.5f * sinf(t * 2.1f)); break;            // slow
            case LED_BREATHE: k = 0.1f + 0.35f * (0.5f + 0.5f * sinf(t * 4.0f)); break;
            case LED_SPIN: {
                float head = fmodf(t * 10.0f, LED_COUNT);
                float d = fmodf(head - i + LED_COUNT, LED_COUNT);
                k = d < 3 ? 0.5f * (1 - d / 3) : 0;
                break;
            }
            case LED_BLINK2: k = (t < 1.2f && fmodf(t, 0.6f) < 0.3f) ? 0.6f : 0; break;
            case LED_DOT: k = i == 0 ? 0.25f : 0; break;
            case LED_OFFLINE: k = fmodf(t, 5.0f) < 0.25f ? 0.4f : 0; break;
            }
            put(i, c, k);
        }
        led_strip_refresh(strip);
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}

void leds_init(void)
{
    led_strip_config_t cfg = { .strip_gpio_num = LED_PIN, .max_leds = LED_COUNT, .led_model = LED_MODEL_WS2812 };
    led_strip_rmt_config_t rmt = { .resolution_hz = 10 * 1000 * 1000 };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&cfg, &rmt, &strip));
    led_strip_clear(strip);
    xTaskCreate(animate, "leds", 3072, NULL, 3, NULL);
}

void leds_set(led_pattern_t p, uint32_t rgb)
{
    if (p == pattern && rgb == color) return;
    color = rgb;
    since = xTaskGetTickCount();
    pattern = p;
}
