#pragma once
#include <Arduino.h>

// Idle behaviour of the display when no host (desktop app / web page) is connected.
//   IDLE_NEVER : stay on (default)
//   IDLE_DIM   : drop to the "dim" backlight level after idle_minutes, nothing is switched off
//   IDLE_SLEEP : display sleep (panel off, backlight 0) after idle_minutes
// A touch or any physical key press counts as activity and restores the display.
// This is separate from the sleepy-cat animation (animation.cpp, 60 s timer), which never touches the display.
enum IdleMode : uint8_t { IDLE_NEVER = 0, IDLE_DIM = 1, IDLE_SLEEP = 2 };
enum PowerState : uint8_t { POWER_ACTIVE = 0, POWER_DIMMED = 1, POWER_ASLEEP = 2 };

extern uint8_t idle_mode; // IdleMode
extern uint8_t idle_minutes; // 1, 2, 5, 10 or 30

void power_load(); // read idle settings from Preferences (call after load_config())
void power_save();
PowerState power_state();

// Call on every touch / key press. Wakes the display if needed.
// Returns true when the display was fully asleep: the caller must then swallow that input
// (no widget click, no paw tap), because it was only meant to wake the screen.
bool power_activity(bool host_connected);

// Force the display on (e.g. a host connected). Safe in any state.
void power_wake(bool host_connected);

// Call every loop pass. Applies dim / sleep when the idle time has run out.
void power_poll(bool host_connected);
