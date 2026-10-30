// Host unit tests for sketch/src/layout_core.cpp. Run: scripts/test-layout-core.sh
#include "../../src/layout_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fstream>
#include <string>
#include <vector>

using namespace lc;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                                          \
  do {                                                                                       \
    if (cond) g_pass++;                                                                      \
    else {                                                                                   \
      g_fail++;                                                                              \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                 \
    }                                                                                        \
  } while (0)

static Err P(const std::string &s) {
  Layout *L = new Layout;
  Err e = parse(s.data(), s.size(), *L);
  delete L;
  return e;
}

static const char *GOOD =
  "{\"v\":1,\"rot\":10,\"pg\":["
  "{\"m\":1},"
  "{\"bg\":{\"t\":\"g\",\"c\":\"#102030\",\"d\":\"#3050a0\"},\"w\":["
  "{\"t\":\"dc\",\"x\":10,\"y\":10,\"w\":120,\"h\":30,\"f\":1,\"q\":1,\"z\":1,\"c\":\"#fff\"},"
  "{\"t\":\"wx\",\"x\":150,\"y\":10,\"w\":80,\"h\":90,\"k\":2},"
  "{\"t\":\"tx\",\"x\":10,\"y\":60,\"w\":130,\"h\":20,\"s\":\"Hello \\\"cat\\\"\",\"a\":1},"
  "{\"t\":\"sl\",\"x\":10,\"y\":290,\"w\":200,\"h\":20,\"k\":1},"
  "{\"t\":\"sh\",\"x\":0,\"y\":100,\"w\":240,\"h\":2,\"c\":\"#FBC979\",\"o\":128},"
  "{\"t\":\"ac\",\"x\":10,\"y\":110,\"w\":90,\"h\":90},"
  "{\"t\":\"np\",\"x\":10,\"y\":210,\"w\":220,\"h\":60},"
  "{\"t\":\"sb\",\"x\":110,\"y\":110,\"w\":120,\"h\":50},"
  "{\"t\":\"tc\",\"x\":10,\"y\":40,\"w\":100,\"h\":20},"
  "{\"t\":\"mc\",\"x\":20,\"y\":150,\"w\":200,\"h\":132}"
  "]},"
  "{\"bg\":{\"t\":\"b\",\"i\":3},\"w\":[]},"
  "{\"bg\":{\"t\":\"s\",\"c\":\"#000\"}}"
  "]}";

static void test_crc() {
  const char *s = "123456789";
  CHECK(crc32((const uint8_t *)s, 9) == 0xCBF43926u);
  uint32_t a = crc32_update(0, (const uint8_t *)s, 4);
  CHECK(crc32_update(a, (const uint8_t *)s + 4, 5) == 0xCBF43926u);
  CHECK(crc32(nullptr, 0) == 0);
}

static void test_parse_good() {
  static Layout LL; // static: avoids a leak report, Layout is large for the stack
  Layout *L = &LL;
  Err e = parse(GOOD, strlen(GOOD), *L);
  CHECK(e == E_OK);
  CHECK(L->npages == 4);
  CHECK(L->rotate_s == 10);
  CHECK(L->pages[0].is_main);
  CHECK(L->pages[1].bg_type == BG_GRADIENT && L->pages[1].c1 == 0x102030 && L->pages[1].c2 == 0x3050a0);
  CHECK(L->pages[1].nw == 10);
  CHECK(L->pages[2].bg_type == BG_BUILTIN && L->pages[2].bg_idx == 3 && L->pages[2].nw == 0);
  CHECK(L->pages[3].bg_type == BG_SOLID && L->pages[3].c1 == 0);
  const Widget &w0 = L->widgets[L->pages[1].first];
  CHECK(w0.type == W_DIGITAL && w0.f24 && w0.secs && w0.z == 1 && w0.has_c && w0.c == 0xFFFFFF);
  const Widget &w2 = L->widgets[L->pages[1].first + 2];
  CHECK(w2.type == W_LABEL && w2.tlen == 11 && strcmp(L->text + w2.toff, "Hello \"cat\"") == 0 && w2.a == 1);
  CHECK(L->widgets[L->pages[1].first + 4].opa == 128);
  CHECK(L->widgets[L->pages[1].first + 3].type == W_SLIDER && L->widgets[L->pages[1].first + 3].k == 1);
  // page with no bg key defaults to weather-driven
  CHECK(P("{\"v\":1,\"pg\":[{}]}") == E_OK);
  // whitespace and unknown keys (forward compatible)
  CHECK(P(" {\n \"v\" : 1 , \"future\": {\"a\":[1,2,{\"b\":null}]}, \"pg\":[ {\"zzz\":true} ] } \n") == E_OK);
  CHECK(P("{\"v\":1,\"pg\":[{\"w\":[{\"t\":\"sh\",\"x\":0,\"y\":0,\"w\":1,\"h\":1,\"extra\":[1.5e3]}]}]}") == E_OK);
}

