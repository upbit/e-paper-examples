#pragma once

#include <stdbool.h>

#include "esp_err.h"

#define STORAGE_PARTITION "storage"
#define STORAGE_BASE_PATH "/www"

esp_err_t storage_mount(void);
bool storage_mounted(void);
