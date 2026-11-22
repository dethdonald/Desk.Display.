#include "sound.h"
#include "settings.h"
#include "logbuf.h"
#include "ui.h"
#include <ESP_I2S.h>
extern "C" {
#include "es8311.h"
}

// Pins from Waveshare's ES8311 example for this board
#define I2S_MCK_IO 42
#define I2S_BCK_IO 9
#define I2S_DI_IO 10
#define I2S_WS_IO 45
#define I2S_DO_IO 8
#define PA_EN 46
#define RATE 16000

static I2SClass s_i2s;
static es8311_handle_t s_codec = nullptr;
static QueueHandle_t s_q = nullptr;
static bool s_ok = false;
static const char *s_status = "not started";
static volatile bool s_enabled = true, s_onPage = true, s_onRotate = true, s_quietNight = true;
static volatile int s_volume = 60;

// One sine "blip": frequency glide with a fast attack and exponential decay.
static size_t synth(int16_t *buf, size_t maxFrames, float f0, float f1, int ms, float amp) {
  size_t n = min(maxFrames, (size_t)(RATE * ms / 1000));
  float phase = 0;
  for (size_t i = 0; i < n; i++) {
    float t = (float)i / n;
    float f = f0 + (f1 - f0) * t;
    phase += 2.0f * PI * f / RATE;
    float env = min(1.0f, i / (RATE * 0.003f)) * expf(-4.0f * t);   // 3 ms attack, smooth decay
    int16_t s = (int16_t)(sinf(phase) * env * amp * 32767.0f);
    buf[2 * i] = s;
    buf[2 * i + 1] = s;
  }
  return n;
}

static void task(void *) {
  const size_t maxFrames = RATE / 4;                       // 250 ms worth
  int16_t *buf = (int16_t *)heap_caps_malloc(maxFrames * 4, MALLOC_CAP_SPIRAM);
  int16_t *silence = (int16_t *)calloc(256, 4);
  SoundKind k;
  for (;;) {
    if (xQueueReceive(s_q, &k, portMAX_DELAY) != pdTRUE || !buf) continue;
    float amp = 0.35f;
    size_t n = 0;
    if (k == SND_PAGE) n = synth(buf, maxFrames, 1500, 2100, 45, amp);
    else if (k == SND_ROTATE) {
      n = synth(buf, maxFrames, 900, 1200, 70, amp);
      n += synth(buf + 2 * n, maxFrames - n, 1350, 1800, 90, amp);
    } else n = synth(buf, maxFrames, 880, 1320, 160, amp);
    digitalWrite(PA_EN, HIGH);
    s_i2s.write((const uint8_t *)silence, 256 * 4);          // let the amplifier settle (avoids a pop)
    s_i2s.write((const uint8_t *)buf, n * 4);
    s_i2s.write((const uint8_t *)silence, 256 * 4);
    vTaskDelay(pdMS_TO_TICKS(30));
    if (uxQueueMessagesWaiting(s_q) == 0) digitalWrite(PA_EN, LOW);
  }
}

void soundInit() {
  pinMode(PA_EN, OUTPUT);
  digitalWrite(PA_EN, LOW);
  s_i2s.setPins(I2S_BCK_IO, I2S_WS_IO, I2S_DO_IO, I2S_DI_IO, I2S_MCK_IO);
  if (!s_i2s.begin(I2S_MODE_STD, RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
    LOGI("sound: I2S init failed");
    s_status = "I2S audio bus failed to start";
    return;
  }
  s_codec = es8311_create(0, ES8311_ADDRESS_0);
  if (!s_codec) { LOGI("sound: codec not found"); s_status = "audio chip (ES8311) not found"; return; }
  const es8311_clock_config_t clk = {.mclk_inverted = false, .sclk_inverted = false, .mclk_from_mclk_pin = true,
                                     .mclk_frequency = RATE * 256, .sample_frequency = RATE};
  if (es8311_init(s_codec, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) { LOGI("sound: codec init failed"); s_status = "audio chip (ES8311) did not respond"; return; }
  es8311_sample_frequency_config(s_codec, clk.mclk_frequency, clk.sample_frequency);
  es8311_microphone_config(s_codec, false);
  s_q = xQueueCreate(4, sizeof(SoundKind));
  xTaskCreatePinnedToCore(task, "sound", 4096, nullptr, 2, nullptr, 0);
  s_ok = true;
  s_status = "ready";
  soundApplySettings();
  LOGI("sound: ready");
}

void soundApplySettings() {
  JsonObject s = S("sound");
  s_enabled = s["enabled"] | true;
  s_volume = constrain((int)(s["volume"] | 60), 0, 100);
  s_onPage = s["page_change"] | true;
  s_onRotate = s["rotation"] | true;
  s_quietNight = s["quiet_at_night"] | true;
  if (s_ok) es8311_voice_volume_set(s_codec, s_volume, nullptr);
}

const char *soundStatus() { return s_status; }

void soundPlay(SoundKind kind) {
  if (!s_ok || !s_q) return;
  if (kind != SND_TEST) {
    if (!s_enabled || s_volume == 0) return;
    if (kind == SND_PAGE && !s_onPage) return;
    if (kind == SND_ROTATE && !s_onRotate) return;
    if (s_quietNight && uiNight()) return;
    if (uiSleeping()) return;
  }
  LOGI("sound: play %d", (int)kind);
  xQueueSend(s_q, &kind, 0);
}
