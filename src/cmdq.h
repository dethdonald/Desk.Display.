#pragma once
#include <Arduino.h>

// Commands from the web server (async TCP task) to the main loop, which owns
// LVGL, the I2C bus, settings and the layout.
enum CmdType : uint8_t {
  CMD_SETTINGS_PATCH,   // payload: JSON patch
  CMD_LAYOUT_SAVE,      // payload: full layout JSON (already validated)
  CMD_LAYOUT_PREVIEW,   // payload: full layout JSON, shown but not saved
  CMD_ACTION,           // payload: action name, arg: page index for "goto"
  CMD_REBOOT,
  CMD_FACTORY_RESET,    // arg: 1 = keep Wi-Fi
  CMD_CALIBRATE_IMU,
  CMD_TOAST,            // payload: message
};

struct Cmd {
  CmdType type;
  int arg = 0;
  String payload;
};

void cmdInit();
bool cmdPost(CmdType type, const String &payload = String(), int arg = 0);
bool cmdTake(Cmd &out);   // non-blocking
