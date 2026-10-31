// A page of the box's state on its own address, and its name on the network (mDNS):
// http://gambeli-XXXX.local/ from a phone on the same Wi-Fi says whether it reaches Clavarium,
// and if not, why. Nothing secret on it: no key, no password.
#include "box.h"
#include "esp_http_server.h"
#include "esp_app_desc.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "mdns.h"
#include <stdio.h>
#include <string.h>

static esp_err_t page(httpd_req_t *r)
{
    static char html[1600];
    esp_netif_ip_info_t ip = {0};
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip);
    const char *p = link_problem();
    snprintf(html, sizeof html,
             "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'><title>Gambeli</title>"
             "<body style='font:16px system-ui;margin:20px;max-width:30rem'><h1>Gambeli · %s</h1>"
             "<p>Firmware %s</p><p>Wi-Fi « %s », signal %d dBm, address " IPSTR "</p>"
             "<p>Clavarium %s : <b>%s</b></p>%s%s%s<p>Tries %d · up %lld min</p>",
             conf.room, esp_app_get_description()->version, conf.ssid, link_rssi(), IP2STR(&ip.ip),
             conf.url, link_up() ? "connected" : "not connected",
             p[0] ? "<p>Last problem: <code>" : "", p, p[0] ? "</code></p>" : "",
             link_tries(), esp_timer_get_time() / 60000000LL);
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    return httpd_resp_sendstr(r, html);
}

void status_start(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char host[24];
    snprintf(host, sizeof host, "gambeli-%02x%02x", mac[4], mac[5]);
    if (mdns_init() == ESP_OK) {
        mdns_hostname_set(host);
        mdns_instance_name_set("Gambeli");
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    }
    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t srv;
    if (httpd_start(&srv, &hc) != ESP_OK) return;
    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = page };
    httpd_register_uri_handler(srv, &root);
}
