#include "storage.h"

#include "esp_log.h"
#include "esp_spiffs.h"

static const char *TAG = "storage";

static bool s_mounted;

esp_err_t storage_mount(void)
{
    if (s_mounted) {
        return ESP_OK;
    }
    esp_vfs_spiffs_conf_t conf = {
        .base_path = STORAGE_BASE_PATH,
        .partition_label = STORAGE_PARTITION,
        .max_files = 8,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
        return err;
    }

    size_t total = 0;
    size_t used = 0;
    if (esp_spiffs_info(STORAGE_PARTITION, &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS mounted (%u KB used / %u KB)", (unsigned)(used / 1024),
                 (unsigned)(total / 1024));
    } else {
        ESP_LOGI(TAG, "SPIFFS mounted");
    }
    s_mounted = true;
    return ESP_OK;
}

bool storage_mounted(void)
{
    return s_mounted;
}
