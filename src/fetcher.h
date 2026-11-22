#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Background task: weather (Open-Meteo), custom JSON sources, Claude window housekeeping,
// and firmware updates pulled from a URL. Only writes to the datastore (thread-safe).
void fetcherStart();
void fetcherApplySettings();          // main loop: snapshot settings for the task
void fetcherRefreshAll();
void fetcherRefreshWeather();
// Claude Code status line payload (or the /usage API shape). Safe from any task.
bool claudeIngest(JsonVariantConst body, String &err);

// Source test: kick off, then poll status ("" while running)
void fetcherTestSource(const String &url);
String fetcherTestResult(bool &done);

// Firmware update from URL
void fetcherUpdateFromUrl(const String &url);
String fetcherUpdateStatus();
void fetcherCheckUpdate();                 // check GitHub releases now
void fetcherInstallLatest(const String &repoOrUrl);   // check + install newest release ("" = configured repo)
String normalizeRepo(String r);
void fetcherUpdateInfo(JsonDocument &d);   // status + latest release info

time_t parseIsoTime(const char *s);
