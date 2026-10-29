# Widget layouts (no-code home screen)

The board can show a **layout** instead of its built-in main screen: several pages of widgets (clock, weather, now playing, ...) on a
320x240 canvas, each page with its own background. Nothing is compiled: the web page builds a small JSON file and sends it to the
board over USB; the firmware reads it and builds the screens.

Status: **Phase 1 (layout engine + upload protocol)** is implemented. The drag-and-drop builder page (Phase 2) and custom image
backgrounds (Phase 3) are not. Until then a layout is written as JSON by hand (see `layout-example.json`) and sent with
`scripts/layout_tool.py`.

## What is and is not possible

The board has no browser. A widget is one of a **fixed set of types drawn by the firmware** from data it already has; you cannot
put arbitrary HTML, CSS, images or fonts on it.

| You can | You cannot (yet) |
|---------|------------------|
| Place the widget types below, anywhere on 320x240, up to 12 per page, 6 pages, 40 in total | Add new widget types, custom fonts or custom icons without new firmware |
| Pick colors, text size (small/large), alignment, opacity, corner radius for the widgets that have them | Resize the analog clock or the mood cat (they have a fixed size) |
| Choose a page background: weather-driven (default), one of the 6 built-in wallpapers, solid color, vertical gradient | Custom image backgrounds (Phase 3) |
| Keep the original main screen as one of the pages (`{"m":1}`) | Edit the original main screen's parts individually |
| Swipe left/right between pages, optional auto-rotate | Per-widget tap actions (stretch goal) |

With **no layout stored the board behaves exactly as before**: the same screens, the same swipe-down Settings, the same packets.

## Layout file (schema version 1)

Compact JSON, at most **4096 bytes** (the example is 687 bytes). Integers only. Unknown keys are ignored (forward compatible).

```
{"v":1, "rot":0, "pg":[ PAGE, ... ]}
```

| Key | Meaning |
|-----|---------|
| `v` | schema version, must be `1` |
| `rot` | auto-rotate: `0` = off, otherwise 3..600 seconds per page. Rotation pauses while the screen is touched. |
| `pg` | 1..6 pages. Page 0 is the home page the board starts on. Swiping wraps around. |

**Page**: `{"m":1}` is the original built-in main screen (allowed once, nothing else in that page object), or
`{"bg":BG, "w":[WIDGET, ...]}` (both optional).

**Background** `bg`:

| | Meaning |
|--|---------|
| `{"t":"w"}` | weather-driven wallpaper (the current behaviour; default when `bg` is missing) |
| `{"t":"b","i":0..5}` | built-in wallpaper: 0 sunny, 1 cloudy, 2 rainy, 3 stormy, 4 snowy, 5 night |
| `{"t":"s","c":"#rrggbb"}` | solid color (`#rgb` also accepted) |
| `{"t":"g","c":"#top","d":"#bottom"}` | vertical gradient |

**Widget**: `{"t":TYPE,"x":,"y":,"w":,"h":, ...options}`. `x y w h` are required for every widget (even the fixed-size ones) and the
widget must lie fully inside 320x240.

| `t` | Widget | Options | Notes |
|-----|--------|---------|-------|
| `ac` | analog clock | | fixed 90x90 (the same clock as the main screen) |
| `dc` | digital clock | `f` 1 = 24 h, `q` 1 = show seconds, `z` size 0/1, `a` align 0/1/2, `c` color | 12 h shows AM/PM |
| `wx` | weather | `k` 0 follow the device C/F setting, 1 = C, 2 = F; `c` text color | icon, temperature, humidity, observation time |
| `np` | now playing | `c` text color | status, title (scrolls), artist |
| `sb` | system bars | | CPU / MEM / BAT bars |
| `tc` | tap counter | `z`, `a`, `c` | "Taps: N", the lifetime key counter |
| `tx` | text label | `s` text (1..24 bytes, required), `z`, `a`, `c` | `\"` and `\\` escapes only; no `\uXXXX` (type the character itself) |
| `mc` | mood cat | | fixed 200x132; eyes shut when happy, asleep after 60 s idle or the sleepy key |
| `sh` | shape / divider | `k` 0 rectangle, 1 ellipse; `c`, `o` opacity 0..255, `r` corner radius | a divider is a thin rectangle |
| `sl` | slider | `k` 0 brightness, 1 volume; `c` | dragging a slider never swipes the page; saved on release |

Limits (checked by the page and by the firmware, same rules): 12 widgets per page, 40 in total, 24 bytes per label, 512 bytes of label
text in total, nesting depth 8. Anything else is rejected whole; the old layout stays in place.

Data comes from what is already sent to the board: time and system bars from the status packet `0x01`, music `0x02`, weather `0x03`
(or WiFi mode), taps from the physical keys. With no feed a widget shows its placeholder.

## Gestures

- Swipe **left / right**: next / previous page (wraps). Starts anywhere that is not a slider, and not on the original screen's chart or the
  music title/artist text (those parts of the original screen swallow gestures).
- Swipe **down**: Settings, as before. Swipe **up** in Settings returns to the page you left.
- Auto-rotate only runs while nobody touches the screen.

## Storage

- The layout is `/layout.json` in the LittleFS partition (the 1 MB one that also holds the sound files). A layout is at most 4 KB.
  Measured on the board: **200 KB free** in that partition with the stock sounds, so layouts cost nothing noticeable.
