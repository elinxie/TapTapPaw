#pragma once
// Layout core: pure C++ (no Arduino, no LVGL) so it can be unit-tested on the host with g++ (scripts/test-layout-core.sh).
//  - layout JSON parser + validator (schema v1, see docs/WIDGETS.md)
//  - CRC32 (zlib / IEEE, same as the web page and python tool)
//  - upload session state machine for packets 0x0B..0x11 (chunked, acked, checksummed, abortable, resumable)
#include <stddef.h>
#include <stdint.h>

namespace lc {

constexpr uint8_t SCHEMA_VERSION = 1;
constexpr size_t MAX_LAYOUT_BYTES = 4096;
constexpr uint8_t MAX_PAGES = 6;
constexpr uint8_t MAX_WIDGETS_PAGE = 12;
constexpr uint8_t MAX_WIDGETS_TOTAL = 40;
constexpr size_t MAX_TEXT_POOL = 512;
constexpr uint8_t MAX_TEXT_LEN = 24;
constexpr uint16_t CHUNK_BYTES = 192; // fixed chunk size: chunk n starts at byte n * 192
constexpr uint16_t SCREEN_W = 320;
constexpr uint16_t SCREEN_H = 240;

enum WType : uint8_t { W_NONE = 0, W_ANALOG, W_DIGITAL, W_WEATHER, W_NOWPLAY, W_SYSBARS, W_TAPS, W_LABEL, W_MOOD, W_SHAPE, W_SLIDER, W_COUNT };
enum BgType : uint8_t { BG_WEATHER = 0, BG_BUILTIN, BG_SOLID, BG_GRADIENT };

struct Widget {
  uint8_t type;
  uint8_t k; // kind: weather unit 0 device/1 C/2 F; shape 0 rect/1 ellipse; slider 0 brightness/1 volume
  uint8_t z; // size: 0 small, 1 large
  uint8_t a; // text align: 0 left, 1 center, 2 right
  uint8_t r; // corner radius
  uint8_t opa; // opacity 0..255 (default 255)
  bool f24; // digital clock: 24 hour
  bool secs; // digital clock: show seconds
  bool has_c;
  uint32_t c; // 0xRRGGBB
  int16_t x, y, w, h;
  uint16_t toff; // text in Layout::text
  uint8_t tlen;
};

struct Page {
  bool is_main; // the original built-in screen
  uint8_t bg_type;
  uint8_t bg_idx; // built-in wallpaper 0..5
  uint32_t c1, c2;
  uint8_t first, nw; // widgets are stored flat; this page owns widgets[first .. first+nw)
};

struct Layout {
  uint8_t version;
  uint16_t rotate_s; // 0 = off, else seconds per page
  uint8_t npages;
  uint8_t nwidgets;
  uint16_t text_used;
  Page pages[MAX_PAGES];
  Widget widgets[MAX_WIDGETS_TOTAL];
  char text[MAX_TEXT_POOL];
};

enum Err : uint8_t {
  E_OK = 0,
  E_EMPTY,
  E_TOO_BIG,
  E_SYNTAX,
  E_DEPTH,
  E_VERSION,
  E_NO_PAGES,
  E_TOO_MANY_PAGES,
  E_PAGE,
  E_MAIN_DUP,
  E_TOO_MANY_WIDGETS,
  E_WIDGET_TYPE,
  E_GEOMETRY,
  E_COLOR,
  E_RANGE,
  E_TEXT,
  E_TEXT_POOL,
  E_TRAILING,
  E_COUNT
};
const char *err_name(Err e);

// Parse and fully validate. On error `out` content is unspecified. Unknown keys are ignored (forward compatible).
Err parse(const char *json, size_t len, Layout &out);

uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t n); // start with 0
inline uint32_t crc32(const uint8_t *data, size_t n) { return crc32_update(0, data, n); }

