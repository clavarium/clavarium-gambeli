// The voice box: states, keys, microphone, and Clavarium's commands (protocol v1, see PROTOCOL.md).
#include "box.h"
#include "endpoint.h"
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
static bool listen_after;   // the answer was a question: listen again once it is said
static char alert_id[48];
static int alert_chimes;
static TickType_t state_since, last_ping;
static char pending_ack[160];
/* The last sharp sounds heard (peak, rms, background), shown on the status page to set the knock. */
static int knocks[6][3]; static int knock_at;
void knock_note(int peak, int rms, int quiet) { knocks[knock_at][0] = peak; knocks[knock_at][1] = rms; knocks[knock_at][2] = quiet; knock_at = (knock_at + 1) % 6; }
int knock_seen(int i, int what) { return knocks[(knock_at + 5 - i) % 6][what]; }   // an ack to send once the link is back (after wifi.join)

bool speech_held(void) { return listening; }

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
    case ST_OFFLINE: leds_set(LED_OFFLINE, 0xff9020); break;
    case ST_IDLE:    leds_set(muted ? LED_DOT : LED_OFF, 0xff0000); break;
    case ST_LISTEN:  leds_set(LED_SOLID, 0x2060ff); break;
    case ST_THINK:   leds_set(LED_SPIN, 0x3070ff); break;
    case ST_SPEAK:   leds_set(LED_BREATHE, 0xffd8a0); break;   // warm
    case ST_ALERT:   leds_set(LED_ALERT, 0xff2000); break;
    case ST_UPDATE:  leds_set(LED_SPIN, 0x9000ff); break;
    case ST_ERROR:   leds_set(LED_BLINK2, 0xff3020); break;
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

static volatile bool follow;
static volatile bool own_voice;   // what plays is its own voice (speak.start kind voice), not a stream
/* A LONG RECORDING (a meeting): asked by Clavarium, started once the box has finished speaking, the whole
   sound sent until the button or the wake word stops it. A slow red glow while it lasts. */
static volatile bool recording, record_pending;
static void record_start(void)
{
    record_pending = false;
    speech_stop();
    sounds_play(SND_DING);
    send_simple("record.start", "reason", "asked");
    recording = true;
    listening = true;
    set_state(ST_LISTEN);
    leds_set(LED_PULSE, 0xff2000);
}
static void record_stop(const char *reason)
{
    record_pending = false;
    if (!recording) return;
    recording = false;
    listening = false;
    send_simple("record.end", "reason", reason);
    sounds_play(SND_DONE);
    set_state(ST_THINK);
}   // listening again without a wake word: a short wait for an answer

static void listen_start(const char *reason)
{
    follow = !strcmp(reason, "follow");
    speech_stop();
    speech_accept = false;
    sounds_play(SND_DING);
    send_simple("listen.start", "reason", reason);
    listening = true;
    set_state(ST_LISTEN);
}

static void listen_end(const char *reason)
{
    if (!listening) return;
    listening = false;
    send_simple("listen.end", "reason", reason);
    if (!strcmp(reason, "nobody")) { set_state(ST_IDLE); return; }   // nobody spoke: back to quiet, no sound
    sounds_play(SND_HEARD);   // a light "got it" before the answer
    set_state(ST_THINK);
}

