# Widget layouts (no-code home screen)

The board can show a **layout** instead of its built-in main screen: several pages of widgets (clock, weather, now playing, ...) on a
240x320 (portrait) canvas, each page with its own background. Nothing is compiled: the web page builds a small JSON file and sends it to the
board over USB; the firmware reads it and builds the screens.

Status: **Phase 1** (layout engine + upload protocol), **Phase 2** (the builder on the web page) and **Phase 3** (typing cat, timers,
buttons, Bluetooth keys, Spotify controls, pictures, album art, pets; see "Phase 3 widgets" below) are done. **Phase 4a** adds custom
sounds (see "Custom sounds" below). **Phase 4b** adds the animated GIF widget ("GIF widget" below). Planned next: a one-tap timer button.

## Using the builder (web page)

Open the web-sync page (`WEB_SYNC.md`) in Chrome or Edge. The **Widget builder** section is right under Connection.

1. Pick a template, or start from what is there. Your last layout is kept in this browser (also across reloads).
2. Page tabs on top: **+ Page** adds a page, **+ Original screen** adds the board's original screen as the first page. Page 1 is the
   home page the board starts on.
3. The buttons under the screen add widgets to the current page. Click a widget to select it, drag to move, drag the blue handles to
   resize. Snapping follows the Grid setting; hold Shift while dragging to turn it off. Arrow keys nudge (Alt = 1 px), Delete removes,
   Ctrl/Cmd+D duplicates, Ctrl/Cmd+Z / Shift+Ctrl/Cmd+Z undo and redo.
4. The panel on the right edits the page (background, order, auto-rotate) and the selected widget (position, size, text, colors,
   options, front/back order).
5. **Connect to board**, then **Send to board**. The progress bar shows the chunks; the page reads the checksum back to confirm.
   **Check board** says whether the board has the layout you see. **Reset board to default** deletes the board's layout.
6. **Export** saves the layout as a `.json` file, **Import** loads one, **Save as** keeps named layouts in this browser.

**Dry run** (Connection) works without a board: a simulated board answers the upload so you can try the whole flow, and the log shows
every packet.

The preview uses the board's own wallpapers, cat, clock face and weather icons (taken from the firmware by
`scripts/make_preview_assets.py`), the same sizes and line heights as the board's fonts, and the page's own time, weather and typed
music. Fonts are close, not identical (the board's Montserrat, Itim and DynaPuff are not loaded from the internet, so the browser's
fallback fonts are used).

## What is and is not possible

The board has no browser. A widget is one of a **fixed set of types drawn by the firmware** from data it already has; you cannot
put arbitrary HTML, CSS, images or fonts on it.

| You can | You cannot (yet) |
|---------|------------------|
| Place the widget types below, anywhere on the 240x320 portrait screen, up to 12 per page, 6 pages, 40 in total | Add new widget types or custom fonts without new firmware |
| Pick colors, text size (small/large), alignment, opacity, corner radius for the widgets that have them | Resize the analog clock or the mood cat (they have a fixed size) |
| Choose a page background: weather-driven (default), one of the 6 built-in wallpapers, solid color, vertical gradient | Animated GIFs (planned) |
| Keep the original main screen as one of the pages (`{"m":1}`) | Edit the original main screen's parts individually |
| Swipe left/right between pages, optional auto-rotate | Run programs on the computer (a page cannot; Bluetooth keys can send shortcuts) |

With **no layout stored the board behaves exactly as before**: the same screens, the same swipe-down Settings, the same packets.

## Layout file (schema version 1)

Compact JSON, at most **4096 bytes** (the example is 647 bytes). Integers only. Unknown keys are ignored (forward compatible).

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
| `{"t":"i","i":0..7}` | your picture in slot 0..7 (Phase 3), sent by the page; a missing slot shows the weather wallpaper |

**Widget**: `{"t":TYPE,"x":,"y":,"w":,"h":, ...options}`. `x y w h` are required for every widget (even the fixed-size ones) and the
widget must lie fully inside the screen: 240 wide x 320 tall, portrait like the original screens (panel 240x320, rotation 0, or 180 degrees with the rotate setting).

| `t` | Widget | Options | Notes |
|-----|--------|---------|-------|
| `ac` | analog clock | | fixed 90x90 (the same clock as the main screen) |
| `dc` | digital clock | `f` 1 = 24 h, `q` 1 = show seconds, `z` size 0/1, `a` align 0/1/2, `c` color | 12 h shows AM/PM |
| `wx` | weather | `k` 0 follow the device C/F setting, 1 = C, 2 = F; `c` text and icon color (white) | icon, temperature, humidity, observation time |
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

