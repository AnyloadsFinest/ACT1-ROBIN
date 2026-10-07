/* Ethernet Basic Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_console.h"
#include "ethernet_init.h"
#include "ads1235.h"
#include "weight.h"
#include "sdkconfig.h"

static const char *TAG = "eth_example";

#define CAL_SAMPLE_COUNT 12

static ads1235_handle_t s_ads1235;
static volatile bool s_adc_stream_log = false;
static volatile bool s_weight_stream = false;
static volatile bool s_weight_log_verbose = false;
static volatile bool s_weight_log_quiet = false;
static volatile bool s_cal_in_progress = false;

#define WEIGHT_STAT_LOG_INTERVAL 50

typedef struct {
    bool valid;
    int32_t zero_raw;
    float grams_per_count;
    float span_grams;
} scale_cal_t;

static scale_cal_t s_cal = { 0 };

static float ads1235_raw_to_mv(int32_t raw)
{
    const float vref_v = 5.0f;
    const float gain = 64.0f;
    return ((float)raw / 8388608.0f) * (vref_v / gain) * 1000.0f;
}

static void ads1235_print_sample(int32_t raw)
{
    printf("raw=%ld  Vdiff=%.4f mV\n", (long)raw, (double)ads1235_raw_to_mv(raw));
}

static bool console_read_line(const char *prompt, char *line, size_t line_size)
{
    if (prompt != NULL) {
        printf("%s", prompt);
        fflush(stdout);
    }
    if (fgets(line, (int)line_size, stdin) == NULL) {
        return false;
    }
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[--len] = '\0';
    }
    return true;
}

static bool console_wait_enter(const char *prompt)
{
    char line[8] = { 0 };
    return console_read_line(prompt, line, sizeof(line));
}

static void sort_int32(int32_t *v, uint8_t n)
{
    for (uint8_t i = 1; i < n; i++) {
        int32_t key = v[i];
        int8_t j = (int8_t)i - 1;
        while (j >= 0 && v[j] > key) {
            v[j + 1] = v[j];
            j--;
        }
        v[j + 1] = key;
    }
}

static int32_t trimmed_mean_raw(int32_t *samples, uint8_t count)
{
    if (count == 0) {
        return 0;
    }
    if (count <= 2) {
        int64_t sum = 0;
        for (uint8_t i = 0; i < count; i++) {
            sum += samples[i];
        }
        return (int32_t)(sum / (int64_t)count);
    }

    int32_t tmp[8];
    memcpy(tmp, samples, count * sizeof(int32_t));
    sort_int32(tmp, count);
    int64_t sum = 0;
    for (uint8_t i = 1; i < count - 1; i++) {
        sum += tmp[i];
    }
    return (int32_t)(sum / (int64_t)(count - 2));
}

typedef struct {
    int32_t buf[8];
    uint8_t count;
    uint8_t cap;
} raw_roll_t;

static raw_roll_t s_raw_roll;

static void raw_roll_reset(void)
{
    s_raw_roll.count = 0;
}

static void raw_roll_set_cap(uint8_t cap)
{
    if (cap < 1) {
        cap = 1;
    }
    if (cap > 8) {
        cap = 8;
    }
    s_raw_roll.cap = cap;
    if (s_raw_roll.count > cap) {
        s_raw_roll.count = cap;
    }
}

static uint8_t raw_avg_depth_from_config(void)
{
    weight_config_t cfg;
    weight_get_config(&cfg);
    int32_t n = cfg.raw_avg_samples;
    if (n < 1) {
        n = 1;
    }
    if (n > 8) {
        n = 8;
    }
    return (uint8_t)n;
}

static void raw_roll_push(int32_t raw)
{
    raw_roll_set_cap(raw_avg_depth_from_config());
    if (s_raw_roll.count < s_raw_roll.cap) {
        s_raw_roll.buf[s_raw_roll.count++] = raw;
        return;
    }
    memmove(&s_raw_roll.buf[0], &s_raw_roll.buf[1], (s_raw_roll.cap - 1) * sizeof(int32_t));
    s_raw_roll.buf[s_raw_roll.cap - 1] = raw;
}

static int32_t raw_roll_value(int32_t latest_raw, float latest_g)
{
    weight_config_t cfg;
    weight_get_config(&cfg);
    const float band = (cfg.raw_track_band_g > 0.0f) ? cfg.raw_track_band_g : 25.0f;
    const float out_g = weight_get_filtered_grams();

    if (s_raw_roll.count < s_raw_roll.cap) {
        return latest_raw;
    }
    if (fabsf(latest_g - out_g) > band) {
        return latest_raw;
    }
    return trimmed_mean_raw(s_raw_roll.buf, s_raw_roll.count);
}

static esp_err_t ads1235_average_raw(int sample_count, int32_t *avg_out)
{
    if (avg_out == NULL || sample_count < 1) {
        return ESP_ERR_INVALID_ARG;
    }

    int64_t sum = 0;
    int got = 0;
    for (int i = 0; i < sample_count; i++) {
        int32_t raw = 0;
        esp_err_t err = ads1235_read_raw_blocking(&s_ads1235, 1500, &raw);
        if (err != ESP_OK) {
            return err;
        }
        sum += raw;
        got++;
    }
    *avg_out = (int32_t)(sum / got);
    return ESP_OK;
}

static float raw_to_grams(int32_t raw)
{
    if (!s_cal.valid) {
        return 0.0f;
    }
    const int32_t adj_raw = raw - s_cal.zero_raw - weight_get_runtime_raw_offset();
    return (float)adj_raw * s_cal.grams_per_count;
}

static esp_err_t run_scale_calibration(void)
{
    int32_t zero_raw = 0;
    int32_t span_raw = 0;
    char line[64] = { 0 };
    float span_grams = 0.0f;

    s_cal_in_progress = true;
    s_weight_stream = false;
    raw_roll_reset();
    weight_reset_filter_state();
    weight_clear_runtime_offset();
    weight_reset_stats();

    printf("\n=== Load cell calibration ===\n");
    printf("Use the serial monitor tab. Follow each prompt in order.\n\n");

    if (!console_wait_enter("Step 1/2 ZERO: Remove all weight. Press Enter when empty... ")) {
        printf("Input cancelled.\n");
        s_cal_in_progress = false;
        return ESP_ERR_INVALID_STATE;
    }

    printf("Averaging %d samples for zero...\n", CAL_SAMPLE_COUNT);
    esp_err_t err = ads1235_average_raw(CAL_SAMPLE_COUNT, &zero_raw);
    if (err != ESP_OK) {
        printf("Zero capture failed: %s\n", esp_err_to_name(err));
        s_cal_in_progress = false;
        return err;
    }
    printf("Zero captured: raw=%ld\n\n", (long)zero_raw);

    if (!console_read_line("Step 2/2 SPAN: Enter known weight in grams (e.g. 500): ", line, sizeof(line))) {
        printf("Input cancelled.\n");
        s_cal_in_progress = false;
        return ESP_ERR_INVALID_STATE;
    }
    span_grams = strtof(line, NULL);
    if (span_grams <= 0.0f) {
        printf("Invalid span weight.\n");
        s_cal_in_progress = false;
        return ESP_ERR_INVALID_ARG;
    }

    printf("Place %.2f g on the scale.\n", (double)span_grams);
    if (!console_wait_enter("Press Enter when span weight is stable... ")) {
        printf("Input cancelled.\n");
        s_cal_in_progress = false;
        return ESP_ERR_INVALID_STATE;
    }

    printf("Averaging %d samples for span...\n", CAL_SAMPLE_COUNT);
    err = ads1235_average_raw(CAL_SAMPLE_COUNT, &span_raw);
    if (err != ESP_OK) {
        printf("Span capture failed: %s\n", esp_err_to_name(err));
        s_cal_in_progress = false;
        return err;
    }

    const float delta = (float)(span_raw - zero_raw);
    if (fabsf(delta) < 64.0f) {
        printf("Span too close to zero (delta=%.0f counts). Check wiring or weight.\n", (double)delta);
        s_cal_in_progress = false;
        return ESP_ERR_INVALID_STATE;
    }

    s_cal.zero_raw = zero_raw;
    s_cal.span_grams = span_grams;
    s_cal.grams_per_count = span_grams / delta;
    s_cal.valid = true;
    s_weight_stream = true;
    s_cal_in_progress = false;
    weight_set_cal_zero_raw(zero_raw);
    raw_roll_reset();
    weight_reset_filter_state();

    printf("\nCalibration OK.\n");
    printf("  zero raw=%ld  span raw=%ld  (delta=%.0f counts)\n",
           (long)zero_raw, (long)span_raw, (double)delta);
    printf("  scale=%.8f g/count\n", (double)s_cal.grams_per_count);
    printf("Streaming weight (g). Commands: filter on | filter level 0-5 | cal\n\n");
    return ESP_OK;
}

static int cmd_adc(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    int32_t raw = 0;
    esp_err_t err = ads1235_read_raw_blocking(&s_ads1235, 500, &raw);
    if (err != ESP_OK) {
        printf("read failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    ads1235_print_sample(raw);
    return 0;
}

static int cmd_regs(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    uint8_t id = 0;
    uint8_t pga = 0;
    uint8_t mux = 0;
    if (ads1235_read_id(&s_ads1235, &id) != ESP_OK ||
        ads1235_read_reg(&s_ads1235, ADS1235_REG_PGA, &pga) != ESP_OK ||
        ads1235_read_reg(&s_ads1235, ADS1235_REG_INPMUX, &mux) != ESP_OK) {
        printf("register read failed\n");
        return 1;
    }
    printf("ID=0x%02x  PGA=0x%02x  INPMUX=0x%02x\n", id, pga, mux);
    return 0;
}

static int cmd_stream(int argc, char **argv)
{
    if (argc == 1) {
        printf("weight stream %s, verbose %s, adc debug %s\n",
               s_weight_stream ? "on" : "off",
               s_weight_log_verbose ? "on" : "off",
               s_adc_stream_log ? "on" : "off");
        return 0;
    }
    if (strcmp(argv[1], "on") == 0) {
        if (!s_cal.valid) {
            printf("calibrate first (boot wizard or 'cal' command)\n");
            return 1;
        }
        s_weight_stream = true;
        s_weight_log_verbose = false;
        s_weight_log_quiet = false;
        s_adc_stream_log = false;
        weight_reset_stats();
        printf("weight stream on (one line: raw + wt + mode)\n");
        return 0;
    }
    if (strcmp(argv[1], "verbose") == 0) {
        s_weight_stream = true;
        s_weight_log_quiet = false;
        s_weight_log_verbose = true;
        s_adc_stream_log = false;
        weight_reset_stats();
        printf("weight stream verbose (multi-line diag)\n");
        return 0;
    }
    if (strcmp(argv[1], "quiet") == 0) {
        s_weight_stream = true;
        s_weight_log_verbose = false;
        s_weight_log_quiet = true;
        s_adc_stream_log = false;
        printf("weight stream quiet (wt only)\n");
        return 0;
    }
    if (strcmp(argv[1], "off") == 0) {
        s_weight_stream = false;
        printf("weight stream off\n");
        return 0;
    }
    if (strcmp(argv[1], "debug") == 0) {
        s_adc_stream_log = true;
        s_weight_stream = false;
        printf("ADC raw/mV debug log on\n");
        return 0;
    }
    if (strcmp(argv[1], "stats") == 0) {
        weight_stats_t st;
        weight_get_stats(&st);
        printf("stats n=%lu raw [%ld..%ld] mean=%.1f std=%.2f\n",
               (unsigned long)st.sample_count, (long)st.raw_min, (long)st.raw_max,
               (double)st.raw_mean, (double)st.raw_std);
        printf("      instant [%.2f..%.2f] mean=%.2f std=%.2f g\n",
               (double)st.instant_min_g, (double)st.instant_max_g,
               (double)st.instant_mean_g, (double)st.instant_std_g);
        printf("      filter fast=%lu slow=%lu  filt=%.2f g\n",
               (unsigned long)st.fast_path_count, (unsigned long)st.slow_path_count,
               (double)st.filtered_last_g);
        return 0;
    }
    printf("usage: stream on | verbose | quiet | off | debug | stats\n");
    return 1;
}

static int cmd_cal(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return (run_scale_calibration() == ESP_OK) ? 0 : 1;
}

static int cmd_filter(int argc, char **argv)
{
    weight_config_t cfg;
    weight_get_config(&cfg);

    if (argc == 1) {
        printf("filter=%s  level=%ld  sample_speed=%ld\n",
               cfg.filter_enable ? "on" : "off",
               (long)cfg.filter_level, (long)cfg.sample_speed_level);
        printf("stab: %ld samples, %ld g bw, instant %ld g  stable=%d\n",
               (long)cfg.stab_time_samples, (long)cfg.stab_bandwidth_g,
               (long)cfg.stab_instant_band_g, weight_get_stable_mark() ? 1 : 0);
        printf("auto_zero: %ld*100ms, band=%ld g\n",
               (long)cfg.auto_zero_time, (long)cfg.auto_zero_tape_g);
        printf("settle_heavy: %s alpha=%.3f exit=%.1f g\n",
               cfg.stable_heavy_enable ? "on" : "off",
               (double)cfg.stable_heavy_alpha, (double)cfg.stable_heavy_exit_g);
        printf("raw_avg: %ld (rolling, ~100 ms/sample)  track_band=%.0f g  step_snap=%.0f g\n",
               (long)cfg.raw_avg_samples, (double)cfg.raw_track_band_g, (double)cfg.step_snap_g);
        printf("usage: filter on|off | level <0-5> | settle <alpha> | stab <t> <band> [exit] | avg <1-8> | step <g> | track <g>\n");
        return 0;
    }

    if (strcmp(argv[1], "on") == 0) {
        cfg.filter_enable = 1;
        weight_set_config(&cfg);
        weight_reset_filter_state();
        printf("filter enabled (level %ld)\n", (long)cfg.filter_level);
        return 0;
    }
    if (strcmp(argv[1], "off") == 0) {
        cfg.filter_enable = 0;
        weight_set_config(&cfg);
        weight_reset_filter_state();
        printf("filter bypass (passthrough)\n");
        return 0;
    }
    if (argc >= 3 && strcmp(argv[1], "level") == 0) {
        long level = strtol(argv[2], NULL, 10);
        if (level < 0 || level > 5) {
            printf("level must be 0..5\n");
            return 1;
        }
        cfg.filter_level = (int32_t)level;
        weight_set_config(&cfg);
        weight_reset_filter_state();
        printf("filter level %ld\n", level);
        return 0;
    }
    if (argc >= 3 && strcmp(argv[1], "settle") == 0) {
        float alpha = strtof(argv[2], NULL);
        if (alpha <= 0.0f || alpha >= 1.0f) {
            printf("settle alpha must be 0..1 (e.g. 0.04 = heavy)\n");
            return 1;
        }
        cfg.stable_heavy_enable = 1;
        cfg.stable_heavy_alpha = alpha;
        weight_set_config(&cfg);
        weight_reset_filter_state();
        printf("settle heavy alpha=%.3f (when STABLE)\n", (double)alpha);
        return 0;
    }
    if (argc >= 4 && strcmp(argv[1], "stab") == 0) {
        long time_s = strtol(argv[2], NULL, 10);
        long band_g = strtol(argv[3], NULL, 10);
        if (time_s < 1 || time_s > 500 || band_g < 1 || band_g > 100) {
            printf("stab time 1..500, bandwidth 1..100 g\n");
            return 1;
        }
        cfg.stab_time_samples = (int32_t)time_s;
        cfg.stab_bandwidth_g = (int32_t)band_g;
        if (argc >= 5) {
            cfg.stable_heavy_exit_g = strtof(argv[4], NULL);
            if (cfg.stable_heavy_exit_g < 1.0f) {
                cfg.stable_heavy_exit_g = 1.0f;
            }
        }
        weight_set_config(&cfg);
        weight_reset_filter_state();
        printf("stab time=%ld band=%ld g exit=%.1f g\n",
               time_s, band_g, (double)cfg.stable_heavy_exit_g);
        return 0;
    }
    if (argc >= 3 && strcmp(argv[1], "avg") == 0) {
        long n = strtol(argv[2], NULL, 10);
        if (n < 1 || n > 8) {
            printf("avg count must be 1..8\n");
            return 1;
        }
        cfg.raw_avg_samples = (int32_t)n;
        weight_set_config(&cfg);
        raw_roll_reset();
        printf("raw rolling avg depth %ld (stable/hold only, still ~100 ms per sample)\n", n);
        return 0;
    }
    if (argc >= 3 && strcmp(argv[1], "step") == 0) {
        cfg.step_snap_g = strtof(argv[2], NULL);
        if (cfg.step_snap_g < 0.0f) {
            cfg.step_snap_g = 0.0f;
        }
        weight_set_config(&cfg);
        printf("step snap threshold %.0f g (0=off)\n", (double)cfg.step_snap_g);
        return 0;
    }
    if (argc >= 3 && strcmp(argv[1], "track") == 0) {
        cfg.raw_track_band_g = strtof(argv[2], NULL);
        if (cfg.raw_track_band_g < 1.0f) {
            cfg.raw_track_band_g = 1.0f;
        }
        weight_set_config(&cfg);
        printf("raw track band %.0f g (use latest raw when |PV-instant| above this)\n",
               (double)cfg.raw_track_band_g);
        return 0;
    }

    printf("usage: filter on|off | level <0-5> | settle <alpha> | stab <t> <band> [exit] | avg <1-8> | step <g> | track <g>\n");
    return 1;
}

static void register_console_commands(void)
{
    const esp_console_cmd_t cmds[] = {
        {
            .command = "adc",
            .help = "Read one ADS1235 sample",
            .hint = NULL,
            .func = &cmd_adc,
        },
        {
            .command = "regs",
            .help = "Print ADS1235 ID, PGA, INPMUX",
            .hint = NULL,
            .func = &cmd_regs,
        },
        {
            .command = "stream",
            .help = "stream on|verbose|quiet|off|debug|stats",
            .hint = NULL,
            .func = &cmd_stream,
        },
        {
            .command = "cal",
            .help = "Run zero/span calibration wizard",
            .hint = NULL,
            .func = &cmd_cal,
        },
        {
            .command = "filter",
            .help = "filter on|off|level|settle|stab|avg",
            .hint = NULL,
            .func = &cmd_filter,
        },
    };

    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
}

static esp_console_repl_t *s_console_repl = NULL;

static void console_init_device(void)
{
    if (s_console_repl != NULL) {
        return;
    }

    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "act1> ";
    repl_config.max_history_len = 8;

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t hw_config =
        ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &s_console_repl));
#elif CONFIG_ESP_CONSOLE_UART
    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &s_console_repl));
#else
#error "No console interface enabled (enable USB Serial/JTAG or UART in menuconfig)"
#endif
}

static void start_console_repl(void)
{
    console_init_device();
    register_console_commands();
    ESP_ERROR_CHECK(esp_console_start_repl(s_console_repl));
    printf("\nConsole ready. stream on|quiet|stats, cal, filter, adc, regs, help\n");
}

static void log_weight_stats_block(void)
{
    weight_stats_t st;
    weight_get_stats(&st);
    if (st.sample_count == 0) {
        return;
    }

    const int32_t raw_span = st.raw_max - st.raw_min;
    ESP_LOGI(TAG,
             "STAT n=%lu  raw span=%ld std=%.1f  wt std=%.2f g  fast/slow=%lu/%lu",
             (unsigned long)st.sample_count, (long)raw_span, (double)st.raw_std,
             (double)st.instant_std_g, (unsigned long)st.fast_path_count,
             (unsigned long)st.slow_path_count);
    weight_reset_stats();
}

static const char *weight_mode_label(const weight_diag_t *d)
{
    if (!d->stable_mark) {
        return "track";
    }
    if (d->settled_heavy) {
        return "hold";
    }
    return "stable";
}

static void ads1235_read_task(void *arg)
{
    (void)arg;
    int32_t raw = 0;

    bool wdt_registered = (esp_task_wdt_add(NULL) == ESP_OK);

    while (1) {
        if (s_cal_in_progress) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        esp_err_t err = ads1235_read_raw_blocking(&s_ads1235, 1500, &raw);
        if (err == ESP_OK) {
            if (wdt_registered) {
                esp_task_wdt_reset();
            }
            if (s_weight_stream && s_cal.valid) {
                raw_roll_push(raw);
                const float latest_g = raw_to_grams(raw);
                const int32_t use_raw = raw_roll_value(raw, latest_g);
                const float instant_g = raw_to_grams(use_raw);
                weight_update(instant_g, use_raw);

                weight_diag_t d;
                weight_get_diag(&d);
                const long wt_g = lroundf(d.output_g);
                weight_stats_t st;
                weight_get_stats(&st);

                if (s_weight_log_quiet) {
                    ESP_LOGI(TAG, "wt=%ld g", wt_g);
                } else if (s_weight_log_verbose) {
                    weight_config_t wcfg;
                    weight_get_config(&wcfg);
                    ESP_LOGI(TAG,
                             "raw=%ld  wt=%ld g  %s | inst=%.1f filt=%.1f heavy=%.1f",
                             (long)use_raw, wt_g, weight_mode_label(&d),
                             (double)d.instant_g, (double)d.filtered_g, (double)d.heavy_g);
                    ESP_LOGI(TAG,
                             "  path=%s err_i=%.1f err_s=%.1f stab=%lu/%ld%s",
                             weight_filter_path_name(d.path),
                             (double)d.instant_error_g, (double)d.stab_error_g,
                             (unsigned long)d.stable_count, (long)wcfg.stab_time_samples,
                             d.auto_zero_fired ? " autozero" : "");
                    if (st.sample_count > 0 &&
                        (st.sample_count % WEIGHT_STAT_LOG_INTERVAL) == 0) {
                        log_weight_stats_block();
                    }
                } else {
                    ESP_LOGI(TAG, "raw=%ld  wt=%ld g  %s",
                             (long)use_raw, wt_g, weight_mode_label(&d));
                }
            } else if (s_adc_stream_log) {
                ESP_LOGI(TAG, "ADS1235 raw: %ld  Vdiff: %.4f mV  inst_if_cal=%.3f g",
                         (long)raw, (double)ads1235_raw_to_mv(raw),
                         (double)raw_to_grams(raw));
            }
        } else if (err != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "ADS1235 read failed: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/** Event handler for Ethernet events */
