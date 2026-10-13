#include "w5500_probe.h"

#include "board.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if CONFIG_EXAMPLE_USE_W5500

static const char *TAG = "w5500_probe";

#define W5500_ADDR_OFFSET 16
#define W5500_BSB_OFFSET  3
#define W5500_RWB_OFFSET  2
#define W5500_BSB_COM_REG 0
#define W5500_ACCESS_MODE_READ 0
#define W5500_SPI_OP_MODE_VDM  0
#define W5500_REG_MR       W5500_SPI_REG(0x0000, W5500_BSB_COM_REG)
#define W5500_REG_VERSIONR W5500_SPI_REG(0x0039, W5500_BSB_COM_REG)
#define W5500_REG_PHYCFGR  W5500_SPI_REG(0x002E, W5500_BSB_COM_REG)
#define W5500_MR_RST       0x80U /* MR_RST in WIZnet w5500.h; wizchip_sw_reset() uses setMR(MR_RST) */

static esp_err_t w5500_reg_read(spi_device_handle_t spi, uint32_t reg, uint8_t *val)
{
    uint32_t cmd = (reg >> W5500_ADDR_OFFSET);
    uint32_t addr = ((reg & 0xFFFFU) | (W5500_ACCESS_MODE_READ << W5500_RWB_OFFSET)
                     | W5500_SPI_OP_MODE_VDM);
    spi_transaction_t trans = {
        .flags = SPI_TRANS_USE_RXDATA,
        .cmd = cmd,
        .addr = addr,
        .length = 8,
    };
    esp_err_t err = spi_device_polling_transmit(spi, &trans);
    if (err != ESP_OK) {
        return err;
    }
    *val = trans.rx_data[0];
    return ESP_OK;
}

static esp_err_t w5500_reg_write(spi_device_handle_t spi, uint32_t reg, uint8_t val)
{
    uint32_t cmd = (reg >> W5500_ADDR_OFFSET);
    uint32_t addr = ((reg & 0xFFFFU) | (1U << W5500_RWB_OFFSET) | W5500_SPI_OP_MODE_VDM);
    spi_transaction_t trans = {
        .cmd = cmd,
        .addr = addr,
        .length = 8,
        .tx_buffer = &val,
    };
    return spi_device_polling_transmit(spi, &trans);
}

static esp_err_t w5500_sw_reset(spi_device_handle_t spi)
{
    uint8_t mr = W5500_MR_RST;
    esp_err_t err = w5500_reg_write(spi, W5500_REG_MR, mr);
    if (err != ESP_OK) {
        return err;
    }
    for (int i = 0; i < 100; i++) {
        if (w5500_reg_read(spi, W5500_REG_MR, &mr) != ESP_OK) {
            return ESP_FAIL;
        }
        if ((mr & W5500_MR_RST) == 0) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ESP_ERR_TIMEOUT;
}

void W5500_BoardRstRelease(void)
{
#if (BOARD_ETH_RST_GPIO >= 0)
    const int rst = BOARD_ETH_RST_GPIO;

    gpio_reset_pin(rst);
    gpio_config_t run = {
        .pin_bit_mask = 1ULL << rst,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&run);

    const int level = gpio_get_level(rst);
    ESP_LOGD(TAG, "W5500 RST GPIO%d: input + internal pull-up (readback=%d, expect 1)", rst, level);
    if (level != 1) {
        ESP_LOGW(TAG, "RST still low — W5500 held in reset (check IO%d net / load)", rst);
    }
#else
    (void)0;
#endif
}

void W5500_BoardRstPulse(void)
{
#if (BOARD_ETH_RST_GPIO >= 0)
    const int rst = BOARD_ETH_RST_GPIO;

    gpio_reset_pin(rst);
    gpio_config_t out = {
        .pin_bit_mask = 1ULL << rst,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&out);
    gpio_set_level(rst, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    W5500_BoardRstRelease();
    vTaskDelay(pdMS_TO_TICKS(50));
#endif
}

esp_err_t W5500_ProbeVersion(uint8_t *version_out)
{
    if (version_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *version_out = 0;

    W5500_BoardRstPulse();

    spi_bus_config_t buscfg = {
        .mosi_io_num = CONFIG_EXAMPLE_ETH_SPI_MOSI_GPIO,
        .miso_io_num = CONFIG_EXAMPLE_ETH_SPI_MISO_GPIO,
        .sclk_io_num = CONFIG_EXAMPLE_ETH_SPI_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    esp_err_t err = spi_bus_initialize(CONFIG_EXAMPLE_ETH_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Match ESP-IDF esp_eth_mac_w5500 + WIZnet VDM: 16-bit addr phase, 8-bit control, then data. */
    spi_device_interface_config_t devcfg = {
        .mode = 0,
        .clock_speed_hz = CONFIG_EXAMPLE_ETH_SPI_CLOCK_MHZ * 1000 * 1000,
        .spics_io_num = CONFIG_EXAMPLE_ETH_SPI_CS0_GPIO,
        .queue_size = 1,
        .command_bits = 16,
        .address_bits = 8,
    };
    spi_device_handle_t spi = NULL;
    err = spi_bus_add_device(CONFIG_EXAMPLE_ETH_SPI_HOST, &devcfg, &spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device (CS=%d) failed: %s",
                 CONFIG_EXAMPLE_ETH_SPI_CS0_GPIO, esp_err_to_name(err));
        return err;
    }

    if (w5500_sw_reset(spi) != ESP_OK) {
        ESP_LOGW(TAG, "W5500 software reset timed out (continuing)");
    }

    uint8_t version = 0;
    uint8_t phycfg = 0;
    for (int attempt = 0; attempt < 20; attempt++) {
        if (w5500_reg_read(spi, W5500_REG_VERSIONR, &version) != ESP_OK) {
            version = 0;
        }
        if (version == W5500_VERSIONR_EXPECTED) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    (void)w5500_reg_read(spi, W5500_REG_PHYCFGR, &phycfg);

    spi_bus_remove_device(spi);

    *version_out = version;

    ESP_LOGI(TAG, "SPI probe CS=%d: VERSIONR=0x%02x (expect 0x%02x) PHYCFGR=0x%02x link=%d",
             CONFIG_EXAMPLE_ETH_SPI_CS0_GPIO, version, W5500_VERSIONR_EXPECTED, phycfg,
             (phycfg & 1U) ? 1 : 0); /* PHYCFGR_LNK_ON bit0 — WIZnet w5500.h */

    if (version == W5500_VERSIONR_EXPECTED) {
        ESP_LOGI(TAG, "MCU<->W5500 SPI OK (chip responded)");
        return ESP_OK;
    }
    if (version == 0x00 || version == 0xFF) {
        ESP_LOGE(TAG, "No W5500 response — wrong CS/pins, no power, CH390 on board, or bad wiring");
    } else {
        ESP_LOGE(TAG, "VERSIONR mismatch — not a W5500 (JACK CH390 board?) or SPI mode wrong");
    }
    return ESP_ERR_NOT_FOUND;
}

#else

void W5500_BoardRstRelease(void)
{
}

void W5500_BoardRstPulse(void)
{
}

esp_err_t W5500_ProbeVersion(uint8_t *version_out)
{
    if (version_out != NULL) {
        *version_out = 0;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