## Phase 3 widgets (firmware feature level 2)

The page asks the board (QUERY, `detail` = feature level) before sending; an older firmware gets a clear message instead of a refusal.

| `t` | Widget | Options | Notes |
|-----|--------|---------|-------|
| `ct` | typing cat | | fixed 240x132: the main screen's cat; taps along with the keys, button widgets, the desktop app and Bluetooth keys |
| `pm` | pomodoro | `p` work minutes 1..180 (25), `b` break 1..60 (5), `c` | tap start/pause, hold reset; beep + cat when work ends |
| `cd` | countdown | `p` time of day in minutes 0..1439 (required), `s` caption, `z`, `c` | counts to that time, then to the same time tomorrow |
| `sw` | stopwatch | `z`, `c` | tap start/stop, hold reset |
| `dt` | date | `k` format 0 `Thu 29 Oct`, 1 `2026-10-29`, 2 `October 29`; `z`, `a`, `c` | date from packet `0x12` (the web page sends it every minute) |
| `fs` | focus sign | `s` busy text (`Focus`) | tap busy/available; the board LED follows (red / green) while it is in the layout |
| `bt` | button | `e` action (required), `s` label (required), `g` argument, `u` press sound 0..4 (feature level 3), `z`, `c`, `r` | actions below |
| `sp` | Spotify controls | `c` | previous / play-pause / next, done by the web page (Spotify Premium) |
| `aa` | album art | | fixed 96x96; the cover the web page sends (JPEG, RAM only) |
| `rm` | reminder | `k` 0 water, 1 stretch, 2 breathe; `p` every 5..240 min (30), `z` | beep, cat smiles, note; tap = done |
| `pc` | picture | `i` slot 0..7 (required) | any size; the page crops and encodes your image to the box |
| `pt` | pet | `n` species 0..17 (4 = cat) | fixed 160x132; tap to pet it. See "Pets" |
| `gf` | animated GIF (feature level 4) | `i` GIF slot 0..3 (required) | plays at its own size (<= 128x128), centered in the box. See "GIF widget" |

**Button actions** (`e`): 0 none, 1 cat taps, 2 cat happy, 3 cat sleepy, 4 toggle C/F, 5 mute, 6 brightness step, 7 next wallpaper,
8 next page, 9 previous page, 10 pomodoro start/pause, 11 pomodoro reset, 12 focus sign, 13 stopwatch start/stop, 14 stopwatch reset,
15-17 Spotify play-pause / next / previous (web page), 18 event to the page (`g` = number, packet `0x13`), 19 Bluetooth key
(`g` = modifiers << 8 | HID usage; modifiers 1 Ctrl, 2 Shift, 4 Alt, 8 Cmd/Win), 20 Bluetooth media key (`g` 0 play/pause,
1 next, 2 previous, 3 volume up, 4 volume down, 5 mute). Bluetooth actions need the Bluetooth build (below); otherwise they show a note.

### GIF widget

