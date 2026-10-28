# Running TapTapPaw yourself

Quick path: `./scripts/setup-dev.sh`

## Desktop app (Electron)
```
cd app
npm install     # postinstall runs electron-rebuild for uiohook-napi / serialport
npm run dev
```
macOS: grant **Accessibility** and **Input Monitoring** to the app that launches it (Terminal / VS Code / Electron).
Tray menu: pick your port (e.g. `/dev/tty.usbserial-10`), then use **Temperature Unit** and **Invert Display Colors**.

## Firmware (PlatformIO)
```
cd sketch
# set BOARD in include/app_config.h (USE_PURPLE_28_CAPACITIVE | USE_CYD_28_1 | USE_CYD_28_2)
pio run                                              # build
pio run -t upload   --upload-port /dev/tty.usbserial-10   # firmware
pio run -t uploadfs --upload-port /dev/tty.usbserial-10   # sounds (LittleFS), first time
```
Disconnect the desktop app first; it holds the serial port.

## Negative / inverted colors
Quickest: tray menu -> **Invert Display Colors**. Permanent: set `INVERT_COLORS_DEFAULT 1` in `app_config.h`, or try the
other board constant (`USE_CYD_28_1` runs the panel without inversion).

## Serial protocol additions
Settings packet: `[0xFF][0x04][len=1][flags]`, flags bit0 = Fahrenheit, bit1 = flip color inversion. Stored on the device.
Weather temperature byte is now signed (two's complement) so sub-zero values display correctly.
