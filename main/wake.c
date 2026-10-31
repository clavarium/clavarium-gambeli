// The wake word ("Jarvis", Espressif's WakeNet9), heard on the chip itself: nothing leaves the box
// before it is said. The microphone task feeds the front end; this task waits for the word.
#include "box.h"
#include "esp_afe_sr_models.h"
#include "esp_afe_config.h"
#include "model_path.h"
#include "esp_wn_iface.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include <string.h>

static const char *TAG = "wake";
static const esp_afe_sr_iface_t *afe;
static esp_afe_sr_data_t *data;
static int chunk, channels = 4;
static int16_t *buf;
static int have;

static void listen_for_word(void *arg)
{
    for (;;) {
        afe_fetch_result_t *r = afe->fetch(data);
        if (r && r->wakeup_state == WAKENET_DETECTED) {
            ESP_LOGI(TAG, "wake word");
            post_event(EV_WAKE, 0, NULL);
        }
    }
}

bool wake_start(void)
{
    srmodel_list_t *models = esp_srmodel_init("model");
    if (!models) { ESP_LOGW(TAG, "no models: no wake word"); return false; }
    /* Both microphones and the playback reference: the echo of its own voice is taken out, and the
       two microphones separate the voice from the room (found 31-10: one microphone, no echo
       cancelling, "Jarvis" was heard now and then only). */
    afe_config_t *cfg = afe_config_init("RMNM", models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    if (!cfg) return false;
    cfg->wakenet_mode = DET_MODE_2CH_95;
    cfg->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    afe = esp_afe_handle_from_config(cfg);
    data = afe->create_from_config(cfg);
    afe_config_free(cfg);
    if (!data) return false;
    chunk = afe->get_feed_chunksize(data);
    channels = afe->get_feed_channel_num(data);
    buf = heap_caps_malloc(chunk * channels * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    xTaskCreatePinnedToCore(listen_for_word, "wake", 6144, NULL, 5, NULL, 1);
    ESP_LOGI(TAG, "listening for the wake word, %d samples a chunk", chunk);
    return true;
}

void wake_feed(const int16_t *four, int n)
{
    if (!data) return;
    while (n > 0) {
        int take = chunk - have < n ? chunk - have : n;
        memcpy(buf + have * channels, four, take * channels * sizeof(int16_t));
        have += take; four += take * channels; n -= take;
        if (have == chunk) { afe->feed(data, buf); have = 0; }
    }
}
