file(GLOB APP_SOURCES "*.c")

idf_component_register(
    SRCS ${APP_SOURCES}
    INCLUDE_DIRS "."
    EMBED_TXTFILES "${CMAKE_SOURCE_DIR}/portal/portal.html"
    REQUIRES
        driver
        esp_timer
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        esp_http_server
        spiffs
)
