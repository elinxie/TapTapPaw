// Custom sound slots: which slots have a custom copy, and which file a slot plays. See sounds.h.
#include "sounds.h"
#include "accessory.h"
#include <Arduino.h>
#include <sys/stat.h>
#include <stdio.h>

void playAudio(const char *filename); // main.cpp: queue a file below /littlefs for the audio task

static const char *const ORIGINAL[lc::SND_ORIGINALS] = {"connect.wav", "disconnect.wav", "climate_sunny.wav", "climate_cloudy.wav",
                                                       "climate_rainy.wav", "climate_stormy.wav", "climate_snowy.wav", "climate_night.wav"};
static uint16_t custom_mask = 0; // bit n: /snd/<n>.wav exists

static bool file_there(const char *vfs_path) {
  struct stat s;
  return stat(vfs_path, &s) == 0;
}

void sounds_refresh(uint8_t slot) {
  if (slot >= lc::SND_COUNT) return;
  char p[32];
  snprintf(p, sizeof p, "/littlefs/snd/%u.wav", (unsigned)slot);
  if (file_there(p)) custom_mask |= (uint16_t)(1u << slot);
  else custom_mask &= (uint16_t)~(1u << slot);
}

void sounds_init() {
  custom_mask = 0;
  for (uint8_t i = 0; i < lc::SND_COUNT; i++) sounds_refresh(i);
}

bool sound_has_custom(uint8_t slot) { return slot < lc::SND_COUNT && (custom_mask & (1u << slot)); }

bool sound_play(uint8_t slot) {
  if (slot >= lc::SND_COUNT) return false;
  if (sound_has_custom(slot)) {
    char name[16];
    snprintf(name, sizeof name, "snd/%u.wav", (unsigned)slot);
    log_i("sound slot %u -> %s", (unsigned)slot, name);
    playAudio(name);
    return true;
  }
  if (slot < lc::SND_ORIGINALS) {
    log_i("sound slot %u -> %s", (unsigned)slot, ORIGINAL[slot]);
    playAudio(ORIGINAL[slot]);
    return true;
  }
  return false;
}

bool sound_play_event(uint8_t slot) {
  if (slot >= lc::SND_COUNT) return false;
  if (sound_play(slot)) return true;
  log_i("sound slot %u -> built-in beep", (unsigned)slot);
  if (slot == lc::SND_POMO_END || slot == lc::SND_REMINDER) beepbeep();
  else clickSound(); // pet tap and the button sounds
  return true;
}
