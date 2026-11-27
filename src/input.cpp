#include "input.h"
#include "ui.h"
#include "hw.h"
#include "settings.h"
#include "logbuf.h"
#include "config.h"
#include "sound.h"
#include "datastore.h"
#include "govee.h"
#include <map>
#include <vector>

struct Cfg {
  uint16_t cooldown = 500, edgeW = 90, swipeMin = 60, longMs = 700, dblMs = 300;
  float tiltTh = 25, tiltHold = 250; bool invLR = false, invFB = false;
  float rest[3] = {0, 0, 1};
  float shakeG = 1.0f; uint16_t flipHold = 800; float pickupG = 0.15f; float stillS = 5;
  bool autoRotate = false; uint16_t autoDefault = 15; uint32_t idleHome = 120; uint32_t sleepAfter = 0; uint32_t dimAfter = 0; bool awakePrinting = true;
};

static Cfg c;
static std::map<String, std::vector<String>> s_map;     // event -> actions
static String s_lastEvent;
static uint32_t s_lastEventMs = 0, s_lastActivity = 0;
static std::map<String, uint32_t> s_lastFire;

// touch state
static bool s_pressing = false, s_moved = false, s_longFired = false;
static lv_point_t s_pressPt, s_lastPt;
static uint32_t s_pressMs = 0;
static bool s_pendingTap = false; static uint32_t s_pendingTapMs = 0; static lv_point_t s_pendingPt;
static uint32_t s_lastTapMs = 0;

// buttons
static bool s_bootDown = false, s_bootLongFired = false, s_bootResetFired = false; static uint32_t s_bootMs = 0;
static uint32_t s_lastPmuPoll = 0;

// imu
static float gf[3] = {0, 0, 1};
static float s_roll = 0, s_pitch = 0, s_angle = 0, s_dev = 0;
static int s_tiltLR = 0, s_tiltFB = 0; static uint32_t s_tiltLRMs = 0, s_tiltFBMs = 0; static bool s_tiltLRFired = false, s_tiltFBFired = false;
static uint32_t s_flipMs = 0; static bool s_flipped = false;
static uint32_t s_stillSince = 0;
static uint32_t s_shakeWindow = 0; static int s_shakePeaks = 0; static bool s_shakeAbove = false;
static uint32_t s_lastImu = 0;

// screen rotation
static String s_rotMode = "auto"; static int s_rotOffset = 0; static bool s_rotReverse = false;
static int s_rotQ = -1, s_rotCand = -1; static uint32_t s_rotCandMs = 0;

static void applyRotation(bool fromImu = false) {
  int rot;
  if (s_rotMode == "auto") {
    if (s_rotQ < 0) return;
    rot = (s_rotReverse ? s_rotQ : -s_rotQ) + s_rotOffset;   // default direction verified on hardware
  } else {
    rot = s_rotMode.toInt() / 90;
  }
  rot = ((rot % 4) + 4) % 4;
  if (rot != hwRotation()) {
    LOGI("display: rotation %d", rot * 90);
    if (fromImu) soundPlay(SND_ROTATE);
    uiRotate(rot, fromImu);
  }
}

// paging
static int s_lastPage = -1; static uint32_t s_pageSince = 0;

static bool bound(const char *ev) { auto it = s_map.find(ev); return it != s_map.end() && !it->second.empty(); }

void inputApplySettings() {
  JsonObject k = S("controls");
  c.cooldown = k["cooldown_ms"] | 500;
  c.edgeW = k["edge_width"] | 90;
  c.swipeMin = k["swipe_min_px"] | 60;
  c.longMs = k["long_press_ms"] | 700;
  c.dblMs = k["double_tap_ms"] | 300;
  c.tiltTh = k["tilt"]["threshold_deg"] | 25.0f;
  c.tiltHold = k["tilt"]["hold_ms"] | 250.0f;
  c.invLR = k["tilt"]["invert_lr"] | false;
  c.invFB = k["tilt"]["invert_fb"] | false;
  JsonArray r = k["tilt"]["rest"];
  if (r.size() == 3) { c.rest[0] = r[0]; c.rest[1] = r[1]; c.rest[2] = r[2]; }
  c.shakeG = k["shake"]["threshold_g"] | 1.0f;
  c.flipHold = k["flip"]["hold_ms"] | 800;
  c.pickupG = k["pickup"]["threshold_g"] | 0.15f;
  c.stillS = k["pickup"]["still_s"] | 5.0f;
  c.autoRotate = k["auto_rotate"]["enabled"] | false;
  c.autoDefault = k["auto_rotate"]["default_s"] | 15;
  c.idleHome = (uint32_t)(k["idle_home_s"] | 120) * 1000;
  c.sleepAfter = (uint32_t)(S("display")["sleep_after_min"] | 0) * 60000;
  c.dimAfter = (uint32_t)(S("display")["dim_after_s"] | 0) * 1000;
  c.awakePrinting = S("display")["awake_while_printing"] | true;
  s_rotMode = (const char *)(S("display")["rotation"]["mode"] | "auto");
  s_rotOffset = S("display")["rotation"]["offset"] | 0;
  s_rotReverse = S("display")["rotation"]["reverse"] | false;
  applyRotation();

  s_map.clear();
  for (JsonPair kv : k["bindings"].as<JsonObject>())
    for (JsonVariant ev : kv.value().as<JsonArray>())
      s_map[ev.as<String>()].push_back(kv.key().c_str());

  // Guardrail: there must always be a way to change pages.
  bool nav = false;
  for (auto &kv : s_map) for (auto &a : kv.second) if (a == "next" || a == "prev") nav = true;
  if (!nav && !c.autoRotate) {
    s_map["swipe_left"].push_back("next");
    s_map["swipe_right"].push_back("prev");
    LOGI("input: no page navigation bound; enabling swipe left/right as a fallback");
  }
}

