#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#define WIFI_SSID_MAX 32
#define WIFI_PASS_MAX 64

typedef enum {
    WIFI_PROV_IDLE = 0,
    WIFI_PROV_CONNECTING,
    WIFI_PROV_CONNECTED,
    WIFI_PROV_FAILED,
} wifi_prov_state_t;

esp_err_t wifi_prov_init(void);

bool wifi_prov_has_creds(void);
esp_err_t wifi_prov_load_creds(char *ssid, size_t ssid_len, char *pass, size_t pass_len);
esp_err_t wifi_prov_save_creds(const char *ssid, const char *pass);
esp_err_t wifi_prov_clear_creds(void);

esp_err_t wifi_prov_start_ap(char *ssid_out, size_t ssid_len);
esp_err_t wifi_prov_start_sta(const char *ssid, const char *pass);
bool wifi_prov_wait_connected(int timeout_ms);

esp_err_t wifi_prov_try_connect(const char *ssid, const char *pass);

wifi_prov_state_t wifi_prov_state(void);
const char *wifi_prov_state_reason(void);
const char *wifi_prov_ip(void);
const char *wifi_prov_ssid(void);
const char *wifi_prov_pass(void);
int wifi_prov_rssi(void);
