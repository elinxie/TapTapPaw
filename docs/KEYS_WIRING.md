# Physical keys (MX switches) on the CYD 2.8"

Press a key wired to the board and the cat taps and clicks, with or without the PC app running.
The ESP32 (classic) has no native USB-HID, so these keys do **not** type on your computer; they drive the cat.

## Free pins (from the ESP32-2432S028R pinout)
Source: [Random Nerd Tutorials – CYD pinout](https://randomnerdtutorials.com/esp32-cheap-yellow-display-cyd-pinout-esp32-2432s028r/)

| Connector | Pins |
|-----------|------|
| CN1 | GND, **IO22**, **IO27**, 3V3 |
| P3  | GND, **IO35**, **IO22**, IO21 (IO21 = backlight, do not use) |

Default config uses **IO22 + IO27 on CN1** (both have internal pull-ups). IO35 is input-only with no internal pull-up, so a
third key there needs an external 10k resistor to 3V3 (set `KEY3_PIN 35` in `sketch/include/app_config.h`).
Verify against the silkscreen on your board before powering up; some "purple" capacitive variants differ.

## Parts
4-pin cable (plugs into CN1, 4 female ends) · male-to-male jumpers · Durock MX hotswap sockets · MX switches + keycaps.

## Wiring (2 keys)
Each switch has two pins (polarity does not matter). One pin to **GND**, the other to the GPIO.

```
CN1 cable end   ->  jumper  ->  hotswap socket pad
GND   ------------------------- Key1 pin A ----+
                                                |-- shared GND (second jumper from GND to Key2 pin A)
IO22  ------------------------- Key1 pin B
IO27  ------------------------- Key2 pin B
3V3   -> leave unconnected (taped); never touch it to GND
```

- Hotswap sockets have solder tabs: solder a jumper (cut one end) or wire to each pad. A bare dupont pin will not hold on the tab.
- Seat the MX switch in the socket after soldering; add keycaps last.
- GND needs to reach both switches: either use two jumpers from one GND female end (twist/solder) or daisy-chain one pad to the other.

## Firmware settings (`sketch/include/app_config.h`)
`KEYS_ENABLED`, `KEY1_PIN` (22), `KEY2_PIN` (27), `KEY3_PIN` (-1 off), `KEY_DEBOUNCE_MS` (25).

## Key mapping (customizable)
Settings screen (swipe down on the main screen) -> **More >** -> **Keys**. Six gestures, each with its own action list:
Key1 tap, Key1 hold, Key2 tap, Key2 hold, Key1+Key2 together, Key1+Key2 held. Key3 (if you enable it) stays a plain tap and is not mappable.
Choices apply immediately and are stored on the board (Preferences `config` / `keymap`). **Reset keys** restores the defaults below.

| Action | Effect |
|--------|--------|
| None | Nothing on the device |
| Paw tap | Paw tap + click, lifetime tap counter +1 |
| Happy / Sleepy | Happy cat (eyes shut for a moment) / sleepy cat (until next activity) |
| Toggle C/F | Same as tapping the weather panel; saved |
| Toggle invert | Flips the color inversion; saved |
| Mute | Sound effects on/off; saved, updates the Sound Effect switch |
| Brightness step | Next backlight step (10%, 30%, 50%, 75%, 100%), wraps; saved, moves the slider |
| Wallpaper next | Next wallpaper; the next weather group change replaces it again |
| App event only | Nothing on the device (same as None, see below) |
| Info toast | Firmware version and tap count |
| Reset taps | Reset the tap counter |

**The key event packet (`0x05`, below) is sent for every gesture whatever it is mapped to**, so an app always sees the physical press.
Because of that, "None" and "App event only" behave the same; both are offered so the intent is readable in the list.

The mapping can also be set from outside with a packet (the web page has a "Key mapping" section for it):
`[0xFF] [0x08] [len=6] [K1 tap] [K1 hold] [K2 tap] [K2 hold] [combo] [combo hold]`, each value the action number in the table order above
(0 None, 1 Paw tap, 2 Happy, 3 Sleepy, 4 Toggle C/F, 5 Toggle invert, 6 Mute, 7 Brightness step, 8 Wallpaper next, 9 App event only,
10 Info toast, 11 Reset taps). A packet with a wrong length or any value above 11 is ignored as a whole.

## Default key actions
| Input | Action |
|-------|--------|
| Tap Key1 / Key2 / Key3 | Paw tap + click; lifetime tap counter +1 (saved to flash after `TAP_SAVE_IDLE_MS` idle) |
| Hold Key1 (`KEY_LONG_MS`) | Happy cat (eyes shut for a moment) |
| Hold Key2 (`KEY_LONG_MS`) | Sleepy cat (until next activity) |
| Key1 + Key2 together | Toast with firmware version and tap count |
| Key1 + Key2 held (`KEY_COMBO_LONG_MS`) | Reset tap counter |

## Serial protocol for other TapTapPaw apps
Key events are sent to a connected app when `KEY_SERIAL_EVENTS` is 1:

`[0xFF] [0x05] [len=6] [key] [type] [count: uint32 LE]`

- `key`: 1..3 = KEY1..KEY3, 0 = two-key combo
- `type`: 1 press, 2 long press, 3 combo, 4 combo long (the gesture, not the action: by default it resets the counter, but it can be mapped to anything)

An app can set the cat's mood with `[0xFF] [0x06] [len=1] [mood]` (0 normal, 1 happy, 2 sleepy).

Other packets: source tag `0x07` (see `WEB_SYNC.md`) and key map `0x08` (above).
While the display is asleep (`IDLE_AND_WAKE.md`) no app is connected, so no key event is sent for the press that wakes it.
