#include "govee.h"
#include "settings.h"
#include "logbuf.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <vector>
#include <map>

struct Light { String id, sku, ip, cloudName; bool cloud = false; uint32_t seenMs = 0; };   // ip set = LAN reachable
struct Step { String dev; int power = -1; bool hasColor = false; uint8_t r = 0, g = 0, b = 0; int bright = -1; };
struct Scene { String name; std::vector<Step> steps; };

static SemaphoreHandle_t s_mtx = nullptr;
struct Lk { Lk() { xSemaphoreTake(s_mtx, portMAX_DELAY); } ~Lk() { xSemaphoreGive(s_mtx); } };

static std::vector<Light> s_lights;                       // guarded by s_mtx
static std::vector<Scene> s_scenes;                       // guarded by s_mtx
static std::map<String, String> s_names;                  // light id -> friendly name
static bool s_enabled = true;
static String s_apiKey;                                   // Govee cloud API key (for lights without LAN Control)
static String s_lastErr;
static String s_pendingToast;
static volatile bool s_busy = false;
static uint32_t s_cloudListAt = 0;
static QueueHandle_t s_q = nullptr;                       // jobs for the task
static WiFiUDP s_udp;
static bool s_udpOpen = false;
static uint32_t s_lastScan = 0;

enum JobKind : uint8_t { J_SCAN, J_SCENE, J_TEST };
struct Job { JobKind kind; int scene; char id[40]; };

static bool parseColor(const char *s, uint8_t &r, uint8_t &g, uint8_t &b) {
  if (!s || s[0] != '#' || strlen(s) != 7) return false;
  uint32_t v = strtoul(s + 1, nullptr, 16);
  r = v >> 16; g = v >> 8; b = v;
  return true;
}

void goveeApplySettings() {
  if (!s_mtx) s_mtx = xSemaphoreCreateMutex();
  JsonObject g = S("govee");
  Lk l;
  s_enabled = g["enabled"] | true;
  String key = (const char *)(g["api_key"] | ""); key.trim();
  if (key != s_apiKey) { s_apiKey = key; s_cloudListAt = 0; }
  s_names.clear();
  for (JsonPair kv : g["names"].as<JsonObject>()) s_names[kv.key().c_str()] = (const char *)(kv.value() | "");
  s_scenes.clear();
  for (JsonObject sc : g["scenes"].as<JsonArray>()) {
    Scene s; s.name = (const char *)(sc["name"] | "");
    for (JsonObject st : sc["steps"].as<JsonArray>()) {
      Step p;
      p.dev = (const char *)(st["dev"] | "all");
      const char *pw = st["power"] | "";
      p.power = !strcmp(pw, "on") ? 1 : !strcmp(pw, "off") ? 0 : -1;
      p.hasColor = parseColor(st["color"] | "", p.r, p.g, p.b);
      p.bright = st["brightness"].is<int>() ? constrain((int)st["brightness"], 1, 100) : -1;
      s.steps.push_back(p);
    }
    s_scenes.push_back(s);
  }
}

static void openUdp() {
  if (s_udpOpen || !WiFi.isConnected()) return;
  s_udpOpen = s_udp.begin(4002);
}

static void sendTo(const String &ip, const char *json) {
  IPAddress a; if (!a.fromString(ip)) return;
  for (int i = 0; i < 2; i++) {                            // UDP: send twice, the commands are idempotent
    s_udp.beginPacket(a, 4003);
    s_udp.write((const uint8_t *)json, strlen(json));
    s_udp.endPacket();
    vTaskDelay(pdMS_TO_TICKS(25));
  }
}

static void pollReplies() {
  for (int n = s_udp.parsePacket(); n > 0; n = s_udp.parsePacket()) {
    char buf[600];
    int len = s_udp.read(buf, sizeof buf - 1);
    if (len <= 0) continue;
    buf[len] = 0;
    JsonDocument d;
    if (deserializeJson(d, buf)) continue;
    if (strcmp(d["msg"]["cmd"] | "", "scan") != 0) continue;
    JsonObject x = d["msg"]["data"];
    Light li; li.ip = (const char *)(x["ip"] | ""); li.id = (const char *)(x["device"] | ""); li.sku = (const char *)(x["sku"] | "");
    li.seenMs = millis();
    if (li.ip.isEmpty() || li.id.isEmpty()) continue;
    Lk l;
    bool found = false;
    for (auto &e : s_lights) if (e.id == li.id) { e.ip = li.ip; e.sku = li.sku; e.seenMs = li.seenMs; found = true; }
    if (!found) { s_lights.push_back(li); LOGI("govee: found %s (%s) at %s", li.sku.c_str(), li.id.c_str(), li.ip.c_str()); }
  }
}

