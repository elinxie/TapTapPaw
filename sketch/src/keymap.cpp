#include "keymap.h"
#include "accessory.h"

uint8_t key_map[SLOT_COUNT];

void keymap_defaults() {
  key_map[SLOT_K1_TAP] = ACT_PAW_TAP;
  key_map[SLOT_K1_HOLD] = ACT_HAPPY;
  key_map[SLOT_K2_TAP] = ACT_PAW_TAP;
  key_map[SLOT_K2_HOLD] = ACT_SLEEPY;
  key_map[SLOT_COMBO] = ACT_INFO;
  key_map[SLOT_COMBO_HOLD] = ACT_RESET_TAPS;
}

static bool valid(const uint8_t *a, uint8_t n) {
  if (n != SLOT_COUNT) return false;
  for (uint8_t i = 0; i < n; i++) {
    if (a[i] >= ACT_COUNT) return false;
  }
  return true;
}

void keymap_load() {
  keymap_defaults();
  uint8_t buf[SLOT_COUNT];
  pref.begin("config", false);
  size_t got = pref.getBytes("keymap", buf, sizeof(buf));
  pref.end();
  if (got == sizeof(buf) && valid(buf, SLOT_COUNT)) {
    memcpy(key_map, buf, sizeof(buf));
  }
}

void keymap_save() {
  pref.begin("config", false);
  pref.putBytes("keymap", key_map, SLOT_COUNT);
  pref.end();
}

bool keymap_set_all(const uint8_t *actions, uint8_t n) {
  if (!valid(actions, n)) return false;
  memcpy(key_map, actions, SLOT_COUNT);
  keymap_save();
  return true;
}

uint8_t keymap_action(uint8_t key, uint8_t type) {
  if (key == 1 || key == 2) {
    if (type == KEY_EVT_PRESS) return key_map[key == 1 ? SLOT_K1_TAP : SLOT_K2_TAP];
    if (type == KEY_EVT_LONG) return key_map[key == 1 ? SLOT_K1_HOLD : SLOT_K2_HOLD];
  } else if (key == 0) {
    if (type == KEY_EVT_COMBO) return key_map[SLOT_COMBO];
    if (type == KEY_EVT_COMBO_LONG) return key_map[SLOT_COMBO_HOLD];
  } else if (type == KEY_EVT_PRESS) {
    return ACT_PAW_TAP; // Key3 (optional pin) is not mappable: it keeps the original tap
  }
  return ACT_NONE;
}
