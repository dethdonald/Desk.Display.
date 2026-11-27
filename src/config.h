#pragma once
// Desk Display — board + build constants (Waveshare ESP32-S3-Touch-AMOLED-2.16)

#define FW_NAME     "DeskDisplay"
#ifndef FW_VERSION
#define FW_VERSION  "1.9.4"          // CI overrides this from the git tag
#endif

// Display (CO5300, QSPI)
#define LCD_SDIO0   4
#define LCD_SDIO1   5
#define LCD_SDIO2   6
#define LCD_SDIO3   7
#define LCD_SCLK    38
#define LCD_RESET   39
#define LCD_CS      12
#define LCD_W       480
#define LCD_H       480

// Shared I2C bus: touch (CST9220), PMU (AXP2101), IMU (QMI8658), RTC (PCF85063)
#define I2C_SDA     15
#define I2C_SCL     14
#define TP_INT      11
#define TP_RST      40

#define PIN_BOOT    0

#define GRID_CELLS  12                 // layout grid is GRID_CELLS x GRID_CELLS
#define CELL_PX     (LCD_W / GRID_CELLS)

#define CFG_DIR         "/"
#define SETTINGS_FILE   "/settings.json"
#define LAYOUT_FILE     "/layout.json"

#define AP_SSID_PREFIX  "DeskDisplay-Setup"
#define AP_PASS         ""             // open setup network (only active when Wi-Fi isn't configured / reachable)
