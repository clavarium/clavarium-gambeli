// Wi-Fi, pairing, and the setup access point "Boitier-XXXX" with its page on the box's own address.
#include "box.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include <string.h>
#include <ctype.h>

static const char *TAG = "portal";
static EventGroupHandle_t wifi_bits;
#define GOT_IP BIT0
#define FAILED BIT1
static int last_reason;
static SemaphoreHandle_t join_lock;

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        last_reason = ((wifi_event_sta_disconnected_t *)data)->reason;
        xEventGroupClearBits(wifi_bits, GOT_IP);
        xEventGroupSetBits(wifi_bits, FAILED);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupClearBits(wifi_bits, FAILED);
        xEventGroupSetBits(wifi_bits, GOT_IP);
    }
}

void wifi_base_init(void)
{
    wifi_bits = xEventGroupCreate();
    join_lock = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_wifi_set_storage(WIFI_STORAGE_RAM);   // our own NVS keeps the settings
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_wifi, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi, NULL);
}

bool wifi_join(const char *ssid, const char *pass, int timeout_ms)
{
    wifi_config_t sta = {0};
    strlcpy((char *)sta.sta.ssid, ssid, sizeof sta.sta.ssid);
    strlcpy((char *)sta.sta.password, pass, sizeof sta.sta.password);
    sta.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WEP : WIFI_AUTH_OPEN;
    xSemaphoreTake(join_lock, portMAX_DELAY);
    esp_wifi_disconnect();
    xEventGroupClearBits(wifi_bits, GOT_IP | FAILED);
    esp_wifi_set_config(WIFI_IF_STA, &sta);
    esp_wifi_connect();
    EventBits_t b = xEventGroupWaitBits(wifi_bits, GOT_IP | FAILED, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    xSemaphoreGive(join_lock);
    return b & GOT_IP;
}

bool wifi_connected(void)
{
    return xEventGroupGetBits(wifi_bits) & GOT_IP;
}

int wifi_scan_json(char *out, size_t len)
{
    wifi_scan_config_t sc = { .show_hidden = false };
    if (esp_wifi_scan_start(&sc, true) != ESP_OK) return snprintf(out, len, "[]");
    uint16_t n = 20;
    wifi_ap_record_t recs[20];
    esp_wifi_scan_get_ap_records(&n, recs);
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        if (!recs[i].ssid[0]) continue;
        bool dup = false;   // one line per network name, the strongest first (records are sorted)
        for (int j = 0; j < i; j++) dup |= !strcmp((char *)recs[j].ssid, (char *)recs[i].ssid);
        if (dup) continue;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "ssid", (char *)recs[i].ssid);
        cJSON_AddNumberToObject(o, "rssi", recs[i].rssi);
        cJSON_AddBoolToObject(o, "secure", recs[i].authmode != WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(arr, o);
    }
    int w = cJSON_PrintPreallocated(arr, out, len, false) ? strlen(out) : snprintf(out, len, "[]");
    cJSON_Delete(arr);
    return w;
}

