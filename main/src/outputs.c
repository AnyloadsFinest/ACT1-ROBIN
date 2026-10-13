#include "outputs.h"
#include "dig_io.h"
#include "params.h"
#include "scale.h"
#include "board.h"
#include "main.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "outputs";

#if (BOARD_OUT1_GPIO != BOARD_GPIO_NONE) && BOARD_DOUT_BENCH_TOGGLE && \
    (!defined(CONFIG_ACT1_DOUT_BENCH_TOGGLE) || CONFIG_ACT1_DOUT_BENCH_TOGGLE) && \
    !ACT1_SUPPRESS_BENCH_IO
#define OUTPUTS_BENCH_TOGGLE 1
#else
#define OUTPUTS_BENCH_TOGGLE 0
#endif

enum {
    OUT_FN_OFF = 0,
    OUT_FN_ABOVE,
    OUT_FN_BELOW,
    OUT_FN_STABLE,
    OUT_FN_MOTION
};

static uint32_t s_delay_start_ms[BOARD_OUT_COUNT];
static uint8_t  s_latched[BOARD_OUT_COUNT];

void Outputs_Init(void)
{
    uint8_t i;

    for (i = 0U; i < (uint8_t)BOARD_OUT_COUNT; i++)
    {
        s_delay_start_ms[i] = 0U;
        s_latched[i] = 0U;
        DigIo_SetOut((int32_t)i + 1, 0U);
    }

#if OUTPUTS_BENCH_TOGGLE
    ESP_LOGI(TAG, "bench toggle ON: DOUT1–3 alternate high/low every %u s (overrides F5/F8 TEST OUT)",
             (unsigned)(BOARD_DOUT_BENCH_TOGGLE_MS / 1000U));
#endif
}

uint8_t Outputs_Eval(int32_t channel)
{
    int32_t fn;
    int32_t weight;
    int32_t trip;
    int32_t hyst;
    uint8_t idx;
    uint8_t want = 0U;

    if (channel < 1 || channel > BOARD_OUT_COUNT)
    {
        return 0U;
    }

    idx = (uint8_t)(channel - 1);
    fn = Params_GetOutFn(channel);
    if (fn == OUT_FN_OFF)
    {
        s_latched[idx] = 0U;
        return 0U;
    }

    if (fn == OUT_FN_STABLE)
    {
        want = Scale_IsStable();
    }
    else if (fn == OUT_FN_MOTION)
    {
        want = (uint8_t)!Scale_IsStable();
    }
    else
    {
        trip = Params_GetOutTrip(channel);
        hyst = Params_GetOutHyst(channel);
        if (Params_GetOutTrack(channel) != 0)
        {
            weight = Scale_GetNetWeight();
        }
        else
        {
            weight = Scale_GetGrossWeight();
        }

        if (fn == OUT_FN_ABOVE)
        {
            if (s_latched[idx] != 0U)
            {
                want = (weight > (trip - hyst)) ? 1U : 0U;
            }
            else
            {
                want = (weight >= trip) ? 1U : 0U;
            }
        }
        else if (fn == OUT_FN_BELOW)
        {
            if (s_latched[idx] != 0U)
            {
                want = (weight < (trip + hyst)) ? 1U : 0U;
            }
            else
            {
                want = (weight <= trip) ? 1U : 0U;
            }
        }
    }

    s_latched[idx] = want;
    return want;
}

void Outputs_Tick(void)
{
#if OUTPUTS_BENCH_TOGGLE
    static uint32_t s_bench_last_ms;
    static uint8_t s_bench_on;
    static uint8_t s_bench_armed;

    const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    if (!s_bench_armed)
    {
        s_bench_armed = 1U;
        s_bench_last_ms = now_ms;
    }
    if ((now_ms - s_bench_last_ms) >= BOARD_DOUT_BENCH_TOGGLE_MS)
    {
        s_bench_last_ms = now_ms;
        s_bench_on = (uint8_t)!s_bench_on;
        for (int32_t ch = 1; ch <= BOARD_OUT_COUNT; ch++)
        {
            DigIo_SetOut(ch, s_bench_on);
        }
#if (BOARD_OUT1_GPIO >= 0)
        ESP_LOGI(TAG, "bench DOUT: logical %s, IO%d reads %d (expect ~%s)",
                 s_bench_on ? "ON" : "OFF",
                 BOARD_OUT1_GPIO,
                 (int)gpio_get_level((gpio_num_t)BOARD_OUT1_GPIO),
                 s_bench_on ? "3" : "0");
#endif
    }
    return;
#endif

    int32_t ch;
    uint8_t want;
    uint8_t drive;
    int32_t delay_tenths;
    uint32_t now = HAL_GetTick();

    for (ch = 1; ch <= BOARD_OUT_COUNT; ch++)
    {
        if (Params_GetTestOut(ch) != 0)
        {
            DigIo_SetOut(ch, 1U);
            continue;
        }

        want = Outputs_Eval(ch);
        delay_tenths = Params_GetOutDelay(ch);
        if (delay_tenths > 0)
        {
            if (want != DigIo_GetOut(ch))
            {
                if (s_delay_start_ms[ch - 1] == 0U)
                {
                    s_delay_start_ms[ch - 1] = now;
                }
                if ((now - s_delay_start_ms[ch - 1]) < (uint32_t)(delay_tenths * 100))
                {
                    continue;
                }
            }
            s_delay_start_ms[ch - 1] = 0U;
        }

        drive = want;
        if (Params_GetOutNc(ch) != 0)
        {
            drive = (uint8_t)!drive;
        }
        DigIo_SetOut(ch, drive);
    }
}
