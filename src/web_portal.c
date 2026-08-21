#include "web_portal.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "storage.h"
#include "wifi_prov.h"

static const char *TAG = "portal";

extern const char portal_html_start[] asm("_binary_portal_html_start");
extern const char portal_html_end[] asm("_binary_portal_html_end");

#define SCAN_MAX 20
#define BODY_MAX 512
#define FILE_CHUNK 2048
#define PATH_MAX_LEN 128

static httpd_handle_t s_server;

static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text) {
        return httpd_resp_send_500(req);
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, text);
    cJSON_free(text);
    return err;
}

static esp_err_t send_error(httpd_req_t *req, const char *status, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "error", message);
    httpd_resp_set_status(req, status);
    return send_json(req, root);
}

static esp_err_t h_portal_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, portal_html_start, portal_html_end - portal_html_start - 1);
}

static esp_err_t h_scan(httpd_req_t *req)
{
    wifi_scan_config_t scan = {.show_hidden = false};
    esp_err_t err = esp_wifi_scan_start(&scan, true);
    if (err != ESP_OK) {
        return send_error(req, "503 Service Unavailable", "扫描失败，请重试");
    }

    uint16_t found = SCAN_MAX;
    wifi_ap_record_t records[SCAN_MAX];
    if (esp_wifi_scan_get_ap_records(&found, records) != ESP_OK) {
        return send_error(req, "503 Service Unavailable", "扫描失败，请重试");
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *nets = cJSON_AddArrayToObject(root, "networks");

    for (uint16_t i = 0; i < found; ++i) {
        const char *ssid = (const char *)records[i].ssid;
        if (ssid[0] == '\0') {
            continue;
        }
        bool dup = false;
        for (uint16_t j = 0; j < i; ++j) {
            if (strcmp(ssid, (const char *)records[j].ssid) == 0) {
                dup = true;
                break;
            }
        }
        if (dup) {
            continue;
        }
        cJSON *net = cJSON_CreateObject();
        cJSON_AddStringToObject(net, "ssid", ssid);
        cJSON_AddNumberToObject(net, "rssi", records[i].rssi);
        cJSON_AddBoolToObject(net, "secure", records[i].authmode != WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(nets, net);
    }

    ESP_LOGI(TAG, "scan found %u networks", (unsigned)found);
    return send_json(req, root);
}

static esp_err_t read_body(httpd_req_t *req, char *buf, size_t len)
{
    if (req->content_len == 0 || req->content_len >= len) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t off = 0;
    while (off < req->content_len) {
        int n = httpd_req_recv(req, buf + off, req->content_len - off);
        if (n <= 0) {
            return ESP_FAIL;
        }
        off += n;
    }
    buf[off] = '\0';
    return ESP_OK;
}

static esp_err_t h_save(httpd_req_t *req)
{
    char body[BODY_MAX];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        return send_error(req, "400 Bad Request", "请求内容无效");
    }

    cJSON *json = cJSON_Parse(body);
    if (!json) {
        return send_error(req, "400 Bad Request", "请求格式错误");
    }
    const cJSON *j_ssid = cJSON_GetObjectItemCaseSensitive(json, "ssid");
    const cJSON *j_pass = cJSON_GetObjectItemCaseSensitive(json, "pass");

    if (!cJSON_IsString(j_ssid) || j_ssid->valuestring[0] == '\0') {
        cJSON_Delete(json);
        return send_error(req, "400 Bad Request", "网络名称不能为空");
    }
    if (strlen(j_ssid->valuestring) > WIFI_SSID_MAX) {
        cJSON_Delete(json);
        return send_error(req, "400 Bad Request", "网络名称过长");
    }
    const char *pass = cJSON_IsString(j_pass) ? j_pass->valuestring : "";
    if (strlen(pass) > WIFI_PASS_MAX) {
        cJSON_Delete(json);
        return send_error(req, "400 Bad Request", "密码过长");
    }

    char ssid[WIFI_SSID_MAX + 1];
    char password[WIFI_PASS_MAX + 1];
    strlcpy(ssid, j_ssid->valuestring, sizeof(ssid));
    strlcpy(password, pass, sizeof(password));
    cJSON_Delete(json);

    esp_err_t err = wifi_prov_try_connect(ssid, password);
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "无法启动连接");
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "state", "connecting");
    return send_json(req, root);
}

static void restart_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

static void restart_once(void)
{
    static bool scheduled;
    if (scheduled) {
        return;
    }
    scheduled = true;
    xTaskCreate(restart_task, "restart", 2048, NULL, 4, NULL);
}

static esp_err_t h_prov_status(httpd_req_t *req)
{
    wifi_prov_state_t state = wifi_prov_state();
    cJSON *root = cJSON_CreateObject();

    if (state == WIFI_PROV_CONNECTED) {
        wifi_prov_save_creds(wifi_prov_ssid(), wifi_prov_pass());

        cJSON_AddStringToObject(root, "state", "connected");
        cJSON_AddStringToObject(root, "ip", wifi_prov_ip());
        ESP_LOGI(TAG, "provisioned, restarting into STA mode");
        restart_once();
    } else if (state == WIFI_PROV_FAILED) {
        cJSON_AddStringToObject(root, "state", "failed");
        cJSON_AddStringToObject(root, "reason", wifi_prov_state_reason());
    } else {
        cJSON_AddStringToObject(root, "state", "connecting");
    }
    return send_json(req, root);
}

