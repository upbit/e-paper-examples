#include <stdbool.h>
#include <stdio.h>

#include "board_led.h"
#include "epd_gfx.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

#define SELFTEST_LED_MS 3000
#define SELFTEST_BREATH_MS 1000

#define CORNER 16
#define TICK_X_MIN (CORNER + 4)
#define TICK_X_MAX (GFX_W - CORNER - 4)
#define TICK_Y_MIN (CORNER + 4)
#define TICK_Y_MAX (GFX_H - CORNER - 4)

static void draw_rulers(void)
{
    for (int x = 0; x <= GFX_W; x += 10) {
        if (x < TICK_X_MIN || x > TICK_X_MAX) {
            continue;
        }
        bool major = (x % 50 == 0);
        int len = major ? 6 : 3;
        gfx_vline(x, 1, len, GFX_BLACK);
        gfx_vline(x, GFX_H - 1 - len, len, GFX_BLACK);
        if (major) {
            char label[8];
            snprintf(label, sizeof(label), "%d", x);
            gfx_text(x - gfx_text_width(1, label) / 2, 9, 1, GFX_BLACK, label);
        }
    }

    for (int y = 0; y <= GFX_H; y += 10) {
        if (y < TICK_Y_MIN || y > TICK_Y_MAX) {
            continue;
        }
        bool major = (y % 50 == 0);
        int len = major ? 6 : 3;
        gfx_hline(1, y, len, GFX_BLACK);
        gfx_hline(GFX_W - 1 - len, y, len, GFX_BLACK);
        if (major) {
            char label[8];
            snprintf(label, sizeof(label), "%d", y);
            gfx_text(8, y - 3, 1, GFX_BLACK, label);
        }
    }
}

static void draw_corners(void)
{
    const int lo = 1;
    const int rx = GFX_W - 1 - CORNER;
    const int by = GFX_H - 1 - CORNER;

    gfx_fill_rect(lo, lo, CORNER, CORNER, GFX_BLACK);

    gfx_circle(rx + CORNER / 2, lo + CORNER / 2, CORNER / 2 - 1, GFX_BLACK);
    gfx_circle(rx + CORNER / 2, lo + CORNER / 2, CORNER / 4 - 1, GFX_BLACK);

    for (int i = 0; i < CORNER; ++i) {
        gfx_hline(lo, by + i, i + 1, GFX_BLACK);
    }

    for (int j = 0; j < CORNER / 4; ++j) {
        for (int i = 0; i < CORNER / 4; ++i) {
            if ((i + j) % 2 == 0) {
                gfx_fill_rect(rx + i * 4, by + j * 4, 4, 4, GFX_BLACK);
            }
        }
    }

    const int tag_in = CORNER + 4;
    gfx_text(tag_in, tag_in, 1, GFX_BLACK, "TL");
    gfx_text(GFX_W - tag_in - gfx_text_width(1, "TR"), tag_in, 1, GFX_BLACK, "TR");
    gfx_text(tag_in, by, 1, GFX_BLACK, "BL");
    gfx_text(GFX_W - tag_in - gfx_text_width(1, "BR"), by, 1, GFX_BLACK, "BR");
}

static void draw_center(void)
{
    const int cx = GFX_W / 2;
    const int cy = GFX_H / 2;

    gfx_hline(cx - 7, cy, 15, GFX_BLACK);
    gfx_vline(cx, cy - 7, 15, GFX_BLACK);
    gfx_circle(cx, cy, 10, GFX_BLACK);

    char title[16];
    snprintf(title, sizeof(title), "%dx%d", GFX_W, GFX_H);
    gfx_text(cx - gfx_text_width(2, title) / 2, 20, 2, GFX_BLACK, title);

    char fb[24];
    snprintf(fb, sizeof(fb), "FB %dx%d Y+%d", EPD_PANEL_HEIGHT, EPD_PANEL_WIDTH, EPD_VIEW_Y);
    gfx_text(cx - gfx_text_width(1, fb) / 2, GFX_H - 38, 1, GFX_BLACK, fb);

    const char *sub = "SSD1675A 2.13in";
    gfx_text(cx - gfx_text_width(1, sub) / 2, GFX_H - 28, 1, GFX_BLACK, sub);
}

void app_main(void)
{
    board_led_init();
    board_led_breathe_start(255, 255, 255, SELFTEST_BREATH_MS);
    int64_t led_start = esp_timer_get_time();

    ESP_ERROR_CHECK(gfx_begin());

    gfx_clear(GFX_WHITE);
    gfx_rect(0, 0, GFX_W, GFX_H, GFX_BLACK);
    draw_rulers();
    draw_corners();
    draw_center();
    gfx_display();

    epd_hibernate();

    int64_t elapsed_ms = (esp_timer_get_time() - led_start) / 1000;
    if (elapsed_ms < SELFTEST_LED_MS) {
        vTaskDelay(pdMS_TO_TICKS(SELFTEST_LED_MS - elapsed_ms));
    }
    board_led_breathe_stop();

    ESP_LOGI(TAG, "self-test done (screen refreshed, LED %dms)", SELFTEST_LED_MS);
}
