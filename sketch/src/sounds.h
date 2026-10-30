#pragma once
// Custom sounds (docs/WIDGETS.md, "Custom sounds"). The web page uploads a WAV for a sound slot (upload kind 3); it is kept as
// /snd/<slot>.wav in LittleFS, next to and never over the original files (connect.wav, climate_*.wav). Playback prefers the custom copy
// and falls back to the original, then to the board's built-in beep.
#include <stdint.h>
#include "layout_core.h"

void sounds_init(); // after LittleFS is mounted: remove a cut-off upload's temp file, find which custom copies exist
bool sound_has_custom(uint8_t slot);
void sounds_refresh(uint8_t slot); // a custom copy was stored or removed
bool sound_play(uint8_t slot); // queue the custom copy, else the slot's original file; false = there is nothing to play (the caller beeps)
bool sound_play_event(uint8_t slot); // what the board does for the slot's event: sound_play, else its built-in beep (PLAY packet, tests)
