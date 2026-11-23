// Desk Display — firmware for the Waveshare ESP32-S3-Touch-AMOLED-2.16
// Clock, Claude plan usage and weather on a layout you design in the device's own web UI.
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_ota_ops.h>
#include <esp_core_dump.h>
#include <esp_system.h>
#include "config.h"
#include "logbuf.h"
#include "settings.h"
#include "datastore.h"
#include "cmdq.h"
#include "hw.h"
#include "ui.h"
#include "input.h"
#include "net.h"
#include "fetcher.h"
#include "web.h"
#include "sound.h"
#include "bambu.h"
#include <esp_heap_caps.h>
#include <mbedtls/platform.h>

// Each TLS connection needs ~40-50 KB. The framework puts that in scarce internal RAM, so with the
// printer link open a second one (GitHub update check / download) fails. Use the 8 MB PSRAM instead.
static void *tlsCalloc(size_t n, size_t sz) {
  void *p = heap_caps_calloc(n, sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : heap_caps_calloc(n, sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
static void tlsFree(void *p) { heap_caps_free(p); }

// Keep a freshly-installed firmware in "pending verify" until it has run healthily for a while.
// If it crashes or reboots before that, the bootloader rolls back to the previous version.
extern "C" bool verifyRollbackLater() { return true; }
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

static bool s_otaPending = false;
static bool s_safeMode = false;

// Boot breadcrumbs survive a crash/reset (RTC memory isn't cleared on a software reset),
// so the next boot can say where the previous one died.
RTC_NOINIT_ATTR static uint32_t s_bcMagic;
RTC_NOINIT_ATTR static char s_bcStep[24];
RTC_NOINIT_ATTR static uint32_t s_bcCrashes;
static const uint32_t BC_MAGIC = 0xDD5AFE01;

static void bootStep(const char *step, bool show = true) {
  strncpy(s_bcStep, step, sizeof(s_bcStep) - 1);
  s_bcStep[sizeof(s_bcStep) - 1] = 0;
  s_bcMagic = BC_MAGIC;
  LOGI("boot: %s", step);
  if (show) hwBootMessage("Desk Display", step);
}

static const char *resetName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "power on";
    case ESP_RST_SW: return "restart";
    case ESP_RST_PANIC: return "crash (panic)";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout (power dip)";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_USB: return "usb reset";
    default: return "other";
  }
}

// Returns true when the previous boot crashed; fills a human-readable report.
static bool previousCrash(String &report) {
  esp_reset_reason_t r = esp_reset_reason();
  bool crashed = r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT || r == ESP_RST_BROWNOUT;
  bool valid = s_bcMagic == BC_MAGIC;
  if (!valid) { s_bcCrashes = 0; s_bcStep[0] = 0; }
  if (!crashed) { s_bcCrashes = 0; return false; }
  s_bcCrashes = valid ? s_bcCrashes + 1 : 1;
  char buf[320];
  int n = snprintf(buf, sizeof buf, "Last boot crashed\nstep: %s\nreason: %s\ncount: %lu\n",
                   valid && s_bcStep[0] ? s_bcStep : "?", resetName(r), (unsigned long)s_bcCrashes);
  esp_core_dump_summary_t *sum = (esp_core_dump_summary_t *)calloc(1, sizeof(esp_core_dump_summary_t));
  if (sum && esp_core_dump_image_check() == ESP_OK && esp_core_dump_get_summary(sum) == ESP_OK) {
    n += snprintf(buf + n, sizeof buf - n, "task: %.16s\npc: %08lx\n", sum->exc_task, (unsigned long)sum->exc_pc);
    for (int i = 0; i < (int)sum->exc_bt_info.depth && i < 6 && n < (int)sizeof buf - 12; i++)
      n += snprintf(buf + n, sizeof buf - n, "bt: %08lx\n", (unsigned long)sum->exc_bt_info.bt[i]);
  }
  free(sum);
  report = buf;
  return true;
}
static uint32_t s_rebootAt = 0;
static uint32_t s_previewSince = 0;
static int s_nightSched = -1;          // last scheduled night state (-1 unknown)
static bool s_nightOverride = false;

// ---------------------------------------------------------------- hooks used by input.cpp
void sysRefreshData() { fetcherRefreshAll(); }

String sysSettingsUrl() {
  if (netApMode()) return "http://" + netIp() + "/";
  if (netConnected()) return "http://" + netIp() + "/";
  return "http://" + netHostname() + ".local/";
}

void sysResetAdminPassword() {
  gSettings["admin"]["pass"] = "";
  settingsSave();
  webApplySettings();
  LOGI("security: admin password cleared with the BOOT button");
  String url = sysSettingsUrl();
  uiShowQr(url, "Password cleared\n" + url);
}

void sysToggleNight() {
  s_nightOverride = true;
  uiSetNight(!uiNight());
  uiToast(uiNight() ? "Night mode on" : "Night mode off");
}

// ---------------------------------------------------------------- helpers
static int minutesOf(const char *hhmm) {
  int h = 0, m = 0;
  if (!hhmm || sscanf(hhmm, "%d:%d", &h, &m) < 1) return -1;
  return h * 60 + m;
}

static void nightTick() {
  static uint32_t last = 0;
  if (millis() - last < 5000) return;
  last = millis();
  JsonObject n = S("display")["night"];
  if (!(n["enabled"] | true) || !netTimeValid()) {
    if (s_nightSched == 1 && !s_nightOverride) uiSetNight(false);
    s_nightSched = 0;
    return;
  }
  time_t now = time(nullptr);
  struct tm t; localtime_r(&now, &t);
  int cur = t.tm_hour * 60 + t.tm_min;
  int st = minutesOf(n["start"] | "22:30"), en = minutesOf(n["end"] | "07:00");
  bool night = st < en ? (cur >= st && cur < en) : (cur >= st || cur < en);
  if ((int)night != s_nightSched) {
    s_nightSched = night;
    s_nightOverride = false;
    uiSetNight(night);
    if (n["screen_off"] | false) uiSetSleep(night);
  }
}

static void sysKeysTick() {
  static uint32_t last = 0;
  if (millis() - last < 2000) return;
  last = millis();
  dsSetStr("sys.name", (const char *)(S("device")["name"] | "Desk Display"));
  dsSetStr("sys.version", FW_VERSION);
  dsSetStr("sys.ip", netIp());
  dsSetStr("sys.host", netHostname() + ".local");
  dsSetStr("sys.url", sysSettingsUrl());
  dsSetNum("sys.rssi", netRssi());
  BatteryInfo b = hwBattery();
  dsSetNum("sys.battery", b.present ? b.percent : -1);
  dsSetNum("sys.charging", b.charging ? 1 : 0);
  dsSetNum("sys.usb", b.usb ? 1 : 0);
  String bt = !b.present ? String("USB") : (b.charging ? "Charging " + String(b.percent) + "%" : String(b.percent) + "%");
  dsSetStr("sys.battery_text", bt);
  dsSetStr("sys.uptime_text", dsDuration(millis() / 1000));
  dsSetNum("sys.heap", ESP.getFreeHeap());
  dsSetNum("sys.page", uiPageIndex());
  dsSetStr("sys.page_name", uiPageName(uiPageIndex()));
}

static int s_chargeMa = 0;

static void applyAllSettings() {
  gTimeH24 = S("time")["h24"] | false;
  s_chargeMa = hwSetChargeCurrentFor(S("battery")["capacity_mah"] | 1000);
  JsonObject t = S("touch");
  hwSetTouchOrientation(t["swap_xy"] | true, t["mirror_x"] | true, t["mirror_y"] | false);
  netApplySettings();
  fetcherApplySettings();
  bambuApplySettings();
  inputApplySettings();
  soundApplySettings();
  webApplySettings();
  s_nightSched = -1;                   // re-evaluate schedule
  uiApplySettings();
}

static void handleCommands() {
  Cmd c;
  while (cmdTake(c)) {
    switch (c.type) {
      case CMD_SETTINGS_PATCH: {
        JsonDocument patch;
        if (deserializeJson(patch, c.payload)) break;
        settingsMerge(patch.as<JsonVariantConst>());
        settingsSave();
        applyAllSettings();
        LOGI("settings: updated");
        uiToast("Settings saved");
        break;
      }
      case CMD_LAYOUT_SAVE:
        if (uiApplyLayout(c.payload, true)) { webSnapshotLayout(); uiToast("Layout saved"); }
        s_previewSince = 0;
        break;
      case CMD_LAYOUT_PREVIEW:
        if (uiApplyLayout(c.payload, false)) { s_previewSince = millis(); uiToast("Previewing layout"); }
        break;
      case CMD_ACTION:
        inputNoteActivity();
        if (c.payload == "revert_preview") { uiRevertPreview(); s_previewSince = 0; }
        else if (c.payload == "goto") { if (uiSleeping()) uiSetSleep(false); uiGoto(c.arg, c.arg > uiPageIndex() ? 1 : -1); }
        else if (c.payload == "test_sound") soundPlay(SND_TEST);
        else if (c.payload == "refresh_weather") { fetcherRefreshWeather(); uiToast("Updating weather"); }
        else if (c.payload == "rotation_here") { inputRotationHere(); webApplySettings(); }
        else if (c.payload == "identify") { uiSetSleep(false); uiToast("Hello from the web UI", 3000); }
        else if (c.payload == "night_on") { s_nightOverride = true; uiSetNight(true); }
        else if (c.payload == "night_off") { s_nightOverride = true; uiSetNight(false); }
        else {
          if (uiSleeping() && c.payload != "sleep" && c.payload != "sleep_toggle") uiSetSleep(false);
          inputDoAction(c.payload);
        }
        break;
      case CMD_REBOOT:
        uiToast("Restarting...", 5000);
        s_rebootAt = millis() + 1200;
        break;
      case CMD_FACTORY_RESET:
        settingsFactoryReset(c.arg == 1);
        uiToast("Factory reset. Restarting...", 5000);
        s_rebootAt = millis() + 1200;
        break;
      case CMD_CALIBRATE_IMU:
        inputCalibrate();
        webApplySettings();
        break;
      case CMD_TOAST:
        uiToast(c.payload, 4000);
        break;
    }
  }
}

// ---------------------------------------------------------------- setup / loop
void setup() {
  Serial.begin(115200);
  delay(1500);                                   // give the USB serial port time to attach for logs
  logInit();
  mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree);
  LOGI("%s %s starting", FW_NAME, FW_VERSION);
  LOGI("psram: %u bytes, flash: %u bytes", (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFlashChipSize());
  dsInit();
  cmdInit();

  hwEarlyInit();
  String crash;
  if (previousCrash(crash)) {
    LOGI("%s", crash.c_str());
    hwBootText("Crash report", crash.c_str());
    delay(8000);                                 // time to read / photograph it
    if (s_bcCrashes >= 2) s_safeMode = true;
  }
  bootStep("starting");

  bootStep("storage");
  if (!LittleFS.begin(false)) {
    hwBootMessage("First boot", "preparing storage (up to a minute)");
    LOGI("fs: formatting (first boot)...");
    if (!LittleFS.begin(true)) LOGI("fs: mount failed");
  }
  LOGI("fs: ok");
  settingsLoad();
  if (String((const char *)(S("api")["token"] | "")).isEmpty()) {
    char tok[17];
    snprintf(tok, sizeof tok, "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());
    gSettings["api"]["token"] = tok;
    settingsSave();
    LOGI("settings: generated API token");
  }
  gTimeH24 = S("time")["h24"] | false;

  if (s_safeMode) {
    LOGI("boot: SAFE MODE (Wi-Fi + web only)");
    hwBootText("Safe mode", "Crashed twice in a row.\nOnly Wi-Fi + web are on.\nOpen the web page and\nupload a fixed firmware.\nPhoto of the crash report\nhelps debugging.");
    bootStep("safe:wifi", false);
    netInit();
    bootStep("safe:web", false);
    webApplySettings();
    webStart();
    bootStep("safe:running", false);
    return;
  }

  bootStep("hardware");
  if (!hwInit()) LOGI("hw: init problem (continuing)");
  JsonObject t = S("touch");
  hwSetTouchOrientation(t["swap_xy"] | true, t["mirror_x"] | true, t["mirror_y"] | false);

  s_chargeMa = hwSetChargeCurrentFor(S("battery")["capacity_mah"] | 1000);
  bootStep("sound", false);
  soundInit();
  bootStep("layout");
  uiInit();
  bootStep("input", false);
  inputInit();
  bootStep("wifi", false);
  netInit();
  bootStep("data", false);
  sysKeysTick();
  fetcherStart();
  bambuStart();
  bootStep("web", false);
  webApplySettings();
  webSnapshotLayout();
  webStart();
  bootStep("loop", false);

  const esp_partition_t *run = esp_ota_get_running_partition();
  esp_ota_img_states_t st;
  s_otaPending = run && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
  if (s_otaPending) { LOGI("ota: new firmware on %s, verifying...", run->label); uiToast("Update installed: v" FW_VERSION, 5000); }
}

static void safeLoop() {
  static uint32_t last = 0;
  netTick();
  handleCommands();
  webTick();
  if (millis() - last > 3000) {
    last = millis();
    String ip = netIp();
    hwBootMessage("Safe mode", ip.length() ? ("http://" + ip + "/").c_str() : "join DeskDisplay-Setup Wi-Fi");
  }
  if (s_rebootAt && millis() > s_rebootAt) ESP.restart();
  delay(10);
}

void loop() {
  if (s_safeMode) { safeLoop(); return; }
  static bool healthy = false;
  if (!healthy && millis() > 30000) { healthy = true; s_bcCrashes = 0; bootStep("running", false); esp_core_dump_image_erase(); }
  lv_timer_handler();
  inputTick();
  uiTick();
  netTick();
  handleCommands();
  nightTick();
  sysKeysTick();
  webTick();

  if (s_otaPending && millis() > 60000 && uiPageCount() > 0) {
    esp_ota_mark_app_valid_cancel_rollback();
    s_otaPending = false;
    LOGI("ota: firmware marked valid");
  }
  if (s_previewSince && millis() - s_previewSince > 10 * 60000UL) {   // abandoned preview
    uiRevertPreview();
    s_previewSince = 0;
  }
  if (s_rebootAt && millis() > s_rebootAt) {
    LittleFS.end();
    ESP.restart();
  }
  delay(4);
}
