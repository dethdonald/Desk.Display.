#include "bambu.h"
#include "datastore.h"
#include "settings.h"
#include "logbuf.h"
#include <WiFi.h>
#include <NetworkClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

struct BambuCfg { bool enabled = false; String ip, serial, code; };

static BambuCfg s_cfg;
static volatile bool s_cfgChanged = true;
static SemaphoreHandle_t s_mtx;
static NetworkClientSecure s_tls;
static PubSubClient s_mqtt(s_tls);
static String s_reportTopic, s_requestTopic;
static uint32_t s_lastPushall = 0;
static String s_lastState;

struct Lock { Lock() { xSemaphoreTake(s_mtx, portMAX_DELAY); } ~Lock() { xSemaphoreGive(s_mtx); } };

static const char *stateText(const String &s) {
  if (s == "RUNNING") return "Printing";
  if (s == "PAUSE") return "Paused";
  if (s == "FINISH") return "Finished";
  if (s == "FAILED") return "Failed";
  if (s == "PREPARE") return "Preparing";
  if (s == "SLICING") return "Slicing";
  if (s == "IDLE") return "Idle";
  return s.length() ? s.c_str() : "Unknown";
}

static void setRemaining(int minutes) {
  dsSetNum("bambu.remaining_min", minutes);
  dsSetStr("bambu.remaining_text", minutes > 0 ? dsDuration((long)minutes * 60) : String("--"));
  time_t now = time(nullptr);
  if (minutes > 0 && now > 1700000000) dsSetNum("bambu.eta", (double)(now + (time_t)minutes * 60));
}

// A1/P1 printers send only changed fields, so each field is applied when present.
static void onMessage(char *, byte *payload, unsigned int len) {
  JsonDocument filter;
  JsonObject f = filter["print"].to<JsonObject>();
  for (const char *k : {"mc_percent", "layer_num", "total_layer_num", "mc_remaining_time", "gcode_state", "subtask_name",
                        "nozzle_temper", "nozzle_target_temper", "bed_temper", "bed_target_temper", "print_error"})
    f[k] = true;
  JsonDocument doc;
  if (deserializeJson(doc, payload, len, DeserializationOption::Filter(filter))) return;
  JsonObject p = doc["print"];
  if (p.isNull()) return;

  if (!p["mc_percent"].isNull()) dsSetNum("bambu.percent", p["mc_percent"].as<double>());
  if (!p["layer_num"].isNull()) dsSetNum("bambu.layer", p["layer_num"].as<double>());
  if (!p["total_layer_num"].isNull()) dsSetNum("bambu.total_layers", p["total_layer_num"].as<double>());
  if (!p["mc_remaining_time"].isNull()) setRemaining(p["mc_remaining_time"].as<int>());
  if (p["subtask_name"].is<const char *>()) dsSetStr("bambu.job", p["subtask_name"].as<const char *>());
  if (!p["nozzle_temper"].isNull()) dsSetNum("bambu.nozzle", p["nozzle_temper"].as<double>());
  if (!p["nozzle_target_temper"].isNull()) dsSetNum("bambu.nozzle_target", p["nozzle_target_temper"].as<double>());
  if (!p["bed_temper"].isNull()) dsSetNum("bambu.bed", p["bed_temper"].as<double>());
  if (!p["bed_target_temper"].isNull()) dsSetNum("bambu.bed_target", p["bed_target_temper"].as<double>());
  if (!p["print_error"].isNull()) dsSetNum("bambu.error_code", p["print_error"].as<double>());
  if (p["gcode_state"].is<const char *>()) {
    String st = p["gcode_state"].as<const char *>();
    dsSetStr("bambu.state", st);
    dsSetStr("bambu.state_text", stateText(st));
    if (st != s_lastState) { LOGI("bambu: %s", stateText(st)); s_lastState = st; }
  }
  dsSetNum("bambu.updated", time(nullptr));
}

static void requestFullStatus() {
  s_mqtt.publish(s_requestTopic.c_str(), "{\"pushing\":{\"sequence_id\":\"0\",\"command\":\"pushall\"}}");
  s_lastPushall = millis();
}

