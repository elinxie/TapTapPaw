// Pet widget renderer: runs the claude-desktop-buddy species functions (species/*.cpp, MIT) against a LovyanGFX sprite
// (same text API and 6x8 font as TFT_eSPI on the original M5StickC) and shows the sprite's pixels as an LVGL image.
// Background pixels use LVGL's chroma key colour (magenta, lv_conf.h) so the page wallpaper shows through.
#include "pets.h"
#include "buddy.h"
#include "buddy_common.h"
#include "LGFX_CYD.h"
#include <new>
#include "../animation.h"
#include <string.h>

extern bool current_sleep_state; // animation.cpp

// Geometry: species pass 1x coordinates around BUDDY_X_CENTER; drawn at 2x around the centre of a 160 px sprite.
const int BUDDY_X_CENTER = 80;
const int BUDDY_CANVAS_W = PET_W;
const int BUDDY_Y_BASE = 30;
const int BUDDY_Y_OVERLAY = 6;
const int BUDDY_CHAR_W = 6;
const int BUDDY_CHAR_H = 8;

const uint16_t BUDDY_BG = 0xF81F; // = LV_COLOR_CHROMA_KEY (magenta): transparent on screen
const uint16_t BUDDY_HEART = 0xF810;
const uint16_t BUDDY_DIM = 0x8410;
const uint16_t BUDDY_YEL = 0xFFE0;
const uint16_t BUDDY_WHITE = 0xFFFF;
const uint16_t BUDDY_CYAN = 0x07FF;
const uint16_t BUDDY_GREEN = 0x07E0;
const uint16_t BUDDY_PURPLE = 0xA01F;
const uint16_t BUDDY_RED = 0xF800;
const uint16_t BUDDY_BLUE = 0x041F;

static LGFX_Sprite *tgt = nullptr; // the sprite being drawn
static const uint8_t SCALE = 2;

void buddyPrintLine(const char *line, int yPx, uint16_t color, int xOff) {
  int len = strlen(line);
  while (len && line[len - 1] == ' ') len--; // art is space padded: trim and re-centre per line (as upstream does at 2x)
  while (len && *line == ' ') {
    line++;
    len--;
  }
  int w = len * BUDDY_CHAR_W * SCALE;
  int x = BUDDY_X_CENTER - w / 2 + xOff * SCALE;
  tgt->setTextColor(color, BUDDY_BG);
  tgt->setCursor(x, yPx);
  for (int i = 0; i < len; i++) tgt->print(line[i]);
}
void buddyPrintSprite(const char *const *lines, uint8_t nLines, int yOffset, uint16_t color, int xOff) {
  tgt->setTextSize(SCALE);
  int yBase = BUDDY_Y_BASE * SCALE - (SCALE - 1) * 14;
  for (uint8_t i = 0; i < nLines; i++) buddyPrintLine(lines[i], yBase + (yOffset + i * BUDDY_CHAR_H) * SCALE, color, xOff);
}
void buddySetCursor(int x, int y) { tgt->setCursor(BUDDY_X_CENTER + (x - BUDDY_X_CENTER) * SCALE, y * SCALE); }
void buddySetColor(uint16_t fg) { tgt->setTextColor(fg, BUDDY_BG); }
void buddyPrint(const char *s) {
  tgt->setTextSize(SCALE);
  tgt->print(s);
}

// ---- species registry (upstream order) ----
extern const Species CAPYBARA_SPECIES, DUCK_SPECIES, GOOSE_SPECIES, BLOB_SPECIES, CAT_SPECIES, DRAGON_SPECIES, OCTOPUS_SPECIES,
    OWL_SPECIES, PENGUIN_SPECIES, TURTLE_SPECIES, SNAIL_SPECIES, GHOST_SPECIES, AXOLOTL_SPECIES, CACTUS_SPECIES, ROBOT_SPECIES,
    RABBIT_SPECIES, MUSHROOM_SPECIES, CHONK_SPECIES;
