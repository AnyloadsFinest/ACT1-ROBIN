#ifndef PARAM_STORAGE_H
#define PARAM_STORAGE_H

#include <stdint.h>

#define PARAM_STORE_MAGIC      0x414C5053UL  /* ALPS */
#define PARAM_STORE_VERSION    8U  /* v8: factory eth DHCP off, static 192.168.1.100 */

typedef struct
{
    int32_t max_weight;
    int32_t increment_idx;
    int32_t units;
    int32_t cal_method;
    int32_t cell_count;
    int32_t cell_cap;
    int32_t sens_mvv_x10000;
    int32_t zero_offset;
    int32_t span_cal;
    int32_t span_test_wt;
    int32_t est_deadload;
    int32_t filter_speed;
    int32_t stable_tenths;
    int32_t filt_bandwidth;
    int32_t filt_intensity;
    int32_t filt_spike;
    int32_t zero_track;
    int32_t max_zero_pct;
    int32_t power_on_zero;
    int32_t preset_tare;
    int32_t peak_hold;
    int32_t net_mode;
    int32_t ser_baud;
    int32_t ser_protocol;
    int32_t ser_addr;
    int32_t ser_parity;
    int32_t ser_stop;
    int32_t ser_update;
    int32_t in1_fn;
    int32_t in2_fn;
    int32_t ana_type;
    int32_t ana_track;
    int32_t ana_wmin;
    int32_t ana_wmax;
    int32_t ana_trim_min_x1000;
    int32_t ana_trim_max_x1000;
    int32_t can_protocol;
    int32_t can_baud;
    int32_t can_node;
    int32_t eth_protocol;
    int32_t eth_dhcp;
    int32_t eth_ip0;
    int32_t eth_ip1;
    int32_t eth_ip2;
    int32_t eth_ip3;
    int32_t eth_mask0;
    int32_t eth_mask1;
    int32_t eth_mask2;
    int32_t eth_mask3;
    int32_t eth_gw0;
    int32_t eth_gw1;
    int32_t eth_gw2;
    int32_t eth_gw3;
    int32_t eth_port;
    int32_t grav_cal_x100000;
    int32_t grav_use_x100000;
    int32_t range_mode;
    int32_t r2_max;
    int32_t r2_inc_idx;
    int32_t r3_max;
    int32_t r3_inc_idx;
    int32_t lin_count_idx;
    int32_t lin_w0;
    int32_t lin_w1;
    int32_t lin_w2;
    int32_t lin_w3;
    int32_t lin_w4;
    int32_t lin_c0;
    int32_t lin_c1;
    int32_t lin_c2;
    int32_t lin_c3;
    int32_t lin_c4;
} ParamStorePayload;

typedef struct
{
    uint32_t         magic;
    uint16_t         version;
    uint16_t         payload_len;
    uint32_t         crc32;
    ParamStorePayload payload;
} ParamStoreRecord;

int  ParamStorage_Init(void);
int  ParamStorage_Read(ParamStoreRecord *out);
int  ParamStorage_Write(const ParamStoreRecord *in);
uint32_t ParamStorage_CalcCrc(const ParamStoreRecord *rec);

#endif