*Firmware feature level 4.* A `gf` widget plays an animated GIF stored in one of **4 GIF slots** (`/gif/<slot>.gif` in LittleFS). The builder's
**+ GIF (animated)** asks for a file (animated GIF, animated WebP or PNG, or a plain photo: Chrome's `ImageDecoder` reads every frame),
then on **Send** re-encodes it in the browser to what the board can hold: **at most 128x128** (the widget box, cropped to fill), every
frame complete, one shared palette of up to 256 colors (median cut), at most **60,000 bytes** (the board takes 65,536). If it does
not fit the page first uses fewer colors (128, 64, 32), then every 2nd, 3rd, 4th frame (delays are added up so the speed stays), then
a smaller picture. A GIF the board already has (same crc) is not sent again; slots no widget uses are deleted.

On the board the decoder is bitbank2's [AnimatedGIF](https://github.com/bitbank2/AnimatedGIF) (Apache-2.0), one decoder object and one
RGB565 canvas per GIF on screen (about 24 KB + width x height x 2 bytes of heap; measured free heap with a 96x96 and a 128x128 GIF
running was never below 150 KB). **At most two GIFs per page**; a third is shown as a "GIF" placeholder, as is an empty slot. Frames are
decoded when their delay is up (never faster than 50 per second). Replacing or deleting a GIF that is on screen first closes its file.

**What the board checks before it keeps a file** (the page uses the same header rules): `GIF87a` or `GIF89a`, canvas 1..128 x 1..128,
the file ends with the `0x3B` trailer, at most 65,536 bytes; then it decodes every frame once with AnimatedGIF and refuses the file
(`invalid layout`, detail 1, and the slot is emptied again) if the decoder reports an error. Upload kind 4, `QUERY [4, slot]` (crc and
size in KB), `RESET [4, slot]`. Space is shared with the layout, pictures and sounds; the first GIF creates the `/gif` folder (8 KB).

Flash: the library adds about 9.4 KB to every build; the standard build still has about 91 KB left.

### Custom sounds

*Firmware feature level 3.* The web page's **Sounds** section turns any audio file into a sound the board can play and sends it to one
of **15 sound slots**. Your copy is stored as `/snd/<slot>.wav` in LittleFS, **next to the original files and never over them**
(`connect.wav`, `climate_*.wav` stay in the root of the partition). Playback prefers your copy; **Reset sound** deletes only your copy,
so the original (or the built-in beep) plays again.

| Slot | Plays when | Without a custom copy |
|------|------------|-----------------------|
| 0 Connected, 1 Disconnected | the desktop app or page connects / the connection drops | `connect.wav`, `disconnect.wav` |
| 2-7 Weather: sunny, cloudy, rainy, stormy, snowy, night | the weather group changes | `climate_<name>.wav` |
| 8 Pet tap | you tap the cat (any screen) or a pet widget | soft purr (cat), click (pet widget) |
| 9 Pomodoro end | a work or break period ends | two beeps |
| 10 Reminder | a reminder widget goes off | two beeps |
| 11-14 Button sound 1-4 | a button widget with `"u":1`..`4` is tapped | the click |

A button with `u` set plays that sound *instead of* the click (the click is muted for 0.4 s so the two do not overlap); the button's
action still runs. `u` is ignored on other widget types, `0` is the default click.

**Format the board accepts** (checked on the board before anything is kept, the same rules as the page): a canonical WAV: 44-byte
header (`RIFF`, `fmt ` 16 bytes, `data` to the end of the file), PCM, **mono, 16-bit, 8000-24000 Hz**, an even number of data bytes,
at most **160,044 bytes** (5 s at 16 kHz). The page always writes 16 kHz (or 11.025 / 8 kHz if you pick a lower quality: half the
space). Anything else is refused at END and the slot keeps what it had. Sounds are played by the existing audio task (8-bit DAC,
global volume and mute apply).

**Space**: the LittleFS partition (about 190 KB free with no pictures) is shared by the layout, pictures and sounds. The first sound
creates the `/snd` folder, which keeps 8 KB for good (measured: a LittleFS directory is two blocks); after that a 1 s sound at
16 kHz is 32 KB; the 5 s maximum takes 157 KB, so you can hold roughly six 1 s sounds, or one 5 s sound and little else. The board
refuses an upload before writing anything when the free space is below size + 8 KB (a replaced slot keeps its old copy until the new
one is complete, so replacing needs room for both). The page shows the free space and how many seconds it holds.

The page cuts the file (start, length <= 5 s), can trim silence at both ends, brings quiet sounds up to 90 % of full scale (never more
than 8x), fades the first and last 6 ms so there is no click, converts to mono and to the chosen rate with the browser's own
resampler, and lets you preview the converted result before it is sent.

### Pictures

The page turns your image into a *split JPEG* (16-row baseline JPEG strips, LVGL's SJPG format) at the exact widget or screen size,
strips the color profile Chrome adds, and uploads it as kind 1 to one of 8 slots (`/img/<slot>.sjpg`, at most 64 KB each). The board
test-decodes the picture before it accepts it, so a broken file is refused and the old one stays. A full-screen 240x320 picture is
about 25-35 KB and takes about 8 s to send. Pictures stay on the board until replaced or deleted (RESET with a slot).

### Pets

18 animated ASCII characters drawn in code (capybara, duck, goose, blob, cat, dragon, octopus, owl, penguin, turtle, snail, ghost,
axolotl, cactus, robot, rabbit, mushroom, chonk), from Anthropic's
[claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy) (MIT, see `sketch/src/pets/UPSTREAM.txt`). No image files:
each pose is a few lines of text. A pet idles with small happy motions, sleeps when the board does, gets busy while you type, celebrates
when a pomodoro ends, waves at reminders, and shows hearts when you tap it. Each pet on screen takes about 41 KB of heap (one per page
is the comfortable limit; a second one that does not fit is left out).

The standard build carries 6 species (capybara, duck, cat, penguin, axolotl, rabbit) to stay inside its 1.44 MB slot; others show the
cat. The WiFi and Bluetooth builds carry all 18 (ack flag 64). The page marks which species need those builds.

**Waiting cat**: Settings > More > "Waiting cat" picks *Cheerful* (default: waves, smiles, chases the mouse paw between 1 and 30 minutes
idle) or *Sleepy (original)*. Tapping the cat on any screen pets it (happy face, heart, purr).

### Bluetooth keyboard build

`pio run -d sketch -e esp32dev_ble` builds firmware that is also a Bluetooth keyboard ("TapTapPaw"): button actions 19 and 20 send
shortcuts and media keys to the paired computer, and the cat taps along. It uses the same partition table as the WiFi build
(`littlefs_noota.csv`: one 2.75 MB app slot, no OTA). The LittleFS partition (sounds, layout, pictures) and NVS keep the same offsets as
in the standard build, so they survive, but going from the standard build to this one does change the partition table: Export your
layout first. Pair it from the computer's Bluetooth settings.

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
| `0x0B` | BEGIN | `kind` (0 layout, 1 picture, 2 album art, 3 sound, 4 GIF), `slot` (picture 0..7, sound 0..14, GIF 0..3), `schema version`, `total u32 LE`, `crc32 u32 LE` (11 bytes) |
| `0x0C` | CHUNK | `seq u16 LE`, 1..192 data bytes. Chunk n holds bytes n*192 .. n*192+191; every chunk but the last is exactly 192 bytes |
| `0x0D` | END | empty: verify, validate, commit |
| `0x0E` | ABORT | empty: drop the upload in progress |
| `0x0F` | QUERY | empty: report state, no side effects (`detail` = feature level, 4 since Phase 4b); `[slot]`: `crc32` of that picture; `[3, slot]` / `[4, slot]`: `crc32` of that custom sound / GIF (0 = none) and its size in KB in `detail` |
| `0x11` | RESET | empty: delete the stored layout, back to the built-in screens; `[slot]`: delete that picture; `[3, slot]`: delete the custom copy of that sound (the original file is never touched); `[4, slot]`: delete that GIF |
| `0x14` | PLAY | `slot` 0..14: play that sound slot now, as the board would for its event (custom copy, else the original, else the built-in beep); acked with status 0 |
| `0x12` | DATE (host to board) | `year-2000`, `month`, `day`, `weekday 0..6` |
| `0x13` | EVENT (board to host) | `action`, `arg u16 LE`: a button the page acts on (Spotify, event); sent only while the page is the data source |
| `0x10` | ACK (board to host) | `cmd`, `status`, `detail`, `flags`, `seq u16`, `free_kb u16`, `crc32 u32` (12 bytes) |

`status`: 0 ok, 1 bad packet, 2 bad sequence (`seq` = the chunk the board wants), 3 too big, 4 checksum mismatch, 5 unsupported,
6 no space, 7 flash write error, 8 invalid layout (`detail` = reason, see `Err` in `layout_core.h`), 9 no upload in progress,
10 incomplete. `flags`: 1 layout active, 2 safe mode, 4 upload in progress, 8 layout stored, 16 Bluetooth build, 32 Bluetooth
paired, 64 all 18 pets. `seq` is the next chunk expected, `crc32`
the stored layout's checksum (0 = none), `free_kb` the free space of the LittleFS partition.

- **Retry**: resend the same packet if no ack arrives. A repeated CHUNK for the one just stored is acked without writing it again.
- **Resume**: BEGIN with the same kind/slot/size/crc as the upload in progress answers `seq` = the next missing chunk. A reset or power
  loss drops the partial upload (the stored layout is untouched); start again from chunk 0.
- **Verify**: after END, send QUERY: `crc32` must equal the checksum of what you sent.
- Pictures (kind 1) are at most 65,536 bytes, album art (kind 2, kept in RAM, not flash) at most 12,288 bytes, sounds (kind 3) at most
  160,044 bytes (and at least 46: a header and one sample). A sound's END ack carries the crc the board read back from flash; a file
  that is not a canonical mono 16-bit WAV in 8000-24000 Hz answers `invalid layout` with detail 0 and is not kept. GIFs (kind 4) are at most
  65,536 bytes and 128x128.
- Speed: about 3.9 KB/s (a 4 KB layout in about 1 s). Sending several chunks before waiting for acks was tried and lost bytes while the
  board writes to flash, so uploads stay stop-and-wait.
- A BEGIN larger than 4096 bytes, an unknown version or kind, or too little free space is refused before anything is written.

The page code is in `web-sync/index.html` (`validateLayout`, `layoutBeginPacket`, ..., `uploadLayoutBytes`); the firmware side is
`sketch/src/layout_core.cpp` (parser, protocol) and `sketch/src/layout.cpp` (screens, safe mode).

## Developer tools and tests

```
scripts/test-layout-core.sh          # firmware parser + upload state machine on the host (g++, ASan/UBSan, mutation fuzz)
node web-sync/test-layout.mjs        # page validator (same shared cases), packets, ack decoding, upload driver
node web-sync/test-packets.mjs       # existing packets, unchanged
node web-sync/test-gifs.mjs          # GIFs: encoder checked by an independent decoder, quantizer, size budgeting, gifCheck
node web-sync/test-sounds.mjs        # sounds: WAV encode/check, resampling, trim/normalize/fade, free-space maths, upload driver
~/.venvs/pio39/bin/python scripts/layout_smoke_test.py   # real board over USB (quit the desktop app first); ends with RESET
~/.venvs/pio39/bin/python scripts/layout_tool.py upload docs/layout-example.json
~/.venvs/pio39/bin/python scripts/layout_tool.py info | reset
~/.venvs/pio39/bin/python scripts/layout_tool.py sound clip.wav --slot 9 | soundreset --slot 9 | play --slot 9
```

`sketch/test/host/layout-cases.tsv` is a table of 128 layouts with the expected verdict; the firmware parser and the page validator must
both agree with every row.

## Measured size (build flags `-DINVERT_COLORS_DEFAULT=1`)

| | Before (`feature/wifi-mode`) | Phase 1 | Change |
|--|--|--|--|
| `esp32dev` flash (1,441,792 B partition) | 1,277,353 B (88.6 %) | 1,294,433 B (89.8 %) | +17,080 B, ~147 KB left |
| `esp32dev` RAM (static) | 57,552 B (17.6 %) | 59,752 B (18.2 %) | +2,200 B |
| `esp32dev_wifi` flash (2,883,584 B partition) | 1,950,537 B (67.6 %) | 1,966,785 B (68.2 %) | +16,248 B |
| `esp32dev_wifi` RAM (static) | 83,020 B (25.3 %) | 85,204 B (26.0 %) | +2,184 B |

Phase 4a (`feature/custom-sounds`, measured 2026-10-29):

| | Phase 3 | Phase 4a | Change |
|--|--|--|--|
| `esp32dev` flash (1,441,792 B) | 1,338,081 B (92.8 %) | 1,340,609 B (93.0 %) | +2.5 KB, ~101 KB left |
| `esp32dev` RAM (static) | 61,336 B | 61,488 B | +152 B |
| `esp32dev_wifi` flash (2,883,584 B) | 2,054,453 B (71.2 %) | 2,056,953 B (71.3 %) | +2.5 KB |
| `esp32dev_wifi` RAM (static) | 87,092 B | 87,228 B | +136 B |
| `esp32dev_ble` flash (2,883,584 B) | 1,713,653 B (59.4 %) | 1,716,265 B (59.5 %) | +2.6 KB |
| `esp32dev_ble` RAM (static) | 75,936 B | 76,080 B | +144 B |

Phase 3 (`feature/widgets-extras`, measured 2026-10-29):

| | Phase 2 | Phase 3 | Change |
|--|--|--|--|
| `esp32dev` flash (1,441,792 B) | 1,294,433 B (89.8 %) | 1,338,081 B (92.8 %) | +43.6 KB (6 pets), ~101 KB left |
| `esp32dev` RAM (static) | 59,752 B | 61,336 B | +1.6 KB |
| `esp32dev_wifi` flash (2,883,584 B) | 1,966,785 B (68.2 %) | 2,054,453 B (71.2 %) | +87.7 KB (18 pets) |
| `esp32dev_wifi` RAM (static) | 85,204 B | 87,092 B | +1.9 KB |
| `esp32dev_ble` flash (2,883,584 B) | | 1,713,653 B (59.4 %) | new build (Bluetooth keyboard, 18 pets) |
| `esp32dev_ble` RAM (static) | | 75,936 B | |
