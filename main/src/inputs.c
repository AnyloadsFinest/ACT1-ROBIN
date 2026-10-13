#include "inputs.h"

#include "dig_io.h"
#include "params.h"
#include "remote_cmd.h"
#include "board.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "inputs";

enum {
    IN_FN_OFF = 0,
    IN_FN_ZERO,
    IN_FN_TARE,
    IN_FN_NET_GROSS,
    IN_FN_HOLD_PEAK,
    IN_FN_RESET_PEAK,
    IN_FN_STATUS
};

static uint8_t s_last[BOARD_IN_COUNT];
static uint8_t s_mask;

#if CONFIG_ACT1_DI_BENCH_LOG
static void Inputs_LogDiState(uint8_t mask, const char *reason)
{
    const char *in1 = (mask & 1U) != 0U ? "CLSD" : "OPEN";
    const char *in2 = (mask & 2U) != 0U ? "CLSD" : "OPEN";

#if (BOARD_IN1_GPIO != BOARD_GPIO_NONE)
    const int g1 = gpio_get_level((gpio_num_t)BOARD_IN1_GPIO);
#if (BOARD_IN2_GPIO != BOARD_GPIO_NONE)
    const int g2 = gpio_get_level((gpio_num_t)BOARD_IN2_GPIO);
    ESP_LOGI(TAG, "DI [%s] IN1=%s IN2=%s (IO%d=%d IO%d=%d)",
             reason, in1, in2, BOARD_IN1_GPIO, g1, BOARD_IN2_GPIO, g2);
#else
    ESP_LOGI(TAG, "DI [%s] IN1=%s IN2=%s (IO%d=%d)",
             reason, in1, in2, BOARD_IN1_GPIO, g1);
#endif
#else
    ESP_LOGI(TAG, "DI [%s] IN1=%s IN2=%s", reason, in1, in2);
#endif
}
#endif

void Inputs_Init(void)
{
    uint8_t i;

    s_mask = 0U;
    for (i = 0U; i < (uint8_t)BOARD_IN_COUNT; i++)
    {
        s_last[i] = 0U;
    }

#if CONFIG_ACT1_DI_BENCH_LOG
    s_mask = DigIo_GetInMask();
    Inputs_LogDiState(s_mask, "init");
    ESP_LOGI(TAG, "DI bench log ON (change + every 2s); set F6 INx FUNCTION=OFF while testing");
#endif
}

static void Inputs_Apply(int32_t channel, uint8_t closed, uint8_t rising)
{
    int32_t fn = Params_GetInFn(channel);

    switch (fn)
    {
    case IN_FN_ZERO:
        if (rising)
        {
            (void)RemoteCmd_Zero();
        }
        break;
    case IN_FN_TARE:
        if (rising)
        {
            (void)RemoteCmd_Tare();
        }
        break;
    case IN_FN_NET_GROSS:
        if (rising)
        {
            (void)RemoteCmd_SetNet((uint8_t)!Params_GetNetMode());
        }
        break;
    case IN_FN_HOLD_PEAK:
        (void)RemoteCmd_HoldPeak(closed);
        break;
    case IN_FN_RESET_PEAK:
        if (rising)
        {
            (void)RemoteCmd_ResetPeak();
        }
        break;
    case IN_FN_STATUS:
        /* Optional: require a live/ready contact before weighing. */
        break;
    default:
        break;
    }
}

void Inputs_Tick(void)
{
    uint8_t i;
    uint8_t now;
    uint8_t rising;
    const uint8_t prev_mask = s_mask;

    s_mask = DigIo_GetInMask();
    Params_SetInputStat((int32_t)s_mask);

#if CONFIG_ACT1_DI_BENCH_LOG
    static uint32_t s_log_last_ms;
    static uint8_t s_log_armed;

    const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    if (!s_log_armed)
    {
        s_log_armed = 1U;
        s_log_last_ms = now_ms;
    }

    if (s_mask != prev_mask)
    {
        Inputs_LogDiState(s_mask, "change");
        s_log_last_ms = now_ms;
    }
    else if ((now_ms - s_log_last_ms) >= 2000U)
    {
        s_log_last_ms = now_ms;
        Inputs_LogDiState(s_mask, "2s");
    }
#endif

    for (i = 0U; i < (uint8_t)BOARD_IN_COUNT; i++)
    {
        now = DigIo_GetIn((int32_t)i + 1);
        rising = (now != 0U && s_last[i] == 0U) ? 1U : 0U;
        Inputs_Apply((int32_t)i + 1, now, rising);
        s_last[i] = now;
    }
}

uint8_t Inputs_GetLiveMask(void)
{
    return s_mask;
}
