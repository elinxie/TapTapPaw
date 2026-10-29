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
constexpr uint32_t MAX_PICTURE_BYTES = 65536; // upload kind 1: an SJPG picture stored in LittleFS (/img/<slot>.sjpg)
constexpr uint32_t MAX_ART_BYTES = 12288; // upload kind 2: album art, a plain 96x96 JPEG kept in RAM only
enum Kind : uint8_t { KIND_LAYOUT = 0, KIND_PICTURE = 1, KIND_ART = 2 };
constexpr uint16_t SCREEN_W = 240; // portrait: panel 240x320, rotation 0/2 (LGFX_CYD.h)
constexpr uint16_t SCREEN_H = 320;

enum WType : uint8_t {
  W_NONE = 0, W_ANALOG, W_DIGITAL, W_WEATHER, W_NOWPLAY, W_SYSBARS, W_TAPS, W_LABEL, W_MOOD, W_SHAPE, W_SLIDER,
  // feature level 2
  W_TCAT, W_POMO, W_COUNTDOWN, W_STOPWATCH, W_DATE, W_FOCUS, W_BUTTON, W_SPOTIFY, W_ALBUM, W_REMIND, W_PICTURE,
  W_COUNT
};
// What this firmware understands; reported in the QUERY ack's detail byte (older firmware answers 0).
constexpr uint8_t FEATURE_LEVEL = 2;
constexpr uint8_t PICTURE_SLOTS = 8;

// Button widget actions ("e"). Arg ("g") only for the ones marked.
enum BtnAction : uint8_t {
  BA_NONE = 0, BA_PAW_TAP, BA_HAPPY, BA_SLEEPY, BA_TOGGLE_UNIT, BA_MUTE, BA_BRIGHTNESS_STEP, BA_WALLPAPER_NEXT,
  BA_PAGE_NEXT, BA_PAGE_PREV, BA_POMO_TOGGLE, BA_POMO_RESET, BA_FOCUS_TOGGLE, BA_SW_TOGGLE, BA_SW_RESET,
  BA_SP_PLAY, BA_SP_NEXT, BA_SP_PREV, // sent to the web page as event 0x13 (Spotify)
  BA_HOST_EVENT, // sent to the host as event 0x13 with arg g
  BA_BLE_KEY, // Bluetooth keyboard build only: g = (modifiers << 8) | HID usage
  BA_BLE_MEDIA, // Bluetooth keyboard build only: g = media key id
  BA_COUNT
};
enum BgType : uint8_t { BG_WEATHER = 0, BG_BUILTIN, BG_SOLID, BG_GRADIENT, BG_IMAGE };

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
  uint16_t p; // minutes: pomodoro work, reminder interval, countdown target (minutes after midnight)
  uint8_t b; // pomodoro break minutes
  uint8_t e; // button action (BtnAction)
  uint16_t g; // button action argument
  uint8_t i; // picture slot
};

struct Page {
  bool is_main; // the original built-in screen
  uint8_t bg_type;
  uint8_t bg_idx; // built-in wallpaper 0..5, or picture slot 0..7 for BG_IMAGE
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
//   0x0B BEGIN : [kind][slot][schema ver][total u32 LE][crc32 u32 LE]       kind 0 layout, 1 picture (slot 0..7), 2 album art
//   0x0C CHUNK : [seq u16 LE][data 1..192]                                   chunk n = bytes n*192 ..
//   0x0D END   : (empty)                                                     verify, validate, commit atomically
//   0x0E ABORT : (empty)
//   0x0F QUERY : (empty) or [slot]                                           ack carries state; with a slot, crc = that picture's
//   0x11 RESET : (empty) or [slot]                                           delete the layout (pictures stay), or one picture
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

enum Flags : uint8_t { FL_LAYOUT_ACTIVE = 1, FL_SAFE_MODE = 2, FL_SESSION_OPEN = 4, FL_LAYOUT_STORED = 8, FL_BLE_KEYS = 16, FL_BLE_PAIRED = 32 };

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
  virtual bool tmp_open(uint8_t kind, uint8_t slot) = 0; // create / truncate the temp file for this kind
  virtual bool tmp_write(const uint8_t *d, size_t n) = 0;
  virtual bool tmp_close() = 0;
  virtual bool tmp_read_all(uint8_t *buf, size_t cap, size_t &n) = 0; // read the temp file back (verify step, layouts)
  // pictures: stream the temp file back, crc of all of it, its first head_n bytes and its last 2 bytes
  virtual bool tmp_scan(uint32_t &crc, size_t &n, uint8_t *head, size_t head_n, uint8_t *tail2) = 0;
  virtual bool tmp_commit(uint8_t kind, uint8_t slot) = 0; // replace the stored layout / picture / album art, atomically
  virtual void tmp_discard() = 0;
  virtual bool final_remove() = 0; // delete the stored layout
  virtual uint32_t stored_crc() = 0; // crc32 of the stored layout, 0 if none
  virtual void set_stored_crc(uint32_t crc) = 0;
  virtual uint8_t state_flags() = 0; // FL_LAYOUT_ACTIVE | FL_SAFE_MODE | FL_LAYOUT_STORED, owned by the firmware glue
  virtual uint32_t picture_crc(uint8_t slot) = 0; // 0 if the slot is empty
  virtual void set_picture_crc(uint8_t slot, uint32_t crc) = 0;
  virtual bool picture_remove(uint8_t slot) = 0;
  // after a picture / album art is stored: can the board's own decoder read it? (firmware: LVGL SJPG/JPEG decode test)
  virtual bool picture_check(uint8_t kind, uint8_t slot) { (void)kind; (void)slot; return true; }
};

class Session {
 public:
  explicit Session(Storage &s) : st(s) {}
  Ack handle(uint8_t id, const uint8_t *payload, uint8_t len);
  bool take_changed() { bool c = changed; changed = false; return c; } // a layout was committed or reset since the last call
  bool take_pictures_changed() { bool c = pics_changed; pics_changed = false; return c; } // a picture or the album art changed
  bool open() const { return is_open; }

 private:
  Ack make(uint8_t cmd, uint8_t status, uint8_t detail = 0, bool own_crc = false, uint32_t crc = 0);
  Ack end_picture();
  Ack do_begin(const uint8_t *p, uint8_t n);
  Ack do_chunk(const uint8_t *p, uint8_t n);
  Ack do_end();
  Ack do_abort();
  void drop();
  Storage &st;
  bool is_open = false;
  bool changed = false, pics_changed = false;
  bool free_known = false;
  size_t free_cache = 0;
  uint8_t kind = 0, slot = 0, ver = 0;
  uint32_t total = 0, want_crc = 0, run_crc = 0, received = 0;
  uint16_t next_seq = 0;
};

} // namespace lc