static void test_parse_bad() {
  { Layout *L = new Layout; CHECK(parse(nullptr, 0, *L) == E_EMPTY); delete L; }
  CHECK(P("") == E_EMPTY);
  CHECK(P(std::string(MAX_LAYOUT_BYTES + 1, ' ')) == E_TOO_BIG);
  CHECK(P("not json") == E_SYNTAX);
  CHECK(P("[]") == E_SYNTAX);
  CHECK(P("{}") == E_VERSION);
  CHECK(P("{\"v\":2,\"pg\":[{}]}") == E_VERSION);
  CHECK(P("{\"v\":0,\"pg\":[{}]}") == E_VERSION);
  CHECK(P("{\"v\":1}") == E_NO_PAGES);
  CHECK(P("{\"v\":1,\"pg\":[]}") == E_NO_PAGES);
  CHECK(P("{\"v\":1,\"pg\":[{}]} x") == E_TRAILING);
  CHECK(P("{\"v\":1,\"pg\":[{}]}{") == E_TRAILING);
  CHECK(P("{\"v\":1,\"pg\":[{}],}") == E_SYNTAX);
  CHECK(P("{\"v\":1.0,\"pg\":[{}]}") == E_SYNTAX);
  CHECK(P("{\"v\":1,\"rot\":1,\"pg\":[{}]}") == E_RANGE);
  CHECK(P("{\"v\":1,\"rot\":601,\"pg\":[{}]}") == E_RANGE);
  CHECK(P("{\"v\":1,\"rot\":-5,\"pg\":[{}]}") == E_RANGE);
  // too many pages
  std::string pg = "{\"v\":1,\"pg\":[";
  for (int i = 0; i < 7; i++) pg += std::string(i ? "," : "") + "{}";
  CHECK(P(pg + "]}") == E_TOO_MANY_PAGES);
  std::string pg6 = "{\"v\":1,\"pg\":[";
  for (int i = 0; i < 6; i++) pg6 += std::string(i ? "," : "") + "{}";
  CHECK(P(pg6 + "]}") == E_OK);
  // main page rules
  CHECK(P("{\"v\":1,\"pg\":[{\"m\":1},{\"m\":1}]}") == E_MAIN_DUP);
  CHECK(P("{\"v\":1,\"pg\":[{\"m\":1,\"bg\":{\"t\":\"w\"}}]}") == E_PAGE);
  CHECK(P("{\"v\":1,\"pg\":[{\"m\":1,\"w\":[{\"t\":\"tc\",\"x\":0,\"y\":0,\"w\":10,\"h\":10}]}]}") == E_PAGE);
  CHECK(P("{\"v\":1,\"pg\":[{\"m\":2}]}") == E_RANGE);
  // background
  CHECK(P("{\"v\":1,\"pg\":[{\"bg\":{\"t\":\"x\"}}]}") == E_PAGE);
  CHECK(P("{\"v\":1,\"pg\":[{\"bg\":{\"t\":\"b\"}}]}") == E_RANGE);
  CHECK(P("{\"v\":1,\"pg\":[{\"bg\":{\"t\":\"b\",\"i\":6}}]}") == E_RANGE);
  CHECK(P("{\"v\":1,\"pg\":[{\"bg\":{\"t\":\"s\"}}]}") == E_COLOR);
  CHECK(P("{\"v\":1,\"pg\":[{\"bg\":{\"t\":\"g\",\"c\":\"#000\"}}]}") == E_COLOR);
  CHECK(P("{\"v\":1,\"pg\":[{\"bg\":{\"t\":\"s\",\"c\":\"red\"}}]}") == E_COLOR);
  CHECK(P("{\"v\":1,\"pg\":[{\"bg\":{\"t\":\"s\",\"c\":\"#12345\"}}]}") == E_COLOR);
  CHECK(P("{\"v\":1,\"pg\":[{\"bg\":{\"t\":\"s\",\"c\":\"#12345g\"}}]}") == E_COLOR);
  // widgets
  const char *pre = "{\"v\":1,\"pg\":[{\"w\":[";
  const char *post = "]}]}";
  auto W = [&](const char *w) { return P(std::string(pre) + w + post); };
  CHECK(W("{\"t\":\"zz\",\"x\":0,\"y\":0,\"w\":10,\"h\":10}") == E_WIDGET_TYPE);
  CHECK(W("{\"x\":0,\"y\":0,\"w\":10,\"h\":10}") == E_WIDGET_TYPE);
  CHECK(W("{\"t\":\"tc\",\"y\":0,\"w\":10,\"h\":10}") == E_GEOMETRY);
  CHECK(W("{\"t\":\"tc\",\"x\":-1,\"y\":0,\"w\":10,\"h\":10}") == E_GEOMETRY);
  CHECK(W("{\"t\":\"tc\",\"x\":0,\"y\":0,\"w\":0,\"h\":10}") == E_GEOMETRY);
  CHECK(W("{\"t\":\"tc\",\"x\":231,\"y\":0,\"w\":10,\"h\":10}") == E_GEOMETRY);
  CHECK(W("{\"t\":\"tc\",\"x\":230,\"y\":310,\"w\":10,\"h\":10}") == E_OK);
  CHECK(W("{\"t\":\"tc\",\"x\":0,\"y\":311,\"w\":10,\"h\":10}") == E_GEOMETRY);
  CHECK(W("{\"t\":\"tc\",\"x\":99999,\"y\":0,\"w\":10,\"h\":10}") == E_GEOMETRY);
  CHECK(W("{\"t\":\"tc\",\"x\":1234567,\"y\":0,\"w\":10,\"h\":10}") == E_SYNTAX);
  CHECK(W("{\"t\":\"tc\",\"x\":10000,\"y\":0,\"w\":10,\"h\":10}") == E_GEOMETRY);
  CHECK(W("{\"t\":\"sl\",\"x\":0,\"y\":0,\"w\":10,\"h\":10,\"k\":2}") == E_RANGE);
  CHECK(W("{\"t\":\"sh\",\"x\":0,\"y\":0,\"w\":10,\"h\":10,\"k\":2}") == E_RANGE);
  CHECK(W("{\"t\":\"wx\",\"x\":0,\"y\":0,\"w\":10,\"h\":10,\"k\":3}") == E_RANGE);
  CHECK(W("{\"t\":\"tc\",\"x\":0,\"y\":0,\"w\":10,\"h\":10,\"z\":2}") == E_RANGE);
  CHECK(W("{\"t\":\"tc\",\"x\":0,\"y\":0,\"w\":10,\"h\":10,\"o\":256}") == E_RANGE);
  CHECK(W("{\"t\":\"tc\",\"x\":0,\"y\":0,\"w\":10,\"h\":10,\"r\":121}") == E_RANGE);
  CHECK(W("{\"t\":\"tx\",\"x\":0,\"y\":0,\"w\":10,\"h\":10}") == E_TEXT);
  CHECK(W("{\"t\":\"tx\",\"x\":0,\"y\":0,\"w\":10,\"h\":10,\"s\":\"\"}") == E_TEXT);
  CHECK(W("{\"t\":\"tx\",\"x\":0,\"y\":0,\"w\":10,\"h\":10,\"s\":\"abcdefghijklmnopqrstuvwxy\"}") == E_TEXT); // 25 chars
  CHECK(W("{\"t\":\"tx\",\"x\":0,\"y\":0,\"w\":10,\"h\":10,\"s\":\"abcdefghijklmnopqrstuvwx\"}") == E_OK); // 24 chars
  CHECK(W("{\"t\":\"tx\",\"x\":0,\"y\":0,\"w\":10,\"h\":10,\"s\":\"a\\u0041\"}") == E_SYNTAX); // \u not supported
  CHECK(W("{\"t\":\"tx\",\"x\":0,\"y\":0,\"w\":10,\"h\":10,\"s\":\"a\nb\"}") == E_SYNTAX); // raw newline in string
  CHECK(W("{\"t\":\"tx\",\"x\":0,\"y\":0,\"w\":10,\"h\":10,\"s\":\"abc}") == E_SYNTAX); // unterminated
  CHECK(W("{\"t\":\"tc\",\"x\":0,\"y\":0,\"w\":10,\"h\":10,\"c\":5}") == E_SYNTAX);
  // widget counts
  std::string many = "{\"v\":1,\"pg\":[{\"w\":[";
  for (int i = 0; i < 13; i++) many += std::string(i ? "," : "") + "{\"t\":\"tc\",\"x\":0,\"y\":0,\"w\":10,\"h\":10}";
  CHECK(P(many + "]}]}") == E_TOO_MANY_WIDGETS);
  std::string total = "{\"v\":1,\"pg\":[";
  for (int pgi = 0; pgi < 4; pgi++) {
    total += std::string(pgi ? "," : "") + "{\"w\":[";
    for (int i = 0; i < 11; i++) total += std::string(i ? "," : "") + "{\"t\":\"tc\",\"x\":0,\"y\":0,\"w\":10,\"h\":10}";
    total += "]}";
  }
  CHECK(P(total + "]}") == E_TOO_MANY_WIDGETS); // 44 > 40
  // text pool: 24 labels of 24 chars = 600 bytes > 512
  std::string pool = "{\"v\":1,\"pg\":[";
  for (int pgi = 0; pgi < 3; pgi++) {
    pool += std::string(pgi ? "," : "") + "{\"w\":[";
    for (int i = 0; i < 8; i++) pool += std::string(i ? "," : "") + "{\"t\":\"tx\",\"x\":0,\"y\":0,\"w\":10,\"h\":10,\"s\":\"abcdefghijklmnopqrstuvwx\"}";
    pool += "]}";
  }
  CHECK(P(pool + "]}") == E_TEXT_POOL);
  // nesting
  std::string deep = "{\"v\":1,\"x\":";
  for (int i = 0; i < 200; i++) deep += "[";
  CHECK(P(deep) == E_DEPTH);
  std::string deepok = "{\"v\":1,\"pg\":[{}],\"x\":";
  for (int i = 0; i < 4; i++) deepok += "[";
  for (int i = 0; i < 4; i++) deepok += "]";
  CHECK(P(deepok + "}") == E_OK);
}

