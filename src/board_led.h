#pragma once

#include <stdint.h>

#ifndef BOARD_RGB_LED_GPIO
#define BOARD_RGB_LED_GPIO 48
#endif

void board_led_init(void);
void board_led_breathe_start(uint8_t r, uint8_t g, uint8_t b, uint32_t period_ms);
void board_led_breathe_stop(void);