static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    /* we can get the ethernet driver handle from event data */
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(TAG, "Ethernet Link Up");
        ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Ethernet Link Down");
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet Started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet Stopped");
        break;
    default:
        break;
    }
}

/** Event handler for IP_EVENT_ETH_GOT_IP */
static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(TAG, "Ethernet Got IP Address");
    ESP_LOGI(TAG, "~~~~~~~~~~~");
    ESP_LOGI(TAG, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
    ESP_LOGI(TAG, "~~~~~~~~~~~");
}

void app_main(void)
{
    ESP_LOGI(TAG, "Boot — use this UART (115200) for calibration prompts");

    weight_config_t wcfg = WEIGHT_CONFIG_DEFAULT();
    wcfg.sample_speed_level = 1;
    wcfg.filter_enable = 1;
    weight_init(&wcfg);

    ads1235_config_t adc_cfg = ADS1235_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(ads1235_init(&adc_cfg, &s_ads1235));
    ESP_ERROR_CHECK(ads1235_default_init(&s_ads1235));

    console_init_device();
    ESP_LOGI(TAG, "Calibration starts in 3 s (empty scale, then span weight)...");
    vTaskDelay(pdMS_TO_TICKS(3000));
    if (run_scale_calibration() != ESP_OK) {
        ESP_LOGW(TAG, "Calibration incomplete; type 'cal' when ready");
    }
    xTaskCreate(ads1235_read_task, "ads1235", 4096, NULL, 5, NULL);

    // Initialize Ethernet driver
    uint8_t eth_port_cnt = 0;
    esp_eth_handle_t *eth_handles;
    ESP_ERROR_CHECK(example_eth_init(&eth_handles, &eth_port_cnt));

    // Initialize TCP/IP network interface aka the esp-netif (should be called only once in application)
    ESP_ERROR_CHECK(esp_netif_init());
    // Create default event loop that running in background
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *eth_netifs[eth_port_cnt];
    esp_eth_netif_glue_handle_t eth_netif_glues[eth_port_cnt];

    // Create instance(s) of esp-netif for Ethernet(s)
    if (eth_port_cnt == 1) {
        // Use ESP_NETIF_DEFAULT_ETH when just one Ethernet interface is used and you don't need to modify
        // default esp-netif configuration parameters.
        esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
        eth_netifs[0] = esp_netif_new(&cfg);
        eth_netif_glues[0] = esp_eth_new_netif_glue(eth_handles[0]);
        // Attach Ethernet driver to TCP/IP stack
        ESP_ERROR_CHECK(esp_netif_attach(eth_netifs[0], eth_netif_glues[0]));
    } else {
        // Use ESP_NETIF_INHERENT_DEFAULT_ETH when multiple Ethernet interfaces are used and so you need to modify
        // esp-netif configuration parameters for each interface (name, priority, etc.).
        esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
        esp_netif_config_t cfg_spi = {
            .base = &esp_netif_config,
            .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH
        };
        char if_key_str[10];
        char if_desc_str[10];
        char num_str[3];
        for (int i = 0; i < eth_port_cnt; i++) {
            itoa(i, num_str, 10);
            strcat(strcpy(if_key_str, "ETH_"), num_str);
            strcat(strcpy(if_desc_str, "eth"), num_str);
            esp_netif_config.if_key = if_key_str;
            esp_netif_config.if_desc = if_desc_str;
            esp_netif_config.route_prio -= i*5;
            eth_netifs[i] = esp_netif_new(&cfg_spi);
            eth_netif_glues[i] = esp_eth_new_netif_glue(eth_handles[i]);
            // Attach Ethernet driver to TCP/IP stack
            ESP_ERROR_CHECK(esp_netif_attach(eth_netifs[i], eth_netif_glues[i]));
        }
    }

    // Register user defined event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

    // Start Ethernet driver state machine
    for (int i = 0; i < eth_port_cnt; i++) {
        ESP_ERROR_CHECK(esp_eth_start(eth_handles[i]));
    }

    start_console_repl();

#if CONFIG_EXAMPLE_ETH_DEINIT_AFTER_S >= 0
    // For demonstration purposes, wait and then deinit Ethernet network
    vTaskDelay(pdMS_TO_TICKS(CONFIG_EXAMPLE_ETH_DEINIT_AFTER_S * 1000));
    ESP_LOGI(TAG, "stop and deinitialize Ethernet network...");
    // Stop Ethernet driver state machine and destroy netif
    for (int i = 0; i < eth_port_cnt; i++) {
        ESP_ERROR_CHECK(esp_eth_stop(eth_handles[i]));
        ESP_ERROR_CHECK(esp_eth_del_netif_glue(eth_netif_glues[i]));
        esp_netif_destroy(eth_netifs[i]);
    }
    esp_netif_deinit();
    ESP_ERROR_CHECK(example_eth_deinit(eth_handles, eth_port_cnt));
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP, got_ip_event_handler));
    ESP_ERROR_CHECK(esp_event_handler_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, eth_event_handler));
    ESP_ERROR_CHECK(esp_event_loop_delete_default());
#endif // EXAMPLE_ETH_DEINIT_AFTER_S > 0
}