// The same table web-sync/test-layout.mjs runs against the page validator: both implementations must give the same verdict.
static void test_shared_cases() {
  std::ifstream f("sketch/test/host/layout-cases.tsv");
  CHECK(f.good());
  std::string line;
  int n = 0;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    size_t tab = line.find('\t');
    std::string want = line.substr(0, tab), text = line.substr(tab + 1);
    for (size_t p; (p = text.find("\\n")) != std::string::npos;) text.replace(p, 2, "\n"); // literal backslash-n in the table = a raw newline
    Layout *L = new Layout;
    Err e = parse(text.data(), text.size(), *L);
    delete L;
    if (std::string(err_name(e)) != want) {
      g_fail++;
      printf("FAIL shared case: got \"%s\" want \"%s\": %.80s\n", err_name(e), want.c_str(), text.c_str());
    } else g_pass++;
    n++;
  }
  CHECK(n >= 60);
  // the documented example parses and its crc matches the golden value (python zlib.crc32), which the page test checks too
  std::ifstream ex("docs/layout-example.json");
  std::string example((std::istreambuf_iterator<char>(ex)), std::istreambuf_iterator<char>());
  while (!example.empty() && (example.back() == '\n' || example.back() == '\r')) example.pop_back();
  CHECK(P(example) == E_OK);
  std::ifstream cf("sketch/test/host/layout-example.crc");
  unsigned long long golden = 0;
  cf >> golden;
  CHECK(golden != 0 && crc32((const uint8_t *)example.data(), example.size()) == (uint32_t)golden);
  CHECK(example.size() < 4096);
}

static void test_truncation_and_fuzz() {
  std::string good = GOOD;
  Layout *L = new Layout;
  int ok = 0;
  for (size_t n = 0; n <= good.size(); n++) {
    Err e = parse(good.data(), n, *L);
    if (e == E_OK) ok++;
  }
  CHECK(ok == 1); // only the full text is valid: every proper prefix is rejected
  // every prefix with trailing junk byte appended
  for (size_t n = 1; n < good.size(); n++) {
    std::string t = good.substr(0, n) + "\x01";
    CHECK(parse(t.data(), t.size(), *L) != E_OK);
  }
  // deterministic mutation fuzz: must never crash (run under ASan/UBSan) and never read past the buffer
  srand(12345);
  int accepted = 0;
  for (int it = 0; it < 200000; it++) {
    std::string m = good;
    int edits = 1 + rand() % 4;
    for (int k = 0; k < edits; k++) {
      size_t pos = (size_t)rand() % m.size();
      switch (rand() % 4) {
        case 0: m[pos] = (char)(rand() & 0xFF); break;
        case 1: m.erase(pos, 1 + (size_t)(rand() % 3)); break;
        case 2: m.insert(pos, 1, "{}[]\",:0-.eE\\"[rand() % 13]); break;
        case 3: m.resize(pos); break;
      }
      if (m.empty()) m = "x";
    }
    char *exact = (char *)malloc(m.size()); // exact-size heap block so ASan sees an overread
    memcpy(exact, m.data(), m.size());
    Err e = parse(exact, m.size(), *L);
    free(exact);
    if (e == E_OK) {
      accepted++;
      // anything accepted must respect every limit
      CHECK(L->npages >= 1 && L->npages <= MAX_PAGES && L->nwidgets <= MAX_WIDGETS_TOTAL && L->text_used <= MAX_TEXT_POOL);
      for (uint8_t i = 0; i < L->nwidgets; i++) {
        const Widget &w = L->widgets[i];
        CHECK(w.x >= 0 && w.y >= 0 && w.w >= 1 && w.h >= 1 && w.x + w.w <= SCREEN_W && w.y + w.h <= SCREEN_H);
        if (w.type == W_LABEL) CHECK(w.toff + w.tlen < MAX_TEXT_POOL && L->text[w.toff + w.tlen] == '\0');
      }
    }
  }
  printf("fuzz: %d mutated layouts still valid\n", accepted);
  delete L;
}

// ------------------------------------------------------------------------------------------------
struct MemStorage : Storage {
  std::string tmp, fin;
  bool tmp_is_open = false;
  size_t free_b = 200000;
  int fail_write_at = -1; // fail the Nth tmp_write
  int writes = 0;
  bool fail_commit = false, fail_read = false, fail_open = false, corrupt_readback = false;
  uint32_t crc = 0;
  uint8_t fl = 0;
  size_t free_bytes() override { return free_b; }
  uint8_t kind = 0;
  std::string pics[8];
  uint32_t pic_crc[8] = {0};
  std::string art;
  std::string snds[SOUND_SLOTS]; // custom sounds (/snd)
  uint32_t snd_crc[SOUND_SLOTS] = {0};
  std::string originals[SND_ORIGINALS]; // the board's original sound files: no upload or reset may ever change these
  int played = -1;
  std::string gifs[GIF_SLOTS];
  uint32_t gif_crc_[GIF_SLOTS] = {0};
  uint32_t gif_crc(uint8_t slot) override { return gifs[slot].empty() ? 0 : gif_crc_[slot]; }
  uint32_t gif_size(uint8_t slot) override { return (uint32_t)gifs[slot].size(); }
  void set_gif_crc(uint8_t slot, uint32_t c) override { gif_crc_[slot] = c; }
  bool gif_remove(uint8_t slot) override { gifs[slot].clear(); return true; }
  bool tmp_open(uint8_t k, uint8_t) override {
    kind = k;
    if (fail_open) return false;
    tmp.clear();
    tmp_is_open = true;
    writes = 0;
    return true;
  }
  bool tmp_write(const uint8_t *d, size_t n) override {
    if (fail_write_at >= 0 && writes++ == fail_write_at) return false;
    tmp.append((const char *)d, n);
    return true;
  }
  bool tmp_close() override {
    tmp_is_open = false;
    return true;
  }
  bool tmp_read_all(uint8_t *buf, size_t cap, size_t &n) override {
    if (fail_read) return false;
    n = tmp.size() < cap ? tmp.size() : cap;
    memcpy(buf, tmp.data(), n);
    if (corrupt_readback && n) buf[0] ^= 0xFF;
    return true;
  }
  bool tmp_scan(uint32_t &c, size_t &n, uint8_t *head, size_t head_n, uint8_t *tail2) override {
    if (fail_read) return false;
    std::string t = tmp;
    if (corrupt_readback && !t.empty()) t[0] ^= 0x55;
    c = crc32((const uint8_t *)t.data(), t.size());
    n = t.size();
    memset(head, 0, head_n);
    memcpy(head, t.data(), n < head_n ? n : head_n);
    if (n >= 2) memcpy(tail2, t.data() + n - 2, 2);
    return true;
  }
  bool tmp_commit(uint8_t k, uint8_t slot) override {
    if (fail_commit) return false;
    if (k == KIND_PICTURE) pics[slot] = tmp;
    else if (k == KIND_ART) art = tmp;
    else if (k == KIND_SOUND) snds[slot] = tmp;
    else if (k == KIND_GIF) gifs[slot] = tmp;
    else fin = tmp;
    tmp.clear();
    return true;
  }
  uint32_t picture_crc(uint8_t slot) override { return pics[slot].empty() ? 0 : pic_crc[slot]; }
  void set_picture_crc(uint8_t slot, uint32_t c) override { pic_crc[slot] = c; }
  bool picture_remove(uint8_t slot) override { pics[slot].clear(); return true; }
  uint32_t sound_crc(uint8_t slot) override { return snds[slot].empty() ? 0 : snd_crc[slot]; }
  uint32_t sound_size(uint8_t slot) override { return (uint32_t)snds[slot].size(); }
  void set_sound_crc(uint8_t slot, uint32_t c) override { snd_crc[slot] = c; }
  bool sound_remove(uint8_t slot) override { snds[slot].clear(); return true; } // only the custom copy
  bool sound_play(uint8_t slot) override { played = slot; return true; }
  bool undecodable = false;
  bool picture_check(uint8_t, uint8_t) override { return !undecodable; }
  void tmp_discard() override {
    tmp.clear();
    tmp_is_open = false;
  }
  bool final_remove() override {
    fin.clear();
    return true;
  }
  uint32_t stored_crc() override { return crc; }
  void set_stored_crc(uint32_t c) override { crc = c; }
  uint8_t state_flags() override { return fl; }
};

static void le32(uint8_t *p, uint32_t v) {
  p[0] = v & 0xFF;
  p[1] = (v >> 8) & 0xFF;
  p[2] = (v >> 16) & 0xFF;
  p[3] = (v >> 24) & 0xFF;
}

