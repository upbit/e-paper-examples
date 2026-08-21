file(GLOB APP_SOURCES "*.c")

idf_component_register(
    SRCS ${APP_SOURCES}
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer
)
