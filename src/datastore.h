#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Global key/value store every widget reads from. Anything can write into it:
// built-in fetchers (weather.*), pushes (claude.*, anything via /api/data),
// custom JSON sources (<prefix>.*) and the system (sys.*).
//
// Template syntax used by text-ish widgets:  "{weather.temp:0}°"  "{claude.five_hour.resets_at|in}"
//   {key}        value (numbers printed with up to 1 decimal)
//   {key:N}      number with N decimals
//   {key|in}     epoch seconds -> time until   ("2h 13m")
//   {key|ago}    epoch seconds -> time since   ("5m ago")
//   {key|time}   epoch seconds -> local clock  ("3:45 PM")
//   {key|day}    epoch seconds -> weekday      ("Mon")
//   {key|date}   epoch seconds -> "Nov 21"
//   {key|pct}    rounded number + "%"
//   {key|upper}  upper-case text
// Missing keys render as "--".

struct DsEntry {
  bool isNum = false;
  double num = 0;
  String str;
  uint32_t updated = 0;   // epoch seconds (or 0 if time unknown)
};

extern volatile bool gTimeH24;

void dsInit();
void dsSetNum(const String &key, double v);
void dsSetStr(const String &key, const String &v);
void dsSetJson(const String &key, JsonVariantConst v);   // number/bool/string
bool dsGet(const String &key, DsEntry &out);
bool dsGetNum(const String &key, double &out);
void dsRemovePrefix(const String &prefix);
// Flatten a JSON tree under prefix ("a.b.0.c"). Returns number of keys written.
int dsFlatten(const String &prefix, JsonVariantConst v, int maxKeys = 400);
String dsRender(const char *tmpl);
String dsFormatNumber(double v, int decimals);   // decimals < 0 = auto
String dsDuration(long seconds);
uint32_t dsVersion();                            // bumps on every write
void dsToJson(JsonDocument &doc);
