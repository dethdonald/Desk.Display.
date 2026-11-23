#include "ui.h"
#include "config.h"
#include "fonts.h"
#include "datastore.h"
#include "settings.h"
#include "input.h"
#include "hw.h"
#include "logbuf.h"
#include "sound.h"
#include "media.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <vector>
#include <time.h>

// ---------------------------------------------------------------- default layout
static const char DEFAULT_LAYOUT[] = R"JSON({
 "version": 1,
 "grid": 12,
 "home": 0,
 "theme": {
  "bg": "#000000",
  "fg": "#F5F2EB",
  "muted": "#8C8A85",
  "accent": "#D97757",
  "ok": "#5FB87A",
  "warn": "#E8B04B",
  "bad": "#E5534B",
  "track": "#232323"
 },
 "night_theme": {
  "bg": "#000000",
  "fg": "#B5513A",
  "muted": "#6B2E22",
  "accent": "#B5513A",
  "ok": "#7A3A2A",
  "warn": "#9A4A30",
  "bad": "#C4553E",
  "track": "#1A0D0A"
 },
 "pages": [
  {
   "name": "Home",
   "enabled": true,
   "duration": 20,
   "widgets": [
    {
     "type": "ring",
     "x": 0,
     "y": 0,
     "w": 12,
     "h": 12,
     "key": "claude.five_hour.used_percentage",
     "thickness": 16,
     "start": 135,
     "sweep": 270,
     "color_mode": "fixed",
     "color": "#D97757",
     "track": "#2A1A14",
     "value_pos": "tip",
     "badge": "5h {claude.five_hour.used_percentage:0}%",
     "badge_size": 16,
     "text": "",
     "caption": ""
    },
    {
     "type": "ring",
     "x": 1,
     "y": 1,
     "w": 10,
     "h": 10,
     "key": "claude.seven_day.used_percentage",
     "thickness": 8,
     "start": 135,
     "sweep": 270,
     "color_mode": "fixed",
     "color": "#D97757",
     "track": "#2A1A14",
     "value_pos": "tip",
     "badge": "wk {claude.seven_day.used_percentage:0}%",
     "badge_size": 14,
     "text": "",
     "caption": ""
    },
    {
     "type": "clock",
     "x": 2,
     "y": 3,
     "w": 8,
     "h": 3,
     "size": 96,
     "ampm": true,
     "color": "fg"
    },
    {
     "type": "date",
     "x": 2,
     "y": 6,
     "w": 8,
     "h": 1,
     "fmt": "%A, %b %e",
     "size": 22,
     "color": "muted"
    },
    {
     "type": "weather",
     "x": 3,
     "y": 7,
     "w": 6,
     "h": 2,
     "icon_size": 48,
     "size": 32,
     "style": "row",
     "show_desc": false
    },
    {
     "type": "text",
     "x": 2,
     "y": 10,
     "w": 8,
     "h": 1,
     "size": 16,
     "color": "muted",
     "align": "center",
     "text": "Session resets in {claude.five_hour.resets_at|in}"
    }
   ]
  },
  {
   "name": "Claude",
   "enabled": true,
   "duration": 15,
   "widgets": [
    {
     "type": "text",
     "x": 1,
     "y": 0,
     "w": 10,
     "h": 2,
     "size": 28,
     "color": "fg",
     "align": "center",
     "text": "Claude usage"
    },
    {
     "type": "bar",
     "x": 1,
     "y": 2,
     "w": 10,
     "h": 3,
     "key": "claude.five_hour.used_percentage",
     "label": "Current session",
     "sub": "Resets in {claude.five_hour.resets_at|in}",
     "size": 22,
     "bar_h": 34,
     "value_pos": "inside",
     "value_size": 20,
     "color_mode": "fixed",
     "color": "#D97757",
     "track": "#2A1A14"
    },
    {
     "type": "bar",
     "x": 1,
     "y": 5,
     "w": 10,
     "h": 3,
     "key": "claude.seven_day.used_percentage",
     "label": "This week",
     "sub": "Resets {claude.seven_day.resets_at|day} {claude.seven_day.resets_at|time}",
     "size": 22,
     "bar_h": 34,
     "value_pos": "inside",
     "value_size": 20,
     "color_mode": "fixed",
     "color": "#D97757",
     "track": "#2A1A14"
    },
    {
     "type": "text",
     "x": 1,
     "y": 8,
     "w": 10,
     "h": 1,
     "size": 18,
     "color": "muted",
     "align": "center",
     "text": "Context {claude.context_window.used_percentage:0}%  {claude.model.display_name}"
    },
    {
     "type": "text",
     "x": 1,
     "y": 10,
     "w": 10,
     "h": 1,
     "size": 16,
     "color": "muted",
     "align": "center",
     "text": "Updated {claude.updated|ago}"
    }
   ]
  },
  {
   "name": "Weather",
   "enabled": true,
   "duration": 15,
   "widgets": [
    {
     "type": "text",
     "x": 1,
     "y": 0,
     "w": 10,
     "h": 2,
     "size": 24,
     "color": "muted",
     "align": "center",
     "text": "{weather.place}"
    },
    {
     "type": "weather",
     "x": 1,
     "y": 2,
     "w": 10,
     "h": 4,
     "icon_size": 128,
     "size": 64,
     "style": "row",
     "show_desc": false
    },
    {
     "type": "text",
     "x": 1,
     "y": 6,
     "w": 10,
     "h": 1,
     "size": 22,
     "color": "fg",
     "align": "center",
     "text": "{weather.desc}   H {weather.hi:0}°  L {weather.lo:0}°   {weather.wind:0} {weather.wind_unit}"
    },
    {
     "type": "forecast",
     "x": 0,
     "y": 7,
     "w": 12,
     "h": 4,
     "mode": "daily",
     "count": 5,
     "icon_size": 32,
     "size": 18,
     "show_wind": true
    }
   ]
  },
  {
   "name": "Info",
   "enabled": true,
   "duration": 10,
   "widgets": [
    {
     "type": "text",
     "x": 1,
     "y": 1,
     "w": 10,
     "h": 1,
     "size": 24,
     "color": "fg",
     "align": "center",
     "text": "{sys.name}"
    },
    {
     "type": "qr",
     "x": 4,
     "y": 3,
     "w": 4,
     "h": 4
    },
    {
     "type": "text",
     "x": 1,
     "y": 8,
     "w": 10,
     "h": 1,
     "size": 20,
     "color": "muted",
     "align": "center",
     "text": "{sys.url}"
    },
    {
     "type": "text",
     "x": 1,
     "y": 9,
     "w": 10,
     "h": 1,
     "size": 18,
     "color": "muted",
     "align": "center",
     "text": "{sym:wifi} {sys.rssi}dBm   {sym:battery} {sys.battery_text}"
    },
    {
     "type": "text",
     "x": 1,
     "y": 10,
     "w": 10,
     "h": 1,
     "size": 16,
     "color": "muted",
     "align": "center",
     "text": "v{sys.version}  up {sys.uptime_text}"
    }
   ]
  },
  {
   "name": "Printer",
   "enabled": false,
   "duration": 20,
   "stay": true,
   "bg": "#000000",
   "widgets": [
    {
     "type": "clock",
     "x": 3,
     "y": 0,
     "w": 6,
     "h": 2,
     "size": 40,
     "ampm": true,
     "color": "#FFFFFF",
     "ampm_color": "#9A9A9A"
    },
    {
     "type": "text",
     "x": 1,
     "y": 2,
     "w": 10,
     "h": 1,
     "size": 18,
     "color": "#9A9A9A",
     "align": "center",
     "overflow": "clip",
     "text": "{bambu.job}"
    },
    {
     "type": "text",
     "x": 0,
     "y": 3,
     "w": 12,
     "h": 3,
     "size": 96,
     "color": "#FFFFFF",
     "align": "center",
     "text": "{bambu.percent:0}%"
    },
    {
     "type": "bar",
     "x": 1,
     "y": 6,
     "w": 10,
     "h": 1,
     "key": "bambu.percent",
     "label": "",
     "sub": "",
     "bar_h": 24,
     "value_pos": "inside",
     "show_value": false,
     "color_mode": "fixed",
     "color": "#00AE42",
     "track": "#2B2B2B"
    },
    {
     "type": "text",
     "x": 1,
     "y": 7,
     "w": 10,
     "h": 1,
     "size": 24,
     "color": "#FFFFFF",
     "align": "center",
     "text": "Layer {bambu.layer:0} / {bambu.total_layers:0}"
    },
    {
     "type": "text",
     "x": 1,
     "y": 8,
     "w": 10,
     "h": 1,
     "size": 22,
     "color": "#00AE42",
     "align": "center",
     "text": "Done at {bambu.eta|time}"
    },
    {
     "type": "text",
     "x": 1,
     "y": 9,
     "w": 10,
     "h": 1,
     "size": 18,
     "color": "#9A9A9A",
     "align": "center",
     "text": "{bambu.remaining_text} left  ·  {bambu.state_text}"
    },
    {
     "type": "text",
     "x": 1,
     "y": 10,
     "w": 10,
     "h": 1,
     "size": 16,
     "color": "#9A9A9A",
     "align": "center",
     "text": "Nozzle {bambu.nozzle:0}°   Bed {bambu.bed:0}°"
    }
   ]
  },
 {
  "name": "Overview",
  "enabled": false,
  "duration": 20,
  "stay": true,
  "bg": "#000000",
  "widgets": [
   {
    "type": "clock",
    "x": 3,
    "y": 0,
    "w": 6,
    "h": 2,
    "size": 44,
    "ampm": true,
    "color": "#FFFFFF",
    "ampm_color": "#9A9A9A"
   },
   {
    "type": "bar",
    "x": 1,
    "y": 2,
    "w": 10,
    "h": 3,
    "key": "bambu.percent",
    "label": "Printer · {bambu.state_text}",
    "sub": "Layer {bambu.layer:0}/{bambu.total_layers:0}  ·  done {bambu.eta|time}",
   "empty": "Printer not connected",
    "size": 20,
    "bar_h": 30,
    "value_pos": "inside",
    "value_size": 18,
    "color_mode": "fixed",
    "color": "#00AE42",
    "track": "#16301F",
    "label_color": "#FFFFFF"
   },
   {
    "type": "bar",
    "x": 1,
    "y": 5,
    "w": 10,
    "h": 3,
    "key": "claude.five_hour.used_percentage",
    "label": "Claude session",
    "sub": "Resets in {claude.five_hour.resets_at|in}",
    "size": 20,
    "bar_h": 30,
    "value_pos": "inside",
    "value_size": 18,
    "color_mode": "fixed",
    "color": "#D97757",
    "track": "#2A1A14",
    "label_color": "#FFFFFF"
   },
   {
    "type": "bar",
    "x": 1,
    "y": 8,
    "w": 10,
    "h": 3,
    "key": "claude.seven_day.used_percentage",
    "label": "Claude week",
    "sub": "Resets {claude.seven_day.resets_at|day} {claude.seven_day.resets_at|time}",
    "size": 20,
    "bar_h": 30,
    "value_pos": "inside",
    "value_size": 18,
    "color_mode": "fixed",
    "color": "#D97757",
    "track": "#2A1A14",
    "label_color": "#FFFFFF"
   },
   {
    "type": "text",
    "x": 0,
    "y": 11,
    "w": 12,
    "h": 1,
    "size": 16,
    "color": "muted",
    "align": "center",
    "overflow": "clip",
    "text": "{weather.temp:0}° {weather.desc}  ·  Claude updated {claude.updated|ago}"
   }
  ]
 }
 ]
})JSON";

