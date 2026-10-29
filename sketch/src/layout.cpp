// Widget layout interpreter. Builds LVGL objects from a layout (layout_core.h) and handles pages, swipes, safe mode and uploads.
#include "layout.h"
#include "accessory.h"
#include "animation.h"
#include "keymap.h"
#include "layout_core.h"
#include "ui/ui.h"
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_system.h>
#include <sys/stat.h>
#include <lvgl.h>

using lc::Widget;

extern const lv_img_dsc_t *wallpaper_img[6]; // main.cpp
extern bool current_sleep_state; // animation.cpp: the cat fell asleep (60 s without activity)

static const char *FINAL_PATH = "/layout.json";
static const char *TMP_PATH = "/layout.tmp";
static const uint32_t STABLE_MS = 10000; // a boot that runs this long without crashing counts as good
static const uint32_t ACCENT = 0xFBC979;

// ------------------------------------------------------------------------------------------------
// State
// ------------------------------------------------------------------------------------------------
static lc::Layout L; // the active layout (about 1.7 KB)
static bool active = false;
static bool handlers_on = false;
static uint8_t safe_reason = 0; // 0 none, 1 BOOT held, 2 crashed twice, 3 invalid file, 4 flagged bad earlier
static uint8_t cur = 0;
static uint32_t last_nav_ms = 0;
static lv_obj_t *scr_c[2] = {NULL, NULL}; // custom page screens, used alternately so a slide animation always has two
static uint8_t scr_next = 0;
static lv_obj_t *dots_main = NULL; // page dots on the original screen
static char status_buf[48] = "Default layout";
static uint32_t stored_crc_cache = 0;

static struct {
  uint32_t ver = 1; // bumped by every feed; widgets redraw when it changed
  bool have_time = false;
  uint8_t h = 0, m = 0, s = 0;
  bool have_w = false;
  uint8_t wgroup = 0;
  int8_t temp = 0;
  uint8_t hum = 0, wh = 0, wm = 0;
  bool have_music = false;
  char title[48] = "", artist[48] = "";
  uint8_t media = 3;
  uint8_t cpu = 0, mem = 0, batt = 0;
  bool chg = false;
  uint32_t taps = 0;
  uint8_t mood = 0;
  uint8_t wall = 0;
  bool have_date = false;
  uint16_t year = 0;
  uint8_t mon = 0, day = 0, wday = 0;
} D;

// ---- feature level 2 state (kept across page changes; RAM only) ----
static struct { bool running = false, on_break = false; uint32_t end_ms = 0, left_ms = 0, total_ms = 0; } POMO;
static struct { bool running = false; uint32_t start_ms = 0, acc_ms = 0; } SW;
static bool focus_busy = false;
static uint32_t remind_next[lc::MAX_WIDGETS_TOTAL]; // millis() when each reminder widget fires next
static uint32_t remind_shown[lc::MAX_WIDGETS_TOTAL]; // millis() it fired, shown as "now" for a minute
static struct { uint8_t e; uint16_t g; } pend[6]; // button actions, run from the main loop (layout_poll), not inside LVGL
static uint8_t pend_n = 0;

// main.cpp
void ui_toast(const char *text, uint32_t ms);
void board_key_action(uint8_t key_action);
void host_send_event(uint8_t action, uint16_t arg);
void status_led_update();
bool ble_keys_send(uint8_t mods, uint8_t key); // false = not in this build / not paired
bool ble_media_send(uint16_t id);
bool ble_keys_available();
bool ble_keys_connected();
static void goto_page(uint8_t idx, lv_scr_load_anim_t anim);

