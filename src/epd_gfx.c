#include "epd_gfx.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "font5x7.h"

static const char *TAG = "gfx";

static uint8_t *s_buf;

esp_err_t gfx_begin(void)
{
    esp_err_t err = epd_init();
    if (err != ESP_OK) {
        return err;
    }
    s_buf = heap_caps_malloc(EPD_BUF_SIZE, MALLOC_CAP_DMA);
    if (!s_buf) {
        ESP_LOGE(TAG, "framebuffer alloc failed");
        return ESP_ERR_NO_MEM;
    }
    gfx_clear(GFX_WHITE);
    return ESP_OK;
}

void gfx_clear(gfx_color_t c)
{
    memset(s_buf, (c == GFX_WHITE) ? 0xFF : 0x00, EPD_BUF_SIZE);
}

void gfx_pixel(int x, int y, gfx_color_t c)
{
    if (x < 0 || x >= GFX_W || y < 0 || y >= GFX_H) {
        return;
    }
    int px = EPD_PANEL_WIDTH - 1 - (y + EPD_VIEW_Y);
    int py = x + EPD_VIEW_X;
    int idx = (px >> 3) + py * EPD_ROW_BYTES;
    uint8_t mask = 0x80 >> (px & 7);
    if (c == GFX_WHITE) {
        s_buf[idx] |= mask;
    } else {
        s_buf[idx] &= ~mask;
    }
}

void gfx_hline(int x, int y, int w, gfx_color_t c)
{
    for (int i = 0; i < w; ++i) {
        gfx_pixel(x + i, y, c);
    }
}

void gfx_vline(int x, int y, int h, gfx_color_t c)
{
    for (int i = 0; i < h; ++i) {
        gfx_pixel(x, y + i, c);
    }
}

void gfx_rect(int x, int y, int w, int h, gfx_color_t c)
{
    gfx_hline(x, y, w, c);
    gfx_hline(x, y + h - 1, w, c);
    gfx_vline(x, y, h, c);
    gfx_vline(x + w - 1, y, h, c);
}

void gfx_fill_rect(int x, int y, int w, int h, gfx_color_t c)
{
    for (int i = 0; i < h; ++i) {
        gfx_hline(x, y + i, w, c);
    }
}

void gfx_circle(int cx, int cy, int r, gfx_color_t c)
{
    int x = r;
    int y = 0;
    int err = 1 - r;
    while (x >= y) {
        gfx_pixel(cx + x, cy + y, c);
        gfx_pixel(cx + y, cy + x, c);
        gfx_pixel(cx - y, cy + x, c);
        gfx_pixel(cx - x, cy + y, c);
        gfx_pixel(cx - x, cy - y, c);
        gfx_pixel(cx - y, cy - x, c);
        gfx_pixel(cx + y, cy - x, c);
        gfx_pixel(cx + x, cy - y, c);
        ++y;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            --x;
            err += 2 * (y - x) + 1;
        }
    }
}

static void gfx_char(int x, int y, uint8_t size, gfx_color_t c, char ch)
{
    uint8_t idx = (ch < 0x20 || ch > 0x7E) ? 0 : (uint8_t)(ch - 0x20);
    for (int col = 0; col < 5; ++col) {
        uint8_t bits = FONT5X7[idx][col];
        for (int row = 0; row < 7; ++row) {
            if (!(bits & (1 << row))) {
                continue;
            }
            if (size == 1) {
                gfx_pixel(x + col, y + row, c);
            } else {
                gfx_fill_rect(x + col * size, y + row * size, size, size, c);
            }
        }
    }
}

void gfx_text(int x, int y, uint8_t size, gfx_color_t c, const char *s)
{
    for (; *s; ++s) {
        gfx_char(x, y, size, c, *s);
        x += 6 * size;
    }
}

int gfx_text_width(uint8_t size, const char *s)
{
    return (int)strlen(s) * 6 * size - size;
}

void gfx_display(void)
{
    epd_refresh_full(s_buf);
}
