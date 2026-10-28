#pragma once
#include <Arduino.h>

// What a physical key gesture does on the device. The key event packet (0x05) is sent to a connected app for every
// gesture regardless of the mapping; the mapping only decides the local action.
enum KeyAction : uint8_t {
  ACT_NONE = 0, // nothing locally
  ACT_PAW_TAP, // paw tap + click, tap counter +1
  ACT_HAPPY, // happy cat
  ACT_SLEEPY, // sleepy cat
  ACT_TOGGLE_UNIT, // C / F
  ACT_TOGGLE_INVERT, // flip color inversion
  ACT_MUTE, // sound on / off
  ACT_BRIGHTNESS_STEP, // next backlight step, wraps around
  ACT_WALLPAPER_NEXT, // next wallpaper (until the weather group changes)
  ACT_APP_ONLY, // nothing locally, event goes to the app (same effect as None: events are always sent)
  ACT_INFO, // toast with firmware version and tap count
  ACT_RESET_TAPS, // reset lifetime tap counter
  ACT_COUNT
};

// Same order as KeyAction, for an LVGL dropdown.
#define KEY_ACTION_OPTIONS \
  "None\nPaw tap\nHappy\nSleepy\nToggle C/F\nToggle invert\nMute\nBrightness step\nWallpaper next\nApp event only\nInfo toast\nReset taps"

enum KeySlot : uint8_t { SLOT_K1_TAP = 0, SLOT_K1_HOLD, SLOT_K2_TAP, SLOT_K2_HOLD, SLOT_COMBO, SLOT_COMBO_HOLD, SLOT_COUNT };

extern uint8_t key_map[SLOT_COUNT];

void keymap_defaults(); // fills key_map with the original behaviour (does not save)
void keymap_load(); // from Preferences, falls back to defaults if missing or invalid
void keymap_save();
bool keymap_set_all(const uint8_t *actions, uint8_t n); // validates, applies and saves; false if invalid

// key: 1..3 or 0 for the two-key combo; type: KeyEventType (accessory.h). Returns the action to run.
uint8_t keymap_action(uint8_t key, uint8_t type);