static const char *const REMIND_TEXT[3] = {"Drink some water", "Stand up and stretch", "Breathe: 4 in, 4 out"};
static const char *const REMIND_SHORT[3] = {"Water", "Stretch", "Breathe"};
static const char *const WDAY[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *const MON3[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static const char *const MONTH[12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

struct RT { // per widget runtime objects of the page on screen
  lv_obj_t *a, *b, *c;
  uint32_t seen;
};
static RT rt[lc::MAX_WIDGETS_PAGE];

static const lv_img_dsc_t *WICON[6] = {&ui_img_sun_png, &ui_img_cloud_png, &ui_img_rain_png, &ui_img_storm_png, &ui_img_snow_png, &ui_img_moon_png};

// ------------------------------------------------------------------------------------------------
// Storage (LittleFS, same partition as the sounds) + NVS bookkeeping
// ------------------------------------------------------------------------------------------------
// stat() instead of LittleFS.exists(): exists() logs an error line for every missing file
static bool path_exists(const char *p) {
  char full[40];
  snprintf(full, sizeof full, "/littlefs%s", p);
  struct stat s;
  return stat(full, &s) == 0;
}
static bool file_exists() { return path_exists(FINAL_PATH); }

// ---- pictures (upload kind 1, LittleFS /img/<slot>.sjpg) and album art (kind 2, RAM only) ----
static const char *PIC_TMP = "/img/tmp.part";
static void pic_path(uint8_t slot, char *out, size_t n) { snprintf(out, n, "/img/%u.sjpg", (unsigned)slot); }
static void pic_lv_path(uint8_t slot, char *out, size_t n) { snprintf(out, n, "S:/img/%u.sjpg", (unsigned)slot); } // LVGL drive S = LittleFS
static bool pic_exists(uint8_t slot) {
  char p[24];
  pic_path(slot, p, sizeof p);
  return path_exists(p);
}
static uint8_t *art_buf = NULL, *art_tmp = NULL; // album art JPEG (shown) and the one being received
static size_t art_len = 0, art_tmp_len = 0, art_tmp_cap = 0;
static lv_img_dsc_t art_dsc; // LVGL decodes the JPEG from RAM with its SJPG/JPEG decoder

class FsStorage : public lc::Storage {
  File f;
  uint8_t kind = 0;
  const char *tmp_path() const { return kind == lc::KIND_PICTURE ? PIC_TMP : TMP_PATH; }

 public:
  size_t free_bytes() override { return LittleFS.totalBytes() - LittleFS.usedBytes(); }
  bool tmp_open(uint8_t k, uint8_t) override {
    kind = k;
    if (k == lc::KIND_ART) {
      free(art_tmp);
      art_tmp = (uint8_t *)malloc(lc::MAX_ART_BYTES);
      art_tmp_len = 0;
      art_tmp_cap = art_tmp ? lc::MAX_ART_BYTES : 0;
      return art_tmp != NULL;
    }
    if (k == lc::KIND_PICTURE && !path_exists("/img")) LittleFS.mkdir("/img");
    f = LittleFS.open(tmp_path(), "w");
    return (bool)f;
  }
  bool tmp_write(const uint8_t *d, size_t n) override {
    if (kind == lc::KIND_ART) {
      if (!art_tmp || art_tmp_len + n > art_tmp_cap) return false;
      memcpy(art_tmp + art_tmp_len, d, n);
      art_tmp_len += n;
      return true;
    }
    return f && f.write(d, n) == n;
  }
  bool tmp_close() override {
    if (f) f.close();
    return true;
  }
  bool tmp_read_all(uint8_t *buf, size_t cap, size_t &n) override {
    File r = LittleFS.open(TMP_PATH, "r");
    if (!r) return false;
    size_t sz = r.size();
    if (sz + 1 > cap) {
      r.close();
      return false;
    }
    n = r.read(buf, sz);
    r.close();
    return n == sz;
  }
  bool tmp_scan(uint32_t &crc, size_t &n, uint8_t *head, size_t head_n, uint8_t *tail2) override {
    crc = 0;
    n = 0;
    if (kind == lc::KIND_ART) { // RAM: nothing was written to flash, check the buffer itself
      if (!art_tmp) return false;
      crc = lc::crc32(art_tmp, art_tmp_len);
      n = art_tmp_len;
      memcpy(head, art_tmp, n < head_n ? n : head_n);
      if (n >= 2) memcpy(tail2, art_tmp + n - 2, 2);
      return true;
    }
    File r = LittleFS.open(tmp_path(), "r");
    if (!r) return false;
    uint8_t blk[256];
    for (;;) {
      int got = r.read(blk, sizeof blk);
      if (got <= 0) break;
      if (n < head_n) memcpy(head + n, blk, (size_t)got < head_n - n ? (size_t)got : head_n - n);
      crc = lc::crc32_update(crc, blk, (size_t)got);
      n += (size_t)got;
      if (got >= 2) memcpy(tail2, blk + got - 2, 2);
      else if (got == 1) { tail2[0] = tail2[1]; tail2[1] = blk[0]; }
    }
    r.close();
    return true;
  }
  bool tmp_commit(uint8_t k, uint8_t slot) override {
    if (k == lc::KIND_ART) { // swap in the new art; the old buffer is freed after LVGL forgets it
      uint8_t *old = art_buf;
      art_buf = art_tmp;
      art_len = art_tmp_len;
      art_tmp = NULL;
      art_tmp_len = art_tmp_cap = 0;
      lv_img_cache_invalidate_src(&art_dsc);
      memset(&art_dsc, 0, sizeof art_dsc);
      art_dsc.header.cf = LV_IMG_CF_RAW;
      art_dsc.header.w = 96;
      art_dsc.header.h = 96;
      art_dsc.data_size = art_len;
      art_dsc.data = art_buf;
      free(old);
      return true;
    }
    char fin[24];
    if (k == lc::KIND_PICTURE) pic_path(slot, fin, sizeof fin);
    else strlcpy(fin, FINAL_PATH, sizeof fin);
    if (k == lc::KIND_PICTURE) { // LVGL may hold the old file open for its image cache
      char lvp[24];
      pic_lv_path(slot, lvp, sizeof lvp);
      lv_img_cache_invalidate_src(lvp);
    }
    if (LittleFS.rename(tmp_path(), fin)) return true;
    LittleFS.remove(fin); // some littlefs builds refuse to rename over an existing file
    return LittleFS.rename(tmp_path(), fin);
  }
  void tmp_discard() override {
    if (kind == lc::KIND_ART) {
      free(art_tmp);
      art_tmp = NULL;
      art_tmp_len = art_tmp_cap = 0;
      return;
    }
    if (f) f.close();
    LittleFS.remove(tmp_path());
  }
  bool final_remove() override { return !file_exists() || LittleFS.remove(FINAL_PATH); }
  uint32_t stored_crc() override { return stored_crc_cache; }
  void set_stored_crc(uint32_t c) override {
    stored_crc_cache = c;
    Preferences lp;
    lp.begin("layout", false);
    lp.putUInt("crc", c);
    lp.putUInt("badcrc", 0); // a new (or removed) layout clears any "this one is bad" flag
    lp.end();
  }
  uint8_t state_flags() override {
    return (uint8_t)((active ? lc::FL_LAYOUT_ACTIVE : 0) | (safe_reason ? lc::FL_SAFE_MODE : 0) | (file_exists() ? lc::FL_LAYOUT_STORED : 0) |
                     (ble_keys_available() ? lc::FL_BLE_KEYS : 0) | (ble_keys_connected() ? lc::FL_BLE_PAIRED : 0));
  }
  uint32_t picture_crc(uint8_t slot) override {
    if (!pic_exists(slot)) return 0;
    Preferences lp;
    lp.begin("layout", true);
    char key[8];
    snprintf(key, sizeof key, "pic%u", (unsigned)slot);
    uint32_t c = lp.getUInt(key, 0);
    lp.end();
    return c;
  }
  void set_picture_crc(uint8_t slot, uint32_t c) override {
    Preferences lp;
    lp.begin("layout", false);
    char key[8];
    snprintf(key, sizeof key, "pic%u", (unsigned)slot);
    lp.putUInt(key, c);
    lp.end();
  }
  bool picture_check(uint8_t k, uint8_t slot) override {
    char lvp[24];
    const void *src = &art_dsc;
    if (k == lc::KIND_PICTURE) {
      pic_lv_path(slot, lvp, sizeof lvp);
      src = lvp;
    }
    lv_img_decoder_dsc_t dsc;
    if (lv_img_decoder_open(&dsc, src, lv_color_white(), 0) != LV_RES_OK) return false;
    bool ok = dsc.img_data != NULL; // a JPEG from RAM is decoded whole on open
    if (!ok && dsc.header.w > 0) { // SJPG is decoded a line at a time: decode the first and the last line
      lv_color_t *line = (lv_color_t *)malloc(dsc.header.w * sizeof(lv_color_t));
      if (line) {
        ok = lv_img_decoder_read_line(&dsc, 0, 0, dsc.header.w, (uint8_t *)line) == LV_RES_OK &&
             lv_img_decoder_read_line(&dsc, 0, dsc.header.h - 1, dsc.header.w, (uint8_t *)line) == LV_RES_OK;
        free(line);
      }
    }
    lv_img_decoder_close(&dsc);
    log_i("picture check kind %u slot %u: %s", (unsigned)k, (unsigned)slot, ok ? "decodes" : "CANNOT decode");
    return ok;
  }
  bool picture_remove(uint8_t slot) override {
    char p[24], lvp[24];
    pic_path(slot, p, sizeof p);
    pic_lv_path(slot, lvp, sizeof lvp);
    lv_img_cache_invalidate_src(lvp);
    return !path_exists(p) || LittleFS.remove(p);
  }
};
static FsStorage fs_store;
static lc::Session session(fs_store);

static lc::Err load_file(lc::Layout &out) {
  File f = LittleFS.open(FINAL_PATH, "r");
  if (!f) return lc::E_EMPTY;
  size_t n = f.size();
  if (n == 0 || n > lc::MAX_LAYOUT_BYTES) {
    f.close();
    return n ? lc::E_TOO_BIG : lc::E_EMPTY;
  }
  char *buf = (char *)malloc(n + 1);
  if (!buf) {
    f.close();
    return lc::E_EMPTY;
  }
  size_t got = f.read((uint8_t *)buf, n);
  f.close();
  lc::Err e = (got == n) ? lc::parse(buf, n, out) : lc::E_SYNTAX;
  free(buf);
  return e;
}

// ------------------------------------------------------------------------------------------------
// Building a page
// ------------------------------------------------------------------------------------------------
static void gesture_cb(lv_event_t *e);
static void widget_cb(lv_event_t *ev);

static const lv_font_t *font_for(uint8_t z) { return z ? &lv_font_montserrat_20 : &lv_font_montserrat_14; }
static lv_text_align_t align_for(uint8_t a) { return a == 1 ? LV_TEXT_ALIGN_CENTER : (a == 2 ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT); }
static uint32_t color_of(const Widget &w, uint32_t def) { return w.has_c ? w.c : def; }

static lv_obj_t *mkbox(lv_obj_t *p, const Widget &w) {
  lv_obj_t *o = lv_obj_create(p);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, w.x, w.y);
  lv_obj_set_size(o, w.w, w.h);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

static lv_obj_t *mklabel(lv_obj_t *p, const lv_font_t *f, uint32_t col) {
  lv_obj_t *l = lv_label_create(p);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
  return l;
}

static void panel_style(lv_obj_t *o) {
  lv_obj_set_style_bg_color(o, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(o, 80, 0);
  lv_obj_set_style_radius(o, 8, 0);
}

static void slider_cb(lv_event_t *e) {
  lv_obj_t *s = lv_event_get_target(e);
  uint8_t kind = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_VALUE_CHANGED) {
    int v = lv_slider_get_value(s);
    if (kind == 0) {
      brightness = (uint8_t)v;
      dim = false;
      tft.setBrightness(brightness);
      lv_slider_set_value(ui_setting_Slider_backlight, v, LV_ANIM_OFF);
    } else {
      volume = (uint8_t)v;
      lv_slider_set_value(ui_setting_Slider_volume, v, LV_ANIM_OFF);
    }
  } else if (code == LV_EVENT_RELEASED) { // save once on release, not on every drag step (flash wear)
    pref.begin("config", false);
    if (kind == 0) pref.putUChar("brightness", brightness);
    else pref.putUChar("volume", volume);
    pref.end();
    if (kind == 1) clickSound();
  }
}

static void build_widget(lv_obj_t *p, const Widget &w, RT &r) {
  r.a = r.b = r.c = NULL;
  r.seen = 0xFFFFFFFFu;
  switch (w.type) {
    case lc::W_ANALOG: {
      lv_obj_t *o = lv_obj_create(p);
      lv_obj_remove_style_all(o);
      lv_obj_set_pos(o, w.x, w.y);
      lv_obj_set_size(o, 90, 90);
      lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_style_radius(o, 45, 0);
      lv_obj_set_style_bg_color(o, lv_color_black(), 0);
      lv_obj_set_style_bg_opa(o, 80, 0);
      lv_obj_set_style_bg_img_src(o, &ui_img_a_clock_png, 0);
      lv_obj_set_style_border_color(o, lv_color_hex(0xFFDDAC), 0);
      lv_obj_set_style_border_opa(o, 255, 0);
      lv_obj_set_style_border_width(o, 1, 0);
      struct Hand {
        const lv_img_dsc_t *img;
        int y, px, py;
      };
      static const Hand hands[3] = {{&ui_img_shorthand_png, -12, 6, 29}, {&ui_img_longhand_png, -21, 6, 46}, {&ui_img_secondhand_png, -20, 7, 46}};
      lv_obj_t **slot[3] = {&r.a, &r.b, &r.c};
      for (int i = 0; i < 3; i++) {
        lv_obj_t *im = lv_img_create(o);
        lv_img_set_src(im, hands[i].img);
        lv_obj_align(im, LV_ALIGN_CENTER, 0, hands[i].y);
        lv_img_set_pivot(im, hands[i].px, hands[i].py);
        *slot[i] = im;
      }
      break;
    }
    case lc::W_DIGITAL: {
      lv_obj_t *l = mklabel(p, font_for(w.z), color_of(w, 0xFFFFFF));
      lv_obj_set_pos(l, w.x, w.y);
      lv_obj_set_width(l, w.w);
      lv_obj_set_style_text_align(l, align_for(w.a), 0);
      lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
      lv_label_set_text(l, w.f24 ? "--:--" : "-:-- --");
      r.a = l;
      break;
    }
    case lc::W_WEATHER: {
      lv_obj_t *o = mkbox(p, w);
      panel_style(o);
      r.a = lv_img_create(o);
      lv_img_set_src(r.a, WICON[D.wgroup < 6 ? D.wgroup : 0]);
      lv_obj_align(r.a, LV_ALIGN_TOP_MID, 0, 4);
      lv_obj_set_style_img_recolor(r.a, lv_color_hex(color_of(w, 0xFFFFFF)), 0); // the icons are black: recolored like the original screen does
      lv_obj_set_style_img_recolor_opa(r.a, LV_OPA_COVER, 0);
      r.b = mklabel(o, &lv_font_montserrat_14, color_of(w, 0xFFFFFF));
      lv_obj_set_width(r.b, lv_pct(100));
      lv_obj_set_style_text_align(r.b, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_align(r.b, LV_ALIGN_BOTTOM_MID, 0, -4);
      break;
    }
    case lc::W_NOWPLAY: {
      lv_obj_t *o = mkbox(p, w);
      panel_style(o);
      lv_obj_set_style_pad_all(o, 4, 0);
      lv_obj_set_style_pad_row(o, 0, 0);
      lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
      lv_obj_set_flex_align(o, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      uint32_t col = color_of(w, 0xFFFFFF);
      lv_obj_t **slot[3] = {&r.a, &r.b, &r.c};
      for (int i = 0; i < 3; i++) {
        lv_obj_t *l = mklabel(o, &ui_font_itim16, col);
        lv_obj_set_width(l, lv_pct(100));
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(l, i == 1 ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);
        *slot[i] = l;
      }
      break;
    }
    case lc::W_SYSBARS: {
      lv_obj_t *o = mkbox(p, w);
      panel_style(o);
      static const char *names[3] = {"CPU", "MEM", "BAT"};
      static const uint32_t cols[3] = {0xA5DBAF, 0xFAA0FC, 0xFFDDAC};
      int rowh = w.h / 3;
      int bh = rowh > 12 ? 8 : (rowh > 6 ? rowh - 4 : 2);
      lv_obj_t **slot[3] = {&r.a, &r.b, &r.c};
      for (int i = 0; i < 3; i++) {
        lv_obj_t *l = mklabel(o, &ui_font_DynaPuff10, 0xFFFFFF);
        lv_label_set_text(l, names[i]);
        lv_obj_set_pos(l, 4, i * rowh + (rowh - 10) / 2);
        lv_obj_t *b = lv_bar_create(o);
        lv_bar_set_range(b, 0, 100);
        lv_obj_set_size(b, w.w - 40 > 4 ? w.w - 40 : 4, bh);
        lv_obj_set_pos(b, 34, i * rowh + (rowh - bh) / 2);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x303030), LV_PART_MAIN);
        lv_obj_set_style_bg_color(b, lv_color_hex(cols[i]), LV_PART_INDICATOR);
        lv_obj_set_style_radius(b, 3, LV_PART_MAIN);
        lv_obj_set_style_radius(b, 3, LV_PART_INDICATOR);
        *slot[i] = b;
      }
      break;
    }
    case lc::W_TAPS: {
      lv_obj_t *l = mklabel(p, font_for(w.z), color_of(w, 0xFFFFFF));
      lv_obj_set_pos(l, w.x, w.y);
      lv_obj_set_width(l, w.w);
      lv_obj_set_style_text_align(l, align_for(w.a), 0);
      lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
      r.a = l;
      break;
    }
    case lc::W_LABEL: {
      lv_obj_t *l = mklabel(p, font_for(w.z), color_of(w, 0xFFFFFF));
      lv_obj_set_pos(l, w.x, w.y);
      lv_obj_set_size(l, w.w, w.h);
      lv_obj_set_style_text_align(l, align_for(w.a), 0);
      lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
      lv_label_set_text(l, L.text + w.toff);
      break;
    }
    case lc::W_MOOD: {
      lv_obj_t *o = lv_obj_create(p);
      lv_obj_remove_style_all(o);
      lv_obj_set_pos(o, w.x, w.y);
      lv_obj_set_size(o, 200, 132); // native size of the cat picture
      lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_style_bg_img_src(o, &ui_img_main_png, 0);
      r.a = lv_img_create(o); // eyes shut (happy)
      lv_img_set_src(r.a, &ui_img_closeeye_png);
      lv_obj_align(r.a, LV_ALIGN_CENTER, 11, -11);
      lv_obj_add_flag(r.a, LV_OBJ_FLAG_HIDDEN);
      r.b = lv_img_create(o); // asleep
      lv_img_set_src(r.b, &ui_img_sleepy_png);
      lv_obj_align(r.b, LV_ALIGN_CENTER, 10, -1);
      lv_obj_add_flag(r.b, LV_OBJ_FLAG_HIDDEN);
      break;
    }
    case lc::W_POMO: { // ring + time + phase; tap = start/pause, long press = reset
      lv_obj_t *o = mkbox(p, w);
      int d = w.w < w.h ? w.w : w.h;
      lv_obj_t *arc = lv_arc_create(o);
      lv_obj_set_size(arc, d, d);
      lv_obj_center(arc);
      lv_arc_set_rotation(arc, 270);
      lv_arc_set_bg_angles(arc, 0, 360);
      lv_arc_set_range(arc, 0, 1000);
      lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
      lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
      int aw = d / 12 < 4 ? 4 : d / 12;
      lv_obj_set_style_arc_width(arc, aw, LV_PART_MAIN);
      lv_obj_set_style_arc_width(arc, aw, LV_PART_INDICATOR);
      lv_obj_set_style_arc_color(arc, lv_color_hex(0x303030), LV_PART_MAIN);
      lv_obj_set_style_arc_color(arc, lv_color_hex(color_of(w, ACCENT)), LV_PART_INDICATOR);
      r.a = arc;
      r.b = mklabel(o, d >= 100 ? &lv_font_montserrat_20 : &lv_font_montserrat_14, 0xFFFFFF);
      lv_obj_align(r.b, LV_ALIGN_CENTER, 0, -6);
      r.c = mklabel(o, &lv_font_montserrat_14, 0xA99BB3);
      lv_obj_align(r.c, LV_ALIGN_CENTER, 0, d >= 100 ? 16 : 10);
      break;
    }
    case lc::W_COUNTDOWN: case lc::W_STOPWATCH: case lc::W_DATE: {
      lv_obj_t *o = mkbox(p, w);
      if (w.type != lc::W_DATE) panel_style(o);
      lv_obj_t *l = mklabel(o, font_for(w.z), color_of(w, 0xFFFFFF));
      lv_obj_set_width(l, lv_pct(100));
      lv_obj_set_style_text_align(l, w.type == lc::W_DATE ? align_for(w.a) : LV_TEXT_ALIGN_CENTER, 0);
      lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
      lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);
      r.a = l;
      if (w.type == lc::W_COUNTDOWN && w.tlen) { // caption on top, time below
        r.b = mklabel(o, &lv_font_montserrat_14, 0xA99BB3);
        lv_obj_set_width(r.b, lv_pct(100));
        lv_obj_set_style_text_align(r.b, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(r.b, LV_LABEL_LONG_CLIP);
        lv_label_set_text(r.b, L.text + w.toff);
        lv_obj_align(r.b, LV_ALIGN_TOP_MID, 0, 4);
        lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -4);
      }
      break;
    }
    case lc::W_FOCUS: case lc::W_REMIND: {
      lv_obj_t *o = mkbox(p, w);
      lv_obj_set_style_radius(o, 10, 0);
      lv_obj_set_style_bg_opa(o, w.type == lc::W_FOCUS ? LV_OPA_COVER : 80, 0);
      lv_obj_set_style_bg_color(o, lv_color_black(), 0);
      lv_obj_t *l = mklabel(o, w.type == lc::W_FOCUS && w.h >= 40 ? &lv_font_montserrat_20 : font_for(w.z), 0xFFFFFF);
      lv_obj_set_width(l, lv_pct(100));
      lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
      lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
      lv_obj_center(l);
      r.a = o;
      r.b = l;
      break;
    }
    case lc::W_BUTTON: {
      lv_obj_t *bt = lv_btn_create(p);
      lv_obj_set_pos(bt, w.x, w.y);
      lv_obj_set_size(bt, w.w, w.h);
      lv_obj_set_style_bg_color(bt, lv_color_hex(color_of(w, ACCENT)), 0);
      lv_obj_set_style_radius(bt, w.r ? w.r : 8, 0);
      lv_obj_t *l = mklabel(bt, font_for(w.z), 0x000000);
      lv_label_set_text(l, w.tlen ? L.text + w.toff : "");
      lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
      lv_obj_set_style_max_width(l, w.w - 8, 0);
      lv_obj_center(l);
      r.a = bt;
      break;
    }
    case lc::W_SPOTIFY: { // three buttons: previous, play/pause, next (run by the web page)
      lv_obj_t *o = mkbox(p, w);
      static const char *sym[3] = {LV_SYMBOL_PREV, LV_SYMBOL_PLAY, LV_SYMBOL_NEXT};
      int gap = 6, bw = (w.w - 2 * gap) / 3;
      for (int k = 0; k < 3; k++) {
        lv_obj_t *bt = lv_btn_create(o);
        lv_obj_set_pos(bt, k * (bw + gap), 0);
        lv_obj_set_size(bt, bw, w.h);
        lv_obj_set_style_bg_color(bt, lv_color_hex(color_of(w, ACCENT)), 0);
        lv_obj_set_style_radius(bt, 8, 0);
        lv_obj_t *l = mklabel(bt, &lv_font_montserrat_20, 0x000000);
        lv_label_set_text(l, sym[k]);
        lv_obj_center(l);
        lv_obj_set_user_data(bt, (void *)(uintptr_t)(k == 0 ? lc::BA_SP_PREV : k == 1 ? lc::BA_SP_PLAY : lc::BA_SP_NEXT));
      }
      r.a = o;
      break;
    }
    case lc::W_PICTURE: case lc::W_ALBUM: {
      bool have = w.type == lc::W_PICTURE ? pic_exists(w.i) : art_buf != NULL;
      if (!have) { // placeholder: the picture has not been sent yet
        lv_obj_t *o = mkbox(p, w);
        panel_style(o);
        lv_obj_t *l = mklabel(o, &lv_font_montserrat_14, 0xA99BB3);
        lv_label_set_text(l, w.type == lc::W_PICTURE ? LV_SYMBOL_IMAGE : LV_SYMBOL_AUDIO);
        lv_obj_center(l);
        break;
      }
      lv_obj_t *im = lv_img_create(p);
      lv_obj_set_pos(im, w.x, w.y);
      lv_obj_set_size(im, w.w, w.h);
      if (w.type == lc::W_PICTURE) {
        char lvp[24];
        pic_lv_path(w.i, lvp, sizeof lvp);
        lv_img_set_src(im, lvp);
      } else {
        lv_img_set_src(im, &art_dsc);
      }
      r.a = im;
      break;
    }
    case lc::W_TCAT: { // the original screen's typing cat, same parts and offsets; layout_sync_cat() mirrors their state
      lv_obj_t *o = lv_obj_create(p);
      lv_obj_remove_style_all(o);
      lv_obj_set_pos(o, w.x, w.y);
      lv_obj_set_size(o, 240, 132);
      lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_bg_img_src(o, &ui_img_main_png, 0);
      struct Part {
        const lv_img_dsc_t *img;
        int16_t x, y;
        bool hidden;
      };
      static const Part parts[5] = {{&ui_img_leftmousepaw_png, 76, 44, false}, {&ui_img_rightpawdown_png, -69, 27, false},
                                    {&ui_img_rightpawup_png, -55, 4, true}, {&ui_img_sleepy_png, 10, -1, true}, {&ui_img_closeeye_png, 11, -11, true}};
      for (const Part &pt : parts) {
        lv_obj_t *im = lv_img_create(o);
        lv_img_set_src(im, pt.img);
        lv_obj_align(im, LV_ALIGN_CENTER, pt.x, pt.y);
        if (pt.hidden) lv_obj_add_flag(im, LV_OBJ_FLAG_HIDDEN);
      }
      r.a = o;
      break;
    }
    case lc::W_SHAPE: {
      lv_obj_t *o = mkbox(p, w);
      lv_obj_set_style_bg_color(o, lv_color_hex(color_of(w, ACCENT)), 0);
      lv_obj_set_style_bg_opa(o, w.opa, 0);
      lv_obj_set_style_radius(o, w.k == 1 ? LV_RADIUS_CIRCLE : w.r, 0);
      break;
    }
    case lc::W_SLIDER: {
      lv_obj_t *s = lv_slider_create(p);
      lv_obj_set_pos(s, w.x, w.y);
      lv_obj_set_size(s, w.w, w.h);
      if (w.k == 0) lv_slider_set_range(s, 1, 200);
      else lv_slider_set_range(s, 0, 80);
      lv_obj_set_style_bg_color(s, lv_color_hex(color_of(w, ACCENT)), LV_PART_INDICATOR);
      lv_obj_set_style_bg_color(s, lv_color_hex(color_of(w, ACCENT)), LV_PART_KNOB);
      lv_obj_clear_flag(s, LV_OBJ_FLAG_GESTURE_BUBBLE); // dragging a slider must not swipe the page
      lv_obj_add_event_cb(s, slider_cb, LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)w.k);
      lv_obj_add_event_cb(s, slider_cb, LV_EVENT_RELEASED, (void *)(uintptr_t)w.k);
      r.a = s;
      break;
    }
    default: break;
  }
}

static void apply_bg(lv_obj_t *scr, const lc::Page &pg) {
  lv_obj_remove_style_all(scr);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  switch (pg.bg_type) {
    case lc::BG_WEATHER: lv_obj_set_style_bg_img_src(scr, wallpaper_img[D.wall < 6 ? D.wall : 0], 0); break;
    case lc::BG_BUILTIN: lv_obj_set_style_bg_img_src(scr, wallpaper_img[pg.bg_idx < 6 ? pg.bg_idx : 0], 0); break;
    case lc::BG_SOLID: lv_obj_set_style_bg_color(scr, lv_color_hex(pg.c1), 0); break;
    case lc::BG_IMAGE:
      if (pic_exists(pg.bg_idx)) {
        static char bg_path[lc::PICTURE_SLOTS][24]; // style keeps the pointer: one stable string per slot
        pic_lv_path(pg.bg_idx, bg_path[pg.bg_idx], sizeof bg_path[0]);
        lv_obj_set_style_bg_img_src(scr, bg_path[pg.bg_idx], 0);
      } else {
        lv_obj_set_style_bg_img_src(scr, wallpaper_img[D.wall < 6 ? D.wall : 0], 0); // not sent yet: weather wallpaper
      }
      break;
    case lc::BG_GRADIENT:
      lv_obj_set_style_bg_color(scr, lv_color_hex(pg.c1), 0);
      lv_obj_set_style_bg_grad_color(scr, lv_color_hex(pg.c2), 0);
      lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
      break;
  }
}

static lv_obj_t *make_dots(lv_obj_t *parent, uint8_t idx) {
  lv_obj_t *c = lv_obj_create(parent);
  lv_obj_remove_style_all(c);
  lv_obj_set_size(c, L.npages * 12, 10);
  lv_obj_align(c, LV_ALIGN_BOTTOM_MID, 0, -2);
  lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  for (uint8_t i = 0; i < L.npages; i++) {
    lv_obj_t *d = lv_obj_create(c);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, 6, 6);
    lv_obj_set_pos(d, i * 12 + 3, 2);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(d, lv_color_hex(i == idx ? 0xFFFFFF : 0x707070), 0);
    lv_obj_clear_flag(d, LV_OBJ_FLAG_CLICKABLE);
  }
  return c;
}

