#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WEIGHT_PATH_FIRST = 0,
    WEIGHT_PATH_BYPASS,
    WEIGHT_PATH_LEVEL0,
    WEIGHT_PATH_FAST,
    WEIGHT_PATH_SLOW,
} weight_filter_path_t;

typedef struct {
    int32_t raw_adc;
    float instant_g;
    float trim_target_g;
    float filtered_g;
    float heavy_g;
    float output_g;
    bool settled_heavy;
    float instant_error_g;
    float stab_error_g;
    float alpha_used;
    weight_filter_path_t path;
    uint32_t stable_count;
    bool stable_mark;
    bool auto_zero_fired;
    uint8_t ring_count;
} weight_diag_t;

typedef struct {
    uint32_t sample_count;
    int32_t raw_min;
    int32_t raw_max;
    int32_t raw_last;
    float raw_mean;
    float raw_std;
    float instant_min_g;
    float instant_max_g;
    float instant_mean_g;
    float instant_std_g;
    float filtered_last_g;
    uint32_t fast_path_count;
    uint32_t slow_path_count;
} weight_stats_t;

typedef struct {
    int32_t sample_speed_level;   /* 1=10 sps, 2=20 sps, 3=~80 sps, 4=~320 sps */
    int32_t filter_enable;        /* 0=passthrough, 1=adaptive filter */
    int32_t filter_level;         /* 0..5 profile row */
    int32_t auto_zero_time;       /* time in band: units of 100 ms */
    int32_t auto_zero_tape_g;     /* |PV| band for auto-zero (grams) */
    int32_t stab_time_samples;    /* consecutive stable samples for stable mark */
    int32_t stab_bandwidth_g;     /* max |Δfiltered| per sample (grams) */
    int32_t stab_instant_band_g;  /* |instant−filtered| band for alt stable counter; 0=off */
    int32_t stable_heavy_enable;  /* extra smooth layer when settled */
    float stable_heavy_alpha;     /* EMA on filtered while settled (0..1) */
    float stable_heavy_exit_g;    /* |instant - output| above this → fast track again */
    int32_t raw_avg_samples;      /* 1..8 rolling raw buffer when stable (trimmed mean) */
    float step_snap_g;            /* |instant - output| above this → snap filter to instant */
    float raw_track_band_g;       /* below this vs output, use latest raw only (not avg) */
} weight_config_t;

#define WEIGHT_CONFIG_DEFAULT() { \
    .sample_speed_level = 1, \
    .filter_enable = 0, \
    .filter_level = 5, \
    .auto_zero_time = 10, \
    .auto_zero_tape_g = 10, \
    .stab_time_samples = 12, \
    .stab_bandwidth_g = 10, \
    .stab_instant_band_g = 8, \
    .stable_heavy_enable = 1, \
    .stable_heavy_alpha = 0.02f, \
    .stable_heavy_exit_g = 18.0f, \
    .raw_avg_samples = 8, \
    .step_snap_g = 80.0f, \
    .raw_track_band_g = 25.0f, \
}

void weight_init(const weight_config_t *cfg);
void weight_reset_filter_state(void);
void weight_set_cal_zero_raw(int32_t zero_raw);
void weight_clear_runtime_offset(void);

void weight_update(float instant_g, int32_t raw_adc);

float weight_get_filtered_grams(void);
bool weight_get_stable_mark(void);
int32_t weight_get_runtime_raw_offset(void);

void weight_set_config(const weight_config_t *cfg);
void weight_get_config(weight_config_t *cfg);

uint32_t weight_sample_period_ms(void);

void weight_get_diag(weight_diag_t *diag);
void weight_get_stats(weight_stats_t *stats);
void weight_reset_stats(void);

const char *weight_filter_path_name(weight_filter_path_t path);

#ifdef __cplusplus
}
#endif