// ---------------------------------------------------------------- PSRAM JSON allocator
struct SpiRamAllocator : ArduinoJson::Allocator {
  void *allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
  void deallocate(void *p) override { heap_caps_free(p); }
  void *reallocate(void *p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
};
static SpiRamAllocator s_psAlloc;

// ---------------------------------------------------------------- state
struct Theme { lv_color_t bg, fg, muted, accent, ok, warn, bad, track; };

struct Widget {
  String type;
  JsonObjectConst p;
  lv_obj_t *box = nullptr;
  lv_obj_t *a = nullptr, *b = nullptr, *c = nullptr, *d = nullptr, *e = nullptr;
  int16_t mode = 0;                  // bar: 0 right, 1 inside, 2 above   ring: 0 center, 1 tip badge, 2 none
  int16_t gx = 0, gy = 0, gw = 0, gh = 0, fy = 0;   // cached geometry (bar rect / ring centre+radius)
  std::vector<lv_obj_t *> cols;      // forecast columns: [label, icon, temp] * n
  String cache[4];
  int32_t lastVal = INT32_MIN;
};

struct Page {
  lv_obj_t *scr = nullptr;
  lv_obj_t *canvas = nullptr;
  std::vector<Widget> widgets;
  String name;
  uint16_t duration = 0;
  bool stay = false;                            // don't return home from this page when idle
  lv_img_dsc_t *snap = nullptr;                 // cached picture of the page, used by the slide transition
  bool snapOk = false;                          // snap still matches what the page shows
};

static JsonDocument *s_layout = nullptr;        // backing doc for the live pages
static String s_savedJson;                      // last saved layout
static bool s_previewing = false;
static std::vector<Page> s_pages;
static int s_cur = 0;
static int s_home = 0;
static Theme s_theme;
static bool s_night = false, s_sleep = false;
static uint32_t s_lastUpdate = 0, s_lastShift = 0;
static int s_shiftIdx = 0;
static lv_obj_t *s_dots = nullptr;
static uint32_t s_dotsHideAt = 0;
static lv_obj_t *s_overlay = nullptr;
static lv_obj_t *s_toast = nullptr;
static uint32_t s_toastHideAt = 0;
static int s_lastSecond = -1;
static bool s_dipping = false;      // brightness "dip" transition running
static void transFinishNow();

// ---------------------------------------------------------------- helpers
static lv_color_t parseHex(const char *s, lv_color_t def) {
  if (!s || s[0] != '#' || strlen(s) < 7) return def;
  return lv_color_hex(strtoul(s + 1, nullptr, 16));
}

static lv_color_t themeColor(const char *name, lv_color_t def) {
  if (!name || !name[0]) return def;
  if (name[0] == '#') return parseHex(name, def);
  if (!strcmp(name, "fg")) return s_theme.fg;
  if (!strcmp(name, "muted")) return s_theme.muted;
  if (!strcmp(name, "accent")) return s_theme.accent;
  if (!strcmp(name, "ok")) return s_theme.ok;
  if (!strcmp(name, "warn")) return s_theme.warn;
  if (!strcmp(name, "bad")) return s_theme.bad;
  if (!strcmp(name, "track")) return s_theme.track;
  if (!strcmp(name, "bg")) return s_theme.bg;
  return def;
}

static void loadTheme(JsonObjectConst t) {
  s_theme.bg = parseHex(t["bg"] | "#000000", lv_color_black());
  s_theme.fg = parseHex(t["fg"] | "#FFFFFF", lv_color_white());
  s_theme.muted = parseHex(t["muted"] | "#888888", lv_color_hex(0x888888));
  s_theme.accent = parseHex(t["accent"] | "#D97757", lv_color_hex(0xD97757));
  s_theme.ok = parseHex(t["ok"] | "#5FB87A", lv_color_hex(0x5FB87A));
  s_theme.warn = parseHex(t["warn"] | "#E8B04B", lv_color_hex(0xE8B04B));
  s_theme.bad = parseHex(t["bad"] | "#E5534B", lv_color_hex(0xE5534B));
  s_theme.track = parseHex(t["track"] | "#222222", lv_color_hex(0x222222));
}

static lv_obj_t *plainObj(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

static lv_obj_t *mkLabel(lv_obj_t *parent, int size, lv_color_t color) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, fontFor(size), 0);
  lv_obj_set_style_text_color(l, color, 0);
  lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
  lv_label_set_text(l, "");
  return l;
}

