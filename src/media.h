#pragma once
// Pictures, GIFs and video loops from the microSD card.
//
// Files live in /media on the card as ".dda" (Desk Display Animation): the web page converts any
// image / GIF / video in the browser into a series of JPEG frames sized for the display, so the
// device only has to decode JPEG. Layout:
//   "DDA1" | u16 width | u16 height | u16 frameCount | u16 flags (bit0 = loop) |
//   frameCount x { u32 offset, u32 size, u16 delayMs, u16 reserved } | JPEG data...
// (little endian; offsets are from the start of the file)
#include <Arduino.h>
#include <ArduinoJson.h>
#include <lvgl.h>

void mediaInit();                     // mount the card (safe to call again after inserting one)
bool mediaMounted();
void mediaStatus(JsonObject out);     // card + file list for the web page
bool mediaDelete(const String &name);
bool mediaFrameJpeg(const String &name, int frame, std::vector<uint8_t> &out);   // for thumbnails
String mediaUploadPath(const String &name);   // sanitised /media/<name>.dda, "" if bad

// Upload (called from the web server task, in order)
bool mediaUploadBegin(const String &name, String &err);
bool mediaUploadWrite(const uint8_t *data, size_t len, String &err);
bool mediaUploadEnd(bool ok, String &err);

// Playback: one media widget at a time (the one on the visible page).
void mediaPlay(lv_obj_t *img, const String &file, int boxW, int boxH);   // no-op if already playing it
void mediaStop();
lv_obj_t *mediaTarget();
String mediaError();                  // why nothing is showing ("" = fine)
void mediaTick();                     // main loop: swaps in decoded frames
void mediaPause(bool paused);         // e.g. while the screen is off
