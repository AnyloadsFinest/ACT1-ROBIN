#include "ads1235.h"

#include <string.h>
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ads1235";

static int32_t ads1235_sign_extend24(uint32_t raw24)
{
    if (raw24 & 0x800000) {
        return (int32_t)(raw24 | 0xFF000000);
    }
    return (int32_t)raw24;
}

static esp_err_t ads1235_transfer(ads1235_handle_t *handle, const uint8_t *tx, uint8_t *rx, size_t len)
{
    spi_transaction_t trans = {
        .length = len * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    return spi_device_polling_transmit(handle->spi, &trans);
}

static esp_err_t ads1235_send_command(ads1235_handle_t *handle, uint8_t cmd)
{
    uint8_t tx[2] = { cmd, 0x00 };
    uint8_t rx[2] = { 0 };
    esp_err_t err = ads1235_transfer(handle, tx, rx, sizeof(tx));
    if (err != ESP_OK) {
        return err;
    }
    return ESP_OK;
}

esp_err_t ads1235_init(const ads1235_config_t *config, ads1235_handle_t *handle)
{
    if (config == NULL || handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(handle, 0, sizeof(*handle));
    handle->drdy_pin = config->drdy_pin;

    spi_bus_config_t buscfg = {
        .mosi_io_num = config->mosi_pin,
        .miso_io_num = config->miso_pin,
        .sclk_io_num = config->sclk_pin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 16,
    };

    esp_err_t err = spi_bus_initialize(config->spi_host, &buscfg, SPI_DMA_DISABLED);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return err;
    }
    handle->bus_initialized = (err == ESP_OK);

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = config->spi_clock_hz,
        .mode = 1,
        .spics_io_num = config->cs_pin,
        .queue_size = 1,
        /* Pre-SCLK wakes DOUT; post-SCLK must be 0 (extra clocks after WREG corrupts register LSB). */
        .cs_ena_pretrans = 2,
        .cs_ena_posttrans = 0,
    };

    err = spi_bus_add_device(config->spi_host, &devcfg, &handle->spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(err));
        if (handle->bus_initialized) {
            spi_bus_free(config->spi_host);
            handle->bus_initialized = false;
        }
        return err;
    }

    gpio_config_t drdy_cfg = {
        .pin_bit_mask = 1ULL << config->drdy_pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&drdy_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DRDY gpio_config failed: %s", esp_err_to_name(err));
        spi_bus_remove_device(handle->spi);
        if (handle->bus_initialized) {
            spi_bus_free(config->spi_host);
            handle->bus_initialized = false;
        }
        return err;
    }

    ESP_LOGI(TAG, "SPI MOSI=%d SCLK=%d MISO=%d CS=%d DRDY=%d @ %d Hz",
             config->mosi_pin, config->sclk_pin, config->miso_pin,
             config->cs_pin, config->drdy_pin, config->spi_clock_hz);
    return ESP_OK;
}

esp_err_t ads1235_deinit(ads1235_handle_t *handle)
{
    if (handle == NULL || handle->spi == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    spi_host_device_t host = ADS1235_SPI_HOST;
    esp_err_t err = spi_bus_remove_device(handle->spi);
    handle->spi = NULL;

    if (handle->bus_initialized) {
        esp_err_t free_err = spi_bus_free(host);
        if (err == ESP_OK) {
            err = free_err;
        }
        handle->bus_initialized = false;
    }
    return err;
}

esp_err_t ads1235_reset(ads1235_handle_t *handle)
{
    return ads1235_send_command(handle, ADS1235_CMD_RESET);
}

esp_err_t ads1235_read_reg(ads1235_handle_t *handle, uint8_t reg, uint8_t *value)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t tx[3] = { (uint8_t)(ADS1235_CMD_RREG | (reg & 0x1F)), 0x00, 0x00 };
    uint8_t rx[3] = { 0 };
    esp_err_t err = ads1235_transfer(handle, tx, rx, sizeof(tx));
    if (err != ESP_OK) {
        return err;
    }

    *value = rx[2];
    return ESP_OK;
}

esp_err_t ads1235_write_reg(ads1235_handle_t *handle, uint8_t reg, uint8_t value)
{
    /* No-CRC WREG is exactly 2 bytes; a 3rd byte starts another command on the same CS frame. */
    uint8_t tx[2] = { (uint8_t)(ADS1235_CMD_WREG | (reg & 0x1F)), value };
    uint8_t rx[2] = { 0 };
    esp_err_t err = ads1235_transfer(handle, tx, rx, sizeof(tx));
    if (err != ESP_OK) {
        return err;
    }
    return ESP_OK;
}

esp_err_t ads1235_read_id(ads1235_handle_t *handle, uint8_t *id)
{
    return ads1235_read_reg(handle, ADS1235_REG_ID, id);
}

esp_err_t ads1235_start(ads1235_handle_t *handle)
{
    return ads1235_send_command(handle, ADS1235_CMD_START);
}