static void task(void *) {
  uint32_t backoff = 2000, nextTry = 0;
  bool wasConnected = false;
  for (;;) {
    BambuCfg cfg;
    bool changed = false;
    { Lock l; cfg = s_cfg; changed = s_cfgChanged; s_cfgChanged = false; }
    if (changed && s_mqtt.connected()) { s_mqtt.disconnect(); nextTry = 0; backoff = 2000; }

    if (!cfg.enabled || cfg.ip.isEmpty() || cfg.serial.isEmpty() || cfg.code.isEmpty()) {
      if (wasConnected || changed) { dsSetNum("bambu.connected", 0); dsSetStr("bambu.error", cfg.enabled ? "Set IP, serial and access code" : "off"); }
      wasConnected = false;
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    if (!WiFi.isConnected()) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }

    if (!s_mqtt.connected()) {
      if (wasConnected) { LOGI("bambu: disconnected"); dsSetNum("bambu.connected", 0); wasConnected = false; }
      if ((int32_t)(millis() - nextTry) < 0) { vTaskDelay(pdMS_TO_TICKS(200)); continue; }
      s_reportTopic = "device/" + cfg.serial + "/report";
      s_requestTopic = "device/" + cfg.serial + "/request";
      s_tls.setInsecure();                       // the printer uses a self-signed certificate
      s_mqtt.setServer(cfg.ip.c_str(), 8883);
      String clientId = "deskdisplay-" + String((uint32_t)ESP.getEfuseMac(), HEX);
      LOGI("bambu: connecting to %s", cfg.ip.c_str());
      if (s_mqtt.connect(clientId.c_str(), "bblp", cfg.code.c_str())) {
        s_mqtt.subscribe(s_reportTopic.c_str());
        requestFullStatus();
        dsSetNum("bambu.connected", 1);
        dsSetStr("bambu.error", "");
        LOGI("bambu: connected");
        wasConnected = true;
        backoff = 2000;
      } else {
        int st = s_mqtt.state();
        String err;
        if (st == 4) err = "Wrong access code";
        else if (st == 5) err = "Not authorized (check the access code)";
        else if (st == -2) {
          // Tell apart "nothing answers at that address" from "answered, but TLS failed"
          char tlsErr[100] = {0};
          int tlsCode = s_tls.lastError(tlsErr, sizeof tlsErr);
          NetworkClient probe;
          bool tcp = probe.connect(cfg.ip.c_str(), 8883, 3000);
          probe.stop();
          if (!tcp) err = "No answer from " + cfg.ip + ":8883 (check the IP and that both are on the same network)";
          else err = "Printer found, secure connection failed (" + String(tlsCode) + ": " + tlsErr + ")";
        } else err = "MQTT error " + String(st);
        dsSetStr("bambu.error", err);
        dsSetNum("bambu.connected", 0);
        LOGI("bambu: connect failed (%s)", err.c_str());
        nextTry = millis() + backoff;
        backoff = min<uint32_t>(backoff * 2, 60000);
      }
    } else {
      s_mqtt.loop();
      if (millis() - s_lastPushall > 10 * 60000UL) requestFullStatus();   // resync occasionally
      vTaskDelay(pdMS_TO_TICKS(20));
    }
  }
}

void bambuStart() {
  s_mtx = xSemaphoreCreateMutex();
  s_mqtt.setBufferSize(32 * 1024);              // full status reports are several KB
  s_mqtt.setCallback(onMessage);
  s_mqtt.setKeepAlive(30);
  s_mqtt.setSocketTimeout(10);
  bambuApplySettings();
  xTaskCreatePinnedToCore(task, "bambu", 16384, nullptr, 1, nullptr, 0);
}

void bambuApplySettings() {
  BambuCfg c;
  JsonObject b = S("bambu");
  c.enabled = b["enabled"] | false;
  c.ip = (const char *)(b["ip"] | ""); c.ip.trim();
  c.serial = (const char *)(b["serial"] | ""); c.serial.trim(); c.serial.toUpperCase();
  c.code = (const char *)(b["access_code"] | ""); c.code.trim();
  if (!s_mtx) { s_cfg = c; return; }
  Lock l;
  if (c.enabled != s_cfg.enabled || c.ip != s_cfg.ip || c.serial != s_cfg.serial || c.code != s_cfg.code) {
    s_cfg = c;
    s_cfgChanged = true;
  }
}
