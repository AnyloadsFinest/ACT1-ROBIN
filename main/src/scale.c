#include "scale.h"
#include "params.h"
#include "adc_front.h"
#include "main.h"
#include "esp_log.h"
#include <stddef.h>
#include <stdio.h>

static const char *TAG = "scale";

#define CAL_SAMPLE_COUNT         12
#define SPAN_MIN_COUNT_DELTA     64

static char s_cal_fail_msg[20];

#define SCALE_FILTER_MAX   10U

static int32_t filter_buf[SCALE_FILTER_MAX];
static uint8_t filter_count = 0U;
static int32_t filtered_adc = 0;
static int32_t s_last_raw_adc = 0;
static uint32_t s_last_adc_sample_ms = 0U;

/* Match AdcFront / ADS1235 data rate (10 SPS → ~100 ms). */
#define SCALE_ADC_MIN_INTERVAL_MS  95U
/* 24-bit signed ADC sanity (not a load-cell range limit). */
#define SCALE_ADC_ABS_MAX          8000000
static int32_t zero_track_adj = 0;
static int32_t live_zero = 0;
static int32_t tare_weight = 0;
static int32_t peak_weight = 0;
static uint8_t scale_stable = 0U;
static uint8_t scale_at_zero = 0U;
static uint8_t scale_overload = 0U;
static int32_t stab_last_weight = 0;
static uint32_t stab_unchanged_ms = 0U;
static uint32_t stab_last_tick_ms = 0U;
static uint8_t power_on_checked = 0U;
static uint8_t power_on_ticks = 0U;
static uint8_t input_peak_hold = 0U;

static int32_t Scale_ReadRawAdc(void)
{
    return AdcFront_ReadCounts();
}

static int32_t iir_y = 0;
static uint8_t iir_primed = 0U;

static int32_t Scale_ApplyFilter(int32_t sample)
{
    uint8_t taps;
    uint8_t i;
    int32_t sum = 0;
    int32_t limited;
    int32_t ma;
    int32_t a;
    int32_t spike;
    int32_t max_step;
    int32_t delta;

    limited = sample;
    spike = Params_GetFilterSpike();
    if ((spike > 0) && (iir_primed != 0U))
    {
        max_step = spike * 20;
        delta = sample - filtered_adc;
        if (delta > max_step)
        {
            limited = filtered_adc + max_step;
        }
        else if (delta < -max_step)
        {
            limited = filtered_adc - max_step;
        }
    }

    taps = (uint8_t)Params_GetDigitalFilter();
    if (taps == 0U)
    {
        taps = 1U;
    }
    if (taps > SCALE_FILTER_MAX)
    {
        taps = SCALE_FILTER_MAX;
    }

    for (i = taps - 1U; i > 0U; i--)
    {
        filter_buf[i] = filter_buf[i - 1U];
    }

    filter_buf[0] = limited;

    if (filter_count < taps)
    {
        filter_count++;
    }

    for (i = 0U; i < filter_count; i++)
    {
        sum += filter_buf[i];
    }

    ma = sum / (int32_t)filter_count;

    a = 11 - Params_GetFilterIntensity();
    if (a < 1)
    {
        a = 1;
    }
    if (a > 10)
    {
        a = 10;
    }

    if (iir_primed == 0U)
    {
        iir_y = ma;
        iir_primed = 1U;
    }
    else
    {
        iir_y = ((iir_y * (10 - a)) + (ma * a)) / 10;
    }

    filtered_adc = iir_y;
    return filtered_adc;
}

static int32_t Scale_RoundToResolution(int32_t weight)
{
    int32_t step = Params_GetResolutionFor(weight);

    if (step <= 0)
    {
        return weight;
    }

    if (weight >= 0)
    {
        return ((weight + (step / 2)) / step) * step;
    }

    return ((weight - (step / 2)) / step) * step;
}

static int32_t Scale_LiveAdcCounts(void)
{
    if (iir_primed == 0U)
    {
        return s_last_raw_adc;
    }

    /* Honest reading: half latest sample, half filtered (no display hold). */
    return (s_last_raw_adc + filtered_adc) / 2;
}

