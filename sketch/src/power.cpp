#include "power.h"
#include "accessory.h"
#include <lvgl.h>

uint8_t idle_mode = IDLE_NEVER;
uint8_t idle_minutes = 5;

static PowerState state = POWER_ACTIVE;
static uint32_t last_activity = 0;

static uint8_t sane_minutes(uint8_t m) {
  return (m == 1 || m == 2 || m == 5 || m == 10 || m == 30) ? m : 5;
}

void power_load() {
  pref.begin("config", false);
  idle_mode = pref.getUChar("idle_mode", IDLE_NEVER);
  idle_minutes = sane_minutes(pref.getUChar("idle_min", 5));
  pref.end();
  if (idle_mode > IDLE_SLEEP) idle_mode = IDLE_NEVER;
  last_activity = millis();
  state = POWER_ACTIVE;
}

void power_save() {
  pref.begin("config", false);
  pref.putUChar("idle_mode", idle_mode);
  pref.putUChar("idle_min", idle_minutes);
  pref.end();
  last_activity = millis(); // changing the setting counts as activity
}

PowerState power_state() { return state; }

// Back to normal brightness. If the panel was asleep it is woken first.
static void to_active(bool host_connected) {
  bool was_asleep = (state == POWER_ASLEEP);
  if (was_asleep) {
    tft.wakeup();
    tft.writeCommand(0x29); // display on
  }
  tft.setBrightness(dim ? dim_brightness : brightness); // dim = light-sensor dimming, unrelated to idle
  if (was_asleep) {
    lv_obj_invalidate(lv_scr_act());
    if (host_connected) {
      led_color(1, 0, 1); // green: host connected
    } else {
      led_color(0, 1, 1); // red: no host
    }
  }
  state = POWER_ACTIVE;
}

static void to_dim() {
  tft.setBrightness(dim_brightness);
  state = POWER_DIMMED;
}

static void to_sleep() {
  led_color(1, 1, 1); // led off
  tft.writeCommand(0x28); // display off
  tft.sleep();
  tft.setBrightness(0); // backlight off
  state = POWER_ASLEEP;
}

bool power_activity(bool host_connected) {
  last_activity = millis();
  if (state == POWER_ACTIVE) return false;
  bool was_asleep = (state == POWER_ASLEEP);
  to_active(host_connected);
  return was_asleep;
}

void power_wake(bool host_connected) {
  last_activity = millis();
  to_active(host_connected);
}

void power_poll(bool host_connected) {
  uint32_t now = millis();
  if (host_connected) { // a connected host keeps the display on, as before
    last_activity = now;
    if (state != POWER_ACTIVE) to_active(true);
    return;
  }
  if (idle_mode == IDLE_NEVER) {
    if (state != POWER_ACTIVE) to_active(false); // setting changed while dimmed
    return;
  }
  bool expired = (now - last_activity) >= (uint32_t)idle_minutes * 60000UL;
  if (!expired) return;
  if (idle_mode == IDLE_DIM && state == POWER_ACTIVE) {
    to_dim();
  } else if (idle_mode == IDLE_SLEEP && state != POWER_ASLEEP) {
    to_sleep();
  }
}
