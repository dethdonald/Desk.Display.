#include "fetcher.h"
#include "datastore.h"
#include "settings.h"
#include "logbuf.h"
#include "cmdq.h"
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <HTTPUpdate.h>
#include <vector>
#include <functional>

struct Source { String name, url, prefix; uint32_t intervalS = 60; bool enabled = true; uint32_t nextAt = 0; };

struct FetchCfg {
  bool wxEnabled = true; bool hasLoc = false; double lat = 0, lon = 0;
  String units = "imperial", place; uint32_t wxIntervalMin = 15;
  uint32_t claudeStaleMin = 60;
  String updRepo; bool updAuto = false; uint32_t updHours = 24;
  std::vector<Source> sources;
};

static FetchCfg s_cfg;
static SemaphoreHandle_t s_mtx;
static volatile bool s_refresh = true;
static volatile bool s_refreshWx = false;
static volatile bool s_cfgChanged = true;
static uint32_t s_wxNext = 0;

static String s_testUrl, s_testResult; static volatile bool s_testPending = false, s_testDone = true;
static String s_updUrl, s_updStatus = "idle"; static volatile bool s_updPending = false;
static String s_latest, s_latestUrl, s_latestNotes; static uint32_t s_checkedAt = 0; static String s_checkErr;
static volatile bool s_checkPending = false; static uint32_t s_nextCheck = 120000;   // first check 2 min after boot

static SemaphoreHandle_t mtx() { if (!s_mtx) s_mtx = xSemaphoreCreateMutex(); return s_mtx; }
struct Lock { Lock() { xSemaphoreTake(mtx(), portMAX_DELAY); } ~Lock() { xSemaphoreGive(mtx()); } };

// ---------------------------------------------------------------- helpers
static String utf8(uint32_t cp) {
  char b[4] = {(char)(0xE0 | (cp >> 12)), (char)(0x80 | ((cp >> 6) & 0x3F)), (char)(0x80 | (cp & 0x3F)), 0};
  return String(b);
}

struct Wmo { int code; const char *desc; uint16_t day, night; };
static const Wmo WMO[] = {
  {0, "Clear", 0xf00d, 0xf02e}, {1, "Mostly clear", 0xf00d, 0xf02e}, {2, "Partly cloudy", 0xf002, 0xf086},
  {3, "Overcast", 0xf013, 0xf013}, {45, "Fog", 0xf014, 0xf014}, {48, "Freezing fog", 0xf014, 0xf014},
  {51, "Light drizzle", 0xf01c, 0xf01c}, {53, "Drizzle", 0xf01c, 0xf01c}, {55, "Heavy drizzle", 0xf01c, 0xf01c},
  {56, "Freezing drizzle", 0xf0b5, 0xf0b5}, {57, "Freezing drizzle", 0xf0b5, 0xf0b5},
  {61, "Light rain", 0xf01a, 0xf01a}, {63, "Rain", 0xf019, 0xf019}, {65, "Heavy rain", 0xf019, 0xf019},
  {66, "Freezing rain", 0xf0b5, 0xf0b5}, {67, "Freezing rain", 0xf0b5, 0xf0b5},
  {71, "Light snow", 0xf01b, 0xf01b}, {73, "Snow", 0xf01b, 0xf01b}, {75, "Heavy snow", 0xf01b, 0xf01b},
  {77, "Snow grains", 0xf01b, 0xf01b}, {80, "Showers", 0xf01a, 0xf01a}, {81, "Showers", 0xf01a, 0xf01a},
  {82, "Heavy showers", 0xf019, 0xf019}, {85, "Snow showers", 0xf01b, 0xf01b}, {86, "Snow showers", 0xf01b, 0xf01b},
  {95, "Thunderstorm", 0xf01e, 0xf01e}, {96, "Thunderstorm", 0xf01e, 0xf01e}, {99, "Thunderstorm", 0xf01e, 0xf01e},
};
static const Wmo &wmo(int code) {
  for (auto &w : WMO) if (w.code == code) return w;
  return WMO[3];
}

