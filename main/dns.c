// Captive portal DNS: every name resolves to the box, so a phone opens the setup page by itself.
#include "box.h"
#include "lwip/sockets.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static void serve(void *arg)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = htonl(INADDR_ANY) };
    bind(s, (struct sockaddr *)&addr, sizeof addr);
    esp_netif_ip_info_t ip;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ip);
    uint8_t buf[512];
    for (;;) {
        struct sockaddr_in from;
        socklen_t fl = sizeof from;
        int n = recvfrom(s, buf, sizeof buf - 16, 0, (struct sockaddr *)&from, &fl);
        if (n < 12 || (buf[2] & 0x80)) continue;            // too short, or not a query
        int q = 12;                                          // skip the question's name
        while (q < n && buf[q]) q += buf[q] + 1;
        q += 5;                                              // zero byte, type, class
        if (q > n) continue;
        buf[2] = 0x81; buf[3] = 0x80;                        // response, recursion available
        buf[6] = 0; buf[7] = 1;                              // one answer
        buf[8] = buf[9] = buf[10] = buf[11] = 0;
        uint8_t ans[16] = { 0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0, 30, 0, 4 };   // A record, 30 s, the box
        memcpy(ans + 12, &ip.ip.addr, 4);
        memcpy(buf + q, ans, sizeof ans);
        sendto(s, buf, q + sizeof ans, 0, (struct sockaddr *)&from, fl);
    }
}

void dns_start(void)
{
    xTaskCreate(serve, "dns", 3072, NULL, 4, NULL);
}
