#include "weight.h"

#include <math.h>
#include <string.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#define WEIGHT_SAMPLE_MAX 7

typedef struct {
    float alpha_fast;
    float alpha_slow;
    float blend_threshold_g;
    uint8_t trim_window;
} weight_profile_t;

static const weight_profile_t s_profiles[6] = {
    { 1.00f, 1.00f, 1.0f, 1 },
    { 0.92f, 0.35f, 12.0f, 3 },
    { 0.88f, 0.28f, 11.0f, 5 },
    { 0.82f, 0.22f, 10.0f, 5 },
    { 0.75f, 0.18f, 9.0f, 7 },
    { 0.65f, 0.15f, 8.0f, 7 },
};

static weight_config_t s_cfg;
static float s_filtered_g = 0.0f;
static float s_prev_output_g = 0.0f;
static float s_ring[WEIGHT_SAMPLE_MAX];
static uint8_t s_ring_count = 0;
static bool s_first_sample = true;
static uint32_t s_stable_samples = 0;
static bool s_stable_mark = false;
static int32_t s_runtime_raw_offset = 0;
static int32_t s_cal_zero_raw = 0;
static int64_t s_auto_zero_band_enter_us = 0;
static bool s_auto_zero_in_band = false;

static float s_heavy_g = 0.0f;
static float s_output_g = 0.0f;
static bool s_prev_stable_mark = false;
static weight_diag_t s_last_diag;
static weight_stats_t s_stats;
static double s_raw_m2;
static double s_inst_m2;
static uint32_t s_inst_quiet_samples = 0;

static weight_profile_t weight_profile_from_level(int32_t level)
{
    if (level < 0) {
        level = 0;
    }
    if (level > 5) {
        level = 5;
    }
    return s_profiles[level];
}

static float adaptive_alpha(float error_g, const weight_profile_t *profile)
{
    float blend = error_g / profile->blend_threshold_g;
    if (blend > 1.0f) {
        blend = 1.0f;
    }
    return profile->alpha_slow + (blend * (profile->alpha_fast - profile->alpha_slow));
}

static void sort_floats(float *v, uint8_t n)
{
    for (uint8_t i = 1; i < n; i++) {
        float key = v[i];
        int8_t j = (int8_t)i - 1;
        while (j >= 0 && v[j] > key) {
            v[j + 1] = v[j];
            j--;
        }
        v[j + 1] = key;
    }
}

static float trimmed_mean(const float *samples, uint8_t count)
{
    if (count == 0) {
        return 0.0f;
    }
    if (count == 1) {
        return samples[0];
    }
    if (count == 2) {
        return (samples[0] + samples[1]) * 0.5f;
    }

    float tmp[WEIGHT_SAMPLE_MAX];
    memcpy(tmp, samples, count * sizeof(float));
    sort_floats(tmp, count);
    float sum = 0.0f;
    for (uint8_t i = 1; i < count - 1; i++) {
        sum += tmp[i];
    }
    return sum / (float)(count - 2);
}

static void ring_push(float instant_g, uint8_t trim_window)
{
    if (trim_window > WEIGHT_SAMPLE_MAX) {
        trim_window = WEIGHT_SAMPLE_MAX;
    }
    if (trim_window == 0) {
        trim_window = 1;
    }

    if (s_ring_count < trim_window) {
        s_ring[s_ring_count++] = instant_g;
    } else {
        memmove(&s_ring[0], &s_ring[1], (trim_window - 1) * sizeof(float));
        s_ring[trim_window - 1] = instant_g;
    }
}

static void reset_ring(void)
{
    s_ring_count = 0;
    memset(s_ring, 0, sizeof(s_ring));
}

