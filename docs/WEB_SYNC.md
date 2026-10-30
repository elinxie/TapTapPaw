# Sync without the desktop app

Two ways to feed the board when the TapTapPaw desktop software is not running.

## Option A: web page (`web-sync/index.html`)

A single static HTML file, no build step, no server needed. It uses the Web Serial API, so it needs **Chrome or Edge on a computer**
(not Safari, Firefox or a phone). Open the file directly, or serve the folder (`python3 -m http.server`) and open
`http://localhost:<port>/web-sync/index.html`.

1. Quit the TapTapPaw desktop app. Only one program can hold the serial port.
2. Open the page, press **Connect to board**, pick the `usbserial` port. The board usually reboots once when the port opens.
3. Time is sent every second. For weather press **Use my location** (or type lat/lon). It refreshes every 15 minutes.
4. Music title/artist are typed in by hand and sent with **Send music**.
5. **Dry run** logs every packet without a board, handy for checking the page.

It sends the same packets as the desktop app (status `0x01`, music `0x02`, weather `0x03`, settings `0x04`, mood `0x06`) plus a new source tag
(`0x07`, below). Settings are only sent when you press **Apply to board**, so the board keeps its own saved values otherwise.
Key events the board sends (`0x05`) are decoded into the page log. The **Key mapping** section sets what each key gesture does on the
board (`0x08`, see `KEYS_WIRING.md`).

`node web-sync/test-packets.mjs` checks the packet bytes against the layouts used in `app/core/taptappaw.js`.

### Spotify now-playing (optional)

The page can read what your Spotify account is playing (any device) through Spotify's Web API and show it on the board. It uses
the Authorization Code flow with PKCE, so there is no client secret anywhere; tokens stay in this browser's localStorage and **Sign out** clears them.
It only sees Spotify. Other players (Apple Music, YouTube, local players) are not visible to a web page.

Setup, once:
1. On <https://developer.spotify.com/dashboard> create an app (Web API). Spotify's rules since February 2026: **the account that owns the app needs Premium**,
   an app has at most **5 authorized users** (add anyone else under User Management), and one app per developer.
2. Register the exact redirect URI the page shows. Spotify accepts only `https://...` or `http://127.0.0.1:<port>/...`, never `file://` or `localhost`,
   so open the page from its https address (e.g. GitHub Pages, see below) or serve it on `127.0.0.1`.
3. Paste the app's Client ID into the page, press **Sign in to Spotify** (a popup, so the board connection stays up), approve.
4. Connect the board. While signed in and connected the page polls every 4 seconds and overwrites the typed title/artist.

If Spotify answers 403, the signed-in account is not on the app's allowlist or the owner's Premium has lapsed.

### Publishing the page (GitHub Pages)

The page is a single file, so it is published from a small `gh-pages` branch that holds only `index.html` and `.nojekyll`:
`https://<user>.github.io/<repo>/`. To republish after changing the page:

```
git worktree add /tmp/ttp-pages gh-pages
git show <branch>:web-sync/index.html > /tmp/ttp-pages/index.html
git -C /tmp/ttp-pages commit -am "Update web-sync page" && git -C /tmp/ttp-pages push
git worktree remove /tmp/ttp-pages
```

### Widget builder (packets `0x0B`-`0x11`)

The **Widget builder** section designs the board's home screen: pages of widgets, backgrounds, templates, export/import, and
**Send to board** (see `WIDGETS.md`). It sends nothing until you press Send, Check or Reset, and the existing packets are unchanged.
It needs firmware with the layout engine; older firmware does not answer and the page says so.

While connected the page also sends the date every minute (packet `0x12`, for the Date widget), sends album art when the Spotify track
changes, and acts on Spotify buttons tapped on the board (packet `0x13`; needs the `user-modify-playback-state` scope, so sign in to
Spotify again once, and Spotify Premium).

`node web-sync/test-layout.mjs` and `node web-sync/test-editor.mjs` test the layout code and the editor model.

### Timer button, note and next event

Three more builder widgets (no uploads): **Timer button** (tap to start a preset timer, tap again to cancel; the end plays the Reminder sound),
**Note / quote** (up to 100 bytes, wraps) and **Next event** (type the day as `09:30 Standup|13:00 Lunch`; the preview uses this computer's clock, the
board the clock this page sends). See `WIDGETS.md`.

