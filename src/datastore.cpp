#include "datastore.h"
#include "fonts.h"
#include <map>
#include <time.h>

volatile bool gTimeH24 = false;

static std::map<String, DsEntry> s_map;
static SemaphoreHandle_t s_mtx;
static volatile uint32_t s_version = 0;

struct Lock {
  Lock() { xSemaphoreTake(s_mtx, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(s_mtx); }
};

static uint32_t nowEpoch() {
  time_t t = time(nullptr);
  return t > 1700000000 ? (uint32_t)t : 0;
}

void dsInit() { s_mtx = xSemaphoreCreateMutex(); }
uint32_t dsVersion() { return s_version; }

void dsSetNum(const String &key, double v) {
  Lock l;
  DsEntry &e = s_map[key];
  if (e.isNum && e.num == v) { e.updated = nowEpoch(); return; }
  e.isNum = true; e.num = v; e.str = ""; e.updated = nowEpoch();
  s_version++;
}

void dsSetStr(const String &key, const String &v) {
  Lock l;
  DsEntry &e = s_map[key];
  if (!e.isNum && e.str == v) { e.updated = nowEpoch(); return; }
  e.isNum = false; e.str = v; e.updated = nowEpoch();
  s_version++;
}

void dsSetJson(const String &key, JsonVariantConst v) {
  if (v.is<bool>()) dsSetNum(key, v.as<bool>() ? 1 : 0);
  else if (v.is<double>()) dsSetNum(key, v.as<double>());
  else if (v.is<const char *>()) dsSetStr(key, v.as<const char *>());
  else if (v.isNull()) dsSetStr(key, "");
}

bool dsGet(const String &key, DsEntry &out) {
  Lock l;
  auto it = s_map.find(key);
  if (it == s_map.end()) return false;
  out = it->second;
  return true;
}

bool dsGetNum(const String &key, double &out) {
  DsEntry e;
  if (!dsGet(key, e)) return false;
  if (e.isNum) { out = e.num; return true; }
  char *end = nullptr;
  out = strtod(e.str.c_str(), &end);
  return end && end != e.str.c_str();
}

void dsRemovePrefix(const String &prefix) {
  Lock l;
  for (auto it = s_map.begin(); it != s_map.end();) {
    if (it->first.startsWith(prefix)) it = s_map.erase(it); else ++it;
  }
  s_version++;
}

static int flattenRec(const String &path, JsonVariantConst v, int budget) {
  if (budget <= 0) return 0;
  int n = 0;
  if (v.is<JsonObjectConst>()) {
    for (JsonPairConst kv : v.as<JsonObjectConst>()) {
      n += flattenRec(path + "." + kv.key().c_str(), kv.value(), budget - n);
      if (n >= budget) break;
    }
  } else if (v.is<JsonArrayConst>()) {
    int i = 0;
    for (JsonVariantConst item : v.as<JsonArrayConst>()) {
      n += flattenRec(path + "." + String(i++), item, budget - n);
      if (n >= budget) break;
    }
  } else {
    dsSetJson(path, v);
    n = 1;
  }
  return n;
}

int dsFlatten(const String &prefix, JsonVariantConst v, int maxKeys) {
  return flattenRec(prefix, v, maxKeys);
}

String dsFormatNumber(double v, int decimals) {
  char buf[32];
  if (decimals < 0) {
    if (fabs(v - llround(v)) < 0.05) snprintf(buf, sizeof buf, "%lld", (long long)llround(v));
    else snprintf(buf, sizeof buf, "%.1f", v);
  } else {
    snprintf(buf, sizeof buf, "%.*f", decimals, v);
  }
  if (strcmp(buf, "-0") == 0) return "0";
  return buf;
}

String dsDuration(long s) {
  if (s <= 0) return "now";
  long d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60;
  char buf[24];
  if (d > 0) snprintf(buf, sizeof buf, "%ldd %ldh", d, h);
  else if (h > 0) snprintf(buf, sizeof buf, "%ldh %ldm", h, m);
  else if (m > 0) snprintf(buf, sizeof buf, "%ldm", m);
  else snprintf(buf, sizeof buf, "%lds", s);
  return buf;
}

static String symbol(const String &name) {
  String n = name;
  if (n == "battery") {                       // pick the battery glyph from the live level
    double pct = -1, chg = 0;
    dsGetNum("sys.battery", pct);
    dsGetNum("sys.charging", chg);
    if (chg > 0) n = "charge";
    else if (pct < 0) n = "bat_full";
    else n = pct > 85 ? "bat_full" : pct > 60 ? "bat_3" : pct > 35 ? "bat_half" : pct > 12 ? "bat_1" : "bat_empty";
  }
  for (auto &s : SYMBOLS) if (n == s.name) return s.utf8;
  return "";
}

static String applyToken(const String &tok) {
  if (tok.startsWith("sym:")) return symbol(tok.substring(4));
  String key = tok, filter;
  int decimals = -1;
  int bar = key.indexOf('|');
  if (bar >= 0) { filter = key.substring(bar + 1); key = key.substring(0, bar); }
  int colon = key.indexOf(':');
  if (colon >= 0) { decimals = key.substring(colon + 1).toInt(); key = key.substring(0, colon); }
  key.trim(); filter.trim();

  DsEntry e;
  if (!dsGet(key, e)) return "--";
  double num = e.num;
  bool isNum = e.isNum;
  if (!isNum && filter.length()) {           // allow numeric strings / ISO dates for filters
    char *end = nullptr;
    double d = strtod(e.str.c_str(), &end);
    if (end && end != e.str.c_str() && *end == 0) { num = d; isNum = true; }
    else {
      struct tm t = {};
      if (strptime(e.str.c_str(), "%Y-%m-%dT%H:%M:%S", &t)) {
        // treat as UTC unless an offset is present (offsets are rare here; good enough)
        time_t epoch = mktime(&t);
        num = (double)epoch; isNum = true;
      }
    }
  }

  if (filter == "") {
    return isNum ? dsFormatNumber(num, decimals) : e.str;
  }
  if (filter == "upper") { String s = isNum ? dsFormatNumber(num, decimals) : e.str; s.toUpperCase(); return s; }
  if (!isNum) return e.str;
  if (filter == "pct") return dsFormatNumber(num, decimals < 0 ? 0 : decimals) + "%";
  time_t now = time(nullptr);
  time_t t = (time_t)num;
  if (t > 100000000000LL) t /= 1000;        // accept milliseconds
  if (filter == "in") return dsDuration((long)(t - now));
  if (filter == "ago") {
    long d = (long)(now - t);
    if (d < 60) return "now";
    return dsDuration(d) + " ago";
  }
  struct tm lt; localtime_r(&t, &lt);
  char buf[32];
  if (filter == "time") {
    strftime(buf, sizeof buf, gTimeH24 ? "%H:%M" : "%l:%M %p", &lt);
    String s = buf; s.trim(); return s;
  }
  if (filter == "day") { strftime(buf, sizeof buf, "%a", &lt); return buf; }
  if (filter == "date") { strftime(buf, sizeof buf, "%b %e", &lt); String s = buf; s.replace("  ", " "); return s; }
  if (filter == "hour") { strftime(buf, sizeof buf, gTimeH24 ? "%H" : "%l%p", &lt); String s = buf; s.trim(); return s; }
  return dsFormatNumber(num, decimals);
}

String dsRender(const char *tmpl) {
  String out;
  if (!tmpl) return out;
  const char *p = tmpl;
  while (*p) {
    if (*p == '{') {
      const char *end = strchr(p, '}');
      if (end) {
        out += applyToken(String(p + 1, end - p - 1));
        p = end + 1;
        continue;
      }
    }
    out += *p++;
  }
  return out;
}

void dsToJson(JsonDocument &doc) {
  Lock l;
  JsonObject o = doc["data"].to<JsonObject>();
  for (auto &kv : s_map) {
    JsonObject e = o[kv.first].to<JsonObject>();
    if (kv.second.isNum) e["v"] = kv.second.num; else e["v"] = kv.second.str;
    e["t"] = kv.second.updated;
  }
  doc["version"] = s_version;
}