static void doScan(uint32_t waitMs) {
  openUdp();
  if (!s_udpOpen) return;
  const char *msg = "{\"msg\":{\"cmd\":\"scan\",\"data\":{\"account_topic\":\"reserve\"}}}";
  for (int i = 0; i < 2; i++) {
    s_udp.beginPacket(IPAddress(239, 255, 255, 250), 4001);
    s_udp.write((const uint8_t *)msg, strlen(msg));
    s_udp.endPacket();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  uint32_t t0 = millis();
  while (millis() - t0 < waitMs) { pollReplies(); vTaskDelay(pdMS_TO_TICKS(50)); }
  s_lastScan = millis();
}

// ---- Govee cloud API (openapi.api.govee.com), same calls as the PC screen-sync app.
// Limited by Govee to ~10 commands per minute per light, so it's for button presses, not effects.
static const char *CLOUD = "https://openapi.api.govee.com/router/api/v1";

static int cloudCall(const char *method, const String &path, const String &body, String &resp) {
  String key; { Lk l; key = s_apiKey; }
  if (key.isEmpty()) return -1;
  NetworkClientSecure tls;
  tls.setInsecure();
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(tls, String(CLOUD) + path)) return -1;
  http.addHeader("Govee-API-Key", key);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  http.setUserAgent("govee-ambient/1.1");
  int code = !strcmp(method, "GET") ? http.GET() : http.POST(body);
  resp = code > 0 ? http.getString() : "";
  http.end();
  return code;
}

static void cloudList() {
  String resp;
  int code = cloudCall("GET", "/user/devices", "", resp);
  s_cloudListAt = millis();
  if (code != 200) {
    Lk l;
    s_lastErr = code == 401 || code == 403 ? "Govee rejected the API key (check it was copied exactly)"
              : code == 429 ? "Govee says too many requests - try again in a minute"
              : code < 0 ? "Couldn't reach Govee's cloud (" + String(HTTPClient::errorToString(code)) + ")"
              : "Govee cloud: HTTP " + String(code) + " " + resp.substring(0, 80);
    LOGI("govee: device list failed (%d) %s", code, resp.substring(0, 160).c_str());
    return;
  }
  JsonDocument d;
  DeserializationError je = deserializeJson(d, resp);
  LOGI("govee: device list %u bytes: %s", (unsigned)resp.length(), resp.substring(0, 200).c_str());
  if (je) { Lk l; s_lastErr = "Couldn't read Govee's reply (" + String(je.c_str()) + ", " + String(resp.length()) + " bytes)"; return; }
  JsonArray list = d["data"].as<JsonArray>();
  if (list.isNull()) list = d["data"]["devices"].as<JsonArray>();
  if (list.isNull()) list = d["payload"]["devices"].as<JsonArray>();
  if (list.isNull() || list.size() == 0) {
    Lk l;
    String msg = (const char *)(d["message"] | ""); if (msg.isEmpty()) msg = (const char *)(d["msg"] | "");
    int c = d["code"] | 0;
    s_lastErr = "Govee's cloud returned no lights" + (c || msg.length() ? " (" + String(c) + " " + msg + ")" : String("")) + ": " + resp.substring(0, 120);
    return;
  }
  Lk l;
  s_lastErr = "";
  for (JsonObject x : list) {
    String id = (const char *)(x["device"] | "");
    if (id.isEmpty()) continue;
    bool found = false;
    for (auto &e : s_lights) if (e.id == id) { e.cloud = true; e.cloudName = (const char *)(x["deviceName"] | ""); e.sku = (const char *)(x["sku"] | ""); found = true; }
    if (!found) {
      Light li; li.id = id; li.sku = (const char *)(x["sku"] | ""); li.cloudName = (const char *)(x["deviceName"] | ""); li.cloud = true; li.seenMs = millis();
      s_lights.push_back(li);
      LOGI("govee: cloud light %s (%s)", li.cloudName.c_str(), li.sku.c_str());
    }
  }
}

