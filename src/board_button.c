#include "board_button.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi_prov.h"

static const char *TAG = "button";

#define HOLD_MS 3000
#define POLL_MS 100

static void button_task(void *arg)
{
    int held_ms = 0;
    while (true) {
        if (gpio_get_level((gpio_num_t)BOOT_BUTTON_GPIO) == 0) {
            held_ms += POLL_MS;
            if (held_ms >= HOLD_MS) {
                ESP_LOGW(TAG, "BOOT held %dms, clearing credentials", held_ms);
                wifi_prov_clear_creds();
                vTaskDelay(pdMS_TO_TICKS(200));
                esp_restart();
            }
        } else {
            held_ms = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

void board_button_start_reset_watch(void)
{
    gpio_config_t in = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&in);
    xTaskCreate(button_task, "btn_reset", 3072, NULL, 2, NULL);
    ESP_LOGI(TAG, "hold BOOT (GPIO%d) %dms to reset WiFi", BOOT_BUTTON_GPIO, HOLD_MS);
}
