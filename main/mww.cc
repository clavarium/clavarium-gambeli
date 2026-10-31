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

extern const uint8_t model_start[] asm("_binary_wake_tflite_start");
extern const uint8_t model_end[] asm("_binary_wake_tflite_end");

static const char *TAG = "mww";
static const int FEATURES = 40;
static const int CUTOFF = (int)(0.97f * 255);     // from the model's own manifest
static const int WINDOW = 5;
static const size_t ARENA = 32 * 1024;

static FrontendConfig config;
static FrontendState state;
static tflite::MicroMutableOpResolver<20> ops;
static tflite::MicroInterpreter *interpreter;
static uint8_t recent[WINDOW];
static int last, stride_step, ignore;
static bool ready;

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
    if (!FrontendPopulateState(&config, &state, BOX_RATE)) { ESP_LOGE(TAG, "features: no memory"); return false; }
    if (!add_ops()) return false;
    const tflite::Model *model = tflite::GetModel(model_start);
    if (model->version() != TFLITE_SCHEMA_VERSION) { ESP_LOGE(TAG, "model schema"); return false; }
    uint8_t *arena = (uint8_t *)heap_caps_malloc(ARENA, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    uint8_t *var_arena = (uint8_t *)heap_caps_malloc(1024, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!arena || !var_arena) { ESP_LOGE(TAG, "arena: no memory"); return false; }
    tflite::MicroAllocator *ma = tflite::MicroAllocator::Create(var_arena, 1024);
    tflite::MicroResourceVariables *mrv = tflite::MicroResourceVariables::Create(ma, 20);
    interpreter = new tflite::MicroInterpreter(model, ops, arena, ARENA, mrv);
    if (interpreter->AllocateTensors() != kTfLiteOk) { ESP_LOGE(TAG, "tensors"); return false; }
    ignore = -WINDOW * 10;   // nothing heard in its first second
    ready = true;
    ESP_LOGI(TAG, "listening for the wake word (stride %d)", interpreter->input(0)->dims->data[1]);
    return true;
}

static void infer(const int8_t *features)
{
    TfLiteTensor *input = interpreter->input(0);
    int stride = input->dims->data[1];
    stride_step %= stride;
    memmove(tflite::GetTensorData<int8_t>(input) + FEATURES * stride_step, features, FEATURES);
    if (++stride_step < stride) return;
    if (interpreter->Invoke() != kTfLiteOk) return;
    last = (last + 1) % WINDOW;
    recent[last] = interpreter->output(0)->data.uint8[0];
    if (ignore < 0) { ignore++; return; }
    int sum = 0;
    for (int i = 0; i < WINDOW; i++) sum += recent[i];
    if (sum > CUTOFF * WINDOW) {
        ESP_LOGI(TAG, "wake word (%d)", sum / WINDOW);
        memset(recent, 0, sizeof recent);
        ignore = -WINDOW * 3;   // not twice for one word
        post_event(EV_WAKE, 0, NULL);
    }
}

extern "C" void mww_feed(const int16_t *pcm, int n)
{
    if (!ready) return;
    while (n > 0) {
        size_t used = 0;
        FrontendOutput out = FrontendProcessSamples(&state, pcm, n, &used);
        pcm += used; n -= used;
        if (out.size == FEATURES) {
            int8_t f[FEATURES];
            for (int i = 0; i < FEATURES; i++) {
                int32_t v = ((out.values[i] * 256) + 333) / 666 - 128;
                f[i] = (int8_t)std::min<int32_t>(127, std::max<int32_t>(-128, v));
            }
            infer(f);
        }
        if (used == 0) break;
    }
}

/* A fresh start after an exchange: the features' noise estimate had learnt the box's own voice (and the
   model's streaming state, a sentence ago), so the next "Hey Jarvis" was heard badly (31-10). */
extern "C" void mww_reset(void)
{
    if (!ready) return;
    FrontendReset(&state);
    interpreter->Reset();
    memset(recent, 0, sizeof recent);
    stride_step = 0;
    ignore = -WINDOW * 4;
}
