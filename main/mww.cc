// The wake word with microWakeWord (Home Assistant's): a small streaming model run on the chip by
// TensorFlow Lite Micro, over the same 40-band features it was trained on. Nothing leaves the box
// before the word is heard. The model is built into the program: an update over Wi-Fi changes it.
// Port of ESPHome's micro_wake_word (Apache-2.0): the features' settings and their scaling are theirs.
#include <cstring>
#include <algorithm>
extern "C" {
#include "box.h"
}
#include "frontend.h"
#include "frontend_util.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_allocator.h"
#include "tensorflow/lite/micro/micro_resource_variable.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern const uint8_t model_start[] asm("_binary_wake_tflite_start");
extern const uint8_t model_end[] asm("_binary_wake_tflite_end");

static const char *TAG = "mww";
static const int FEATURES = 40;
static const int CUTOFF_QUIET = (int)(0.90f * 255);     // from the model's own manifest (main/wake.json: "Mésange", trained by its owner, 31-10)
static const int WINDOW = 5;
static const size_t ARENA = 32 * 1024;

static FrontendConfig config;
static tflite::MicroMutableOpResolver<20> ops;
static bool ready;

/* TWO DETECTORS, the same model (31-10, the owner's idea): one hears the microphone, the other what the box
   is about to play. The word heard by both at once is the box's own voice: ignored. Heard by the microphone
   alone: someone said it -- even over the box speaking, which may then be interrupted by voice. */
struct Detector {
    FrontendState state;
    tflite::MicroInterpreter *interpreter = nullptr;
    uint8_t recent[5] = {0};
    int last = 0, stride_step = 0, ignore = 0;
};
static Detector mic, out;
static volatile TickType_t out_heard;   // when the box's own sound last said the word
static volatile int cutoff = CUTOFF_QUIET;
static const int CUTOFF_OWN_SOUND = (int)(0.99f * 255);
/* 0: quiet, its own cutoff; 1: the box plays (its voice, a stream), near-certain only and checked against what
   it plays; 2: deaf to the word (kept for a firmware without the second detector). The button always works. */
extern "C" void mww_strict(int level) { cutoff = level == 2 ? 255 * 2 : level == 1 ? CUTOFF_OWN_SOUND : CUTOFF_QUIET; }

static bool add_ops()
{
    return ops.AddCallOnce() == kTfLiteOk && ops.AddVarHandle() == kTfLiteOk && ops.AddReshape() == kTfLiteOk &&
           ops.AddReadVariable() == kTfLiteOk && ops.AddStridedSlice() == kTfLiteOk && ops.AddConcatenation() == kTfLiteOk &&
           ops.AddAssignVariable() == kTfLiteOk && ops.AddConv2D() == kTfLiteOk && ops.AddMul() == kTfLiteOk &&
           ops.AddAdd() == kTfLiteOk && ops.AddMean() == kTfLiteOk && ops.AddFullyConnected() == kTfLiteOk &&
           ops.AddLogistic() == kTfLiteOk && ops.AddQuantize() == kTfLiteOk && ops.AddDepthwiseConv2D() == kTfLiteOk &&
           ops.AddAveragePool2D() == kTfLiteOk && ops.AddMaxPool2D() == kTfLiteOk && ops.AddPad() == kTfLiteOk &&
           ops.AddPack() == kTfLiteOk && ops.AddSplitV() == kTfLiteOk;
}

static bool make(Detector &d, uint32_t caps)
{
    if (!FrontendPopulateState(&config, &d.state, BOX_RATE)) { ESP_LOGE(TAG, "features: no memory"); return false; }
    const tflite::Model *model = tflite::GetModel(model_start);
    if (model->version() != TFLITE_SCHEMA_VERSION) { ESP_LOGE(TAG, "model schema"); return false; }
    uint8_t *arena = (uint8_t *)heap_caps_malloc(ARENA, caps | MALLOC_CAP_8BIT);
    uint8_t *var_arena = (uint8_t *)heap_caps_malloc(1024, caps | MALLOC_CAP_8BIT);
    if (!arena || !var_arena) { ESP_LOGE(TAG, "arena: no memory"); return false; }
    tflite::MicroAllocator *ma = tflite::MicroAllocator::Create(var_arena, 1024);
    tflite::MicroResourceVariables *mrv = tflite::MicroResourceVariables::Create(ma, 20);
    d.interpreter = new tflite::MicroInterpreter(model, ops, arena, ARENA, mrv);
    if (d.interpreter->AllocateTensors() != kTfLiteOk) { ESP_LOGE(TAG, "tensors"); return false; }
    d.ignore = -WINDOW * 10;   // nothing heard in its first second
    return true;
}

