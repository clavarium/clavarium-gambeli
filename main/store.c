// Settings in NVS. The Wi-Fi password and the key are written here and read back only to use them.
#include "box.h"
#include "nvs.h"
#include <string.h>

box_conf_t conf = { .volume = 60 };

static void get(nvs_handle_t h, const char *name, char *out, size_t len)
{
    size_t n = len;
    if (nvs_get_str(h, name, out, &n) != ESP_OK) out[0] = 0;
}

bool store_load(void)
{
    nvs_handle_t h;
    if (nvs_open("box", NVS_READONLY, &h) != ESP_OK) return false;
    get(h, "ssid", conf.ssid, sizeof conf.ssid);
    get(h, "pass", conf.pass, sizeof conf.pass);
    get(h, "url", conf.url, sizeof conf.url);
    get(h, "room", conf.room, sizeof conf.room);
    get(h, "id", conf.id, sizeof conf.id);
    get(h, "key", conf.key, sizeof conf.key);
    int32_t v;
    if (nvs_get_i32(h, "volume", &v) == ESP_OK) conf.volume = v;
    nvs_close(h);
    return conf.ssid[0] && conf.url[0] && conf.key[0];
}

void store_save(void)
{
    nvs_handle_t h;
    if (nvs_open("box", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "ssid", conf.ssid);
    nvs_set_str(h, "pass", conf.pass);
    nvs_set_str(h, "url", conf.url);
    nvs_set_str(h, "room", conf.room);
    nvs_set_str(h, "id", conf.id);
    nvs_set_str(h, "key", conf.key);
    nvs_set_i32(h, "volume", conf.volume);
    nvs_commit(h);
    nvs_close(h);
}

void store_volume(int level)
{
    conf.volume = level;
    nvs_handle_t h;
    if (nvs_open("box", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, "volume", level);
    nvs_commit(h);
    nvs_close(h);
}

void store_wipe(void)
{
    nvs_handle_t h;
    if (nvs_open("box", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_all(h);
    nvs_commit(h);
    nvs_close(h);
    memset(&conf, 0, sizeof conf);
    conf.volume = 60;
}