static Ack begin(Session &s, const std::string &data, uint8_t kind = 0, uint8_t ver = 1, int32_t total_override = -1, int64_t crc_override = -1) {
  uint8_t p[11];
  p[0] = kind;
  p[1] = 0;
  p[2] = ver;
  le32(p + 3, total_override >= 0 ? (uint32_t)total_override : (uint32_t)data.size());
  le32(p + 7, crc_override >= 0 ? (uint32_t)crc_override : crc32((const uint8_t *)data.data(), data.size()));
  return s.handle(PKT_BEGIN, p, 11);
}
static Ack chunk(Session &s, const std::string &data, uint16_t seq) {
  uint8_t p[2 + CHUNK_BYTES];
  size_t off = (size_t)seq * CHUNK_BYTES;
  size_t n = off < data.size() ? data.size() - off : 0;
  if (n > CHUNK_BYTES) n = CHUNK_BYTES;
  p[0] = seq & 0xFF;
  p[1] = seq >> 8;
  memcpy(p + 2, data.data() + off, n);
  return s.handle(PKT_CHUNK, p, (uint8_t)(2 + n));
}
static size_t nchunks(const std::string &d) { return (d.size() + CHUNK_BYTES - 1) / CHUNK_BYTES; }

// a layout big enough for several chunks
static std::string big_layout() {
  std::string s = "{\"v\":1,\"pg\":[";
  for (int pgi = 0; pgi < 3; pgi++) {
    s += std::string(pgi ? "," : "") + "{\"bg\":{\"t\":\"g\",\"c\":\"#102030\",\"d\":\"#3050a0\"},\"w\":[";
    for (int i = 0; i < 9; i++) s += std::string(i ? "," : "") + "{\"t\":\"tx\",\"x\":10,\"y\":10,\"w\":100,\"h\":20,\"s\":\"label " + std::to_string(pgi * 10 + i) + "\",\"c\":\"#ffffff\"}";
    s += "]}";
  }
  return s + "]}";
}

static void test_upload_happy() {
  MemStorage st;
  Session s(st);
  std::string d = big_layout();
  CHECK(d.size() > 3 * CHUNK_BYTES && d.size() <= MAX_LAYOUT_BYTES);
  Ack a = begin(s, d);
  CHECK(a.status == ST_OK && (a.flags & FL_SESSION_OPEN) && a.seq == 0 && a.free_kb == 195);
  for (size_t i = 0; i < nchunks(d); i++) {
    a = chunk(s, d, (uint16_t)i);
    CHECK(a.status == ST_OK && a.seq == i + 1);
  }
  a = s.handle(PKT_END, nullptr, 0);
  CHECK(a.status == ST_OK && !(a.flags & FL_SESSION_OPEN) && a.crc == crc32((const uint8_t *)d.data(), d.size()));
  CHECK(st.fin == d && s.take_changed() && !s.take_changed());
  // ack frame bytes
  uint8_t f[ACK_FRAME_BYTES];
  CHECK(ack_encode(a, f) == 15 && f[0] == 0xFF && f[1] == 0x10 && f[2] == 12 && f[3] == PKT_END && f[4] == 0);
  CHECK(f[11] == (a.crc & 0xFF) && f[14] == (a.crc >> 24));
  // query has no side effects
  a = s.handle(PKT_QUERY, nullptr, 0);
  CHECK(a.status == ST_OK && a.cmd == PKT_QUERY && a.crc == st.crc && !s.take_changed());
  // reset
  a = s.handle(PKT_RESET, nullptr, 0);
  CHECK(a.status == ST_OK && st.fin.empty() && st.crc == 0 && s.take_changed());
}

static void test_upload_errors() {
  MemStorage st;
  Session s(st);
  std::string d = big_layout();
  // chunk / end without a session
  CHECK(chunk(s, d, 0).status == ST_NO_SESSION);
  CHECK(s.handle(PKT_END, nullptr, 0).status == ST_NO_SESSION);
  // bad begin packets
  uint8_t p[11] = {0};
  CHECK(s.handle(PKT_BEGIN, p, 10).status == ST_BAD_PKT);
  CHECK(s.handle(PKT_BEGIN, p, 12).status == ST_BAD_PKT);
  CHECK(begin(s, d, 5).status == ST_BAD_PKT); // kinds: 0 layout, 1 picture, 2 album art, 3 sound, 4 gif
  CHECK(begin(s, d, 7).status == ST_BAD_PKT);
  CHECK(begin(s, d, 0, 2).status == ST_INVALID);
  CHECK(begin(s, d, 0, 1, 0).status == ST_BAD_PKT);
  CHECK(begin(s, d, 0, 1, (int32_t)MAX_LAYOUT_BYTES + 1).status == ST_TOO_BIG);
  CHECK(begin(s, d, 0, 1, 100000).status == ST_TOO_BIG);
  st.free_b = 1000;
  CHECK(begin(s, d).status == ST_NO_SPACE);
  st.free_b = 200000;
  st.fail_open = true;
  CHECK(begin(s, d).status == ST_IO);
  st.fail_open = false;
  CHECK(!s.open());
  // out of order / duplicate / gap
  CHECK(begin(s, d).status == ST_OK);
  Ack a = chunk(s, d, 1);
  CHECK(a.status == ST_BAD_SEQ && a.seq == 0);
  CHECK(chunk(s, d, 0).status == ST_OK);
  CHECK(chunk(s, d, 0).status == ST_OK); // duplicate of the last chunk (lost ack): accepted without rewriting
  CHECK(st.tmp.size() == CHUNK_BYTES);
  a = chunk(s, d, 3);
  CHECK(a.status == ST_BAD_SEQ && a.seq == 1);
  // short non-final chunk is refused, state unchanged
  uint8_t sp[2 + 10] = {1, 0};
  CHECK(s.handle(PKT_CHUNK, sp, 12).status == ST_BAD_PKT);
  CHECK(s.handle(PKT_CHUNK, sp, 2).status == ST_BAD_PKT); // no data
  CHECK(s.handle(PKT_CHUNK, sp, 1).status == ST_BAD_PKT);
  uint8_t big[2 + 200] = {1, 0};
  CHECK(s.handle(PKT_CHUNK, big, 202).status == ST_BAD_PKT); // > 192 data
  CHECK(chunk(s, d, 1).status == ST_OK);
  // END before all chunks: incomplete, session stays open and can be finished
  a = s.handle(PKT_END, nullptr, 0);
  CHECK(a.status == ST_INCOMPLETE && s.open());
  for (size_t i = 2; i < nchunks(d); i++) CHECK(chunk(s, d, (uint16_t)i).status == ST_OK);
  CHECK(s.handle(PKT_END, nullptr, 0).status == ST_OK);
  CHECK(st.fin == d);
  s.take_changed();
  // extra data beyond the announced total -> abort, nothing committed
  std::string old = st.fin;
  CHECK(begin(s, d, 0, 1, (int32_t)d.size() - 5).status == ST_OK);
  size_t i = 0;
  Ack last;
  for (; i < nchunks(d); i++) {
    last = chunk(s, d, (uint16_t)i);
    if (last.status != ST_OK) break;
  }
  CHECK(last.status == ST_TOO_BIG || last.status == ST_BAD_PKT);
  CHECK(st.fin == old);
  if (last.status == ST_TOO_BIG) CHECK(!s.open());
  s.handle(PKT_ABORT, nullptr, 0);
  // wrong crc
  CHECK(begin(s, d, 0, 1, -1, 0x12345678).status == ST_OK);
  for (size_t j = 0; j < nchunks(d); j++) chunk(s, d, (uint16_t)j);
  a = s.handle(PKT_END, nullptr, 0);
  CHECK(a.status == ST_CRC && !s.open() && st.fin == old && !s.take_changed());
  // flash read-back differs from what was received
  CHECK(begin(s, d).status == ST_OK);
  for (size_t j = 0; j < nchunks(d); j++) chunk(s, d, (uint16_t)j);
  st.corrupt_readback = true;
  CHECK(s.handle(PKT_END, nullptr, 0).status == ST_CRC);
  st.corrupt_readback = false;
  CHECK(st.fin == old);
  // read-back failure and commit failure keep the old layout
  CHECK(begin(s, d).status == ST_OK);
  for (size_t j = 0; j < nchunks(d); j++) chunk(s, d, (uint16_t)j);
  st.fail_read = true;
  CHECK(s.handle(PKT_END, nullptr, 0).status == ST_IO);
  st.fail_read = false;
  CHECK(begin(s, d).status == ST_OK);
  for (size_t j = 0; j < nchunks(d); j++) chunk(s, d, (uint16_t)j);
  st.fail_commit = true;
  CHECK(s.handle(PKT_END, nullptr, 0).status == ST_IO);
  st.fail_commit = false;
  CHECK(st.fin == old && !s.take_changed());
  // bad packet ids / lengths
  CHECK(s.handle(0x55, nullptr, 0).status == ST_BAD_PKT);
  CHECK(s.handle(PKT_END, p, 3).status == ST_BAD_PKT);
  CHECK(s.handle(PKT_ABORT, p, 1).status == ST_BAD_PKT);
  CHECK(s.handle(PKT_RESET, p, 2).status == ST_BAD_PKT); // 1 byte = delete a picture slot (test_pictures)
  uint8_t badslot = 8;
  CHECK(s.handle(PKT_RESET, &badslot, 1).status == ST_BAD_PKT);
}

