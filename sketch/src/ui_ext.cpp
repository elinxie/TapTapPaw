#include "ui_ext.h"
#include "keymap.h"
#include "layout.h"
#include "power.h"
#include "ui/ui.h"
#include "wifi_sync.h"
#include <lvgl.h>

static lv_obj_t *scr_ext = NULL;
#if ENABLE_WIFI
static lv_obj_t *lbl_wifi = NULL;
#endif
static lv_obj_t *lbl_layout = NULL;
static lv_obj_t *dd_idle_after = NULL;
static lv_obj_t *dd_keys[SLOT_COUNT] = {NULL};
static const uint8_t IDLE_MINUTE_CHOICES[] = {1, 2, 5, 10, 30};

static void style_label(lv_obj_t *l) { lv_obj_set_style_text_color(l, lv_color_white(), LV_PART_MAIN); }

// one row: label on the left, dropdown on the right
static lv_obj_t *add_row(lv_obj_t *parent, const char *text, const char *options, uint16_t selected, lv_event_cb_t cb, void *user_data) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *l = lv_label_create(row);
  lv_label_set_text(l, text);
  style_label(l);
  lv_obj_t *dd = lv_dropdown_create(row);
  lv_dropdown_set_options(dd, options);
  lv_dropdown_set_selected(dd, selected);
  lv_obj_set_width(dd, 150);
  lv_obj_add_event_cb(dd, cb, LV_EVENT_VALUE_CHANGED, user_data);
  return dd;
}

static void add_heading(lv_obj_t *parent, const char *text) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_color(l, lv_color_hex(0xFBC979), LV_PART_MAIN);
}

static void refresh_idle_rows() {
  if (dd_idle_after == NULL) return;
  if (idle_mode == IDLE_NEVER) {
    lv_obj_add_state(dd_idle_after, LV_STATE_DISABLED);
  } else {
    lv_obj_clear_state(dd_idle_after, LV_STATE_DISABLED);
  }
}

static void idle_mode_cb(lv_event_t *e) {
  idle_mode = (uint8_t)lv_dropdown_get_selected(lv_event_get_target(e));
  power_save();
  refresh_idle_rows();
}

static void idle_after_cb(lv_event_t *e) {
  uint16_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
  if (sel < sizeof(IDLE_MINUTE_CHOICES)) idle_minutes = IDLE_MINUTE_CHOICES[sel];
  power_save();
}

static void key_slot_cb(lv_event_t *e) {
  uintptr_t slot = (uintptr_t)lv_event_get_user_data(e);
  uint16_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
  if (slot < SLOT_COUNT && sel < ACT_COUNT) {
    key_map[slot] = (uint8_t)sel;
    keymap_save();
  }
}

void ui_ext_refresh() {
  for (uint8_t i = 0; i < SLOT_COUNT; i++) {
    if (dd_keys[i] != NULL) lv_dropdown_set_selected(dd_keys[i], key_map[i]);
  }
}

static void keys_reset_cb(lv_event_t *e) {
  keymap_defaults();
  keymap_save();
  ui_ext_refresh();
}

static void back_cb(lv_event_t *e) {
  lv_scr_load_anim(ui_Screen_setting, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
}

static lv_obj_t *add_button(lv_obj_t *parent, const char *text, int w, lv_event_cb_t cb) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, w, 28);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0xFBC979), LV_PART_MAIN);
  lv_obj_t *l = lv_label_create(btn);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_color(l, lv_color_black(), LV_PART_MAIN);
  lv_obj_center(l);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
  return btn;
}

#if ENABLE_WIFI
static void wifi_enable_cb(lv_event_t *e) {
  wifi_sync_set_enabled(lv_dropdown_get_selected(lv_event_get_target(e)) == 1);
}

static void wifi_forget_cb(lv_event_t *e) {
  wifi_sync_set_credentials("", "");
}

static void wifi_status_timer(lv_timer_t *t) {
  if (lbl_wifi == NULL) return;
  const char *s = wifi_sync_status();
  if (strcmp(lv_label_get_text(lbl_wifi), s) != 0) lv_label_set_text(lbl_wifi, s); // only redraw on change
}
#endif

static void layout_reset_cb(lv_event_t *e) {
  layout_reset();
  if (lbl_layout != NULL) lv_label_set_text(lbl_layout, layout_status());
}

