#include "web.h"
#include "config.h"
#include "settings.h"
#include "datastore.h"
#include "cmdq.h"
#include "fetcher.h"
#include "logbuf.h"
#include "ui.h"
#include "net.h"
#include "hw.h"
#include "input.h"
#include "www_assets.h"
#include "sound.h"
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_ota_ops.h>

static AsyncWebServer s_server(80);
static SemaphoreHandle_t s_mtx;
static String s_user = "admin", s_pass, s_token;
static String s_settingsJson, s_layoutJson, s_statusJson = "{}";
static bool s_uploadOk = false;
static String s_uploadErr;

// Created on first use: webApplySettings() runs before webStart().
static SemaphoreHandle_t mtx() { if (!s_mtx) s_mtx = xSemaphoreCreateMutex(); return s_mtx; }
struct Lock { Lock() { xSemaphoreTake(mtx(), portMAX_DELAY); } ~Lock() { xSemaphoreGive(mtx()); } };

// ---------------------------------------------------------------- auth
static bool authOk(AsyncWebServerRequest *r) {
  String u, p;
  { Lock l; u = s_user; p = s_pass; }
  if (p.isEmpty()) return true;                         // no admin password set yet
  return r->authenticate(u.c_str(), p.c_str());
}

static AsyncMiddlewareFunction s_auth([](AsyncWebServerRequest *r, ArMiddlewareNext next) {
  if (!authOk(r)) { r->requestAuthentication(AsyncAuthType::AUTH_BASIC, "Desk Display"); return; }
  next();
});

static bool tokenOk(AsyncWebServerRequest *r) {
  String t;
  { Lock l; t = s_token; }
  if (t.isEmpty()) return true;
  if (r->hasHeader("X-Api-Token") && r->header("X-Api-Token") == t) return true;
  if (r->hasHeader("Authorization") && r->header("Authorization") == "Bearer " + t) return true;
  if (r->hasParam("token") && r->getParam("token")->value() == t) return true;
  return authOk(r);                                     // the web UI itself may push too
}

static void sendJson(AsyncWebServerRequest *r, int code, const String &body) {
  AsyncWebServerResponse *res = r->beginResponse(code, "application/json", body);
  res->addHeader("Cache-Control", "no-store");
  r->send(res);
}
static void ok(AsyncWebServerRequest *r) { sendJson(r, 200, "{\"ok\":true}"); }
static void fail(AsyncWebServerRequest *r, int code, const String &msg) {
  JsonDocument d; d["ok"] = false; d["error"] = msg;
  String s; serializeJson(d, s); sendJson(r, code, s);
}

static AsyncCallbackJsonWebHandler *jsonRoute(const char *uri, ArJsonRequestHandlerFunction fn, bool admin = true) {
  auto *h = new AsyncCallbackJsonWebHandler(AsyncURIMatcher::exact(uri), fn);
  h->setMethod(HTTP_POST);
  h->setMaxContentLength(96 * 1024);
  if (admin) h->addMiddleware(&s_auth);
  s_server.addHandler(h);
  return h;
}

// ---------------------------------------------------------------- static assets
static void serveAsset(AsyncWebServerRequest *r, const WwwAsset &a) {
  if (r->hasHeader("If-None-Match") && r->header("If-None-Match") == "\"" FW_VERSION "\"") { r->send(304); return; }
  AsyncWebServerResponse *res = r->beginResponse(200, a.mime, a.data, a.len);
  res->addHeader("Content-Encoding", "gzip");
  // Always revalidate: the UI ships inside the firmware, so after an update the browser must not reuse old files.
  res->addHeader("Cache-Control", "no-cache, must-revalidate");
  res->addHeader("ETag", "\"" FW_VERSION "\"");
  r->send(res);
}

static const WwwAsset *findAsset(const String &path) {
  for (size_t i = 0; i < WWW_ASSET_COUNT; i++) if (path == WWW_ASSETS[i].path) return &WWW_ASSETS[i];
  return nullptr;
}

