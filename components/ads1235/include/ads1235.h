#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ADS1235_SPI_HOST          SPI2_HOST

#define ADS1235_PIN_MOSI          GPIO_NUM_11
#define ADS1235_PIN_SCLK          GPIO_NUM_12
#define ADS1235_PIN_MISO          GPIO_NUM_13
#define ADS1235_PIN_CS            GPIO_NUM_3
#define ADS1235_PIN_DRDY          GPIO_NUM_8

#define ADS1235_CMD_NOP           0x00
#define ADS1235_CMD_RESET         0x06
#define ADS1235_CMD_START         0x08
#define ADS1235_CMD_STOP          0x0A
#define ADS1235_CMD_RDATA         0x12
#define ADS1235_CMD_RREG          0x20
#define ADS1235_CMD_WREG          0x40

#define ADS1235_REG_ID            0x00
#define ADS1235_REG_STATUS        0x01
#define ADS1235_REG_MODE0         0x02
#define ADS1235_REG_MODE1         0x03
#define ADS1235_REG_MODE2         0x04
#define ADS1235_REG_MODE3         0x05
#define ADS1235_REG_REF           0x06
#define ADS1235_REG_PGA           0x10
#define ADS1235_REG_INPMUX        0x11

/* MODE0: DR[6:3] data rate, FILTER[2:0] (000=sinc1 .. 011=sinc4, 100=FIR). */
#define ADS1235_MODE0_10SPS_FIR    0x14u
#define ADS1235_MODE0_20SPS_FIR    0x24u  /* power-on default */
#define ADS1235_MODE0_50SPS_SINC4  0x2Bu
#define ADS1235_MODE0_100SPS_SINC4 0x3Bu

#define ADS1235_DEV_ID_MASK       0xF0
#define ADS1235_DEV_ID_EXPECT     0xC0

typedef struct {
    spi_host_device_t spi_host;
    gpio_num_t mosi_pin;
    gpio_num_t miso_pin;
    gpio_num_t sclk_pin;
    gpio_num_t cs_pin;
    gpio_num_t drdy_pin;
    int spi_clock_hz;
} ads1235_config_t;

typedef struct {
    spi_device_handle_t spi;
    gpio_num_t drdy_pin;
    bool bus_initialized;
} ads1235_handle_t;

#define ADS1235_CONFIG_DEFAULT() { \
    .spi_host = ADS1235_SPI_HOST, \
    .mosi_pin = ADS1235_PIN_MOSI, \
    .miso_pin = ADS1235_PIN_MISO, \
    .sclk_pin = ADS1235_PIN_SCLK, \
    .cs_pin = ADS1235_PIN_CS, \
    .drdy_pin = ADS1235_PIN_DRDY, \
    .spi_clock_hz = 2 * 1000 * 1000, \
}

esp_err_t ads1235_init(const ads1235_config_t *config, ads1235_handle_t *handle);
esp_err_t ads1235_deinit(ads1235_handle_t *handle);

esp_err_t ads1235_reset(ads1235_handle_t *handle);
esp_err_t ads1235_read_id(ads1235_handle_t *handle, uint8_t *id);
esp_err_t ads1235_read_reg(ads1235_handle_t *handle, uint8_t reg, uint8_t *value);
esp_err_t ads1235_write_reg(ads1235_handle_t *handle, uint8_t reg, uint8_t value);

esp_err_t ads1235_start(ads1235_handle_t *handle);
esp_err_t ads1235_stop(ads1235_handle_t *handle);

bool ads1235_drdy_ready(ads1235_handle_t *handle);
esp_err_t ads1235_wait_drdy(ads1235_handle_t *handle, uint32_t timeout_ms);

esp_err_t ads1235_read_raw(ads1235_handle_t *handle, int32_t *raw);
esp_err_t ads1235_read_raw_blocking(ads1235_handle_t *handle, uint32_t timeout_ms, int32_t *raw);

esp_err_t ads1235_default_init(ads1235_handle_t *handle);

#ifdef __cplusplus
}
#endif