// days-from-civil based UTC conversion (no TZ env involved)
static time_t timegmUtc(int y, int mo, int d, int h, int mi, int s) {
  y -= mo <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return ((time_t)(era * 146097L + (long)doe - 719468L)) * 86400 + h * 3600 + mi * 60 + s;
}

time_t parseIsoTime(const char *s) {
  int y, mo, d, h = 0, mi = 0, sec = 0;
  if (!s || sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &sec) < 3) return 0;
  time_t t = timegmUtc(y, mo, d, h, mi, sec);
  const char *tz = s + 19;
  while (*tz == '.' || isdigit((unsigned char)*tz)) tz++;       // fractional seconds
  if (*tz == '+' || *tz == '-') {
    int oh = 0, om = 0;
    sscanf(tz + 1, "%d:%d", &oh, &om);
    long off = oh * 3600L + om * 60L;
    t -= (*tz == '+') ? off : -off;
  }
  return t;
}

static bool httpGetJson(const String &url, JsonDocument &doc, String &err, size_t maxLen = 48 * 1024) {
  if (!WiFi.isConnected()) { err = "offline"; return false; }
  HTTPClient http;
  NetworkClient plain;
  NetworkClientSecure secure;
  bool https = url.startsWith("https://");
  if (https) secure.setInsecure();        // LAN/self-signed friendly; data is read-only display info
  http.setTimeout(8000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!(https ? http.begin(secure, url) : http.begin(plain, url))) { err = "bad URL"; return false; }
  http.addHeader("Accept", "application/json");
  http.useHTTP10(true);      // no chunked transfer encoding: getStream() must be plain JSON for the parser
  int code = http.GET();
  if (code != 200) { err = "HTTP " + String(code); http.end(); return false; }
  int len = http.getSize();
  if (len > (int)maxLen) { err = "response too large"; http.end(); return false; }
  DeserializationError e = deserializeJson(doc, http.getStream());
  http.end();
  if (e) { err = String("JSON: ") + e.c_str(); return false; }
  return true;
}