static int32_t Scale_TrackDivisions(void)
{
    int32_t mode = Params_GetZeroTrack();

    if (mode <= 0)
    {
        return 0;
    }
    if (mode == 1)
    {
        return Params_GetResolution() / 2;
    }
    if (mode == 2)
    {
        return Params_GetResolution();
    }
    return Params_GetResolution() * 2;
}

static void Scale_ApplyZeroTrack(int32_t weight)
{
    int32_t threshold = Scale_TrackDivisions();

    if (threshold <= 0)
    {
        return;
    }

    if (!scale_stable)
    {
        return;
    }

    if (weight > threshold || weight < -threshold)
    {
        return;
    }

    if (weight > 0)
    {
        zero_track_adj += 1;
    }
    else if (weight < 0)
    {
        zero_track_adj -= 1;
    }
}

static int32_t Scale_ApplyGravity(int32_t weight)
{
    float g_cal = Params_GetGravCal();
    float g_use = Params_GetGravUse();
    float scaled;

    if (g_cal < 0.1f || g_use < 0.1f)
    {
        return weight;
    }

    if (g_cal == g_use)
    {
        return weight;
    }

    scaled = ((float)weight * g_use) / g_cal;
    if (scaled >= 0.0f)
    {
        return (int32_t)(scaled + 0.5f);
    }
    return (int32_t)(scaled - 0.5f);
}

typedef struct
{
    int32_t delta;
    int32_t weight;
} ScaleLinPt;

static uint8_t Scale_TryMultiPoint(int32_t raw, int32_t zero, int32_t *out)
{
    ScaleLinPt pts[5];
    int32_t want;
    int32_t n = 0;
    int32_t i;
    int32_t j;
    int32_t w;
    int32_t c;
    int32_t d;
    int32_t delta;
    ScaleLinPt tmp;
    int64_t num;

    want = Params_GetLinPointCount();
    if (want < 2)
    {
        return 0U;
    }

    for (i = 0; i < want && i < 5; i++)
    {
        Params_GetLinPoint(i, &w, &c);
        d = c - zero;
        if (w <= 0 || d <= 0)
        {
            continue;
        }
        pts[n].delta = d;
        pts[n].weight = w;
        n++;
    }

    if (n < 2)
    {
        return 0U;
    }

    for (i = 0; i < n - 1; i++)
    {
        for (j = i + 1; j < n; j++)
        {
            if (pts[j].delta < pts[i].delta)
            {
                tmp = pts[i];
                pts[i] = pts[j];
                pts[j] = tmp;
            }
        }
    }

    delta = raw - zero;
    if (delta <= pts[0].delta)
    {
        num = ((int64_t)delta * (int64_t)pts[0].weight) / (int64_t)pts[0].delta;
        *out = (int32_t)num;
        return 1U;
    }

    for (i = 0; i < n - 1; i++)
    {
        if (delta <= pts[i + 1].delta)
        {
            int32_t dd = pts[i + 1].delta - pts[i].delta;
            int32_t dw = pts[i + 1].weight - pts[i].weight;

            if (dd == 0)
            {
                *out = pts[i].weight;
                return 1U;
            }
            num = (int64_t)pts[i].weight +
                  (((int64_t)(delta - pts[i].delta) * (int64_t)dw) / (int64_t)dd);
            *out = (int32_t)num;
            return 1U;
        }
    }

    {
        int32_t dd = pts[n - 1].delta - pts[n - 2].delta;
        int32_t dw = pts[n - 1].weight - pts[n - 2].weight;

        if (dd == 0)
        {
            *out = pts[n - 1].weight;
            return 1U;
        }
        num = (int64_t)pts[n - 1].weight +
              (((int64_t)(delta - pts[n - 1].delta) * (int64_t)dw) / (int64_t)dd);
        *out = (int32_t)num;
        return 1U;
    }
}