- RAM: the active layout is parsed into about 1.7 KB of static memory; a page on screen is built from LVGL objects (heap) and freed when
  you swipe away. Measured with the 3-page example cycling on the board: free heap stays around 241 KB (no growth over 10 page changes).
- Flash: the engine adds about 17 KB to the normal build (see "Measured size" below).
- NVS namespace `layout` holds the stored layout's crc and the safe-mode counters. Nothing else is touched.

## Safe mode

A bad layout cannot lock you out. The board ignores the stored layout (and shows the built-in screens) when:

| Reason | What happens | How you get the layout back |
|--------|--------------|-----------------------------|
| BOOT button held at power-up | skipped for this boot only | the next normal boot loads it |
| The layout crashed the board twice (panic / watchdog before it ran 10 s) | ignored until replaced | upload a new layout, or reset |
| The stored file fails validation | ignored until replaced | upload a new layout, or reset |

A red note appears for a few seconds ("Safe mode: ...") and Settings > More > "Widget layout" shows the state and has a **Reset
layout** button (deletes the stored layout). A normal reboot, USB reset or power cut is *not* counted as a crash.

Note: holding BOOT at power-up also starts the **touch calibration** (existing behaviour). Do both, or just touch the targets.

Layout uploads are applied only after the whole file has been received, its CRC32 checked, **read back from flash and checked again**,
and validated; the old layout is replaced atomically (temp file + rename). An interrupted upload never touches the stored layout.

## Upload protocol

Same framing as the other packets: `[0xFF][id][len][payload]`, 115200 baud. The board answers every layout packet with an ack. Existing
packets `0x01`-`0x0A` are unchanged. Stop-and-wait: send one packet, wait for its ack (there is no flow control on the serial line).

| id | name | payload |
|----|------|---------|
| `0x0B` | BEGIN | `kind` (0 layout, 1 image = Phase 3, refused today), `slot`, `schema version`, `total u32 LE`, `crc32 u32 LE` (11 bytes) |
| `0x0C` | CHUNK | `seq u16 LE`, 1..192 data bytes. Chunk n holds bytes n*192 .. n*192+191; every chunk but the last is exactly 192 bytes |
| `0x0D` | END | empty: verify, validate, commit |
| `0x0E` | ABORT | empty: drop the upload in progress |
| `0x0F` | QUERY | empty: report state, no side effects |
| `0x11` | RESET | empty: delete the stored layout, back to the built-in screens |
| `0x10` | ACK (board to host) | `cmd`, `status`, `detail`, `flags`, `seq u16`, `free_kb u16`, `crc32 u32` (12 bytes) |

`status`: 0 ok, 1 bad packet, 2 bad sequence (`seq` = the chunk the board wants), 3 too big, 4 checksum mismatch, 5 unsupported,
6 no space, 7 flash write error, 8 invalid layout (`detail` = reason, see `Err` in `layout_core.h`), 9 no upload in progress,
10 incomplete. `flags`: 1 layout active, 2 safe mode, 4 upload in progress, 8 layout stored. `seq` is the next chunk expected, `crc32`
the stored layout's checksum (0 = none), `free_kb` the free space of the LittleFS partition.

- **Retry**: resend the same packet if no ack arrives. A repeated CHUNK for the one just stored is acked without writing it again.
- **Resume**: BEGIN with the same kind/slot/size/crc as the upload in progress answers `seq` = the next missing chunk. A reset or power
  loss drops the partial upload (the stored layout is untouched); start again from chunk 0.
- **Verify**: after END, send QUERY: `crc32` must equal the checksum of what you sent.
- A BEGIN larger than 4096 bytes, an unknown version or kind, or too little free space is refused before anything is written.

The page code is in `web-sync/index.html` (`validateLayout`, `layoutBeginPacket`, ..., `uploadLayoutBytes`); the firmware side is
`sketch/src/layout_core.cpp` (parser, protocol) and `sketch/src/layout.cpp` (screens, safe mode).

## Developer tools and tests

```
scripts/test-layout-core.sh          # firmware parser + upload state machine on the host (g++, ASan/UBSan, mutation fuzz)
node web-sync/test-layout.mjs        # page validator (same shared cases), packets, ack decoding, upload driver
node web-sync/test-packets.mjs       # existing packets, unchanged
~/.venvs/pio39/bin/python scripts/layout_smoke_test.py   # real board over USB (quit the desktop app first); ends with RESET
~/.venvs/pio39/bin/python scripts/layout_tool.py upload docs/layout-example.json
~/.venvs/pio39/bin/python scripts/layout_tool.py info | reset
```

`sketch/test/host/layout-cases.tsv` is a table of 73 layouts with the expected verdict; the firmware parser and the page validator must
both agree with every row.

## Measured size (build flags `-DINVERT_COLORS_DEFAULT=1`)

| | Before (`feature/wifi-mode`) | Phase 1 | Change |
|--|--|--|--|
| `esp32dev` flash (1,441,792 B partition) | 1,277,353 B (88.6 %) | 1,294,189 B (89.8 %) | +16,836 B, ~147 KB left |
| `esp32dev` RAM (static) | 57,552 B (17.6 %) | 59,752 B (18.2 %) | +2,200 B |
| `esp32dev_wifi` flash (2,883,584 B partition) | 1,950,537 B (67.6 %) | 1,966,497 B (68.2 %) | +15,960 B |
| `esp32dev_wifi` RAM (static) | 83,020 B (25.3 %) | 85,204 B (26.0 %) | +2,184 B |
