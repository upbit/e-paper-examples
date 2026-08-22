#include <stdbool.h>

#include "board_button.h"
#include "board_led.h"
#include "epd_gfx.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "screens.h"
#include "storage.h"
#include "web_portal.h"
#include "wifi_prov.h"

static const char *TAG = "main";

#define SELFTEST_LED_MS 3000
#define SELFTEST_BREATH_MS 1000
#define STA_CONNECT_TIMEOUT_MS 30000

static void run_selftest(void)
{
    board_led_init();
    board_led_breathe_start(255, 255, 255, SELFTEST_BREATH_MS);
    int64_t started = esp_timer_get_time();

    ESP_ERROR_CHECK(gfx_begin());
    screen_selftest();

    int64_t elapsed_ms = (esp_timer_get_time() - started) / 1000;
    if (elapsed_ms < SELFTEST_LED_MS) {
        vTaskDelay(pdMS_TO_TICKS(SELFTEST_LED_MS - elapsed_ms));
    }
    board_led_breathe_stop();
    ESP_LOGI(TAG, "self-test done");
}

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase, reformatting");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static void start_provisioning(void)
{
    char ap_ssid[WIFI_SSID_MAX + 1];
    ESP_ERROR_CHECK(wifi_prov_start_ap(ap_ssid, sizeof(ap_ssid)));
    screen_ap_mode(ap_ssid, "http://192.168.4.1");
    ESP_ERROR_CHECK(web_portal_start(PORTAL_PROVISION));
}

static void start_station(void)
{
    char ssid[WIFI_SSID_MAX + 1] = {0};
    char pass[WIFI_PASS_MAX + 1] = {0};
    ESP_ERROR_CHECK(wifi_prov_load_creds(ssid, sizeof(ssid), pass, sizeof(pass)));
    ESP_ERROR_CHECK(wifi_prov_start_sta(ssid, pass));

    if (wifi_prov_wait_connected(STA_CONNECT_TIMEOUT_MS)) {
        screen_sta_ready(ssid, wifi_prov_ip());
        ESP_ERROR_CHECK(web_portal_start(PORTAL_STATIC));
        return;
    }

    ESP_LOGW(TAG, "not connected after %dms, retrying in background",
             STA_CONNECT_TIMEOUT_MS);
    screen_sta_failed(ssid, wifi_prov_state_reason());

    if (wifi_prov_wait_connected(portMAX_DELAY)) {
        screen_sta_ready(ssid, wifi_prov_ip());
        ESP_ERROR_CHECK(web_portal_start(PORTAL_STATIC));
    }
}

void app_main(void)
{
    run_selftest();
    init_nvs();
    storage_mount();

    ESP_ERROR_CHECK(wifi_prov_init());
    board_button_start_reset_watch();

    if (wifi_prov_has_creds()) {
        start_station();
    } else {
        start_provisioning();
    }
}
