// The voice box: states, keys, microphone, and Clavarium's commands (protocol v1, see PROTOCOL.md).
#include "box.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

static const char *TAG = "box";
typedef enum { ST_OFFLINE, ST_IDLE, ST_LISTEN, ST_THINK, ST_SPEAK, ST_ALERT, ST_UPDATE, ST_ERROR } state_t;
static state_t state = ST_OFFLINE;
static QueueHandle_t events;
static volatile bool listening, muted;
static bool speak_ending;
static char alert_id[48];
static int alert_chimes;
static TickType_t state_since, last_ping;
static char pending_ack[160];   // an ack to send once the link is back (after wifi.join)

void post_event(ev_type_t type, int arg, char *text)
{
    box_ev_t e = { type, arg, text };
    if (xQueueSend(events, &e, pdMS_TO_TICKS(100)) != pdTRUE) free(text);
}

static void set_state(state_t s)
{
    state = s;
    state_since = xTaskGetTickCount();
    switch (s) {
    case ST_OFFLINE: leds_set(LED_OFFLINE, 0xffa000); break;
    case ST_IDLE:    leds_set(muted ? LED_DOT : LED_OFF, 0xff0000); break;
    case ST_LISTEN:  leds_set(LED_SOLID, 0x0050ff); break;
    case ST_THINK:   leds_set(LED_SPIN, 0x0050ff); break;
    case ST_SPEAK:   leds_set(LED_BREATHE, 0xffffff); break;
    case ST_ALERT:   leds_set(LED_PULSE, 0xff6000); break;
    case ST_UPDATE:  leds_set(LED_SPIN, 0x9000ff); break;
    case ST_ERROR:   leds_set(LED_BLINK2, 0xff0000); break;
    }
}

static void send_obj(cJSON *o)
{
    char *s = cJSON_PrintUnformatted(o);
    link_send_text(s);
    cJSON_free(s);
    cJSON_Delete(o);
}

static void send_simple(const char *t, const char *key, const char *value)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "t", t);
    if (key) cJSON_AddStringToObject(o, key, value);
    send_obj(o);
}

static void ack(const char *id, bool ok, const char *detail)
{
    if (!id || !id[0]) return;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "t", "ack");
    cJSON_AddStringToObject(o, "id", id);
    cJSON_AddBoolToObject(o, "ok", ok);
    if (detail) cJSON_AddStringToObject(o, "detail", detail);
    send_obj(o);
}

static void error_cue(void)
{
    sounds_play(SND_ERROR);
    set_state(ST_ERROR);
}

/* ---- microphone ---- */

static void listen_start(void)
{
    speech_stop();
    speech_accept = false;
    sounds_play(SND_DING);
    send_simple("listen.start", "reason", "button");
    listening = true;
    set_state(ST_LISTEN);
}

static void listen_end(const char *reason)
{
    if (!listening) return;
    listening = false;
    send_simple("listen.end", "reason", reason);
    set_state(ST_THINK);
}

static void mic_task(void *arg)
{
    static int16_t pcm[BOX_CHUNK];
    for (;;) {
        if (!listening || muted) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        // A new sentence: let the "ding" finish and drop what the codec held meanwhile.
        for (int i = 0; i < 15 && listening; i++) board_mic_read(pcm, BOX_CHUNK);
        float floor = 0;
        int chunks = 0, quiet = 0;
        bool heard = false;
        while (listening && !muted) {
            int n = board_mic_read(pcm, BOX_CHUNK);
            if (n <= 0) continue;
            link_send_audio(pcm, n);
            double sum = 0;
            for (int i = 0; i < n; i++) sum += (double)pcm[i] * pcm[i];
            float rms = sqrtf(sum / n);
            if (chunks < 10) floor = (floor * chunks + rms) / (chunks + 1);
            chunks++;
            bool voice = rms > fmaxf(500, floor * 3);
            if (voice) { heard = true; quiet = 0; } else quiet++;
            if (heard && quiet >= 40) { post_event(EV_LISTEN_END, 0, NULL); break; }     // 0.8 s of silence
            if (chunks >= 15 * 1000 / 20) { post_event(EV_LISTEN_END, 1, NULL); break; } // 15 s at most
        }
        while (listening) vTaskDelay(pdMS_TO_TICKS(10));   // the main task closes the sentence
    }
}

