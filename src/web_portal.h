#pragma once

#include "esp_err.h"

typedef enum {
    PORTAL_PROVISION = 0,
    PORTAL_STATIC,
} portal_mode_t;

esp_err_t web_portal_start(portal_mode_t mode);