void inputInit() {
  s_lastActivity = millis();
  inputApplySettings();
}

void inputNoteActivity() { s_lastActivity = millis(); }
uint32_t inputIdleMs() { return millis() - s_lastActivity; }
String inputLastEvent() { return s_lastEvent; }
uint32_t inputLastEventAgeMs() { return s_lastEventMs ? millis() - s_lastEventMs : UINT32_MAX; }

void inputDoAction(const String &a) {
  LOGI("action: %s", a.c_str());
  if (a == "next") uiNext();
  else if (a == "prev") uiPrev();
  else if (a == "home") uiHome();
  else if (a == "sleep_toggle") uiSetSleep(!uiSleeping());
  else if (a == "sleep") uiSetSleep(true);
  else if (a == "wake") uiSetSleep(false);
  else if (a == "night_toggle") sysToggleNight();
  else if (a == "show_qr") { String u = sysSettingsUrl(); uiShowQr(u, "Scan to open settings\n" + u); }
  else if (a == "refresh") { sysRefreshData(); uiToast("Refreshing data"); }
  else if (a.startsWith("govee:")) {
    String err, scene = a.substring(6);
    if (goveeRunScene(scene, err)) uiToast("Lights: " + scene, 1200); else uiToast(err, 3000);
  } else if (a.startsWith("goto:")) uiGotoName(a.substring(5));
}

static bool isWakeEvent(const String &ev) {
  return ev == "tap" || ev == "double_tap" || ev == "long_press" || ev.startsWith("swipe") || ev.startsWith("edge_tap") ||
         ev == "boot_short" || ev == "boot_long" || ev == "pwr_short" || ev == "pickup";
}

static void emit(const String &ev) {
  uint32_t now = millis();
  s_lastEvent = ev;
  s_lastEventMs = now;
  if (ev != "flip") s_lastActivity = now;

  if (uiSleeping()) {
    if (isWakeEvent(ev)) { uiSetDim(false); uiSetSleep(false); }
    return;
  }
  if (uiOverlayVisible() && (ev.startsWith("boot") || ev == "pwr_short")) { uiHideOverlay(); return; }

  auto it = s_map.find(ev);
  if (it == s_map.end()) return;
  uint32_t &last = s_lastFire[ev];
  if (now - last < c.cooldown) return;
  last = now;
  for (auto &a : it->second) inputDoAction(a);
}

static void emitTap(lv_point_t pt) {
  if (pt.x < c.edgeW && bound("edge_tap_left")) emit("edge_tap_left");
  else if (pt.x > LCD_W - c.edgeW && bound("edge_tap_right")) emit("edge_tap_right");
  else emit("tap");
}