static int32_t Scale_ComputeGross(void)
{
    int32_t raw = Scale_LiveAdcCounts();
    int32_t zero = Params_GetZeroOffset() + zero_track_adj;
    int64_t weight = 0;
    int32_t max_w = Params_GetMaxWeight();
    int32_t sys_fs;
    int32_t counts_fs;
    int32_t span;
    int32_t span_delta;
    int32_t lin_w;

    if (Scale_TryMultiPoint(raw, zero, &lin_w))
    {
        weight = lin_w;
    }
    else if (Params_GetCalMethod() == 0)
    {
        sys_fs = Params_GetCellCount() * Params_GetCellCap();
        if (sys_fs <= 0)
        {
            sys_fs = 1;
        }
        counts_fs = (int32_t)(Params_GetSensMvv() * 1000.0f);
        if (counts_fs < 1)
        {
            counts_fs = 1;
        }
        weight = ((int64_t)(raw - zero) * (int64_t)sys_fs) / (int64_t)counts_fs;
        weight -= Params_GetEstDeadLoad();
    }
    else
    {
        span = Params_GetSpanCal();
        span_delta = span - zero;
        if (span_delta <= 0)
        {
            return 0;
        }
        {
            int32_t ref_w = Params_GetSpanTestWeight();

            if (ref_w <= 0)
            {
                ref_w = max_w;
            }
            if (ref_w <= 0)
            {
                ref_w = 1;
            }
            weight = ((int64_t)(raw - zero) * (int64_t)ref_w) / (int64_t)span_delta;
        }
    }

    weight = Scale_ApplyGravity((int32_t)weight);
    weight -= live_zero;
    return (int32_t)weight;
}

static int32_t Scale_MaxZeroAmount(void)
{
    int32_t max_w = Params_GetMaxWeight();
    int32_t pct = Params_GetMaxZeroPct();

    if (max_w <= 0)
    {
        return 0;
    }
    if (pct <= 0)
    {
        return 0;
    }
    return (max_w * pct) / 100;
}

static void Scale_UpdateStability(int32_t w)
{
    uint32_t now = HAL_GetTick();
    uint32_t dt = now - stab_last_tick_ms;
    uint32_t hold_ms;
    int32_t tenths = Params_GetStableTenths();

    if (tenths < 2)
    {
        tenths = 2;
    }
    hold_ms = (uint32_t)tenths * 100U;

    if (stab_last_tick_ms != 0U && dt < 1000U)
    {
        if (w != stab_last_weight)
        {
            stab_unchanged_ms = 0U;
            scale_stable = 0U;
        }
        else
        {
            stab_unchanged_ms += dt;
            if (stab_unchanged_ms >= hold_ms)
            {
                scale_stable = 1U;
            }
        }
    }
    else if (stab_last_tick_ms == 0U)
    {
        scale_stable = 0U;
        stab_unchanged_ms = 0U;
    }

    stab_last_weight = w;
    stab_last_tick_ms = now;
}

void Scale_ResetFilterForCal(void)
{
    filter_count = 0U;
    iir_y = 0;
    iir_primed = 0U;
}

const char *Scale_GetLastCalFailMsg(void)
{
    return s_cal_fail_msg;
}

static uint8_t Scale_ReadCalAverage(int32_t *sample_out)
{
    int32_t avg = 0;

    s_cal_fail_msg[0] = '\0';
    if (sample_out == NULL) {
        return 0U;
    }
    if (AdcFront_AverageCounts(CAL_SAMPLE_COUNT, &avg) != 0) {
        snprintf(s_cal_fail_msg, sizeof(s_cal_fail_msg), "ADC READ FAIL");
        ESP_LOGE(TAG, "cal average failed (%d samples)", CAL_SAMPLE_COUNT);
        return 0U;
    }
    filtered_adc = avg;
    Scale_ResetFilterForCal();
    filter_buf[0] = avg;
    filter_count = 1U;
    iir_y = avg;
    iir_primed = 1U;
    *sample_out = avg;
    return 1U;
}

void Scale_Init(void)
{
    Scale_ResetFilterForCal();
    filtered_adc = Scale_ReadRawAdc();
    s_last_raw_adc = filtered_adc;
    s_last_adc_sample_ms = HAL_GetTick();
    zero_track_adj = 0;
    live_zero = 0;
    tare_weight = 0;
    peak_weight = 0;
    scale_stable = 0U;
    scale_at_zero = 0U;
    scale_overload = 0U;
    stab_last_weight = 0;
    stab_unchanged_ms = 0U;
    stab_last_tick_ms = 0U;
    power_on_checked = 0U;
    power_on_ticks = 0U;
    input_peak_hold = 0U;
}

