#include "media.h"
#include "logbuf.h"
#include <FS.h>
#include <SD_MMC.h>
#include <SD.h>
#include <SPI.h>
#include <JPEGDEC.h>
#include <esp_heap_caps.h>
#include <vector>

// TF slot on the ESP32-S3-Touch-AMOLED-2.16: CMD=GPIO1, CLK=GPIO2, D0=GPIO3, CS/D3=GPIO41.
static const int SD_CMD_PIN = 1, SD_CLK_PIN = 2, SD_D0_PIN = 3, SD_CS_PIN = 41;
static const char *MEDIA_DIR = "/media";

static fs::FS *s_fs = nullptr;
static String s_cardType;
static uint64_t s_cardSize = 0;
static SemaphoreHandle_t s_mtx = nullptr;               // guards the request + file access
static SPIClass *s_sdSpi = nullptr;

struct Lk { Lk() { xSemaphoreTake(s_mtx, portMAX_DELAY); } ~Lk() { xSemaphoreGive(s_mtx); } };

void mediaInit() {
  if (!s_mtx) s_mtx = xSemaphoreCreateMutex();
  if (s_fs) return;
  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);                         // D3 high = SD mode (also deselects SPI)
  SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN);
  bool ok = SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_HIGHSPEED, 4);
  if (!ok) ok = SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT, 4);
  if (ok && SD_MMC.cardType() != CARD_NONE) {
    s_fs = &SD_MMC;
    s_cardSize = SD_MMC.cardSize();
    s_cardType = "SD (1-bit)";
  } else {
    SD_MMC.end();
    if (!s_sdSpi) s_sdSpi = new SPIClass(HSPI);
    s_sdSpi->begin(SD_CLK_PIN, SD_D0_PIN, SD_CMD_PIN, SD_CS_PIN);
    if (SD.begin(SD_CS_PIN, *s_sdSpi, 20000000, "/sd", 4) && SD.cardType() != CARD_NONE) {
      s_fs = &SD;
      s_cardSize = SD.cardSize();
      s_cardType = "SD (SPI)";
    } else {
      SD.end();
    }
  }
  if (s_fs) {
    if (!s_fs->exists(MEDIA_DIR)) s_fs->mkdir(MEDIA_DIR);
    LOGI("media: %s card, %u MB", s_cardType.c_str(), (unsigned)(s_cardSize >> 20));
  } else {
    LOGI("media: no microSD card");
  }
}

bool mediaMounted() { return s_fs != nullptr; }

static uint64_t usedBytes() {
  if (s_fs == &SD_MMC) return SD_MMC.usedBytes();
  if (s_fs == &SD) return SD.usedBytes();
  return 0;
}
static uint64_t totalBytes() {
  if (s_fs == &SD_MMC) return SD_MMC.totalBytes();
  if (s_fs == &SD) return SD.totalBytes();
  return 0;
}

String mediaUploadPath(const String &name) {
  String n = name;
  int slash = n.lastIndexOf('/'); if (slash >= 0) n = n.substring(slash + 1);
  if (n.endsWith(".dda")) n = n.substring(0, n.length() - 4);
  String out;
  for (char c : n) if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == ' ' || c == '.') out += c;
  out.trim();
  while (out.startsWith(".")) out.remove(0, 1);
  if (out.isEmpty() || out.length() > 60) return "";
  return String(MEDIA_DIR) + "/" + out + ".dda";
}

// ---------------------------------------------------------------- file format
struct DdaHeader { uint16_t w = 0, h = 0, n = 0, flags = 0; };
struct FrameIdx { uint32_t off, size; uint16_t delay, pad; };

static bool readHeader(fs::File &f, DdaHeader &h, std::vector<FrameIdx> *idx) {
  uint8_t b[12];
  if (f.read(b, 12) != 12 || memcmp(b, "DDA1", 4) != 0) return false;
  h.w = b[4] | b[5] << 8; h.h = b[6] | b[7] << 8; h.n = b[8] | b[9] << 8; h.flags = b[10] | b[11] << 8;
  if (!h.w || !h.h || h.w > 480 || h.h > 480 || !h.n) return false;
  if (idx) {
    idx->resize(h.n);
    if (f.read((uint8_t *)idx->data(), h.n * sizeof(FrameIdx)) != h.n * sizeof(FrameIdx)) return false;
  }
  return true;
}

