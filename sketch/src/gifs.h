#pragma once
// Animated GIF widget (type "gf"): plays /gif/<slot>.gif from LittleFS with bitbank2/AnimatedGIF (Apache-2.0) into an LVGL canvas.
#include <lvgl.h>
#include <stdint.h>

lv_obj_t *gif_create(lv_obj_t *parent, int x, int y, int w, int h, uint8_t slot); // NULL if the file or the memory is not there
void gifs_release(uint8_t slot); // close the file of any GIF on screen that shows this slot (before the file is replaced or deleted)
void gifs_tick(); // advance every GIF on screen whose next frame is due (called from an lv_timer)
bool gif_decode_test(uint8_t slot); // can the decoder open the stored file and decode its first frame?
