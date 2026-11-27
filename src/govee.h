#pragma once
// Govee lights over the local LAN API (no cloud, no account).
// Each light needs "LAN Control" switched on in the Govee Home app (device settings).
//   discovery: JSON "scan" to multicast 239.255.255.250:4001, replies arrive on UDP 4002
//   control:   JSON to <light ip>:4003  (turn / brightness / colorwc)
#include <Arduino.h>
#include <ArduinoJson.h>

void goveeStart();                       // background task (discovery + sending)
void goveeApplySettings();
void goveeScan();                        // ask lights to report (results within ~2 s)
void goveeDevices(JsonArray out);        // lights seen: id, sku, ip, name, age
bool goveeRunScene(const String &nameOrIndex, String &err);
bool goveeTest(const String &id);        // blink one light so you can tell which it is