static uint8_t Scale_SamplePlausible(int32_t sample)
{
    int32_t z = Params_GetZeroOffset();
    int32_t sp = Params_GetSpanCal();
    int32_t lo;
    int32_t hi;

    if (sample > SCALE_ADC_ABS_MAX || sample < -SCALE_ADC_ABS_MAX) {
        return 0U;
    }

    /* Allow full swing between stored zero and span (+ margin). Do not cap at 600k — span reads ~1.4M. */
    if (sp > z + SPAN_MIN_COUNT_DELTA) {
        lo = z - 250000;
        hi = sp + 250000;
    } else {
        lo = z - 500000;
        hi = z + 3000000;
    }

    if (sample < lo || sample > hi) {
        ESP_LOGD(TAG, "ADC outside cal window: sample=%ld zero=%ld span=%ld",
                 (long)sample, (long)z, (long)sp);
        return 0U;
    }
    return 1U;
}

static void Scale_SnapFilterToSample(int32_t sample)
{
    filter_buf[0] = sample;
    filter_count = 1U;
    iir_y = sample;
    iir_primed = 1U;
    filtered_adc = sample;
}

void Scale_Tick(void)
{
    int32_t sample;
    uint32_t now = HAL_GetTick();

    AdcFront_Tick();

    if ((now - s_last_adc_sample_ms) < SCALE_ADC_MIN_INTERVAL_MS) {
        return;
    }
    s_last_adc_sample_ms = now;

    sample = Scale_ReadRawAdc();
    if (Scale_SamplePlausible(sample) == 0U) {
        return;
    }

    if (iir_primed != 0U) {
        int32_t step = sample - s_last_raw_adc;
        if (step < 0) {
            step = -step;
        }
        /* Large real load steps: accept immediately and snap filter (not a 80k-per-sample creep). */
        if (step > 50000) {
            Scale_SnapFilterToSample(sample);
        }
    }

    s_last_raw_adc = sample;
    Scale_ApplyFilter(sample);

    if (!power_on_checked && Params_GetPowerOnZero())
    {
        power_on_ticks++;
        if (power_on_ticks >= 10U)
        {
            int32_t gross = Scale_RoundToResolution(Scale_ComputeGross());
            int32_t lim = Scale_MaxZeroAmount();
            int32_t mag = (gross < 0) ? -gross : gross;

            if (lim > 0 && mag <= lim)
            {
                live_zero += gross;
            }
            power_on_checked = 1U;
        }
    }
}

int32_t Scale_GetRawAdc(void)
{
    return s_last_raw_adc;
}

int32_t Scale_GetFilteredAdc(void)
{
    return filtered_adc;
}

int32_t Scale_GetCellMillivolts(void)
{
    return AdcFront_CountsToMillivolts(s_last_raw_adc);
}

int32_t Scale_GetGrossWeight(void)
{
    return Scale_RoundToResolution(Scale_ComputeGross());
}

int32_t Scale_GetNetWeight(void)
{
    int32_t net;

    net = Scale_GetGrossWeight() - tare_weight - Params_GetPresetTare();
    return Scale_RoundToResolution(net);
}

int32_t Scale_GetDisplayWeight(void)
{
    int32_t gross;
    int32_t net;
    int32_t shown;
    int32_t max_w = Params_GetMaxWeight();
    int32_t mag;

    gross = Scale_GetGrossWeight();
    Scale_ApplyZeroTrack(gross);

    net = Scale_RoundToResolution(gross - tare_weight - Params_GetPresetTare());

    if (Params_GetNetMode() != 0)
    {
        shown = net;
    }
    else
    {
        shown = gross;
    }

    if (Params_GetPeakHold() != 0 || input_peak_hold != 0U)
    {
        if (shown > peak_weight)
        {
            peak_weight = shown;
        }
        shown = peak_weight;
    }
    else
    {
        peak_weight = shown;
    }

    mag = (shown < 0) ? -shown : shown;
    if (max_w > 0 && mag > max_w)
    {
        scale_overload = 1U;
    }
    else
    {
        scale_overload = 0U;
    }

    Scale_UpdateStability(shown);
    scale_at_zero = (gross == 0) ? 1U : 0U;

    return shown;
}

uint8_t Scale_IsStable(void)
{
    return scale_stable;
}

