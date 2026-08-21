#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/spi_master.h"

#ifndef EPD_PIN_SCK
#define EPD_PIN_SCK 12
#endif
#ifndef EPD_PIN_SDA
#define EPD_PIN_SDA 11
#endif
#ifndef EPD_PIN_CS
#define EPD_PIN_CS 10
#endif
#ifndef EPD_PIN_DC
#define EPD_PIN_DC 9
#endif
#ifndef EPD_PIN_RST
#define EPD_PIN_RST 14
#endif
#ifndef EPD_PIN_BUSY
#define EPD_PIN_BUSY 3
#endif
#ifndef EPD_SPI_HOST
#define EPD_SPI_HOST SPI2_HOST
#endif
#ifndef EPD_SPI_HZ
#define EPD_SPI_HZ (4 * 1000 * 1000)
#endif

#define EPD_RAM_WIDTH 128
#define EPD_PANEL_WIDTH 122
#define EPD_PANEL_HEIGHT 250
#define EPD_ROW_BYTES (EPD_RAM_WIDTH / 8)
#define EPD_BUF_SIZE ((size_t)EPD_ROW_BYTES * EPD_PANEL_HEIGHT)

esp_err_t epd_init(void);
void epd_refresh_full(const uint8_t *bw);
void epd_hibernate(void);
