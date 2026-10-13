#include "dig_io.h"

#include "board.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "dig_io";

static const int s_out_gpio[BOARD_OUT_COUNT] = {
    BOARD_OUT1_GPIO, BOARD_OUT2_GPIO, BOARD_OUT3_GPIO
};
static const int s_in_gpio[BOARD_IN_COUNT] = {
    BOARD_IN1_GPIO, BOARD_IN2_GPIO
};

static uint8_t s_out_latched[BOARD_OUT_COUNT];

static int out_gpio_level(uint8_t on)
{
#if BOARD_OUT_ACTIVE_HIGH
    return (on != 0U) ? 1 : 0;
#else
    return (on != 0U) ? 0 : 1;
#endif
}

static int in_gpio_to_closed(int level)
{
#if BOARD_IN_ACTIVE_HIGH
    return (level != 0) ? 1 : 0;
#else
    return (level == 0) ? 1 : 0;
#endif
}

static void dig_io_config_input(gpio_num_t gpio)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "gpio_config input IO%d failed: %s", (int)gpio, esp_err_to_name(err));
    }
}

static void dig_io_config_output(gpio_num_t gpio)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "gpio_config IO%d failed: %s", (int)gpio, esp_err_to_name(err));
        return;
    }
    gpio_set_level(gpio, out_gpio_level(0U));
}

void DigIo_Init(void)
{
    uint8_t i;
    uint8_t out_configured = 0U;
    uint8_t in_configured = 0U;

    for (i = 0U; i < (uint8_t)BOARD_OUT_COUNT; i++)
    {
        s_out_latched[i] = 0U;
        if (s_out_gpio[i] != BOARD_GPIO_NONE)
        {
            dig_io_config_output((gpio_num_t)s_out_gpio[i]);
            out_configured++;
        }
    }

    for (i = 0U; i < (uint8_t)BOARD_IN_COUNT; i++)
    {
        if (s_in_gpio[i] != BOARD_GPIO_NONE)
        {
            dig_io_config_input((gpio_num_t)s_in_gpio[i]);
            in_configured++;
        }
    }

    if (out_configured > 0U)
    {
        ESP_LOGI(TAG, "DOUT1–%u: IO%d / IO%d / IO%d (AQY282SX, active-%s)",
                 (unsigned)BOARD_OUT_COUNT,
                 BOARD_OUT1_GPIO, BOARD_OUT2_GPIO, BOARD_OUT3_GPIO,
                 BOARD_OUT_ACTIVE_HIGH ? "high ON" : "low ON");
    }
    if (in_configured > 0U)
    {
        ESP_LOGI(TAG, "DIN1–%u: IO%d / IO%d (ISO1212 OUT, active-%s → CLSD in F8.9)",
                 (unsigned)BOARD_IN_COUNT,
                 BOARD_IN1_GPIO, BOARD_IN2_GPIO,
                 BOARD_IN_ACTIVE_HIGH ? "high" : "low");
    }
}

void DigIo_SetOut(int32_t channel, uint8_t on)
{
    if (channel < 1 || channel > BOARD_OUT_COUNT)
    {
        return;
    }

    const uint8_t idx = (uint8_t)(channel - 1);
    s_out_latched[idx] = (on != 0U) ? 1U : 0U;
    if (s_out_gpio[idx] != BOARD_GPIO_NONE)
    {
        gpio_set_level((gpio_num_t)s_out_gpio[idx], out_gpio_level(s_out_latched[idx]));
    }
}

uint8_t DigIo_GetOut(int32_t channel)
{
    if (channel < 1 || channel > BOARD_OUT_COUNT)
    {
        return 0U;
    }

    return s_out_latched[channel - 1];
}

uint8_t DigIo_GetIn(int32_t channel)
{
    if (channel < 1 || channel > BOARD_IN_COUNT)
    {
        return 0U;
    }

    const int gpio = s_in_gpio[channel - 1];
    if (gpio == BOARD_GPIO_NONE)
    {
        return 0U;
    }

    const int level = gpio_get_level((gpio_num_t)gpio);
    return (uint8_t)in_gpio_to_closed(level);
}

uint8_t DigIo_GetInMask(void)
{
    uint8_t mask = 0U;
    uint8_t i;

    for (i = 0U; i < (uint8_t)BOARD_IN_COUNT; i++)
    {
        if (DigIo_GetIn((int32_t)i + 1) != 0U)
        {
            mask |= (uint8_t)(1U << i);
        }
    }

    return mask;
}