/* ---- keys ---- */

static void keys_task(void *arg)
{
    uint32_t prev = 0;
    TickType_t down[3] = {0};
    bool long_sent = false;
    for (;;) {
        uint32_t k = board_keys();
        for (int i = 0; i < 3; i++) {
            uint32_t bit = 1u << i;
            if ((k & bit) && !(prev & bit)) { down[i] = xTaskGetTickCount(); if (i == 0) long_sent = false; }
            if (i == 0 && (k & bit) && !long_sent && xTaskGetTickCount() - down[i] > pdMS_TO_TICKS(5000)) {
                long_sent = true;
                post_event(EV_KEY_LONG, KEY_MAIN, NULL);
            }
            if (!(k & bit) && (prev & bit) && !(i == 0 && long_sent)) post_event(EV_KEY_SHORT, bit, NULL);
        }
        prev = k;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* ---- Clavarium's commands ---- */

static void hello(void)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "t", "hello");
    cJSON_AddStringToObject(o, "firmware", esp_app_get_description()->version);
    cJSON_AddItemToObject(o, "capabilities", cJSON_Parse(BOX_CAPS));
    cJSON_AddNumberToObject(o, "rssi", link_rssi());
    cJSON_AddNumberToObject(o, "volume", conf.volume);
    cJSON_AddBoolToObject(o, "muted", muted);
    send_obj(o);
}

typedef struct { char id[48], ssid[33], pass[65], url[256], sha[65]; } job_t;

static void wifi_job(void *arg)
{
    job_t *j = arg;
    char old_ssid[33], old_pass[65];
    strcpy(old_ssid, conf.ssid);
    strcpy(old_pass, conf.pass);
    bool ok = wifi_join(j->ssid, j->pass, 20000);
    if (ok) {
        strcpy(conf.ssid, j->ssid);
        strcpy(conf.pass, j->pass);
        store_save();
    } else {
        wifi_join(old_ssid, old_pass, 20000);
    }
    snprintf(pending_ack, sizeof pending_ack, "%s|%d", j->id, ok);
    memset(j, 0, sizeof *j);
    free(j);
    vTaskDelete(NULL);
}

static void ota_job(void *arg)
{
    job_t *j = arg;
    char detail[64] = "";
    bool ok = ota_install(j->url, j->sha, detail, sizeof detail);
    ack(j->id, ok, ok ? NULL : detail);
    free(j);
    if (ok) { vTaskDelay(pdMS_TO_TICKS(500)); esp_restart(); }
    post_event(EV_TICK, 1, NULL);   // back from the update state
    vTaskDelete(NULL);
}

static uint32_t parse_color(const char *s)
{
    return s && s[0] == '#' ? strtoul(s + 1, NULL, 16) : 0xffffff;
}

