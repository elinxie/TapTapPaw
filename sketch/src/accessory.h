#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include "LGFX_CYD.h"
extern LGFX tft; /* LGFX instance */
extern Preferences pref;

#define BUTTON_PIN 0
#define LDR_PIN 34
#define LED_RED_PIN 4
#define LED_GREEN_PIN 16
#define LED_BLUE_PIN 17

#define SD_MOSI 23
#define SD_MISO 19
#define SD_SCK 18
#define SD_CS 5

#define SPEAKER_PIN 26
#define SPEAKER_CH 2


//-----------------------------
extern uint8_t volume;
extern bool sound_on;
extern bool flip;
extern uint8_t rotation;
extern bool dim; // back light dim flag
extern uint8_t dim_brightness;
extern uint8_t brightness;
extern bool invert_flip; // flips the board's default color inversion
extern bool use_fahrenheit; // temperature unit
//-----------------------------

void save_display_prefs(); // persist invert_flip + use_fahrenheit (no beep)

// physical keys (MX switches on CN1/P3)
void keys_init();

// One event per keys_poll() call. key = 1..3, or 0 for the two-key combo.
enum KeyEventType : uint8_t {
  KEY_EVT_PRESS = 1,       // key went down (debounced)
  KEY_EVT_LONG = 2,        // key held for KEY_LONG_MS (fires once per hold)
  KEY_EVT_COMBO = 3,       // Key1 + Key2 both down
  KEY_EVT_COMBO_LONG = 4   // Key1 + Key2 held for KEY_COMBO_LONG_MS
};
struct KeyEvent {
  uint8_t key;
  uint8_t type;
};
bool keys_poll(KeyEvent &ev); // false = nothing new
void keys_swallow_held(); // drop the press that is held right now (it only woke the display): no tap, no long-press

uint32_t tap_count_load(); // lifetime physical-key tap count, kept in flash
void tap_count_save(uint32_t count);

void initSpeaker();

// playToneWithVolume(2000, 20, 200);  // 20% volume, 2000 Hz, 200ms
void playToneWithVolume(uint16_t freq, uint8_t volumePercent, uint16_t duration);

void audioPauseForTone();
void audioResumeAfterTone();

void beepbeep();
void beep();
void clickSound();
void offSound();


void autoDim();

// led_onoff function
void led_color(bool r, bool g, bool b  );

//------------- configuration
void load_config();
void save_config();

void initLittleFS();
void lvgl_littlefs_init();
