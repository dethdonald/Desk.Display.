#pragma once
// LVGL object memory. Plain malloc puts every small LVGL object in the ESP32-S3's ~300 KB internal
// RAM, which Wi-Fi, TLS, task stacks and the web server also need; a layout with many pages could
// run it out (layout saves silently failing, occasional crashes). Prefer the 8 MB PSRAM instead.
#include <stddef.h>
#include <esp_heap_caps.h>
static inline void *lv_ps_malloc(size_t s) {
  void *p = heap_caps_malloc(s, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : heap_caps_malloc(s, MALLOC_CAP_8BIT);
}
static inline void *lv_ps_realloc(void *p, size_t s) {
  void *q = heap_caps_realloc(p, s, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return q ? q : heap_caps_realloc(p, s, MALLOC_CAP_8BIT);
}
static inline void lv_ps_free(void *p) { heap_caps_free(p); }