static void on_message(char *text)
{
    cJSON *m = cJSON_Parse(text);
    if (!m) return;
    const char *t = cJSON_GetStringValue(cJSON_GetObjectItem(m, "t"));
    const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(m, "id"));
    if (!t) t = "";
    if (!strcmp(t, "pong")) {
    } else if (!strcmp(t, "cue")) {
        const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(m, "name"));
        if (name && !strcmp(name, "thinking")) {
            listening = false;
            sounds_play(SND_TIC);
            set_state(ST_THINK);
        } else if (name && !strcmp(name, "error")) {
            listening = false;
            error_cue();
        } else if (name && !strcmp(name, "done")) {
            sounds_play(SND_DONE);
            set_state(ST_IDLE);
        }
        ack(id, true, NULL);
    } else if (!strcmp(t, "speak.start")) {
        listening = false;
        speak_ending = false;
        speech_accept = true;
        set_state(ST_SPEAK);
    } else if (!strcmp(t, "speak.end")) {
        speak_ending = true;
    } else if (!strcmp(t, "alert")) {
        strlcpy(alert_id, id ? id : "", sizeof alert_id);
        alert_chimes = 0;
        set_state(ST_ALERT);
    } else if (!strcmp(t, "led")) {
        const char *p = cJSON_GetStringValue(cJSON_GetObjectItem(m, "pattern"));
        uint32_t c = parse_color(cJSON_GetStringValue(cJSON_GetObjectItem(m, "color")));
        led_pattern_t lp = !p ? LED_SOLID : !strcmp(p, "off") ? LED_OFF : !strcmp(p, "pulse") ? LED_PULSE : !strcmp(p, "spin") ? LED_SPIN : LED_SOLID;
        leds_set(lp, c);
        ack(id, true, NULL);
    } else if (!strcmp(t, "volume")) {
        cJSON *l = cJSON_GetObjectItem(m, "level");
        bool ok = cJSON_IsNumber(l) && l->valueint >= 0 && l->valueint <= 100;
        if (ok) { board_volume(l->valueint); store_volume(l->valueint); }
        ack(id, ok, ok ? NULL : "level 0-100");
    } else if (!strcmp(t, "wifi.scan")) {
        static char list[2048];
        wifi_scan_json(list, sizeof list);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "t", "wifi.scan.result");
        cJSON_AddStringToObject(o, "id", id ? id : "");
        cJSON_AddItemToObject(o, "networks", cJSON_Parse(list));
        send_obj(o);
    } else if (!strcmp(t, "wifi.join")) {
        const char *ssid = cJSON_GetStringValue(cJSON_GetObjectItem(m, "ssid"));
        const char *pass = cJSON_GetStringValue(cJSON_GetObjectItem(m, "password"));
        if (!ssid || !ssid[0]) ack(id, false, "ssid");
        else {
            job_t *j = calloc(1, sizeof *j);
            strlcpy(j->id, id ? id : "", sizeof j->id);
            strlcpy(j->ssid, ssid, sizeof j->ssid);
            strlcpy(j->pass, pass ? pass : "", sizeof j->pass);
            xTaskCreate(wifi_job, "join", 4096, j, 5, NULL);
        }
    } else if (!strcmp(t, "reboot")) {
        ack(id, true, NULL);
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    } else if (!strcmp(t, "ota")) {
        const char *url = cJSON_GetStringValue(cJSON_GetObjectItem(m, "url"));
        const char *sha = cJSON_GetStringValue(cJSON_GetObjectItem(m, "sha256"));
        if (!url || !sha || strlen(sha) != 64) ack(id, false, "url and sha256");
        else {
            job_t *j = calloc(1, sizeof *j);
            strlcpy(j->id, id ? id : "", sizeof j->id);
            strlcpy(j->url, url, sizeof j->url);
            strlcpy(j->sha, sha, sizeof j->sha);
            set_state(ST_UPDATE);
            xTaskCreate(ota_job, "ota", 8192, j, 5, NULL);
        }
    } else {
        ack(id, false, "unknown");
    }
    cJSON_Delete(m);
}

static void on_key(int key)
{
    if (key == KEY_MUTE) {
        muted = !muted;
        if (muted && listening) listen_end("button");
        sounds_play(SND_CLICK);
        send_simple("event", "name", muted ? "mute.on" : "mute.off");
        if (state == ST_IDLE) set_state(ST_IDLE);
        return;
    }
    switch (state) {
    case ST_ALERT: {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "t", "event");
        cJSON_AddStringToObject(o, "name", "button");
        cJSON_AddStringToObject(o, "alert", alert_id);
        send_obj(o);
        ack(alert_id, true, NULL);
        set_state(ST_IDLE);
        break;
    }
    case ST_LISTEN: listen_end("button"); break;
    case ST_OFFLINE: case ST_UPDATE: error_cue(); break;
    default:
        if (muted) { sounds_play(SND_CLICK); leds_set(LED_BLINK2, 0xff0000); break; }
        listen_start();   // also interrupts an answer being spoken
    }
}

