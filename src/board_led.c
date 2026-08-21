#include "board_led.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

static const char *TAG = "led";

#define BREATHE_STEPS 60

static led_strip_handle_t s_strip;
static TaskHandle_t s_task;
static volatile bool s_run;
static uint8_t s_r, s_g, s_b;
static uint32_t s_period_ms;

void board_led_init(void)
{
    if (s_strip) {
        return;
    }
    led_strip_config_t strip = {
        .strip_gpio_num = BOARD_RGB_LED_GPIO,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    led_strip_rmt_config_t rmt = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };
    esp_err_t err = led_strip_new_rmt_device(&strip, &rmt, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_strip init failed: %s", esp_err_to_name(err));
        s_strip = NULL;
        return;
    }
    led_strip_clear(s_strip);
    ESP_LOGI(TAG, "WS2812 ready on GPIO%d", BOARD_RGB_LED_GPIO);
}

static void breathe_task(void *arg)
{
    TickType_t step = pdMS_TO_TICKS(s_period_ms / BREATHE_STEPS);
    if (step == 0) {
        step = 1;
    }
    while (s_run) {
        for (int i = 0; i < BREATHE_STEPS && s_run; ++i) {
            float phase = (float)i / BREATHE_STEPS;
            float level = (phase < 0.5f) ? phase * 2.0f : (1.0f - phase) * 2.0f;
            level *= level;
            led_strip_set_pixel(s_strip, 0, (uint8_t)(s_r * level + 0.5f),
                                (uint8_t)(s_g * level + 0.5f),
                                (uint8_t)(s_b * level + 0.5f));
            led_strip_refresh(s_strip);
            vTaskDelay(step);
        }
    }
    led_strip_clear(s_strip);
    s_task = NULL;
    vTaskDelete(NULL);
}

void board_led_breathe_start(uint8_t r, uint8_t g, uint8_t b, uint32_t period_ms)
{
    if (!s_strip || s_task) {
        return;
    }
    s_r = r;
    s_g = g;
    s_b = b;
    s_period_ms = period_ms ? period_ms : 1000;
    s_run = true;
    xTaskCreate(breathe_task, "led_breathe", 2560, NULL, 3, &s_task);
}

void board_led_breathe_stop(void)
{
    if (!s_run) {
        return;
    }
    s_run = false;
    while (s_task) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