// ---------------------------------------------------------------- weather
static void fetchWeather(const FetchCfg &cfg) {
  if (!cfg.wxEnabled) return;
  if (!cfg.hasLoc) { dsSetStr("weather.desc", "Set location"); dsSetStr("weather.place", "Weather: set a location in Settings"); return; }
  bool imperial = cfg.units != "metric";
  char url[512];
  snprintf(url, sizeof url,
           "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m,is_day,precipitation"
           "&hourly=temperature_2m,weather_code,precipitation_probability,is_day"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset,precipitation_probability_max,wind_speed_10m_max"
           "&timezone=auto&timeformat=unixtime&forecast_days=6&forecast_hours=24%s",
           cfg.lat, cfg.lon, imperial ? "&temperature_unit=fahrenheit&wind_speed_unit=mph&precipitation_unit=inch" : "");
  JsonDocument doc;
  String err;
  if (!httpGetJson(url, doc, err)) { LOGI("weather: %s", err.c_str()); dsSetStr("weather.error", err); return; }
  JsonObject cur = doc["current"];
  int code = cur["weather_code"] | 0;
  bool day = (cur["is_day"] | 1) == 1;
  const Wmo &w = wmo(code);
  dsSetNum("weather.temp", cur["temperature_2m"] | 0.0);
  dsSetNum("weather.feels", cur["apparent_temperature"] | 0.0);
  dsSetNum("weather.humidity", cur["relative_humidity_2m"] | 0.0);
  dsSetNum("weather.wind", cur["wind_speed_10m"] | 0.0);
  dsSetNum("weather.precip", cur["precipitation"] | 0.0);
  dsSetNum("weather.code", code);
  dsSetNum("weather.is_day", day ? 1 : 0);
  dsSetStr("weather.icon", utf8(day ? w.day : w.night));
  dsSetStr("weather.desc", w.desc);
  dsSetStr("weather.unit", imperial ? "°F" : "°C");
  dsSetStr("weather.wind_unit", imperial ? "mph" : "km/h");
  dsSetStr("weather.place", cfg.place.length() ? cfg.place : String("Weather"));
  dsSetStr("weather.error", "");

  JsonObject d = doc["daily"];
  for (int i = 0; i < 6; i++) {
    String b = "weather.d" + String(i);
    int dc = d["weather_code"][i] | 0;
    dsSetNum(b + ".t", d["time"][i] | 0);
    dsSetNum(b + ".hi", d["temperature_2m_max"][i] | 0.0);
    dsSetNum(b + ".lo", d["temperature_2m_min"][i] | 0.0);
    dsSetNum(b + ".code", dc);
    dsSetNum(b + ".pp", d["precipitation_probability_max"][i] | 0);
    dsSetNum(b + ".wind", d["wind_speed_10m_max"][i] | 0.0);
    dsSetStr(b + ".icon", utf8(wmo(dc).day));
    dsSetStr(b + ".desc", wmo(dc).desc);
    if (i == 0) {
      dsSetNum("weather.hi", d["temperature_2m_max"][0] | 0.0);
      dsSetNum("weather.lo", d["temperature_2m_min"][0] | 0.0);
      dsSetNum("weather.pp", d["precipitation_probability_max"][0] | 0);
      dsSetNum("weather.sunrise", d["sunrise"][0] | 0);
      dsSetNum("weather.sunset", d["sunset"][0] | 0);
    }
  }
  JsonObject h = doc["hourly"];
  for (int i = 0; i < 24; i++) {
    String b = "weather.h" + String(i);
    int hc = h["weather_code"][i] | 0;
    bool hd = (h["is_day"][i] | 1) == 1;
    dsSetNum(b + ".t", h["time"][i] | 0);
    dsSetNum(b + ".temp", h["temperature_2m"][i] | 0.0);
    dsSetNum(b + ".pp", h["precipitation_probability"][i] | 0);
    dsSetNum(b + ".code", hc);
    dsSetStr(b + ".icon", utf8(hd ? wmo(hc).day : wmo(hc).night));
  }
  dsSetNum("weather.updated", time(nullptr));
  LOGI("weather: %.1f %s, %s", (double)(cur["temperature_2m"] | 0.0), imperial ? "F" : "C", w.desc);
}

// ---------------------------------------------------------------- custom sources
static void fetchSource(Source &s) {
  JsonDocument doc;
  String err;
  String prefix = s.prefix.length() ? s.prefix : s.name;
  if (httpGetJson(s.url, doc, err)) {
    int n = dsFlatten(prefix, doc.as<JsonVariantConst>());
    dsSetNum(prefix + ".updated", time(nullptr));
    dsSetStr(prefix + ".error", "");
    LOGI("source %s: %d keys", prefix.c_str(), n);
  } else {
    dsSetStr(prefix + ".error", err);
    LOGI("source %s: %s", prefix.c_str(), err.c_str());
  }
}

