// Improv Wi-Fi over the USB serial line (improv-wifi.com, serial v1): right after a browser flash,
// the same page gives the box its Wi-Fi, without a phone. One command of our own (0xC0) adds
// Clavarium's address, the room and a pairing code; Wi-Fi then pairing happen at once.
#include "box.h"
#include "driver/usb_serial_jtag.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

enum { TYPE_STATE = 1, TYPE_ERROR = 2, TYPE_RPC = 3, TYPE_RESULT = 4 };
enum { STATE_READY = 2, STATE_PROVISIONING = 3, STATE_PROVISIONED = 4 };
enum { ERR_NONE = 0, ERR_INVALID = 1, ERR_UNKNOWN_CMD = 2, ERR_CONNECT = 3, ERR_PAIRING = 0x80, ERR_OTHER = 0xff };
enum { CMD_WIFI = 1, CMD_STATE = 2, CMD_INFO = 3, CMD_SCAN = 4, CMD_CLAVARIUM = 0xc0 };

static char pending_code[16];   // given by 0xC0, used by the next Wi-Fi command

static void packet(uint8_t type, const uint8_t *data, uint8_t len)
{
    uint8_t buf[9 + 255 + 2];
    memcpy(buf, "IMPROV", 6);
    buf[6] = 1; buf[7] = type; buf[8] = len;
    memcpy(buf + 9, data, len);
    uint8_t sum = 0;
    for (int i = 0; i < 9 + len; i++) sum += buf[i];
    buf[9 + len] = sum;
    buf[10 + len] = '\n';
    usb_serial_jtag_write_bytes(buf, 11 + len, pdMS_TO_TICKS(100));
}

static void state(uint8_t s) { packet(TYPE_STATE, &s, 1); }
static void error(uint8_t e) { packet(TYPE_ERROR, &e, 1); }

// An RPC result: the command, then strings, each with its length.
static void result(uint8_t cmd, const char *const *strings, int n)
{
    uint8_t data[255];
    int at = 2;
    for (int i = 0; i < n; i++) {
        int l = strlen(strings[i]);
        if (at + 1 + l > (int)sizeof data) break;
        data[at++] = l;
        memcpy(data + at, strings[i], l);
        at += l;
    }
    data[0] = cmd;
    data[1] = at - 2;
    packet(TYPE_RESULT, data, at);
}

// The strings of a command's data: count them into out[], each NUL-terminated in place.
static int strings_of(uint8_t *data, int len, char **out, int max)
{
    int n = 0, at = 0;
    while (at < len && n < max) {
        int l = data[at];
        if (at + 1 + l > len) return -1;
        memmove(data + at, data + at + 1, l);   // shift left one byte to make room for the NUL
        data[at + l] = 0;
        out[n++] = (char *)data + at;
        at += l + 1;
    }
    return n;
}

static void scan(void)
{
    static char json[2048];
    wifi_scan_json(json, sizeof json);
    // The JSON is ours and flat: [{"ssid":"…","rssi":-50,"secure":true},…]
    char *p = json;
    while ((p = strstr(p, "\"ssid\":\"")) != NULL) {
        p += 8;
        char ssid[33] = {0}, rssi[8] = {0};
        int i = 0;
        while (*p && *p != '"' && i < 32) { if (*p == '\\' && p[1]) p++; ssid[i++] = *p++; }
        char *r = strstr(p, "\"rssi\":");
        char *s = strstr(p, "\"secure\":");
        if (!r || !s) break;
        snprintf(rssi, sizeof rssi, "%d", atoi(r + 7));
        const char *strings[] = { ssid, rssi, strncmp(s + 9, "true", 4) == 0 ? "YES" : "NO" };
        result(CMD_SCAN, strings, 3);
    }
    result(CMD_SCAN, NULL, 0);
}

static void rpc(uint8_t *data, int len)
{
    if (len < 2 || data[1] != len - 2) { error(ERR_INVALID); return; }
    uint8_t cmd = data[0];
    char *s[4];
    int n = strings_of(data + 2, len - 2, s, 4);
    if (n < 0) { error(ERR_INVALID); return; }
    switch (cmd) {
    case CMD_STATE:
        state(wifi_connected() && conf.key[0] ? STATE_PROVISIONED : STATE_READY);
        if (wifi_connected() && conf.url[0]) { const char *u[] = { conf.url }; result(CMD_STATE, u, 1); }
        break;
    case CMD_INFO: {
        const char *info[] = { "gambeli", esp_app_get_description()->version, "ESP32-S3", conf.room[0] ? conf.room : "Gambeli" };
        result(CMD_INFO, info, 4);
        break;
    }
    case CMD_SCAN: scan(); break;
    case CMD_CLAVARIUM:
        if (n < 3) { error(ERR_INVALID); return; }
        strlcpy(conf.url, s[0], sizeof conf.url);
        strlcpy(conf.room, s[1], sizeof conf.room);
        strlcpy(pending_code, s[2], sizeof pending_code);
        error(ERR_NONE);
        result(CMD_CLAVARIUM, NULL, 0);
        break;
    case CMD_WIFI: {
        if (n < 1) { error(ERR_INVALID); return; }
        state(STATE_PROVISIONING);
        if (!wifi_join(s[0], n > 1 ? s[1] : "", 20000)) { error(ERR_CONNECT); state(STATE_READY); return; }
        if (pending_code[0]) {
            char why[160];
            if (!pair_box(pending_code, why, sizeof why)) {
                ESP_LOGW("improv", "pairing refused: %s", why);
                error(ERR_PAIRING);
                state(STATE_READY);
                return;
            }
            pending_code[0] = 0;
        }
        strlcpy(conf.ssid, s[0], sizeof conf.ssid);
        strlcpy(conf.pass, n > 1 ? s[1] : "", sizeof conf.pass);
        store_save();
        error(ERR_NONE);
        state(STATE_PROVISIONED);
        const char *u[] = { conf.url };
        result(CMD_WIFI, u, conf.url[0] ? 1 : 0);
        vTaskDelay(pdMS_TO_TICKS(1500));
        esp_restart();   // starts again, connected
        break;
    }
    default:
        error(ERR_UNKNOWN_CMD);
    }
}

static void serve(void *arg)
{
    static uint8_t buf[9 + 256];
    int have = 0;
    for (;;) {
        uint8_t c;
        if (usb_serial_jtag_read_bytes(&c, 1, portMAX_DELAY) != 1) continue;
        if (have < 6) {   // looking for "IMPROV"
            if (c == (uint8_t)"IMPROV"[have]) buf[have++] = c;
            else have = c == 'I' ? (buf[0] = c, 1) : 0;
            continue;
        }
        buf[have++] = c;
        if (have < 9) continue;
        int len = buf[8];
        if (have < 10 + len) continue;
        uint8_t sum = 0;
        for (int i = 0; i < 9 + len; i++) sum += buf[i];
        if (buf[6] == 1 && sum == buf[9 + len] && buf[7] == TYPE_RPC) rpc(buf + 9, len);
        else if (sum != buf[9 + len]) error(ERR_INVALID);
        have = 0;
    }
}

void improv_start(void)
{
    usb_serial_jtag_driver_config_t cfg = { .tx_buffer_size = 512, .rx_buffer_size = 512 };
    if (usb_serial_jtag_driver_install(&cfg) != ESP_OK) return;
    xTaskCreate(serve, "improv", 6144, NULL, 4, NULL);
}