static void stats_push(int32_t raw_adc, float instant_g, weight_filter_path_t path)
{
    s_stats.sample_count++;
    s_stats.raw_last = raw_adc;
    s_stats.filtered_last_g = s_filtered_g;

    if (s_stats.sample_count == 1) {
        s_stats.raw_min = raw_adc;
        s_stats.raw_max = raw_adc;
        s_stats.raw_mean = (float)raw_adc;
        s_stats.instant_min_g = instant_g;
        s_stats.instant_max_g = instant_g;
        s_stats.instant_mean_g = instant_g;
        s_raw_m2 = 0.0;
        s_inst_m2 = 0.0;
    } else {
        if (raw_adc < s_stats.raw_min) {
            s_stats.raw_min = raw_adc;
        }
        if (raw_adc > s_stats.raw_max) {
            s_stats.raw_max = raw_adc;
        }
        if (instant_g < s_stats.instant_min_g) {
            s_stats.instant_min_g = instant_g;
        }
        if (instant_g > s_stats.instant_max_g) {
            s_stats.instant_max_g = instant_g;
        }

        const float n = (float)s_stats.sample_count;
        const float raw_f = (float)raw_adc;
        const float dr = raw_f - s_stats.raw_mean;
        s_stats.raw_mean += dr / n;
        s_raw_m2 += (double)dr * (double)(raw_f - s_stats.raw_mean);
        s_stats.raw_std = (float)sqrt(s_raw_m2 / (double)n);

        const float di = instant_g - s_stats.instant_mean_g;
        s_stats.instant_mean_g += di / n;
        s_inst_m2 += (double)di * (double)(instant_g - s_stats.instant_mean_g);
        s_stats.instant_std_g = (float)sqrt(s_inst_m2 / (double)n);
    }

    if (path == WEIGHT_PATH_FAST) {
        s_stats.fast_path_count++;
    } else if (path == WEIGHT_PATH_SLOW) {
        s_stats.slow_path_count++;
    }
}

static void weight_filter_apply(float instant_g)
{
    const weight_profile_t profile = weight_profile_from_level(s_cfg.filter_level);
    const uint8_t trim_window = profile.trim_window;

    ring_push(instant_g, trim_window);
    s_last_diag.ring_count = s_ring_count;
    s_last_diag.instant_error_g = fabsf(instant_g - s_filtered_g);

    if (profile.alpha_fast >= 1.0f) {
        s_filtered_g = instant_g;
        s_last_diag.path = WEIGHT_PATH_LEVEL0;
        s_last_diag.alpha_used = 1.0f;
        s_last_diag.trim_target_g = instant_g;
        return;
    }

    if (s_last_diag.instant_error_g >= profile.blend_threshold_g) {
        s_last_diag.path = WEIGHT_PATH_FAST;
        s_last_diag.alpha_used = adaptive_alpha(s_last_diag.instant_error_g, &profile);
        if (s_cfg.step_snap_g > 0.0f &&
            s_last_diag.instant_error_g >= s_cfg.step_snap_g * 0.5f) {
            s_last_diag.alpha_used = 1.0f;
        }
        s_last_diag.trim_target_g = instant_g;
        s_filtered_g += s_last_diag.alpha_used * (instant_g - s_filtered_g);
        return;
    }

    s_last_diag.path = WEIGHT_PATH_SLOW;
    s_last_diag.trim_target_g = trimmed_mean(s_ring, s_ring_count);
    s_last_diag.alpha_used = profile.alpha_slow;
    s_filtered_g += profile.alpha_slow * (s_last_diag.trim_target_g - s_filtered_g);
}

static void weight_stability_update(void)
{
    s_last_diag.stab_error_g = fabsf(s_filtered_g - s_prev_output_g);
    int32_t stab_bw = s_cfg.stab_bandwidth_g;
    int32_t stab_time = s_cfg.stab_time_samples;
    if (stab_time < 1) {
        stab_time = 1;
    }

    if (s_last_diag.stab_error_g <= (float)stab_bw) {
        s_stable_samples++;
    } else {
        s_stable_samples = 0;
    }

    const bool filt_stable = (s_stable_samples >= (uint32_t)stab_time);
    const bool inst_stable = (s_cfg.stab_instant_band_g > 0 &&
                              s_inst_quiet_samples >= (uint32_t)stab_time);
    s_stable_mark = filt_stable || inst_stable;
    s_last_diag.stable_count = s_stable_samples;
    s_last_diag.stable_mark = s_stable_mark;
    s_prev_output_g = s_filtered_g;
}