// ---------------------------------------------------------------- claude
bool claudeIngest(JsonVariantConst body, String &err) {
  if (!body.is<JsonObjectConst>()) { err = "expected a JSON object"; return false; }
  JsonVariantConst rl = body["rate_limits"];
  if (rl.isNull() && (body["five_hour"].is<JsonObjectConst>() || body["seven_day"].is<JsonObjectConst>())) rl = body;  // /usage API shape
  if (rl.isNull() && body["context_window"].isNull()) { err = "no rate_limits or context_window in payload"; return false; }

  if (rl.is<JsonObjectConst>()) {
    for (JsonPairConst kv : rl.as<JsonObjectConst>()) {
      if (!kv.value().is<JsonObjectConst>()) continue;
      String base = String("claude.") + kv.key().c_str();
      JsonVariantConst pct = kv.value()["used_percentage"];
      if (pct.isNull()) pct = kv.value()["utilization"];
      if (!pct.isNull()) dsSetNum(base + ".used_percentage", pct.as<double>());
      JsonVariantConst ra = kv.value()["resets_at"];
      if (ra.is<const char *>()) dsSetNum(base + ".resets_at", (double)parseIsoTime(ra.as<const char *>()));
      else if (!ra.isNull()) dsSetNum(base + ".resets_at", ra.as<double>());
      // keep any other scalar fields (e.g. spend limits) as-is
      for (JsonPairConst f : kv.value().as<JsonObjectConst>()) {
        if (!strcmp(f.key().c_str(), "used_percentage") || !strcmp(f.key().c_str(), "utilization") || !strcmp(f.key().c_str(), "resets_at")) continue;
        if (!f.value().is<JsonObjectConst>() && !f.value().is<JsonArrayConst>()) dsSetJson(base + "." + f.key().c_str(), f.value());
      }
    }
  }
  if (!body["context_window"]["used_percentage"].isNull()) dsSetNum("claude.context_window.used_percentage", body["context_window"]["used_percentage"].as<double>());
  if (body["model"]["display_name"].is<const char *>()) dsSetStr("claude.model.display_name", body["model"]["display_name"].as<const char *>());
  if (!body["cost"]["total_cost_usd"].isNull()) dsSetNum("claude.cost.total_cost_usd", body["cost"]["total_cost_usd"].as<double>());
  if (body["source"].is<const char *>()) dsSetStr("claude.source", body["source"].as<const char *>());
  dsSetNum("claude.updated", time(nullptr));
  dsSetNum("claude.stale", 0);
  double h5 = -1, wk = -1;
  dsGetNum("claude.five_hour.used_percentage", h5);
  dsGetNum("claude.seven_day.used_percentage", wk);
  LOGI("claude: usage received (5h %.0f%%, week %.0f%%)%s", h5, wk, rl.isNull() ? " - no rate_limits in this push" : "");
  return true;
}

static void claudeHousekeeping(const FetchCfg &cfg) {
  time_t now = time(nullptr);
  if (now < 1700000000) return;
  static const char *wins[] = {"five_hour", "seven_day", "seven_day_opus", "seven_day_sonnet"};
  for (auto w : wins) {
    double ra;
    String b = String("claude.") + w;
    if (dsGetNum(b + ".resets_at", ra) && ra > 0 && now > (time_t)ra) {
      double pct;
      if (dsGetNum(b + ".used_percentage", pct) && pct != 0) dsSetNum(b + ".used_percentage", 0);
    }
  }
  double upd;
  if (dsGetNum("claude.updated", upd)) dsSetNum("claude.stale", (now - (time_t)upd) > (time_t)cfg.claudeStaleMin * 60 ? 1 : 0);
}

// ---------------------------------------------------------------- task
static void runTest() {
  String url;
  { Lock l; url = s_testUrl; }
  JsonDocument doc;
  String err, out;
  if (httpGetJson(url, doc, err)) {
    JsonDocument res;
    res["ok"] = true;
    // Flatten into a preview list (not into the datastore)
    JsonObject keys = res["keys"].to<JsonObject>();
    std::function<void(const String &, JsonVariantConst, int &)> rec = [&](const String &p, JsonVariantConst v, int &n) {
      if (n > 200) return;
      if (v.is<JsonObjectConst>()) for (JsonPairConst kv : v.as<JsonObjectConst>()) rec(p.length() ? p + "." + kv.key().c_str() : String(kv.key().c_str()), kv.value(), n);
      else if (v.is<JsonArrayConst>()) { int i = 0; for (JsonVariantConst it : v.as<JsonArrayConst>()) rec(p + "." + String(i++), it, n); }
      else { keys[p] = v; n++; }
    };
    int n = 0;
    rec("", doc.as<JsonVariantConst>(), n);
    serializeJson(res, out);
  } else {
    JsonDocument res; res["ok"] = false; res["error"] = err;
    serializeJson(res, out);
  }
  Lock l; s_testResult = out; s_testDone = true;
}

