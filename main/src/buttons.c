#include "buttons.h"
#include "board.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/gpio_num.h"

static const gpio_num_t s_btn_pins[] = {
    (gpio_num_t)BOARD_BTN_LEFT_GPIO,
    (gpio_num_t)BOARD_BTN_RIGHT_GPIO,
    (gpio_num_t)BOARD_BTN_UP_GPIO,
    (gpio_num_t)BOARD_BTN_DOWN_GPIO,
    (gpio_num_t)BOARD_BTN_ENTER_GPIO,
};

static const char *const s_btn_names[] = { "LEFT", "RIGHT", "UP", "DOWN", "ENTER" };

/* WROOM-1 module pad numbers (LEFT, RIGHT, UP, DOWN, ENTER). */
static const int s_btn_module_pads[] = { 25, 26, 22, 23, 24 };

static const char *TAG = "buttons";

static uint8_t s_log_edges_primed;

static uint8_t Button_IsPressed(gpio_num_t pin)
{
    int lvl = gpio_get_level(pin);

#if BOARD_BTN_ACTIVE_LOW
    return (lvl == 0) ? 1U : 0U;
#else
    return (lvl != 0) ? 1U : 0U;
#endif
}

static void Buttons_ConfigureAll(void)
{
    uint64_t mask = 0;
    size_t i;

    for (i = 0; i < sizeof(s_btn_pins) / sizeof(s_btn_pins[0]); i++) {
        gpio_reset_pin(s_btn_pins[i]);
        mask |= (1ULL << s_btn_pins[i]);
    }

    gpio_config_t cfg = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    for (i = 0; i < sizeof(s_btn_pins) / sizeof(s_btn_pins[0]); i++) {
        gpio_hold_dis(s_btn_pins[i]);
        ESP_ERROR_CHECK(gpio_set_pull_mode(s_btn_pins[i], GPIO_PULLUP_ONLY));
    }
}

static void Buttons_MonitorTask(void *arg)
{
    int last_raw[5];
    size_t i;

    (void)arg;
    for (i = 0; i < 5; i++) {
        last_raw[i] = -1;
    }

    for (;;) {
        for (i = 0; i < 5; i++) {
            int lvl = gpio_get_level(s_btn_pins[i]);
            if (lvl != last_raw[i]) {
                ESP_LOGI(TAG, "GPIO%d raw %d->%d (%s)",
                         s_btn_pins[i], last_raw[i], lvl, s_btn_names[i]);
                last_raw[i] = lvl;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void Buttons_StartMonitorTask(void)
{
    static uint8_t started;

    if (started != 0U) {
        return;
    }
    started = 1U;
    xTaskCreate(Buttons_MonitorTask, "btn_mon", 2048, NULL, 10, NULL);
}

void Buttons_LogLevels(void)
{
    ESP_LOGI(TAG,
             "levels (1=pressed): L=%d R=%d U=%d D=%d E=%d  raw: L=%d R=%d U=%d D=%d E=%d",
             (int)Button_IsPressed(s_btn_pins[0]),
             (int)Button_IsPressed(s_btn_pins[1]),
             (int)Button_IsPressed(s_btn_pins[2]),
             (int)Button_IsPressed(s_btn_pins[3]),
             (int)Button_IsPressed(s_btn_pins[4]),
             (int)gpio_get_level(s_btn_pins[0]),
             (int)gpio_get_level(s_btn_pins[1]),
             (int)gpio_get_level(s_btn_pins[2]),
             (int)gpio_get_level(s_btn_pins[3]),
             (int)gpio_get_level(s_btn_pins[4]));
}

static void Buttons_LogEdge(const char *name, uint8_t now, uint8_t *last)
{
    if (now == *last) {
        return;
    }
    ESP_LOGI(TAG, "btn %s %s", name, now ? "pressed" : "released");
    *last = now;
}

void Buttons_LogEdges(uint8_t left, uint8_t right, uint8_t up, uint8_t down, uint8_t enter)
{
    static uint8_t last_left;
    static uint8_t last_right;
    static uint8_t last_up;
    static uint8_t last_down;
    static uint8_t last_enter;

    if (s_log_edges_primed == 0U) {
        last_left = left;
        last_right = right;
        last_up = up;
        last_down = down;
        last_enter = enter;
        s_log_edges_primed = 1U;
        ESP_LOGI(TAG, "edge log armed (1=pressed in levels line above)");
        return;
    }

    Buttons_LogEdge("LEFT", left, &last_left);
    Buttons_LogEdge("RIGHT", right, &last_right);
    Buttons_LogEdge("UP", up, &last_up);
    Buttons_LogEdge("DOWN", down, &last_down);
    Buttons_LogEdge("ENTER", enter, &last_enter);
}

void Buttons_Init(void)
{
    size_t i;

    s_log_edges_primed = 0U;
    Buttons_ConfigureAll();
    ESP_LOGI(TAG, "Buttons (IO=GPIO, active low, internal pull-up):");
    for (i = 0; i < sizeof(s_btn_pins) / sizeof(s_btn_pins[0]); i++) {
        ESP_LOGI(TAG, "  %s: GPIO%d (IO%d) module pad %d",
                 s_btn_names[i], s_btn_pins[i], s_btn_pins[i], s_btn_module_pads[i]);
    }
}

void Buttons_Read(uint8_t *left, uint8_t *right, uint8_t *up, uint8_t *down, uint8_t *enter)
{
    if (left != NULL) {
        *left = Button_IsPressed(s_btn_pins[0]);
    }
    if (right != NULL) {
        *right = Button_IsPressed(s_btn_pins[1]);
    }
    if (up != NULL) {
        *up = Button_IsPressed(s_btn_pins[2]);
    }
    if (down != NULL) {
        *down = Button_IsPressed(s_btn_pins[3]);
    }
    if (enter != NULL) {
        *enter = Button_IsPressed(s_btn_pins[4]);
    }
}
