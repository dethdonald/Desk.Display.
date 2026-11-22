#include "net.h"
#include "settings.h"
#include "config.h"
#include "logbuf.h"
#include "hw.h"
#include "ui.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <esp_sntp.h>
#include <time.h>

static DNSServer s_dns;
static bool s_ap = false;
static bool s_mdns = false;
static uint32_t s_connectStart = 0;
static uint32_t s_lastAttempt = 0;
static String s_ssid, s_pass, s_host;
static bool s_timeValid = false;
static uint32_t s_lastRtcWrite = 0;
static bool s_wasConnected = false;

static void onTimeSync(struct timeval *) {
  s_timeValid = true;
  time_t now = time(nullptr);
  if (hwHasRtc() && millis() - s_lastRtcWrite > 3600000UL) {
    // RTC is written from the main loop (I2C belongs to it); just flag it here
    s_lastRtcWrite = 0;
  }
  LOGI("time: NTP sync ok (%ld)", (long)now);
}

static void applyTime() {
  const char *tz = S("time")["tz"] | "EST5EDT,M3.2.0,M11.1.0";
  const char *ntp = S("time")["ntp"] | "pool.ntp.org";
  sntp_set_time_sync_notification_cb(onTimeSync);
  configTzTime(tz, ntp, "time.google.com", "time.cloudflare.com");
}

static void startAp() {
  if (s_ap) return;
  uint8_t mac[6]; WiFi.macAddress(mac);
  char ssid[40]; snprintf(ssid, sizeof ssid, "%s-%02X%02X", AP_SSID_PREFIX, mac[4], mac[5]);
  WiFi.mode(s_ssid.length() ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP(ssid, strlen(AP_PASS) ? AP_PASS : nullptr);
  s_dns.start(53, "*", WiFi.softAPIP());
  s_ap = true;
  LOGI("wifi: setup network '%s' at http://%s/", ssid, WiFi.softAPIP().toString().c_str());
  uiToast(String("Wi-Fi setup: join ") + ssid, 8000);
}

static void stopAp() {
  if (!s_ap) return;
  s_dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  s_ap = false;
  LOGI("wifi: setup network closed");
}

static void startMdns() {
  if (s_mdns) MDNS.end();
  s_mdns = MDNS.begin(s_host.c_str());
  if (s_mdns) MDNS.addService("http", "tcp", 80);
}

static void connect() {
  s_lastAttempt = millis();
  if (s_ssid.isEmpty()) { startAp(); return; }
  LOGI("wifi: connecting to '%s'", s_ssid.c_str());
  WiFi.mode(s_ap ? WIFI_AP_STA : WIFI_STA);
  WiFi.setHostname(s_host.c_str());
  WiFi.begin(s_ssid.c_str(), s_pass.c_str());
  s_connectStart = millis();
}

void netInit() {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  s_ssid = (const char *)(S("wifi")["ssid"] | "");
  s_pass = (const char *)(S("wifi")["pass"] | "");
  s_host = (const char *)(S("device")["hostname"] | "deskdisplay");

  time_t rtc;
  if (hwRtcRead(rtc)) {                       // show a sane clock before NTP arrives
    struct timeval tv = {rtc, 0};
    settimeofday(&tv, nullptr);
    LOGI("time: loaded from RTC");
  }
  applyTime();
  connect();
}

void netApplySettings() {
  String ssid = (const char *)(S("wifi")["ssid"] | "");
  String pass = (const char *)(S("wifi")["pass"] | "");
  String host = (const char *)(S("device")["hostname"] | "deskdisplay");
  applyTime();
  bool reconnect = ssid != s_ssid || pass != s_pass;
  bool rehost = host != s_host;
  s_ssid = ssid; s_pass = pass; s_host = host;
  if (reconnect) { WiFi.disconnect(); connect(); }
  else if (rehost && WiFi.isConnected()) startMdns();
}

void netTick() {
  if (s_ap) s_dns.processNextRequest();
  bool up = WiFi.isConnected();
  if (up && !s_wasConnected) {
    LOGI("wifi: connected, IP %s, RSSI %d", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    startMdns();
    if (s_ap) { stopAp(); uiToast("Connected: " + WiFi.localIP().toString(), 5000); }
  }
  if (!up && s_wasConnected) LOGI("wifi: disconnected");
  s_wasConnected = up;

  if (!up) {
    // Fall back to the setup network if we can't get on Wi-Fi within 20 s.
    if (!s_ap && millis() - s_connectStart > 20000) startAp();
    if (s_ssid.length() && millis() - s_lastAttempt > 30000) connect();
  }

  // Keep the RTC in sync with NTP-derived system time (hourly).
  if (s_timeValid && hwHasRtc() && (s_lastRtcWrite == 0 || millis() - s_lastRtcWrite > 3600000UL)) {
    hwRtcWrite(time(nullptr));
    s_lastRtcWrite = millis() | 1;
  }
}

bool netConnected() { return WiFi.isConnected(); }
bool netApMode() { return s_ap; }
String netIp() { return WiFi.isConnected() ? WiFi.localIP().toString() : (s_ap ? WiFi.softAPIP().toString() : String("")); }
String netHostname() { return s_host; }
int netRssi() { return WiFi.isConnected() ? WiFi.RSSI() : 0; }
String netSsid() { return WiFi.isConnected() ? WiFi.SSID() : String(""); }
bool netTimeValid() { return s_timeValid || time(nullptr) > 1700000000; }
