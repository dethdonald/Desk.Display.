#pragma once
#include <Arduino.h>

// Small ring-buffer logger: everything goes to USB serial and to a buffer the web UI can read.
void logInit();
void logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
String logDump();

#define LOGI(...) logf(__VA_ARGS__)