static void set_dots(lv_obj_t *c, uint8_t idx) {
  if (!c) return;
  for (uint8_t i = 0; i < L.npages; i++) {
    lv_obj_t *d = lv_obj_get_child(c, i);
    if (d) lv_obj_set_style_bg_color(d, lv_color_hex(i == idx ? 0xFFFFFF : 0x707070), 0);
  }
}

// Returns the screen showing page idx (the original screen, or a freshly built custom one).
static lv_obj_t *prepare_page(uint8_t idx) {
  const lc::Page &pg = L.pages[idx];
  if (pg.is_main) return ui_Screen_main;
  lv_obj_t *s = scr_c[scr_next];
  if (s && lv_scr_act() == s) { // never rebuild what is on screen
    scr_next ^= 1;
    s = scr_c[scr_next];
  }
  if (!s) {
    s = lv_obj_create(NULL);
    lv_obj_add_event_cb(s, gesture_cb, LV_EVENT_GESTURE, NULL);
    scr_c[scr_next] = s;
  } else {
    lv_obj_clean(s);
  }
  scr_next ^= 1;
  apply_bg(s, pg);
  memset(rt, 0, sizeof rt);
  for (uint8_t i = 0; i < pg.nw && i < lc::MAX_WIDGETS_PAGE; i++) {
    uint32_t before = lv_obj_get_child_cnt(s);
    const Widget &w = L.widgets[pg.first + i];
    build_widget(s, w, rt[i]);
    lv_obj_t *root = lv_obj_get_child_cnt(s) > before ? lv_obj_get_child(s, before) : NULL;
    if (!root) continue;
    void *ud = (void *)(uintptr_t)i;
    switch (w.type) { // tappable widgets
      case lc::W_BUTTON: case lc::W_POMO: case lc::W_STOPWATCH: case lc::W_FOCUS: case lc::W_REMIND:
        lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(root, widget_cb, LV_EVENT_SHORT_CLICKED, ud);
        lv_obj_add_event_cb(root, widget_cb, LV_EVENT_LONG_PRESSED, ud);
        break;
      case lc::W_SPOTIFY:
        for (uint32_t k = 0; k < lv_obj_get_child_cnt(root); k++) lv_obj_add_event_cb(lv_obj_get_child(root, k), widget_cb, LV_EVENT_SHORT_CLICKED, ud);
        break;
      default: break;
    }
  }
  if (L.npages > 1) make_dots(s, idx);
  return s;
}

