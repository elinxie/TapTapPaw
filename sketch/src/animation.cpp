#include "animation.h"
#include "accessory.h"
#include "layout.h"
void cat_heart(); // layout.cpp
#include "ui/ui.h"
//=========================== custom animation task ===========================

// state for non‑blocking animation handling
unsigned long last_sleep_check = 0;
bool current_sleep_state = false;
bool last_sleep_state = false;
#define SLEEP_TIMER 60000 // sleep state after 1 minute
// state for non‑blocking blink handling
long eyelid_show_timer = 0; // when the eyelid was made visible
bool eyelid_visible = false; // are we currently showing the eyelid

bool last_keyDown = false;
bool last_mouseMove = false;
bool last_mouseClick = false;
unsigned long eye_blink_timer = 0; // next time we may blink
unsigned long delayTime = 0; // random delay until next blink

uint8_t last_hour = 0;
uint8_t last_min = 0;
uint8_t last_sec = 0;
uint8_t last_mediaState = 3; // invalid initial state to ensure first update occurs

// Chart data - single series with 4 points: [cpu, mem, batt, charge]
static lv_chart_series_t *chart_data_series = NULL;
uint8_t evt, cpu, mem_used, battery, charging = 0;

static uint8_t last_charge = true;
unsigned long last_chart_update = 0;
// ---------- Timer handle ----------
lv_timer_t *random_mouse_timer = NULL;

//------------------------------------------