static void test_upload_invalid_layout() {
  MemStorage st;
  Session s(st);
  st.fin = "OLD";
  st.crc = 0xAAAA5555;
  // valid framing + crc but invalid layout (every proper prefix of a real layout, plus oversized and malformed content)
  std::string good = big_layout();
  std::vector<std::string> bad = {good.substr(0, good.size() - 1), good.substr(0, 100), "{\"v\":1,\"pg\":[{\"w\":[{\"t\":\"zz\"}]}]}", "{\"v\":9,\"pg\":[{}]}", "nothing", "{}"};
  for (auto &b : bad) {
    Ack a = begin(s, b);
    CHECK(a.status == ST_OK);
    for (size_t j = 0; j < nchunks(b); j++) chunk(s, b, (uint16_t)j);
    a = s.handle(PKT_END, nullptr, 0);
    CHECK(a.status == ST_INVALID && a.detail != E_OK);
    CHECK(st.fin == "OLD" && st.crc == 0xAAAA5555 && !s.open());
  }
  CHECK(!s.take_changed());
}

static void test_upload_interrupted_and_resume() {
  MemStorage st;
  Session s(st);
  std::string d = big_layout();
  size_t n = nchunks(d);
  CHECK(n >= 4);
  // interruption: host stops after 2 chunks (cable pulled). The old layout is untouched and the session can be resumed.
  st.fin = "OLD";
  CHECK(begin(s, d).status == ST_OK);
  CHECK(chunk(s, d, 0).status == ST_OK);
  CHECK(chunk(s, d, 1).status == ST_OK);
  CHECK(st.fin == "OLD" && s.open());
  // host reconnects and repeats BEGIN with the same parameters: ack carries next seq (2), nothing is rewritten
  Ack a = begin(s, d);
  CHECK(a.status == ST_OK && a.seq == 2 && st.tmp.size() == 2 * CHUNK_BYTES);
  for (size_t j = 2; j < n; j++) CHECK(chunk(s, d, (uint16_t)j).status == ST_OK);
  CHECK(s.handle(PKT_END, nullptr, 0).status == ST_OK && st.fin == d);
  s.take_changed();
  // BEGIN with different parameters starts over
  std::string d2 = d;
  d2.replace(d2.find("label 1"), 7, "label Z"); // different content, same size, still a valid layout
  CHECK(begin(s, d).status == ST_OK);
  CHECK(chunk(s, d, 0).status == ST_OK);
  a = begin(s, d2);
  CHECK(a.status == ST_OK && a.seq == 0 && st.tmp.empty());
  // abort drops the partial upload and keeps the stored layout
  CHECK(chunk(s, d2, 0).status == ST_OK);
  a = s.handle(PKT_ABORT, nullptr, 0);
  CHECK(a.status == ST_OK && !s.open() && st.tmp.empty() && st.fin == d);
  // write failure in the middle (flash full / error): session dropped, old layout kept
  st.fail_write_at = 2;
  CHECK(begin(s, d).status == ST_OK);
  CHECK(chunk(s, d, 0).status == ST_OK);
  CHECK(chunk(s, d, 1).status == ST_OK);
  CHECK(chunk(s, d, 2).status == ST_IO);
  CHECK(!s.open() && st.fin == d);
  st.fail_write_at = -1;
  // after a failure a clean retry works
  CHECK(begin(s, d2).status == ST_OK);
  for (size_t j = 0; j < n; j++) CHECK(chunk(s, d2, (uint16_t)j).status == ST_OK);
  CHECK(s.handle(PKT_END, nullptr, 0).status == ST_OK && st.fin == d2);
  // oversized: a 5000 byte layout is refused at BEGIN, no flash is touched
  MemStorage st2;
  Session s2(st2);
  CHECK(begin(s2, std::string(5000, 'x')).status == ST_TOO_BIG && !st2.tmp_is_open);
}

// a fake SJPG of the right shape: header + bytes. The board only checks the header, size and crc; LVGL decodes it later.
static std::string fake_sjpg(uint16_t w, uint16_t h, size_t total) {
  std::string s("_SJPG__\0V1.00\0", 14);
  uint16_t v[4] = {w, h, (uint16_t)((h + 15) / 16), 16};
  s.append((const char *)v, 8);
  while (s.size() < total) s.push_back((char)(s.size() * 7));
  return s;
}
static Ack begin_kind(Session &s, const std::string &data, uint8_t kind, uint8_t slot) {
  uint8_t p[11];
  p[0] = kind; p[1] = slot; p[2] = 1;
  le32(p + 3, (uint32_t)data.size());
  le32(p + 7, crc32((const uint8_t *)data.data(), data.size()));
  return s.handle(PKT_BEGIN, p, 11);
}
static Ack send_all(Session &s, const std::string &d) {
  for (size_t j = 0; j < nchunks(d); j++) { Ack a = chunk(s, d, (uint16_t)j); if (a.status != ST_OK) return a; }
  return s.handle(PKT_END, nullptr, 0);
}