static void enter_setup(void)
{
    send_simple("event", "name", "button.long");
    nvs_handle_t h;
    if (nvs_open("box", NVS_READWRITE, &h) == ESP_OK) { nvs_set_u8(h, "setup", 1); nvs_commit(h); nvs_close(h); }
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
}

static bool setup_asked(void)
{
    nvs_handle_t h;
    uint8_t v = 0;
    if (nvs_open("box", NVS_READWRITE, &h) != ESP_OK) return false;
    nvs_get_u8(h, "setup", &v);
    if (v) { nvs_erase_key(h, "setup"); nvs_commit(h); }
    nvs_close(h);
    return v;
}

void app_main(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) { nvs_flash_erase(); nvs_flash_init(); }
    events = xQueueCreate(16, sizeof(box_ev_t));
    board_init();
    leds_init();
    sounds_init();
    bool paired = store_load();
    board_volume(conf.volume);
    wifi_base_init();
    ESP_LOGI(TAG, "firmware %s, %s", esp_app_get_description()->version, paired ? "paired" : "not paired");
    if (!paired || setup_asked()) portal_run(NULL);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    set_state(ST_OFFLINE);
    link_start();
    xTaskCreate(mic_task, "mic", 6144, NULL, 7, NULL);
    xTaskCreate(keys_task, "keys", 3072, NULL, 4, NULL);

    for (;;) {
        box_ev_t ev;
        if (xQueueReceive(events, &ev, pdMS_TO_TICKS(100)) == pdTRUE) {
            switch (ev.type) {
            case EV_LINK_UP:
                hello();
                last_ping = xTaskGetTickCount();
                if (pending_ack[0]) {
                    char *bar = strchr(pending_ack, '|');
                    *bar = 0;
                    ack(pending_ack, bar[1] == '1', bar[1] == '1' ? NULL : "join failed");
                    pending_ack[0] = 0;
                }
                if (state == ST_OFFLINE) set_state(ST_IDLE);
                break;
            case EV_LINK_DOWN:
                listening = false;
                speech_accept = false;
                speech_stop();
                set_state(ST_OFFLINE);
                break;
            case EV_MSG: on_message(ev.text); free(ev.text); break;
            case EV_KEY_SHORT: on_key(ev.arg); break;
            case EV_KEY_LONG: enter_setup(); break;
            case EV_LISTEN_END: listen_end(ev.arg ? "timeout" : "silence"); break;
            case EV_TICK: if (ev.arg == 1 && state == ST_UPDATE) set_state(ST_IDLE); break;
            default: break;
            }
        }
        TickType_t now = xTaskGetTickCount();
        if (state == ST_SPEAK && speak_ending && speech_idle()) { speech_accept = false; speak_ending = false; set_state(ST_IDLE); }
        if (state == ST_ERROR && now - state_since > pdMS_TO_TICKS(1500)) set_state(ST_IDLE);
        if (state == ST_THINK && now - state_since > pdMS_TO_TICKS(30000)) error_cue();   // no answer came
        if (state == ST_ALERT && alert_chimes < 3 && now - state_since > pdMS_TO_TICKS(alert_chimes * 4000)) {
            alert_chimes++;
            sounds_play(SND_CHIME);
        }
        if (state != ST_OFFLINE && now - last_ping > pdMS_TO_TICKS(30000)) {
            last_ping = now;
            send_simple("ping", NULL, NULL);
        }
    }
}
