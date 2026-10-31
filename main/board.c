// Waveshare ESP32-S3-AUDIO-Board: ES7210 (microphones) and ES8311 (speaker) on one I2S bus,
// the amplifier and the keys on a TCA9555 expander. Pins from the board's own examples.
#include "box.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_io_expander_tca95xx_16bit.h"
#include "esp_log.h"
#include <stdlib.h>
#include "esp_heap_caps.h"

#define PIN_SDA 11
#define PIN_SCL 10
#define PIN_MCLK 12
#define PIN_BCLK 13
#define PIN_WS 14
#define PIN_DIN 15
#define PIN_DOUT 16
#define EXIO_AMP IO_EXPANDER_PIN_NUM_8
#define EXIO_K1 IO_EXPANDER_PIN_NUM_9
#define EXIO_K2 IO_EXPANDER_PIN_NUM_10
#define EXIO_K3 IO_EXPANDER_PIN_NUM_11

static const char *TAG = "board";
static i2c_master_bus_handle_t i2c;
static esp_io_expander_handle_t expander;
static i2s_chan_handle_t tx, rx;
static esp_codec_dev_handle_t mic, spk;
static int32_t *frame;   // stereo 32-bit frames, both directions (one at a time per direction)
static int32_t *out;

void board_init(void)
{
    i2c_master_bus_config_t bus = {
        .i2c_port = 0, .sda_io_num = PIN_SDA, .scl_io_num = PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &i2c));

    ESP_ERROR_CHECK(esp_io_expander_new_i2c_tca95xx_16bit(i2c, ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_000, &expander));
    esp_io_expander_set_dir(expander, EXIO_AMP, IO_EXPANDER_OUTPUT);
    esp_io_expander_set_dir(expander, EXIO_K1 | EXIO_K2 | EXIO_K3, IO_EXPANDER_INPUT);
    board_amp(false);

    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan.auto_clear = true;   // silence, not the last buffer again, when nothing is played
    ESP_ERROR_CHECK(i2s_new_channel(&chan, &tx, &rx));
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(BOX_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = { .mclk = PIN_MCLK, .bclk = PIN_BCLK, .ws = PIN_WS, .dout = PIN_DOUT, .din = PIN_DIN },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &std));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx, &std));
    ESP_ERROR_CHECK(i2s_channel_enable(tx));
    ESP_ERROR_CHECK(i2s_channel_enable(rx));

    esp_codec_dev_sample_info_t fs = { .sample_rate = BOX_RATE, .channel = 2, .bits_per_sample = 32 };

    audio_codec_i2s_cfg_t in_i2s = { .port = I2S_NUM_1, .rx_handle = rx };
    audio_codec_i2c_cfg_t in_i2c = { .addr = ES7210_CODEC_DEFAULT_ADDR, .bus_handle = i2c };
    es7210_codec_cfg_t es7210 = {
        .ctrl_if = audio_codec_new_i2c_ctrl(&in_i2c),
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4,
    };
    esp_codec_dev_cfg_t in = {
        .codec_if = es7210_codec_new(&es7210), .data_if = audio_codec_new_i2s_data(&in_i2s),
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    mic = esp_codec_dev_new(&in);
    esp_codec_dev_open(mic, &fs);
    esp_codec_dev_set_in_gain(mic, 30.0);

    audio_codec_i2s_cfg_t out_i2s = { .port = I2S_NUM_1, .tx_handle = tx };
    audio_codec_i2c_cfg_t out_i2c = { .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = i2c };
    es8311_codec_cfg_t es8311 = {
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC, .ctrl_if = audio_codec_new_i2c_ctrl(&out_i2c),
        .gpio_if = audio_codec_new_gpio(), .pa_pin = -1, .use_mclk = false,
    };
    esp_codec_dev_cfg_t outc = {
        .codec_if = es8311_codec_new(&es8311), .data_if = audio_codec_new_i2s_data(&out_i2s),
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    };
    spk = esp_codec_dev_new(&outc);
    esp_codec_dev_open(spk, &fs);

    frame = heap_caps_malloc(BOX_CHUNK * 2 * sizeof(int32_t), MALLOC_CAP_DMA);
    out = heap_caps_malloc(BOX_CHUNK * 2 * sizeof(int32_t), MALLOC_CAP_DMA);
    ESP_LOGI(TAG, "audio ready");
}

int board_mic_read(int16_t *mono, int samples)
{
    if (samples > BOX_CHUNK) samples = BOX_CHUNK;
    if (esp_codec_dev_read(mic, frame, samples * 2 * sizeof(int32_t)) != ESP_CODEC_DEV_OK) return 0;
    for (int i = 0; i < samples; i++) mono[i] = (int16_t)(frame[2 * i] >> 16);   // first microphone
    return samples;
}

void board_play(const int16_t *mono, int samples)
{
    while (samples > 0) {
        int n = samples > BOX_CHUNK ? BOX_CHUNK : samples;
        for (int i = 0; i < n; i++) out[2 * i] = out[2 * i + 1] = (int32_t)mono[i] << 16;
        esp_codec_dev_write(spk, out, n * 2 * sizeof(int32_t));
        mono += n;
        samples -= n;
    }
}

void board_volume(int level)
{
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    esp_codec_dev_set_out_vol(spk, level);
}

void board_amp(bool on)
{
    esp_io_expander_set_level(expander, EXIO_AMP, on ? 1 : 0);
}

uint32_t board_keys(void)
{
    uint32_t level = 0;
    if (esp_io_expander_get_level(expander, EXIO_K1 | EXIO_K2 | EXIO_K3, &level) != ESP_OK) return 0;
    uint32_t keys = 0;   // the keys pull their line low
    if (!(level & EXIO_K1)) keys |= KEY_MAIN;
    if (!(level & EXIO_K2)) keys |= KEY_MUTE;
    if (!(level & EXIO_K3)) keys |= KEY_THIRD;
    return keys;
}
