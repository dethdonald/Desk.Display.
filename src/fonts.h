#pragma once
#include <lvgl.h>

// Generated with lv_font_conv (Montserrat Medium + a few Font Awesome symbols; weather glyphs
// from Erik Flowers' Weather Icons, SIL OFL 1.1). 96/128 px fonts only contain digits and ": . % - + ° A P M F C".
LV_FONT_DECLARE(font_mont_12) LV_FONT_DECLARE(font_mont_14) LV_FONT_DECLARE(font_mont_16)
LV_FONT_DECLARE(font_mont_18) LV_FONT_DECLARE(font_mont_20) LV_FONT_DECLARE(font_mont_22)
LV_FONT_DECLARE(font_mont_24) LV_FONT_DECLARE(font_mont_28) LV_FONT_DECLARE(font_mont_32)
LV_FONT_DECLARE(font_mont_36) LV_FONT_DECLARE(font_mont_40) LV_FONT_DECLARE(font_mont_48)
LV_FONT_DECLARE(font_mont_64) LV_FONT_DECLARE(font_mont_96) LV_FONT_DECLARE(font_mont_128)
LV_FONT_DECLARE(font_wx_32) LV_FONT_DECLARE(font_wx_64) LV_FONT_DECLARE(font_wx_128)

static const int FONT_SIZES[] = {12, 14, 16, 18, 20, 22, 24, 28, 32, 36, 40, 48, 64, 96, 128};

inline const lv_font_t *fontFor(int px) {
  static const lv_font_t *fonts[] = {&font_mont_12, &font_mont_14, &font_mont_16, &font_mont_18, &font_mont_20,
                                     &font_mont_22, &font_mont_24, &font_mont_28, &font_mont_32, &font_mont_36,
                                     &font_mont_40, &font_mont_48, &font_mont_64, &font_mont_96, &font_mont_128};
  int best = 0;
  for (int i = 0; i < (int)(sizeof(FONT_SIZES) / sizeof(int)); i++)
    if (abs(FONT_SIZES[i] - px) < abs(FONT_SIZES[best] - px)) best = i;
  return fonts[best];
}

inline const lv_font_t *wxFontFor(int px) {
  if (px >= 96) return &font_wx_128;
  if (px >= 48) return &font_wx_64;
  return &font_wx_32;
}

// Symbol glyphs available in every text font (use {sym:name} in templates)
struct SymDef { const char *name; const char *utf8; };
static const SymDef SYMBOLS[] = {
  {"wifi", "\xEF\x87\xAB"}, {"bat_full", "\xEF\x89\x80"}, {"bat_3", "\xEF\x89\x81"}, {"bat_half", "\xEF\x89\x82"},
  {"bat_1", "\xEF\x89\x83"}, {"bat_empty", "\xEF\x89\x84"}, {"charge", "\xEF\x83\xA7"}, {"refresh", "\xEF\x80\xA1"},
  {"bell", "\xEF\x83\xB3"}, {"home", "\xEF\x80\x95"}, {"gear", "\xEF\x80\x93"}, {"power", "\xEF\x80\x91"},
  {"clock", "\xEF\x80\x97"}, {"cloud", "\xEF\x83\x82"}, {"fire", "\xEF\x81\xAD"}, {"warn", "\xEF\x81\xB1"},
};