esp_err_t ads1235_stop(ads1235_handle_t *handle)
{
    return ads1235_send_command(handle, ADS1235_CMD_STOP);
}

bool ads1235_drdy_ready(ads1235_handle_t *handle)
{
    return gpio_get_level(handle->drdy_pin) == 0;
}

static void ads1235_wait_yield(void)
{
    vTaskDelay(pdMS_TO_TICKS(1));
}

esp_err_t ads1235_wait_drdy(ads1235_handle_t *handle, uint32_t timeout_ms)
{
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while (xTaskGetTickCount() < deadline) {
        if (ads1235_drdy_ready(handle)) {
            return ESP_OK;
        }
        ads1235_wait_yield();
    }
    return ESP_ERR_TIMEOUT;
}

/* Wait for a new conversion: brief wait for DRDY high, then for next ready (low). */
static esp_err_t ads1235_wait_new_drdy(ads1235_handle_t *handle, uint32_t timeout_ms)
{
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    uint32_t release_ms = timeout_ms / 4;
    if (release_ms < 20) {
        release_ms = 20;
    }
    if (release_ms > 120) {
        release_ms = 120;
    }

    if (ads1235_drdy_ready(handle)) {
        const TickType_t release_end = xTaskGetTickCount() + pdMS_TO_TICKS(release_ms);
        while (xTaskGetTickCount() < release_end) {
            if (!ads1235_drdy_ready(handle)) {
                break;
            }
            ads1235_wait_yield();
        }
    }

    while (xTaskGetTickCount() < deadline) {
        if (ads1235_drdy_ready(handle)) {
            return ESP_OK;
        }
        ads1235_wait_yield();
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t ads1235_read_raw(ads1235_handle_t *handle, int32_t *raw)
{
    if (raw == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t tx[5] = { ADS1235_CMD_RDATA, 0x00, 0x00, 0x00, 0x00 };
    uint8_t rx[5] = { 0 };
    esp_err_t err = ads1235_transfer(handle, tx, rx, sizeof(tx));
    if (err != ESP_OK) {
        return err;
    }

    uint32_t code = ((uint32_t)rx[2] << 16) | ((uint32_t)rx[3] << 8) | rx[4];
    *raw = ads1235_sign_extend24(code);
    return ESP_OK;
}

esp_err_t ads1235_read_raw_blocking(ads1235_handle_t *handle, uint32_t timeout_ms, int32_t *raw)
{
    esp_err_t err = ads1235_wait_new_drdy(handle, timeout_ms);
    if (err != ESP_OK) {
        return err;
    }
    return ads1235_read_raw(handle, raw);
}

esp_err_t ads1235_default_init(ads1235_handle_t *handle)
{
    esp_err_t err = ads1235_reset(handle);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t id = 0;
    err = ads1235_read_id(handle, &id);
    if (err != ESP_OK) {
        return err;
    }
    if ((id & ADS1235_DEV_ID_MASK) != ADS1235_DEV_ID_EXPECT) {
        uint8_t tx[3] = { ADS1235_CMD_RREG | ADS1235_REG_ID, 0x00, 0x00 };
        uint8_t rx[3] = { 0 };
        (void)ads1235_transfer(handle, tx, rx, sizeof(rx));
        ESP_LOGE(TAG, "unexpected device ID 0x%02x (expected 0xCx), RREG rx: %02x %02x %02x",
                 id, rx[0], rx[1], rx[2]);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "ADS1235 detected, ID=0x%02x", id);

    /* 10 SPS FIR: slower, quieter readings; good 50/60 Hz rejection. */
    err = ads1235_stop(handle);
    if (err != ESP_OK) {
        return err;
    }

    err = ads1235_write_reg(handle, ADS1235_REG_MODE0, ADS1235_MODE0_10SPS_FIR);
    if (err != ESP_OK) {
        return err;
    }

    err = ads1235_write_reg(handle, ADS1235_REG_PGA, 0x06);
    if (err != ESP_OK) {
        return err;
    }
    /* AIN4 (+) / AIN5 (-): MUXP=0111 (AIN4), MUXN=1000 (AIN5) per datasheet Table 41. */
    err = ads1235_write_reg(handle, ADS1235_REG_INPMUX, 0x78);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(1));

    uint8_t pga = 0;
    uint8_t mux = 0;
    err = ads1235_read_reg(handle, ADS1235_REG_PGA, &pga);
    if (err != ESP_OK) {
        return err;
    }
    err = ads1235_read_reg(handle, ADS1235_REG_INPMUX, &mux);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t mode0 = 0;
    err = ads1235_read_reg(handle, ADS1235_REG_MODE0, &mode0);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "MODE0=0x%02x (10 SPS FIR)", mode0);
    }
    ESP_LOGI(TAG, "PGA=0x%02x INPMUX=0x%02x (expect PGA=0x06 INPMUX=0x78)", pga, mux);
    if (pga != 0x06 || mux != 0x78) {
        ESP_LOGW(TAG, "register readback mismatch (continuing anyway)");
    }

    return ads1235_start(handle);
}
