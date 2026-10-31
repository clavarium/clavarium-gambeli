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

static float shown[LED_COUNT][3];

/* A new pattern blends in over half a second (a sudden switch-on was harsh, 31-10); after that the
   pattern itself is drawn exactly, so a pulse stays even (smoothing it all the time made it irregular). */
static void put(int i, uint32_t rgb, float k, float t)
{
    float want[3] = { ((rgb >> 16) & 255) * k, ((rgb >> 8) & 255) * k, (rgb & 255) * k };
    float a = t < 0.5f ? 0.15f : 1.0f;
    for (int j = 0; j < 3; j++) shown[i][j] += (want[j] - shown[i][j]) * a;
    led_strip_set_pixel(strip, i, (uint32_t)shown[i][0], (uint32_t)shown[i][1], (uint32_t)shown[i][2]);
}

static void animate(void *arg)
{
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        float t = (xTaskGetTickCount() - since) * portTICK_PERIOD_MS / 1000.0f;
        uint32_t c = color;
        for (int i = 0; i < LED_COUNT; i++) {
            float k = 0;
            /* Soft by default (an assistant in a room, not a warning light): only an alert is bright. */
            switch (pattern) {
            case LED_OFF: k = 0; break;
            case LED_SOLID: k = 0.22f; break;
            case LED_PULSE: k = 0.02f + 0.18f * (0.5f + 0.5f * sinf(t * 1.3f)); break;          // setup: slow, dim
            case LED_BREATHE: k = 0.04f + 0.14f * (0.5f + 0.5f * sinf(t * 2.2f)); break;        // speaking
            case LED_SPIN: {                                                                     // thinking: a slow, soft turn
                float head = fmodf(t * 3.5f, LED_COUNT);
                float d = fmodf(head - i + LED_COUNT, LED_COUNT);
                k = d < 4 ? 0.22f * (1 - d / 4) : 0;
                break;
            }
            case LED_BLINK2: k = t < 1.6f ? 0.18f * (0.5f - 0.5f * cosf(t * 2 * (float)M_PI / 0.8f)) : 0; break;   // error: two soft swells
            case LED_DOT: k = i == 0 ? 0.12f : 0; break;
            case LED_OFFLINE: k = i % 2 ? 0 : 0.015f + 0.05f * (0.5f + 0.5f * sinf(t * 0.7f)); break;            // a faint, slow glow
            case LED_ALERT: k = 0.15f + 0.65f * (0.5f + 0.5f * sinf(t * 5.0f)); break;          // a real alert: bright, seen across the room
            }
            put(i, c, k, t);
        }
        led_strip_refresh(strip);
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(40));   // a steady 25 frames a second, whatever else runs
    }
}

void leds_init(void)
{
    led_strip_config_t cfg = { .strip_gpio_num = LED_PIN, .max_leds = LED_COUNT, .led_model = LED_MODEL_WS2812 };
    led_strip_rmt_config_t rmt = { .resolution_hz = 10 * 1000 * 1000 };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&cfg, &rmt, &strip));
    led_strip_clear(strip);
    xTaskCreatePinnedToCore(animate, "leds", 3072, NULL, 8, NULL, 1);   // above the wake word: its bursts made the lights stutter
}

void leds_set(led_pattern_t p, uint32_t rgb)
{
    if (p == pattern && rgb == color) return;
    color = rgb;
    since = xTaskGetTickCount();
    pattern = p;
}
