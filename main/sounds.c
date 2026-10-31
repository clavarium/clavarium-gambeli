// Playback: one task drains a stream of PCM to the speaker. Cues are synthesised into it;
// Clavarium's speech is fed into it as it arrives. The amplifier is on only while playing.
#include "box.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include "esp_heap_caps.h"
#include <math.h>
#include <string.h>

#define STREAM_BYTES (64 * 1024)   // 2 s of speech ahead
static StreamBufferHandle_t stream;
static volatile bool playing;

static void player(void *arg)
{
    static int16_t buf[BOX_CHUNK];
    int idle = 0;
    for (;;) {
        size_t got = xStreamBufferReceive(stream, buf, sizeof buf, pdMS_TO_TICKS(40));
        if (got >= 2) {
            if (!playing) { board_amp(true); playing = true; }
            idle = 0;
            board_play(buf, got / 2);
        } else if (playing && ++idle > 12) {   // half a second of nothing: amplifier off
            board_amp(false);
            playing = false;
        }
    }
}

void sounds_init(void)
{
    static StaticStreamBuffer_t meta;
    uint8_t *mem = heap_caps_malloc(STREAM_BYTES + 1, MALLOC_CAP_SPIRAM);
    stream = xStreamBufferCreateStatic(STREAM_BYTES, 2, mem, &meta);
    xTaskCreate(player, "player", 4096, NULL, 6, NULL);
}

void speech_feed(const uint8_t *pcm, size_t len)
{
    xStreamBufferSend(stream, pcm, len & ~(size_t)1, pdMS_TO_TICKS(5000));
}

void speech_stop(void)
{
    xStreamBufferReset(stream);
}

bool speech_idle(void)
{
    return xStreamBufferIsEmpty(stream) && !playing;
}

// A note: frequency glide from f0 to f1, with a short attack and an exponential fade.
static void note(float f0, float f1, int ms, float gain)
{
    int n = BOX_RATE * ms / 1000;
    int16_t chunk[BOX_CHUNK];
    float phase = 0;
    for (int i = 0; i < n;) {
        int m = n - i > BOX_CHUNK ? BOX_CHUNK : n - i;
        for (int j = 0; j < m; j++, i++) {
            float x = (float)i / n;
            float f = f0 + (f1 - f0) * x;
            phase += 2 * (float)M_PI * f / BOX_RATE;
            float env = (i < 160 ? i / 160.0f : 1) * expf(-3.5f * x);
            chunk[j] = (int16_t)(gain * 32767 * env * sinf(phase));
        }
        xStreamBufferSend(stream, chunk, m * 2, pdMS_TO_TICKS(1000));
    }
}

static void rest(int ms)
{
    int16_t zero[BOX_CHUNK] = {0};
    for (int n = BOX_RATE * ms / 1000; n > 0; n -= BOX_CHUNK)
        xStreamBufferSend(stream, zero, (n > BOX_CHUNK ? BOX_CHUNK : n) * 2, pdMS_TO_TICKS(1000));
}

void sounds_play(sound_t s)
{
    switch (s) {
    case SND_DING:  note(880, 1320, 180, 0.22f); break;                              // rising
    case SND_TIC:   note(1500, 1400, 45, 0.2f); break;                               // soft tick
    case SND_ERROR: note(660, 640, 160, 0.16f); note(523, 510, 240, 0.14f); break;   // soft: an alert sounds, an error only says so   // two notes down
    case SND_CHIME: note(1047, 1047, 300, 0.35f); note(784, 784, 300, 0.3f); note(1047, 1047, 450, 0.3f); rest(200); break;
    case SND_CLICK: note(2400, 2000, 15, 0.25f); break;
    case SND_DONE:  note(784, 784, 120, 0.18f); note(1175, 1175, 220, 0.18f); break;
    }
}
