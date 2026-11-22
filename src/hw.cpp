#include "hw.h"
#include "config.h"
#include "logbuf.h"
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>
#include "TouchDrvCSTXXX.hpp"
#include <SensorQMI8658.hpp>
#include <SensorPCF85063.hpp>

static Arduino_DataBus *s_bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
static Arduino_CO5300 *s_gfx = new Arduino_CO5300(s_bus, LCD_RESET, 0, LCD_W, LCD_H, 0, 0, 0, 0);

static TouchDrvCST92xx s_touch;
static XPowersPMU s_pmu;
static SensorQMI8658 s_imu;
static SensorPCF85063 s_rtc;

static bool s_hasTouch = false, s_hasPmu = false, s_hasImu = false, s_hasRtc = false;
static volatile bool s_touchIrq = false;
static bool s_lastPressed = false;
static lv_indev_t *s_indev = nullptr;

static lv_disp_draw_buf_t s_drawBuf;
static lv_disp_drv_t s_dispDrv;
static lv_indev_drv_t s_indevDrv;

static void IRAM_ATTR onTouchIrq() { s_touchIrq = true; }

// CO5300 needs even start / odd end coordinates for every flushed area.
static void rounderCb(lv_disp_drv_t *, lv_area_t *a) {
  a->x1 &= ~1; a->y1 &= ~1;
  a->x2 |= 1;  a->y2 |= 1;
}

static void flushCb(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *px) {
  uint32_t w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1;
  s_gfx->draw16bitRGBBitmap(a->x1, a->y1, (uint16_t *)&px->full, w, h);
  lv_disp_flush_ready(drv);
}