static void mic_task(void *arg)
{
    static int16_t pcm[BOX_CHUNK];
    static int16_t four[BOX_CHUNK * 4];
    static TickType_t last_knock;
    static float quiet_rms;
    bool was = false, ended = false;
    int skip = 0;
    for (;;) {
        if (muted) { was = false; vTaskDelay(pdMS_TO_TICKS(20)); continue; }   // muted: the microphone is not read at all
        int n = board_mic_read_raw(four, BOX_CHUNK);
        if (n <= 0) continue;
        for (int i = 0; i < n; i++) pcm[i] = four[4 * i + 1];              // the first microphone
        if (!listening) {
            /* After a sentence, or once the box has finished speaking: the wake word starts afresh. */
            static bool busy_before;
            bool busy = !speech_idle();
            mww_strict(busy ? (own_voice ? 2 : 1) : 0);
            if (was || (busy_before && !busy)) mww_reset();
            busy_before = busy;
            was = false;
            mww_feed(pcm, n);                                              // only the wake word hears it, on the chip
            /* Knock knock: two short, sharp sounds 0.15 to 0.7 s apart, the box quiet -- as a press. */
            int peak = 0; double e = 0;
            for (int i = 0; i < n; i++) { int a = abs(pcm[i]); if (a > peak) peak = a; e += (double)pcm[i] * pcm[i]; }
            float rms = sqrtf(e / n);
            TickType_t now = xTaskGetTickCount();
            bool sharp = peak > 3500 && peak > rms * 3.5f && peak > quiet_rms * 6 && speech_idle();
            if (peak > 2500 && peak > rms * 3) knock_note(peak, (int)rms, (int)quiet_rms);   // for the status page: what a knock looks like here
            if (sharp && now - last_knock > pdMS_TO_TICKS(120)) {
                TickType_t gap = now - last_knock;
                if (gap >= pdMS_TO_TICKS(150) && gap <= pdMS_TO_TICKS(700)) { post_event(EV_KEY_SHORT, KEY_MAIN, NULL); last_knock = 0; }
                else last_knock = now;
            }
            quiet_rms = quiet_rms * 0.9f + rms * 0.1f;
            ep_room(rms);                                                  // the room is learnt all the time
            continue;
        }
        if (recording) { link_send_audio(pcm, n); mww_feed(pcm, n); continue; }   // all of it; the wake word stops it
        if (!was) { was = true; ended = false; skip = 15; ep_begin(follow ? 6 : 15); }
        if (skip > 0) { skip--; continue; }                                 // the "ding", and what the codec held meanwhile
        if (ended) continue;                                                // the main task closes the sentence
        link_send_audio(pcm, n);
        double sum = 0;
        for (int i = 0; i < n; i++) sum += (double)pcm[i] * pcm[i];
        float rms = sqrtf(sum / n);
        ep_result_t r = ep_chunk(rms);
        if (r != EP_GOING) { ended = true; post_event(EV_LISTEN_END, r == EP_TOO_LONG ? 1 : r == EP_NOBODY ? 2 : 0, NULL); }
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
    if (link_was()[0]) cJSON_AddStringToObject(o, "problem", link_was());   // why it was offline: for the server's log
    cJSON_AddNumberToObject(o, "tries", link_tries());
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
    char detail[128] = "";
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
        speak_ending = false;
        speech_accept = true;
        /* THE BOX'S WORDS, sound and light together (agreed 31-10): rising + steady blue, I listen; two light
           notes + turning blue, got it; two falling notes + lights out, I stopped listening; a small bell +
           warm breathing, a message comes (said of itself, not an answer). */
        const char *kind = cJSON_GetStringValue(cJSON_GetObjectItem(m, "kind"));
        own_voice = !kind || strcmp(kind, "stream") != 0;
        if (!listening && state == ST_IDLE) sounds_play(SND_BELL);
        if (!listening) set_state(ST_SPEAK);   // someone is speaking: it waits for the end of their sentence
    } else if (!strcmp(t, "speak.end")) {
        const char *then = cJSON_GetStringValue(cJSON_GetObjectItem(m, "then"));
        listen_after = then && !strcmp(then, "listen");
        speak_ending = true;
    } else if (!strcmp(t, "record")) {
        if (cJSON_IsTrue(cJSON_GetObjectItem(m, "on"))) record_pending = true;
        else record_stop("asked");
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
    case ST_LISTEN: if (recording) record_stop("button"); else listen_end("button"); break;
    case ST_OFFLINE: case ST_UPDATE: error_cue(); break;
    default:
        if (muted) { sounds_play(SND_CLICK); leds_set(LED_BLINK2, 0xff0000); break; }
        listen_start("button");   // also interrupts an answer being spoken
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
    improv_start();
    ESP_LOGI(TAG, "firmware %s, %s", esp_app_get_description()->version, paired ? "paired" : "not paired");
    if (!paired || setup_asked()) portal_run(NULL);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    set_state(ST_OFFLINE);
    link_start();
    xTaskCreate(mic_task, "mic", 6144, NULL, 5, NULL);
    xTaskCreate(keys_task, "keys", 3072, NULL, 4, NULL);
    status_start();
    mww_start();

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
            case EV_WAKE:
                if (recording) { record_stop("wake"); break; }
                if (!muted && !listening && state != ST_OFFLINE && state != ST_UPDATE && state != ST_ALERT) listen_start("wake");
                else if (state == ST_OFFLINE) error_cue();   // heard, but not connected yet: say so rather than nothing
                break;
            case EV_LISTEN_END: listen_end(ev.arg == 1 ? "timeout" : ev.arg == 2 ? "nobody" : "silence"); break;
            case EV_TICK: if (ev.arg == 1 && state == ST_UPDATE) set_state(ST_IDLE); break;
            default: break;
            }
        }
        TickType_t now = xTaskGetTickCount();
        if (record_pending && !listening && speech_idle()) record_start();
        if (!listening && state != ST_SPEAK && state != ST_UPDATE && speech_waiting()) set_state(ST_SPEAK);   // held speech, now said
        if (state == ST_SPEAK && speak_ending && speech_idle()) {
            speech_accept = false; speak_ending = false; set_state(ST_IDLE);
            if (listen_after && !muted) { listen_after = false; listen_start("follow"); }   // a question was asked: the reply needs no wake word
        }
        if (state == ST_ERROR && now - state_since > pdMS_TO_TICKS(1500)) set_state(ST_IDLE);
        if (state == ST_THINK && now - state_since > pdMS_TO_TICKS(120000)) set_state(ST_IDLE);   // no answer came: quietly back
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
