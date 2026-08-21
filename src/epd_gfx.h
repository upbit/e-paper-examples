#pragma once

#include <stdint.h>

#include "epd_ssd1675a.h"

#ifndef EPD_VIEW_X
#define EPD_VIEW_X 0
#endif
#ifndef EPD_VIEW_Y
#define EPD_VIEW_Y 18
#endif
#ifndef EPD_VIEW_W
#define EPD_VIEW_W 212
#endif
#ifndef EPD_VIEW_H
#define EPD_VIEW_H 104
#endif

#define GFX_W EPD_VIEW_W
#define GFX_H EPD_VIEW_H

typedef enum {
    GFX_WHITE = 0,
    GFX_BLACK = 1,
} gfx_color_t;

esp_err_t gfx_begin(void);
void gfx_clear(gfx_color_t c);
void gfx_pixel(int x, int y, gfx_color_t c);
void gfx_hline(int x, int y, int w, gfx_color_t c);
void gfx_vline(int x, int y, int h, gfx_color_t c);
void gfx_rect(int x, int y, int w, int h, gfx_color_t c);
void gfx_fill_rect(int x, int y, int w, int h, gfx_color_t c);
void gfx_circle(int cx, int cy, int r, gfx_color_t c);
void gfx_text(int x, int y, uint8_t size, gfx_color_t c, const char *s);
int gfx_text_width(uint8_t size, const char *s);
void gfx_display(void);
