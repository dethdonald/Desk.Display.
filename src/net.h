#pragma once
#include <Arduino.h>

// Wi-Fi station with automatic fallback to a setup access point + captive portal,
// mDNS (<hostname>.local), NTP with POSIX time zone, RTC sync.
void netInit();             // call after settings load
void netTick();             // main loop
void netApplySettings();    // re-read wifi/hostname/time settings (reconnects if needed)
bool netConnected();
bool netApMode();
String netIp();
String netHostname();
int netRssi();
String netSsid();
bool netTimeValid();