uint8_t Scale_IsAtZero(void)
{
    return scale_at_zero;
}

uint8_t Scale_IsNet(void)
{
    return (Params_GetNetMode() != 0 && (tare_weight != 0 || Params_GetPresetTare() != 0)) ? 1U : 0U;
}

uint8_t Scale_IsOverload(void)
{
    return scale_overload;
}

const char *Scale_GetUnitsLabel(void)
{
    return Params_GetUnitsLabel();
}

uint8_t Scale_CaptureZero(void)
{
    int32_t sample;

    if (Scale_ReadCalAverage(&sample) == 0U) {
        return 0U;
    }

    Params_SetCalMethodPhysical();
    Params_SetZeroOffset(sample);
    /* Force span recal if old span ADC is not above new zero. */
    if (Params_GetSpanCal() <= sample + SPAN_MIN_COUNT_DELTA) {
        Params_SetSpanCal(sample);
    }
    zero_track_adj = 0;
    live_zero = 0;
    Params_Save();
    ESP_LOGI(TAG, "zero cal: avg raw=%ld (%d samples)", (long)sample, CAL_SAMPLE_COUNT);
    return 1U;
}

static uint8_t Scale_SpanDeltaOk(int32_t span_raw)
{
    int32_t z = Params_GetZeroOffset();
    int32_t delta = span_raw - z;

    if (delta < SPAN_MIN_COUNT_DELTA && delta > -SPAN_MIN_COUNT_DELTA)
    {
        snprintf(s_cal_fail_msg, sizeof(s_cal_fail_msg), "ADD TEST LOAD");
        ESP_LOGW(TAG, "span cal rejected: span=%ld zero=%ld delta=%ld (need |d|>=%d)",
                 (long)span_raw, (long)z, (long)delta, SPAN_MIN_COUNT_DELTA);
        return 0U;
    }
    return 1U;
}

uint8_t Scale_CaptureSpan(void)
{
    int32_t sample;

    if (Scale_ReadCalAverage(&sample) == 0U) {
        return 0U;
    }

    if (Scale_SpanDeltaOk(sample) == 0U)
    {
        return 0U;
    }

    Params_SetCalMethodPhysical();
    Params_SetSpanCal(sample);
    Params_Save();
    ESP_LOGI(TAG, "span cal: avg raw=%ld test_wt=%ld zero=%ld delta=%ld",
             (long)sample, (long)Params_GetSpanTestWeight(), (long)Params_GetZeroOffset(),
             (long)(sample - Params_GetZeroOffset()));
    return 1U;
}

uint8_t Scale_CaptureAdc(int32_t *dest)
{
    int32_t sample;

    if (dest == NULL)
    {
        return 0U;
    }

    if (Scale_ReadCalAverage(&sample) == 0U) {
        return 0U;
    }

    if (dest == Params_SpanCalPtr() && Scale_SpanDeltaOk(sample) == 0U)
    {
        return 0U;
    }

    if (dest == Params_SpanCalPtr()) {
        Params_SetCalMethodPhysical();
    }
    *dest = sample;
    Params_Save();
    ESP_LOGI(TAG, "adc cal capture: avg raw=%ld", (long)sample);
    return 1U;
}

void Scale_DoZero(void)
{
    int32_t gross = Scale_RoundToResolution(Scale_ComputeGross());
    int32_t lim = Scale_MaxZeroAmount();
    int32_t mag = (gross < 0) ? -gross : gross;

    if (lim > 0 && mag <= lim)
    {
        live_zero += gross;
        zero_track_adj = 0;
    }
}

void Scale_DoTare(void)
{
    int32_t gross = Scale_RoundToResolution(Scale_ComputeGross());

    if (gross == 0)
    {
        return;
    }

    tare_weight = gross;
    Params_SetNetMode(1);
}

void Scale_ClearTare(void)
{
    tare_weight = 0;
    Params_SetNetMode(0);
}

void Scale_ResetPeak(void)
{
    peak_weight = Scale_GetGrossWeight();
}

void Scale_SetInputPeakHold(uint8_t hold)
{
    input_peak_hold = (hold != 0U) ? 1U : 0U;
}

int32_t Scale_GetTareWeight(void)
{
    return tare_weight + Params_GetPresetTare();
}
