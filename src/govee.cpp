#include "govee.h"
#include "settings.h"
#include "logbuf.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <vector>
#include <map>

struct Light { String id, sku, ip; uint32_t seenMs = 0; };
struct Step { String dev; int power = -1; bool hasColor = false; uint8_t r = 0, g = 0, b = 0; int bright = -1; };
struct Scene { String name; std::vector<Step> steps; };

static SemaphoreHandle_t s_mtx = nullptr;
struct Lk { Lk() { xSemaphoreTake(s_mtx, portMAX_DELAY); } ~Lk() { xSemaphoreGive(s_mtx); } };

static std::vector<Light> s_lights;                       // guarded by s_mtx
static std::vector<Scene> s_scenes;                       // guarded by s_mtx
static std::map<String, String> s_names;                  // light id -> friendly name
static bool s_enabled = true;
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
    for (auto &e : s_lights) if (e.id == li.id) { e = li; found = true; }
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

// Which lights a step addresses: "all", a friendly name, or a device id.
static std::vector<String> targets(const String &dev) {
  std::vector<String> ips;
  Lk l;
  for (auto &li : s_lights) {
    String nm = s_names.count(li.id) ? s_names[li.id] : "";
    if (dev == "all" || dev.equalsIgnoreCase(nm) || dev == li.id) ips.push_back(li.ip);
  }
  return ips;
}

static void runScene(int idx) {
  Scene sc;
  { Lk l; if (idx < 0 || idx >= (int)s_scenes.size()) return; sc = s_scenes[idx]; }
  if (s_lights.empty() || millis() - s_lastScan > 10 * 60000) doScan(1500);   // IPs may have changed
  int sent = 0;
  for (auto &st : sc.steps) {
    for (auto &ip : targets(st.dev)) {
      char js[160];
      if (st.power >= 0) { snprintf(js, sizeof js, "{\"msg\":{\"cmd\":\"turn\",\"data\":{\"value\":%d}}}", st.power); sendTo(ip, js); }
      if (st.power != 0 && st.hasColor) {
        snprintf(js, sizeof js, "{\"msg\":{\"cmd\":\"colorwc\",\"data\":{\"color\":{\"r\":%u,\"g\":%u,\"b\":%u},\"colorTemInKelvin\":0}}}", st.r, st.g, st.b);
        sendTo(ip, js);
      }
      if (st.power != 0 && st.bright > 0) { snprintf(js, sizeof js, "{\"msg\":{\"cmd\":\"brightness\",\"data\":{\"value\":%d}}}", st.bright); sendTo(ip, js); }
      sent++;
    }
  }
  LOGI("govee: scene '%s' sent to %d light(s)", sc.name.c_str(), sent);
}

static void task(void *) {
  uint32_t lastAuto = 0;
  for (;;) {
    Job j;
    if (xQueueReceive(s_q, &j, pdMS_TO_TICKS(500)) == pdTRUE) {
      if (!WiFi.isConnected()) continue;
      openUdp();
      if (j.kind == J_SCAN) doScan(2000);
      else if (j.kind == J_SCENE) runScene(j.scene);
      else if (j.kind == J_TEST) {
        String ip; { Lk l; for (auto &li : s_lights) if (li.id == j.id) ip = li.ip; }
        if (ip.length()) {
          for (int i = 0; i < 2; i++) {
            sendTo(ip, "{\"msg\":{\"cmd\":\"turn\",\"data\":{\"value\":0}}}"); vTaskDelay(pdMS_TO_TICKS(600));
            sendTo(ip, "{\"msg\":{\"cmd\":\"turn\",\"data\":{\"value\":1}}}"); vTaskDelay(pdMS_TO_TICKS(600));
          }
        }
      }
    }
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
  xTaskCreatePinnedToCore(task, "govee", 6144, nullptr, 1, nullptr, 0);
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

void goveeDevices(JsonArray out) {
  Lk l;
  for (auto &li : s_lights) {
    JsonObject o = out.add<JsonObject>();
    o["id"] = li.id; o["sku"] = li.sku; o["ip"] = li.ip;
    o["name"] = s_names.count(li.id) ? s_names[li.id] : "";
    o["age_s"] = (millis() - li.seenMs) / 1000;
  }
}