void mediaStatus(JsonObject out) {
  JsonObject sd = out["sd"].to<JsonObject>();
  sd["mounted"] = s_fs != nullptr;
  JsonArray files = out["files"].to<JsonArray>();
  if (!s_fs) return;
  Lk l;
  sd["type"] = s_cardType;
  sd["total"] = totalBytes();
  sd["used"] = usedBytes();
  fs::File dir = s_fs->open(MEDIA_DIR);
  if (!dir) return;
  for (fs::File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String nm = f.name();
    if (f.isDirectory() || !nm.endsWith(".dda")) { f.close(); continue; }
    JsonObject o = files.add<JsonObject>();
    o["name"] = nm.substring(0, nm.length() - 4);
    o["size"] = f.size();
    DdaHeader h; std::vector<FrameIdx> idx;
    if (readHeader(f, h, &idx)) {
      o["w"] = h.w; o["h"] = h.h; o["frames"] = h.n;
      uint32_t total = 0; for (auto &e : idx) total += e.delay;
      o["seconds"] = total / 1000.0;
    } else o["bad"] = true;
    f.close();
  }
}

bool mediaFrameJpeg(const String &name, int frame, std::vector<uint8_t> &out) {
  String path = mediaUploadPath(name);
  if (!s_fs || path.isEmpty()) return false;
  Lk l;
  fs::File f = s_fs->open(path, "r");
  if (!f) return false;
  DdaHeader h; std::vector<FrameIdx> idx;
  bool ok = readHeader(f, h, &idx) && frame >= 0 && frame < h.n && idx[frame].size < 512 * 1024;
  if (ok) {
    out.resize(idx[frame].size);
    ok = f.seek(idx[frame].off) && f.read(out.data(), out.size()) == out.size();
  }
  f.close();
  return ok;
}

// ---------------------------------------------------------------- playback
static TaskHandle_t s_task = nullptr;
// request (main thread -> task), guarded by s_mtx
static String s_reqFile;
static volatile uint32_t s_reqGen = 0;
static volatile bool s_paused = false;
static volatile bool s_closeReq = false, s_fileOpen = false;
// target widget (main thread only)
static lv_obj_t *s_img = nullptr;
static String s_imgFile;
static volatile uint32_t s_imgGen = 0;
// frame hand-over: the task fills buf[front ^ 1], main swaps when ready
static uint16_t *s_buf[2] = {nullptr, nullptr};
static uint16_t s_bw = 0, s_bh = 0;
static volatile int s_front = 0;
static volatile bool s_backReady = false;
static volatile uint16_t s_backDelay = 100;
static volatile uint32_t s_backGen = 0;
static volatile bool s_shownOnce = false;
static String s_err;
static lv_img_dsc_t s_dsc;
static uint32_t s_nextAt = 0;
static uint16_t s_curDelay = 100;
static JPEGDEC *s_jpeg = nullptr;

static void freeBuffers() {
  for (auto &b : s_buf) { if (b) heap_caps_free(b); b = nullptr; }
  s_bw = s_bh = 0;
}

static int jpegNoDraw(JPEGDRAW *) { return 1; }     // output goes straight into the framebuffer

