#include "analog_out.h"
#include "analog_dac.h"
#include "params.h"
#include "scale.h"
#include <stdio.h>

/* Analog types must match SIGNAL TYPE option order in params.c. */
enum {
    ANA_TYPE_4_20MA = 0,
    ANA_TYPE_0_20MA,
    ANA_TYPE_0_10V,
    ANA_TYPE_0_5V,
    ANA_TYPE_PM_10V,
    ANA_TYPE_PM_5V
};

enum {
    ANA_TEST_FOLLOW = 0,
    ANA_TEST_MIN,
    ANA_TEST_MID,
    ANA_TEST_MAX
};

static float    s_value  = 4.0f;
static uint16_t s_counts = 0U;

static uint8_t AnalogOut_TypeIsCurrent(int32_t type)
{
    return (type == ANA_TYPE_4_20MA || type == ANA_TYPE_0_20MA) ? 1U : 0U;
}

static void AnalogOut_TypeRange(int32_t type, float *lo, float *hi)
{
    switch (type)
    {
    case ANA_TYPE_0_20MA:
        *lo = 0.0f;
        *hi = 20.0f;
        break;
    case ANA_TYPE_0_10V:
        *lo = 0.0f;
        *hi = 10.0f;
        break;
    case ANA_TYPE_0_5V:
        *lo = 0.0f;
        *hi = 5.0f;
        break;
    case ANA_TYPE_PM_10V:
        *lo = -10.0f;
        *hi = 10.0f;
        break;
    case ANA_TYPE_PM_5V:
        *lo = -5.0f;
        *hi = 5.0f;
        break;
    case ANA_TYPE_4_20MA:
    default:
        *lo = 4.0f;
        *hi = 20.0f;
        break;
    }
}

/* Encode onto the analog module's full span so trim below 4 mA can still move. */
static void AnalogOut_HardwareSpan(int32_t type, float *lo, float *hi)
{
    if (AnalogOut_TypeIsCurrent(type))
    {
        *lo = 0.0f;
        *hi = 20.0f;
        return;
    }

    AnalogOut_TypeRange(type, lo, hi);
}

static float AnalogOut_Clampf(float value, float lo, float hi)
{
    if (value < lo)
    {
        return lo;
    }
    if (value > hi)
    {
        return hi;
    }
    return value;
}

static float AnalogOut_FracFromWeight(int32_t weight)
{
    int32_t wmin = Params_GetAnaWeightMin();
    int32_t wmax = Params_GetAnaWeightMax();
    int32_t span = wmax - wmin;
    float   frac;

    if (span == 0)
    {
        return 0.0f;
    }

    frac = (float)(weight - wmin) / (float)span;
    return AnalogOut_Clampf(frac, 0.0f, 1.0f);
}

static int32_t AnalogOut_TrackedWeight(void)
{
    if (Params_GetAnaTrack() != 0)
    {
        return Scale_GetNetWeight();
    }

    return Scale_GetGrossWeight();
}

static uint16_t AnalogOut_ToCounts(float analog, int32_t type)
{
    float hw_lo;
    float hw_hi;
    float span;
    float n;

    AnalogOut_HardwareSpan(type, &hw_lo, &hw_hi);
    span = hw_hi - hw_lo;
    if (span <= 0.0f)
    {
        return 0U;
    }

    n = (analog - hw_lo) / span;
    n = AnalogOut_Clampf(n, 0.0f, 1.0f);
    return (uint16_t)(n * 65535.0f + 0.5f);
}

static void AnalogOut_WriteModule(uint16_t counts)
{
    s_counts = counts;
    AnalogDac_Write(counts);
}

void AnalogOut_Init(void)
{
    AnalogDac_Init();
    AnalogDac_SetType(ANA_TYPE_4_20MA);
    s_value = 4.0f;
    s_counts = AnalogOut_ToCounts(s_value, ANA_TYPE_4_20MA);
    AnalogOut_WriteModule(s_counts);
}

void AnalogOut_Tick(void)
{
    int32_t type = Params_GetAnaType();
    int32_t test = Params_GetAnaTest();
    float   type_lo;
    float   type_hi;
    float   out_lo;
    float   out_hi;
    float   frac;

    AnalogDac_SetType(type);
    AnalogOut_TypeRange(type, &type_lo, &type_hi);
    out_lo = type_lo + Params_GetAnaTrimMin();
    out_hi = type_hi + Params_GetAnaTrimMax();

    switch (test)
    {
    case ANA_TEST_MIN:
        frac = 0.0f;
        break;
    case ANA_TEST_MID:
        frac = 0.5f;
        break;
    case ANA_TEST_MAX:
        frac = 1.0f;
        break;
    case ANA_TEST_FOLLOW:
    default:
        frac = AnalogOut_FracFromWeight(AnalogOut_TrackedWeight());
        break;
    }

    s_value = out_lo + (frac * (out_hi - out_lo));
    AnalogOut_WriteModule(AnalogOut_ToCounts(s_value, type));
}

float AnalogOut_GetValue(void)
{
    return s_value;
}

uint16_t AnalogOut_GetCounts(void)
{
    return s_counts;
}

uint8_t AnalogOut_IsCurrent(void)
{
    return AnalogOut_TypeIsCurrent(Params_GetAnaType());
}

void AnalogOut_FormatLive(char *buf, size_t buf_size)
{
    if (buf == NULL || buf_size == 0U)
    {
        return;
    }

    if (AnalogOut_IsCurrent())
    {
        snprintf(buf, buf_size, "%.3f MA", (double)s_value);
    }
    else
    {
        snprintf(buf, buf_size, "%.3f V", (double)s_value);
    }
}