static void touchReadCb(lv_indev_drv_t *, lv_indev_data_t *data) {
  static int16_t lx = 0, ly = 0;
  if (s_hasTouch && (s_touchIrq || s_lastPressed)) {
    s_touchIrq = false;
    int16_t x[2], y[2];
    uint8_t n = s_touch.getPoint(x, y, 1);
    s_lastPressed = n > 0;
    if (n) { lx = constrain(x[0], 0, LCD_W - 1); ly = constrain(y[0], 0, LCD_H - 1); }
  }
  data->point.x = lx;
  data->point.y = ly;
  data->state = s_lastPressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

static bool i2cProbe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

static bool s_displayUp = false;

// Bring the panel up the same way Waveshare's example does (touch, IMU, then display),
// so there's something on screen before slower steps like formatting storage.
bool hwEarlyInit() {
  pinMode(PIN_BOOT, INPUT_PULLUP);
  Wire.begin(I2C_SDA, I2C_SCL, 400000);

  s_touch.setPins(TP_RST, TP_INT);                 // before begin() so the controller gets reset
  s_hasTouch = s_touch.begin(Wire, CST92XX_SLAVE_ADDRESS, I2C_SDA, I2C_SCL);
  if (s_hasTouch) {
    s_touch.setMaxCoordinates(LCD_W, LCD_H);
    s_touch.setSwapXY(true);
    s_touch.setMirrorXY(true, false);
    pinMode(TP_INT, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(TP_INT), onTouchIrq, FALLING);
  }
  LOGI("hw: touch %s", s_hasTouch ? "ok" : "missing");

  s_hasImu = s_imu.begin(Wire, QMI8658_L_SLAVE_ADDRESS, I2C_SDA, I2C_SCL);
  if (s_hasImu) {
    s_imu.configAccelerometer(SensorQMI8658::ACC_RANGE_4G, SensorQMI8658::ACC_ODR_125Hz, SensorQMI8658::LPF_MODE_0);
    s_imu.enableAccelerometer();
  }
  LOGI("hw: IMU %s", s_hasImu ? "ok" : "missing");

  LOGI("hw: display init...");
  s_displayUp = s_gfx->begin();
  if (!s_displayUp) { LOGI("hw: display init FAILED"); return false; }
  s_gfx->setBrightness(200);
  s_bus->writeC8D8(0x36, 0xA0);                    // same orientation as Waveshare's example (matches touch mapping)
  s_gfx->fillScreen(0x0000);
  LOGI("hw: display ok");
  return true;
}

void hwBootMessage(const char *line1, const char *line2) {
  if (!s_displayUp) return;
  s_gfx->fillRect(0, 200, LCD_W, 90, 0x0000);
  s_gfx->setTextColor(0xFFFF);
  s_gfx->setTextSize(3);
  int16_t x1, y1; uint16_t w, h;
  s_gfx->getTextBounds(line1, 0, 0, &x1, &y1, &w, &h);
  s_gfx->setCursor((LCD_W - w) / 2, 210);
  s_gfx->print(line1);
  if (line2 && line2[0]) {
    s_gfx->setTextSize(2);
    s_gfx->setTextColor(0x8C51);
    s_gfx->getTextBounds(line2, 0, 0, &x1, &y1, &w, &h);
    s_gfx->setCursor((LCD_W - w) / 2, 255);
    s_gfx->print(line2);
  }
}

void hwBootText(const char *title, const char *body) {
  if (!s_displayUp) return;
  s_gfx->fillScreen(0x0000);
  s_gfx->setTextSize(3);
  s_gfx->setTextColor(0xFD20);
  s_gfx->setCursor(60, 70);
  s_gfx->print(title);
  s_gfx->setTextSize(2);
  s_gfx->setTextColor(0xFFFF);
  int y = 120;
  const char *p = body;
  while (*p && y < 430) {
    const char *nl = strchr(p, '\n');
    int n = nl ? (int)(nl - p) : (int)strlen(p);
    char line[40]; int k = min(n, 30);
    memcpy(line, p, k); line[k] = 0;
    s_gfx->setCursor(60, y);
    s_gfx->print(line);
    y += 24;
    p += n; if (*p == '\n') p++;
  }
}

bool hwInit() {
  if (!s_displayUp && !hwEarlyInit()) return false;

  s_hasPmu = s_pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, I2C_SDA, I2C_SCL);
  if (s_hasPmu) {
    s_pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    s_pmu.setChargeTargetVoltage(3);              // 4.2 V (same as Waveshare's example)
    s_pmu.enableBattDetection();
    s_pmu.enableBattVoltageMeasure();
    s_pmu.enableVbusVoltageMeasure();
    s_pmu.enableSystemVoltageMeasure();
    s_pmu.clearIrqStatus();
    s_pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ);
  }
  LOGI("hw: PMU %s", s_hasPmu ? "ok" : "missing");

  if (i2cProbe(0x51)) s_hasRtc = s_rtc.begin(Wire, I2C_SDA, I2C_SCL);
  LOGI("hw: RTC %s", s_hasRtc ? "ok" : "missing");

  // LVGL draw buffers live in PSRAM: the QSPI bus copies pixels into its own DMA buffer,
  // and internal RAM is needed for Wi-Fi, TLS (HTTPS) and the web server.
  lv_init();
  size_t lines = 80;
  size_t bufPx = LCD_W * lines;
  lv_color_t *b1 = (lv_color_t *)heap_caps_malloc(bufPx * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  lv_color_t *b2 = (lv_color_t *)heap_caps_malloc(bufPx * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  if (!b1 || !b2) { LOGI("hw: LVGL buffer alloc failed"); return false; }
  lv_disp_draw_buf_init(&s_drawBuf, b1, b2, bufPx);
  lv_disp_drv_init(&s_dispDrv);
  s_dispDrv.hor_res = LCD_W;
  s_dispDrv.ver_res = LCD_H;
  s_dispDrv.flush_cb = flushCb;
  s_dispDrv.rounder_cb = rounderCb;
  s_dispDrv.draw_buf = &s_drawBuf;
  s_dispDrv.sw_rotate = 1;                         // LVGL rotates the image (and touch) for auto-rotation
  lv_disp_drv_register(&s_dispDrv);

  lv_indev_drv_init(&s_indevDrv);
  s_indevDrv.type = LV_INDEV_TYPE_POINTER;
  s_indevDrv.read_cb = touchReadCb;
  s_indev = lv_indev_drv_register(&s_indevDrv);
  LOGI("hw: LVGL ready");
  return true;
}

void hwSetRotation(int quarterTurns) {
  lv_disp_t *d = lv_disp_get_default();
  if (!d) return;
  lv_disp_rot_t r = (lv_disp_rot_t)(((quarterTurns % 4) + 4) % 4);
  if (lv_disp_get_rotation(d) != r) lv_disp_set_rotation(d, r);
}

int hwRotation() {
  lv_disp_t *d = lv_disp_get_default();
  return d ? (int)lv_disp_get_rotation(d) : 0;
}

void hwSetBrightness(uint8_t level) { s_gfx->setBrightness(level); }

void hwDisplayPower(bool on) {
  if (on) s_gfx->displayOn(); else s_gfx->displayOff();
}

void hwSetTouchOrientation(bool swapXY, bool mirrorX, bool mirrorY) {
  if (!s_hasTouch) return;
  s_touch.setSwapXY(swapXY);
  s_touch.setMirrorXY(mirrorX, mirrorY);
}

lv_indev_t *hwTouchIndev() { return s_indev; }

bool hwHasImu() { return s_hasImu; }
bool hwReadAccel(float &x, float &y, float &z) {
  if (!s_hasImu) return false;
  return s_imu.getAccelerometer(x, y, z);
}

bool hwHasPmu() { return s_hasPmu; }
BatteryInfo hwBattery() {
  BatteryInfo b;
  if (!s_hasPmu) return b;
  b.present = s_pmu.isBatteryConnect();
  b.usb = s_pmu.isVbusIn();
  if (b.present) {
    b.charging = s_pmu.isCharging();
    b.percent = s_pmu.getBatteryPercent();
    b.mv = s_pmu.getBattVoltage();
  }
  return b;
}

bool hwPollPowerKeyShort() {
  if (!s_hasPmu) return false;
  s_pmu.getIrqStatus();
  bool pressed = s_pmu.isPekeyShortPressIrq();
  s_pmu.clearIrqStatus();
  return pressed;
}

bool hwHasRtc() { return s_hasRtc; }

// The RTC holds UTC. Convert without touching the TZ environment.
static time_t utcFromFields(int y, int mo, int d, int h, int mi, int s) {
  // days from civil (Howard Hinnant)
  y -= mo <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = era * 146097L + (long)doe - 719468L;
  return (time_t)days * 86400 + h * 3600 + mi * 60 + s;
}

bool hwRtcRead(time_t &utc) {
  if (!s_hasRtc) return false;
  RTC_DateTime dt = s_rtc.getDateTime();
  if (dt.getYear() < 2024 || dt.getYear() > 2099) return false;
  utc = utcFromFields(dt.getYear(), dt.getMonth(), dt.getDay(), dt.getHour(), dt.getMinute(), dt.getSecond());
  return true;
}

void hwRtcWrite(time_t utc) {
  if (!s_hasRtc) return;
  struct tm t; gmtime_r(&utc, &t);
  s_rtc.setDateTime(RTC_DateTime(t));
}

bool hwBootButtonDown() { return digitalRead(PIN_BOOT) == LOW; }
