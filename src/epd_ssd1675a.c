#include "epd_ssd1675a.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "epd";

static const uint8_t LUT_FULL[] = {
    0x80, 0x60, 0x40, 0x00, 0x00, 0x00, 0x00,
    0x10, 0x60, 0x20, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x60, 0x40, 0x00, 0x00, 0x00, 0x00,
    0x10, 0x60, 0x20, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x03, 0x03, 0x00, 0x00, 0x02,
    0x09, 0x09, 0x00, 0x00, 0x02,
    0x03, 0x03, 0x00, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
};

#define BUSY_TIMEOUT_MS 30000

static spi_device_handle_t s_spi;
static bool s_hibernating = true;
static bool s_power_on;

static void IRAM_ATTR epd_spi_pre(spi_transaction_t *t)
{
    gpio_set_level((gpio_num_t)EPD_PIN_DC, (int)(intptr_t)t->user);
}

static void epd_write(const uint8_t *buf, size_t len, int dc)
{
    const size_t chunk = 2048;
    for (size_t off = 0; off < len;) {
        size_t n = (len - off > chunk) ? chunk : (len - off);
        spi_transaction_t t = {
            .length = n * 8,
            .tx_buffer = buf + off,
            .user = (void *)(intptr_t)dc,
        };
        spi_device_polling_transmit(s_spi, &t);
        off += n;
    }
}

static void epd_cmd(uint8_t c) { epd_write(&c, 1, 0); }

static void epd_data(uint8_t d) { epd_write(&d, 1, 1); }

static bool epd_wait_busy(const char *what)
{
    vTaskDelay(pdMS_TO_TICKS(1));
    TickType_t start = xTaskGetTickCount();
    while (gpio_get_level((gpio_num_t)EPD_PIN_BUSY) == 1) {
        if (xTaskGetTickCount() - start > pdMS_TO_TICKS(BUSY_TIMEOUT_MS)) {
            ESP_LOGE(TAG, "BUSY timeout in %s", what);
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return true;
}

static void epd_reset(void)
{
    gpio_set_level((gpio_num_t)EPD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level((gpio_num_t)EPD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level((gpio_num_t)EPD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    s_hibernating = false;
    s_power_on = false;
    epd_wait_busy("reset");
}

static void epd_set_ram_area(void)
{
    epd_cmd(0x11);
    epd_data(0x03);
    epd_cmd(0x44);
    epd_data(0x00);
    epd_data((EPD_RAM_WIDTH - 1) / 8);
    epd_cmd(0x45);
    epd_data(0x00);
    epd_data(0x00);
    epd_data((EPD_PANEL_HEIGHT - 1) % 256);
    epd_data((EPD_PANEL_HEIGHT - 1) / 256);
    epd_cmd(0x4E);
    epd_data(0x00);
    epd_cmd(0x4F);
    epd_data(0x00);
    epd_data(0x00);
}

static void epd_init_display(void)
{
    if (s_hibernating) {
        epd_reset();
    }
    epd_cmd(0x74);
    epd_data(0x54);
    epd_cmd(0x7E);
    epd_data(0x3B);
    epd_cmd(0x01);
    epd_data(0xF9);
    epd_data(0x00);
    epd_data(0x00);
    epd_cmd(0x3C);
    epd_data(0x03);
    epd_cmd(0x2C);
    epd_data(0x70);
    epd_cmd(0x03);
    epd_data(0x15);
    epd_cmd(0x04);
    epd_data(0x41);
    epd_data(0xA8);
    epd_data(0x32);
    epd_cmd(0x3A);
    epd_data(0x30);
    epd_cmd(0x3B);
    epd_data(0x0A);
    epd_set_ram_area();
}

static void epd_power_on(void)
{
    if (s_power_on) {
        return;
    }
    epd_cmd(0x22);
    epd_data(0xC0);
    epd_cmd(0x20);
    epd_wait_busy("power on");
    s_power_on = true;
}

static void epd_write_ram(uint8_t cmd, const uint8_t *bw)
{
    epd_set_ram_area();
    epd_cmd(cmd);
    epd_write(bw, EPD_BUF_SIZE, 1);
}

esp_err_t epd_init(void)
{
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << EPD_PIN_DC) | (1ULL << EPD_PIN_RST),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&out);

    gpio_config_t in = {
        .pin_bit_mask = (1ULL << EPD_PIN_BUSY),
        .mode = GPIO_MODE_INPUT,
    };
    gpio_config(&in);

    gpio_set_level((gpio_num_t)EPD_PIN_RST, 1);
    gpio_set_level((gpio_num_t)EPD_PIN_DC, 0);

    spi_bus_config_t bus = {
        .mosi_io_num = EPD_PIN_SDA,
        .miso_io_num = -1,
        .sclk_io_num = EPD_PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    esp_err_t err = spi_bus_initialize(EPD_SPI_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        return err;
    }

    spi_device_interface_config_t dev = {
        .clock_speed_hz = EPD_SPI_HZ,
        .mode = 0,
        .spics_io_num = EPD_PIN_CS,
        .queue_size = 4,
        .pre_cb = epd_spi_pre,
    };
    err = spi_bus_add_device(EPD_SPI_HOST, &dev, &s_spi);
    if (err != ESP_OK) {
        return err;
    }

    s_hibernating = true;
    ESP_LOGI(TAG, "SSD1675A ready (%dx%d)", EPD_PANEL_WIDTH, EPD_PANEL_HEIGHT);
    return ESP_OK;
}

void epd_refresh_full(const uint8_t *bw)
{
    epd_init_display();
    epd_cmd(0x32);
    epd_write(LUT_FULL, sizeof(LUT_FULL), 1);
    epd_power_on();
    epd_write_ram(0x24, bw);
    epd_write_ram(0x26, bw);
    epd_cmd(0x22);
    epd_data(0xC4);
    epd_cmd(0x20);
    epd_wait_busy("full refresh");
    ESP_LOGI(TAG, "full refresh done");
}

void epd_hibernate(void)
{
    if (s_power_on) {
        epd_cmd(0x22);
        epd_data(0xC3);
        epd_cmd(0x20);
        epd_wait_busy("power off");
        s_power_on = false;
    }
    epd_cmd(0x10);
    epd_data(0x01);
    s_hibernating = true;
    ESP_LOGI(TAG, "hibernated");
}