static bool s_pressOnButton = false, s_pressWoke = false;
bool inputPressMoved() { return s_moved || s_pressWoke; }
void inputTouch(lv_event_code_t code, lv_point_t pt, bool onButton) {
  uint32_t now = millis();
  if (code == LV_EVENT_PRESSED) {
    s_pressing = true; s_moved = false; s_longFired = false; s_pressOnButton = onButton; s_pressWoke = uiSleeping();
    s_pressPt = s_lastPt = pt; s_pressMs = now;
    s_lastActivity = now;
  } else if (code == LV_EVENT_PRESSING) {
    s_lastPt = pt;
    if (abs(pt.x - s_pressPt.x) > 20 || abs(pt.y - s_pressPt.y) > 20) s_moved = true;
  } else if (code == LV_EVENT_RELEASED) {
    if (!s_pressing) return;
    s_pressing = false;
    if (s_longFired) return;
    int dx = s_lastPt.x - s_pressPt.x, dy = s_lastPt.y - s_pressPt.y;
    if (abs(dx) >= c.swipeMin || abs(dy) >= c.swipeMin) {
      if (abs(dx) >= abs(dy)) emit(dx < 0 ? "swipe_left" : "swipe_right");
      else emit(dy < 0 ? "swipe_up" : "swipe_down");
      return;
    }
    if (s_moved) return;
    if (s_pressOnButton && !uiSleeping()) { s_lastTapMs = now; return; }   // the button handles it
    if (bound("double_tap") && !uiSleeping()) {
      if (s_pendingTap && now - s_pendingTapMs < c.dblMs) { s_pendingTap = false; emit("double_tap"); }
      else { s_pendingTap = true; s_pendingTapMs = now; s_pendingPt = s_pressPt; }
    } else {
      emitTap(s_pressPt);
    }
    s_lastTapMs = now;
  } else if (code == LV_EVENT_PRESS_LOST) {
    s_pressing = false;
  }
}

void inputCalibrate() {
  JsonArray r = gSettings["controls"]["tilt"]["rest"].to<JsonArray>();
  r.add(gf[0]); r.add(gf[1]); r.add(gf[2]);
  settingsSave();
  c.rest[0] = gf[0]; c.rest[1] = gf[1]; c.rest[2] = gf[2];
  LOGI("input: rest orientation set to %.2f %.2f %.2f", gf[0], gf[1], gf[2]);
  uiToast("Resting angle saved");
}

void inputRotationHere() {
  if (s_rotQ < 0) { uiToast("Hold it upright for a moment, then try again"); return; }
  int off = (((s_rotReverse ? -s_rotQ : s_rotQ) % 4) + 4) % 4;
  gSettings["display"]["rotation"]["offset"] = off;
  gSettings["display"]["rotation"]["mode"] = "auto";
  settingsSave();
  s_rotOffset = off; s_rotMode = "auto";
  applyRotation();
  LOGI("display: 'this way up' saved (offset %d)", off);
  uiToast("Saved: this way is up");
}

int inputOrientation() { return s_rotQ; }

void inputImuSnapshot(float &roll, float &pitch, float &angle, float &dev) {
  roll = s_roll; pitch = s_pitch; angle = s_angle; dev = s_dev;
}

static float wrap180(float a) { while (a > 180) a -= 360; while (a < -180) a += 360; return a; }

static void imuTick(uint32_t now) {
  float x, y, z;
  if (!hwReadAccel(x, y, z)) return;
  float mag = sqrtf(x * x + y * y + z * z);
  if (mag < 0.2f) return;
  s_dev = fabsf(mag - 1.0f);
  float n[3] = {x / mag, y / mag, z / mag};
  for (int i = 0; i < 3; i++) gf[i] = 0.75f * gf[i] + 0.25f * n[i];
  float gl = sqrtf(gf[0] * gf[0] + gf[1] * gf[1] + gf[2] * gf[2]);
  float g[3] = {gf[0] / gl, gf[1] / gl, gf[2] / gl};
  float rl = sqrtf(c.rest[0] * c.rest[0] + c.rest[1] * c.rest[1] + c.rest[2] * c.rest[2]);
  if (rl < 0.1f) rl = 1;
  float r[3] = {c.rest[0] / rl, c.rest[1] / rl, c.rest[2] / rl};
  const float R2D = 57.29578f;
  s_roll = (asinf(constrain(g[0], -1.f, 1.f)) - asinf(constrain(r[0], -1.f, 1.f))) * R2D * (c.invLR ? -1 : 1);
  s_pitch = wrap180((atan2f(g[1], g[2]) - atan2f(r[1], r[2])) * R2D) * (c.invFB ? -1 : 1);
  float dot = constrain(g[0] * r[0] + g[1] * r[1] + g[2] * r[2], -1.f, 1.f);
  s_angle = acosf(dot) * R2D;

  // orientation for auto-rotate: gravity direction within the screen plane, only while roughly upright
  if (fabsf(g[2]) < 0.8f) {
    float a = atan2f(g[0], g[1]) * R2D;
    int q = (((int)lroundf(a / 90.f)) % 4 + 4) % 4;
    if (fabsf(wrap180(a - q * 90.f)) < 30.f) {
      if (q != s_rotCand) { s_rotCand = q; s_rotCandMs = now; }
      else if (q != s_rotQ && now - s_rotCandMs > 700) { bool first = s_rotQ < 0; s_rotQ = q; applyRotation(!first); }
    }
  }

  // flip (face down relative to rest)
  if (s_angle > 135) {
    if (!s_flipMs) s_flipMs = now;
    if (!s_flipped && now - s_flipMs > c.flipHold) { s_flipped = true; emit("flip"); }
  } else if (s_angle < 60) {
    s_flipMs = 0;
    if (s_flipped) { s_flipped = false; if (uiSleeping()) emit("pickup"); }
  }

  if (s_angle < 100) {
    // left/right tilt
    int dir = s_roll > c.tiltTh ? 1 : s_roll < -c.tiltTh ? -1 : 0;
    if (dir != 0) {
      if (dir != s_tiltLR) { s_tiltLR = dir; s_tiltLRMs = now; s_tiltLRFired = false; }
      else if (!s_tiltLRFired && now - s_tiltLRMs > c.tiltHold) { s_tiltLRFired = true; emit(dir > 0 ? "tilt_right" : "tilt_left"); }
    } else if (fabsf(s_roll) < c.tiltTh * 0.5f) { s_tiltLR = 0; s_tiltLRFired = false; }
    // forward/back tilt
    dir = s_pitch > c.tiltTh ? 1 : s_pitch < -c.tiltTh ? -1 : 0;
    if (dir != 0) {
      if (dir != s_tiltFB) { s_tiltFB = dir; s_tiltFBMs = now; s_tiltFBFired = false; }
      else if (!s_tiltFBFired && now - s_tiltFBMs > c.tiltHold) { s_tiltFBFired = true; emit(dir > 0 ? "tilt_forward" : "tilt_back"); }
    } else if (fabsf(s_pitch) < c.tiltTh * 0.5f) { s_tiltFB = 0; s_tiltFBFired = false; }
  }

  // shake: 3 peaks above threshold within 600 ms
  bool above = s_dev > c.shakeG;
  if (above && !s_shakeAbove) {
    if (now - s_shakeWindow > 600) { s_shakeWindow = now; s_shakePeaks = 0; }
    if (++s_shakePeaks >= 3) { s_shakePeaks = 0; emit("shake"); }
  }
  s_shakeAbove = above;

  // pickup: motion after a period of stillness
  if (s_dev < 0.04f) { if (!s_stillSince) s_stillSince = now; }
  else if (s_dev > c.pickupG) {
    if (s_stillSince && now - s_stillSince > c.stillS * 1000) emit("pickup");
    s_stillSince = 0;
  }
}