// POST {url}/api/box/pair. On success the id and key land in conf; otherwise `why` says it in one sentence.
bool pair_box(const char *code, char *why, size_t why_len)
{
    char url[160], body[384], resp[512] = {0};
    snprintf(url, sizeof url, "%s/api/box/pair", conf.url);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "code", code);
    cJSON_AddStringToObject(o, "room", conf.room);
    cJSON_AddStringToObject(o, "type", BOX_TYPE);
    cJSON_AddStringToObject(o, "firmware", esp_app_get_description()->version);
    cJSON_AddItemToObject(o, "capabilities", cJSON_Parse(BOX_CAPS));
    cJSON_PrintPreallocated(o, body, sizeof body, false);
    cJSON_Delete(o);

    esp_http_client_config_t hc = { .url = url, .method = HTTP_METHOD_POST, .timeout_ms = 10000,
                                    .crt_bundle_attach = esp_crt_bundle_attach };
    esp_http_client_handle_t c = esp_http_client_init(&hc);
    esp_http_client_set_header(c, "Content-Type", "application/json");
    int status = -1;
    if (esp_http_client_open(c, strlen(body)) == ESP_OK) {
        esp_http_client_write(c, body, strlen(body));
        esp_http_client_fetch_headers(c);
        status = esp_http_client_get_status_code(c);
        esp_http_client_read_response(c, resp, sizeof resp - 1);
    }
    esp_http_client_cleanup(c);

    if (status == 200) {
        cJSON *r = cJSON_Parse(resp);
        cJSON *id = cJSON_GetObjectItem(r, "id"), *key = cJSON_GetObjectItem(r, "key"), *name = cJSON_GetObjectItem(r, "name");
        bool ok = cJSON_IsString(id) && cJSON_IsString(key);
        if (ok) {
            strlcpy(conf.id, id->valuestring, sizeof conf.id);
            strlcpy(conf.key, key->valuestring, sizeof conf.key);
            if (cJSON_IsString(name)) strlcpy(conf.room, name->valuestring, sizeof conf.room);
        }
        cJSON_Delete(r);
        if (ok) return true;
        snprintf(why, why_len, "Clavarium a répondu, mais sans clé : vérifiez l'adresse.");
    } else if (status == 403) {
        snprintf(why, why_len, "Code d'appairage refusé ou expiré : demandez-en un nouveau dans Clavarium.");
    } else if (status == 429) {
        snprintf(why, why_len, "Trop d'essais : attendez une minute.");
    } else if (status < 0) {
        snprintf(why, why_len, "Clavarium est injoignable à cette adresse.");
    } else {
        snprintf(why, why_len, "Clavarium a refusé (erreur %d).", status);
    }
    return false;
}

/* ---- the setup access point ---- */

extern const char portal_html[] asm("_binary_portal_html_start");
static char why_text[160];
static volatile int job;   // 0 idle, 1 working, 2 done, 3 failed
static char form_ssid[33], form_pass[65], form_code[16];

static void url_decode(char *s)
{
    char *o = s;
    for (; *s; s++, o++) {
        if (*s == '+') *o = ' ';
        else if (*s == '%' && isxdigit((int)s[1]) && isxdigit((int)s[2])) {
            char h[3] = { s[1], s[2], 0 };
            *o = (char)strtol(h, NULL, 16);
            s += 2;
        } else *o = *s;
    }
    *o = 0;
}

static void field(const char *body, const char *name, char *out, size_t len)
{
    char tmp[200];
    out[0] = 0;
    if (httpd_query_key_value(body, name, tmp, sizeof tmp) == ESP_OK) {
        url_decode(tmp);
        strlcpy(out, tmp, len);
    }
}

static void setup_job(void *arg)
{
    char why[160] = "";
    if (!wifi_join(form_ssid, form_pass, 20000)) {
        snprintf(why, sizeof why, last_reason == WIFI_REASON_NO_AP_FOUND
                 ? "Le Wi-Fi « %s » est introuvable." : "Connexion au Wi-Fi « %s » impossible : vérifiez le mot de passe.", form_ssid);
    } else if (pair_box(form_code, why, sizeof why)) {
        strlcpy(conf.ssid, form_ssid, sizeof conf.ssid);
        strlcpy(conf.pass, form_pass, sizeof conf.pass);
        store_save();
        job = 2;
        leds_set(LED_SOLID, 0x00c040);
        vTaskDelay(pdMS_TO_TICKS(4000));   // the page has time to say it worked
        esp_restart();
    }
    memset(form_pass, 0, sizeof form_pass);
    strlcpy(why_text, why, sizeof why_text);
    job = 3;
    vTaskDelete(NULL);
}

static esp_err_t send_json(httpd_req_t *r, const char *json)
{
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    return httpd_resp_sendstr(r, json);
}