static bool cloudControl(const Light &li, const char *type, const char *instance, long value) {
  JsonDocument d;
  d["requestId"] = String((uint32_t)esp_random(), HEX);
  JsonObject p = d["payload"].to<JsonObject>();
  p["sku"] = li.sku; p["device"] = li.id;
  JsonObject c = p["capability"].to<JsonObject>();
  c["type"] = type; c["instance"] = instance; c["value"] = value;
  String body, resp; serializeJson(d, body);
  int code = cloudCall("POST", "/device/control", body, resp);
  if (code != 200) {
    Lk l;
    s_lastErr = code == 429 ? "Govee says too many commands - wait a minute" : code == 401 ? "Govee rejected the API key" : "Govee cloud: HTTP " + String(code);
    LOGI("govee: cloud %s failed (%d) %s", instance, code, resp.substring(0, 120).c_str());
    return false;
  }
  return true;
}

static String displayName(const Light &li) {
  if (s_names.count(li.id) && s_names[li.id].length()) return s_names[li.id];
  return li.cloudName;
}

// Which lights a step addresses: "all", a friendly name (yours or the Govee app's), or a device id.
static std::vector<Light> targets(const String &dev) {
  std::vector<Light> out;
  Lk l;
  for (auto &li : s_lights) {
    if (dev == "all" || dev.equalsIgnoreCase(displayName(li)) || dev.equalsIgnoreCase(li.cloudName) || dev == li.id) out.push_back(li);
  }
  return out;
}

static bool lanFresh(const Light &li) { return li.ip.length() && millis() - li.seenMs < 15 * 60000; }

static void runScene(int idx) {
  Scene sc;
  { Lk l; if (idx < 0 || idx >= (int)s_scenes.size()) return; sc = s_scenes[idx]; }
  bool haveKey; { Lk l; haveKey = s_apiKey.length() > 0; }
  if (s_lights.empty() || millis() - s_lastScan > 10 * 60000) doScan(1500);   // IPs may have changed
  if (haveKey && (!s_cloudListAt || millis() - s_cloudListAt > 30 * 60000)) cloudList();
  { Lk l; s_lastErr = ""; }
  int sent = 0;
  for (auto &st : sc.steps) {
    for (auto &li : targets(st.dev)) {
      if (lanFresh(li)) {
        char js[160];
        if (st.power >= 0) { snprintf(js, sizeof js, "{\"msg\":{\"cmd\":\"turn\",\"data\":{\"value\":%d}}}", st.power); sendTo(li.ip, js); }
        if (st.power != 0 && st.hasColor) {
          snprintf(js, sizeof js, "{\"msg\":{\"cmd\":\"colorwc\",\"data\":{\"color\":{\"r\":%u,\"g\":%u,\"b\":%u},\"colorTemInKelvin\":0}}}", st.r, st.g, st.b);
          sendTo(li.ip, js);
        }
        if (st.power != 0 && st.bright > 0) { snprintf(js, sizeof js, "{\"msg\":{\"cmd\":\"brightness\",\"data\":{\"value\":%d}}}", st.bright); sendTo(li.ip, js); }
      } else if (li.cloud && haveKey) {
        if (st.power >= 0) cloudControl(li, "devices.capabilities.on_off", "powerSwitch", st.power);
        if (st.power != 0 && st.hasColor) cloudControl(li, "devices.capabilities.color_setting", "colorRgb", ((long)st.r << 16) | (st.g << 8) | st.b);
        if (st.power != 0 && st.bright > 0) cloudControl(li, "devices.capabilities.range", "brightness", st.bright);
      } else continue;
      sent++;
    }
  }
  if (!sent) { Lk l; if (s_lastErr.isEmpty()) s_lastErr = haveKey ? "No matching lights found" : "No lights found - add your Govee API key in Settings"; }
  { Lk l; if (s_lastErr.length()) s_pendingToast = s_lastErr; }
  LOGI("govee: scene '%s' sent to %d light(s)", sc.name.c_str(), sent);
}