void inputTick() {
  uint32_t now = millis();

  // touch: long press + delayed single tap (when double-tap is bound)
  if (s_pressing && !s_moved && !s_longFired && !s_pressOnButton && now - s_pressMs > c.longMs) { s_longFired = true; emit("long_press"); }
  if (s_pendingTap && now - s_pendingTapMs > c.dblMs) { s_pendingTap = false; emitTap(s_pendingPt); }

  // BOOT button
  bool down = hwBootButtonDown();
  if (down && !s_bootDown) { s_bootDown = true; s_bootMs = now; s_bootLongFired = false; s_bootResetFired = false; }
  else if (down && !s_bootResetFired && now - s_bootMs > 10000) { s_bootResetFired = true; sysResetAdminPassword(); }
  else if (down && !s_bootLongFired && now - s_bootMs > 700) { s_bootLongFired = true; emit("boot_long"); }
  else if (!down && s_bootDown) { s_bootDown = false; if (!s_bootLongFired && now - s_bootMs > 30) emit("boot_short"); }

  // PWR button (short press via PMU interrupt flags)
  if (now - s_lastPmuPoll > 120) { s_lastPmuPoll = now; if (hwPollPowerKeyShort()) emit("pwr_short"); }

  // IMU ~50 Hz
  if (hwHasImu() && now - s_lastImu >= 20) { s_lastImu = now; imuTick(now); }

  // paging automation
  int page = uiPageIndex();
  if (page != s_lastPage) { s_lastPage = page; s_pageSince = now; }
  if (!uiSleeping() && !uiOverlayVisible()) {
    uint32_t idle = now - s_lastActivity;
    if (c.autoRotate) {
      uint32_t dwell = (uiPageDuration(page) ? uiPageDuration(page) : c.autoDefault) * 1000UL;
      if (now - s_pageSince > dwell && idle > 5000) uiNext();
    } else if (c.idleHome && idle > c.idleHome && !uiPageStay(page)) {
      uiHome();
    }
    bool printing = false;
    if (c.awakePrinting) { DsEntry e; printing = dsGet("bambu.state", e) && (e.str == "RUNNING" || e.str == "PREPARE"); }
    if (printing) idle = 0;
    if (c.dimAfter && idle > c.dimAfter) uiSetDim(true);
    else if (uiDimmed() && idle < 1000) uiSetDim(false);
    if (c.sleepAfter && idle > c.sleepAfter) uiSetSleep(true);
  }
  if (uiOverlayVisible() && now - s_lastActivity > 30000) uiHideOverlay();
}