static void layout_status_timer(lv_timer_t *t) {
  if (lbl_layout == NULL) return;
  const char *s = layout_status();
  if (strcmp(lv_label_get_text(lbl_layout), s) != 0) lv_label_set_text(lbl_layout, s);
}

static void build_ext_screen() {
  scr_ext = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_ext, lv_color_hex(0x1B1620), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scr_ext, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_pad_all(scr_ext, 8, LV_PART_MAIN);
  lv_obj_set_style_pad_row(scr_ext, 8, LV_PART_MAIN);
  lv_obj_set_flex_flow(scr_ext, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(scr_ext, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(scr_ext, LV_DIR_VER);

  add_button(scr_ext, LV_SYMBOL_LEFT " Back", 80, back_cb);

  add_heading(scr_ext, "Idle (no app / web page connected)");
  add_row(scr_ext, "When idle", "Never sleep\nDim only\nSleep display", idle_mode, idle_mode_cb, NULL);
  uint16_t sel = 2;
  for (uint16_t i = 0; i < sizeof(IDLE_MINUTE_CHOICES); i++) {
    if (IDLE_MINUTE_CHOICES[i] == idle_minutes) sel = i;
  }
  dd_idle_after = add_row(scr_ext, "After", "1 minute\n2 minutes\n5 minutes\n10 minutes\n30 minutes", sel, idle_after_cb, NULL);
  refresh_idle_rows();

  lv_obj_t *hint = lv_label_create(scr_ext);
  lv_label_set_text(hint, "Touch or press a key to wake.\nThe wake touch or press is not a tap.");
  lv_obj_set_style_text_color(hint, lv_color_hex(0xA99BB3), LV_PART_MAIN);

#if ENABLE_WIFI
  add_heading(scr_ext, "WiFi (when no app / web page is connected)");
  add_row(scr_ext, "WiFi", "Off\nOn", wifi_sync_enabled() ? 1 : 0, wifi_enable_cb, NULL);
  lbl_wifi = lv_label_create(scr_ext);
  lv_label_set_text(lbl_wifi, wifi_sync_status());
  lv_obj_set_style_text_color(lbl_wifi, lv_color_white(), LV_PART_MAIN);
  lv_obj_t *wifi_hint = lv_label_create(scr_ext);
  lv_label_set_text(wifi_hint, "Network name, password and location are\nsent from the web page over USB.");
  lv_obj_set_style_text_color(wifi_hint, lv_color_hex(0xA99BB3), LV_PART_MAIN);
  add_button(scr_ext, "Forget WiFi", 110, wifi_forget_cb);
  lv_timer_create(wifi_status_timer, 1000, NULL);
#endif

  add_heading(scr_ext, "Widget layout (built on the web page)");
  lbl_layout = lv_label_create(scr_ext);
  lv_label_set_text(lbl_layout, layout_status());
  lv_obj_set_style_text_color(lbl_layout, lv_color_white(), LV_PART_MAIN);
  add_button(scr_ext, "Reset layout", 110, layout_reset_cb);
  lv_timer_create(layout_status_timer, 1000, NULL);

  add_heading(scr_ext, "Keys (events always go to the app)");
  static const char *slot_names[SLOT_COUNT] = {"Key1 tap", "Key1 hold", "Key2 tap", "Key2 hold", "Key1+Key2", "Key1+Key2 hold"};
  for (uint8_t i = 0; i < SLOT_COUNT; i++) {
    dd_keys[i] = add_row(scr_ext, slot_names[i], KEY_ACTION_OPTIONS, key_map[i], key_slot_cb, (void *)(uintptr_t)i);
  }
  add_button(scr_ext, "Reset keys", 110, keys_reset_cb);
}

static void more_cb(lv_event_t *e) {
  if (scr_ext == NULL) build_ext_screen(); // built on first use to save RAM at boot
  lv_scr_load_anim(scr_ext, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}

void ui_ext_init() {
  // "More >" button, top right of the existing settings screen
  lv_obj_t *btn = lv_btn_create(ui_Screen_setting);
  lv_obj_set_size(btn, 70, 26);
  lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, -4, 4);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0xFBC979), LV_PART_MAIN);
  lv_obj_t *l = lv_label_create(btn);
  lv_label_set_text(l, "More " LV_SYMBOL_RIGHT);
  lv_obj_set_style_text_color(l, lv_color_black(), LV_PART_MAIN);
  lv_obj_center(l);
  lv_obj_add_event_cb(btn, more_cb, LV_EVENT_CLICKED, NULL);
}
