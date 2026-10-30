// GIF widget: one decoder + one RGB565 canvas per GIF on screen (at most 2 at a time; a third that does not fit is left out).
#include "gifs.h"
#include <AnimatedGIF.h>
#include <Arduino.h>
#include <stdio.h>
#include <esp_heap_caps.h>

struct Gif {
  lv_obj_t *cv;
  uint16_t *buf;
  AnimatedGIF *g;
  int cw, ch;
  uint32_t next_ms;
  bool used;
  bool closed; // the file was released (it is about to be replaced or deleted): the canvas keeps its last frame
  uint8_t slot;
};
static Gif slots[2];

static FILE *g_open = NULL; // the file our open callback handed out and nobody closed yet
static void *gOpen(const char *fn, int32_t *sz) {
  FILE *f = fopen(fn, "rb");
  if (!f) return NULL;
  g_open = f;
  fseek(f, 0, SEEK_END);
  *sz = (int32_t)ftell(f);
  fseek(f, 0, SEEK_SET);
  return f;
}
static void gClose(void *h) {
  fclose((FILE *)h);
  if ((FILE *)h == g_open) g_open = NULL;
}
// AnimatedGIF::open() leaves the file open when the file is not a GIF it can read: close it ourselves, or every refused upload leaks a handle
static void close_failed_open(AnimatedGIF *g) {
  if (g_open) g->close();
}
static int32_t gRead(GIFFILE *pf, uint8_t *b, int32_t n) {
  int32_t r = (int32_t)fread(b, 1, (size_t)n, (FILE *)pf->fHandle);
  pf->iPos += r;
  return r;
}
static int32_t gSeek(GIFFILE *pf, int32_t pos) {
  fseek((FILE *)pf->fHandle, pos, SEEK_SET);
  pf->iPos = pos;
  return pos;
}
// one decoded line into the canvas; transparent pixels keep what the previous frame left there
static void gDraw(GIFDRAW *d) {
  Gif *G = (Gif *)d->pUser;
  if (!G) return;
  int y = d->iY + d->y;
  if (y < 0 || y >= G->ch) return;
  int w = d->iWidth;
  if (d->iX + w > G->cw) w = G->cw - d->iX;
  if (w <= 0) return;
  uint16_t *dst = G->buf + y * G->cw + d->iX;
  const uint8_t *s = d->pPixels;
  const uint16_t *pal = d->pPalette;
  if (d->ucHasTransparency) {
    for (int i = 0; i < w; i++)
      if (s[i] != d->ucTransparent) dst[i] = pal[s[i]];
  } else {
    for (int i = 0; i < w; i++) dst[i] = pal[s[i]];
  }
}
static void gDrawNone(GIFDRAW *) {}

static void gif_deleted(lv_event_t *e) {
  Gif *G = (Gif *)lv_event_get_user_data(e);
  if (!G->used) return;
  G->used = false;
  if (G->g) {
    if (!G->closed) G->g->close();
    delete G->g;
  }
  free(G->buf);
  G->g = NULL;
  G->buf = NULL;
  G->cv = NULL;
}

lv_obj_t *gif_create(lv_obj_t *parent, int x, int y, int w, int h, uint8_t slot) {
  Gif *G = NULL;
  for (auto &s : slots)
    if (!s.used) { G = &s; break; }
  if (!G) return NULL;
  char path[32];
  snprintf(path, sizeof path, "/littlefs/gif/%u.gif", (unsigned)slot);
  AnimatedGIF *g = new (std::nothrow) AnimatedGIF;
  if (!g) { log_w("gif: no memory for the decoder"); return NULL; }
  g->begin(GIF_PALETTE_RGB565_BE); // LVGL here keeps RGB565 byte-swapped (LV_COLOR_16_SWAP)
  if (!g->open(path, gOpen, gClose, gRead, gSeek, gDraw)) { close_failed_open(g); delete g; return NULL; }
  int cw = g->getCanvasWidth(), ch = g->getCanvasHeight();
  if (cw < 1 || ch < 1 || cw > 128 || ch > 128) { g->close(); delete g; return NULL; }
  uint16_t *buf = (uint16_t *)heap_caps_calloc((size_t)cw * ch, 2, MALLOC_CAP_8BIT);
  if (!buf) { log_w("gif: no memory for the %dx%d canvas", cw, ch); g->close(); delete g; return NULL; }
  G->used = true;
  G->closed = false;
  G->slot = slot;
  G->g = g;
  G->buf = buf;
  G->cw = cw;
  G->ch = ch;
  G->next_ms = 0;
  G->cv = lv_canvas_create(parent);
  lv_canvas_set_buffer(G->cv, buf, cw, ch, LV_IMG_CF_TRUE_COLOR);
  lv_obj_set_pos(G->cv, x + (w - cw) / 2, y + (h - ch) / 2); // natural size, centered in the widget box
  lv_obj_clear_flag(G->cv, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(G->cv, gif_deleted, LV_EVENT_DELETE, G);
  log_i("gif slot %u: %dx%d, heap free %u", (unsigned)slot, cw, ch, (unsigned)ESP.getFreeHeap());
  return G->cv;
}

void gifs_release(uint8_t slot) {
  for (auto &G : slots)
    if (G.used && !G.closed && G.slot == slot) {
      G.g->close(); // an open file must not be replaced or deleted
      delete G.g; // and give the decoder's memory back: the board's own decode test of the new file needs a contiguous block of it
      G.g = NULL;
      G.closed = true;
    }
}

void gifs_tick() {
  uint32_t now = millis();
  for (auto &G : slots) {
    if (!G.used || G.closed || (int32_t)(now - G.next_ms) < 0) continue;
    int delay = 0;
    int r = G.g->playFrame(false, &delay, &G);
    lv_obj_invalidate(G.cv);
    if (r <= 0) { // last frame shown (0) or a decode error (< 0): start over, from a clean canvas
      G.g->reset();
      memset(G.buf, 0, (size_t)G.cw * G.ch * 2);
    }
    if (delay < 20) delay = 20; // cap at 50 frames a second; most GIFs ask 50-100 ms
    G.next_ms = now + (uint32_t)delay;
  }
}

bool gif_decode_test(uint8_t slot) {
  char path[32];
  snprintf(path, sizeof path, "/littlefs/gif/%u.gif", (unsigned)slot);
  AnimatedGIF *g = new (std::nothrow) AnimatedGIF;
  if (!g) return false;
  g->begin(GIF_PALETTE_RGB565_BE);
  bool ok = false;
  if (g->open(path, gOpen, gClose, gRead, gSeek, gDrawNone)) {
    // decode every frame to the end: a file cut off inside a frame, or with garbage in it, fails somewhere along the way
    int delay = 0, r = 1, frames = 0;
    while (r > 0 && frames < 300) {
      r = g->playFrame(false, &delay, NULL);
      frames++;
    }
    ok = r == 0 && frames >= 1 && g->getLastError() == GIF_SUCCESS;
    g->close();
  } else {
    close_failed_open(g);
  }
  delete g;
  log_i("gif check slot %u: %s", (unsigned)slot, ok ? "decodes" : "CANNOT decode");
  return ok;
}