static void weight_auto_zero_update(int32_t raw_adc)
{
    s_last_diag.auto_zero_fired = false;

    if (!s_stable_mark) {
        s_auto_zero_in_band = false;
        return;
    }

    if (fabsf(s_filtered_g) > (float)s_cfg.auto_zero_tape_g) {
        s_auto_zero_in_band = false;
        return;
    }

    if (s_cfg.auto_zero_time <= 0) {
        return;
    }

    const int64_t now_us = esp_timer_get_time();
    if (!s_auto_zero_in_band) {
        s_auto_zero_in_band = true;
        s_auto_zero_band_enter_us = now_us;
        return;
    }

    const int64_t required_us = (int64_t)s_cfg.auto_zero_time * 100000LL;
    if ((now_us - s_auto_zero_band_enter_us) < required_us) {
        return;
    }

    s_runtime_raw_offset = raw_adc - s_cal_zero_raw;
    s_last_diag.auto_zero_fired = true;

    s_filtered_g = 0.0f;
    s_prev_output_g = 0.0f;
    s_heavy_g = 0.0f;
    s_output_g = 0.0f;
    s_prev_stable_mark = false;
    reset_ring();
    s_stable_samples = 0;
    s_stable_mark = false;
    s_auto_zero_in_band = false;
    s_first_sample = true;
}

void weight_init(const weight_config_t *cfg)
{
    if (cfg != NULL) {
        s_cfg = *cfg;
    } else {
        weight_config_t defaults = WEIGHT_CONFIG_DEFAULT();
        s_cfg = defaults;
    }
    weight_reset_filter_state();
    weight_reset_stats();
}

void weight_reset_filter_state(void)
{
    s_filtered_g = 0.0f;
    s_prev_output_g = 0.0f;
    s_heavy_g = 0.0f;
    s_output_g = 0.0f;
    s_prev_stable_mark = false;
    reset_ring();
    s_first_sample = true;
    s_stable_samples = 0;
    s_stable_mark = false;
    s_auto_zero_in_band = false;
    s_inst_quiet_samples = 0;
    memset(&s_last_diag, 0, sizeof(s_last_diag));
}

static void weight_apply_settle_output(float instant_g)
{
    s_last_diag.heavy_g = s_heavy_g;
    s_last_diag.settled_heavy = false;

    if (s_cfg.filter_enable == 0 || s_cfg.stable_heavy_enable == 0) {
        s_output_g = s_filtered_g;
        s_heavy_g = s_filtered_g;
        s_last_diag.output_g = s_output_g;
        s_prev_stable_mark = s_stable_mark;
        return;
    }

    const bool now_stable = s_stable_mark;
    const float exit_g = (s_cfg.stable_heavy_exit_g > 0.0f) ? s_cfg.stable_heavy_exit_g : 5.0f;
    const bool load_changing = (!now_stable) ||
                               (s_last_diag.path == WEIGHT_PATH_FAST) ||
                               (fabsf(instant_g - s_heavy_g) > exit_g);

    if (load_changing) {
        s_heavy_g = s_filtered_g;
        s_output_g = s_filtered_g;
        s_prev_stable_mark = now_stable;
        s_last_diag.output_g = s_output_g;
        return;
    }

    if (!s_prev_stable_mark && now_stable) {
        s_heavy_g = s_filtered_g;
    }

    float alpha = s_cfg.stable_heavy_alpha;
    if (alpha <= 0.0f) {
        alpha = 0.04f;
    }
    if (alpha > 1.0f) {
        alpha = 1.0f;
    }

    s_heavy_g += alpha * (s_filtered_g - s_heavy_g);
    s_output_g = s_heavy_g;
    s_last_diag.settled_heavy = true;
    s_last_diag.heavy_g = s_heavy_g;
    s_last_diag.output_g = s_output_g;
    s_prev_stable_mark = now_stable;
}

