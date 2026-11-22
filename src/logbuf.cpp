#include "logbuf.h"
#include <stdarg.h>
#include <time.h>

static const size_t LOG_CAP = 12 * 1024;
static char *s_buf = nullptr;
static size_t s_head = 0;     // next write position
static bool s_wrapped = false;
static SemaphoreHandle_t s_mtx = nullptr;

void logInit() {
  s_buf = (char *)ps_malloc(LOG_CAP);
  if (!s_buf) s_buf = (char *)malloc(LOG_CAP);
  s_mtx = xSemaphoreCreateMutex();
}

static void append(const char *s, size_t n) {
  for (size_t i = 0; i < n; i++) {
    s_buf[s_head++] = s[i];
    if (s_head >= LOG_CAP) { s_head = 0; s_wrapped = true; }
  }
}

void logf(const char *fmt, ...) {
  char line[256];
  int off = 0;
  time_t now = time(nullptr);
  if (now > 1700000000) {
    struct tm t; localtime_r(&now, &t);
    off = strftime(line, sizeof(line), "%H:%M:%S ", &t);
  } else {
    off = snprintf(line, sizeof(line), "+%lus ", (unsigned long)(millis() / 1000));
  }
  va_list ap; va_start(ap, fmt);
  int n = vsnprintf(line + off, sizeof(line) - off - 2, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  size_t len = strnlen(line, sizeof(line) - 2);
  line[len++] = '\n'; line[len] = 0;
  Serial.print(line);
  if (!s_buf) return;
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  append(line, len);
  xSemaphoreGive(s_mtx);
}

String logDump() {
  String out;
  if (!s_buf) return out;
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  if (s_wrapped) {
    out.reserve(LOG_CAP);
    out.concat(s_buf + s_head, LOG_CAP - s_head);
    out.concat(s_buf, s_head);
    int nl = out.indexOf('\n');          // drop the partial first line
    if (nl >= 0) out.remove(0, nl + 1);
  } else {
    out.concat(s_buf, s_head);
  }
  xSemaphoreGive(s_mtx);
  return out;
}