static void runUpdate() {
  String url;
  { Lock l; url = s_updUrl; s_updStatus = "downloading"; }
  LOGI("ota: pulling %s", url.c_str());
  cmdPost(CMD_TOAST, "Downloading update...");
  NetworkClient plain; NetworkClientSecure secure;
  bool https = url.startsWith("https://");
  if (https) secure.setInsecure();
  httpUpdate.rebootOnUpdate(false);
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  HTTPUpdateResult r = https ? httpUpdate.update(secure, url) : httpUpdate.update(plain, url);
  Lock l;
  if (r == HTTP_UPDATE_OK) {
    s_updStatus = "ok: rebooting";
    LOGI("ota: update written, rebooting");
    cmdPost(CMD_REBOOT);
  } else if (r == HTTP_UPDATE_NO_UPDATES) {
    s_updStatus = "no update";
  } else {
    s_updStatus = "error: " + httpUpdate.getLastErrorString();
    LOGI("ota: %s", s_updStatus.c_str());
  }
}

// "1.10.2" > "1.9.9"
static int cmpVersion(const String &a, const String &b) {
  int ai[3] = {0, 0, 0}, bi[3] = {0, 0, 0};
  sscanf(a.c_str(), "%d.%d.%d", &ai[0], &ai[1], &ai[2]);
  sscanf(b.c_str(), "%d.%d.%d", &bi[0], &bi[1], &bi[2]);
  for (int i = 0; i < 3; i++) if (ai[i] != bi[i]) return ai[i] < bi[i] ? -1 : 1;
  return 0;
}

// Reads https://github.com/<repo>/releases/latest/download/version.json (published by the CI workflow).
static void checkForUpdate(const FetchCfg &cfg) {
  if (cfg.updRepo.isEmpty()) return;
  String url = "https://github.com/" + cfg.updRepo + "/releases/latest/download/version.json";
  JsonDocument doc;
  String err;
  bool ok = httpGetJson(url, doc, err, 8192);
  Lock l;
  s_checkedAt = time(nullptr);
  if (!ok) { s_checkErr = err == "HTTP 404" ? String("no release published yet") : err; LOGI("update: check failed (%s)", s_checkErr.c_str()); return; }
  s_checkErr = "";
  s_latest = (const char *)(doc["version"] | "");
  s_latestNotes = (const char *)(doc["notes"] | "");
  s_latestUrl = (const char *)(doc["ota_url"] | "");
  if (s_latestUrl.isEmpty() && s_latest.length())
    s_latestUrl = "https://github.com/" + cfg.updRepo + "/releases/download/v" + s_latest + "/deskdisplay-ota.bin";
  bool newer = s_latest.length() && cmpVersion(s_latest, FW_VERSION) > 0;
  LOGI("update: latest %s (running %s)%s", s_latest.c_str(), FW_VERSION, newer ? " - update available" : "");
  dsSetStr("sys.update", newer ? s_latest : String(""));
  if (newer && cfg.updAuto && !s_updPending) {
    LOGI("update: auto-installing %s", s_latest.c_str());
    s_updUrl = s_latestUrl; s_updStatus = "queued"; s_updPending = true;
  }
}