void weight_reset_stats(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
    s_raw_m2 = 0.0;
    s_inst_m2 = 0.0;
}

void weight_set_cal_zero_raw(int32_t zero_raw)
{
    s_cal_zero_raw = zero_raw;
}

void weight_clear_runtime_offset(void)
{
    s_runtime_raw_offset = 0;
}

static void weight_snap_step(float instant_g)
{
    const float thr = s_cfg.step_snap_g;
    if (thr <= 0.0f) {
        return;
    }
    if (fabsf(instant_g - s_output_g) < thr) {
        return;
    }

    s_filtered_g = instant_g;
    s_heavy_g = instant_g;
    s_output_g = instant_g;
    s_prev_output_g = instant_g;
    s_stable_samples = 0;
    s_stable_mark = false;
    s_prev_stable_mark = false;
    reset_ring();
    s_first_sample = false;
}

void weight_update(float instant_g, int32_t raw_adc)
{
    s_last_diag.raw_adc = raw_adc;
    s_last_diag.instant_g = instant_g;
    s_last_diag.auto_zero_fired = false;

    weight_snap_step(instant_g);

    if (s_first_sample || s_cfg.filter_enable == 0) {
        s_filtered_g = instant_g;
        s_first_sample = false;
        s_last_diag.path = (s_cfg.filter_enable == 0) ? WEIGHT_PATH_BYPASS : WEIGHT_PATH_FIRST;
        s_last_diag.alpha_used = 1.0f;
        s_last_diag.trim_target_g = instant_g;
        s_last_diag.instant_error_g = 0.0f;
        ring_push(instant_g, weight_profile_from_level(s_cfg.filter_level).trim_window);
        s_last_diag.ring_count = s_ring_count;
    } else {
        weight_filter_apply(instant_g);
    }

    s_last_diag.filtered_g = s_filtered_g;

    if (s_cfg.stab_instant_band_g > 0 && !s_first_sample) {
        if (s_last_diag.instant_error_g <= (float)s_cfg.stab_instant_band_g) {
            s_inst_quiet_samples++;
        } else {
            s_inst_quiet_samples = 0;
        }
    }

    weight_stability_update();
    weight_auto_zero_update(raw_adc);
    weight_apply_settle_output(instant_g);
    s_last_diag.filtered_g = s_filtered_g;
    s_last_diag.output_g = s_output_g;

    stats_push(raw_adc, instant_g, s_last_diag.path);
}

float weight_get_filtered_grams(void)
{
    return s_output_g;
}

bool weight_get_stable_mark(void)
{
    return s_stable_mark;
}

int32_t weight_get_runtime_raw_offset(void)
{
    return s_runtime_raw_offset;
}

void weight_set_config(const weight_config_t *cfg)
{
    if (cfg != NULL) {
        s_cfg = *cfg;
    }
}

void weight_get_config(weight_config_t *cfg)
{
    if (cfg != NULL) {
        *cfg = s_cfg;
    }
}

void weight_get_diag(weight_diag_t *diag)
{
    if (diag != NULL) {
        *diag = s_last_diag;
    }
}

void weight_get_stats(weight_stats_t *stats)
{
    if (stats != NULL) {
        *stats = s_stats;
    }
}

const char *weight_filter_path_name(weight_filter_path_t path)
{
    switch (path) {
    case WEIGHT_PATH_BYPASS:
        return "bypass";
    case WEIGHT_PATH_LEVEL0:
        return "level0";
    case WEIGHT_PATH_FAST:
        return "fast";
    case WEIGHT_PATH_SLOW:
        return "slow";
    case WEIGHT_PATH_FIRST:
    default:
        return "first";
    }
}

uint32_t weight_sample_period_ms(void)
{
    switch (s_cfg.sample_speed_level) {
    case 2:
        return 50;
    case 3:
        return 12;
    case 4:
        return 3;
    case 1:
    default:
        return 100;
    }
}
