#include "settings.h"
#include "config.h"
#include "logbuf.h"
#include <LittleFS.h>

JsonDocument gSettings;

static const char DEFAULTS[] = R"JSON({
  "device":  {"name": "Desk Display", "hostname": "deskdisplay"},
  "wifi":    {"ssid": "", "pass": ""},
  "admin":   {"user": "admin", "pass": ""},
  "api":     {"token": ""},
  "time":    {"tz_name": "America/New_York", "tz": "EST5EDT,M3.2.0,M11.1.0", "ntp": "pool.ntp.org", "h24": false},
  "weather": {"enabled": true, "lat": null, "lon": null, "place": "", "units": "imperial", "interval_min": 15},
  "claude":  {"stale_after_min": 60},
  "display": {
    "brightness": 180,
    "pixel_shift": true,
    "rotation": {"mode": "auto", "offset": 0, "reverse": false},
    "sleep_after_min": 0,
    "night": {"enabled": true, "start": "22:30", "end": "07:00", "brightness": 25, "use_night_theme": true, "screen_off": false}
  },
  "touch":   {"swap_xy": true, "mirror_x": true, "mirror_y": false},
  "controls": {
    "bindings": {
      "next":         ["swipe_left", "edge_tap_right", "boot_short"],
      "prev":         ["swipe_right", "edge_tap_left"],
      "home":         ["double_tap", "swipe_down"],
      "sleep_toggle": ["pwr_short", "flip"],
      "night_toggle": [],
      "show_qr":      ["long_press", "boot_long"],
      "refresh":      ["swipe_up"]
    },
    "cooldown_ms": 500,
    "edge_width": 90,
    "swipe_min_px": 60,
    "long_press_ms": 700,
    "double_tap_ms": 300,
    "tilt":   {"threshold_deg": 25, "hold_ms": 250, "invert_lr": false, "invert_fb": false, "rest": [0, 0, 1]},
    "shake":  {"threshold_g": 1.0},
    "flip":   {"hold_ms": 800},
    "pickup": {"threshold_g": 0.15, "still_s": 5},
    "auto_rotate": {"enabled": false, "default_s": 15},
    "idle_home_s": 120,
    "transition": "slide",
    "transition_ms": 250,
    "page_dots": "on_change"
  },
  "sources": [],
  "sound":   {"enabled": true, "volume": 80, "page_change": true, "rotation": true, "quiet_at_night": true},
  "update":  {"repo": "dethdonald/Desk.Display.", "auto": false, "check_hours": 24}
})JSON";

const char *settingsDefaultsJson() { return DEFAULTS; }

static const char *SECRET_PATHS[][2] = {{"wifi", "pass"}, {"admin", "pass"}, {"api", "token"}};

// Recursively merge src into dst (objects merge, everything else replaces).
static void deepMerge(JsonVariant dst, JsonVariantConst src) {
  if (src.is<JsonObjectConst>() && dst.is<JsonObject>()) {
    for (JsonPairConst kv : src.as<JsonObjectConst>()) {
      if (kv.value().is<const char *>() && strcmp(kv.value().as<const char *>(), "********") == 0) continue;
      JsonVariant child = dst[kv.key()];
      if (kv.value().is<JsonObjectConst>() && child.is<JsonObject>()) deepMerge(child, kv.value());
      else dst[kv.key()] = kv.value();
    }
  } else {
    dst.set(src);
  }
}

void settingsLoad() {
  gSettings.clear();
  deserializeJson(gSettings, DEFAULTS);
  File f = LittleFS.open(SETTINGS_FILE, "r");
  if (!f) { LOGI("settings: using defaults"); return; }
  JsonDocument stored;
  DeserializationError e = deserializeJson(stored, f);
  f.close();
  if (e) { LOGI("settings: parse error (%s), using defaults", e.c_str()); return; }
  deepMerge(gSettings, stored);
  LOGI("settings: loaded");
}

bool settingsSave() {
  File f = LittleFS.open(SETTINGS_FILE ".tmp", "w");
  if (!f) { LOGI("settings: save failed"); return false; }
  serializeJson(gSettings, f);
  f.close();
  LittleFS.remove(SETTINGS_FILE);
  LittleFS.rename(SETTINGS_FILE ".tmp", SETTINGS_FILE);
  return true;
}

void settingsMerge(JsonVariantConst patch) {
  deepMerge(gSettings, patch);
  // "bindings" lists and "sources" arrays are replaced wholesale by deepMerge (arrays aren't merged).
}

String settingsToJson(bool mask) {
  JsonDocument copy;
  copy.set(gSettings);
  if (mask) {
    for (auto &p : SECRET_PATHS) {
      const char *v = copy[p[0]][p[1]] | "";
      if (v[0]) copy[p[0]][p[1]] = "********";
    }
  }
  String out;
  serializeJson(copy, out);
  return out;
}

void settingsFactoryReset(bool keepWifi) {
  String ssid = gSettings["wifi"]["ssid"] | "";
  String pass = gSettings["wifi"]["pass"] | "";
  gSettings.clear();
  deserializeJson(gSettings, DEFAULTS);
  if (keepWifi) { gSettings["wifi"]["ssid"] = ssid; gSettings["wifi"]["pass"] = pass; }
  settingsSave();
  LittleFS.remove(LAYOUT_FILE);
}