### GIF widget (upload kind 4, packets `0x0F` / `0x11` with `[4, slot]`)

**+ GIF (animated)** in the builder asks for an animated GIF, WebP or PNG (or a photo), keeps the source in this browser (up to 250 KB
in localStorage; bigger ones last until the page is closed) and shows it animated in the preview. **Send to board** converts it to at most
128x128 and 60 KB (see `WIDGETS.md`, "GIF widget"), uploads it after the pictures and before the layout, and deletes GIF slots no widget
uses. Needs Chrome (`ImageDecoder`) for animation; other browsers get the first frame only. `node web-sync/test-gifs.mjs` tests the encoder.

### Sounds (upload kind 3, packets `0x0F` / `0x11` with `[3, slot]`, `0x14`)

The **Sounds** section (under the builder) lists the board's 15 sound slots: connected, disconnected, six weather sounds, pet tap,
pomodoro end, reminder and four button sounds. For each slot:

1. **Choose file** takes any audio the browser can decode (wav, mp3, m4a, ogg, flac...). The page converts it in the browser (nothing
   leaves your computer): mono, 16 kHz (or 11 kHz / 8 kHz to save space), cut with **Start** and **Length** (at most 5 s), optional
   **trim silence** and **make it loud**. The waveform shows the part that is kept.
2. **Preview** plays the *converted* result, so what you hear is what the board gets.
3. **Use and send to board** (or **Use**, then **Send** later) uploads it. The page asks the board for its feature level and free space
   first and says so if the sound does not fit. A progress bar shows the upload (about 3.9 KB/s: a 1 s sound takes 8 s, 5 s takes 41 s).
4. **Play on board** plays the slot on the board the way it would for its event (your copy, else the original, else the built-in beep).
5. **Reset sound** deletes only your copy from the board (after asking). The original sound or the built-in beep plays again.

**Check board** asks the board which slots hold a custom sound (and how big) and shows the free storage, which pictures share. Names of
sounds are remembered per browser for the sounds it sent; a sound sent from somewhere else shows as "sent from somewhere else".
The audio itself is not kept in the browser: the board is the store (choose the file again to change a sound).

Button sounds: select a Button widget in the builder and set **Press sound** to Custom 1..4. The layout then needs firmware
feature level 3; the page checks before sending.

`node web-sync/test-sounds.mjs` tests the conversion code and the upload driver.

### What a browser page cannot do

| Missing | Why |
|---------|-----|
| Paw following your typing, mouse-cat | A browser only sees input inside its own tab, never system-wide key or mouse events. Use the desktop app. |
| CPU and memory bars | The browser does not expose them. The page sends 0, so those bars stay empty. |
| Now-playing from anything but Spotify | The Media Session API only covers audio the page plays itself. Spotify works through its Web API (above); everything else is typed in. |
| Battery | Only where Chrome offers the Battery API; otherwise the page sends 0. |
| Turning the board's keys into computer keystrokes over USB | The ESP32 has no USB-HID, and a page cannot inject keystrokes. The `esp32dev_ble` build sends them over Bluetooth instead (see `WIDGETS.md`). |
| Keeping going with the tab closed or the computer asleep | The 1 Hz timer runs in a Web Worker so a background tab is not throttled, but closing the tab stops it. |

## Option B: ESP32 WiFi mode (opt-in build)

The board can fetch time and weather itself. It does not fit the normal app partition (measured: WiFi + NTP + HTTP was 1,930,469 bytes,
133.9% of the 1,441,792 byte partition; TLS added almost nothing on top of the WiFi stack), so it is a separate build with a larger
partition table. See `WIFI_MODE.md`. The web page has a **Board WiFi** section to send the network name, password and location.

## Data source indicator

Top right of the music panel on the main screen:

| Shows | Meaning |
|-------|---------|
| USB symbol (green) | A serial feed, assumed to be the desktop app |
| `WEB` (green) | The web page (it sends source tag `0x07` every second) |
| WiFi symbol (green) | No host, and the board is on WiFi (WiFi build only) |
| Warning symbol (yellow) | Nothing connected |

Source packet, host to board: `[0xFF] [0x07] [len=1] [src]` with `src` 1 = desktop app, 2 = web page. Hosts that never send it
(the current desktop app) stay on the USB symbol. The setting is forgotten when the connection drops.