// Index = species number in layouts ("n"), upstream order. All 18 take ~75 KB of flash, too much for the 1.44 MB slot of the
// default build: it carries 6 (unused entries fall back to the cat, linker drops the rest). WiFi / Bluetooth builds carry all.
#ifndef PETS_ALL
#define PETS_ALL 0
#endif
#if PETS_ALL
#define PET(x) &x
#else
#define PET(x) nullptr
#endif
static const Species *const SPECIES[] = {&CAPYBARA_SPECIES, &DUCK_SPECIES, PET(GOOSE_SPECIES), PET(BLOB_SPECIES), &CAT_SPECIES, PET(DRAGON_SPECIES),
                                         PET(OCTOPUS_SPECIES), PET(OWL_SPECIES), &PENGUIN_SPECIES, PET(TURTLE_SPECIES), PET(SNAIL_SPECIES), PET(GHOST_SPECIES),
                                         &AXOLOTL_SPECIES, PET(CACTUS_SPECIES), PET(ROBOT_SPECIES), &RABBIT_SPECIES, PET(MUSHROOM_SPECIES), PET(CHONK_SPECIES)};
static const uint8_t N_SPECIES = sizeof SPECIES / sizeof SPECIES[0];
static const uint8_t CAT_IDX = 4;
uint8_t pets_count() { return N_SPECIES; }
bool pets_all() { return PETS_ALL; }
const char *pets_name(uint8_t i) { return i < N_SPECIES && SPECIES[i] ? SPECIES[i]->name : "?"; }

// ---- mood ----
static uint32_t busy_until = 0, ev_until = 0, tick = 0;
static uint8_t ev_mood = PET_IDLE;
static uint32_t pet_times[5] = {0};
void pets_busy() { busy_until = millis() + 1500; }
void pets_event(uint8_t mood, uint32_t ms) {
  ev_mood = mood;
  ev_until = millis() + ms;
}
void pets_petted() {
  uint32_t now = millis();
  memmove(pet_times, pet_times + 1, sizeof pet_times - sizeof pet_times[0]);
  pet_times[4] = now;
  bool frantic = pet_times[0] && now - pet_times[0] < 2500; // five pets in 2.5 s
  pets_event(frantic ? PET_DIZZY : PET_HEART, frantic ? 3000 : 3500);
}
static uint8_t mood_now() {
  uint32_t now = millis();
  if ((int32_t)(ev_until - now) > 0) return ev_mood;
  if ((int32_t)(busy_until - now) > 0) return PET_BUSY;
  return current_sleep_state ? PET_SLEEP : PET_IDLE; // same idle timer and waiting style as the cat
}

// ---- LVGL objects: one sprite + image per pet on screen ----
struct Pet {
  LGFX_Sprite spr;
  lv_img_dsc_t dsc;
  lv_obj_t *img;
  uint8_t species;
  Pet *next;
};
static Pet *pets = nullptr;

static void draw(Pet *p, uint8_t mood) {
  tgt = &p->spr;
  p->spr.fillScreen(BUDDY_BG);
  SPECIES[p->species]->states[mood](tick);
  lv_img_cache_invalidate_src(&p->dsc);
  lv_obj_invalidate(p->img);
}

static void pet_deleted(lv_event_t *e) {
  Pet *p = (Pet *)lv_event_get_user_data(e);
  for (Pet **pp = &pets; *pp; pp = &(*pp)->next)
    if (*pp == p) {
      *pp = p->next;
      break;
    }
  p->spr.deleteSprite();
  delete p;
}

lv_obj_t *pet_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, uint8_t species) {
  Pet *p = new (std::nothrow) Pet();
  if (!p) return nullptr;
  p->spr.setColorDepth(16);
  if (!p->spr.createSprite(PET_W, PET_H)) { // 41 KB of heap
    delete p;
    return nullptr;
  }
  p->spr.setTextWrap(false);
  p->species = species < N_SPECIES && SPECIES[species] ? species : CAT_IDX;
  memset(&p->dsc, 0, sizeof p->dsc);
  p->dsc.header.cf = LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED;
  p->dsc.header.w = PET_W;
  p->dsc.header.h = PET_H;
  p->dsc.data_size = PET_W * PET_H * 2;
  p->dsc.data = (const uint8_t *)p->spr.getBuffer(); // swapped RGB565 = LVGL with LV_COLOR_16_SWAP
  p->img = lv_img_create(parent);
  lv_obj_set_pos(p->img, x, y);
  lv_img_set_src(p->img, &p->dsc);
  lv_obj_add_event_cb(p->img, pet_deleted, LV_EVENT_DELETE, p);
  p->next = pets;
  pets = p;
  draw(p, mood_now());
  return p->img;
}

void pets_tick() {
  if (!pets) return;
  tick++;
  uint8_t m = mood_now();
  for (Pet *p = pets; p; p = p->next) draw(p, m);
}
