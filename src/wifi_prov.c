#include "wifi_prov.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs.h"

static const char *TAG = "wifi";

#define NVS_NAMESPACE "wifi_cfg"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASS "pass"

#define BIT_CONNECTED BIT0
#define RETRY_SOFT_LIMIT 5

static EventGroupHandle_t s_events;
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static esp_timer_handle_t s_retry_timer;

static volatile wifi_prov_state_t s_state = WIFI_PROV_IDLE;
static char s_reason[48];
static char s_ip[16] = "0.0.0.0";
static char s_ssid[WIFI_SSID_MAX + 1];
static char s_pass[WIFI_PASS_MAX + 1];
static volatile bool s_sta_wanted;
static bool s_validating;
static int s_retries;

static const char *disconnect_reason(uint8_t code)
{
    switch (code) {
    case WIFI_REASON_NO_AP_FOUND:
        return "network not found";
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
        return "wrong password";
    case WIFI_REASON_AUTH_EXPIRE:
        return "authentication expired";
    case WIFI_REASON_ASSOC_FAIL:
        return "association failed";
    case WIFI_REASON_BEACON_TIMEOUT:
        return "signal lost";
    default:
        return "connection failed";
    }
}

static void schedule_retry(void)
{
    static const int backoff_ms[] = {1000, 2000, 4000, 8000, 15000, 30000};
    int idx = s_retries < 6 ? s_retries : 5;
    esp_timer_stop(s_retry_timer);
    esp_timer_start_once(s_retry_timer, (uint64_t)backoff_ms[idx] * 1000);
}

static void retry_cb(void *arg)
{
    if (s_sta_wanted) {
        esp_wifi_connect();
    }
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (s_sta_wanted) {
            esp_wifi_connect();
        }
        return;
    }

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *e = data;
        xEventGroupClearBits(s_events, BIT_CONNECTED);
        strlcpy(s_ip, "0.0.0.0", sizeof(s_ip));

        if (!s_sta_wanted) {
            return;
        }
        strlcpy(s_reason, disconnect_reason(e->reason), sizeof(s_reason));

        if (s_validating) {
            s_state = WIFI_PROV_FAILED;
            ESP_LOGW(TAG, "validation failed: %s (reason %d)", s_reason, e->reason);
            return;
        }

        ++s_retries;
        if (s_retries >= RETRY_SOFT_LIMIT) {
            s_state = WIFI_PROV_FAILED;
        }
        ESP_LOGW(TAG, "disconnected: %s (reason %d), retry #%d", s_reason, e->reason, s_retries);
        schedule_retry();
        return;
    }

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&e->ip_info.ip));
        if (!s_sta_wanted) {
            ESP_LOGW(TAG, "unexpected IP %s while provisioning, disconnecting", s_ip);
            esp_wifi_disconnect();
            return;
        }
        s_retries = 0;
        s_reason[0] = '\0';
        s_state = WIFI_PROV_CONNECTED;
        xEventGroupSetBits(s_events, BIT_CONNECTED);
        ESP_LOGI(TAG, "connected, IP %s", s_ip);
    }
}

esp_err_t wifi_prov_init(void)
{
    if (s_events) {
        return ESP_OK;
    }
    s_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                       &on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                       &on_wifi_event, NULL, NULL));

    const esp_timer_create_args_t timer = {
        .callback = retry_cb,
        .name = "wifi_retry",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer, &s_retry_timer));
    return ESP_OK;
}

bool wifi_prov_has_creds(void)
{
    char ssid[WIFI_SSID_MAX + 1];
    return wifi_prov_load_creds(ssid, sizeof(ssid), NULL, 0) == ESP_OK && ssid[0] != '\0';
}

esp_err_t wifi_prov_load_creds(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_get_str(nvs, NVS_KEY_SSID, ssid, &ssid_len);
    if (err == ESP_OK && pass) {
        if (nvs_get_str(nvs, NVS_KEY_PASS, pass, &pass_len) != ESP_OK) {
            pass[0] = '\0';
        }
    }
    nvs_close(nvs);
    return err;
}