static void task(void *) {
  uint32_t lastAuto = 0;
  for (;;) {
    Job j;
    if (xQueueReceive(s_q, &j, pdMS_TO_TICKS(500)) == pdTRUE) {
      if (!WiFi.isConnected()) { Lk l; s_lastErr = "Wi-Fi not connected"; continue; }
      s_busy = true;
      openUdp();
      if (j.kind == J_SCAN) {
        { Lk l; s_lastErr = ""; }
        doScan(2000);
        bool k; { Lk l; k = s_apiKey.length() > 0; }
        if (k) cloudList();
        Lk l;
        if (s_lights.empty() && s_lastErr.isEmpty()) s_lastErr = k ? "Govee's cloud returned no lights for this API key" : "No API key saved, and no lights answered on the network";
        if (!s_lights.empty() && s_lastErr.startsWith("Govee's cloud returned no lights")) s_lastErr = "";
      }
      else if (j.kind == J_SCENE) runScene(j.scene);
      else if (j.kind == J_TEST) {
        Light t; bool have = false; { Lk l; for (auto &li : s_lights) if (li.id == j.id) { t = li; have = true; } }
        String ip = have && lanFresh(t) ? t.ip : "";
        if (have && ip.isEmpty() && t.cloud) {
          cloudControl(t, "devices.capabilities.on_off", "powerSwitch", 0); vTaskDelay(pdMS_TO_TICKS(1500));
          cloudControl(t, "devices.capabilities.on_off", "powerSwitch", 1);
        } else if (ip.length()) {
          for (int i = 0; i < 2; i++) {
            sendTo(ip, "{\"msg\":{\"cmd\":\"turn\",\"data\":{\"value\":0}}}"); vTaskDelay(pdMS_TO_TICKS(600));
            sendTo(ip, "{\"msg\":{\"cmd\":\"turn\",\"data\":{\"value\":1}}}"); vTaskDelay(pdMS_TO_TICKS(600));
          }
        }
      }
    }
    s_busy = false;
    if (s_udpOpen) pollReplies();
    // Rescan now and then so lights that got a new IP are followed.
    bool enabled; { Lk l; enabled = s_enabled && !s_scenes.empty(); }
    if (enabled && WiFi.isConnected() && (lastAuto == 0 ? millis() > 20000 : millis() - lastAuto > 5 * 60000)) {
      lastAuto = millis();
      doScan(1500);
    }
    if (!WiFi.isConnected() && s_udpOpen) { s_udp.stop(); s_udpOpen = false; }
  }
}

void goveeStart() {
  if (!s_mtx) s_mtx = xSemaphoreCreateMutex();
  goveeApplySettings();
  s_q = xQueueCreate(6, sizeof(Job));
  xTaskCreatePinnedToCore(task, "govee", 12288, nullptr, 1, nullptr, 0);   // TLS for the cloud API
}

static bool post(Job j) { return s_q && xQueueSend(s_q, &j, 0) == pdTRUE; }

void goveeScan() { Job j{}; j.kind = J_SCAN; post(j); }

bool goveeTest(const String &id) {
  Job j{}; j.kind = J_TEST; strlcpy(j.id, id.c_str(), sizeof j.id);
  return post(j);
}

bool goveeRunScene(const String &which, String &err) {
  int idx = -1;
  {
    Lk l;
    if (!s_enabled) { err = "Govee control is off in Settings"; return false; }
    for (size_t i = 0; i < s_scenes.size(); i++) if (s_scenes[i].name.equalsIgnoreCase(which)) idx = i;
    if (idx < 0 && which.length() && isDigit(which[0]) && which.toInt() < (int)s_scenes.size()) idx = which.toInt();
    if (idx < 0) { err = "No light scene called \"" + which + "\""; return false; }
  }
  Job j{}; j.kind = J_SCENE; j.scene = idx;
  if (!post(j)) { err = "busy"; return false; }
  return true;
}

bool goveeBusy() { return s_busy || (s_q && uxQueueMessagesWaiting(s_q) > 0); }
String goveeLastError() { Lk l; return s_lastErr; }
String goveeTakeToast() { if (!s_mtx) return ""; Lk l; String t = s_pendingToast; s_pendingToast = ""; return t; }
void goveeDevices(JsonArray out) {
  Lk l;
  for (auto &li : s_lights) {
    JsonObject o = out.add<JsonObject>();
    o["id"] = li.id; o["sku"] = li.sku; o["ip"] = li.ip;
    o["name"] = s_names.count(li.id) ? s_names[li.id] : "";
    o["cloud_name"] = li.cloudName;
    o["via"] = lanFresh(li) ? "lan" : li.cloud ? "cloud" : "lan (not seen lately)";
    o["age_s"] = (millis() - li.seenMs) / 1000;
  }
}