// ---- waiting style: SLEEPY = the original (falls asleep after a minute), CHEERFUL = plays while it waits (waves,
// smiles with a heart, bats the mouse) and only falls asleep after 30 minutes. Settings > More > "Waiting cat".
uint8_t idle_style = IDLE_STYLE_CHEERFUL;
static bool sleepy_forced = false; // the sleepy mood (key or app) still means "sleep now"
#define CHEER_SLEEP_MS (30UL * 60000UL)
static uint8_t play_kind = 0, play_step = 0;
static uint32_t play_next = 0, play_at = 0;
static void paw_show(bool up) {
  if (up) {
    lv_obj_clear_flag(ui_main_Image_rightpawup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_main_Image_rightpawdown, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(ui_main_Image_rightpawdown, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_main_Image_rightpawup, LV_OBJ_FLAG_HIDDEN);
  }
}
static void play_stop() {
  if (play_kind) paw_show(false);
  play_kind = 0;
}
static void play_done(uint32_t now) {
  play_kind = 0;
  play_next = now + 3500 + esp_random() % 6000;
}
void mouse_wiggle(); // below
static void play_tick(uint32_t now) {
  if (!play_kind) {
    if ((int32_t)(now - play_next) < 0) return;
    play_kind = 1 + esp_random() % 3;
    play_step = 0;
    play_at = now;
  }
  if ((int32_t)(now - play_at) < 0) return;
  switch (play_kind) {
    case 1: // wave the right paw: up/down three times
      paw_show(play_step % 2 == 0);
      play_at = now + 160;
      if (++play_step >= 6) {
        paw_show(false);
        play_done(now);
      }
      break;
    case 2: // smile, with a heart
      {
        unsigned long keep = last_sleep_check; // set_mood restarts the idle timer; keep it, so the 30 minute limit still counts
        set_mood(MOOD_HAPPY);
        last_sleep_check = keep;
      }
      cat_heart();
      play_done(now);
      break;
    default: // bat the mouse a few times
      mouse_wiggle();
      play_at = now + 380;
      if (++play_step >= 3) play_done(now);
      break;
  }
}

// Tapping the cat (original screen, typing cat or mood cat widget): happy eyes, a heart, a soft purr-like chirp
static volatile bool pet_pending = false;
void pet_request() { pet_pending = true; }
void pet_poll() {
  if (!pet_pending) return;
  pet_pending = false;
  set_mood(MOOD_HAPPY);
  cat_heart();
  playToneWithVolume(220, 12, 45);
  playToneWithVolume(180, 12, 45);
  playToneWithVolume(240, 12, 60);
}
static void pet_cb(lv_event_t *e) { pet_request(); }
void pet_init() {
  // the original screen's cat (SquareLine objects: only flags and an event are added here, the generated code is unchanged)
  lv_obj_add_flag(ui_main_Container_cat, LV_OBJ_FLAG_CLICKABLE);
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(ui_main_Container_cat); i++) lv_obj_clear_flag(lv_obj_get_child(ui_main_Container_cat, i), LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(ui_main_Container_cat, pet_cb, LV_EVENT_SHORT_CLICKED, NULL);
  pref.begin("config", true);
  idle_style = pref.getUChar("idle_style", IDLE_STYLE_CHEERFUL);
  pref.end();
  if (idle_style > IDLE_STYLE_CHEERFUL) idle_style = IDLE_STYLE_CHEERFUL;
}
void idle_style_save() {
  pref.begin("config", false);
  pref.putUChar("idle_style", idle_style);
  pref.end();
}

// sleepy animation (non-blocking)
// eyes blinking animation (non-blocking)
void sleepy_animation() {
  unsigned long now = millis(), idle = now - last_sleep_check;
  if (idle < SLEEP_TIMER) sleepy_forced = false; // any activity cancels a forced sleep
  bool cheer = idle_style == IDLE_STYLE_CHEERFUL && !sleepy_forced && idle >= SLEEP_TIMER && idle < CHEER_SLEEP_MS;
  current_sleep_state = idle >= SLEEP_TIMER && !cheer;
  if (cheer) play_tick(now);
  else play_stop();


  if (current_sleep_state != last_sleep_state) {
    if (current_sleep_state) {
      lv_obj_clear_flag(ui_main_Image_sleepy, LV_OBJ_FLAG_HIDDEN); // sleepy now
    } else {
      lv_obj_add_flag(ui_main_Image_sleepy, LV_OBJ_FLAG_HIDDEN); // not sleepy
      last_sleep_check = now;
    }
    last_sleep_state = current_sleep_state;
  }
}

// ---- mood: HAPPY = eyes squeezed shut for a moment, SLEEPY = fall asleep now, NORMAL = wake up
#define HAPPY_MS 2500
static bool happy_active = false;
static unsigned long happy_start = 0;

void set_mood(uint8_t mood) {
  unsigned long now = millis();
  layout_feed_mood(mood);
  happy_active = false;
  if (mood == MOOD_HAPPY) {
    happy_active = true;
    happy_start = now;
    last_sleep_check = now; // wake up if sleepy
  } else if (mood == MOOD_SLEEPY) {
    sleepy_forced = true;
    last_sleep_check = now - SLEEP_TIMER; // unsigned wrap is fine: sleepy_animation() sees the timer as expired
  } else {
    last_sleep_check = now;
  }
}

// eyes blinking animation (non-blocking)
void eye_blink_animation() {
  unsigned long now = millis();

  if (happy_active) {
    if (now - happy_start < HAPPY_MS) {
      lv_obj_clear_flag(ui_main_Image_eyelid, LV_OBJ_FLAG_HIDDEN); // eyes shut, content
      eyelid_visible = true;
      eyelid_show_timer = now; // normal blink logic reopens the eyes 200 ms after the mood ends
      return;
    }
    happy_active = false;
    layout_feed_mood(MOOD_NORMAL);
  }

  if (!eyelid_visible) {
    // waiting for next blink

    if (now - eye_blink_timer > delayTime) {
      eye_blink_timer = now;
      eyelid_visible = true;
      eyelid_show_timer = now;
      lv_obj_clear_flag(ui_main_Image_eyelid, LV_OBJ_FLAG_HIDDEN); // close eye
      delayTime = 1000 + (esp_random() % 6000);
    }
  } else {
    // eyelid currently shown, check if 200 ms elapsed
    if (now - eyelid_show_timer >= 200) {
      eyelid_visible = false;
      lv_obj_add_flag(ui_main_Image_eyelid, LV_OBJ_FLAG_HIDDEN); // open eye
    }
  }
}

// Cat moving mouse animation
static ui_anim_user_data_t cat_sprite_ud;
static ui_anim_user_data_t cat_move_ud;
int last_x_cat = 76; // first cat pos in ui_Screen_Info.c
int target_x_cat = 0;
int travel_time = 0;
#define MIN_MOUSE_POS 65
#define MAX_MOUSE_POS 85
#define frame_speed 0.5
static const lv_img_dsc_t *ui_imgset_cat_left_const[] = {&ui_img_leftmousepaw_png, &ui_img_leftmousepaw_png};
// Persistent LVGL animations (do NOT put on stack)
static lv_anim_t cat_sprite_anim;
static lv_anim_t cat_move_anim;
// Stop mouseing sprite animation when movement ends
static void mouse_move_ready_cb(lv_anim_t *a) {
  ui_anim_user_data_t *move_ud = (ui_anim_user_data_t *)a->user_data;
  lv_obj_t *obj = move_ud->target;
  lv_anim_del(obj, NULL); // stop all animations on this obj
  ui_anim_user_data_t *sprite_ud;
  sprite_ud = &cat_sprite_ud;
  // Freeze sprite animation
  sprite_ud->imgset_size = 1;
  lv_img_set_src(obj, sprite_ud->imgset[0]);
}

// ------ Generalized animation function for mouse movement + sprite change------
void mouse_animation(lv_obj_t *obj, int beginX, int endX, lv_img_dsc_t **imgset, uint8_t imgset_size, uint8_t frameBegin, uint8_t frameEnd, uint16_t time, uint16_t playbackDelay, uint16_t playbackTime) {
  lv_anim_del(obj, NULL); // stop previous animations
  ui_anim_user_data_t *sprite_ud;
  ui_anim_user_data_t *move_ud;
  lv_anim_t *sprite_anim;
  lv_anim_t *move_anim;

  sprite_ud = &cat_sprite_ud;
  move_ud = &cat_move_ud;
  sprite_anim = &cat_sprite_anim;
  move_anim = &cat_move_anim;

  // ---------- SPRITE ----------
  sprite_ud->target = obj;
  sprite_ud->imgset = imgset;
  sprite_ud->imgset_size = imgset_size;
  sprite_ud->val = -1;

  lv_anim_init(sprite_anim);
  lv_anim_set_var(sprite_anim, obj);
  lv_anim_set_user_data(sprite_anim, sprite_ud);
  lv_anim_set_time(sprite_anim, frame_speed);
  lv_anim_set_values(sprite_anim, frameBegin, frameEnd);
  lv_anim_set_custom_exec_cb(sprite_anim, _ui_anim_callback_set_image_frame);
  lv_anim_set_path_cb(sprite_anim, lv_anim_path_linear);
  // lv_anim_set_repeat_count(sprite_anim, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_repeat_count(sprite_anim, time / frame_speed);
  lv_anim_start(sprite_anim);

  // ---------- MOVE ----------
  move_ud->target = obj;
  move_ud->val = -1;

  lv_anim_init(move_anim);
  lv_anim_set_var(move_anim, obj);
  lv_anim_set_user_data(move_anim, move_ud);
  lv_anim_set_time(move_anim, time);
  lv_anim_set_values(move_anim, beginX, endX);
  lv_anim_set_custom_exec_cb(move_anim, _ui_anim_callback_set_x);
  lv_anim_set_path_cb(move_anim, lv_anim_path_linear);
  lv_anim_set_playback_time(move_anim, playbackTime);
  lv_anim_set_playback_delay(move_anim, playbackDelay);
  lv_anim_set_ready_cb(move_anim, mouse_move_ready_cb);
  lv_anim_start(move_anim);
}

// ---------- Timer-based random mouse move----------
void random_mouse_task(lv_timer_t *timer) {
  travel_time = random(200, 400); // faster: reduced from 500-1000ms to 200-400ms
  uint32_t next_delay = travel_time;
  target_x_cat = random(MIN_MOUSE_POS, MAX_MOUSE_POS);
  lv_img_dsc_t **image_set_cat = (lv_img_dsc_t **)ui_imgset_cat_left_const;
  mouse_animation(ui_main_Image_leftpaw, last_x_cat, target_x_cat, image_set_cat, 2, 0, 1, travel_time, 0, 0);
  last_x_cat = target_x_cat;
  lv_timer_set_period(timer, next_delay);
}

// analog clock animation
void clock_animation(uint8_t h, uint8_t m, uint8_t s) {
  layout_feed_time(h, m, s);

  if (m != last_min) {  //update shorthand and longhand only when minute changes (to reduce unnecessary updates)
    lv_img_set_angle(ui_main_Image_shorthand, h * 300 + m * 5); 
    lv_img_set_angle(ui_main_Image_longhand, m * 60);
    last_min = m;
  }
  if (s != last_sec) {// update second hand only when second changes
    lv_img_set_angle(ui_main_Image_secondhand, s * 60);
    last_sec = s;
  }
}

// chart animation
void chart_animation(uint8_t cpu, uint8_t mem_used, uint8_t battery, bool charging) {
      // symtem information bar chart animation
      static lv_coord_t chart_data[3] = {0, 0, 0};
      chart_data[0] = cpu;
      chart_data[1] = mem_used;
      chart_data[2] = battery;
      layout_feed_system(cpu, mem_used, battery, charging);
      //charge status icon display
      if (charging != last_charge) {
        if (charging) {
          lv_obj_clear_flag(ui_main_Label_charge, LV_OBJ_FLAG_HIDDEN);
        } else {
          lv_obj_add_flag(ui_main_Label_charge, LV_OBJ_FLAG_HIDDEN);
        }
        last_charge = charging;
      }

      // Update chart using the data array
      lv_chart_series_t *s = lv_chart_get_series_next(ui_main_Chart_systeminfo, NULL);
      if (s != NULL) {
        lv_chart_set_ext_y_array(ui_main_Chart_systeminfo, s, chart_data);
      }
}

// media play status
void media_play_status(uint8_t mediaState) {
  layout_feed_media_state(mediaState);

  if (mediaState != last_mediaState) {
    last_mediaState = mediaState;
    switch (mediaState) {
    case 0: lv_label_set_text(ui_main_Label_status, "[ STOPPED ]"); break;
    case 1: lv_label_set_text(ui_main_Label_status, "[ PLAYING ]"); break;
    case 2: lv_label_set_text(ui_main_Label_status, "[ PAUSED ]"); break;
    }
  }
}

// media track information
void media_track_status(char *title, char *artist) {
  layout_feed_music(title, artist);
  lv_label_set_text(ui_main_Label_title, title);
  lv_label_set_text(ui_main_Label_artist, artist);
}
// one bat at the mouse with the left paw (same animation the desktop app's mouse movement drives)
void mouse_wiggle() {
  int target = random(MIN_MOUSE_POS, MAX_MOUSE_POS);
  mouse_animation(ui_main_Image_leftpaw, last_x_cat, target, (lv_img_dsc_t **)ui_imgset_cat_left_const, 2, 0, 1, 260, 0, 0);
  last_x_cat = target;
}
