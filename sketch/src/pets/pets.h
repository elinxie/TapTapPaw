#pragma once
// Animated ASCII pets for the "pet" widget. The 18 species and their animations come from Anthropic's claude-desktop-buddy
// (MIT, code-drawn ASCII art; see pets/LICENSE-claude-desktop-buddy.txt and pets/UPSTREAM.txt). No image assets.
#include <lvgl.h>

constexpr int PET_W = 160, PET_H = 132; // drawn at 2x: 12 columns of 6x8 text = 144 px, 5 rows = 80 px plus particles

enum PetMood : uint8_t { PET_SLEEP = 0, PET_IDLE, PET_BUSY, PET_ATTENTION, PET_CELEBRATE, PET_DIZZY, PET_HEART };

uint8_t pets_count();
bool pets_all(); // false: this build carries 6 of the 18 species (see pets.cpp)
const char *pets_name(uint8_t i);
lv_obj_t *pet_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, uint8_t species); // freed with its LVGL object
void pets_tick(); // every 200 ms: advance and redraw the pets on screen

// what the board is doing, for the pets' mood
void pets_busy(); // a key / paw tap: typing for a moment
void pets_petted(); // tapped on screen: heart, or dizzy when petted very fast
void pets_event(uint8_t mood, uint32_t ms); // celebrate (pomodoro done), attention (reminder) ...
