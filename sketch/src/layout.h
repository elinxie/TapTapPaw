#pragma once
// Widget layout engine (docs/WIDGETS.md). The board renders a data-driven layout (layout_core.h schema) stored in LittleFS.
// No layout = the original SquareLine screens, untouched. A bad layout can never lock the board out: see "safe mode" in layout.cpp.
#include <Arduino.h>

void layout_begin(bool boot_held); // setup(), after ui_ext_init(). boot_held = BOOT button was down at power-up
void layout_poll(); // loop(): applies a freshly uploaded layout
bool layout_packet_id(uint8_t id); // 0x0B..0x0F and 0x11 belong to the layout upload protocol
void layout_packet(const uint8_t *pkt, uint8_t len); // full frame [0xFF][id][len][payload]; answers with a 0x10 ack on Serial
bool layout_active();
const char *layout_status(); // short text for the settings screen
void layout_reset(); // delete the stored layout, back to the built-in screens

// data feeds, called by main.cpp / animation.cpp whenever a value is known (cheap)
void layout_feed_time(uint8_t h, uint8_t m, uint8_t s);
void layout_feed_weather(uint8_t group, int8_t temp_c, uint8_t humidity, uint8_t hh, uint8_t mm);
void layout_feed_music(const char *title, const char *artist);
void layout_feed_media_state(uint8_t state);
void layout_feed_system(uint8_t cpu, uint8_t mem, uint8_t batt, bool charging);
void layout_feed_taps(uint32_t count);
void layout_feed_mood(uint8_t mood);
void layout_on_wallpaper(uint8_t idx); // weather (or the wallpaper key action) picked wallpaper idx
void layout_sync_cat();
void layout_feed_date(uint16_t year, uint8_t mon, uint8_t day, uint8_t wday); // packet 0x12
int8_t layout_focus_led(); // -1 no focus sign, 1 busy (red LED), 0 available (green LED)
 // typing cat widgets copy the original cat's paw/eyes/sleep state now (paw_tap calls it)