esp_err_t wifi_prov_save_creds(const char *ssid, const char *pass)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(nvs, NVS_KEY_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, NVS_KEY_PASS, pass ? pass : "");
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    ESP_LOGI(TAG, "credentials saved for \"%s\"", ssid);
    return err;
}

esp_err_t wifi_prov_clear_creds(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    nvs_erase_key(nvs, NVS_KEY_SSID);
    nvs_erase_key(nvs, NVS_KEY_PASS);
    err = nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGW(TAG, "credentials cleared");
    return err;
}

esp_err_t wifi_prov_start_ap(char *ssid_out, size_t ssid_len)
{
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP));

    char ap_ssid[WIFI_SSID_MAX + 1];
    snprintf(ap_ssid, sizeof(ap_ssid), "ESP32-ePaper-%02X%02X%02X", mac[3], mac[4], mac[5]);

    wifi_config_t cfg = {0};
    strlcpy((char *)cfg.ap.ssid, ap_ssid, sizeof(cfg.ap.ssid));
    cfg.ap.ssid_len = strlen(ap_ssid);
    cfg.ap.channel = 1;
    cfg.ap.max_connection = 4;
    cfg.ap.authmode = WIFI_AUTH_OPEN;

    s_sta_wanted = false;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    if (ssid_out) {
        strlcpy(ssid_out, ap_ssid, ssid_len);
    }
    s_state = WIFI_PROV_IDLE;
    ESP_LOGI(TAG, "no credentials, starting AP: %s", ap_ssid);
    return ESP_OK;
}

static esp_err_t apply_sta_config(const char *ssid, const char *pass)
{
    wifi_config_t cfg = {0};
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, pass ? pass : "", sizeof(cfg.sta.password));
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    strlcpy(s_ssid, ssid, sizeof(s_ssid));
    strlcpy(s_pass, pass ? pass : "", sizeof(s_pass));
    return esp_wifi_set_config(WIFI_IF_STA, &cfg);
}

esp_err_t wifi_prov_start_sta(const char *ssid, const char *pass)
{
    s_validating = false;
    s_sta_wanted = true;
    s_retries = 0;
    s_state = WIFI_PROV_CONNECTING;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(apply_sta_config(ssid, pass));
    ESP_LOGI(TAG, "connecting to \"%s\"", ssid);
    return esp_wifi_start();
}

esp_err_t wifi_prov_try_connect(const char *ssid, const char *pass)
{
    esp_timer_stop(s_retry_timer);
    s_validating = true;
    s_sta_wanted = true;
    s_retries = 0;
    s_reason[0] = '\0';
    s_state = WIFI_PROV_CONNECTING;

    esp_wifi_disconnect();
    esp_err_t err = apply_sta_config(ssid, pass);
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(TAG, "validating \"%s\"", ssid);
    return esp_wifi_connect();
}

bool wifi_prov_wait_connected(int timeout_ms)
{
    EventBits_t bits = xEventGroupWaitBits(s_events, BIT_CONNECTED, pdFALSE, pdTRUE,
                                          pdMS_TO_TICKS(timeout_ms));
    return (bits & BIT_CONNECTED) != 0;
}

wifi_prov_state_t wifi_prov_state(void)
{
    return s_state;
}

const char *wifi_prov_state_reason(void)
{
    return s_reason;
}

const char *wifi_prov_ip(void)
{
    return s_ip;
}

const char *wifi_prov_ssid(void)
{
    return s_ssid;
}

const char *wifi_prov_pass(void)
{
    return s_pass;
}

int wifi_prov_rssi(void)
{
    wifi_ap_record_t info;
    if (esp_wifi_sta_get_ap_info(&info) != ESP_OK) {
        return 0;
    }
    return info.rssi;
}
