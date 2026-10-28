# WiFi mode (standalone time and weather)

With WiFi mode the board gets the time (NTP) and the weather (Open-Meteo) **by itself**, so it works with only USB power and no
computer. That is all it does: no music/now-playing, no keyboard/mouse activity, no CPU or memory bars (those need a host: the desktop app
or the web page). Weather and clock are the two things it replaces.

It is an opt-in build, because WiFi does not fit the normal app partition.

## Build and flash

```
PLATFORMIO_BUILD_FLAGS="-DINVERT_COLORS_DEFAULT=1" ~/.venvs/pio39/bin/pio run -d sketch -e esp32dev_wifi -t upload
```

Quit the desktop app first (it holds the serial port). Plain `pio run -d sketch` still builds the normal, WiFi-free firmware
(`default_envs = esp32dev`), so nothing changes for anyone who does not ask for WiFi.

### What the partition change does

| | `esp32dev` (default) | `esp32dev_wifi` |
|--|--|--|
| Table | `littlefs_1mb_ota.csv` | `littlefs_noota.csv` |
| App partition | 1.44 MB (two slots for OTA) | 2.75 MB (one slot, no OTA) |
| `nvs` (settings, touch calibration) | 0x9000 | 0x9000 (same, kept) |
| `spiffs` (LittleFS sounds) | 0x2D0000 | 0x2D0000 (same, kept) |
| Firmware size | 1,277,353 bytes, 88.6% | 1,950,537 bytes, 67.6% |

Only the second OTA slot is removed (nothing in the code uses OTA). Flashing either env rewrites the partition table, and because `nvs`
and `spiffs` do not move, your settings and sounds are kept in both directions. To go back, flash the default env.

Measured on the board: free heap with the WiFi radio started is about 122 KB (largest free block about 78 KB).

## Set it up

1. Flash the `esp32dev_wifi` build.
2. Open the web page (see `WEB_SYNC.md`), connect the board, set the location in the Weather section, enter the WiFi name and password in
   **Board WiFi**, press **Send WiFi and location to board**. The board answers in the page log (`[wifi] connecting...`, `[wifi] connected, ip ...`).
3. Close the page. With no host connected the board switches WiFi on, shows the **WiFi symbol** (top right of the music panel), takes the time from NTP
   and the weather from Open-Meteo (refreshed every 15 minutes, retried every minute on failure).

Settings screen, **More >**: WiFi on/off, a status line, and **Forget WiFi**. There is no on-screen keyboard, so the network name,
password and location are entered from the web page only (over USB, once; they are stored on the board).

## Behaviour

- WiFi is **paused while a serial host is connected** (desktop app or web page) and resumes about two seconds after it goes away.
- The clock uses the time zone (including daylight saving) that Open-Meteo reports for the chosen location, so it matches the weather location, not the computer.
- The display's idle sleep (`IDLE_AND_WAKE.md`) is unaffected; WiFi keeps running while the display sleeps.
- Serial packets: credentials `[0xFF][0x09][len][ssid_len][ssid][pass_len][pass]` (empty ssid forgets), location `[0xFF][0x0A][len=8][lat int32 LE][lon int32 LE]` in
  degrees times 100000. The normal build ignores them and prints a note.

## Limits

- 2.4 GHz only, WPA2/WPA3-personal. **Company (802.1X) networks and networks with a sign-in page will not work.**
- The password is stored **unencrypted** in the board's flash, and the weather request is plain HTTP (the answer is only a temperature and a code).
- Not tested by hand yet: a real network connection, NTP sync and a live weather fetch (the smoke test used a fake network, which exercises parsing, the task, heap and forget).
  Possible interactions to watch for: audio crackle while the radio is active.