// ---------------------------------------------------------------- routes
void webStart() {
  mtx();

  // UI files (embedded in firmware, so one OTA update covers everything)
  for (size_t i = 0; i < WWW_ASSET_COUNT; i++) {
    const WwwAsset *a = &WWW_ASSETS[i];
    s_server.on(AsyncURIMatcher::exact(a->path), HTTP_GET, [a](AsyncWebServerRequest *r) { serveAsset(r, *a); }).addMiddleware(&s_auth);
  }
  s_server.on(AsyncURIMatcher::exact("/"), HTTP_GET, [](AsyncWebServerRequest *r) { serveAsset(r, *findAsset("/index.html")); }).addMiddleware(&s_auth);

  s_server.on(AsyncURIMatcher::exact("/api/status"), HTTP_GET, [](AsyncWebServerRequest *r) {
    String s; { Lock l; s = s_statusJson; } sendJson(r, 200, s);
  }).addMiddleware(&s_auth);

  s_server.on(AsyncURIMatcher::exact("/api/settings"), HTTP_GET, [](AsyncWebServerRequest *r) {
    String s; { Lock l; s = s_settingsJson; } sendJson(r, 200, s);
  }).addMiddleware(&s_auth);
  s_server.on(AsyncURIMatcher::exact("/api/settings/defaults"), HTTP_GET, [](AsyncWebServerRequest *r) { sendJson(r, 200, settingsDefaultsJson()); }).addMiddleware(&s_auth);
  jsonRoute("/api/settings", [](AsyncWebServerRequest *r, JsonVariant &j) {
    if (!j.is<JsonObject>()) return fail(r, 400, "expected object");
    String s; serializeJson(j, s);
    cmdPost(CMD_SETTINGS_PATCH, s) ? ok(r) : fail(r, 503, "busy");
  });

  s_server.on(AsyncURIMatcher::exact("/api/layout"), HTTP_GET, [](AsyncWebServerRequest *r) {
    String s; { Lock l; s = s_layoutJson; } sendJson(r, 200, s);
  }).addMiddleware(&s_auth);
  s_server.on(AsyncURIMatcher::exact("/api/layout/default"), HTTP_GET, [](AsyncWebServerRequest *r) { sendJson(r, 200, uiLayoutDefaultJson()); }).addMiddleware(&s_auth);
  auto layoutPost = [](bool save) {
    return [save](AsyncWebServerRequest *r, JsonVariant &j) {
      String s; serializeJson(j, s);
      String err;
      if (!uiValidateLayout(s, err)) return fail(r, 400, err);
      cmdPost(save ? CMD_LAYOUT_SAVE : CMD_LAYOUT_PREVIEW, s) ? ok(r) : fail(r, 503, "busy");
    };
  };
  jsonRoute("/api/layout", layoutPost(true));
  jsonRoute("/api/layout/preview", layoutPost(false));
  s_server.on(AsyncURIMatcher::exact("/api/layout/revert"), HTTP_POST, [](AsyncWebServerRequest *r) { cmdPost(CMD_ACTION, "revert_preview"); ok(r); }).addMiddleware(&s_auth);

  // Data
  s_server.on(AsyncURIMatcher::exact("/api/data"), HTTP_GET, [](AsyncWebServerRequest *r) {
    JsonDocument d; dsToJson(d); String s; serializeJson(d, s); sendJson(r, 200, s);
  }).addMiddleware(&s_auth);
  jsonRoute("/api/data", [](AsyncWebServerRequest *r, JsonVariant &j) {
    if (!tokenOk(r)) return fail(r, 401, "bad token");
    if (!j.is<JsonObject>()) return fail(r, 400, "expected object");
    int n = 0;
    if (j["prefix"].is<const char *>() && !j["data"].isNull()) n = dsFlatten(j["prefix"].as<String>(), j["data"]);
    else for (JsonPair kv : j.as<JsonObject>()) n += dsFlatten(kv.key().c_str(), kv.value());
    JsonDocument d; d["ok"] = true; d["keys"] = n; String s; serializeJson(d, s); sendJson(r, 200, s);
  }, false);
  jsonRoute("/api/usage", [](AsyncWebServerRequest *r, JsonVariant &j) {
    if (!tokenOk(r)) { LOGI("claude: push rejected (wrong API token) from %s", r->client()->remoteIP().toString().c_str()); return fail(r, 401, "bad token"); }
    String err;
    claudeIngest(j, err) ? ok(r) : fail(r, 400, err);
  }, false);

  // Control
  jsonRoute("/api/action", [](AsyncWebServerRequest *r, JsonVariant &j) {
    String a = j["action"] | "";
    if (a.isEmpty()) return fail(r, 400, "missing action");
    cmdPost(CMD_ACTION, a, j["page"] | 0) ? ok(r) : fail(r, 503, "busy");
  });
  s_server.on(AsyncURIMatcher::exact("/api/calibrate"), HTTP_POST, [](AsyncWebServerRequest *r) { cmdPost(CMD_CALIBRATE_IMU); ok(r); }).addMiddleware(&s_auth);

  // Maintenance
  s_server.on(AsyncURIMatcher::exact("/api/logs"), HTTP_GET, [](AsyncWebServerRequest *r) {
    AsyncWebServerResponse *res = r->beginResponse(200, "text/plain; charset=utf-8", logDump());
    res->addHeader("Cache-Control", "no-store");
    r->send(res);
  }).addMiddleware(&s_auth);
  s_server.on(AsyncURIMatcher::exact("/api/reboot"), HTTP_POST, [](AsyncWebServerRequest *r) { cmdPost(CMD_REBOOT); ok(r); }).addMiddleware(&s_auth);
  jsonRoute("/api/factory-reset", [](AsyncWebServerRequest *r, JsonVariant &j) {
    cmdPost(CMD_FACTORY_RESET, "", (j["keep_wifi"] | true) ? 1 : 0); ok(r);
  });
  s_server.on(AsyncURIMatcher::exact("/api/backup"), HTTP_GET, [](AsyncWebServerRequest *r) {
    bool secrets = r->hasParam("secrets") && r->getParam("secrets")->value() == "1";
    String settings, layout;
    { Lock l; settings = s_settingsJson; layout = s_layoutJson; }
    if (secrets) { cmdPost(CMD_TOAST, "Backup downloaded (with secrets)"); }
    String body = "{\"type\":\"deskdisplay-backup\",\"version\":\"" FW_VERSION "\",\"settings\":" +
                  (settings.length() ? settings : String("{}")) + ",\"layout\":" + (layout.length() ? layout : String("null")) + "}";
    AsyncWebServerResponse *res = r->beginResponse(200, "application/json", body);
    res->addHeader("Content-Disposition", "attachment; filename=deskdisplay-backup.json");
    r->send(res);
  }).addMiddleware(&s_auth);
  jsonRoute("/api/restore", [](AsyncWebServerRequest *r, JsonVariant &j) {
    if (j["settings"].is<JsonObject>()) { String s; serializeJson(j["settings"], s); cmdPost(CMD_SETTINGS_PATCH, s); }
    if (j["layout"].is<JsonObject>()) {
      String s; serializeJson(j["layout"], s); String err;
      if (!uiValidateLayout(s, err)) return fail(r, 400, "layout: " + err);
      cmdPost(CMD_LAYOUT_SAVE, s);
    }
    ok(r);
  });

  s_server.on(AsyncURIMatcher::exact("/api/wifi/scan"), HTTP_GET, [](AsyncWebServerRequest *r) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_FAILED) { WiFi.scanNetworks(true); return sendJson(r, 202, "{\"scanning\":true}"); }
    if (n == WIFI_SCAN_RUNNING) return sendJson(r, 202, "{\"scanning\":true}");
    JsonDocument d; JsonArray a = d["networks"].to<JsonArray>();
    for (int i = 0; i < n; i++) {
      JsonObject o = a.add<JsonObject>();
      o["ssid"] = WiFi.SSID(i); o["rssi"] = WiFi.RSSI(i); o["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    }
    WiFi.scanDelete();
    String s; serializeJson(d, s); sendJson(r, 200, s);
  }).addMiddleware(&s_auth);

  jsonRoute("/api/sources/test", [](AsyncWebServerRequest *r, JsonVariant &j) {
    String url = j["url"] | "";
    if (!url.startsWith("http")) return fail(r, 400, "URL must start with http:// or https://");
    fetcherTestSource(url); ok(r);
  });
  s_server.on(AsyncURIMatcher::exact("/api/sources/test"), HTTP_GET, [](AsyncWebServerRequest *r) {
    bool done; String res = fetcherTestResult(done);
    if (!done) return sendJson(r, 202, "{\"pending\":true}");
    sendJson(r, 200, res.length() ? res : String("{\"ok\":false,\"error\":\"no test run\"}"));
  }).addMiddleware(&s_auth);

  // Firmware update: upload a .bin ...
  s_server.on(AsyncURIMatcher::exact("/api/update"), HTTP_POST,
    [](AsyncWebServerRequest *r) {
      if (!authOk(r)) return r->requestAuthentication(AsyncAuthType::AUTH_BASIC, "Desk Display");
      if (s_uploadOk) { ok(r); cmdPost(CMD_REBOOT); }
      else fail(r, 400, s_uploadErr.length() ? s_uploadErr : String("upload failed"));
    },
    [](AsyncWebServerRequest *r, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
      if (index == 0) {
        s_uploadOk = false; s_uploadErr = "";
        if (!authOk(r)) { s_uploadErr = "unauthorized"; return; }
        LOGI("ota: receiving %s", filename.c_str());
        cmdPost(CMD_TOAST, "Installing update...");
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) { s_uploadErr = Update.errorString(); return; }
      }
      if (s_uploadErr.length()) return;
      if (Update.write(data, len) != len) { s_uploadErr = Update.errorString(); Update.abort(); return; }
      if (final) {
        if (Update.end(true)) { s_uploadOk = true; LOGI("ota: %u bytes written, rebooting", (unsigned)(index + len)); }
        else { s_uploadErr = Update.errorString(); LOGI("ota: failed: %s", s_uploadErr.c_str()); }
      }
    });
  // ... or pull one from a URL (GitHub release, your server, ...)
  jsonRoute("/api/update/url", [](AsyncWebServerRequest *r, JsonVariant &j) {
    String url = j["url"] | "";
    url.trim();
    if (url.endsWith(".bin")) {                         // a direct firmware link
      if (!url.startsWith("http")) return fail(r, 400, "URL must start with http:// or https://");
      fetcherUpdateFromUrl(url);
    } else {                                            // a repository: grab its newest release
      if (normalizeRepo(url).indexOf('/') < 0) return fail(r, 400, "enter a repository like owner/repo");
      fetcherInstallLatest(url);
    }
    ok(r);
  });
  s_server.on(AsyncURIMatcher::exact("/api/update/status"), HTTP_GET, [](AsyncWebServerRequest *r) {
    JsonDocument d; fetcherUpdateInfo(d); String s; serializeJson(d, s); sendJson(r, 200, s);
  }).addMiddleware(&s_auth);
  s_server.on(AsyncURIMatcher::exact("/api/update/check"), HTTP_POST, [](AsyncWebServerRequest *r) { fetcherCheckUpdate(); ok(r); }).addMiddleware(&s_auth);
  // Install the newest GitHub release: optional {"repo": "owner/repo" or GitHub URL}, default = configured repo
  jsonRoute("/api/update/install-latest", [](AsyncWebServerRequest *r, JsonVariant &j) {
    fetcherInstallLatest(j["repo"] | ""); ok(r);
  });

  // Captive portal + SPA fallback
  s_server.onNotFound([](AsyncWebServerRequest *r) {
    if (r->url().startsWith("/api/")) return fail(r, 404, "not found");
    if (netApMode()) return r->redirect("http://" + WiFi.softAPIP().toString() + "/");
    if (!authOk(r)) return r->requestAuthentication(AsyncAuthType::AUTH_BASIC, "Desk Display");
    serveAsset(r, *findAsset("/index.html"));
  });

  s_server.begin();
  LOGI("web: server started");
}

