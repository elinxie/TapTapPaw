#pragma once
#include <Arduino.h>
#include <lvgl.h>
// state for non‑blocking animation handling
extern unsigned long last_sleep_check;
extern uint8_t evt, cpu, mem_used, battery, charging;

extern bool last_keyDown;
extern bool last_mouseMove;
extern bool last_mouseClick;
extern unsigned long last_chart_update;
extern lv_timer_t *random_mouse_timer;


//------------------------------------------

// sleepy animation (non-blocking)
// eyes blinking animation (non-blocking)
void sleepy_animation();

// cat mood, triggered by physical keys or the PC app (serial packet 0x06)
enum Mood : uint8_t { MOOD_NORMAL = 0, MOOD_HAPPY = 1, MOOD_SLEEPY = 2 };
void set_mood(uint8_t mood);
enum IdleStyle : uint8_t { IDLE_STYLE_SLEEPY = 0, IDLE_STYLE_CHEERFUL = 1 };
extern uint8_t idle_style; // what the cat does while nothing happens (Settings > More)
void idle_style_save();
void pet_init(); // after ui_init(): tap the cat to pet it
void pet_request(); // also used by the cat widgets (layout.cpp)
void pet_poll(); // loop()
void eye_blink_animation();
void random_mouse_task(lv_timer_t *timer);
// analog clock animation
void clock_animation(uint8_t h, uint8_t m, uint8_t s);

// chart animation
void chart_animation(uint8_t cpu, uint8_t mem_used, uint8_t battery, bool charging);

// media play status
void media_play_status(uint8_t mediaState);

// media track information
void media_track_status(char *title, char *artist);