extern "C" bool mww_start(void)
{
    config.window.size_ms = 30;
    config.window.step_size_ms = 10;
    config.filterbank.num_channels = FEATURES;
    config.filterbank.lower_band_limit = 125.0f;
    config.filterbank.upper_band_limit = 7500.0f;
    config.noise_reduction.smoothing_bits = 10;
    config.noise_reduction.even_smoothing = 0.025f;
    config.noise_reduction.odd_smoothing = 0.06f;
    config.noise_reduction.min_signal_remaining = 0.05f;
    config.pcan_gain_control.enable_pcan = 1;
    config.pcan_gain_control.strength = 0.95f;
    config.pcan_gain_control.offset = 80.0f;
    config.pcan_gain_control.gain_bits = 21;
    config.log_scale.enable_log = 1;
    config.log_scale.scale_shift = 6;
    if (!add_ops()) return false;
    if (!make(mic, MALLOC_CAP_INTERNAL)) return false;
    if (!make(out, MALLOC_CAP_SPIRAM)) ESP_LOGW(TAG, "no second detector: deaf while it speaks instead");
    ready = true;
    ESP_LOGI(TAG, "listening for the wake word (stride %d)", mic.interpreter->input(0)->dims->data[1]);
    return true;
}

/* One step of a detector: true when the word is heard (the mean of the last WINDOW over the cutoff). */
static bool infer(Detector &d, const int8_t *features, int bar)
{
    TfLiteTensor *input = d.interpreter->input(0);
    int stride = input->dims->data[1];
    d.stride_step %= stride;
    memmove(tflite::GetTensorData<int8_t>(input) + FEATURES * d.stride_step, features, FEATURES);
    if (++d.stride_step < stride) return false;
    if (d.interpreter->Invoke() != kTfLiteOk) return false;
    d.last = (d.last + 1) % WINDOW;
    d.recent[d.last] = d.interpreter->output(0)->data.uint8[0];
    if (d.ignore < 0) { d.ignore++; return false; }
    int sum = 0;
    for (int i = 0; i < WINDOW; i++) sum += d.recent[i];
    if (sum <= bar * WINDOW) return false;
    memset(d.recent, 0, sizeof d.recent);
    d.ignore = -WINDOW * 3;   // not twice for one word
    return true;
}

static void feed(Detector &d, const int16_t *pcm, int n, bool microphone)
{
    while (n > 0) {
        size_t used = 0;
        FrontendOutput o = FrontendProcessSamples(&d.state, pcm, n, &used);
        pcm += used; n -= used;
        if (o.size == FEATURES) {
            int8_t f[FEATURES];
            for (int i = 0; i < FEATURES; i++) {
                int32_t v = ((o.values[i] * 256) + 333) / 666 - 128;
                f[i] = (int8_t)std::min<int32_t>(127, std::max<int32_t>(-128, v));
            }
            if (!microphone) { if (infer(d, f, CUTOFF_QUIET)) out_heard = xTaskGetTickCount(); }
            else if (infer(d, f, cutoff)) {
                bool own = out.interpreter && xTaskGetTickCount() - out_heard < pdMS_TO_TICKS(1500);
                ESP_LOGI(TAG, "wake word%s", own ? ": its own voice, ignored" : "");
                if (!own) post_event(EV_WAKE, 0, NULL);
            }
        }
        if (used == 0) break;
    }
}

extern "C" void mww_feed(const int16_t *pcm, int n) { if (ready) feed(mic, pcm, n, true); }
/* What the box is about to play (sounds.c, before the amplifier). */
extern "C" void mww_feed_out(const int16_t *pcm, int n) { if (ready && out.interpreter) feed(out, pcm, n, false); }
extern "C" bool mww_two(void) { return ready && out.interpreter; }

/* A fresh start after an exchange: the features' noise estimate had learnt the box's own voice (and the
   model's streaming state, a sentence ago), so the next word was heard badly (31-10). */
extern "C" void mww_reset(void)
{
    if (!ready) return;
    FrontendReset(&mic.state);
    mic.interpreter->Reset();
    memset(mic.recent, 0, sizeof mic.recent);
    mic.stride_step = 0;
    mic.ignore = -WINDOW * 4;
}