static void task(void *) {
  fs::File f;
  DdaHeader hdr;
  std::vector<FrameIdx> idx;
  uint32_t loadedGen = UINT32_MAX;
  int frame = 0;
  uint8_t *jbuf = nullptr; size_t jcap = 0;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(30));
    uint32_t gen = s_reqGen;
    if (s_closeReq) {                                   // upload/delete wants the file closed
      if (f) f.close();
      s_fileOpen = false;
      loadedGen = UINT32_MAX;
      while (s_closeReq) vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    if (gen != loadedGen) {
      String file;
      { Lk l; file = s_reqFile; if (f) f.close(); s_fileOpen = false; }
      loadedGen = gen;
      frame = 0;
      idx.clear();
      if (file.isEmpty()) { freeBuffers(); s_backReady = false; continue; }
      String err;
      if (!s_fs) err = "Insert a microSD card";
      else {
        String path = mediaUploadPath(file);
        Lk l;
        f = path.length() ? s_fs->open(path, "r") : fs::File();
        if (!f) err = "Not found on the card: " + file;
        else if (!readHeader(f, hdr, &idx)) { f.close(); err = "Not a Desk Display media file"; }
        else s_fileOpen = true;
      }
      if (err.isEmpty() && (hdr.w != s_bw || hdr.h != s_bh || !s_buf[0])) {
        freeBuffers();
        size_t bytes = (size_t)hdr.w * (hdr.h + 16) * 2;   // +16 rows: the decoder writes whole 16-px blocks
        s_buf[0] = (uint16_t *)heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM);
        s_buf[1] = (uint16_t *)heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM);
        if (!s_buf[0] || !s_buf[1]) { freeBuffers(); err = "Not enough memory"; }
        else { s_bw = hdr.w; s_bh = hdr.h; }
      }
      if (err.length()) { Lk l; s_err = err; if (f) f.close(); s_fileOpen = false; continue; }
      { Lk l; s_err = ""; }
      s_backReady = false;
    }
    if (!s_fileOpen || s_paused || s_backReady || idx.empty()) continue;
    if (s_shownOnce && hdr.n == 1) continue;             // still image: nothing more to do
    const FrameIdx &fr = idx[frame];
    if (fr.size > jcap) {
      heap_caps_free(jbuf);
      jcap = fr.size + 16 * 1024;
      jbuf = (uint8_t *)heap_caps_malloc(jcap, MALLOC_CAP_SPIRAM);
      if (!jbuf) { jcap = 0; Lk l; s_err = "Not enough memory"; continue; }
    }
    bool ok;
    { Lk l; ok = f.seek(fr.off) && f.read(jbuf, fr.size) == fr.size; }
    if (!ok) { Lk l; s_err = "Card read error"; vTaskDelay(pdMS_TO_TICKS(500)); continue; }
    if (!s_jpeg) {
      void *mem = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) > 200 * 1024 ? heap_caps_malloc(sizeof(JPEGDEC), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) : nullptr;
      if (!mem) mem = heap_caps_malloc(sizeof(JPEGDEC), MALLOC_CAP_SPIRAM);
      s_jpeg = new (mem) JPEGDEC();
    }
    uint16_t *back = s_buf[s_front ^ 1];
    if (s_jpeg->openRAM(jbuf, fr.size, jpegNoDraw)) {
      s_jpeg->setPixelType(RGB565_LITTLE_ENDIAN);
      s_jpeg->setFramebuffer(back);
      ok = s_jpeg->decode(0, 0, 0) == 1;
      s_jpeg->close();
    } else ok = false;
    if (gen != s_reqGen) continue;                       // changed while decoding: drop it
    if (ok) {
      s_backDelay = max<uint16_t>(fr.delay, 20);
      s_backGen = gen;
      s_backReady = true;
    }
    frame = (frame + 1) % hdr.n;
  }
}

static void ensureTask() {
  if (s_task) return;
  if (!s_mtx) s_mtx = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(task, "media", 8192, nullptr, 1, &s_task, tskNO_AFFINITY);
}

void mediaPlay(lv_obj_t *img, const String &file, int, int) {
  if (img == s_img && file == s_imgFile && s_img) return;
  ensureTask();
  if (s_img && lv_obj_is_valid(s_img)) lv_img_set_src(s_img, nullptr);
  s_img = img; s_imgFile = file;
  s_shownOnce = false;
  s_backReady = false;
  { Lk l; s_reqFile = file; s_err = ""; s_reqGen = s_reqGen + 1; }
  s_imgGen = s_reqGen;
  s_nextAt = 0;
  xTaskNotifyGive(s_task);
}

void mediaStop() {
  if (!s_img && s_imgFile.isEmpty()) return;
  if (s_img && lv_obj_is_valid(s_img)) lv_img_set_src(s_img, nullptr);
  s_img = nullptr; s_imgFile = "";
  if (s_task) { { Lk l; s_reqFile = ""; s_reqGen = s_reqGen + 1; } xTaskNotifyGive(s_task); }
}

