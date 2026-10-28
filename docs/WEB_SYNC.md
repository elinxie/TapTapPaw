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

### What a browser page cannot do

| Missing | Why |
|---------|-----|
| Paw following your typing, mouse-cat | A browser only sees input inside its own tab, never system-wide key or mouse events. Use the desktop app. |
| CPU and memory bars | The browser does not expose them. The page sends 0, so those bars stay empty. |
| Now-playing from anything but Spotify | The Media Session API only covers audio the page plays itself. Spotify works through its Web API (above); everything else is typed in. |
| Battery | Only where Chrome offers the Battery API; otherwise the page sends 0. |
| Turning the board's keys into computer keystrokes | The ESP32 has no USB-HID, and a page cannot inject keystrokes anyway. |
| Keeping going with the tab closed or the computer asleep | The 1 Hz timer runs in a Web Worker so a background tab is not throttled, but closing the tab stops it. |

## Option B: ESP32 WiFi mode (not built: does not fit)

Measured with a throwaway probe that links what WiFi mode would need (STA connect, NTP, an HTTP GET to Open-Meteo, Preferences),
default partition table, `-DINVERT_COLORS_DEFAULT=1`:

| Build | Flash used | Of 1,441,792 byte app partition |
|-------|-----------|----------------------------------|
| Current firmware | 1,273,145 | 88.3% |
| + WiFi, NTP, plain HTTP | 1,930,469 | 133.9% (about 489 KB too big) |
| + WiFi, NTP, HTTPS (TLS) | 1,929,965 | 133.9% |

The WiFi stack alone is about 657 KB, and TLS adds almost nothing on top of it, so dropping HTTPS does not help
(Open-Meteo does answer plain HTTP). Fitting it needs either a bigger app partition (drops the unused OTA slot) or removing most of the
wallpaper images. Neither was done; see the PR description.

## Data source indicator

Top right of the music panel on the main screen:

| Shows | Meaning |
|-------|---------|
| USB symbol (green) | A serial feed, assumed to be the desktop app |
| `WEB` (green) | The web page (it sends source tag `0x07` every second) |
| WiFi symbol (green) | Reserved for a future WiFi mode; not produced today |
| Warning symbol (yellow) | Nothing connected |

Source packet, host to board: `[0xFF] [0x07] [len=1] [src]` with `src` 1 = desktop app, 2 = web page. Hosts that never send it
(the current desktop app) stay on the USB symbol. The setting is forgotten when the connection drops.
