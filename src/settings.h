#pragma once
#include <ArduinoJson.h>

// All settings live in one JSON document (/settings.json on the config filesystem).
// Only the main loop task mutates it; the web server queues patches (see cmdq.h).
extern JsonDocument gSettings;

void settingsLoad();
bool settingsSave();
// Merge a patch into the live settings. Values equal to "********" (masked secrets) are ignored.
void settingsMerge(JsonVariantConst patch);
// Serialize settings; secrets are replaced with "********" when mask is true.
String settingsToJson(bool mask);
void settingsFactoryReset(bool keepWifi);
const char *settingsDefaultsJson();

// Convenience getters (main-loop use)
inline JsonObject S(const char *section) { return gSettings[section].as<JsonObject>(); }