// ---------------------------------------------------------------------------------------------
// Upload protocol (host -> board). Frame: [0xFF][id][len][payload...], len <= 253
//   0x0B BEGIN : [kind][slot][schema ver][total u32 LE][crc32 u32 LE]       kind 0 = layout, 1 = image (phase 3)
//   0x0C CHUNK : [seq u16 LE][data 1..192]                                   chunk n = bytes n*192 ..
//   0x0D END   : (empty)                                                     verify, validate, commit atomically
//   0x0E ABORT : (empty)
//   0x0F QUERY : (empty)                                                     ack carries state, no side effects
//   0x11 RESET : (empty)                                                     delete layout, back to the built-in default
// Board -> host 0x10 ACK: [cmd][status][detail][flags][seq u16 LE][free_kb u16 LE][crc32 u32 LE]  (payload 12 bytes)
// ---------------------------------------------------------------------------------------------
enum PktId : uint8_t { PKT_BEGIN = 0x0B, PKT_CHUNK = 0x0C, PKT_END = 0x0D, PKT_ABORT = 0x0E, PKT_QUERY = 0x0F, PKT_ACK = 0x10, PKT_RESET = 0x11 };

enum Status : uint8_t {
  ST_OK = 0,
  ST_BAD_PKT = 1, // wrong length or field
  ST_BAD_SEQ = 2, // seq field of the ack = the chunk the board expects
  ST_TOO_BIG = 3,
  ST_CRC = 4,
  ST_UNSUPPORTED = 5,
  ST_NO_SPACE = 6,
  ST_IO = 7,
  ST_INVALID = 8, // layout failed validation, detail = Err
  ST_NO_SESSION = 9,
  ST_INCOMPLETE = 10
};

enum Flags : uint8_t { FL_LAYOUT_ACTIVE = 1, FL_SAFE_MODE = 2, FL_SESSION_OPEN = 4, FL_LAYOUT_STORED = 8 };

constexpr size_t ACK_FRAME_BYTES = 3 + 12;

struct Ack {
  uint8_t cmd, status, detail, flags;
  uint16_t seq;
  uint16_t free_kb;
  uint32_t crc;
};
size_t ack_encode(const Ack &a, uint8_t *out); // returns ACK_FRAME_BYTES

class Storage {
 public:
  virtual ~Storage() {}
  virtual size_t free_bytes() = 0;
  virtual bool tmp_open() = 0; // create / truncate the temp file
  virtual bool tmp_write(const uint8_t *d, size_t n) = 0;
  virtual bool tmp_close() = 0;
  virtual bool tmp_read_all(uint8_t *buf, size_t cap, size_t &n) = 0; // read the temp file back (verify step)
  virtual bool tmp_commit() = 0; // replace the stored layout with the temp file, atomically
  virtual void tmp_discard() = 0;
  virtual bool final_remove() = 0; // delete the stored layout
  virtual uint32_t stored_crc() = 0; // crc32 of the stored layout, 0 if none
  virtual void set_stored_crc(uint32_t crc) = 0;
  virtual uint8_t state_flags() = 0; // FL_LAYOUT_ACTIVE | FL_SAFE_MODE | FL_LAYOUT_STORED, owned by the firmware glue
};

class Session {
 public:
  explicit Session(Storage &s) : st(s) {}
  Ack handle(uint8_t id, const uint8_t *payload, uint8_t len);
  bool take_changed() { bool c = changed; changed = false; return c; } // a layout was committed or reset since the last call
  bool open() const { return is_open; }

 private:
  Ack make(uint8_t cmd, uint8_t status, uint8_t detail = 0);
  Ack do_begin(const uint8_t *p, uint8_t n);
  Ack do_chunk(const uint8_t *p, uint8_t n);
  Ack do_end();
  Ack do_abort();
  void drop();
  Storage &st;
  bool is_open = false;
  bool changed = false;
  uint8_t kind = 0, slot = 0, ver = 0;
  uint32_t total = 0, want_crc = 0, run_crc = 0, received = 0;
  uint16_t next_seq = 0;
};

} // namespace lc