lv_obj_t *mediaTarget() { return s_img; }
String mediaError() { if (!s_mtx) return ""; Lk l; return s_err; }
void mediaPause(bool p) { s_paused = p; if (!p && s_task) xTaskNotifyGive(s_task); }

void mediaTick() {
  if (!s_img) return;
  if (!lv_obj_is_valid(s_img)) { s_img = nullptr; mediaStop(); return; }
  if (!s_backReady || s_backGen != s_imgGen) return;
  uint32_t now = millis();
  if (s_nextAt && (int32_t)(now - s_nextAt) < 0) return;
  s_front ^= 1;
  memset(&s_dsc, 0, sizeof s_dsc);
  s_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  s_dsc.header.w = s_bw; s_dsc.header.h = s_bh;
  s_dsc.data_size = (uint32_t)s_bw * s_bh * 2;
  s_dsc.data = (const uint8_t *)s_buf[s_front];
  lv_img_set_src(s_img, &s_dsc);
  lv_obj_invalidate(s_img);
  s_curDelay = s_backDelay;
  s_nextAt = (s_nextAt && now - s_nextAt < 200 ? s_nextAt : now) + s_curDelay;   // keep a steady pace
  s_shownOnce = true;
  s_backReady = false;
  xTaskNotifyGive(s_task);
}

// ---------------------------------------------------------------- upload / delete
static fs::File s_up;
static String s_upTarget;

static void closeForWrite(const String &path) {
  // If the player has this file open, have it let go first (FAT can't replace an open file).
  if (!s_task || !s_fileOpen) return;
  String cur; { Lk l; cur = mediaUploadPath(s_reqFile); }
  if (cur != path) return;
  s_closeReq = true;
  xTaskNotifyGive(s_task);
  for (int i = 0; i < 100 && s_fileOpen; i++) vTaskDelay(pdMS_TO_TICKS(10));
}
static void reopenAfterWrite() {
  if (!s_closeReq) return;
  s_closeReq = false;
  { Lk l; s_reqGen = s_reqGen + 1; }
  // the main thread's target generation must follow, or new frames would be ignored
  s_imgGen = s_reqGen;
  xTaskNotifyGive(s_task);
}

bool mediaUploadBegin(const String &name, String &err) {
  if (!s_fs) { err = "no microSD card"; return false; }
  s_upTarget = mediaUploadPath(name);
  if (s_upTarget.isEmpty()) { err = "bad file name"; return false; }
  Lk l;
  if (!s_fs->exists(MEDIA_DIR)) s_fs->mkdir(MEDIA_DIR);
  s_fs->remove(String(MEDIA_DIR) + "/upload.tmp");
  s_up = s_fs->open(String(MEDIA_DIR) + "/upload.tmp", "w");
  if (!s_up) { err = "can't write to the card"; return false; }
  return true;
}

bool mediaUploadWrite(const uint8_t *data, size_t len, String &err) {
  if (!s_up) { err = "upload not started"; return false; }
  Lk l;
  if (s_up.write(data, len) != len) { err = "card full or write error"; s_up.close(); return false; }
  return true;
}

bool mediaUploadEnd(bool ok, String &err) {
  String tmp = String(MEDIA_DIR) + "/upload.tmp";
  { Lk l; if (s_up) s_up.close(); }
  if (!ok) { Lk l; s_fs->remove(tmp); return false; }
  {
    Lk l;
    fs::File f = s_fs->open(tmp, "r");
    DdaHeader h;
    bool good = f && readHeader(f, h, nullptr);
    if (f) f.close();
    if (!good) { s_fs->remove(tmp); err = "not a Desk Display media file"; return false; }
  }
  closeForWrite(s_upTarget);
  bool renamed;
  { Lk l; s_fs->remove(s_upTarget); renamed = s_fs->rename(tmp, s_upTarget); }
  reopenAfterWrite();
  if (!renamed) { err = "rename failed"; return false; }
  LOGI("media: saved %s", s_upTarget.c_str());
  return true;
}

bool mediaDelete(const String &name) {
  String path = mediaUploadPath(name);
  if (!s_fs || path.isEmpty()) return false;
  closeForWrite(path);
  bool ok; { Lk l; ok = s_fs->remove(path); }
  reopenAfterWrite();
  return ok;
}