// ------------------------------------------------------------------------------------------------
// Live data
// ------------------------------------------------------------------------------------------------
static void update_widgets() {
  lv_obj_t *act = lv_scr_act();
  if (!active || L.pages[cur].is_main || (act != scr_c[0] && act != scr_c[1])) return;
  const lc::Page &pg = L.pages[cur];
  char buf[48];
  for (uint8_t i = 0; i < pg.nw && i < lc::MAX_WIDGETS_PAGE; i++) {
    const Widget &w = L.widgets[pg.first + i];
    RT &r = rt[i];
    bool changed = (r.seen != D.ver);
    r.seen = D.ver;
    switch (w.type) {
      case lc::W_ANALOG:
        if (changed && D.have_time && r.a) {
          lv_img_set_angle(r.a, (D.h % 12) * 300 + D.m * 5);
          lv_img_set_angle(r.b, D.m * 60);
          lv_img_set_angle(r.c, D.s * 60);
        }
        break;
      case lc::W_DIGITAL:
        if (changed && r.a) {
          if (!D.have_time) break;
          if (w.f24) {
            if (w.secs) snprintf(buf, sizeof buf, "%02u:%02u:%02u", D.h, D.m, D.s);
            else snprintf(buf, sizeof buf, "%02u:%02u", D.h, D.m);
          } else {
            unsigned h12 = D.h % 12 ? D.h % 12 : 12;
            if (w.secs) snprintf(buf, sizeof buf, "%u:%02u:%02u %s", h12, D.m, D.s, D.h < 12 ? "AM" : "PM");
            else snprintf(buf, sizeof buf, "%u:%02u %s", h12, D.m, D.h < 12 ? "AM" : "PM");
          }
          lv_label_set_text(r.a, buf);
        }
        break;
      case lc::W_WEATHER:
        if (changed && r.a && r.b) {
          bool fah = (w.k == 0) ? use_fahrenheit : (w.k == 2);
          if (!D.have_w) {
            snprintf(buf, sizeof buf, "--%s\n--%%\n--:--", fah ? "°F" : "°C");
          } else {
            int t = D.temp;
            if (fah) t = (int)lroundf(D.temp * 9.0f / 5.0f + 32.0f);
            snprintf(buf, sizeof buf, "%d%s\n%u%%\n%02u:%02u", t, fah ? "°F" : "°C", (unsigned)D.hum, D.wh, D.wm);
            lv_img_set_src(r.a, WICON[D.wgroup < 6 ? D.wgroup : 0]);
          }
          lv_label_set_text(r.b, buf);
        }
        break;
      case lc::W_NOWPLAY:
        if (changed && r.a) {
          lv_label_set_text(r.a, D.media == 0 ? "[ STOPPED ]" : D.media == 1 ? "[ PLAYING ]" : D.media == 2 ? "[ PAUSED ]" : "[ Status ]");
          lv_label_set_text(r.b, D.have_music ? D.title : "Youtube/Spotify/Music");
          lv_label_set_text(r.c, D.have_music ? D.artist : "* Swipe down for setting *");
        }
        break;
      case lc::W_SYSBARS:
        if (changed && r.a) {
          lv_bar_set_value(r.a, D.cpu, LV_ANIM_OFF);
          lv_bar_set_value(r.b, D.mem, LV_ANIM_OFF);
          lv_bar_set_value(r.c, D.batt, LV_ANIM_OFF);
        }
        break;
      case lc::W_TAPS:
        if (changed && r.a) {
          snprintf(buf, sizeof buf, "Taps: %lu", (unsigned long)D.taps);
          lv_label_set_text(r.a, buf);
        }
        break;
      case lc::W_MOOD:
        if (r.a && r.b) { // not gated: the cat falls asleep by itself
          bool happy = (D.mood == MOOD_HAPPY);
          bool asleep = current_sleep_state; // covers the sleepy key action too: it just fast-forwards the 60 s timer
          if (happy) lv_obj_clear_flag(r.a, LV_OBJ_FLAG_HIDDEN);
          else lv_obj_add_flag(r.a, LV_OBJ_FLAG_HIDDEN);
          if (asleep) lv_obj_clear_flag(r.b, LV_OBJ_FLAG_HIDDEN);
          else lv_obj_add_flag(r.b, LV_OBJ_FLAG_HIDDEN);
        }
        break;
      case lc::W_SLIDER:
        if (r.a && !lv_obj_has_state(r.a, LV_STATE_PRESSED)) {
          int v = (w.k == 0) ? brightness : volume;
          if (lv_slider_get_value(r.a) != v) lv_slider_set_value(r.a, v, LV_ANIM_OFF);
        }
        break;
      case lc::W_POMO:
        if (r.a && r.b && r.c) {
          uint32_t left = POMO.running ? (int32_t)(POMO.end_ms - millis()) > 0 ? POMO.end_ms - millis() : 0 : POMO.left_ms;
          uint32_t total = POMO.total_ms ? POMO.total_ms : (uint32_t)w.p * 60000UL;
          if (!POMO.running && !POMO.left_ms) left = total = (uint32_t)w.p * 60000UL; // idle: shows the work length
          uint32_t sec = (left + 999) / 1000;
          snprintf(buf, sizeof buf, "%02lu:%02lu", (unsigned long)(sec / 60), (unsigned long)(sec % 60));
          if (strcmp(lv_label_get_text(r.b), buf)) lv_label_set_text(r.b, buf);
          lv_arc_set_value(r.a, total ? (int16_t)(1000UL * (total - left) / total) : 0);
          const char *ph = !POMO.running && !POMO.left_ms ? "tap to start" : POMO.on_break ? (POMO.running ? "break" : "break, paused") : (POMO.running ? "focus" : "paused");
          if (strcmp(lv_label_get_text(r.c), ph)) lv_label_set_text(r.c, ph);
          lv_obj_set_style_arc_color(r.a, lv_color_hex(POMO.on_break ? 0xA5DBAF : color_of(w, ACCENT)), LV_PART_INDICATOR);
        }
        break;
      case lc::W_COUNTDOWN:
        if (changed && r.a) {
          if (!D.have_time) { lv_label_set_text(r.a, "--:--:--"); break; }
          int32_t now = D.h * 3600 + D.m * 60 + D.s, target = (int32_t)w.p * 60;
          int32_t left = target - now;
          if (left < 0) left += 86400; // the next time it comes round
          if (left == 0 || left > 86340) snprintf(buf, sizeof buf, "now!");
          else snprintf(buf, sizeof buf, "%ld:%02ld:%02ld", (long)(left / 3600), (long)(left / 60 % 60), (long)(left % 60));
          lv_label_set_text(r.a, buf);
        }
        break;
      case lc::W_STOPWATCH:
        if (r.a) {
          uint32_t ms = SW.acc_ms + (SW.running ? millis() - SW.start_ms : 0);
          snprintf(buf, sizeof buf, "%02lu:%02lu.%lu", (unsigned long)(ms / 60000 % 100), (unsigned long)(ms / 1000 % 60), (unsigned long)(ms / 100 % 10));
          if (strcmp(lv_label_get_text(r.a), buf)) lv_label_set_text(r.a, buf);
        }
        break;
      case lc::W_DATE:
        if (changed && r.a) {
          if (!D.have_date) { lv_label_set_text(r.a, "---"); break; }
          uint8_t mi = D.mon >= 1 && D.mon <= 12 ? D.mon - 1 : 0;
          if (w.k == 1) snprintf(buf, sizeof buf, "%04u-%02u-%02u", D.year, D.mon, D.day);
          else if (w.k == 2) snprintf(buf, sizeof buf, "%s %u", MONTH[mi], D.day);
          else snprintf(buf, sizeof buf, "%s %u %s", WDAY[D.wday % 7], D.day, MON3[mi]);
          lv_label_set_text(r.a, buf);
        }
        break;
      case lc::W_FOCUS:
        if (r.a && r.b) {
          const char *t = focus_busy ? (w.tlen ? L.text + w.toff : "Focus") : "Available";
          if (strcmp(lv_label_get_text(r.b), t)) {
            lv_label_set_text(r.b, t);
            lv_obj_set_style_bg_color(r.a, lv_color_hex(focus_busy ? 0xB3261E : 0x2E7D32), 0);
          }
        }
        break;
      case lc::W_REMIND:
        if (r.b) {
          uint8_t idx = pg.first + i, kind = w.k < 3 ? w.k : 0;
          if (remind_shown[idx] && millis() - remind_shown[idx] < 60000UL) snprintf(buf, sizeof buf, "%s: now!", REMIND_SHORT[kind]);
          else {
            uint32_t left = (int32_t)(remind_next[idx] - millis()) > 0 ? remind_next[idx] - millis() : 0;
            snprintf(buf, sizeof buf, "%s in %lu min", REMIND_SHORT[kind], (unsigned long)((left + 59999) / 60000));
          }
          if (strcmp(lv_label_get_text(r.b), buf)) lv_label_set_text(r.b, buf);
        }
        break;
      default: break;
    }
  }
}