static void task(void *) {
  uint32_t lastHk = 0;
  for (;;) {
    FetchCfg cfg;
    { Lock l; cfg = s_cfg; }
    uint32_t now = millis();

    if (s_updPending) { s_updPending = false; runUpdate(); }
    if (WiFi.isConnected() && (s_checkPending || (cfg.updRepo.length() && (int32_t)(now - s_nextCheck) >= 0))) {
      s_checkPending = false;
      s_nextCheck = millis() + max<uint32_t>(1, cfg.updHours) * 3600000UL;
      checkForUpdate(cfg);
    }
    if (s_testPending) { s_testPending = false; runTest(); }

    if (WiFi.isConnected() && time(nullptr) > 1700000000) {
      bool force = s_refresh; s_refresh = false;
      if (s_cfgChanged) { s_cfgChanged = false; force = true; }
      if (force || s_refreshWx || (int32_t)(now - s_wxNext) >= 0) {
        s_refreshWx = false;
        fetchWeather(cfg);
        s_wxNext = millis() + cfg.wxIntervalMin * 60000UL;
      }
      bool anyDue = false;
      {
        Lock l;
        for (auto &s : s_cfg.sources) {
          if (!s.enabled || s.url.isEmpty()) continue;
          if (force || (int32_t)(now - s.nextAt) >= 0) { s.nextAt = now + s.intervalS * 1000UL; anyDue = true; }
        }
      }
      if (anyDue) {
        for (auto &s : cfg.sources) {
          if (!s.enabled || s.url.isEmpty()) continue;
          if (force || (int32_t)(now - s.nextAt) >= 0) fetchSource(s);
        }
      }
    }
    if (now - lastHk > 10000) { lastHk = now; claudeHousekeeping(cfg); }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

void fetcherStart() {
  mtx();
  fetcherApplySettings();
  xTaskCreatePinnedToCore(task, "fetcher", 24576, nullptr, 1, nullptr, 0);   // HTTPS (TLS handshake) needs a deep stack
}

void fetcherApplySettings() {
  FetchCfg c;
  JsonObject w = S("weather");
  c.wxEnabled = w["enabled"] | true;
  c.hasLoc = !w["lat"].isNull() && !w["lon"].isNull();
  c.lat = w["lat"] | 0.0; c.lon = w["lon"] | 0.0;
  c.units = (const char *)(w["units"] | "imperial");
  c.place = (const char *)(w["place"] | "");
  c.wxIntervalMin = max(5, (int)(w["interval_min"] | 15));
  c.claudeStaleMin = S("claude")["stale_after_min"] | 60;
  c.updRepo = (const char *)(S("update")["repo"] | "");
  c.updRepo.trim();
  c.updAuto = S("update")["auto"] | false;
  c.updHours = max(1, (int)(S("update")["check_hours"] | 24));
  for (JsonObject s : gSettings["sources"].as<JsonArray>()) {
    Source src;
    src.name = (const char *)(s["name"] | "source");
    src.url = (const char *)(s["url"] | "");
    src.prefix = (const char *)(s["prefix"] | "");
    if (src.prefix.isEmpty()) src.prefix = src.name;
    src.intervalS = max(10, (int)(s["interval_s"] | 60));
    src.enabled = s["enabled"] | true;
    c.sources.push_back(src);
  }
  Lock l;
  s_cfg = c;
  s_cfgChanged = true;
}

void fetcherRefreshAll() { s_refresh = true; }
void fetcherRefreshWeather() { s_refreshWx = true; }

void fetcherTestSource(const String &url) {
  Lock l;
  s_testUrl = url; s_testResult = ""; s_testDone = false; s_testPending = true;
}

String fetcherTestResult(bool &done) { Lock l; done = s_testDone; return s_testResult; }

void fetcherUpdateFromUrl(const String &url) { Lock l; s_updUrl = url; s_updStatus = "queued"; s_updPending = true; }
String fetcherUpdateStatus() { Lock l; return s_updStatus; }
void fetcherCheckUpdate() { s_checkPending = true; }
void fetcherUpdateInfo(JsonDocument &d) {
  Lock l;
  d["status"] = s_updStatus;
  d["current"] = FW_VERSION;
  d["latest"] = s_latest;
  d["latest_url"] = s_latestUrl;
  d["notes"] = s_latestNotes;
  d["available"] = s_latest.length() && cmpVersion(s_latest, FW_VERSION) > 0;
  d["checked_at"] = s_checkedAt;
  d["check_error"] = s_checkErr;
  d["checking"] = (bool)s_checkPending;
}
