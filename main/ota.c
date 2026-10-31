// Update over the air: download, check the sha256 Clavarium gave, install, and boot it next time.
// The bootloader's signature check (Secure Boot) is what refuses an image not signed by us.
#include "box.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "mbedtls/sha256.h"
#include <string.h>
#include <stdio.h>

bool ota_install(const char *url, const char *sha256, char *detail, size_t len)
{
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) { snprintf(detail, len, "no update partition"); return false; }
    esp_http_client_config_t hc = { .url = url, .timeout_ms = 15000, .crt_bundle_attach = esp_crt_bundle_attach };
    esp_http_client_handle_t c = esp_http_client_init(&hc);
    if (esp_http_client_open(c, 0) != ESP_OK) { esp_http_client_cleanup(c); snprintf(detail, len, "download failed"); return false; }
    esp_http_client_fetch_headers(c);
    if (esp_http_client_get_status_code(c) != 200) {
        snprintf(detail, len, "download refused (%d)", esp_http_client_get_status_code(c));
        esp_http_client_cleanup(c);
        return false;
    }
    esp_ota_handle_t ota;
    if (esp_ota_begin(part, OTA_SIZE_UNKNOWN, &ota) != ESP_OK) { esp_http_client_cleanup(c); snprintf(detail, len, "cannot write"); return false; }
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    static char buf[4096];
    bool ok = true;
    int n;
    while ((n = esp_http_client_read(c, buf, sizeof buf)) > 0) {
        mbedtls_sha256_update(&sha, (unsigned char *)buf, n);
        if (esp_ota_write(ota, buf, n) != ESP_OK) { ok = false; snprintf(detail, len, "write failed"); break; }
    }
    if (n < 0) { ok = false; snprintf(detail, len, "download cut"); }
    esp_http_client_cleanup(c);
    unsigned char sum[32];
    mbedtls_sha256_finish(&sha, sum);
    mbedtls_sha256_free(&sha);
    char hex[65];
    for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", sum[i]);
    if (ok && strcasecmp(hex, sha256) != 0) { ok = false; snprintf(detail, len, "sha256 mismatch"); }
    if (!ok) { esp_ota_abort(ota); return false; }
    if (esp_ota_end(ota) != ESP_OK) { snprintf(detail, len, "image refused"); return false; }
    if (esp_ota_set_boot_partition(part) != ESP_OK) { snprintf(detail, len, "cannot boot it"); return false; }
    return true;
}