// ---- pomodoro / reminders run whether or not their page is on screen ----
static void pomo_start(uint16_t work, uint8_t brk) {
  if (POMO.running) { // pause
    POMO.left_ms = (int32_t)(POMO.end_ms - millis()) > 0 ? POMO.end_ms - millis() : 0;
    POMO.running = false;
    return;
  }
  if (!POMO.left_ms) { // fresh start of a work period
    POMO.on_break = false;
    POMO.total_ms = (uint32_t)work * 60000UL;
    POMO.left_ms = POMO.total_ms;
  }
  (void)brk;
  POMO.end_ms = millis() + POMO.left_ms;
  POMO.running = true;
}
static void pomo_reset() { POMO.running = false; POMO.on_break = false; POMO.left_ms = 0; POMO.total_ms = 0; }
static const Widget *first_of(uint8_t type) {
  for (uint8_t i = 0; i < L.nwidgets; i++) if (L.widgets[i].type == type) return &L.widgets[i];
  return NULL;
}
static void timers_cb(lv_timer_t *) {
  if (!active) return;
  uint32_t now = millis();
  if (POMO.running && (int32_t)(now - POMO.end_ms) >= 0) {
    const Widget *pw = first_of(lc::W_POMO);
    beepbeep();
    if (!POMO.on_break && pw) { // work done: the break starts by itself
      POMO.on_break = true;
      POMO.total_ms = (uint32_t)pw->b * 60000UL;
      POMO.end_ms = now + POMO.total_ms;
      set_mood(MOOD_HAPPY);
      ui_toast("Break time!", 4000);
    } else { // break done: wait for the next tap
      pomo_reset();
      ui_toast("Break over: tap to focus again", 4000);
    }
  }
  for (uint8_t i = 0; i < L.nwidgets; i++) {
    const Widget &w = L.widgets[i];
    if (w.type != lc::W_REMIND || (int32_t)(now - remind_next[i]) < 0) continue;
    remind_next[i] = now + (uint32_t)w.p * 60000UL;
    remind_shown[i] = now;
    ui_toast(REMIND_TEXT[w.k < 3 ? w.k : 0], 6000);
    set_mood(MOOD_HAPPY);
    beepbeep();
  }
}

