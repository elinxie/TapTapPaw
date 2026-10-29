// Bluetooth keyboard for button widgets (BtnAction BA_BLE_KEY / BA_BLE_MEDIA). Only the esp32dev_ble build has it
// (-D ENABLE_BLE_KEYS=1); every other build answers false and the button shows a short note.
// The board advertises as "TapTapPaw Keys"; pair it once from the computer's Bluetooth settings.
#include <Arduino.h>

#ifndef ENABLE_BLE_KEYS
#define ENABLE_BLE_KEYS 0
#endif

#if ENABLE_BLE_KEYS
#include <BleKeyboard.h>

static BleKeyboard kb("TapTapPaw Keys", "TapTapPaw", 100);

void ble_keys_begin() { kb.begin(); }
bool ble_keys_available() { return true; }
bool ble_keys_connected() { return kb.isConnected(); }

// mods: HID modifier bits (1 Ctrl, 2 Shift, 4 Alt, 8 GUI), usage: HID keyboard usage id (0x04 = A ...)
bool ble_keys_send(uint8_t mods, uint8_t usage) {
  if (!kb.isConnected()) return false;
  KeyReport r = {};
  r.modifiers = mods;
  r.keys[0] = usage;
  kb.sendReport(&r);
  delay(12);
  kb.releaseAll();
  return true;
}

bool ble_media_send(uint16_t id) {
  static const MediaKeyReport *const keys[] = {&KEY_MEDIA_PLAY_PAUSE, &KEY_MEDIA_NEXT_TRACK, &KEY_MEDIA_PREVIOUS_TRACK,
                                               &KEY_MEDIA_VOLUME_UP, &KEY_MEDIA_VOLUME_DOWN, &KEY_MEDIA_MUTE};
  if (!kb.isConnected() || id >= sizeof keys / sizeof keys[0]) return false;
  kb.write(*keys[id]);
  return true;
}
#else
void ble_keys_begin() {}
bool ble_keys_available() { return false; }
bool ble_keys_connected() { return false; }
bool ble_keys_send(uint8_t, uint8_t) { return false; }
bool ble_media_send(uint16_t) { return false; }
#endif
