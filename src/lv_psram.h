#pragma once
// LVGL memory policy.
// Internal RAM is fast but small (~300 KB, shared with Wi-Fi, the web server and task stacks);
// PSRAM is 8 MB but several times slower. LVGL's objects and its short-lived drawing buffers are
// touched on every frame, so they go to internal RAM while plenty is left; once internal RAM gets
// low (or for big blocks like page snapshots) they go to PSRAM, so a large layout can never
// starve the rest of the system.
#include <stddef.h>
#include <string.h>
#include <esp_heap_caps.h>
#define LV_PS_SMALL_MAX   (16 * 1024)    // larger blocks always go to PSRAM
// Internal RAM to leave for everything else. Starts high so nothing is taken from Wi-Fi / tasks
// that are still starting up; main.cpp lowers it once the system is running.
#ifdef __cplusplus
extern "C" {
#endif
extern volatile size_t g_lvInternalReserve;
#ifdef __cplusplus
}
#endif
#define LV_PS_RESERVE g_lvInternalReserve
static inline void *lv_ps_malloc(size_t s) {
  if (s <= LV_PS_SMALL_MAX && heap_caps_get_free_size(MALLOC_CAP_INTERNAL) > LV_PS_RESERVE + s) {
    void *p = heap_caps_malloc(s, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (p) return p;
  }
  void *p = heap_caps_malloc(s, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : heap_caps_malloc(s, MALLOC_CAP_8BIT);
}
static inline void *lv_ps_realloc(void *p, size_t s) {
  if (!p) return lv_ps_malloc(s);
  if (!s) { heap_caps_free(p); return NULL; }
  size_t old = heap_caps_get_allocated_size(p);
  void *q = lv_ps_malloc(s);                           // re-apply the policy (may move to faster RAM)
  if (!q) return NULL;
  memcpy(q, p, old < s ? old : s);
  heap_caps_free(p);
  return q;
}
static inline void lv_ps_free(void *p) { heap_caps_free(p); }