static void test_pictures() {
  MemStorage st;
  Session s(st);
  st.fin = "LAYOUT";
  st.crc = 0x1111;
  // a 40 KB picture into slot 3
  std::string pic = fake_sjpg(240, 320, 40000);
  CHECK(begin_kind(s, pic, KIND_PICTURE, 3).status == ST_OK);
  Ack a = send_all(s, pic);
  uint32_t pc = crc32((const uint8_t *)pic.data(), pic.size());
  CHECK(a.status == ST_OK && a.crc == pc && st.pics[3] == pic);
  CHECK(st.fin == "LAYOUT" && st.crc == 0x1111); // the layout is untouched
  CHECK(s.take_pictures_changed() && !s.take_changed());
  // query with a slot answers that picture's crc; without, the layout's
  uint8_t q3 = 3, q4 = 4, q9 = 9;
  CHECK(s.handle(PKT_QUERY, &q3, 1).crc == pc);
  CHECK(s.handle(PKT_QUERY, &q4, 1).crc == 0);
  CHECK(s.handle(PKT_QUERY, &q9, 1).status == ST_BAD_PKT);
  CHECK(s.handle(PKT_QUERY, nullptr, 0).crc == 0x1111);
  CHECK(s.handle(PKT_QUERY, nullptr, 0).detail == FEATURE_LEVEL);
  // limits and checks
  CHECK(begin_kind(s, fake_sjpg(240, 320, 65537), KIND_PICTURE, 0).status == ST_TOO_BIG);
  CHECK(begin_kind(s, pic, KIND_PICTURE, 8).status == ST_BAD_PKT);
  std::string notjpg = pic; notjpg[1] = 'X';
  CHECK(begin_kind(s, notjpg, KIND_PICTURE, 1).status == ST_OK);
  CHECK(send_all(s, notjpg).status == ST_INVALID && st.pics[1].empty());
  std::string wide = fake_sjpg(241, 100, 2000);
  CHECK(begin_kind(s, wide, KIND_PICTURE, 1).status == ST_OK);
  CHECK(send_all(s, wide).status == ST_INVALID);
  std::string small = fake_sjpg(96, 64, 3000); // a picture widget smaller than the screen is fine
  CHECK(begin_kind(s, small, KIND_PICTURE, 1).status == ST_OK);
  CHECK(send_all(s, small).status == ST_OK && st.pics[1] == small);
  st.corrupt_readback = true; // flash returns other bytes than were sent
  CHECK(begin_kind(s, pic, KIND_PICTURE, 2).status == ST_OK);
  CHECK(send_all(s, pic).status == ST_CRC && st.pics[2].empty());
  st.corrupt_readback = false;
  st.free_b = 30000; // no room: refused before anything is written
  CHECK(begin_kind(s, pic, KIND_PICTURE, 2).status == ST_NO_SPACE);
  st.free_b = 200000;
  // delete one picture
  CHECK(s.handle(PKT_RESET, &q3, 1).status == ST_OK && st.pics[3].empty() && st.pic_crc[3] == 0);
  CHECK(st.fin == "LAYOUT"); // the layout stays
  // interrupted picture upload, resumed
  CHECK(begin_kind(s, pic, KIND_PICTURE, 5).status == ST_OK);
  for (int j = 0; j < 50; j++) chunk(s, pic, (uint16_t)j);
  a = begin_kind(s, pic, KIND_PICTURE, 5);
  CHECK(a.status == ST_OK && a.seq == 50);
  for (size_t j = 50; j < nchunks(pic); j++) chunk(s, pic, (uint16_t)j);
  CHECK(s.handle(PKT_END, nullptr, 0).status == ST_OK && st.pics[5] == pic);
  // a picture the board cannot decode is removed again and refused (detail 1)
  st.undecodable = true;
  CHECK(begin_kind(s, pic, KIND_PICTURE, 6).status == ST_OK);
  a = send_all(s, pic);
  CHECK(a.status == ST_INVALID && a.detail == 1 && st.pics[6].empty() && s.handle(PKT_QUERY, (const uint8_t *)"\x06", 1).crc == 0);
  st.undecodable = false;
  // album art: plain JPEG in RAM, <= 12 KB
  std::string jpg = "\xFF\xD8" + std::string(5000, 'j') + "\xFF\xD9";
  CHECK(begin_kind(s, jpg, KIND_ART, 0).status == ST_OK);
  a = send_all(s, jpg);
  CHECK(a.status == ST_OK && st.art == jpg);
  std::string badjpg = "\xFF\xD8" + std::string(100, 'j') + "xx";
  CHECK(begin_kind(s, badjpg, KIND_ART, 0).status == ST_OK);
  CHECK(send_all(s, badjpg).status == ST_INVALID && st.art == jpg);
  CHECK(begin_kind(s, std::string(12289, 'x'), KIND_ART, 0).status == ST_TOO_BIG);
  st.free_b = 1000; // album art does not need flash space
  CHECK(begin_kind(s, jpg, KIND_ART, 0).status == ST_OK);
  s.handle(PKT_ABORT, nullptr, 0);
}

// A canonical WAV like the page writes: 44-byte header, mono 16-bit PCM, a quiet 220 Hz-ish square so the data is not all zero
static std::string fake_wav(uint32_t samples, uint32_t rate = 16000) {
  std::string w(44, '\0');
  auto put32 = [&](size_t o, uint32_t v) { for (int i = 0; i < 4; i++) w[o + i] = (char)((v >> (8 * i)) & 0xFF); };
  auto put16 = [&](size_t o, uint16_t v) { w[o] = (char)(v & 0xFF); w[o + 1] = (char)(v >> 8); };
  memcpy(&w[0], "RIFF", 4); put32(4, 36 + samples * 2); memcpy(&w[8], "WAVE", 4); memcpy(&w[12], "fmt ", 4);
  put32(16, 16); put16(20, 1); put16(22, 1); put32(24, rate); put32(28, rate * 2); put16(32, 2); put16(34, 16);
  memcpy(&w[36], "data", 4); put32(40, samples * 2);
  for (uint32_t i = 0; i < samples; i++) { int16_t v = (i / 36) % 2 ? 3000 : -3000; w.push_back((char)(v & 0xFF)); w.push_back((char)((v >> 8) & 0xFF)); }
  return w;
}

static void test_wav_check() {
  std::string w = fake_wav(1600);
  uint16_t rate = 0;
  CHECK(wav_check((const uint8_t *)w.data(), 44, (uint32_t)w.size(), &rate) && rate == 16000);
  CHECK(wav_check((const uint8_t *)fake_wav(1600, 8000).data(), 44, 3244) && wav_check((const uint8_t *)fake_wav(1600, 24000).data(), 44, 3244));
  CHECK(!wav_check(nullptr, 44, 3244) && !wav_check((const uint8_t *)w.data(), 43, 3244)); // short header
  CHECK(!wav_check((const uint8_t *)w.data(), 44, 3243) && !wav_check((const uint8_t *)w.data(), 44, 3245)); // file size differs from the header's
  CHECK(!wav_check((const uint8_t *)w.data(), 44, 44) && !wav_check((const uint8_t *)w.data(), 44, MAX_SOUND_BYTES + 2));
  // every header field, wrong one at a time
  struct Mut { size_t off; uint8_t val; const char *what; } muts[] = {
    {0, 'X', "RIFF"}, {8, 'X', "WAVE"}, {12, 'X', "fmt "}, {16, 18, "fmt size"}, {20, 3, "float format"}, {20, 0, "format 0"}, {22, 2, "stereo"},
    {34, 8, "8-bit"}, {32, 4, "alignment"}, {36, 'X', "data tag"}, {4, 0, "riff size"}, {40, 0, "data size"}, {28, 1, "byte rate"},
  };
  for (const Mut &m : muts) {
    std::string b = w;
    b[m.off] = (char)m.val;
    CHECK(!wav_check((const uint8_t *)b.data(), 44, (uint32_t)b.size()));
  }
  std::string lo = fake_wav(1600, 7999), hi = fake_wav(1600, 24001); // the rate is in range only between 8000 and 24000
  CHECK(!wav_check((const uint8_t *)lo.data(), 44, (uint32_t)lo.size()) && !wav_check((const uint8_t *)hi.data(), 44, (uint32_t)hi.size()));
  std::string odd = w; odd.pop_back(); // half a sample at the end
  CHECK(!wav_check((const uint8_t *)odd.data(), 44, (uint32_t)odd.size()));
}

