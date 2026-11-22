#pragma once
#include <Arduino.h>
#include <lvgl.h>

// Input mapping. Raw inputs become named events, events trigger actions according
// to settings.controls.bindings  (action -> [events]).
//
// Events:  tap double_tap long_press swipe_left swipe_right swipe_up swipe_down
//          edge_tap_left edge_tap_right tilt_left tilt_right tilt_forward tilt_back
//          shake flip pickup boot_short boot_long pwr_short
// Actions: next prev home sleep_toggle night_toggle show_qr refresh

void inputInit();
void inputApplySettings();
void inputTick();
void inputTouch(lv_event_code_t code, lv_point_t pt);   // from ui screen events
void inputCalibrate();                                  // store current orientation as "rest"
void inputDoAction(const String &action);
String inputLastEvent();
uint32_t inputLastEventAgeMs();
uint32_t inputIdleMs();
void inputNoteActivity();
void inputImuSnapshot(float &rollDeg, float &pitchDeg, float &angleDeg, float &dev);

// Provided by main.cpp
void sysToggleNight();
void sysRefreshData();
String sysSettingsUrl();
void sysResetAdminPassword();      // BOOT held for 10 s
