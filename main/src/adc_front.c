#include "adc_front.h"
#include "ads1235.h"
#include "esp_log.h"

static const char *TAG = "adc_front";

static ads1235_handle_t s_adc;
static uint8_t s_adc_ok;
static int32_t s_adc_last_good;

void AdcFront_Init(void)
{
    ads1235_config_t cfg = ADS1235_CONFIG_DEFAULT();
    esp_err_t err = ads1235_init(&cfg, &s_adc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ads1235_init failed");
        s_adc_ok = 0U;
        return;
    }
    err = ads1235_default_init(&s_adc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ads1235_default_init failed");
        s_adc_ok = 0U;
        return;
    }
    s_adc_ok = 1U;
    s_adc_last_good = 0;
    ESP_LOGI(TAG, "ADS1235 ready (AIN4/5, 10 SPS FIR)");
}

void AdcFront_Tick(void)
{
}

#define ADC_FRONT_CAL_TIMEOUT_MS  1500U

int32_t AdcFront_ReadCounts(void)
{
    int32_t raw = 0;

    if (s_adc_ok == 0U) {
        return s_adc_last_good;
    }
    if (ads1235_read_raw_blocking(&s_adc, 200, &raw) != ESP_OK) {
        return s_adc_last_good;
    }
    s_adc_last_good = raw;
    return raw;
}

int AdcFront_AverageCounts(int sample_count, int32_t *avg_out)
{
    int64_t sum = 0;
    int got = 0;
    int i;

    if (avg_out == NULL || sample_count < 1 || s_adc_ok == 0U) {
        return -1;
    }

    for (i = 0; i < sample_count; i++) {
        int32_t raw = 0;
        if (ads1235_read_raw_blocking(&s_adc, ADC_FRONT_CAL_TIMEOUT_MS, &raw) != ESP_OK) {
            return -1;
        }
        sum += raw;
        got++;
    }

    *avg_out = (int32_t)(sum / got);
    s_adc_last_good = *avg_out;
    return 0;
}

int32_t AdcFront_CountsToMillivolts(int32_t counts)
{
    const float vref_v = 5.0f;
    const float gain = 64.0f;
    return (int32_t)(((float)counts / 8388608.0f) * (vref_v / gain) * 1000.0f);
}

uint8_t AdcFront_IsReady(void)
{
    return s_adc_ok;
}