// ---- taps on widgets ----
static void queue_action(uint8_t e, uint16_t g) {
  if (pend_n < sizeof pend / sizeof pend[0]) pend[pend_n++] = {e, g};
}
static void widget_cb(lv_event_t *ev) {
  if (!active || L.pages[cur].is_main) return;
  lv_event_code_t code = lv_event_get_code(ev);
  uint8_t i = (uint8_t)(uintptr_t)lv_event_get_user_data(ev);
  const lc::Page &pg = L.pages[cur];
  if (i >= pg.nw) return;
  const Widget &w = L.widgets[pg.first + i];
  bool tap = (code == LV_EVENT_SHORT_CLICKED), hold = (code == LV_EVENT_LONG_PRESSED);
  switch (w.type) {
    case lc::W_BUTTON: if (tap) queue_action(w.e, w.g); break;
    case lc::W_SPOTIFY: if (tap) queue_action((uint8_t)(uintptr_t)lv_obj_get_user_data(lv_event_get_target(ev)), 0); break;
    case lc::W_POMO: if (tap) queue_action(lc::BA_POMO_TOGGLE, 0); else if (hold) queue_action(lc::BA_POMO_RESET, 0); break;
    case lc::W_STOPWATCH: if (tap) queue_action(lc::BA_SW_TOGGLE, 0); else if (hold) queue_action(lc::BA_SW_RESET, 0); break;
    case lc::W_FOCUS: if (tap) queue_action(lc::BA_FOCUS_TOGGLE, 0); break;
    case lc::W_REMIND: if (tap) { uint8_t idx = pg.first + i; remind_next[idx] = millis() + (uint32_t)w.p * 60000UL; remind_shown[idx] = 0; clickSound(); } break;
    default: break;
  }
}

static void run_action(uint8_t e, uint16_t g) {
  switch (e) {
    case lc::BA_PAW_TAP: board_key_action(ACT_PAW_TAP); break;
    case lc::BA_HAPPY: board_key_action(ACT_HAPPY); break;
    case lc::BA_SLEEPY: board_key_action(ACT_SLEEPY); break;
    case lc::BA_TOGGLE_UNIT: board_key_action(ACT_TOGGLE_UNIT); break;
    case lc::BA_MUTE: board_key_action(ACT_MUTE); break;
    case lc::BA_BRIGHTNESS_STEP: board_key_action(ACT_BRIGHTNESS_STEP); break;
    case lc::BA_WALLPAPER_NEXT: board_key_action(ACT_WALLPAPER_NEXT); break;
    case lc::BA_PAGE_NEXT: if (L.npages > 1) goto_page((cur + 1) % L.npages, LV_SCR_LOAD_ANIM_MOVE_LEFT); break;
    case lc::BA_PAGE_PREV: if (L.npages > 1) goto_page((cur + L.npages - 1) % L.npages, LV_SCR_LOAD_ANIM_MOVE_RIGHT); break;
    case lc::BA_POMO_TOGGLE: {
      const Widget *pw = first_of(lc::W_POMO);
      pomo_start(pw ? pw->p : 25, pw ? pw->b : 5);
      clickSound();
      break;
    }
    case lc::BA_POMO_RESET: pomo_reset(); offSound(); break;
    case lc::BA_FOCUS_TOGGLE: focus_busy = !focus_busy; status_led_update(); clickSound(); break;
    case lc::BA_SW_TOGGLE:
      if (SW.running) { SW.acc_ms += millis() - SW.start_ms; SW.running = false; }
      else { SW.start_ms = millis(); SW.running = true; }
      clickSound();
      break;
    case lc::BA_SW_RESET: SW.running = false; SW.acc_ms = 0; offSound(); break;
    case lc::BA_SP_PLAY: case lc::BA_SP_NEXT: case lc::BA_SP_PREV: case lc::BA_HOST_EVENT:
      host_send_event(e, g);
      clickSound();
      break;
    case lc::BA_BLE_KEY:
      if (!ble_keys_send((uint8_t)(g >> 8), (uint8_t)(g & 0xFF))) ui_toast("Bluetooth keys: not paired, or not in this firmware", 2500);
      else board_key_action(ACT_PAW_TAP); // the cat types what you send
      break;
    case lc::BA_BLE_MEDIA:
      if (!ble_media_send(g)) ui_toast("Bluetooth keys: not paired, or not in this firmware", 2500);
      else clickSound();
      break;
    default: break;
  }
  D.ver++; // redraw what the action changed
}

