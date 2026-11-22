#pragma once
#include <Arduino.h>
#include <lvgl.h>

// Layout engine: builds LVGL screens from layout.json and keeps widgets updated
// from the datastore. Main-loop only.

void uiInit();                                     // load saved layout (or default) and show home page
bool uiApplyLayout(const String &json, bool save, String *err = nullptr);
bool uiValidateLayout(const String &json, String &err);
String uiLayoutJson();                             // currently saved layout
String uiLayoutDefaultJson();
void uiRevertPreview();
void uiTick();                                     // call every loop

// Navigation
void uiNext();
void uiPrev();
void uiHome();
void uiGoto(int index, int dir = 0);
int uiPageIndex();
int uiPageCount();
String uiPageName(int index);
uint16_t uiPageDuration(int index);                // seconds for auto-rotate (0 = use default)

// Display state
void uiSetSleep(bool sleep);
bool uiSleeping();
void uiSetNight(bool night);
bool uiNight();
void uiApplyBrightness();
void uiApplySettings();                            // re-read display/time settings (theme/night/brightness)

// Overlays
void uiShowQr(const String &url, const String &caption);
bool uiOverlayVisible();
void uiHideOverlay();
void uiToast(const String &msg, uint32_t ms = 2000);