static esp_err_t page(httpd_req_t *r)
{
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    return httpd_resp_sendstr(r, portal_html);
}

static esp_err_t info(httpd_req_t *r)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "firmware", esp_app_get_description()->version);
    cJSON_AddStringToObject(o, "url", conf.url);
    cJSON_AddStringToObject(o, "room", conf.room);
    cJSON_AddStringToObject(o, "ssid", conf.ssid);
    cJSON_AddNumberToObject(o, "job", job);
    cJSON_AddStringToObject(o, "why", why_text);
    char *s = cJSON_PrintUnformatted(o);
    send_json(r, s);
    cJSON_free(s);
    cJSON_Delete(o);
    return ESP_OK;
}

static esp_err_t scan(httpd_req_t *r)
{
    static char buf[2048];
    wifi_scan_json(buf, sizeof buf);
    return send_json(r, buf);
}

static esp_err_t save(httpd_req_t *r)
{
    char body[600];
    int n = r->content_len < sizeof body - 1 ? r->content_len : sizeof body - 1;
    int got = httpd_req_recv(r, body, n);
    if (got <= 0 || job == 1) return send_json(r, "{\"ok\":false}");
    body[got] = 0;
    field(body, "ssid", form_ssid, sizeof form_ssid);
    field(body, "pass", form_pass, sizeof form_pass);
    field(body, "url", conf.url, sizeof conf.url);
    field(body, "room", conf.room, sizeof conf.room);
    field(body, "code", form_code, sizeof form_code);
    memset(body, 0, sizeof body);
    size_t l = strlen(conf.url);
    while (l && conf.url[l - 1] == '/') conf.url[--l] = 0;
    if (!form_ssid[0] || !conf.url[0] || !form_code[0]) return send_json(r, "{\"ok\":false}");
    job = 1;
    why_text[0] = 0;
    xTaskCreate(setup_job, "setup", 8192, NULL, 5, NULL);
    return send_json(r, "{\"ok\":true}");
}

static esp_err_t elsewhere(httpd_req_t *r, httpd_err_code_t err)
{
    httpd_resp_set_status(r, "302 Found");
    static char where[32];   // the access point's own address, as the DNS answers it
    esp_netif_ip_info_t ip;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ip);
    snprintf(where, sizeof where, "http://" IPSTR "/", IP2STR(&ip.ip));
    httpd_resp_set_hdr(r, "Location", where);
    return httpd_resp_send(r, NULL, 0);
}

void portal_run(const char *why)
{
    strlcpy(why_text, why ? why : "", sizeof why_text);
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    wifi_config_t ap = { .ap = { .channel = 6, .max_connection = 4, .authmode = WIFI_AUTH_OPEN } };
    int l = snprintf((char *)ap.ap.ssid, sizeof ap.ap.ssid, "Boitier-%02X%02X", mac[4], mac[5]);
    ap.ap.ssid_len = l;
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_AP, &ap);
    esp_wifi_start();
    ESP_LOGI(TAG, "setup access point %s", ap.ap.ssid);
    leds_set(LED_PULSE, 0x0040ff);
    dns_start();

    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    hc.stack_size = 8192;
    httpd_handle_t srv;
    ESP_ERROR_CHECK(httpd_start(&srv, &hc));
    httpd_uri_t routes[] = {
        { .uri = "/", .method = HTTP_GET, .handler = page },
        { .uri = "/info", .method = HTTP_GET, .handler = info },
        { .uri = "/scan", .method = HTTP_GET, .handler = scan },
        { .uri = "/save", .method = HTTP_POST, .handler = save },
    };
    for (int i = 0; i < 4; i++) httpd_register_uri_handler(srv, &routes[i]);
    httpd_register_err_handler(srv, HTTPD_404_NOT_FOUND, elsewhere);
    for (;;) vTaskDelay(portMAX_DELAY);
}