void webApplySettings() {
  String json = settingsToJson(true);
  Lock l;
  s_user = (const char *)(S("admin")["user"] | "admin");
  s_pass = (const char *)(S("admin")["pass"] | "");
  s_token = (const char *)(S("api")["token"] | "");
  s_settingsJson = json;
}

void webSnapshotLayout() {
  String j = uiLayoutJson();
  Lock l;
  s_layoutJson = j;
}

void webTick() {
  static uint32_t last = 0;
  if (millis() - last < 1000) return;
  last = millis();
  JsonDocument d;
  d["name"] = S("device")["name"] | "Desk Display";
  d["version"] = FW_VERSION;
  d["build"] = __DATE__ " " __TIME__;
  d["uptime_s"] = millis() / 1000;
  d["heap"] = ESP.getFreeHeap();
  d["heap_min"] = ESP.getMinFreeHeap();
  d["psram"] = ESP.getFreePsram();
  JsonObject n = d["net"].to<JsonObject>();
  n["connected"] = netConnected(); n["ap"] = netApMode(); n["ip"] = netIp(); n["ssid"] = netSsid();
  n["rssi"] = netRssi(); n["host"] = netHostname(); n["mac"] = WiFi.macAddress();
  BatteryInfo b = hwBattery();
  JsonObject bt = d["battery"].to<JsonObject>();
  bt["present"] = b.present; bt["charging"] = b.charging; bt["usb"] = b.usb; bt["percent"] = b.percent; bt["mv"] = b.mv;
  bt["capacity_mah"] = S("battery")["capacity_mah"] | 1000; bt["charge_ma"] = max(100, min(1000, (int)(S("battery")["capacity_mah"] | 1000) / 2));
  JsonObject u = d["ui"].to<JsonObject>();
  u["page"] = uiPageIndex(); u["pages"] = uiPageCount(); u["page_name"] = uiPageName(uiPageIndex());
  u["sleeping"] = uiSleeping(); u["night"] = uiNight(); u["rotation"] = hwRotation() * 90; u["orientation"] = inputOrientation();
  JsonObject in = d["input"].to<JsonObject>();
  in["last"] = inputLastEvent(); in["age_ms"] = inputLastEventAgeMs(); in["idle_ms"] = inputIdleMs();
  float roll, pitch, ang, dev; inputImuSnapshot(roll, pitch, ang, dev);
  in["roll"] = roll; in["pitch"] = pitch; in["angle"] = ang; in["motion_g"] = dev; in["imu"] = hwHasImu();
  d["time"] = (long)time(nullptr);
  d["time_valid"] = netTimeValid();
  const esp_partition_t *run = esp_ota_get_running_partition();
  d["partition"] = run ? run->label : "?";
  esp_ota_img_states_t st;
  if (run && esp_ota_get_state_partition(run, &st) == ESP_OK) d["ota_state"] = st == ESP_OTA_IMG_PENDING_VERIFY ? "pending_verify" : "valid";
  d["sound"] = soundStatus();
  { float fps, mbps, busy; hwDisplayStats(fps, mbps, busy);
    JsonObject ds = d["display"].to<JsonObject>(); ds["fps"] = fps; ds["mbps"] = mbps; ds["bus_busy_pct"] = busy; }
  d["has_admin_pass"] = String((const char *)(S("admin")["pass"] | "")).length() > 0;
  d["api_token"] = S("api")["token"] | "";
  String s; serializeJson(d, s);
  Lock l;
  s_statusJson = s;
}