static void test_sounds() {
  MemStorage st;
  Session s(st);
  st.fin = "LAYOUT";
  st.crc = 0x1111;
  st.pics[2] = "PICTURE";
  st.pic_crc[2] = 0x2222;
  for (int i = 0; i < SND_ORIGINALS; i++) st.originals[i] = "ORIGINAL" + std::to_string(i);
  auto originals_intact = [&]() { for (int i = 0; i < SND_ORIGINALS; i++) if (st.originals[i] != "ORIGINAL" + std::to_string(i)) return false; return true; };
  CHECK(FEATURE_LEVEL == 5);
  // one second of sound into the "pomodoro end" slot
  std::string wav = fake_wav(16000);
  uint32_t wc = crc32((const uint8_t *)wav.data(), wav.size());
  CHECK(begin_kind(s, wav, KIND_SOUND, SND_POMO_END).status == ST_OK);
  Ack a = send_all(s, wav);
  CHECK(a.status == ST_OK && a.crc == wc && st.snds[SND_POMO_END] == wav && st.snd_crc[SND_POMO_END] == wc && !s.open());
  CHECK(st.fin == "LAYOUT" && st.crc == 0x1111 && st.pics[2] == "PICTURE" && originals_intact()); // nothing else is touched
  CHECK(!s.take_changed() && !s.take_pictures_changed());
  // query [3, slot]: crc and size in KB (32044 bytes -> 32)
  uint8_t qs[2] = {KIND_SOUND, SND_POMO_END};
  a = s.handle(PKT_QUERY, qs, 2);
  CHECK(a.status == ST_OK && a.crc == wc && a.detail == 32);
  qs[1] = SND_REMINDER;
  a = s.handle(PKT_QUERY, qs, 2);
  CHECK(a.status == ST_OK && a.crc == 0 && a.detail == 0);
  qs[1] = SOUND_SLOTS;
  CHECK(s.handle(PKT_QUERY, qs, 2).status == ST_BAD_PKT);
  uint8_t qbad[2] = {KIND_ART, 0}, qpic[2] = {KIND_PICTURE, 2}, q1[1] = {SND_POMO_END};
  CHECK(s.handle(PKT_QUERY, qbad, 2).status == ST_BAD_PKT);
  CHECK(s.handle(PKT_QUERY, qpic, 2).crc == 0x2222); // [1, slot] is the picture query
  CHECK(s.handle(PKT_QUERY, q1, 1).status == ST_BAD_PKT); // a lone byte is still a picture slot (0..7): 9 is not one
  CHECK(s.handle(PKT_QUERY, nullptr, 0).crc == 0x1111);
  // begin: slot range, size range
  CHECK(begin_kind(s, wav, KIND_SOUND, SOUND_SLOTS).status == ST_BAD_PKT);
  CHECK(begin_kind(s, std::string(45, 'x'), KIND_SOUND, 0).status == ST_BAD_PKT); // too small to hold a sound
  CHECK(begin_kind(s, std::string(MAX_SOUND_BYTES + 1, 'x'), KIND_SOUND, 0).status == ST_TOO_BIG);
  std::string biggest = fake_wav((MAX_SOUND_BYTES - 44) / 2); // exactly 5 s at 16 kHz
  CHECK(biggest.size() == MAX_SOUND_BYTES);
  st.free_b = 400000;
  CHECK(begin_kind(s, biggest, KIND_SOUND, SND_BUTTON4).status == ST_OK && send_all(s, biggest).status == ST_OK && st.snds[SND_BUTTON4] == biggest);
  st.free_b = 200000;
  // a replace keeps the old copy until the new one is whole
  std::string wav2 = fake_wav(8000, 8000);
  CHECK(begin_kind(s, wav2, KIND_SOUND, SND_POMO_END).status == ST_OK);
  CHECK(st.snds[SND_POMO_END] == wav);
  for (size_t j = 0; j < nchunks(wav2) / 2; j++) chunk(s, wav2, (uint16_t)j);
  CHECK(st.snds[SND_POMO_END] == wav);
  s.handle(PKT_ABORT, nullptr, 0);
  CHECK(st.snds[SND_POMO_END] == wav);
  CHECK(begin_kind(s, wav2, KIND_SOUND, SND_POMO_END).status == ST_OK && send_all(s, wav2).status == ST_OK && st.snds[SND_POMO_END] == wav2);
  // a sound the board cannot play is refused whole, and the slot keeps what it had
  struct Bad { size_t off; uint8_t val; } bads[] = {{0, 'X'}, {8, 'X'}, {16, 18}, {20, 3}, {22, 2}, {34, 8}, {36, 'X'}, {4, 1}, {40, 1}, {24, 0}};
  for (const Bad &b : bads) {
    std::string bw = wav;
    bw[b.off] = (char)b.val;
    CHECK(begin_kind(s, bw, KIND_SOUND, SND_REMINDER).status == ST_OK);
    a = send_all(s, bw);
    CHECK(a.status == ST_INVALID && st.snds[SND_REMINDER].empty() && !s.open());
    CHECK(s.handle(PKT_END, nullptr, 0).status == ST_NO_SESSION);
  }
  std::string text(4000, 't'); // not a WAV at all
  CHECK(begin_kind(s, text, KIND_SOUND, SND_REMINDER).status == ST_OK && send_all(s, text).status == ST_INVALID && st.snds[SND_REMINDER].empty());
  std::string odd = wav; odd.pop_back();
  CHECK(begin_kind(s, odd, KIND_SOUND, SND_REMINDER).status == ST_OK && send_all(s, odd).status == ST_INVALID);
  st.undecodable = true; // the picture decoder test is not applied to sounds
  CHECK(begin_kind(s, wav, KIND_SOUND, SND_REMINDER).status == ST_OK && send_all(s, wav).status == ST_OK && st.snds[SND_REMINDER] == wav);
  st.undecodable = false;
  // a flash read-back that differs, and a crc that does not match
  st.corrupt_readback = true;
  CHECK(begin_kind(s, wav, KIND_SOUND, SND_BUTTON1).status == ST_OK && send_all(s, wav).status == ST_CRC && st.snds[SND_BUTTON1].empty());
  st.corrupt_readback = false;
  CHECK(begin(s, wav, KIND_SOUND, 1, -1, 0x12345678).status == ST_OK);
  for (size_t j = 0; j < nchunks(wav); j++) chunk(s, wav, (uint16_t)j);
  CHECK(s.handle(PKT_END, nullptr, 0).status == ST_CRC && st.snds[0].empty());
  // no room: refused before anything is written
  st.free_b = 30000;
  CHECK(begin_kind(s, wav, KIND_SOUND, SND_BUTTON2).status == ST_NO_SPACE);
  st.free_b = 200000;
  st.fail_commit = true;
  CHECK(begin_kind(s, wav, KIND_SOUND, SND_BUTTON2).status == ST_OK && send_all(s, wav).status == ST_IO && st.snds[SND_BUTTON2].empty());
  st.fail_commit = false;
  // interrupted upload, resumed from where it stopped
  CHECK(begin_kind(s, wav, KIND_SOUND, SND_BUTTON3).status == ST_OK);
  for (int j = 0; j < 40; j++) chunk(s, wav, (uint16_t)j);
  a = begin_kind(s, wav, KIND_SOUND, SND_BUTTON3);
  CHECK(a.status == ST_OK && a.seq == 40);
  for (size_t j = 40; j < nchunks(wav); j++) chunk(s, wav, (uint16_t)j);
  CHECK(s.handle(PKT_END, nullptr, 0).status == ST_OK && st.snds[SND_BUTTON3] == wav);
  // delete the custom copy: only that file, the layout, pictures and originals stay
  uint8_t rs[2] = {KIND_SOUND, SND_POMO_END};
  CHECK(s.handle(PKT_RESET, rs, 2).status == ST_OK && st.snds[SND_POMO_END].empty() && st.snd_crc[SND_POMO_END] == 0);
  a = s.handle(PKT_QUERY, rs, 2);
  CHECK(a.crc == 0 && a.detail == 0 && !st.snds[SND_BUTTON3].empty());
  CHECK(st.fin == "LAYOUT" && st.crc == 0x1111 && st.pics[2] == "PICTURE" && originals_intact());
  CHECK(s.handle(PKT_RESET, rs, 2).status == ST_OK); // deleting what is not there is fine
  rs[1] = SOUND_SLOTS;
  CHECK(s.handle(PKT_RESET, rs, 2).status == ST_BAD_PKT);
  rs[0] = KIND_ART; rs[1] = 0;
  CHECK(s.handle(PKT_RESET, rs, 2).status == ST_BAD_PKT);
  rs[0] = KIND_SOUND; rs[1] = SND_BUTTON1;
  CHECK(begin_kind(s, wav, KIND_SOUND, SND_BUTTON1).status == ST_OK); // a reset of the slot being uploaded drops that upload
  chunk(s, wav, 0);
  CHECK(s.handle(PKT_RESET, rs, 2).status == ST_OK && !s.open());
  // PLAY
  uint8_t pl = SND_PET_TAP;
  CHECK(s.handle(PKT_PLAY, &pl, 1).status == ST_OK && st.played == SND_PET_TAP);
  pl = SOUND_SLOTS;
  CHECK(s.handle(PKT_PLAY, &pl, 1).status == ST_BAD_PKT);
  CHECK(s.handle(PKT_PLAY, nullptr, 0).status == ST_BAD_PKT && s.handle(PKT_PLAY, rs, 2).status == ST_BAD_PKT);
  CHECK(originals_intact());
}

