#include "comm.h"

#include "board.h"
#include "params.h"

#include <string.h>

#include "driver/twai.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "can_bus";

static bool s_twai_installed;
static bool s_twai_running;

static const char *can_bus_state_name(twai_state_t state)
{
    switch (state) {
    case TWAI_STATE_RUNNING:
        return "running";
    case TWAI_STATE_BUS_OFF:
        return "bus_off";
    case TWAI_STATE_RECOVERING:
        return "recovering";
    case TWAI_STATE_STOPPED:
    default:
        return "stopped";
    }
}

/** After bus-off, IDF returns to STOPPED; must twai_start() again before TX works. */
static void can_bus_maintain(void)
{
    twai_status_info_t st;
    if (twai_get_status_info(&st) != ESP_OK) {
        return;
    }

    if (st.state == TWAI_STATE_BUS_OFF) {
        ESP_LOGW(TAG, "bus-off TEC=%lu REC=%lu — starting recovery (need another node on bus to ACK)",
                 (unsigned long)st.tx_error_counter, (unsigned long)st.rx_error_counter);
        (void)twai_initiate_recovery();
        return;
    }

    if (s_twai_running && st.state == TWAI_STATE_STOPPED) {
        esp_err_t err = twai_start();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "TWAI started (was stopped; e.g. after bus-off recovery)");
        } else {
            ESP_LOGW(TAG, "twai_start failed: %s", esp_err_to_name(err));
        }
    }
}

static twai_timing_config_t can_bus_timing_from_param(int32_t baud_index)
{
    switch (baud_index) {
    case 0:
        return (twai_timing_config_t)TWAI_TIMING_CONFIG_125KBITS();
    case 2:
        return (twai_timing_config_t)TWAI_TIMING_CONFIG_500KBITS();
    case 3:
        return (twai_timing_config_t)TWAI_TIMING_CONFIG_1MBITS();
    case 1:
    default:
        return (twai_timing_config_t)TWAI_TIMING_CONFIG_250KBITS();
    }
}

static void can_bus_shutdown(void)
{
    if (s_twai_running) {
        (void)twai_stop();
        s_twai_running = false;
    }
    if (s_twai_installed) {
        (void)twai_driver_uninstall();
        s_twai_installed = false;
    }
}

void CanBus_Init(void)
{
    if (BOARD_CAN_TX_GPIO == BOARD_GPIO_NONE || BOARD_CAN_RX_GPIO == BOARD_GPIO_NONE) {
        ESP_LOGW(TAG, "CAN pins not configured (board.h)");
        return;
    }

    can_bus_shutdown();

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)BOARD_CAN_TX_GPIO,
        (gpio_num_t)BOARD_CAN_RX_GPIO,
        TWAI_MODE_NORMAL);
    g_config.rx_queue_len = 16;
    g_config.tx_queue_len = 8;

    twai_timing_config_t t_config = can_bus_timing_from_param(Params_GetCanBaud());
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "twai_driver_install failed: %s", esp_err_to_name(err));
        return;
    }
    s_twai_installed = true;

    err = twai_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "twai_start failed: %s", esp_err_to_name(err));
        can_bus_shutdown();
        return;
    }
    s_twai_running = true;

    static const char *const k_baud_labels[] = { "125K", "250K", "500K", "1M" };
    int32_t bi = Params_GetCanBaud();
    if (bi < 0 || bi > 3) {
        bi = 1;
    }
    ESP_LOGI(TAG, "TWAI IO%d TX / IO%d RX, %s, node %ld (CA-IS2062A)",
             BOARD_CAN_TX_GPIO, BOARD_CAN_RX_GPIO, k_baud_labels[bi],
             (long)Params_GetCanNode());
#if CONFIG_ACT1_CAN_BENCH_BEACON && !ACT1_SUPPRESS_BENCH_IO
    ESP_LOGI(TAG, "bench beacon ON: 1 Hz ID 0x7FE (F7.3=%ld)",
             (long)Params_GetCanProtocol());
#endif
}

int CanBus_Send(uint32_t id, const uint8_t *data, uint8_t len)
{
    if (!s_twai_running || data == NULL || len > 8) {
        return -1;
    }

    twai_message_t msg = {
        .identifier = id,
        .data_length_code = len,
        .flags = (id > 0x7FFU) ? TWAI_MSG_FLAG_EXTD : 0,
    };
    for (uint8_t i = 0; i < len; i++) {
        msg.data[i] = data[i];
    }

    esp_err_t err = twai_transmit(&msg, pdMS_TO_TICKS(50));
    return (err == ESP_OK) ? (int)len : -1;
}

int CanBus_Recv(uint32_t *id, uint8_t *data, uint8_t *len)
{
    if (!s_twai_running || id == NULL || data == NULL || len == NULL) {
        return 0;
    }

    twai_message_t msg;
    esp_err_t err = twai_receive(&msg, 0);
    if (err != ESP_OK) {
        return 0;
    }

    *id = msg.identifier;
    *len = msg.data_length_code;
    for (uint8_t i = 0; i < msg.data_length_code && i < 8; i++) {
        data[i] = msg.data[i];
    }
    return 1;
}

void CanBus_Tick(void)
{
    if (!s_twai_running) {
        return;
    }

    can_bus_maintain();

#if CONFIG_ACT1_CAN_BENCH_BEACON && !ACT1_SUPPRESS_BENCH_IO
    static uint32_t s_last_beacon_ms;
    static uint32_t s_beacon_seq;
    const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    if ((now_ms - s_last_beacon_ms) < 1000U) {
        return;
    }
    s_last_beacon_ms = now_ms;
    const uint32_t seq = s_beacon_seq;
    uint8_t payload[8] = { 'A', 'C', 'T', '1',
                           (uint8_t)(seq >> 24), (uint8_t)(seq >> 16),
                           (uint8_t)(seq >> 8), (uint8_t)seq };
    s_beacon_seq++;

    twai_message_t msg = {
        .identifier = 0x7FEU,
        .data_length_code = sizeof(payload),
        .flags = 0,
    };
    memcpy(msg.data, payload, sizeof(payload));

    esp_err_t err = twai_transmit(&msg, pdMS_TO_TICKS(50));
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "bench beacon TX ok id=0x7FE seq=%lu dlc=8", (unsigned long)seq);
    } else {
        twai_status_info_t st;
        if (twai_get_status_info(&st) == ESP_OK) {
            ESP_LOGW(TAG, "bench beacon TX failed seq=%lu: %s (state=%s TEC=%lu REC=%lu)",
                     (unsigned long)seq, esp_err_to_name(err), can_bus_state_name(st.state),
                     (unsigned long)st.tx_error_counter, (unsigned long)st.rx_error_counter);
        } else {
            ESP_LOGW(TAG, "bench beacon TX failed seq=%lu: %s", (unsigned long)seq, esp_err_to_name(err));
        }
    }
#endif
}
