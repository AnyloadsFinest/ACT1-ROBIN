#include "comm.h"

#include "board.h"
#include "params.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "rs485";

static bool s_uart_ready;

static int rs485_baud_from_param(int32_t idx)
{
    static const int rates[] = { 9600, 19200, 38400, 57600, 115200 };
    if (idx < 0 || idx >= (int)(sizeof(rates) / sizeof(rates[0]))) {
        return 9600;
    }
    return rates[idx];
}

static void rs485_set_driver_enable(bool transmit)
{
#if (BOARD_RS485_DE_GPIO >= 0)
    /* CA-IS2092A: DE high = drive bus; with DE+RE tied on PCB, EN high = TX, low = RX. */
    gpio_set_level((gpio_num_t)BOARD_RS485_DE_GPIO, transmit ? 1 : 0);
#endif
}

void Rs485_Init(void)
{
    if (BOARD_RS485_TX_GPIO == BOARD_GPIO_NONE || BOARD_RS485_RX_GPIO == BOARD_GPIO_NONE) {
        ESP_LOGW(TAG, "RS485 pins not configured (board.h)");
        return;
    }

    if (BOARD_RS485_DE_GPIO >= 0) {
        gpio_config_t en = {
            .pin_bit_mask = 1ULL << BOARD_RS485_DE_GPIO,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&en);
        rs485_set_driver_enable(false);
    }

    const uart_port_t port = (uart_port_t)BOARD_RS485_UART_NUM;
    uart_config_t cfg = {
        .baud_rate = rs485_baud_from_param(Params_GetSerBaud()),
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    switch (Params_GetSerParity()) {
    case 1:
        cfg.parity = UART_PARITY_EVEN;
        break;
    case 2:
        cfg.parity = UART_PARITY_ODD;
        break;
    default:
        break;
    }

    if (Params_GetSerStop() >= 2) {
        cfg.stop_bits = UART_STOP_BITS_2;
    }

    esp_err_t err = uart_driver_install(port, 512, 512, 0, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return;
    }
    err = uart_param_config(port, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        return;
    }
    err = uart_set_pin(port, BOARD_RS485_TX_GPIO, BOARD_RS485_RX_GPIO,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        return;
    }

    s_uart_ready = true;
    ESP_LOGI(TAG, "UART%d IO%d TX / IO%d RX / IO%d EN @ %d baud (CA-IS2092A)",
             (int)port, BOARD_RS485_TX_GPIO, BOARD_RS485_RX_GPIO, BOARD_RS485_DE_GPIO,
             cfg.baud_rate);
#if CONFIG_ACT1_RS485_BENCH_HEARTBEAT && !ACT1_SUPPRESS_BENCH_IO
    ESP_LOGI(TAG, "bench heartbeat ON when F7.2 protocol OFF (CONFIG_ACT1_RS485_BENCH_HEARTBEAT)");
#endif
}

void Rs485_Tick(void)
{
#if CONFIG_ACT1_RS485_BENCH_HEARTBEAT && !ACT1_SUPPRESS_BENCH_IO
    if (!s_uart_ready || Params_GetRs485Protocol() != 0) {
        return;
    }
    static uint32_t s_last_ms;
    const uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    if ((now - s_last_ms) < 2000U) {
        return;
    }
    s_last_ms = now;
    static const char line[] = "ACT1-RS485\r\n";
    if (Rs485_Write((const uint8_t *)line, sizeof(line) - 1U) < 0) {
        ESP_LOGW(TAG, "bench heartbeat TX failed");
    }
#endif
}

int Rs485_Write(const uint8_t *data, size_t len)
{
    if (!s_uart_ready || data == NULL || len == 0) {
        return -1;
    }

    const uart_port_t port = (uart_port_t)BOARD_RS485_UART_NUM;
    rs485_set_driver_enable(true);
    int n = (int)uart_write_bytes(port, data, len);
    if (n > 0) {
        (void)uart_wait_tx_done(port, pdMS_TO_TICKS(100));
    }
    rs485_set_driver_enable(false);
    return (n >= 0) ? n : -1;
}

int Rs485_Read(uint8_t *data, size_t max_len)
{
    if (!s_uart_ready || data == NULL || max_len == 0) {
        return 0;
    }

    const uart_port_t port = (uart_port_t)BOARD_RS485_UART_NUM;
    int n = uart_read_bytes(port, data, max_len, 0);
    return (n > 0) ? n : 0;
}
