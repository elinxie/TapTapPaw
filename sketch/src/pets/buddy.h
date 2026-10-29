#pragma once
// Interface the species files in species/ expect (from claude-desktop-buddy, MIT, see LICENSE-claude-desktop-buddy.txt).
#include <stdint.h>

// Per-species state function: renders the pet for animation tick t (one tick = 200 ms) into the shared sprite.
typedef void (*StateFn)(uint32_t t);

struct Species {
  const char *name;
  uint16_t bodyColor; // RGB565
  StateFn states[7]; // 0 sleep, 1 idle, 2 busy, 3 attention, 4 celebrate, 5 dizzy, 6 heart
};
