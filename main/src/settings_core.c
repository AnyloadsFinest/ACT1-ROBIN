#include "settings_core.h"

#include "param_storage.h"
#include "params.h"
#include "esp_log.h"
#include <stddef.h>
#include <string.h>

static const char *TAG = "settings_core";

typedef struct {
    const char *key;
    size_t      offset;
} SettingsFieldDef;

#define SFIELD(name) { #name, offsetof(ParamStorePayload, name) }

static const SettingsFieldDef s_fields[] = {
    SFIELD(max_weight),
    SFIELD(increment_idx),
    SFIELD(units),
    SFIELD(cal_method),
    SFIELD(cell_count),
    SFIELD(cell_cap),
    SFIELD(sens_mvv_x10000),
    SFIELD(zero_offset),
    SFIELD(span_cal),
    SFIELD(span_test_wt),
    SFIELD(est_deadload),
    SFIELD(filter_speed),
    SFIELD(stable_tenths),
    SFIELD(filt_bandwidth),
    SFIELD(filt_intensity),
    SFIELD(filt_spike),
    SFIELD(zero_track),
    SFIELD(max_zero_pct),
    SFIELD(power_on_zero),
    SFIELD(preset_tare),
    SFIELD(peak_hold),
    SFIELD(net_mode),
    SFIELD(ser_baud),
    SFIELD(ser_protocol),
    SFIELD(ser_addr),
    SFIELD(ser_parity),
    SFIELD(ser_stop),
    SFIELD(ser_update),
    SFIELD(in1_fn),
    SFIELD(in2_fn),
    SFIELD(ana_type),
    SFIELD(ana_track),
    SFIELD(ana_wmin),
    SFIELD(ana_wmax),
    SFIELD(ana_trim_min_x1000),
    SFIELD(ana_trim_max_x1000),
    SFIELD(can_protocol),
    SFIELD(can_baud),
    SFIELD(can_node),
    SFIELD(eth_protocol),
    SFIELD(eth_dhcp),
    SFIELD(eth_ip0),
    SFIELD(eth_ip1),
    SFIELD(eth_ip2),
    SFIELD(eth_ip3),
    SFIELD(eth_mask0),
    SFIELD(eth_mask1),
    SFIELD(eth_mask2),
    SFIELD(eth_mask3),
    SFIELD(eth_gw0),
    SFIELD(eth_gw1),
    SFIELD(eth_gw2),
    SFIELD(eth_gw3),
    SFIELD(eth_port),
    SFIELD(grav_cal_x100000),
    SFIELD(grav_use_x100000),
    SFIELD(range_mode),
    SFIELD(r2_max),
    SFIELD(r2_inc_idx),
    SFIELD(r3_max),
    SFIELD(r3_inc_idx),
    SFIELD(lin_count_idx),
    SFIELD(lin_w0),
    SFIELD(lin_w1),
    SFIELD(lin_w2),
    SFIELD(lin_w3),
    SFIELD(lin_w4),
    SFIELD(lin_c0),
    SFIELD(lin_c1),
    SFIELD(lin_c2),
    SFIELD(lin_c3),
    SFIELD(lin_c4),
};

int Settings_ApplyJsonObject(cJSON *settings)
{
    ParamStoreRecord rec;
    size_t i;

    if (settings == NULL || !cJSON_IsObject(settings)) {
        return -1;
    }

    Params_FillRecord(&rec);

    for (i = 0; i < (sizeof(s_fields) / sizeof(s_fields[0])); i++) {
        cJSON *item = cJSON_GetObjectItem(settings, s_fields[i].key);
        int32_t *slot;

        if (!cJSON_IsNumber(item)) {
            continue;
        }

        slot = (int32_t *)((uint8_t *)&rec.payload + s_fields[i].offset);
        *slot = (int32_t)item->valuedouble;
    }

    Params_ApplyRecord(&rec);
    if (rec.payload.filter_speed >= 0 && rec.payload.filter_speed <= 4) {
        Params_ApplyFilterLevel(rec.payload.filter_speed);
    }
    if (Params_Save() != 0) {
        return -1;
    }

    ESP_LOGI(TAG, "settings applied");
    return 0;
}
