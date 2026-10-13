/*
 * ACT1 display, menu, and 5-button input on ESP32-S3.
 *
 * OLED: SCL GPIO16, SDA GPIO17
 * Buttons (active-low): UP 14, DOWN 21, CONFIRM 47, LEFT 48, RIGHT 45
 * Enter menu: press any two buttons together, or CONFIRM on the weight screen.
 * Weight screen: UP zero, DOWN tare, LEFT clear tare, RIGHT net/gross.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "driver/i2c_master.h"
#include "display.h"
#include "buttons.h"
#include "menu.h"
#include "params.h"
#include "scale.h"
#include "analog_out.h"
#include "adc_front.h"
#include "dig_io.h"
#include "outputs.h"
#include "inputs.h"
#include "comm.h"
#include "main.h"
#include "robin_eth.h"
#include "robin_net.h"
#include "robin_wifi.h"
#include "nvs_flash.h"
#include "act1_ui.h"
#include "sdkconfig.h"
#include "board.h"

static const char *TAG = "act1";

#define I2C_SCL_GPIO    16
#define I2C_SDA_GPIO    17
#define I2C_PORT        0
#define BTN_DEBOUNCE_MS 150U

uint32_t HAL_GetTick(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void Act1_ProcessButtons(void)
{
    static uint8_t synced = 0U;
    static uint8_t last_up = 0U;
    static uint8_t last_down = 0U;
    static uint8_t last_left = 0U;
    static uint8_t last_right = 0U;
    static uint8_t last_enter = 0U;
    static uint8_t last_pressed_count = 0U;
    static uint32_t last_action_ms = 0U;

    uint8_t left;
    uint8_t right;
    uint8_t up;
    uint8_t down;
    uint8_t enter;
    uint8_t pressed_count;
    uint32_t now = HAL_GetTick();

    Buttons_Read(&left, &right, &up, &down, &enter);
    Buttons_LogEdges(left, right, up, down, enter);
    pressed_count = (uint8_t)(up + down + left + right + enter);

    if (!synced) {
        last_up = up;
        last_down = down;
        last_left = left;
        last_right = right;
        last_enter = enter;
        last_pressed_count = pressed_count;
        synced = 1U;
        return;
    }

    if ((now - last_action_ms) < BTN_DEBOUNCE_MS) {
        last_up = up;
        last_down = down;
        last_left = left;
        last_right = right;
        last_enter = enter;
        last_pressed_count = pressed_count;
        return;
    }

    if (pressed_count >= 2U && last_pressed_count < 2U) {
        if (!Menu_IsActive()) {
            ESP_LOGI(TAG, "button combo -> menu");
            printf("button combo\n");
            fflush(stdout);
            Menu_Enter();
        } else {
            ESP_LOGI(TAG, "button combo -> weigh");
            Menu_Exit();
        }
        last_action_ms = now;
    } else if (!Menu_IsActive()) {
        if (up && !last_up) {
            ESP_LOGI(TAG, "weigh ZERO");
            Scale_DoZero();
            last_action_ms = now;
        } else if (down && !last_down) {
            ESP_LOGI(TAG, "weigh TARE");
            Scale_DoTare();
            last_action_ms = now;
        } else if (left && !last_left) {
            ESP_LOGI(TAG, "weigh CLEAR TARE");
            Scale_ClearTare();
            last_action_ms = now;
        } else if (right && !last_right) {
            ESP_LOGI(TAG, "weigh NET/GROSS");
            Params_SetNetMode(Params_GetNetMode() == 0 ? 1 : 0);
            last_action_ms = now;
        } else if (enter && !last_enter) {
            ESP_LOGI(TAG, "button CONFIRM");
            Menu_Enter();
            last_action_ms = now;
        }
    } else if (up && !last_up) {
        ESP_LOGI(TAG, "button UP");
        Menu_HandleButton(MENU_BTN_UP);
        last_action_ms = now;
    } else if (down && !last_down) {
        ESP_LOGI(TAG, "button DOWN");
        Menu_HandleButton(MENU_BTN_DOWN);
        last_action_ms = now;
    } else if (left && !last_left) {
        ESP_LOGI(TAG, "button LEFT");
        Menu_HandleButton(MENU_BTN_LEFT);
        last_action_ms = now;
    } else if (right && !last_right) {
        ESP_LOGI(TAG, "button RIGHT");
        Menu_HandleButton(MENU_BTN_RIGHT);
        last_action_ms = now;
    } else if (enter && !last_enter) {
        ESP_LOGI(TAG, "button CONFIRM");
        Menu_HandleButton(MENU_BTN_ENTER);
        last_action_ms = now;
    }

    last_up = up;
    last_down = down;
    last_left = left;
    last_right = right;
    last_enter = enter;
    last_pressed_count = pressed_count;
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Initialize I2C bus");
    i2c_master_bus_handle_t i2c_bus = NULL;
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus));

    Buttons_Init();
    Params_Init();

#if CONFIG_ACT1_SKIP_ADC
    ESP_LOGW(TAG, "ACT1_SKIP_ADC: W5500 eth without ADS1235 (SPI2 not used by ADC)");
    RobinNet_Init();
    RobinWifi_Start();
    RobinEth_Start();
#else
    AdcFront_Init();
#endif

    DigIo_Init();
#if CONFIG_ACT1_DI_BENCH_LOG && (BOARD_IN1_GPIO != BOARD_GPIO_NONE)
    ESP_LOGI(TAG, "FW %s — watch inputs: tag for DI OPEN/CLSD (IO%d IO%d)",
             FIRMWARE_VERSION_STR, BOARD_IN1_GPIO, BOARD_IN2_GPIO);
#endif
    Display_Init(i2c_bus);
#if !CONFIG_ACT1_SKIP_ADC
    Scale_Init();
#endif
    AnalogOut_Init();
    Outputs_Init();
    Inputs_Init();
    Comm_Init();
#if !CONFIG_ACT1_SKIP_ADC
    ESP_LOGI(TAG, "network: Wi-Fi AP + W5500 eth");
    RobinNet_Init();
    RobinWifi_Start();
    RobinEth_Start();
#endif
    Display_PlayBootAnimation();
    /* Re-apply pull-ups after ETH/display init; log idle levels (expect all 0). */
    Buttons_Init();
    Buttons_LogLevels();
    Act1_ProcessButtons(); /* sync edge detector to released state before UI loop */

    while (1) {
        Act1_ProcessButtons();
#if !CONFIG_ACT1_SKIP_ADC
        Scale_Tick();
        Act1_ProcessButtons(); /* poll again after ADC (Scale_Tick can block ~200 ms) */
#endif

        AnalogOut_Tick();
        Outputs_Tick();
        Inputs_Tick();
        Comm_Tick();
        {
#if CONFIG_ACT1_SKIP_ADC
            Menu_Tick();
            if (!Menu_IsActive()) {
                Display_Clear();
                Display_SetCursor(0, 0);
                Display_WriteStringBig("ETH TEST", 1);
                Display_SetCursor(0, 24);
                Display_WriteStringBig("NO ADC", 1);
                Display_Update();
            }
#else
            int32_t shown = Scale_GetDisplayWeight();

            Params_UpdateLiveValues(shown, Scale_GetFilteredAdc());
            Menu_Tick();

            if (!Menu_IsActive()) {
                Display_ShowWeightFormatted(shown,
                                            (uint8_t)Params_GetDecimalPtFor(shown),
                                            Scale_GetUnitsLabel());
            }
#endif
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void Act1_BootYield(void)
{
    Act1_ProcessButtons();
}