static uint32_t s_visualChanges = 0;            // bumped whenever a widget's look changes
static void setText(lv_obj_t *l, String &cache, const String &txt) {
  if (!l || cache == txt) return;
  cache = txt;
  s_visualChanges++;
  lv_label_set_text(l, txt.c_str());
}

static lv_text_align_t alignOf(JsonObjectConst p, lv_text_align_t def = LV_TEXT_ALIGN_CENTER) {
  const char *a = p["align"] | "";
  if (!strcmp(a, "left")) return LV_TEXT_ALIGN_LEFT;
  if (!strcmp(a, "right")) return LV_TEXT_ALIGN_RIGHT;
  if (!strcmp(a, "center")) return LV_TEXT_ALIGN_CENTER;
  return def;
}

static lv_color_t valueColor(JsonObjectConst p, double v, bool has) {
  const char *mode = p["color_mode"] | "fixed";
  if (!strcmp(mode, "threshold") && has) {
    double warn = p["warn"] | 60.0, bad = p["bad"] | 85.0;
    if (v >= bad) return s_theme.bad;
    if (v >= warn) return s_theme.warn;
    return s_theme.ok;
  }
  return themeColor(p["color"] | "accent", s_theme.accent);
}

static String keyOf(JsonObjectConst p) { return String((const char *)(p["key"] | "")); }


