#include "layout_core.h"
#include <new>
#include <string.h>

namespace lc {

static const char *const ERR_NAMES[E_COUNT] = {
  "ok", "empty", "too big", "syntax error", "nested too deep", "unsupported version", "no pages", "too many pages", "bad page",
  "second main page", "too many widgets", "unknown widget type", "widget outside the 240x320 screen", "bad color", "value out of range",
  "bad text", "text pool full", "trailing data"};

const char *err_name(Err e) { return e < E_COUNT ? ERR_NAMES[e] : "?"; }

uint32_t crc32_update(uint32_t crc, const uint8_t *d, size_t n) {
  crc = ~crc;
  while (n--) {
    crc ^= *d++;
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

// ------------------------------------------------------------------------------------------------
// Minimal bounded JSON reader. Only what the schema needs: objects, arrays, strings (\" \\ \/ escapes), integers, true/false/null.
// ------------------------------------------------------------------------------------------------
namespace {

constexpr uint8_t MAX_DEPTH = 8;

struct P {
  const char *s;
  size_t n;
  size_t i;
};

void ws(P &p) {
  while (p.i < p.n) {
    char c = p.s[p.i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') p.i++;
    else break;
  }
}
bool eat(P &p, char c) {
  ws(p);
  if (p.i < p.n && p.s[p.i] == c) {
    p.i++;
    return true;
  }
  return false;
}
bool lit(P &p, const char *w) {
  size_t l = strlen(w);
  if (p.i + l > p.n || memcmp(p.s + p.i, w, l) != 0) return false;
  p.i += l;
  return true;
}

// Reads a string. Writes at most cap-1 chars to out (out may be NULL); len is the full decoded length.
Err read_str(P &p, char *out, size_t cap, size_t &len) {
  ws(p);
  if (p.i >= p.n || p.s[p.i] != '"') return E_SYNTAX;
  p.i++;
  len = 0;
  for (;;) {
    if (p.i >= p.n) return E_SYNTAX;
    unsigned char c = (unsigned char)p.s[p.i++];
    if (c == '"') break;
    if (c < 0x20) return E_SYNTAX;
    if (c == '\\') {
      if (p.i >= p.n) return E_SYNTAX;
      char e = p.s[p.i++];
      if (e == '"' || e == '\\' || e == '/') c = (unsigned char)e;
      else return E_SYNTAX;
    }
    if (out && cap > 0 && len + 1 < cap) out[len] = (char)c;
    len++;
  }
  if (out && cap > 0) out[len < cap ? len : cap - 1] = '\0';
  return E_OK;
}

Err read_int(P &p, int32_t &v) {
  ws(p);
  bool neg = false;
  if (p.i < p.n && p.s[p.i] == '-') {
    neg = true;
    p.i++;
  }
  size_t d = 0;
  int32_t x = 0;
  while (p.i < p.n && p.s[p.i] >= '0' && p.s[p.i] <= '9') {
    if (d >= 6) return E_SYNTAX;
    x = x * 10 + (p.s[p.i] - '0');
    p.i++;
    d++;
  }
  if (d == 0) return E_SYNTAX;
  if (p.i < p.n && (p.s[p.i] == '.' || p.s[p.i] == 'e' || p.s[p.i] == 'E')) return E_SYNTAX; // integers only
  v = neg ? -x : x;
  return E_OK;
}

int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

Err read_color(P &p, uint32_t &out) {
  char b[10];
  size_t l;
  Err e = read_str(p, b, sizeof b, l);
  if (e) return e;
  if ((l != 4 && l != 7) || b[0] != '#') return E_COLOR;
  uint32_t v = 0;
  if (l == 4) {
    for (int i = 1; i < 4; i++) {
      int h = hexval(b[i]);
      if (h < 0) return E_COLOR;
      v = (v << 8) | (uint32_t)(h * 17);
    }
  } else {
    for (int i = 1; i < 7; i++) {
      int h = hexval(b[i]);
      if (h < 0) return E_COLOR;
      v = (v << 4) | (uint32_t)h;
    }
  }
  out = v;
  return E_OK;
}

Err skip(P &p, uint8_t depth) {
  if (depth > MAX_DEPTH) return E_DEPTH;
  ws(p);
  if (p.i >= p.n) return E_SYNTAX;
  char c = p.s[p.i];
  if (c == '"') {
    size_t l;
    return read_str(p, nullptr, 0, l);
  }
  if (c == '{') {
    p.i++;
    if (eat(p, '}')) return E_OK;
    for (;;) {
      size_t l;
      Err e = read_str(p, nullptr, 0, l);
      if (e) return e;
      if (!eat(p, ':')) return E_SYNTAX;
      e = skip(p, depth + 1);
      if (e) return e;
      if (eat(p, ',')) continue;
      if (eat(p, '}')) return E_OK;
      return E_SYNTAX;
    }
  }
  if (c == '[') {
    p.i++;
    if (eat(p, ']')) return E_OK;
    for (;;) {
      Err e = skip(p, depth + 1);
      if (e) return e;
      if (eat(p, ',')) continue;
      if (eat(p, ']')) return E_OK;
      return E_SYNTAX;
    }
  }
  if (lit(p, "true") || lit(p, "false") || lit(p, "null")) return E_OK;
  if (c == '-' || (c >= '0' && c <= '9')) {
    size_t st = p.i;
    while (p.i < p.n && ((p.s[p.i] >= '0' && p.s[p.i] <= '9') || p.s[p.i] == '-' || p.s[p.i] == '+' || p.s[p.i] == '.' || p.s[p.i] == 'e' || p.s[p.i] == 'E')) p.i++;
    return (p.i - st > 20) ? E_SYNTAX : E_OK;
  }
  return E_SYNTAX;
}

bool key_is(const char *k, size_t kl, const char *name) { return kl == strlen(name) && memcmp(k, name, kl) == 0; }

Err int_in(P &p, int32_t lo, int32_t hi, int32_t &v) {
  Err e = read_int(p, v);
  if (e) return e;
  return (v < lo || v > hi) ? E_RANGE : E_OK;
}

WType wtype_from(const char *s, size_t l) {
  static const char *const names[] = {"", "ac", "dc", "wx", "np", "sb", "tc", "tx", "mc", "sh", "sl", "ct", "pm", "cd", "sw", "dt", "fs", "bt", "sp", "aa", "rm", "pc", "pt", "gf", "nt", "ne", "tm"};
  for (uint8_t i = 1; i < W_COUNT; i++)
    if (key_is(s, l, names[i])) return (WType)i;
  return W_NONE;
}

Err parse_widget(P &p, Layout &L) {
  if (L.nwidgets >= MAX_WIDGETS_TOTAL) return E_TOO_MANY_WIDGETS;
  if (!eat(p, '{')) return E_SYNTAX;
  Widget w;
  memset(&w, 0, sizeof w);
  w.opa = 255;
  bool hx = false, hy = false, hw = false, hh = false, hp = false, he = false, hi = false;
  char text[MAX_NOTE_LEN + 1];
  size_t tl = 0;
  bool has_text = false;
  int32_t v;
  if (!eat(p, '}')) {
    for (;;) {
      char key[8];
      size_t kl;
      Err e = read_str(p, key, sizeof key, kl);
      if (e) return e;
      if (!eat(p, ':')) return E_SYNTAX;
      if (key_is(key, kl, "t")) {
        char tb[4];
        size_t l;
        e = read_str(p, tb, sizeof tb, l);
        if (e) return e;
        w.type = wtype_from(tb, l);
        if (w.type == W_NONE) return E_WIDGET_TYPE;
      } else if (key_is(key, kl, "x")) { e = read_int(p, v); w.x = (int16_t)v; hx = true; if (!e && (v < -9999 || v > 9999)) e = E_GEOMETRY; }
      else if (key_is(key, kl, "y")) { e = read_int(p, v); w.y = (int16_t)v; hy = true; if (!e && (v < -9999 || v > 9999)) e = E_GEOMETRY; }
      else if (key_is(key, kl, "w")) { e = read_int(p, v); w.w = (int16_t)v; hw = true; if (!e && (v < -9999 || v > 9999)) e = E_GEOMETRY; }
      else if (key_is(key, kl, "h")) { e = read_int(p, v); w.h = (int16_t)v; hh = true; if (!e && (v < -9999 || v > 9999)) e = E_GEOMETRY; }
      else if (key_is(key, kl, "k")) { e = int_in(p, 0, 2, v); w.k = (uint8_t)v; }
      else if (key_is(key, kl, "z")) { e = int_in(p, 0, 1, v); w.z = (uint8_t)v; }
      else if (key_is(key, kl, "a")) { e = int_in(p, 0, 2, v); w.a = (uint8_t)v; }
      else if (key_is(key, kl, "r")) { e = int_in(p, 0, 120, v); w.r = (uint8_t)v; }
      else if (key_is(key, kl, "o")) { e = int_in(p, 0, 255, v); w.opa = (uint8_t)v; }
      else if (key_is(key, kl, "f")) { e = int_in(p, 0, 1, v); w.f24 = v != 0; }
      else if (key_is(key, kl, "q")) { e = int_in(p, 0, 1, v); w.secs = v != 0; }
      else if (key_is(key, kl, "c")) { e = read_color(p, w.c); w.has_c = true; }
      else if (key_is(key, kl, "p")) { e = int_in(p, 0, 1439, v); w.p = (uint16_t)v; hp = true; }
      else if (key_is(key, kl, "b")) { e = int_in(p, 1, 60, v); w.b = (uint8_t)v; }
      else if (key_is(key, kl, "e")) { e = int_in(p, 0, 63, v); w.e = (uint8_t)v; he = true; }
      else if (key_is(key, kl, "g")) { e = int_in(p, 0, 65535, v); w.g = (uint16_t)v; }
      else if (key_is(key, kl, "i")) { e = int_in(p, 0, PICTURE_SLOTS - 1, v); w.i = (uint8_t)v; hi = true; }
      else if (key_is(key, kl, "n")) { e = int_in(p, 0, 31, v); w.n = (uint8_t)v; }
      else if (key_is(key, kl, "u")) { e = int_in(p, 0, 4, v); w.u = (uint8_t)v; }
      else if (key_is(key, kl, "s")) {
        e = read_str(p, text, sizeof text, tl);
        if (!e && tl > MAX_NOTE_LEN) e = E_TEXT; // 24 bytes for most widgets: checked below once the type is known
        has_text = true;
      } else e = skip(p, 3);
      if (e) return e;
      if (eat(p, ',')) continue;
      if (eat(p, '}')) break;
      return E_SYNTAX;
    }
  }
  if (w.type == W_NONE) return E_WIDGET_TYPE;
  if (!(hx && hy && hw && hh)) return E_GEOMETRY;
  if (w.x < 0 || w.y < 0 || w.w < 1 || w.h < 1 || w.x + w.w > SCREEN_W || w.y + w.h > SCREEN_H) return E_GEOMETRY;
  if ((w.type == W_SLIDER && w.k > 1) || (w.type == W_SHAPE && w.k > 1)) return E_RANGE; // wx, dt, rm use k 0..2
  switch (w.type) { // per-type rules for the feature level 2 widgets
    case W_TCAT: if (w.w != 240 || w.h != 132) return E_GEOMETRY; break; // drawn at its fixed size
    case W_ALBUM: if (w.w != 96 || w.h != 96) return E_GEOMETRY; break;
    case W_PET: if (w.w != 160 || w.h != 132) return E_GEOMETRY; break;
    case W_POMO:
      if (!hp) w.p = 25;
      if (!w.b) w.b = 5;
      if (w.p < 1 || w.p > 180) return E_RANGE;
      break;
    case W_COUNTDOWN: if (!hp) return E_RANGE; break;
    case W_REMIND:
      if (!hp) w.p = 30;
      if (w.p < 5 || w.p > 240) return E_RANGE;
      break;
    case W_BUTTON:
      if (!he || w.e >= BA_COUNT) return E_RANGE;
      if (!has_text || tl == 0) return E_TEXT;
      break;
    case W_PICTURE: if (!hi) return E_RANGE; break;
    case W_GIF: if (!hi || w.i >= GIF_SLOTS) return E_RANGE; break;
    case W_NOTE: case W_EVENTS: if (!has_text || tl == 0) return E_TEXT; break;
    case W_TIMER: if (!hp || w.p < 1 || w.p > 180) return E_RANGE; break;
    default: break;
  }
  bool long_text = (w.type == W_NOTE || w.type == W_EVENTS);
  if (has_text && tl > (long_text ? MAX_NOTE_LEN : MAX_TEXT_LEN)) return E_TEXT;
  bool keeps_text = (w.type == W_LABEL || w.type == W_BUTTON || w.type == W_FOCUS || w.type == W_COUNTDOWN || long_text || w.type == W_TIMER);
  if (w.type == W_LABEL && (!has_text || tl == 0)) return E_TEXT;
  if (keeps_text && has_text && tl > 0) {
    if (L.text_used + tl + 1 > MAX_TEXT_POOL) return E_TEXT_POOL;
    memcpy(L.text + L.text_used, text, tl);
    L.text[L.text_used + tl] = '\0';
    w.toff = L.text_used;
    w.tlen = (uint8_t)tl;
    L.text_used = (uint16_t)(L.text_used + tl + 1);
  }
  L.widgets[L.nwidgets++] = w;
  return E_OK;
}

Err parse_bg(P &p, Page &pg) {
  if (!eat(p, '{')) return E_SYNTAX;
  char t = 'w';
  int32_t idx = -1, v;
  bool hc = false, hd = false;
  if (!eat(p, '}')) {
    for (;;) {
      char key[4];
      size_t kl;
      Err e = read_str(p, key, sizeof key, kl);
      if (e) return e;
      if (!eat(p, ':')) return E_SYNTAX;
      if (key_is(key, kl, "t")) {
        char tb[4];
        size_t l;
        e = read_str(p, tb, sizeof tb, l);
        if (e) return e;
        if (l != 1 || (tb[0] != 'w' && tb[0] != 'b' && tb[0] != 's' && tb[0] != 'g' && tb[0] != 'i')) return E_PAGE;
        t = tb[0];
      } else if (key_is(key, kl, "i")) { e = int_in(p, 0, PICTURE_SLOTS - 1, v); idx = v; }
      else if (key_is(key, kl, "c")) { e = read_color(p, pg.c1); hc = true; }
      else if (key_is(key, kl, "d")) { e = read_color(p, pg.c2); hd = true; }
      else e = skip(p, 4);
      if (e) return e;
      if (eat(p, ',')) continue;
      if (eat(p, '}')) break;
      return E_SYNTAX;
    }
  }
  switch (t) {
    case 'w': pg.bg_type = BG_WEATHER; break;
    case 'b':
      if (idx < 0 || idx > 5) return E_RANGE;
      pg.bg_type = BG_BUILTIN;
      pg.bg_idx = (uint8_t)idx;
      break;
    case 's':
      if (!hc) return E_COLOR;
      pg.bg_type = BG_SOLID;
      break;
    case 'g':
      if (!hc || !hd) return E_COLOR;
      pg.bg_type = BG_GRADIENT;
      break;
    case 'i':
      if (idx < 0) return E_RANGE;
      pg.bg_type = BG_IMAGE;
      pg.bg_idx = (uint8_t)idx;
      break;
  }
  return E_OK;
}

Err parse_page(P &p, Layout &L) {
  if (L.npages >= MAX_PAGES) return E_TOO_MANY_PAGES;
  if (!eat(p, '{')) return E_SYNTAX;
  Page pg;
  memset(&pg, 0, sizeof pg);
  pg.bg_type = BG_WEATHER;
  pg.first = L.nwidgets;
  bool has_bg = false, has_w = false;
  int32_t v;
  if (!eat(p, '}')) {
    for (;;) {
      char key[4];
      size_t kl;
      Err e = read_str(p, key, sizeof key, kl);
      if (e) return e;
      if (!eat(p, ':')) return E_SYNTAX;
      if (key_is(key, kl, "m")) {
        e = int_in(p, 0, 1, v);
        pg.is_main = (v == 1);
      } else if (key_is(key, kl, "bg")) {
        e = parse_bg(p, pg);
        has_bg = true;
      } else if (key_is(key, kl, "w")) {
        has_w = true;
        if (!eat(p, '[')) return E_SYNTAX;
        if (!eat(p, ']')) {
          for (;;) {
            if ((uint8_t)(L.nwidgets - pg.first) >= MAX_WIDGETS_PAGE) return E_TOO_MANY_WIDGETS;
            e = parse_widget(p, L);
            if (e) return e;
            if (eat(p, ',')) continue;
            if (eat(p, ']')) break;
            return E_SYNTAX;
          }
        }
      } else e = skip(p, 3);
      if (e) return e;
      if (eat(p, ',')) continue;
      if (eat(p, '}')) break;
      return E_SYNTAX;
    }
  }
  if (pg.is_main) {
    if (has_bg || (has_w && L.nwidgets != pg.first)) return E_PAGE;
    for (uint8_t i = 0; i < L.npages; i++)
      if (L.pages[i].is_main) return E_MAIN_DUP;
  }
  pg.nw = (uint8_t)(L.nwidgets - pg.first);
  L.pages[L.npages++] = pg;
  return E_OK;
}

} // namespace

Err parse(const char *json, size_t len, Layout &out) {
  if (json == nullptr || len == 0) return E_EMPTY;
  if (len > MAX_LAYOUT_BYTES) return E_TOO_BIG;
  memset(&out, 0, sizeof out);
  P p{json, len, 0};
  if (!eat(p, '{')) return E_SYNTAX;
  bool hv = false, hp = false;
  int32_t v;
  if (!eat(p, '}')) {
    for (;;) {
      char key[4];
      size_t kl;
      Err e = read_str(p, key, sizeof key, kl);
      if (e) return e;
      if (!eat(p, ':')) return E_SYNTAX;
      if (key_is(key, kl, "v")) {
        e = read_int(p, v);
        if (!e && v != SCHEMA_VERSION) e = E_VERSION;
        out.version = SCHEMA_VERSION;
        hv = true;
      } else if (key_is(key, kl, "rot")) {
        e = read_int(p, v);
        if (!e && v != 0 && (v < 3 || v > 600)) e = E_RANGE;
        out.rotate_s = (uint16_t)v;
      } else if (key_is(key, kl, "pg")) {
        hp = true;
        if (!eat(p, '[')) return E_SYNTAX;
        if (!eat(p, ']')) {
          for (;;) {
            e = parse_page(p, out);
            if (e) return e;
            if (eat(p, ',')) continue;
            if (eat(p, ']')) break;
            return E_SYNTAX;
          }
        }
      } else e = skip(p, 2);
      if (e) return e;
      if (eat(p, ',')) continue;
      if (eat(p, '}')) break;
      return E_SYNTAX;
    }
  }
  ws(p);
  if (p.i != p.n) return E_TRAILING;
  if (!hv) return E_VERSION;
  if (!hp || out.npages == 0) return E_NO_PAGES;
  return E_OK;
}

// ------------------------------------------------------------------------------------------------
// Upload session
// ------------------------------------------------------------------------------------------------
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

// Canonical 44-byte-header WAV only (what the page writes and what WAVFileReader reads): anything else is refused before it is kept.
bool wav_check(const uint8_t *h, size_t n, uint32_t total, uint16_t *rate_out) {
  if (!h || n < WAV_HEADER_BYTES || total < WAV_HEADER_BYTES + 2 || total > MAX_SOUND_BYTES) return false;
  if (memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4) || memcmp(h + 12, "fmt ", 4) || memcmp(h + 36, "data", 4)) return false;
  if (le32(h + 4) != total - 8) return false; // RIFF size covers the rest of the file
  if (le32(h + 16) != 16 || le16(h + 20) != 1 || le16(h + 22) != 1) return false; // plain PCM, mono
  uint32_t rate = le32(h + 24);
  if (rate < SOUND_RATE_MIN || rate > SOUND_RATE_MAX) return false;
  if (le32(h + 28) != rate * 2 || le16(h + 32) != 2 || le16(h + 34) != 16) return false; // 16-bit
  uint32_t data = le32(h + 40);
  if (data != total - WAV_HEADER_BYTES || (data & 1)) return false; // samples run to the end of the file, whole samples
  if (rate_out) *rate_out = (uint16_t)rate;
  return true;
}

bool next_event(const char *text, uint16_t now_min, char *out, size_t out_n, uint16_t *in_min) {
  if (!text || !out || out_n < 8) return false;
  int best = -1;
  const char *best_title = NULL;
  size_t best_len = 0;
  const char *p = text;
  while (*p) {
    const char *end = p;
    while (*end && *end != '|') end++;
    const char *q = p;
    while (q < end && *q == ' ') q++;
    int hh = -1, mm = -1;
    if (q < end && *q >= '0' && *q <= '9') {
      hh = *q++ - '0';
      if (q < end && *q >= '0' && *q <= '9') hh = hh * 10 + (*q++ - '0');
      if (q + 2 < end + 0 && *q == ':' && q[1] >= '0' && q[1] <= '9' && q[2] >= '0' && q[2] <= '9') {
        mm = (q[1] - '0') * 10 + (q[2] - '0');
        q += 3;
      } else hh = -1;
    }
    if (hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59 && q < end && *q == ' ') {
      while (q < end && *q == ' ') q++;
      const char *te = end;
      while (te > q && te[-1] == ' ') te--;
      int t = hh * 60 + mm;
      if (te > q && t >= now_min && (best < 0 || t < best)) {
        best = t;
        best_title = q;
        best_len = (size_t)(te - q);
      }
    }
    p = *end ? end + 1 : end;
  }
  if (best < 0) return false;
  if (best_len > 40) best_len = 40;
  if (out_n < best_len + 7) best_len = out_n - 7;
  out[0] = (char)('0' + best / 600);
  out[1] = (char)('0' + (best / 60) % 10);
  out[2] = ':';
  out[3] = (char)('0' + (best % 60) / 10);
  out[4] = (char)('0' + best % 10);
  out[5] = ' ';
  memcpy(out + 6, best_title, best_len);
  out[6 + best_len] = '\0';
  if (in_min) *in_min = (uint16_t)(best - now_min);
  return true;
}

bool gif_check(const uint8_t *h, size_t n, uint32_t total, const uint8_t *tail2, uint16_t *w_out, uint16_t *h_out) {
  if (!h || n < 13 || total < 14 || total > MAX_GIF_BYTES) return false;
  if (memcmp(h, "GIF87a", 6) && memcmp(h, "GIF89a", 6)) return false;
  uint16_t w = le16(h + 6), hh = le16(h + 8);
  if (w < 1 || w > GIF_MAX_SIDE || hh < 1 || hh > GIF_MAX_SIDE) return false;
  if (!tail2 || tail2[1] != 0x3B) return false; // the file must end with the GIF trailer
  if (w_out) *w_out = w;
  if (h_out) *h_out = hh;
  return true;
}

size_t ack_encode(const Ack &a, uint8_t *o) {
  o[0] = 0xFF;
  o[1] = PKT_ACK;
  o[2] = 12;
  o[3] = a.cmd;
  o[4] = a.status;
  o[5] = a.detail;
  o[6] = a.flags;
  o[7] = (uint8_t)(a.seq & 0xFF);
  o[8] = (uint8_t)(a.seq >> 8);
  o[9] = (uint8_t)(a.free_kb & 0xFF);
  o[10] = (uint8_t)(a.free_kb >> 8);
  o[11] = (uint8_t)(a.crc & 0xFF);
  o[12] = (uint8_t)((a.crc >> 8) & 0xFF);
  o[13] = (uint8_t)((a.crc >> 16) & 0xFF);
  o[14] = (uint8_t)(a.crc >> 24);
  return ACK_FRAME_BYTES;
}

Ack Session::make(uint8_t cmd, uint8_t status, uint8_t detail, bool own_crc, uint32_t crc) {
  Ack a;
  a.cmd = cmd;
  a.status = status;
  a.detail = detail;
  a.flags = (uint8_t)(st.state_flags() | (is_open ? FL_SESSION_OPEN : 0));
  a.seq = next_seq;
  // free space costs a walk over the whole LittleFS partition (~0.1 s): chunk acks reuse the value from the last other ack
  if (cmd != PKT_CHUNK || !free_known) {
    free_cache = st.free_bytes();
    free_known = true;
  }
  size_t kb = free_cache / 1024;
  a.free_kb = (uint16_t)(kb > 65535 ? 65535 : kb);
  a.crc = own_crc ? crc : st.stored_crc();
  return a;
}

void Session::drop() {
  if (is_open) st.tmp_discard();
  is_open = false;
  received = 0;
  next_seq = 0;
  run_crc = 0;
}

Ack Session::handle(uint8_t id, const uint8_t *p, uint8_t n) {
  switch (id) {
    case PKT_BEGIN: return do_begin(p, n);
    case PKT_CHUNK: return do_chunk(p, n);
    case PKT_END: return n == 0 ? do_end() : make(id, ST_BAD_PKT);
    case PKT_ABORT: return n == 0 ? do_abort() : make(id, ST_BAD_PKT);
    case PKT_QUERY: // detail = feature level; with a slot byte the crc is that picture's; [3, slot]: that custom sound (detail = KB)
      if (n == 1 && p[0] < PICTURE_SLOTS) return make(id, ST_OK, FEATURE_LEVEL, true, st.picture_crc(p[0]));
      if (n == 2 && p[0] == KIND_PICTURE && p[1] < PICTURE_SLOTS) return make(id, ST_OK, FEATURE_LEVEL, true, st.picture_crc(p[1]));
      if (n == 2 && p[0] == KIND_GIF && p[1] < GIF_SLOTS) {
        uint32_t kb = (st.gif_size(p[1]) + 1023) / 1024;
        return make(id, ST_OK, (uint8_t)(kb > 255 ? 255 : kb), true, st.gif_crc(p[1]));
      }
      if (n == 2 && p[0] == KIND_SOUND && p[1] < SOUND_SLOTS) {
        uint32_t kb = (st.sound_size(p[1]) + 1023) / 1024;
        return make(id, ST_OK, (uint8_t)(kb > 255 ? 255 : kb), true, st.sound_crc(p[1]));
      }
      return n == 0 ? make(id, ST_OK, FEATURE_LEVEL) : make(id, ST_BAD_PKT);
    case PKT_PLAY: // play one sound slot on the board (custom copy, else the original, else its beep)
      if (n != 1 || p[0] >= SOUND_SLOTS) return make(id, ST_BAD_PKT);
      return st.sound_play(p[0]) ? make(id, ST_OK) : make(id, ST_UNSUPPORTED);
    case PKT_RESET:
      if (n == 2 && p[0] == KIND_GIF && p[1] < GIF_SLOTS) { // delete one GIF
        if (is_open && kind == KIND_GIF && slot == p[1]) drop();
        if (!st.gif_remove(p[1])) return make(id, ST_IO);
        st.set_gif_crc(p[1], 0);
        pics_changed = true;
        return make(id, ST_OK, 0, true, 0);
      }
      if (n == 2 && p[0] == KIND_SOUND && p[1] < SOUND_SLOTS) { // delete the custom copy of one sound: the original file is never touched
        if (is_open && kind == KIND_SOUND && slot == p[1]) drop();
        if (!st.sound_remove(p[1])) return make(id, ST_IO);
        st.set_sound_crc(p[1], 0);
        return make(id, ST_OK, 0, true, 0);
      }
      if (n == 1 && p[0] < PICTURE_SLOTS) { // delete one picture
        if (is_open && kind == KIND_PICTURE && slot == p[0]) drop();
        if (!st.picture_remove(p[0])) return make(id, ST_IO);
        st.set_picture_crc(p[0], 0);
        pics_changed = true;
        return make(id, ST_OK, 0, true, 0);
      }
      if (n != 0) return make(id, ST_BAD_PKT);
      drop();
      if (!st.final_remove()) return make(id, ST_IO);
      st.set_stored_crc(0);
      changed = true;
      return make(id, ST_OK);
    default: return make(id, ST_BAD_PKT);
  }
}

Ack Session::do_begin(const uint8_t *p, uint8_t n) {
  if (n != 11) return make(PKT_BEGIN, ST_BAD_PKT);
  uint8_t k = p[0], sl = p[1], v = p[2];
  uint32_t tot = le32(p + 3), crc = le32(p + 7);
  if (k > KIND_GIF) return make(PKT_BEGIN, ST_BAD_PKT);
  if (k == KIND_GIF && (sl >= GIF_SLOTS || tot < 14)) return make(PKT_BEGIN, ST_BAD_PKT);
  if (k == KIND_PICTURE && sl >= PICTURE_SLOTS) return make(PKT_BEGIN, ST_BAD_PKT);
  if (k == KIND_SOUND && (sl >= SOUND_SLOTS || tot < WAV_HEADER_BYTES + 2)) return make(PKT_BEGIN, ST_BAD_PKT);
  if (v != SCHEMA_VERSION) return make(PKT_BEGIN, ST_INVALID, E_VERSION);
  if (tot == 0) return make(PKT_BEGIN, ST_BAD_PKT);
  uint32_t cap = k == KIND_LAYOUT ? MAX_LAYOUT_BYTES : k == KIND_PICTURE ? MAX_PICTURE_BYTES : k == KIND_SOUND ? MAX_SOUND_BYTES : k == KIND_GIF ? MAX_GIF_BYTES : MAX_ART_BYTES;
  if (tot > cap) return make(PKT_BEGIN, ST_TOO_BIG, k == KIND_LAYOUT ? E_TOO_BIG : 0);
  if (is_open && kind == k && slot == sl && ver == v && total == tot && want_crc == crc) return make(PKT_BEGIN, ST_OK); // resume: ack carries next_seq
  drop();
  // album art lives in RAM. A replaced picture keeps its old file until the new one is complete, so both must fit.
  if (k != KIND_ART && st.free_bytes() < (size_t)tot + 8192) return make(PKT_BEGIN, ST_NO_SPACE); // keep 8 KB for LittleFS itself
  if (!st.tmp_open(k, sl)) return make(PKT_BEGIN, ST_IO);
  is_open = true;
  kind = k;
  slot = sl;
  ver = v;
  total = tot;
  want_crc = crc;
  received = 0;
  run_crc = 0;
  next_seq = 0;
  return make(PKT_BEGIN, ST_OK);
}

Ack Session::do_chunk(const uint8_t *p, uint8_t n) {
  if (!is_open) return make(PKT_CHUNK, ST_NO_SESSION);
  if (n < 3) return make(PKT_CHUNK, ST_BAD_PKT);
  uint16_t seq = (uint16_t)(p[0] | (p[1] << 8));
  const uint8_t *d = p + 2;
  size_t dn = (size_t)n - 2;
  if (dn > CHUNK_BYTES) return make(PKT_CHUNK, ST_BAD_PKT);
  if (next_seq > 0 && seq == (uint16_t)(next_seq - 1)) return make(PKT_CHUNK, ST_OK); // duplicate of the last chunk (lost ack): already stored
  if (seq != next_seq) return make(PKT_CHUNK, ST_BAD_SEQ);
  if (received + dn > total) {
    drop();
    return make(PKT_CHUNK, ST_TOO_BIG);
  }
  if (received + dn < total && dn != CHUNK_BYTES) return make(PKT_CHUNK, ST_BAD_PKT);
  if (!st.tmp_write(d, dn)) {
    drop();
    return make(PKT_CHUNK, ST_IO);
  }
  run_crc = crc32_update(run_crc, d, dn);
  received += (uint32_t)dn;
  next_seq++;
  return make(PKT_CHUNK, ST_OK);
}

Ack Session::do_end() {
  if (!is_open) return make(PKT_END, ST_NO_SESSION);
  if (received != total) return make(PKT_END, ST_INCOMPLETE); // session stays open, host may send the missing chunks
  if (!st.tmp_close()) {
    drop();
    return make(PKT_END, ST_IO);
  }
  if (run_crc != want_crc) {
    drop();
    return make(PKT_END, ST_CRC);
  }
  if (kind != KIND_LAYOUT) return end_picture();
  uint8_t *buf = new (std::nothrow) uint8_t[total + 1];
  if (!buf) {
    drop();
    return make(PKT_END, ST_IO);
  }
  size_t got = 0;
  bool ok = st.tmp_read_all(buf, (size_t)total + 1, got);
  if (!ok || got != total) {
    delete[] buf;
    drop();
    return make(PKT_END, ST_IO);
  }
  if (crc32(buf, got) != want_crc) { // what is in flash is not what was received
    delete[] buf;
    drop();
    return make(PKT_END, ST_CRC);
  }
  Layout *L = new (std::nothrow) Layout;
  if (!L) {
    delete[] buf;
    drop();
    return make(PKT_END, ST_IO);
  }
  Err e = parse((const char *)buf, got, *L);
  delete L;
  delete[] buf;
  if (e != E_OK) {
    drop();
    return make(PKT_END, ST_INVALID, (uint8_t)e);
  }
  if (!st.tmp_commit(KIND_LAYOUT, 0)) {
    drop();
    return make(PKT_END, ST_IO);
  }
  st.set_stored_crc(want_crc);
  is_open = false;
  received = 0;
  next_seq = 0;
  changed = true;
  return make(PKT_END, ST_OK);
}

// Pictures are too big to read back into RAM: stream them back for the crc and check the format's markers.
Ack Session::end_picture() {
  uint8_t head[WAV_HEADER_BYTES] = {0}, tail[2] = {0};
  uint32_t crc = 0;
  size_t n = 0;
  if (!st.tmp_scan(crc, n, head, sizeof head, tail) || n != total) {
    drop();
    return make(PKT_END, ST_IO);
  }
  if (crc != want_crc) {
    drop();
    return make(PKT_END, ST_CRC);
  }
  bool ok;
  if (kind == KIND_SOUND) ok = wav_check(head, n < sizeof head ? n : sizeof head, (uint32_t)n);
  else if (kind == KIND_GIF) ok = gif_check(head, n < sizeof head ? n : sizeof head, (uint32_t)n, tail);
  else if (kind == KIND_PICTURE) { // LVGL SJPG: "_SJPG__\0V1.00\0", width u16, height u16, splits u16, split height u16
    uint16_t w = (uint16_t)(head[14] | (head[15] << 8)), h = (uint16_t)(head[16] | (head[17] << 8)), parts = (uint16_t)(head[18] | (head[19] << 8));
    ok = n > 22 && memcmp(head, "_SJPG__", 7) == 0 && w >= 1 && w <= SCREEN_W && h >= 1 && h <= SCREEN_H && parts >= 1;
  } else { // plain baseline JPEG
    ok = n > 4 && head[0] == 0xFF && head[1] == 0xD8 && tail[0] == 0xFF && tail[1] == 0xD9;
  }
  if (!ok) {
    drop();
    return make(PKT_END, ST_INVALID);
  }
  if (!st.tmp_commit(kind, slot)) {
    drop();
    return make(PKT_END, ST_IO);
  }
  if (kind == KIND_SOUND) { // a sound has no decoder test: the header check above is the test
    st.set_sound_crc(slot, want_crc);
    is_open = false;
    received = 0;
    next_seq = 0;
    return make(PKT_END, ST_OK, 0, true, want_crc);
  }
  if (kind == KIND_GIF) { // stored; the board's decoder must be able to open it and decode a frame
    bool good = st.picture_check(kind, slot);
    if (!good) {
      st.gif_remove(slot);
      st.set_gif_crc(slot, 0);
    } else {
      st.set_gif_crc(slot, want_crc);
    }
    is_open = false;
    received = 0;
    next_seq = 0;
    pics_changed = true;
    return good ? make(PKT_END, ST_OK, 0, true, want_crc) : make(PKT_END, ST_INVALID, 1);
  }
  if (!st.picture_check(kind, slot)) { // stored, but the board cannot decode it: remove it again
    if (kind == KIND_PICTURE) {
      st.picture_remove(slot);
      st.set_picture_crc(slot, 0);
    }
    is_open = false;
    received = 0;
    next_seq = 0;
    pics_changed = true;
    return make(PKT_END, ST_INVALID, 1);
  }
  if (kind == KIND_PICTURE) st.set_picture_crc(slot, want_crc);
  is_open = false;
  received = 0;
  next_seq = 0;
  pics_changed = true;
  return make(PKT_END, ST_OK, 0, true, want_crc);
}

Ack Session::do_abort() {
  drop();
  return make(PKT_ABORT, ST_OK);
}

} // namespace lc