static void tick_cb(lv_timer_t *) { update_widgets(); }

// Typing cat widgets copy the state of the original screen's cat (which main.cpp and animation.cpp keep animating
// whether or not that screen is shown): paw up/down, eyes shut, asleep, mouse paw position.
static inline void copy_hidden(lv_obj_t *dst, lv_obj_t *src) {
  bool h = lv_obj_has_flag(src, LV_OBJ_FLAG_HIDDEN);
  if (h != lv_obj_has_flag(dst, LV_OBJ_FLAG_HIDDEN)) {
    if (h) lv_obj_add_flag(dst, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(dst, LV_OBJ_FLAG_HIDDEN);
  }
}
void layout_sync_cat() {
  if (!active || L.pages[cur].is_main) return;
  lv_obj_t *act = lv_scr_act();
  if (act != scr_c[0] && act != scr_c[1]) return;
  const lc::Page &pg = L.pages[cur];
  for (uint8_t i = 0; i < pg.nw && i < lc::MAX_WIDGETS_PAGE; i++) {
    if (L.widgets[pg.first + i].type != lc::W_TCAT || !rt[i].a) continue;
    lv_obj_t *o = rt[i].a;
    lv_obj_t *mouse = lv_obj_get_child(o, 0);
    lv_coord_t mx = lv_obj_get_x_aligned(ui_main_Image_leftpaw);
    if (lv_obj_get_x_aligned(mouse) != mx) lv_obj_set_x(mouse, mx);
    copy_hidden(lv_obj_get_child(o, 1), ui_main_Image_rightpawdown);
    copy_hidden(lv_obj_get_child(o, 2), ui_main_Image_rightpawup);
    copy_hidden(lv_obj_get_child(o, 3), ui_main_Image_sleepy);
    copy_hidden(lv_obj_get_child(o, 4), ui_main_Image_eyelid);
  }
}
static void cat_cb(lv_timer_t *) { layout_sync_cat(); }

void layout_feed_time(uint8_t h, uint8_t m, uint8_t s) {
  D.have_time = true;
  D.h = h;
  D.m = m;
  D.s = s;
  D.ver++;
}
void layout_feed_weather(uint8_t group, int8_t temp_c, uint8_t humidity, uint8_t hh, uint8_t mm) {
  D.have_w = true;
  D.wgroup = group;
  D.temp = temp_c;
  D.hum = humidity;
  D.wh = hh;
  D.wm = mm;
  D.ver++;
}
void layout_feed_music(const char *title, const char *artist) {
  D.have_music = true;
  strlcpy(D.title, title, sizeof D.title);
  strlcpy(D.artist, artist, sizeof D.artist);
  D.ver++;
}
void layout_feed_media_state(uint8_t state) {
  D.media = state;
  D.ver++;
}
void layout_feed_system(uint8_t cpu, uint8_t mem, uint8_t batt, bool charging) {
  D.cpu = cpu;
  D.mem = mem;
  D.batt = batt;
  D.chg = charging;
  D.ver++;
}
void layout_feed_taps(uint32_t count) {
  D.taps = count;
  D.ver++;
}
void layout_feed_mood(uint8_t mood) { D.mood = mood; }
void layout_feed_date(uint16_t year, uint8_t mon, uint8_t day, uint8_t wday) {
  D.have_date = true;
  D.year = year;
  D.mon = mon;
  D.day = day;
  D.wday = wday;
  D.ver++;
}
int8_t layout_focus_led() { // -1: no focus sign in the active layout, else 1 busy / 0 available
  if (!active || !first_of(lc::W_FOCUS)) return -1;
  return focus_busy ? 1 : 0;
}
void layout_on_wallpaper(uint8_t idx) {
  if (idx > 5) return;
  D.wall = idx;
  if (active && !L.pages[cur].is_main && L.pages[cur].bg_type == lc::BG_WEATHER) {
    lv_obj_t *act = lv_scr_act();
    if (act == scr_c[0] || act == scr_c[1]) lv_obj_set_style_bg_img_src(act, wallpaper_img[idx], 0);
  }
}

// ------------------------------------------------------------------------------------------------
// Navigation
// ------------------------------------------------------------------------------------------------
static bool is_page_screen(lv_obj_t *s) { return s == ui_Screen_main || s == scr_c[0] || s == scr_c[1]; }

static void clean_later_cb(lv_timer_t *t) {
  lv_obj_t *s = (lv_obj_t *)t->user_data;
  if (s && lv_scr_act() != s && (s == scr_c[0] || s == scr_c[1])) lv_obj_clean(s); // free the heap of a page that slid away
  lv_timer_del(t);
}

static void goto_page(uint8_t idx, lv_scr_load_anim_t anim) {
  if (!active || idx >= L.npages) return;
  lv_obj_t *old = lv_scr_act();
  lv_obj_t *t = prepare_page(idx);
  cur = idx;
  last_nav_ms = millis();
  set_dots(dots_main, cur);
  lv_scr_load_anim(t, anim, anim == LV_SCR_LOAD_ANIM_NONE ? 0 : 300, 0, false);
  log_i("layout page %u/%u heap free %u largest %u", (unsigned)cur + 1, (unsigned)L.npages, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
  if (old != t && (old == scr_c[0] || old == scr_c[1])) lv_timer_set_repeat_count(lv_timer_create(clean_later_cb, 800, old), 1);
}

static void gesture_cb(lv_event_t *e) {
  if (!active) return;
  lv_indev_t *in = lv_indev_get_act();
  lv_dir_t d = lv_indev_get_gesture_dir(in);
  if (d == LV_DIR_LEFT && L.npages > 1) {
    lv_indev_wait_release(in);
    goto_page((cur + 1) % L.npages, LV_SCR_LOAD_ANIM_MOVE_LEFT);
  } else if (d == LV_DIR_RIGHT && L.npages > 1) {
    lv_indev_wait_release(in);
    goto_page((cur + L.npages - 1) % L.npages, LV_SCR_LOAD_ANIM_MOVE_RIGHT);
  } else if (d == LV_DIR_BOTTOM && lv_event_get_current_target(e) != ui_Screen_main) { // the original screen has its own swipe-down handler
    lv_indev_wait_release(in);
    lv_scr_load_anim(ui_Screen_setting, LV_SCR_LOAD_ANIM_MOVE_BOTTOM, 500, 0, false);
  }
}

// The generated handler on the settings screen returns to ui_Screen_main; with a layout active it must return to the current page.
static void settings_gesture_cb(lv_event_t *e) {
  if (!active || lv_event_get_code(e) != LV_EVENT_GESTURE) return;
  lv_indev_t *in = lv_indev_get_act();
  if (lv_indev_get_gesture_dir(in) != LV_DIR_TOP) return;
  lv_indev_wait_release(in);
  lv_obj_t *t = prepare_page(cur);
  last_nav_ms = millis();
  lv_scr_load_anim(t, LV_SCR_LOAD_ANIM_MOVE_TOP, 500, 0, false);
}

// Boot: the SquareLine title screen fades into ui_Screen_main. If the home page is a custom page, hop over to it.
static void main_loaded_cb(lv_event_t *e) {
  if (!active || lv_event_get_code(e) != LV_EVENT_SCREEN_LOADED || L.pages[cur].is_main) return;
  lv_scr_load(prepare_page(cur));
}

static void rotate_cb(lv_timer_t *) {
  if (!active || L.rotate_s == 0 || L.npages < 2 || !is_page_screen(lv_scr_act())) return;
  if (lv_disp_get_inactive_time(NULL) < 3000) return; // someone is touching the screen
  if (millis() - last_nav_ms < (uint32_t)L.rotate_s * 1000UL) return;
  goto_page((cur + 1) % L.npages, LV_SCR_LOAD_ANIM_MOVE_LEFT);
}

static void set_status() {
  if (safe_reason) {
    static const char *why[5] = {"", "Safe mode: BOOT held", "Safe mode: layout crashed", "Safe mode: layout invalid", "Safe mode: layout ignored"};
    snprintf(status_buf, sizeof status_buf, "%s", why[safe_reason < 5 ? safe_reason : 4]);
  } else if (active) {
    snprintf(status_buf, sizeof status_buf, "Custom layout, %u page%s", L.npages, L.npages == 1 ? "" : "s");
  } else {
    snprintf(status_buf, sizeof status_buf, "Default layout");
  }
}

static void activate() {
  if (!handlers_on) {
    lv_obj_remove_event_cb(ui_Screen_setting, ui_event_Screen_setting); // generated: swipe up -> ui_Screen_main
    lv_obj_add_event_cb(ui_Screen_setting, settings_gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(ui_Screen_main, gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(ui_Screen_main, main_loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);
    handlers_on = true;
    lv_timer_create(tick_cb, 250, NULL);
    lv_timer_create(cat_cb, 30, NULL); // blink (200 ms) and mouse paw need a quicker copy than the other widgets
    lv_timer_create(rotate_cb, 1000, NULL);
    lv_timer_create(timers_cb, 250, NULL); // pomodoro and reminders keep running on any page
  }
  for (uint8_t i = 0; i < L.nwidgets; i++) { // reminders count from now
    remind_next[i] = millis() + (uint32_t)L.widgets[i].p * 60000UL;
    remind_shown[i] = 0;
  }
  active = true;
  cur = 0;
  last_nav_ms = millis();
  if (dots_main) {
    lv_obj_del(dots_main);
    dots_main = NULL;
  }
  if (L.npages > 1) dots_main = make_dots(ui_Screen_main, cur);
  safe_reason = 0;
  set_status();
  status_led_update(); // a focus sign takes over the LED
}

static void deactivate() {
  active = false;
  if (handlers_on) {
    lv_obj_remove_event_cb(ui_Screen_setting, settings_gesture_cb);
    lv_obj_remove_event_cb(ui_Screen_main, gesture_cb);
    lv_obj_remove_event_cb(ui_Screen_main, main_loaded_cb);
    lv_obj_add_event_cb(ui_Screen_setting, ui_event_Screen_setting, LV_EVENT_ALL, NULL); // the generated behaviour again
    handlers_on = false;
  }
  if (dots_main) {
    lv_obj_del(dots_main);
    dots_main = NULL;
  }
  lv_obj_t *act = lv_scr_act();
  if (act == scr_c[0] || act == scr_c[1]) lv_scr_load_anim(ui_Screen_main, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);
  for (int i = 0; i < 2; i++)
    if (scr_c[i] && scr_c[i] != act) lv_obj_clean(scr_c[i]);
  set_status();
}

// ------------------------------------------------------------------------------------------------
// Safe mode + boot
// ------------------------------------------------------------------------------------------------
static void stable_cb(lv_timer_t *t) {
  Preferences lp;
  lp.begin("layout", false);
  lp.putUChar("pend", 0);
  lp.putUChar("crash", 0);
  lp.end();
  lv_timer_del(t);
}

static void show_note(const char *text) {
  lv_obj_t *l = lv_label_create(lv_layer_top());
  lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(l, lv_color_white(), 0);
  lv_obj_set_style_bg_color(l, lv_color_hex(0x802020), 0);
  lv_obj_set_style_bg_opa(l, LV_OPA_80, 0);
  lv_obj_set_style_radius(l, 8, 0);
  lv_obj_set_style_pad_all(l, 6, 0);
  lv_label_set_text(l, text);
  lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 6);
  lv_timer_t *t = lv_timer_create([](lv_timer_t *tt) {
    lv_obj_del((lv_obj_t *)tt->user_data);
    lv_timer_del(tt);
  }, 12000, l);
  lv_timer_set_repeat_count(t, 1);
}

void layout_begin(bool boot_held) {
  log_i("layout: display %dx%d", (int)lv_disp_get_hor_res(NULL), (int)lv_disp_get_ver_res(NULL)); // layouts are checked against 240x320 (layout_core.h)
  Preferences lp;
  lp.begin("layout", false);
  stored_crc_cache = lp.getUInt("crc", 0);
  uint32_t badcrc = lp.getUInt("badcrc", 0);
  uint8_t pend = lp.getUChar("pend", 0), crashes = lp.getUChar("crash", 0);
  esp_reset_reason_t rr = esp_reset_reason();
  bool crashed = (rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT || rr == ESP_RST_WDT || rr == ESP_RST_BROWNOUT);
  // pend = the previous run started a layout and never reached 10 stable seconds. Only a crash counts: power cuts and USB resets do not.
  if (pend && crashed && crashes < 250) {
    crashes++;
    lp.putUChar("crash", crashes);
  }
  if (path_exists(TMP_PATH)) LittleFS.remove(TMP_PATH); // leftover of an upload that was cut by a reset or power loss
  if (path_exists(PIC_TMP)) LittleFS.remove(PIC_TMP);
  bool stored = file_exists();
  if (stored && stored_crc_cache == 0) stored_crc_cache = 0xFFFFFFFFu; // a layout file with unknown crc (should not happen)
  const char *note = NULL;

  if (!stored) {
    lp.putUChar("pend", 0);
    lp.putUChar("crash", 0);
  } else if (boot_held) {
    safe_reason = 1; // this boot only; the layout comes back on the next normal boot
    note = "Safe mode: layout skipped (BOOT held)";
  } else if (crashes >= 2) {
    safe_reason = 2;
    lp.putUInt("badcrc", stored_crc_cache);
    lp.putUChar("crash", 0);
    lp.putUChar("pend", 0);
    note = "Safe mode: layout crashed twice, ignored";
  } else if (badcrc != 0 && badcrc == stored_crc_cache) {
    safe_reason = 4;
    note = "Safe mode: layout ignored (upload a new one)";
  } else {
    lc::Err e = load_file(L);
    if (e != lc::E_OK) {
      safe_reason = 3;
      lp.putUInt("badcrc", stored_crc_cache);
      note = "Safe mode: stored layout is invalid";
    } else {
      lp.putUChar("pend", 1); // armed until stable_cb clears it
      activate();
      lv_timer_create(stable_cb, STABLE_MS, NULL);
    }
  }
  lp.end();
  if (!active) {
    lv_timer_create(stable_cb, STABLE_MS, NULL); // clears the counters either way
    set_status();
  }
  if (note) show_note(note);
}

// ------------------------------------------------------------------------------------------------
// Upload protocol
// ------------------------------------------------------------------------------------------------
bool layout_packet_id(uint8_t id) { return (id >= lc::PKT_BEGIN && id <= lc::PKT_QUERY) || id == lc::PKT_RESET; }

void layout_packet(const uint8_t *pkt, uint8_t len) {
  if (len < 3) return;
  lc::Ack a = session.handle(pkt[1], pkt + 3, pkt[2]);
  uint8_t out[lc::ACK_FRAME_BYTES];
  lc::ack_encode(a, out);
  Serial.write(out, sizeof out);
}

void layout_poll() {
  if (pend_n) { // button actions, run here so they never run inside an LVGL event (paw taps redraw, pages change)
    uint8_t n = pend_n;
    decltype(pend) todo;
    memcpy(todo, pend, sizeof pend);
    pend_n = 0;
    for (uint8_t i = 0; i < n; i++) run_action(todo[i].e, todo[i].g);
  }
  if (session.take_pictures_changed() && active && !L.pages[cur].is_main) { // rebuild the page so it picks up the new picture
    lv_obj_t *act = lv_scr_act();
    if (act == scr_c[0] || act == scr_c[1]) lv_scr_load(prepare_page(cur));
  }
  if (!session.take_changed()) return;
  if (!file_exists()) {
    safe_reason = 0;
    deactivate();
    return;
  }
  lc::Err e = load_file(L);
  if (e != lc::E_OK) { // cannot happen after the END check, but never leave a half-parsed layout active
    deactivate();
    return;
  }
  Preferences lp;
  lp.begin("layout", false);
  lp.putUChar("crash", 0);
  lp.putUChar("pend", 0);
  lp.end();
  activate();
  lv_obj_t *t = prepare_page(0);
  lv_scr_load_anim(t, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);
  log_i("layout applied: %u pages, %u widgets, heap free %u largest %u", (unsigned)L.npages, (unsigned)L.nwidgets, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
}

bool layout_active() { return active; }
const char *layout_status() { return status_buf; }

void layout_reset() {
  session.handle(lc::PKT_RESET, nullptr, 0);
  layout_poll();
}