// ---------------------------------------------------------------- widget builders
static void buildWidget(Page &pg, JsonObjectConst p) {
  Widget w;
  w.type = (const char *)(p["type"] | "text");
  w.p = p;
  int gx = constrain((int)(p["x"] | 0), 0, GRID_CELLS - 1);
  int gy = constrain((int)(p["y"] | 0), 0, GRID_CELLS - 1);
  int gw = constrain((int)(p["w"] | 4), 1, GRID_CELLS - gx);
  int gh = constrain((int)(p["h"] | 1), 1, GRID_CELLS - gy);
  int W = gw * CELL_PX, H = gh * CELL_PX;

  w.box = plainObj(pg.canvas);
  lv_obj_set_pos(w.box, gx * CELL_PX, gy * CELL_PX);
  lv_obj_set_size(w.box, W, H);
  const char *bg = p["bg"] | "";
  if (bg[0]) {
    lv_obj_set_style_bg_color(w.box, themeColor(bg, s_theme.track), 0);
    lv_obj_set_style_bg_opa(w.box, (lv_opa_t)(int)(p["bg_opa"] | 255), 0);
  }
  lv_obj_set_style_radius(w.box, p["radius"] | 0, 0);
  int border = p["border"] | 0;
  if (border > 0) {
    lv_obj_set_style_border_width(w.box, border, 0);
    lv_obj_set_style_border_color(w.box, themeColor(p["border_color"] | "muted", s_theme.muted), 0);
  }
  int pad = p["pad"] | 0;
  lv_obj_set_style_pad_all(w.box, pad, 0);
  lv_color_t fg = themeColor(p["color"] | "fg", s_theme.fg);
  int size = p["size"] | 24;

  if (w.type == "clock") {
    lv_obj_set_flex_flow(w.box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(w.box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    w.a = mkLabel(w.box, size, fg);
    w.b = mkLabel(w.box, max(14, size / 3), themeColor(p["ampm_color"] | "muted", s_theme.muted));
    lv_obj_set_style_pad_bottom(w.b, size / 7, 0);
    lv_obj_set_style_pad_left(w.b, 6, 0);
  } else if (w.type == "date" || w.type == "text" || w.type == "value") {
    w.a = mkLabel(w.box, size, fg);
    lv_obj_set_width(w.a, W - 2 * pad);
    lv_obj_set_style_text_align(w.a, alignOf(p), 0);
    const char *ov = p["overflow"] | "wrap";
    lv_label_set_long_mode(w.a, !strcmp(ov, "scroll") ? LV_LABEL_LONG_SCROLL_CIRCULAR
                               : !strcmp(ov, "clip") ? LV_LABEL_LONG_CLIP : LV_LABEL_LONG_WRAP);
    if (w.type == "value") {
      w.b = mkLabel(w.box, p["label_size"] | 16, themeColor(p["label_color"] | "muted", s_theme.muted));
      lv_obj_set_width(w.b, W - 2 * pad);
      lv_obj_set_style_text_align(w.b, alignOf(p), 0);
      lv_obj_set_flex_flow(w.box, LV_FLEX_FLOW_COLUMN);
      lv_obj_set_flex_align(w.box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    } else {
      lv_obj_align(w.a, LV_ALIGN_LEFT_MID, 0, 0);
    }
  } else if (w.type == "bar") {
    const char *vp = p["value_pos"] | "right";
    w.mode = !strcmp(vp, "inside") ? 1 : !strcmp(vp, "follow") ? 2 : 0;
    int vsz = p["value_size"] | size;
    int lh = lv_font_get_line_height(fontFor(size));
    int vlh = lv_font_get_line_height(fontFor(vsz));
    int slh = lv_font_get_line_height(fontFor(p["sub_size"] | 16));
    int bh = p["bar_h"] | 14;
    bool hasLabel = strlen(p["label"] | "") > 0 || w.mode == 0;
    bool hasSub = strlen(p["sub"] | "") > 0;
    int cw = W - 2 * pad, ch = H - 2 * pad;
    int total = (hasLabel ? lh + 6 : 0) + (w.mode == 2 ? vlh + 2 : 0) + bh + (hasSub ? 6 + slh : 0);
    int y = max(0, (ch - total) / 2);
    lv_color_t lc = themeColor(p["label_color"] | "fg", s_theme.fg);   // text stays neutral; "color" is the bar fill
    w.a = mkLabel(w.box, size, lc);                     // label (top left)
    lv_obj_set_pos(w.a, 0, y);
    if (w.mode == 0) { w.c = mkLabel(w.box, vsz, lc); lv_obj_align(w.c, LV_ALIGN_TOP_RIGHT, 0, y); }
    if (hasLabel) y += lh + 6;
    if (w.mode == 2) { w.fy = y; y += vlh + 2; }
    w.d = lv_bar_create(w.box);
    lv_obj_clear_flag(w.d, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(w.d, 0, y);
    lv_obj_set_size(w.d, cw, bh);
    lv_bar_set_range(w.d, 0, 1000);
    lv_obj_set_style_bg_color(w.d, themeColor(p["track"] | "track", s_theme.track), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(w.d, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(w.d, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(w.d, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    w.gx = 0; w.gy = y; w.gw = cw; w.gh = bh;
    if (w.mode == 1) w.e = mkLabel(w.d, vsz, lc);       // rides inside the bar
    if (w.mode == 2) w.e = mkLabel(w.box, vsz, lc);     // rides above the bar
    y += bh;
    w.b = mkLabel(w.box, p["sub_size"] | 16, s_theme.muted);  // sub text
    lv_obj_set_pos(w.b, 0, y + 6);
    if (!hasSub) lv_obj_add_flag(w.b, LV_OBJ_FLAG_HIDDEN);
  } else if (w.type == "ring") {
    int side = min(W, H) - 2 * pad;
    const char *vp = p["value_pos"] | "center";
    w.mode = !strcmp(vp, "tip") ? 1 : !strcmp(vp, "none") ? 2 : 0;
    w.d = lv_arc_create(w.box);
    lv_obj_set_size(w.d, side, side);
    lv_obj_center(w.d);
    lv_obj_remove_style(w.d, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(w.d, LV_OBJ_FLAG_CLICKABLE);
    int start = p["start"] | 135, sweep = constrain((int)(p["sweep"] | 270), 10, 360);
    lv_arc_set_bg_angles(w.d, start, start + sweep);
    lv_arc_set_rotation(w.d, 0);
    lv_arc_set_range(w.d, 0, 1000);
    lv_arc_set_value(w.d, 0);
    int th = p["thickness"] | 12;
    lv_obj_set_style_arc_width(w.d, th, LV_PART_MAIN);
    lv_obj_set_style_arc_width(w.d, th, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(w.d, themeColor(p["track"] | "track", s_theme.track), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(w.d, p["rounded"] | true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(w.d, p["rounded"] | true, LV_PART_MAIN);
    lv_obj_set_style_pad_all(w.d, 0, 0);
    w.a = mkLabel(w.box, size, fg);
    lv_obj_align(w.a, LV_ALIGN_CENTER, 0, -(int)(p["caption_size"] | 16) / 2);
    w.b = mkLabel(w.box, p["caption_size"] | 16, s_theme.muted);
    lv_obj_align(w.b, LV_ALIGN_CENTER, 0, size / 2 + 4);
    w.gx = (W - 2 * pad) / 2; w.gy = (H - 2 * pad) / 2; w.gw = side / 2 - th / 2;
    if (w.mode == 1) {                                  // badge riding the arc tip
      w.e = mkLabel(w.box, p["badge_size"] | 16, s_theme.bg);
      lv_obj_set_style_bg_opa(w.e, LV_OPA_COVER, 0);
      lv_obj_set_style_radius(w.e, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_pad_hor(w.e, 8, 0);
      lv_obj_set_style_pad_ver(w.e, 2, 0);
      lv_obj_add_flag(w.e, LV_OBJ_FLAG_HIDDEN);
    }
  } else if (w.type == "weather") {
    bool row = strcmp(p["style"] | "row", "row") == 0;
    lv_obj_set_flex_flow(w.box, row ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(w.box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(w.box, 14, 0);
    int is = p["icon_size"] | 64;
    w.a = lv_label_create(w.box);
    lv_obj_set_style_text_font(w.a, wxFontFor(is), 0);
    lv_obj_set_style_text_color(w.a, themeColor(p["icon_color"] | "fg", s_theme.fg), 0);
    lv_label_set_text(w.a, "");
    lv_obj_t *col = plainObj(w.box);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, row ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    w.b = mkLabel(col, size, fg);
    if (p["show_desc"] | false) w.c = mkLabel(col, p["desc_size"] | 18, s_theme.muted);
  } else if (w.type == "forecast") {
    lv_obj_set_flex_flow(w.box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(w.box, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    int n = constrain((int)(p["count"] | 5), 1, 8);
    int is = p["icon_size"] | 32;
    for (int i = 0; i < n; i++) {
      lv_obj_t *col = plainObj(w.box);
      lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
      lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
      lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      lv_obj_set_style_pad_row(col, 4, 0);
      lv_obj_t *l1 = mkLabel(col, size, s_theme.muted);
      lv_obj_t *ic = lv_label_create(col);
      lv_obj_set_style_text_font(ic, wxFontFor(is), 0);
      lv_obj_set_style_text_color(ic, s_theme.fg, 0);
      lv_label_set_text(ic, "");
      lv_obj_t *l2 = mkLabel(col, size, fg);
      lv_obj_t *l3 = mkLabel(col, p["wind_size"] | 14, s_theme.muted);     // wind (hidden unless enabled)
      if (!(p["show_wind"] | false)) lv_obj_add_flag(l3, LV_OBJ_FLAG_HIDDEN);
      w.cols.push_back(l1); w.cols.push_back(ic); w.cols.push_back(l2); w.cols.push_back(l3);
    }
  } else if (w.type == "icon") {
    int is = p["size"] | 64;
    w.a = lv_label_create(w.box);
    lv_obj_set_style_text_font(w.a, wxFontFor(is), 0);
    lv_obj_set_style_text_color(w.a, fg, 0);
    lv_label_set_text(w.a, "");
    lv_obj_center(w.a);
  } else if (w.type == "qr") {
    int side = min(W, H) - 2 * pad - 8;
    w.d = lv_qrcode_create(w.box, side, lv_color_black(), lv_color_white());
    lv_obj_set_style_border_color(w.d, lv_color_white(), 0);
    lv_obj_set_style_border_width(w.d, 4, 0);
    lv_obj_center(w.d);
  } else if (w.type == "media") {
    w.d = lv_img_create(w.box);                         // frames are set by media.cpp
    lv_obj_center(w.d);
    w.b = mkLabel(w.box, 16, s_theme.muted);
    lv_obj_set_width(w.b, W - 2 * pad - 8);
    lv_obj_set_style_text_align(w.b, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(w.b, LV_LABEL_LONG_WRAP);
    lv_label_set_text(w.b, "");
    lv_obj_center(w.b);
  } else if (w.type == "box") {
    // decoration only (bg/border/radius already applied)
  } else {
    w.a = mkLabel(w.box, 14, s_theme.bad);
    lv_label_set_text_fmt(w.a, "unknown: %s", w.type.c_str());
  }
  pg.widgets.push_back(std::move(w));
}

// ---------------------------------------------------------------- widget updates
static void updateWidget(Widget &w, bool force) {
  JsonObjectConst p = w.p;
  if (force) { for (auto &c : w.cache) c = ""; w.lastVal = INT32_MIN; }

  if (w.type == "clock") {
    time_t now = time(nullptr);
    if (now < 1700000000) { setText(w.a, w.cache[0], "--:--"); return; }
    struct tm t; localtime_r(&now, &t);
    const char *mode = p["h24"] | "auto";
    bool h24 = !strcmp(mode, "24") || (!strcmp(mode, "auto") && gTimeH24);
    bool secs = p["seconds"] | false;
    char buf[16];
    if (h24) strftime(buf, sizeof buf, secs ? "%H:%M:%S" : "%H:%M", &t);
    else {
      int h = t.tm_hour % 12; if (h == 0) h = 12;
      if (secs) snprintf(buf, sizeof buf, "%d:%02d:%02d", h, t.tm_min, t.tm_sec);
      else snprintf(buf, sizeof buf, "%d:%02d", h, t.tm_min);
    }
    setText(w.a, w.cache[0], buf);
    setText(w.b, w.cache[1], (!h24 && (p["ampm"] | true)) ? (t.tm_hour < 12 ? "AM" : "PM") : "");
  } else if (w.type == "date") {
    time_t now = time(nullptr);
    if (now < 1700000000) { setText(w.a, w.cache[0], ""); return; }
    struct tm t; localtime_r(&now, &t);
    char buf[64];
    strftime(buf, sizeof buf, p["fmt"] | "%A, %b %e", &t);
    String s = buf; s.replace("  ", " ");
    setText(w.a, w.cache[0], s);
  } else if (w.type == "text") {
    setText(w.a, w.cache[0], dsRender(p["text"] | ""));
  } else if (w.type == "value") {
    setText(w.a, w.cache[0], dsRender(p["value"] | ""));
    setText(w.b, w.cache[1], dsRender(p["label"] | ""));
    double v; bool has = dsGetNum(keyOf(p), v);
    if (strcmp(p["color_mode"] | "fixed", "threshold") == 0) {
      int32_t q = has ? (int32_t)(v * 10) : -1;
      if (q != w.lastVal) { w.lastVal = q; s_visualChanges++; lv_obj_set_style_text_color(w.a, valueColor(p, v, has), 0); }
    }
  } else if (w.type == "bar" || w.type == "ring") {
    double v = 0; bool has = dsGetNum(keyOf(p), v);
    double mn = p["min"] | 0.0, mx = p["max"] | 100.0;
    double frac = has && mx > mn ? constrain((v - mn) / (mx - mn), 0.0, 1.0) : 0.0;
    int32_t q = has ? (int32_t)lround(frac * 1000) : -1;
    bool changed = q != w.lastVal;
    if (changed) {
      w.lastVal = q;
      s_visualChanges++;
      lv_color_t col = valueColor(p, v, has);
      if (w.type == "bar") {
        lv_bar_set_value(w.d, max((int32_t)0, q), LV_ANIM_OFF);
        lv_obj_set_style_bg_color(w.d, col, LV_PART_INDICATOR);
      } else {
        lv_arc_set_value(w.d, max((int32_t)0, q));
        lv_obj_set_style_arc_color(w.d, col, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(w.d, q > 0 ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_INDICATOR);
      }
    }
    String vt = has ? dsFormatNumber(v, 0) + ((p["unit"] | "%")) : String("--");
    if (w.type == "bar") {
      setText(w.a, w.cache[0], dsRender(p["label"] | ""));
      if (!(p["show_value"] | true)) vt = "";
      if (w.mode == 0) setText(w.c, w.cache[2], vt);
      setText(w.b, w.cache[1], has ? dsRender(p["sub"] | "") : String((const char *)(p["empty"] | "No data yet")));
      if (w.e && (w.cache[3] != vt || changed || force)) {
        w.cache[3] = vt;
        s_visualChanges++;
        lv_label_set_text(w.e, vt.c_str());
        lv_point_t sz;
        lv_txt_get_size(&sz, vt.c_str(), fontFor(p["value_size"] | (int)(p["size"] | 24)), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        int fillW = (int)lround(frac * w.gw);
        if (w.mode == 1) {
          int padIn = max(8, w.gh / 3);
          bool inside = fillW >= sz.x + 2 * padIn;
          int x = inside ? fillW - sz.x - padIn : fillW + padIn;
          if (x + sz.x > w.gw) x = w.gw - sz.x - padIn;
          lv_obj_set_pos(w.e, x, (w.gh - sz.y) / 2);
          lv_obj_set_style_text_color(w.e, inside ? themeColor(p["value_color"] | "bg", s_theme.bg) : s_theme.fg, 0);
        } else {
          int x = constrain(fillW - sz.x / 2, 0, max(0, w.gw - (int)sz.x));
          lv_obj_set_pos(w.e, w.gx + x, w.fy);
        }
      }
    } else {
      String key = keyOf(p);
      String defText = w.mode == 0 ? "{" + key + "|pct}" : String("");
      setText(w.a, w.cache[0], dsRender(p["text"].is<const char *>() ? (const char *)p["text"] : defText.c_str()));
      setText(w.b, w.cache[1], dsRender(p["caption"] | ""));
      if (w.e) {
        String defBadge = "{" + key + ":0}%";
        String bt = dsRender(p["badge"].is<const char *>() ? (const char *)p["badge"] : defBadge.c_str());
        if (!has) { if (!lv_obj_has_flag(w.e, LV_OBJ_FLAG_HIDDEN)) { s_visualChanges++; lv_obj_add_flag(w.e, LV_OBJ_FLAG_HIDDEN); } }
        else if (bt != w.cache[3] || changed || force) {
          w.cache[3] = bt;
          s_visualChanges++;
          lv_label_set_text(w.e, bt.c_str());
          lv_obj_set_style_bg_color(w.e, valueColor(p, v, has), 0);
          lv_obj_clear_flag(w.e, LV_OBJ_FLAG_HIDDEN);
          lv_point_t sz;
          lv_txt_get_size(&sz, bt.c_str(), fontFor(p["badge_size"] | 16), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
          int bw = sz.x + 16, bhh = sz.y + 4;
          int start = p["start"] | 135, sweep = constrain((int)(p["sweep"] | 270), 10, 360);
          float a = (start + sweep * frac) * 0.0174533f;
          int x = w.gx + (int)lroundf(w.gw * cosf(a)) - bw / 2;
          int y = w.gy + (int)lroundf(w.gw * sinf(a)) - bhh / 2;
          lv_obj_set_pos(w.e, x, y);
        }
      }
    }
  } else if (w.type == "weather") {
    DsEntry e;
    String icon = dsGet("weather.icon", e) ? e.str : String("");
    setText(w.a, w.cache[0], icon);
    const char *unit = strcmp(S("weather")["units"] | "imperial", "metric") == 0 ? "C" : "F";
    String t = dsRender("{weather.temp:0}°");
    if (p["show_unit"] | false) t += unit;
    setText(w.b, w.cache[1], t);
    if (w.c) setText(w.c, w.cache[2], dsRender("{weather.desc}"));
  } else if (w.type == "forecast") {
    bool hourly = strcmp(p["mode"] | "daily", "hourly") == 0;
    int step = hourly ? (int)(p["step"] | 2) : 1;
    int n = w.cols.size() / 4;
    DsEntry wu; String windUnit = dsGet("weather.wind_unit", wu) ? wu.str : String("mph");
    std::vector<String> txt;
    String agg;
    for (int i = 0; i < n; i++) {
      String base = hourly ? "weather.h" + String(i * step) : "weather.d" + String(i);
      String l1 = hourly ? dsRender(("{" + base + ".t|hour}").c_str()) : (i == 0 ? String("Today") : dsRender(("{" + base + ".t|day}").c_str()));
      DsEntry e; String ic = dsGet(base + ".icon", e) ? e.str : String("");
      String l2 = hourly ? dsRender(("{" + base + ".temp:0}°").c_str())
                         : dsRender(("{" + base + ".hi:0}°/{" + base + ".lo:0}°").c_str());
      String l3 = hourly ? String("") : dsRender(("{" + base + ".wind:0} ").c_str()) + windUnit;
      txt.push_back(l1); txt.push_back(ic); txt.push_back(l2); txt.push_back(l3);
      agg += l1 + "\x1f" + ic + "\x1f" + l2 + "\x1f" + l3 + "\x1e";
    }
    if (agg != w.cache[0]) {
      w.cache[0] = agg;
      s_visualChanges++;
      for (size_t i = 0; i < txt.size(); i++) lv_label_set_text(w.cols[i], txt[i].c_str());
    }
  } else if (w.type == "icon") {
    DsEntry e;
    String key = keyOf(p);
    String g = key.length() && dsGet(key, e) ? e.str : String((const char *)(p["glyph"] | ""));
    setText(w.a, w.cache[0], g);
  } else if (w.type == "qr") {
    DsEntry e;
    String url = dsGet("sys.url", e) ? e.str : String("");
    if (url.length() && url != w.cache[0]) {
      w.cache[0] = url;
      lv_qrcode_update(w.d, url.c_str(), url.length());
    }
  }
}

// ---------------------------------------------------------------- pages
static void screenEventCb(lv_event_t *e) {
  lv_indev_t *indev = lv_indev_get_act();
  lv_point_t pt = {0, 0};
  if (indev) lv_indev_get_point(indev, &pt);
  inputTouch(lv_event_get_code(e), pt);
}

// Bring a page's widgets up to date; drop its cached picture if anything visible changed.
static void updatePage(int i, bool force = false) {
  if (i < 0 || i >= (int)s_pages.size()) return;
  uint32_t before = s_visualChanges;
  for (auto &w : s_pages[i].widgets) updateWidget(w, force);
  if (force || s_visualChanges != before) s_pages[i].snapOk = false;
}
static void snapFree(Page &pg) {
  if (pg.snap) { lv_snapshot_free(pg.snap); pg.snap = nullptr; }
  pg.snapOk = false;
}
static void destroyPages(std::vector<Page> &pages) {
  for (auto &pg : pages) { snapFree(pg); if (pg.scr) lv_obj_del(pg.scr); }
  pages.clear();
}

static void updateDots();

static bool buildFromDoc(JsonDocument *doc) {
  JsonObjectConst root = doc->as<JsonObjectConst>();
  bool useNight = s_night && (S("display")["night"]["use_night_theme"] | true) && root["night_theme"].is<JsonObjectConst>();
  loadTheme(useNight ? root["night_theme"] : root["theme"]);

  std::vector<Page> pages;
  for (JsonObjectConst pj : root["pages"].as<JsonArrayConst>()) {
    if (!(pj["enabled"] | true)) continue;
    Page pg;
    pg.name = (const char *)(pj["name"] | "Page");
    pg.duration = pj["duration"] | 0;
    pg.stay = pj["stay"] | (pg.name == "Printer");
    pg.scr = lv_obj_create(nullptr);
    lv_obj_clear_flag(pg.scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(pg.scr, themeColor(pj["bg"] | "", s_theme.bg), 0);
    lv_obj_set_style_bg_opa(pg.scr, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(pg.scr, screenEventCb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(pg.scr, screenEventCb, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(pg.scr, screenEventCb, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(pg.scr, screenEventCb, LV_EVENT_PRESS_LOST, nullptr);
    pg.canvas = plainObj(pg.scr);
    lv_obj_set_size(pg.canvas, LCD_W, LCD_H);
    for (JsonObjectConst wj : pj["widgets"].as<JsonArrayConst>()) buildWidget(pg, wj);
    pages.push_back(std::move(pg));
  }
  if (pages.empty()) {                         // never leave the user with nothing on screen
    Page pg;
    pg.name = "Empty";
    pg.scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(pg.scr, lv_color_black(), 0);
    lv_obj_add_event_cb(pg.scr, screenEventCb, LV_EVENT_ALL, nullptr);
    pg.canvas = plainObj(pg.scr);
    lv_obj_set_size(pg.canvas, LCD_W, LCD_H);
    lv_obj_t *l = mkLabel(pg.canvas, 20, lv_color_white());
    lv_label_set_text(l, "No pages enabled.\nOpen the web UI to add one.");
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);
    pages.push_back(std::move(pg));
  }

  transFinishNow();
  lv_anim_del_all();
  s_dipping = false;
  int keep = s_pages.empty() ? -1 : s_cur;
  std::vector<Page> old = std::move(s_pages);
  s_pages = std::move(pages);
  s_home = constrain((int)(root["home"] | 0), 0, (int)s_pages.size() - 1);
  s_cur = (keep >= 0 && keep < (int)s_pages.size()) ? keep : s_home;
  updatePage(s_cur, true);
  lv_scr_load(s_pages[s_cur].scr);
  destroyPages(old);
  updateDots();
  return true;
}

bool uiValidateLayout(const String &json, String &err) {
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, json);
  if (e) { err = String("JSON error: ") + e.c_str(); return false; }
  if (!doc["pages"].is<JsonArray>() || doc["pages"].size() == 0) { err = "Layout needs at least one page"; return false; }
  int i = 0;
  for (JsonObject pg : doc["pages"].as<JsonArray>()) {
    if (!pg["widgets"].is<JsonArray>()) { err = "Page " + String(i) + " has no widgets array"; return false; }
    if (pg["widgets"].size() > 40) { err = "Page " + String(i) + " has more than 40 widgets"; return false; }
    i++;
  }
  if (i > 16) { err = "At most 16 pages"; return false; }
  return true;
}

static bool loadDocFrom(const String &json, JsonDocument *&out) {
  JsonDocument *doc = new JsonDocument(&s_psAlloc);
  if (deserializeJson(*doc, json)) { delete doc; return false; }
  out = doc;
  return true;
}

bool uiApplyLayout(const String &json, bool save, String *err) {
  String e;
  if (!uiValidateLayout(json, e)) { if (err) *err = e; LOGI("layout: rejected (%s)", e.c_str()); return false; }
  JsonDocument *doc = nullptr;
  if (!loadDocFrom(json, doc)) { if (err) *err = "parse failed"; return false; }
  JsonDocument *old = s_layout;
  s_layout = doc;
  buildFromDoc(s_layout);
  delete old;
  if (save) {
    File f = LittleFS.open(LAYOUT_FILE, "w");
    if (f) { f.print(json); f.close(); }
    s_savedJson = json;
    s_previewing = false;
    LOGI("layout: saved (%d pages)", (int)s_pages.size());
  } else {
    s_previewing = !s_savedJson.isEmpty();
    LOGI(s_previewing ? "layout: previewing (%d pages)" : "layout: loaded (%d pages)", (int)s_pages.size());
  }
  return true;
}

void uiRevertPreview() {
  if (s_previewing) uiApplyLayout(s_savedJson, false), s_previewing = false;
}

String uiLayoutJson() { return s_savedJson; }
String uiLayoutDefaultJson() { return DEFAULT_LAYOUT; }

// ---------------------------------------------------------------- dots / overlays
static void updateDots() {
  if (!s_dots) {
    s_dots = plainObj(lv_layer_top());
    lv_obj_set_size(s_dots, LV_SIZE_CONTENT, 16);
    lv_obj_align(s_dots, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_flex_flow(s_dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(s_dots, 8, 0);
  }
  lv_obj_clean(s_dots);
  int n = s_pages.size();
  for (int i = 0; i < n && n > 1; i++) {
    lv_obj_t *d = plainObj(s_dots);
    bool on = i == s_cur;
    lv_obj_set_size(d, on ? 18 : 8, 8);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(d, on ? s_theme.fg : s_theme.muted, 0);
  }
  const char *mode = S("controls")["page_dots"] | "on_change";
  if (!strcmp(mode, "never") || n < 2) lv_obj_add_flag(s_dots, LV_OBJ_FLAG_HIDDEN);
  else {
    lv_obj_clear_flag(s_dots, LV_OBJ_FLAG_HIDDEN);
    s_dotsHideAt = !strcmp(mode, "always") ? 0 : millis() + 1800;
  }
}

static void overlayTapCb(lv_event_t *) { uiHideOverlay(); }

void uiShowQr(const String &url, const String &caption) {
  if (!lv_is_initialized()) return;
  uiHideOverlay();
  s_overlay = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_overlay);
  lv_obj_set_size(s_overlay, LCD_W, LCD_H);
  lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
  lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_overlay, overlayTapCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *qr = lv_qrcode_create(s_overlay, 240, lv_color_black(), lv_color_white());
  lv_obj_set_style_border_color(qr, lv_color_white(), 0);
  lv_obj_set_style_border_width(qr, 8, 0);
  lv_qrcode_update(qr, url.c_str(), url.length());
  lv_obj_align(qr, LV_ALIGN_CENTER, 0, -40);
  lv_obj_t *l = mkLabel(s_overlay, 22, lv_color_white());
  lv_label_set_text(l, caption.c_str());
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(l, LV_ALIGN_CENTER, 0, 130);
  lv_obj_t *h = mkLabel(s_overlay, 16, lv_color_hex(0x888888));
  lv_label_set_text(h, "Tap to close");
  lv_obj_align(h, LV_ALIGN_BOTTOM_MID, 0, -24);
}

bool uiOverlayVisible() { return s_overlay != nullptr; }
void uiHideOverlay() {
  if (s_overlay) { lv_obj_del(s_overlay); s_overlay = nullptr; }
}

void uiToast(const String &msg, uint32_t ms) {
  if (!lv_is_initialized()) return;
  if (!s_toast) {
    s_toast = mkLabel(lv_layer_top(), 20, lv_color_white());
    lv_obj_set_style_bg_color(s_toast, lv_color_hex(0x202020), 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_toast, 18, 0);
    lv_obj_set_style_pad_hor(s_toast, 18, 0);
    lv_obj_set_style_pad_ver(s_toast, 10, 0);
  }
  lv_label_set_text(s_toast, msg.c_str());
  lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -40);
  lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
  s_toastHideAt = millis() + ms;
}

// ---------------------------------------------------------------- navigation
// ---------------------------------------------------------------- transitions
// Slide: both pages are drawn once into pictures, then only the pictures move (cheap per frame).
// Pictures of the current page and its neighbours are kept and refreshed in the background while
// nobody is touching the display, so most swipes start instantly instead of rendering two pages.
static lv_obj_t *s_tScr = nullptr;
static lv_obj_t *s_imgA = nullptr, *s_imgB = nullptr;
static int s_tTarget = -1;

static bool snapEnsure(int i) {
  Page &pg = s_pages[i];
  if (pg.snap && pg.snapOk) return true;
  lv_obj_update_layout(pg.scr);
  if (pg.snap) {
    uint32_t need = lv_snapshot_buf_size_needed(pg.scr, LV_IMG_CF_TRUE_COLOR);
    if (pg.snap->data_size >= need &&
        lv_snapshot_take_to_buf(pg.scr, LV_IMG_CF_TRUE_COLOR, pg.snap, (void *)pg.snap->data, pg.snap->data_size) == LV_RES_OK) {
      pg.snapOk = true; return true;
    }
    snapFree(pg);
  }
  pg.snap = lv_snapshot_take(pg.scr, LV_IMG_CF_TRUE_COLOR);
  pg.snapOk = pg.snap != nullptr;
  return pg.snapOk;
}

static void transFinishNow() {
  if (!s_tScr) return;
  if (s_imgA) lv_anim_del(s_imgA, nullptr);
  if (s_imgB) lv_anim_del(s_imgB, nullptr);
  if (s_tTarget >= 0 && s_tTarget < (int)s_pages.size()) lv_scr_load(s_pages[s_tTarget].scr);
  lv_obj_del(s_tScr);
  s_tScr = s_imgA = s_imgB = nullptr;
  s_tTarget = -1;
}

static void slideXCb(void *obj, int32_t v) { lv_obj_set_x((lv_obj_t *)obj, v); }
static void slideReadyCb(lv_anim_t *) { transFinishNow(); }

static bool slideTo(int from, int target, int dir, int ms) {
  if (from < 0 || from >= (int)s_pages.size() || lv_scr_act() != s_pages[from].scr) return false;
  if (!snapEnsure(from) || !snapEnsure(target)) return false;   // out of memory: just switch
  s_tTarget = target;
  s_tScr = lv_obj_create(nullptr);
  lv_obj_clear_flag(s_tScr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s_tScr, s_theme.bg, 0);
  s_imgA = lv_img_create(s_tScr); lv_img_set_src(s_imgA, s_pages[from].snap); lv_obj_set_pos(s_imgA, 0, 0);
  s_imgB = lv_img_create(s_tScr); lv_img_set_src(s_imgB, s_pages[target].snap); lv_obj_set_pos(s_imgB, dir * LCD_W, 0);
  lv_scr_load(s_tScr);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_exec_cb(&a, slideXCb);
  lv_anim_set_time(&a, ms);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_var(&a, s_imgA); lv_anim_set_values(&a, 0, -dir * LCD_W); lv_anim_start(&a);
  lv_anim_set_var(&a, s_imgB); lv_anim_set_values(&a, dir * LCD_W, 0);
  lv_anim_set_ready_cb(&a, slideReadyCb);
  lv_anim_start(&a);
  return true;
}

// Background upkeep of the cached pictures: only while idle and nothing is animating, at most one
// page drawn per pass.
static uint32_t s_snapWorkAt = 0, s_neighbourCheckAt = 0;
static void snapMaintain(uint32_t now) {
  int n = s_pages.size();
  if (n < 2 || s_tScr || s_dipping || s_sleep) return;
  if (strcmp(S("controls")["transition"] | "slide", "slide") != 0) {
    for (auto &pg : s_pages) if (pg.snap) snapFree(pg);
    return;
  }
  if (lv_disp_get_inactive_time(nullptr) < 1500 || now - s_snapWorkAt < 400) return;
  int want[3] = {s_cur, (s_cur + 1) % n, (s_cur + n - 1) % n};
  for (int i = 0; i < n; i++) if (i != want[0] && i != want[1] && i != want[2] && s_pages[i].snap) snapFree(s_pages[i]);
  if (now - s_neighbourCheckAt > 2000) { s_neighbourCheckAt = now; updatePage(want[1]); updatePage(want[2]); }
  for (int k = 0; k < 3; k++) {
    Page &pg = s_pages[want[k]];
    if (pg.snap && pg.snapOk) continue;
    snapEnsure(want[k]);
    s_snapWorkAt = millis();
    return;
  }
}

// Dip: fade the panel's own brightness down, do something while dark, fade back up.
// Costs no rendering at all, so it is perfectly smooth.
enum DipAction : uint8_t { DIP_NONE, DIP_PAGE, DIP_ROTATE };
static DipAction s_dipAction = DIP_NONE;
static int s_dipArg = 0, s_dipMs = 300;
static uint8_t currentBrightness();

static void dipBrightCb(void *, int32_t v) { hwSetBrightness((uint8_t)v); }
static void dipUpDone(lv_anim_t *) { s_dipping = false; uiApplyBrightness(); }
static void dipDownDone(lv_anim_t *) {
  if (s_dipAction == DIP_PAGE && s_dipArg >= 0 && s_dipArg < (int)s_pages.size()) lv_scr_load(s_pages[s_dipArg].scr);
  if (s_dipAction == DIP_ROTATE) hwSetRotation(s_dipArg);
  lv_refr_now(nullptr);                              // draw the new state completely while the screen is dark
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, &s_dipAction);
  lv_anim_set_exec_cb(&a, dipBrightCb);
  lv_anim_set_values(&a, 0, currentBrightness());
  lv_anim_set_time(&a, s_dipMs / 2);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_set_ready_cb(&a, dipUpDone);
  lv_anim_start(&a);
}

static void dip(DipAction act, int arg, int ms) {
  lv_anim_del(&s_dipAction, nullptr);
  s_dipAction = act; s_dipArg = arg; s_dipMs = max(80, ms); s_dipping = true;
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, &s_dipAction);
  lv_anim_set_exec_cb(&a, dipBrightCb);
  lv_anim_set_values(&a, currentBrightness(), 0);
  lv_anim_set_time(&a, s_dipMs / 2);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
  lv_anim_set_ready_cb(&a, dipDownDone);
  lv_anim_start(&a);
}

void uiGoto(int index, int dir) {
  int n = s_pages.size();
  if (n == 0) return;
  index = ((index % n) + n) % n;
  if (index == s_cur && dir == 0) return;
  transFinishNow();
  if (index != s_cur) soundPlay(SND_PAGE);
  int from = s_cur;
  s_cur = index;
  updatePage(s_cur);
  const char *tr = S("controls")["transition"] | "slide";
  int ms = S("controls")["transition_ms"] | 300;
  if (dir == 0) dir = index > from ? 1 : -1;
  bool animate = !s_sleep && ms > 0 && index != from;
  if (animate && !strcmp(tr, "slide")) {
    if (!slideTo(from, s_cur, dir, ms)) lv_scr_load(s_pages[s_cur].scr);
  } else if (animate && !strcmp(tr, "dim")) {
    dip(DIP_PAGE, s_cur, ms);
  } else if (animate && !strcmp(tr, "fade")) {
    lv_scr_load_anim(s_pages[s_cur].scr, LV_SCR_LOAD_ANIM_FADE_ON, ms, 0, false);
  } else {
    lv_scr_load(s_pages[s_cur].scr);
  }
  updateDots();
}

void uiRotate(int quarterTurns, bool animate) {
  quarterTurns = ((quarterTurns % 4) + 4) % 4;
  if (quarterTurns == hwRotation()) return;
  const char *fx = S("display")["rotation"]["effect"] | "dip";
  if (animate && !s_sleep && !strcmp(fx, "dip")) {
    transFinishNow();
    dip(DIP_ROTATE, quarterTurns, S("display")["rotation"]["effect_ms"] | 360);
  } else {
    hwSetRotation(quarterTurns);
  }
}

void uiNext() { uiGoto(s_cur + 1, 1); }
void uiPrev() { uiGoto(s_cur - 1, -1); }
void uiHome() { if (s_cur != s_home) uiGoto(s_home, s_home < s_cur ? -1 : 1); }
int uiPageIndex() { return s_cur; }
int uiPageCount() { return s_pages.size(); }
String uiPageName(int i) { return (i >= 0 && i < (int)s_pages.size()) ? s_pages[i].name : String(""); }
bool uiPageStay(int i) { return i >= 0 && i < (int)s_pages.size() && s_pages[i].stay; }
uint16_t uiPageDuration(int i) { return (i >= 0 && i < (int)s_pages.size()) ? s_pages[i].duration : 0; }

// ---------------------------------------------------------------- display state
static bool s_dimmed = false;
static uint8_t currentBrightness() {
  JsonObject d = S("display");
  int b = s_night ? (int)(d["night"]["brightness"] | 25) : (int)(d["brightness"] | 180);
  if (s_dimmed) b = min(b, max(1, b * constrain((int)(d["dim_percent"] | 15), 1, 100) / 100));
  return (uint8_t)constrain(b, 1, 255);
}
static int s_dimAnimVar;
static void dimExec(void *, int32_t v) { hwSetBrightness((uint8_t)v); }

void uiApplyBrightness() {
  if (s_sleep || s_dipping) return;
  hwSetBrightness(currentBrightness());
}

void uiSetSleep(bool sleep) {
  if (sleep == s_sleep) return;
  s_sleep = sleep;
  if (sleep) { hwSetBrightness(0); hwDisplayPower(false); LOGI("display: sleep"); }
  else { hwDisplayPower(true); uiApplyBrightness(); lv_obj_invalidate(lv_scr_act()); LOGI("display: wake"); }
}
bool uiSleeping() { return s_sleep; }

// Inactivity dim: fade to a fraction of the normal brightness (slow down, quick up).
void uiSetDim(bool dim) {
  if (dim == s_dimmed) return;
  uint8_t from = currentBrightness();
  s_dimmed = dim;
  LOGI("display: %s", dim ? "dimmed" : "undimmed");
  if (s_sleep || s_dipping) return;
  lv_anim_del(&s_dimAnimVar, dimExec);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, &s_dimAnimVar);
  lv_anim_set_exec_cb(&a, dimExec);
  lv_anim_set_values(&a, from, currentBrightness());
  lv_anim_set_time(&a, dim ? 1500 : 250);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_start(&a);
}
bool uiDimmed() { return s_dimmed; }

void uiSetNight(bool night) {
  if (night == s_night) return;
  s_night = night;
  LOGI("display: night %s", night ? "on" : "off");
  if (s_layout) buildFromDoc(s_layout);
  uiApplyBrightness();
}
bool uiNight() { return s_night; }

void uiApplySettings() {
  if (s_layout) buildFromDoc(s_layout);
  uiApplyBrightness();
}

// ---------------------------------------------------------------- media
// Only the media widget on the visible page plays; everything else is stopped.
static void mediaSync() {
  Widget *mw = nullptr;
  if (!s_sleep && !s_tScr && s_cur >= 0 && s_cur < (int)s_pages.size())
    for (auto &w : s_pages[s_cur].widgets) if (w.type == "media") { mw = &w; break; }
  if (!mw) { mediaStop(); return; }
  String file = mw->p["file"] | "";
  if (file.isEmpty()) { mediaStop(); setText(mw->b, mw->cache[0], "Choose a picture or video for this widget in Layout"); return; }
  mediaPlay(mw->d, file, 0, 0);
  setText(mw->b, mw->cache[0], mediaError());
  mediaPause(s_sleep);
  mediaTick();
}

// ---------------------------------------------------------------- init / tick
void uiInit() {
  String json;
  File f = LittleFS.open(LAYOUT_FILE, "r");
  if (f) { json = f.readString(); f.close(); }
  String err;
  if (json.isEmpty() || !uiValidateLayout(json, err)) {
    if (json.length()) LOGI("layout: saved layout invalid (%s), using default", err.c_str());
    json = DEFAULT_LAYOUT;
  }
  uiApplyLayout(json, false);
  s_savedJson = json;
  s_previewing = false;
  uiApplyBrightness();
}

void uiTick() {
  if (!lv_is_initialized()) return;
  uint32_t now = millis();
  if (s_toast && s_toastHideAt && now > s_toastHideAt) { lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN); s_toastHideAt = 0; }
  if (s_dots && s_dotsHideAt && now > s_dotsHideAt) { lv_obj_add_flag(s_dots, LV_OBJ_FLAG_HIDDEN); s_dotsHideAt = 0; }
  if (s_sleep || s_pages.empty()) return;

  // Burn-in protection: nudge every page's canvas by a couple of pixels every 2 minutes.
  if ((S("display")["pixel_shift"] | true) && now - s_lastShift > 120000) {
    s_lastShift = now;
    static const int8_t offs[][2] = {{0, 0}, {2, 0}, {2, 2}, {0, 2}, {-2, 2}, {-2, 0}, {-2, -2}, {0, -2}, {2, -2}};
    s_shiftIdx = (s_shiftIdx + 1) % 9;
    for (auto &pg : s_pages) { lv_obj_set_pos(pg.canvas, offs[s_shiftIdx][0], offs[s_shiftIdx][1]); pg.snapOk = false; }
  }

  time_t t = time(nullptr);
  int sec = t % 60;
  if (now - s_lastUpdate >= 500 || sec != s_lastSecond) {
    s_lastUpdate = now;
    s_lastSecond = sec;
    updatePage(s_cur);
  }
  mediaSync();
  snapMaintain(now);
}
