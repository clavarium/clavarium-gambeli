// The permanent connection to Clavarium: wss://…/c/api/box/ws, key in Authorization.
// Text frames are JSON messages, handed to the main task; binary frames are speech, played at once.
#include "box.h"
#include "esp_websocket_client.h"
#include "esp_crt_bundle.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "link";
static esp_websocket_client_handle_t ws;
static EventGroupHandle_t bits;
#define DOWN BIT0
#define REFUSED BIT1
#define WAS_UP BIT2
volatile bool speech_accept;
static volatile bool up;
static char *text;        // a text message being reassembled
static char problem[160]; // why the last try failed, for the status page
static char was[160];     // the problem before this connection: said to Clavarium in hello
static int tries;
static size_t text_len;

static void on_ws(void *arg, esp_event_base_t base, int32_t id, void *ev)
{
    esp_websocket_event_data_t *d = ev;
    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED:
        up = true;
        strlcpy(was, problem, sizeof was);
        problem[0] = 0;
        xEventGroupSetBits(bits, WAS_UP);
        post_event(EV_LINK_UP, 0, NULL);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
        if (up) post_event(EV_LINK_DOWN, 0, NULL);
        up = false;
        xEventGroupSetBits(bits, DOWN);
        break;
    case WEBSOCKET_EVENT_ERROR:
        if (d->error_handle.esp_ws_handshake_status_code == 401) xEventGroupSetBits(bits, REFUSED);
        snprintf(problem, sizeof problem, "error type %d, http %d, tls %s (%d), errno %d",
                 d->error_handle.error_type, d->error_handle.esp_ws_handshake_status_code,
                 esp_err_to_name(d->error_handle.esp_tls_last_esp_err), d->error_handle.esp_tls_stack_err,
                 d->error_handle.esp_transport_sock_errno);
        ESP_LOGW(TAG, "%s", problem);
        break;
    case WEBSOCKET_EVENT_DATA:
        if (d->op_code == 0x2 || (d->op_code == 0x0 && !text)) {
            if (speech_accept && d->data_len > 0) speech_feed((const uint8_t *)d->data_ptr, d->data_len);
        } else if (d->op_code == 0x1 || (d->op_code == 0x0 && text)) {
            if (d->payload_offset == 0) { free(text); text = malloc(d->payload_len + 1); text_len = 0; }
            if (!text || text_len + d->data_len > (size_t)d->payload_len) break;
            memcpy(text + text_len, d->data_ptr, d->data_len);
            text_len += d->data_len;
            if (text_len == (size_t)d->payload_len) {
                text[text_len] = 0;
                /* The speech follows speak.start at once, in this same task: it is let in here and
                   now, not when the main task gets to the message (found on the real box: the
                   whole answer was dropped while speak.start waited in the queue). */
                if (strstr(text, "\"t\":\"speak.start\"")) { speech_stop(); speech_accept = true; }
                post_event(EV_MSG, 0, text);   // the main task frees it
                text = NULL;
            }
        }
        break;
    }
}

static void run(void *arg)
{
    char url[320], auth[128];
    const char *rest = strncmp(conf.url, "https://", 8) == 0 ? conf.url + 8 : strncmp(conf.url, "http://", 7) == 0 ? conf.url + 7 : conf.url;
    snprintf(url, sizeof url, "%s://%s/c/api/box/ws", strncmp(conf.url, "http://", 7) == 0 ? "ws" : "wss", rest);
    snprintf(auth, sizeof auth, "Authorization: Bearer %s\r\n", conf.key);
    int wait = 1;
    for (;;) {
        tries++;
        if (!wifi_connected() && !wifi_join(conf.ssid, conf.pass, 15000)) snprintf(problem, sizeof problem, "Wi-Fi \"%s\" not joined", conf.ssid);
        if (wifi_connected()) {
            esp_websocket_client_config_t c = {
                .uri = url, .headers = auth, .crt_bundle_attach = esp_crt_bundle_attach,
                .buffer_size = 4096, .task_stack = 8192, .disable_auto_reconnect = true,
                .network_timeout_ms = 10000, .ping_interval_sec = 30,
            };
            xEventGroupClearBits(bits, DOWN | REFUSED | WAS_UP);
            ws = esp_websocket_client_init(&c);
            esp_websocket_register_events(ws, WEBSOCKET_EVENT_ANY, on_ws, NULL);
            esp_websocket_client_start(ws);
            EventBits_t b = xEventGroupWaitBits(bits, DOWN, pdTRUE, pdFALSE, portMAX_DELAY);
            b |= xEventGroupGetBits(bits);
            esp_websocket_client_handle_t old = ws;
            ws = NULL;
            esp_websocket_client_destroy(old);
            if (b & REFUSED) {   // the key was withdrawn: back to setup
                ESP_LOGW(TAG, "key refused");
                store_wipe();
                esp_restart();
            }
            if (b & WAS_UP) wait = 1;   // was connected: start the backoff again
        }
        ESP_LOGI(TAG, "reconnecting in %d s", wait);
        vTaskDelay(pdMS_TO_TICKS(wait * 1000));
        wait = wait >= 32 ? 60 : wait * 2;
    }
}

void link_start(void)
{
    bits = xEventGroupCreate();
    xTaskCreate(run, "link", 6144, NULL, 5, NULL);
}

bool link_send_text(const char *json)
{
    if (!ws || !up) return false;
    return esp_websocket_client_send_text(ws, json, strlen(json), pdMS_TO_TICKS(2000)) >= 0;
}

bool link_send_audio(const int16_t *pcm, int samples)
{
    if (!ws || !up) return false;
    return esp_websocket_client_send_bin(ws, (const char *)pcm, samples * 2, pdMS_TO_TICKS(1000)) >= 0;
}

int link_rssi(void)
{
    wifi_ap_record_t ap;
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
}

const char *link_problem(void) { return problem; }
int link_tries(void) { return tries; }
bool link_up(void) { return up; }
const char *link_was(void) { return was; }