static esp_err_t h_device_status(httpd_req_t *req)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "ip", wifi_prov_ip());
    cJSON_AddStringToObject(root, "ssid", wifi_prov_ssid());
    cJSON_AddStringToObject(root, "mac", mac_str);
    cJSON_AddNumberToObject(root, "rssi", wifi_prov_rssi());
    cJSON_AddNumberToObject(root, "uptime", esp_timer_get_time() / 1000000);
    cJSON_AddNumberToObject(root, "heap", esp_get_free_heap_size());
    cJSON_AddStringToObject(root, "panel", "SSD1675A 2.13\" 212x104");
    cJSON_AddStringToObject(root, "chip", "ESP32-S3 (16MB flash / 8MB PSRAM)");
    return send_json(req, root);
}

static esp_err_t h_reset(httpd_req_t *req)
{
    wifi_prov_clear_creds();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "state", "reset");
    esp_err_t err = send_json(req, root);
    ESP_LOGW(TAG, "credentials cleared over HTTP, restarting");
    restart_once();
    return err;
}

static const char *mime_for(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) {
        return "application/octet-stream";
    }
    if (strcmp(dot, ".html") == 0) {
        return "text/html; charset=utf-8";
    }
    if (strcmp(dot, ".css") == 0) {
        return "text/css";
    }
    if (strcmp(dot, ".js") == 0) {
        return "application/javascript";
    }
    if (strcmp(dot, ".json") == 0) {
        return "application/json";
    }
    if (strcmp(dot, ".svg") == 0) {
        return "image/svg+xml";
    }
    if (strcmp(dot, ".png") == 0) {
        return "image/png";
    }
    if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0) {
        return "image/jpeg";
    }
    if (strcmp(dot, ".ico") == 0) {
        return "image/x-icon";
    }
    return "text/plain; charset=utf-8";
}

static esp_err_t h_static(httpd_req_t *req)
{
    if (strstr(req->uri, "..")) {
        return send_error(req, "403 Forbidden", "invalid path");
    }

    const char *uri = req->uri;
    char rel[PATH_MAX_LEN];
    size_t n = strcspn(uri, "?#");
    if (n >= sizeof(rel)) {
        return send_error(req, "414 URI Too Long", "path too long");
    }
    memcpy(rel, uri, n);
    rel[n] = '\0';
    if (rel[0] == '\0' || strcmp(rel, "/") == 0) {
        strlcpy(rel, "/index.html", sizeof(rel));
    }

    char path[PATH_MAX_LEN + sizeof(STORAGE_BASE_PATH)];
    if (snprintf(path, sizeof(path), "%s%s", STORAGE_BASE_PATH, rel) >= (int)sizeof(path)) {
        return send_error(req, "414 URI Too Long", "path too long");
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        httpd_resp_set_type(req, "text/html; charset=utf-8");
        httpd_resp_set_status(req, "404 Not Found");
        return httpd_resp_sendstr(req,
                                 "<meta charset=utf-8><h3>404</h3>"
                                 "<p>静态文件未找到。请先执行 "
                                 "<code>pio run -t buildfs &amp;&amp; pio run -t uploadfs</code> "
                                 "把 www/ 上传到设备。</p>");
    }

    httpd_resp_set_type(req, mime_for(path));
    char *buf = malloc(FILE_CHUNK);
    if (!buf) {
        fclose(f);
        return httpd_resp_send_500(req);
    }
    size_t read;
    do {
        read = fread(buf, 1, FILE_CHUNK, f);
        if (read && httpd_resp_send_chunk(req, buf, read) != ESP_OK) {
            free(buf);
            fclose(f);
            return ESP_FAIL;
        }
    } while (read == FILE_CHUNK);
    free(buf);
    fclose(f);
    return httpd_resp_send_chunk(req, NULL, 0);
}

esp_err_t web_portal_start(portal_mode_t mode)
{
    if (s_server) {
        return ESP_OK;
    }
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.stack_size = 8192;
    cfg.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    if (mode == PORTAL_PROVISION) {
        httpd_uri_t page = {"/", HTTP_GET, h_portal_page, NULL};
        httpd_uri_t scan = {"/api/scan", HTTP_GET, h_scan, NULL};
        httpd_uri_t save = {"/api/save", HTTP_POST, h_save, NULL};
        httpd_uri_t status = {"/api/status", HTTP_GET, h_prov_status, NULL};
        httpd_register_uri_handler(s_server, &page);
        httpd_register_uri_handler(s_server, &scan);
        httpd_register_uri_handler(s_server, &save);
        httpd_register_uri_handler(s_server, &status);
        ESP_LOGI(TAG, "provisioning server on http://192.168.4.1");
    } else {
        httpd_uri_t status = {"/api/status", HTTP_GET, h_device_status, NULL};
        httpd_uri_t reset = {"/api/reset", HTTP_POST, h_reset, NULL};
        httpd_uri_t files = {"/*", HTTP_GET, h_static, NULL};
        httpd_register_uri_handler(s_server, &status);
        httpd_register_uri_handler(s_server, &reset);
        httpd_register_uri_handler(s_server, &files);
        ESP_LOGI(TAG, "static server on http://%s", wifi_prov_ip());
    }
    return ESP_OK;
}
