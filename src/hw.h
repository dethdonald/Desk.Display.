#pragma once
#include <Arduino.h>
#include <lvgl.h>

// Hardware layer. Everything here touches the shared I2C bus or the display,
// so it must only be called from the main loop task.

struct BatteryInfo {
  bool present = false;
  bool charging = false;
  bool usb = false;
  int percent = -1;
  int mv = 0;
};

bool hwEarlyInit();                    // touch + IMU + display (shows something ASAP)
void hwBootMessage(const char *line1, const char *line2 = nullptr);
void hwBootText(const char *title, const char *body);   // multi-line diagnostics screen
bool hwInit();                         // PMU + RTC + LVGL (calls hwEarlyInit if needed)
void hwSetBrightness(uint8_t level);   // 0..255
void hwSetRotation(int quarterTurns);  // 0..3, software rotation of display + touch
int hwRotation();
void hwDisplayStats(float &fps, float &mbps, float &busyPct);   // refreshed once a second
void hwDisplayPower(bool on);
void hwSetTouchOrientation(bool swapXY, bool mirrorX, bool mirrorY);
lv_indev_t *hwTouchIndev();

bool hwHasImu();
bool hwReadAccel(float &x, float &y, float &z);   // in g

bool hwHasPmu();
BatteryInfo hwBattery();
bool hwPollPowerKeyShort();
int hwSetChargeCurrentFor(int capacityMah);   // returns the charge current set (mA)            // true once per short press of PWR

bool hwHasRtc();
bool hwRtcRead(time_t &utc);
void hwRtcWrite(time_t utc);

bool hwBootButtonDown();