static std::string fake_gif(uint16_t w, uint16_t h, size_t total, const char *magic = "GIF89a") {
  std::string g(magic, 6);
  g.push_back((char)(w & 0xFF)); g.push_back((char)(w >> 8)); g.push_back((char)(h & 0xFF)); g.push_back((char)(h >> 8));
  while (g.size() + 1 < total) g.push_back((char)(g.size() * 7));
  g.push_back(0x3B);
  return g;
}

static void test_gifs() {
  MemStorage st;
  Session s(st);
  st.fin = "LAYOUT"; st.crc = 0x1111; st.pics[1] = "PICTURE"; st.snds[3] = "SOUND"; st.snd_crc[3] = 0x3333;
  std::string g = fake_gif(96, 96, 30000);
  uint32_t gc = crc32((const uint8_t *)g.data(), g.size());
  CHECK(begin_kind(s, g, KIND_GIF, 2).status == ST_OK);
  Ack a = send_all(s, g);
  CHECK(a.status == ST_OK && a.crc == gc && st.gifs[2] == g && st.gif_crc_[2] == gc && !s.open());
  CHECK(st.fin == "LAYOUT" && st.pics[1] == "PICTURE" && st.snds[3] == "SOUND"); // nothing else is touched
  CHECK(s.take_pictures_changed() && !s.take_changed());
  uint8_t q[2] = {KIND_GIF, 2};
  a = s.handle(PKT_QUERY, q, 2);
  CHECK(a.status == ST_OK && a.crc == gc && a.detail == 30); // 30000 B -> 30 KB
  q[1] = 0; CHECK(s.handle(PKT_QUERY, q, 2).crc == 0);
  q[1] = GIF_SLOTS; CHECK(s.handle(PKT_QUERY, q, 2).status == ST_BAD_PKT);
  CHECK(begin_kind(s, g, KIND_GIF, GIF_SLOTS).status == ST_BAD_PKT);
  CHECK(begin_kind(s, std::string(10, 'g'), KIND_GIF, 0).status == ST_BAD_PKT);
  CHECK(begin_kind(s, fake_gif(96, 96, MAX_GIF_BYTES + 1), KIND_GIF, 0).status == ST_TOO_BIG);
  CHECK(begin_kind(s, fake_gif(128, 128, MAX_GIF_BYTES), KIND_GIF, 1).status == ST_OK && send_all(s, fake_gif(128, 128, MAX_GIF_BYTES)).status == ST_OK); // the largest
  std::string good = fake_gif(64, 64, 2000), bads[] = {fake_gif(64, 64, 2000, "GIF90a"), fake_gif(129, 64, 2000), fake_gif(64, 129, 2000), fake_gif(0, 64, 2000), fake_gif(64, 0, 2000), good.substr(0, good.size() - 1) + "x", std::string(3000, 'x')};
  for (const std::string &b : bads) {
    std::string before = st.gifs[3];
    CHECK(begin_kind(s, b, KIND_GIF, 3).status == ST_OK);
    CHECK(send_all(s, b).status == ST_INVALID && st.gifs[3] == before && !s.open());
  }
  CHECK(begin_kind(s, fake_gif(128, 128, 2000, "GIF87a"), KIND_GIF, 3).status == ST_OK && send_all(s, fake_gif(128, 128, 2000, "GIF87a")).status == ST_OK); // GIF87a is fine
  st.undecodable = true; // the board's decoder refuses it: removed again, detail 1
  CHECK(begin_kind(s, good, KIND_GIF, 0).status == ST_OK);
  a = send_all(s, good);
  CHECK(a.status == ST_INVALID && a.detail == 1 && st.gifs[0].empty());
  st.undecodable = false;
  st.corrupt_readback = true;
  CHECK(begin_kind(s, good, KIND_GIF, 0).status == ST_OK && send_all(s, good).status == ST_CRC && st.gifs[0].empty());
  st.corrupt_readback = false;
  st.free_b = 30000;
  CHECK(begin_kind(s, g, KIND_GIF, 0).status == ST_NO_SPACE);
  st.free_b = 200000;
  CHECK(begin_kind(s, g, KIND_GIF, 0).status == ST_OK);
  for (int j = 0; j < 40; j++) chunk(s, g, (uint16_t)j);
  a = begin_kind(s, g, KIND_GIF, 0);
  CHECK(a.status == ST_OK && a.seq == 40);
  for (size_t j = 40; j < nchunks(g); j++) chunk(s, g, (uint16_t)j);
  CHECK(s.handle(PKT_END, nullptr, 0).status == ST_OK && st.gifs[0] == g);
  uint8_t rs[2] = {KIND_GIF, 0};
  CHECK(s.handle(PKT_RESET, rs, 2).status == ST_OK && st.gifs[0].empty() && st.gif_crc_[0] == 0 && st.gifs[2] == g);
  CHECK(st.fin == "LAYOUT" && st.pics[1] == "PICTURE" && st.snds[3] == "SOUND");
  rs[1] = GIF_SLOTS; CHECK(s.handle(PKT_RESET, rs, 2).status == ST_BAD_PKT);
  uint16_t w = 0, h = 0;
  CHECK(gif_check((const uint8_t *)good.data(), 44, (uint32_t)good.size(), (const uint8_t *)(good.data() + good.size() - 2), &w, &h) && w == 64 && h == 64);
}

static void test_next_event() {
  std::ifstream f("sketch/test/host/event-cases.tsv");
  std::string line;
  int n = 0;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    size_t a = line.find('\t'), b = line.find('\t', a + 1);
    if (a == std::string::npos || b == std::string::npos) continue;
    std::string text = line.substr(0, a), now = line.substr(a + 1, b - a - 1), want = line.substr(b + 1);
    uint16_t nm = (uint16_t)(atoi(now.c_str()) * 60 + atoi(now.c_str() + 3)), mins = 0;
    char out[64];
    bool got = next_event(text.c_str(), nm, out, sizeof out, &mins);
    std::string have = got ? std::string(out) + "|" + std::to_string(mins) : "none";
    if (have != want) printf("event case failed: [%s] at %s: wanted [%s] got [%s]\n", text.c_str(), now.c_str(), want.c_str(), have.c_str());
    CHECK(have == want);
    n++;
  }
  CHECK(n >= 15);
  char small[8];
  uint16_t m = 0;
  CHECK(!next_event(nullptr, 0, small, sizeof small, &m) && !next_event("12:00 x", 0, small, 4, &m));
  CHECK(next_event("12:00 abc", 0, small, sizeof small, &m) && strlen(small) <= 7); // a small buffer cuts the title, never overflows
  char big[64];
  CHECK(next_event("12:00 0123456789012345678901234567890123456789012345678901234567890123456789", 0, big, sizeof big, &m) && strlen(big) == 6 + 40); // 40 byte title limit
}

int main() {
  test_crc();
  test_parse_good();
  test_parse_bad();
  test_shared_cases();
  test_truncation_and_fuzz();
  test_upload_happy();
  test_upload_errors();
  test_upload_invalid_layout();
  test_upload_interrupted_and_resume();
  test_pictures();
  test_wav_check();
  test_sounds();
  test_gifs();
  test_next_event();
  printf("layout_core: %d checks passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
