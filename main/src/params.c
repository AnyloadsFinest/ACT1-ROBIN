#include "params.h"
#include "param_storage.h"
#include "scale.h"
#include <stddef.h>

#define MENU_COUNT(arr) ((uint8_t)(sizeof(arr) / sizeof((arr)[0])))

/* ---- Scale (display) ---- */
static int32_t p_max_weight    = 30000;
static int32_t p_increment_idx = 5; /* 0.05 with 2 decimals */
static int32_t p_units         = 0;

static const char * const units_labels[] = { "KG", "LB", "OZ", "G" };
static const char * const increment_labels[] = {
    "0.001", "0.002", "0.005",
    "0.01",  "0.02",  "0.05",
    "0.1",   "0.2",   "0.5",
    "1",     "2",     "5",     "10"
};

static const uint8_t increment_decimals[] = {
    3, 3, 3,
    2, 2, 2,
    1, 1, 1,
    0, 0, 0, 0
};

static const int32_t increment_steps[] = {
    1, 2, 5,
    1, 2, 5,
    1, 2, 5,
    1, 2, 5, 10
};

static const char * const range_mode_labels[] = { "OFF", "2 RANGE", "3 RANGE" };

static int32_t p_range_mode = 0;
static int32_t p_r2_max     = 0;
static int32_t p_r2_inc     = 5;
static int32_t p_r3_max     = 0;
static int32_t p_r3_inc     = 5;

static const MenuNode scale_children[] = {
    { "MAX WEIGHT",  PARAM_TYPE_I32,          &p_max_weight,    0, 999999, 1, NULL,             0, NULL, NULL, 0, "F1.1" },
    { "INCREMENT",   PARAM_TYPE_OPTION,       &p_increment_idx, 0, 12,     1,  increment_labels, MENU_COUNT(increment_labels), NULL, NULL, 0, "F1.2" },
    { "UNITS",       PARAM_TYPE_OPTION,       &p_units,         0, 3,      1,  units_labels,     MENU_COUNT(units_labels), NULL, NULL, 0, "F1.3" },
    { "MULTI RANGE", PARAM_TYPE_RANGE_WIZARD, NULL,            0, 0,      0,  NULL,             0, NULL, NULL, 0, "F1.4" },
};

/* ---- Calibration ---- */
static int32_t p_cal_method   = 1; /* 0 digital, 1 physical */
static int32_t p_cell_count   = 1;
static int32_t p_cell_cap     = 10000;
static float   p_sens_mvv     = 2.0f;
static int32_t p_zero_offset  = 0;
static int32_t p_span_cal     = 4095;
static int32_t p_span_test_wt = 0;
static int32_t p_est_deadload = 0;
static float   p_grav_cal     = 9.80665f;
static float   p_grav_use     = 9.80665f;
static int32_t p_lin_idx      = 0; /* 0 = off */
static int32_t p_lin_w[5]     = { 0, 0, 0, 0, 0 };
static int32_t p_lin_c[5]     = { 0, 0, 0, 0, 0 };

static const char * const cal_method_labels[] = { "DIGITAL", "PHYSICAL" };
static const char * const lin_count_labels[] = { "OFF", "2", "3", "4", "5" };

static void Params_ActionFactoryReset(void)
{
    Params_DoFactoryCal();
}

static void Params_ActionClearLinPoints(void)
{
    uint8_t i;

    p_lin_idx = 0;
    for (i = 0U; i < 5U; i++)
    {
        p_lin_w[i] = 0;
        p_lin_c[i] = 0;
    }
    Params_Save();
}

static const MenuNode digital_cal_children[] = {
    { "CELL COUNT", PARAM_TYPE_I32,   &p_cell_count, 1, 8,      1,      NULL, 0, NULL, NULL, 0, "F2.4.1" },
    { "CELL CAP", PARAM_TYPE_I32,   &p_cell_cap,   1, 999999, 10,     NULL, 0, NULL, NULL, 0, "F2.4.2" },
    { "CELL SENS", PARAM_TYPE_FLOAT, &p_sens_mvv,   0.5f, 7.0f, 0.0001f, NULL, 0, NULL, NULL, 0, "F2.4.3" },
};

static const MenuNode wizard_children[] = {
    { "CELL COUNT", PARAM_TYPE_I32,        &p_cell_count,   1, 8,      1,      NULL, 0, NULL, NULL, 0, "F2.4.1" },
    { "CELL CAP", PARAM_TYPE_I32,        &p_cell_cap,     1, 999999, 10,     NULL, 0, NULL, NULL, 0, "F2.4.2" },
    { "CELL SENS", PARAM_TYPE_FLOAT,      &p_sens_mvv,     0.5f, 7.0f, 0.0001f, NULL, 0, NULL, NULL, 0, "F2.4.3" },
    { "CALIBRATE ZERO", PARAM_TYPE_ZERO_CALIB, &p_zero_offset,  0, 0,      0,      NULL, 0, NULL, NULL, 0, "F2.2" },
    { "CALIBRATE SPAN", PARAM_TYPE_SPAN_CALIB, &p_span_cal,     0, 0,      0,      NULL, 0, NULL, NULL, 0, "F2.3" },
    { "EST DEAD LOAD",   PARAM_TYPE_I32,       &p_est_deadload, 0, 999999, 1,      NULL, 0, NULL, NULL, 0, "F2.9" },
    { "GRAV AT CAL", PARAM_TYPE_FLOAT,      &p_grav_cal,     0.0f, 9.99999f, 0.00001f, NULL, 0, NULL, NULL, 0, "F2.8.1" },
    { "GRAV AT USE", PARAM_TYPE_FLOAT,      &p_grav_use,     0.0f, 9.99999f, 0.00001f, NULL, 0, NULL, NULL, 0, "F2.8.2" },
};

static const MenuNode lin_pt1_children[] = {
    { "TEST WT", PARAM_TYPE_I32,        &p_lin_w[0], 0, 999999, 1, NULL, 0, NULL, NULL, 0, "F2.7.2.1" },
    { "CALIBRATE", PARAM_TYPE_SPAN_CALIB, &p_lin_c[0], 0, 0,      0, NULL, 0, NULL, NULL, 0, "F2.7.2.2" },
};

static const MenuNode lin_pt2_children[] = {
    { "TEST WT", PARAM_TYPE_I32,        &p_lin_w[1], 0, 999999, 1, NULL, 0, NULL, NULL, 0, "F2.7.3.1" },
    { "CALIBRATE", PARAM_TYPE_SPAN_CALIB, &p_lin_c[1], 0, 0,      0, NULL, 0, NULL, NULL, 0, "F2.7.3.2" },
};

static const MenuNode lin_pt3_children[] = {
    { "TEST WT", PARAM_TYPE_I32,        &p_lin_w[2], 0, 999999, 1, NULL, 0, NULL, NULL, 0, "F2.7.4.1" },
    { "CALIBRATE", PARAM_TYPE_SPAN_CALIB, &p_lin_c[2], 0, 0,      0, NULL, 0, NULL, NULL, 0, "F2.7.4.2" },
};

static const MenuNode lin_pt4_children[] = {
    { "TEST WT", PARAM_TYPE_I32,        &p_lin_w[3], 0, 999999, 1, NULL, 0, NULL, NULL, 0, "F2.7.5.1" },
    { "CALIBRATE", PARAM_TYPE_SPAN_CALIB, &p_lin_c[3], 0, 0,      0, NULL, 0, NULL, NULL, 0, "F2.7.5.2" },
};

static const MenuNode lin_pt5_children[] = {
    { "TEST WT", PARAM_TYPE_I32,        &p_lin_w[4], 0, 999999, 1, NULL, 0, NULL, NULL, 0, "F2.7.6.1" },
    { "CALIBRATE", PARAM_TYPE_SPAN_CALIB, &p_lin_c[4], 0, 0,      0, NULL, 0, NULL, NULL, 0, "F2.7.6.2" },
};

static const MenuNode lin_children[] = {
    { "POINTS USED", PARAM_TYPE_OPTION, &p_lin_idx, 0, 4, 1, lin_count_labels, MENU_COUNT(lin_count_labels), NULL, NULL, 0, "F2.7.1" },
    { "POINT 1",       PARAM_TYPE_FOLDER, NULL,      0, 0, 0, NULL, 0, NULL, lin_pt1_children, MENU_COUNT(lin_pt1_children), "F2.7.2" },
    { "POINT 2",       PARAM_TYPE_FOLDER, NULL,      0, 0, 0, NULL, 0, NULL, lin_pt2_children, MENU_COUNT(lin_pt2_children), "F2.7.3" },
    { "POINT 3",       PARAM_TYPE_FOLDER, NULL,      0, 0, 0, NULL, 0, NULL, lin_pt3_children, MENU_COUNT(lin_pt3_children), "F2.7.4" },
    { "POINT 4",       PARAM_TYPE_FOLDER, NULL,      0, 0, 0, NULL, 0, NULL, lin_pt4_children, MENU_COUNT(lin_pt4_children), "F2.7.5" },
    { "POINT 5",       PARAM_TYPE_FOLDER, NULL,      0, 0, 0, NULL, 0, NULL, lin_pt5_children, MENU_COUNT(lin_pt5_children), "F2.7.6" },
    { "CLEAR POINTS",  PARAM_TYPE_ACTION, NULL,      0, 0, 0, NULL, 0, Params_ActionClearLinPoints, NULL, 0, "F2.7.7" },
};

static const MenuNode gravity_children[] = {
    { "GRAV AT CAL", PARAM_TYPE_FLOAT, &p_grav_cal, 0.0f, 9.99999f, 0.00001f, NULL, 0, NULL, NULL, 0, "F2.8.1" },
    { "GRAV AT USE", PARAM_TYPE_FLOAT, &p_grav_use, 0.0f, 9.99999f, 0.00001f, NULL, 0, NULL, NULL, 0, "F2.8.2" },
};

static const MenuNode cal_children[] = {
    { "CALIB METHOD",   PARAM_TYPE_OPTION,     &p_cal_method,   0, 1, 1, cal_method_labels, MENU_COUNT(cal_method_labels), NULL, NULL, 0, "F2.1" },
    { "CALIBRATE ZERO", PARAM_TYPE_ZERO_CALIB, &p_zero_offset,  0, 0, 0, NULL, 0, NULL, NULL, 0, "F2.2" },
    { "SPAN TEST WT",   PARAM_TYPE_I32,        &p_span_test_wt, 1, 999999, 1, NULL, 0, NULL, NULL, 0, "F2.2.1" },
    { "CALIBRATE SPAN", PARAM_TYPE_SPAN_CALIB, &p_span_cal,     0, 0, 0, NULL, 0, NULL, NULL, 0, "F2.3" },
    { "DIGITAL CALIB",  PARAM_TYPE_FOLDER,     NULL,            0, 0, 0, NULL, 0, NULL, digital_cal_children, MENU_COUNT(digital_cal_children), "F2.4" },
    { "SETUP WIZARD",   PARAM_TYPE_FOLDER,     NULL,            0, 0, 0, NULL, 0, NULL, wizard_children, MENU_COUNT(wizard_children), "F2.5" },
    { "MULTI POINT",    PARAM_TYPE_LIN_WIZARD, NULL,            0, 0, 0, NULL, 0, NULL, NULL, 0, "F2.6" },
    { "EDIT POINTS",    PARAM_TYPE_FOLDER,     NULL,            0, 0, 0, NULL, 0, NULL, lin_children, MENU_COUNT(lin_children), "F2.7" },
    { "GRAVITY",        PARAM_TYPE_FOLDER,     NULL,            0, 0, 0, NULL, 0, NULL, gravity_children, MENU_COUNT(gravity_children), "F2.8" },
    { "EST DEAD LOAD",  PARAM_TYPE_I32,        &p_est_deadload, 0, 999999, 1, NULL, 0, NULL, NULL, 0, "F2.9" },
};

/* ---- Weighing ---- */
static int32_t p_net_mode      = 0; /* 0 gross, 1 net */
static int32_t p_preset_tare   = 0;
static int32_t p_peak_hold     = 0;
static int32_t p_max_zero_pct  = 2;
static int32_t p_power_on_zero = 0;
static int32_t p_zero_track    = 0;

static const char * const track_labels[] = { "OFF", "0.5 D", "1 D", "2 D" };

static const MenuNode weigh_children[] = {
    { "PRESET TARE",         PARAM_TYPE_I32,    &p_preset_tare,  0, 999999, 1, NULL, 0, NULL, NULL, 0, "F3.1" },
    { "PEAK HOLD",           PARAM_TYPE_ONOFF,  &p_peak_hold,    0, 1, 1, NULL, 0, NULL, NULL, 0, "F3.2" },
    { "MAX ZERO RANGE", PARAM_TYPE_I32,    &p_max_zero_pct, 0, 20, 1, NULL, 0, NULL, NULL, 0, "F3.3" },
    { "ZERO PWR ON",    PARAM_TYPE_ONOFF,  &p_power_on_zero, 0, 1, 1, NULL, 0, NULL, NULL, 0, "F3.4" },
    { "ZERO TRACKING", PARAM_TYPE_OPTION, &p_zero_track,   0, 3, 1, track_labels, MENU_COUNT(track_labels), NULL, NULL, 0, "F3.5" },
};

/* ---- Filter ---- */
static int32_t p_filter_level   = 2; /* 0-4 = levels 1-5, 5 = custom */
static int32_t p_filt_bandwidth = 5;
static int32_t p_filt_intensity = 5;
static int32_t p_filt_spike     = 2;
static int32_t p_stable_tenths  = 10;

static const char * const filter_level_labels[] = { "1", "2", "3", "4", "5", "CUSTOM" };

static const int32_t filt_preset_bw[5]        = { 8, 7, 5, 3, 1 };
static const int32_t filt_preset_intensity[5] = { 3, 3, 5, 7, 9 };
static const int32_t filt_preset_spike[5]     = { 0, 0, 2, 5, 8 };
static const int32_t filt_preset_stable[5]    = { 4, 7, 10, 16, 25 };

static const MenuNode filter_children[] = {
    { "FILTER LEVEL", PARAM_TYPE_OPTION, &p_filter_level,   0, 5,  1, filter_level_labels, MENU_COUNT(filter_level_labels), NULL, NULL, 0, "F4.1" },
    { "BANDWIDTH",    PARAM_TYPE_I32,    &p_filt_bandwidth, 1, 10, 1, NULL, 0, NULL, NULL, 0, "F4.2" },
    { "INTENSITY",    PARAM_TYPE_I32,    &p_filt_intensity, 1, 10, 1, NULL, 0, NULL, NULL, 0, "F4.3" },
    { "SPIKE REJECT", PARAM_TYPE_I32,    &p_filt_spike,     0, 10, 1, NULL, 0, NULL, NULL, 0, "F4.4" },
    { "STABLE TIME",  PARAM_TYPE_I32,    &p_stable_tenths,  2, 50, 1, NULL, 0, NULL, NULL, 0, "F4.5" },
};

/* ---- Outputs ---- */
static int32_t p_out1_fn    = 0;
static int32_t p_out1_trip  = 0;
static int32_t p_out1_hyst  = 0;
static int32_t p_out1_track = 0;
static int32_t p_out1_delay = 0;
static int32_t p_out1_nc    = 0;
static int32_t p_out2_fn    = 0;
static int32_t p_out2_trip  = 0;
static int32_t p_out2_hyst  = 0;
static int32_t p_out2_track = 0;
static int32_t p_out2_delay = 0;
static int32_t p_out2_nc    = 0;
static int32_t p_out3_fn    = 0;
static int32_t p_out3_trip  = 0;
static int32_t p_out3_hyst  = 0;
static int32_t p_out3_track = 0;
static int32_t p_out3_delay = 0;
static int32_t p_out3_nc    = 0;

static const char * const out_fn_labels[] = { "OFF", "ABOVE", "BELOW", "STABLE", "MOTION" };
static const char * const contact_labels[] = { "NORM OPEN", "NORM CLSD" };
static const char * const out_track_labels[] = { "GROSS", "NET" };

static const MenuNode out1_children[] = {
    { "FUNCTION",    PARAM_TYPE_OPTION, &p_out1_fn,    0, 4, 1, out_fn_labels, MENU_COUNT(out_fn_labels), NULL, NULL, 0, "F5.1.1" },
    { "TRIP WEIGHT", PARAM_TYPE_I32,    &p_out1_trip,  0, 999999, 1, NULL, 0, NULL, NULL, 0, "F5.1.2" },
    { "HYSTERESIS",  PARAM_TYPE_I32,    &p_out1_hyst,  0, 999999, 1, NULL, 0, NULL, NULL, 0, "F5.1.3" },
    { "TRACK",       PARAM_TYPE_OPTION, &p_out1_track, 0, 1, 1, out_track_labels, MENU_COUNT(out_track_labels), NULL, NULL, 0, "F5.1.4" },
    { "DELAY",       PARAM_TYPE_I32,    &p_out1_delay, 0, 50, 1, NULL, 0, NULL, NULL, 0, "F5.1.5" },
    { "CONTACT",     PARAM_TYPE_OPTION, &p_out1_nc,    0, 1, 1, contact_labels, MENU_COUNT(contact_labels), NULL, NULL, 0, "F5.1.6" },
};

static const MenuNode out2_children[] = {
    { "FUNCTION",    PARAM_TYPE_OPTION, &p_out2_fn,    0, 4, 1, out_fn_labels, MENU_COUNT(out_fn_labels), NULL, NULL, 0, "F5.2.1" },
    { "TRIP WEIGHT", PARAM_TYPE_I32,    &p_out2_trip,  0, 999999, 1, NULL, 0, NULL, NULL, 0, "F5.2.2" },
    { "HYSTERESIS",  PARAM_TYPE_I32,    &p_out2_hyst,  0, 999999, 1, NULL, 0, NULL, NULL, 0, "F5.2.3" },
    { "TRACK",       PARAM_TYPE_OPTION, &p_out2_track, 0, 1, 1, out_track_labels, MENU_COUNT(out_track_labels), NULL, NULL, 0, "F5.2.4" },
    { "DELAY",       PARAM_TYPE_I32,    &p_out2_delay, 0, 50, 1, NULL, 0, NULL, NULL, 0, "F5.2.5" },
    { "CONTACT",     PARAM_TYPE_OPTION, &p_out2_nc,    0, 1, 1, contact_labels, MENU_COUNT(contact_labels), NULL, NULL, 0, "F5.2.6" },
};

static const MenuNode out3_children[] = {
    { "FUNCTION",    PARAM_TYPE_OPTION, &p_out3_fn,    0, 4, 1, out_fn_labels, MENU_COUNT(out_fn_labels), NULL, NULL, 0, "F5.3.1" },
    { "TRIP WEIGHT", PARAM_TYPE_I32,    &p_out3_trip,  0, 999999, 1, NULL, 0, NULL, NULL, 0, "F5.3.2" },
    { "HYSTERESIS",  PARAM_TYPE_I32,    &p_out3_hyst,  0, 999999, 1, NULL, 0, NULL, NULL, 0, "F5.3.3" },
    { "TRACK",       PARAM_TYPE_OPTION, &p_out3_track, 0, 1, 1, out_track_labels, MENU_COUNT(out_track_labels), NULL, NULL, 0, "F5.3.4" },
    { "DELAY",       PARAM_TYPE_I32,    &p_out3_delay, 0, 50, 1, NULL, 0, NULL, NULL, 0, "F5.3.5" },
    { "CONTACT",     PARAM_TYPE_OPTION, &p_out3_nc,    0, 1, 1, contact_labels, MENU_COUNT(contact_labels), NULL, NULL, 0, "F5.3.6" },
};

static const MenuNode outputs_children[] = {
    { "OUT 1", PARAM_TYPE_FOLDER, NULL, 0, 0, 0, NULL, 0, NULL, out1_children, MENU_COUNT(out1_children), "F5.1" },
    { "OUT 2", PARAM_TYPE_FOLDER, NULL, 0, 0, 0, NULL, 0, NULL, out2_children, MENU_COUNT(out2_children), "F5.2" },
    { "OUT 3", PARAM_TYPE_FOLDER, NULL, 0, 0, 0, NULL, 0, NULL, out3_children, MENU_COUNT(out3_children), "F5.3" },
};

/* ---- Inputs ---- */
static int32_t p_in1_fn = 1; /* ZERO */
static int32_t p_in2_fn = 3; /* NET GROSS */

static const char * const in_fn_labels[] = {
    "OFF", "ZERO", "TARE", "NET GROSS", "HOLD PEAK", "RST PEAK", "STATUS"
};

static const MenuNode inputs_children[] = {
    { "IN 1", PARAM_TYPE_OPTION, &p_in1_fn, 0, 6, 1, in_fn_labels, MENU_COUNT(in_fn_labels), NULL, NULL, 0, "F6.1" },
    { "IN 2", PARAM_TYPE_OPTION, &p_in2_fn, 0, 6, 1, in_fn_labels, MENU_COUNT(in_fn_labels), NULL, NULL, 0, "F6.2" },
};

/* ---- Communication: analog, RS485, CAN, Ethernet ---- */
static int32_t p_ser_baud     = 0;
static int32_t p_ser_protocol = 0;
static int32_t p_ser_addr     = 1;
static int32_t p_ser_parity   = 0;
static int32_t p_ser_stop     = 1;
static int32_t p_ser_update   = 10;

static int32_t p_can_protocol = 1; /* CANOPEN — TWAI always on; stack stub until implemented */
static int32_t p_can_baud     = 1; /* 250K */
static int32_t p_can_node     = 1;

static int32_t p_eth_protocol = 1; /* MODBUS TCP — W5500 always on; server stub until implemented */
static int32_t p_eth_dhcp     = 0;  /* static 192.168.1.100 for direct PC / bench (no DHCP server) */
static int32_t p_eth_ip0      = 192;
static int32_t p_eth_ip1      = 168;
static int32_t p_eth_ip2      = 1;
static int32_t p_eth_ip3      = 100;
static int32_t p_eth_mask0    = 255;
static int32_t p_eth_mask1    = 255;
static int32_t p_eth_mask2    = 255;
static int32_t p_eth_mask3    = 255;
static int32_t p_eth_gw0      = 192;
static int32_t p_eth_gw1      = 168;
static int32_t p_eth_gw2      = 1;
static int32_t p_eth_gw3      = 1;
static int32_t p_eth_port     = 502;

static int32_t p_ana_type     = 0; /* 4-20 mA */
static int32_t p_ana_track    = 0; /* gross */
static int32_t p_ana_wmin     = 0;
static int32_t p_ana_wmax     = 30000;
static float   p_ana_trim_min = 0.0f;
static float   p_ana_trim_max = 0.0f;
static int32_t p_ana_test     = 0; /* FOLLOW, not saved */
static int32_t p_ana_live     = 0;

static const char * const baud_labels[] = { "9600", "19200", "38400", "57600", "115200" };
static const char * const proto_labels[] = { "OFF", "MODBUS RTU", "CONTIN ASCII", "ASCII CMD" };
static const char * const proto_list_labels[] = { "OFF", "RTU", "CONT", "CMD" };
static const char * const parity_labels[] = { "NONE", "EVEN", "ODD" };
static const char * const can_proto_labels[] = { "OFF", "CANOPEN", "DEVICENET" };
static const char * const can_list_labels[] = { "OFF", "CANOPEN", "DNET" };
static const char * const can_baud_labels[] = { "125K", "250K", "500K", "1M" };
static const char * const eth_proto_labels[] = { "OFF", "MODBUS TCP", "ETHERNET IP" };
static const char * const eth_list_labels[] = { "OFF", "MB TCP", "EIP" };
static const char * const ana_type_labels[] = {
    "4-20 MA", "0-20 MA", "0-10 V", "0-5 V", "+/-10 V", "+/-5 V"
};
static const char * const ana_list_labels[] = {
    "4-20MA", "0-20MA", "0-10V", "0-5V", "+/-10V", "+/-5V"
};
static const char * const ana_track_labels[] = { "GROSS", "NET" };
static const char * const ana_test_labels[] = { "FOLLOW", "MIN", "MID", "MAX" };

static const MenuNode rs485_children[] = {
    { "PROTOCOL",     PARAM_TYPE_OPTION, &p_ser_protocol, 0, 3,  1, proto_labels, MENU_COUNT(proto_labels), NULL, NULL, 0, "F7.2.1" },
    { "BAUD RATE",    PARAM_TYPE_OPTION, &p_ser_baud,     0, 4,  1, baud_labels, MENU_COUNT(baud_labels), NULL, NULL, 0, "F7.2.2" },
    { "ADDRESS",      PARAM_TYPE_I32,    &p_ser_addr,     1, 99, 1, NULL, 0, NULL, NULL, 0, "F7.2.3" },
    { "PARITY",       PARAM_TYPE_OPTION, &p_ser_parity,   0, 2,  1, parity_labels, MENU_COUNT(parity_labels), NULL, NULL, 0, "F7.2.4" },
    { "STOP BITS",    PARAM_TYPE_I32,    &p_ser_stop,     1, 2,  1, NULL, 0, NULL, NULL, 0, "F7.2.5" },
    { "UPDATE RATE",  PARAM_TYPE_I32,    &p_ser_update,   10, 50, 1, NULL, 0, NULL, NULL, 0, "F7.2.6" },
};

static const MenuNode can_children[] = {
    { "PROTOCOL", PARAM_TYPE_OPTION, &p_can_protocol, 0, 2,   1, can_proto_labels, MENU_COUNT(can_proto_labels), NULL, NULL, 0, "F7.3.1" },
    { "BIT RATE", PARAM_TYPE_OPTION, &p_can_baud,     0, 3,   1, can_baud_labels, MENU_COUNT(can_baud_labels), NULL, NULL, 0, "F7.3.2" },
    { "NODE ID",  PARAM_TYPE_I32,    &p_can_node,     1, 127, 1, NULL, 0, NULL, NULL, 0, "F7.3.3" },
};

static const MenuNode eth_ip_children[] = {
    { "OCTET 1", PARAM_TYPE_I32, &p_eth_ip0, 0, 255, 1, NULL, 0, NULL, NULL, 0, "F7.4.3.1" },
    { "OCTET 2", PARAM_TYPE_I32, &p_eth_ip1, 0, 255, 1, NULL, 0, NULL, NULL, 0, "F7.4.3.2" },
    { "OCTET 3", PARAM_TYPE_I32, &p_eth_ip2, 0, 255, 1, NULL, 0, NULL, NULL, 0, "F7.4.3.3" },
    { "OCTET 4", PARAM_TYPE_I32, &p_eth_ip3, 0, 255, 1, NULL, 0, NULL, NULL, 0, "F7.4.3.4" },
};

static const MenuNode eth_mask_children[] = {
    { "OCTET 1", PARAM_TYPE_I32, &p_eth_mask0, 0, 255, 1, NULL, 0, NULL, NULL, 0, "F7.4.4.1" },
    { "OCTET 2", PARAM_TYPE_I32, &p_eth_mask1, 0, 255, 1, NULL, 0, NULL, NULL, 0, "F7.4.4.2" },
    { "OCTET 3", PARAM_TYPE_I32, &p_eth_mask2, 0, 255, 1, NULL, 0, NULL, NULL, 0, "F7.4.4.3" },
    { "OCTET 4", PARAM_TYPE_I32, &p_eth_mask3, 0, 255, 1, NULL, 0, NULL, NULL, 0, "F7.4.4.4" },
};

static const MenuNode eth_gw_children[] = {
    { "OCTET 1", PARAM_TYPE_I32, &p_eth_gw0, 0, 255, 1, NULL, 0, NULL, NULL, 0, "F7.4.5.1" },
    { "OCTET 2", PARAM_TYPE_I32, &p_eth_gw1, 0, 255, 1, NULL, 0, NULL, NULL, 0, "F7.4.5.2" },
    { "OCTET 3", PARAM_TYPE_I32, &p_eth_gw2, 0, 255, 1, NULL, 0, NULL, NULL, 0, "F7.4.5.3" },
    { "OCTET 4", PARAM_TYPE_I32, &p_eth_gw3, 0, 255, 1, NULL, 0, NULL, NULL, 0, "F7.4.5.4" },
};

static const MenuNode analog_children[] = {
    { "SIGNAL TYPE",   PARAM_TYPE_OPTION, &p_ana_type,     0, 5, 1, ana_type_labels, MENU_COUNT(ana_type_labels), NULL, NULL, 0, "F7.1.1" },
    { "TRACK",         PARAM_TYPE_OPTION, &p_ana_track,    0, 1, 1, ana_track_labels, MENU_COUNT(ana_track_labels), NULL, NULL, 0, "F7.1.2" },
    { "WT AT MIN", PARAM_TYPE_I32,    &p_ana_wmin,     0, 999999, 1, NULL, 0, NULL, NULL, 0, "F7.1.3" },
    { "WT AT MAX", PARAM_TYPE_I32,    &p_ana_wmax,     0, 999999, 1, NULL, 0, NULL, NULL, 0, "F7.1.4" },
    { "TRIM MIN",      PARAM_TYPE_FLOAT,  &p_ana_trim_min, -5.0f, 5.0f, 0.001f, NULL, 0, NULL, NULL, 0, "F7.1.5" },
    { "TRIM MAX",      PARAM_TYPE_FLOAT,  &p_ana_trim_max, -5.0f, 5.0f, 0.001f, NULL, 0, NULL, NULL, 0, "F7.1.6" },
    { "TEST ANALOG",   PARAM_TYPE_OPTION, &p_ana_test,     0, 3, 1, ana_test_labels, MENU_COUNT(ana_test_labels), NULL, NULL, 0, "F7.1.7" },
    { "LIVE OUTPUT",   PARAM_TYPE_LIVE,   &p_ana_live,     0, 1, 1, NULL, 0, NULL, NULL, 0, "F7.1.8" },
};

static const MenuNode ethernet_children[] = {
    { "PROTOCOL",    PARAM_TYPE_OPTION, &p_eth_protocol, 0, 2,     1, eth_proto_labels, MENU_COUNT(eth_proto_labels), NULL, NULL, 0, "F7.4.1" },
    { "DHCP",        PARAM_TYPE_ONOFF,  &p_eth_dhcp,     0, 1,     1, NULL, 0, NULL, NULL, 0, "F7.4.2" },
    { "IP ADDRESS",  PARAM_TYPE_FOLDER, NULL,            0, 0,     0, NULL, 0, NULL, eth_ip_children, MENU_COUNT(eth_ip_children), "F7.4.3" },
    { "SUBNET MASK", PARAM_TYPE_FOLDER, NULL,            0, 0,     0, NULL, 0, NULL, eth_mask_children, MENU_COUNT(eth_mask_children), "F7.4.4" },
    { "GATEWAY",     PARAM_TYPE_FOLDER, NULL,            0, 0,     0, NULL, 0, NULL, eth_gw_children, MENU_COUNT(eth_gw_children), "F7.4.5" },
    { "PORT",        PARAM_TYPE_I32,    &p_eth_port,     1, 65535, 1, NULL, 0, NULL, NULL, 0, "F7.4.6" },
};

static const MenuNode comm_children[] = {
    { "ANALOG",   PARAM_TYPE_FOLDER, NULL, 0, 0, 0, NULL, 0, NULL, analog_children,   MENU_COUNT(analog_children), "F7.1" },
    { "RS485",    PARAM_TYPE_FOLDER, NULL, 0, 0, 0, NULL, 0, NULL, rs485_children,    MENU_COUNT(rs485_children), "F7.2" },
    { "CAN BUS",  PARAM_TYPE_FOLDER, NULL, 0, 0, 0, NULL, 0, NULL, can_children,      MENU_COUNT(can_children), "F7.3" },
    { "ETHERNET", PARAM_TYPE_FOLDER, NULL, 0, 0, 0, NULL, 0, NULL, ethernet_children, MENU_COUNT(ethernet_children), "F7.4" },
};

/* ---- System ---- */
static int32_t p_live_weight = 0;
static int32_t p_raw_adc     = 0;
static int32_t p_cell_mv     = 0;
static int32_t p_input_stat  = 0;
static int32_t p_test_out1   = 0;
static int32_t p_test_out2   = 0;
static int32_t p_test_out3   = 0;
static int32_t p_firmware    = 0;

static const MenuNode system_children[] = {
    { "FACTORY RESET",   PARAM_TYPE_ACTION, NULL,           0, 0, 0, NULL, 0, Params_ActionFactoryReset, NULL, 0, "F8.1" },
    { "FIRMWARE",        PARAM_TYPE_LIVE,   &p_firmware,    0, 1, 1, NULL, 0, NULL, NULL, 0, "F8.2" },
    { "LIVE WEIGHT",     PARAM_TYPE_LIVE,   &p_live_weight, 0, 1, 1, NULL, 0, NULL, NULL, 0, "F8.3" },
    { "RAW COUNTS",      PARAM_TYPE_LIVE,   &p_raw_adc,     0, 1, 1, NULL, 0, NULL, NULL, 0, "F8.4" },
    { "CELL MV",        PARAM_TYPE_LIVE,   &p_cell_mv,     0, 1, 1, NULL, 0, NULL, NULL, 0, "F8.5" },
    { "TEST OUT 1",     PARAM_TYPE_ONOFF,  &p_test_out1,   0, 1, 1, NULL, 0, NULL, NULL, 0, "F8.6" },
    { "TEST OUT 2",     PARAM_TYPE_ONOFF,  &p_test_out2,   0, 1, 1, NULL, 0, NULL, NULL, 0, "F8.7" },
    { "TEST OUT 3",     PARAM_TYPE_ONOFF,  &p_test_out3,   0, 1, 1, NULL, 0, NULL, NULL, 0, "F8.8" },
    { "TEST INPUTS",     PARAM_TYPE_LIVE,   &p_input_stat,  0, 1, 1, NULL, 0, NULL, NULL, 0, "F8.9" },
};

const MenuNode menu_root[] = {
    { "SCALE",         PARAM_TYPE_FOLDER, NULL, 0, 0, 0, NULL, 0, NULL, scale_children,   MENU_COUNT(scale_children), "F1" },
    { "CALIBRATION",   PARAM_TYPE_FOLDER, NULL, 0, 0, 0, NULL, 0, NULL, cal_children,     MENU_COUNT(cal_children), "F2" },
    { "WEIGHING",      PARAM_TYPE_FOLDER, NULL, 0, 0, 0, NULL, 0, NULL, weigh_children,   MENU_COUNT(weigh_children), "F3" },
    { "FILTER",        PARAM_TYPE_FOLDER, NULL, 0, 0, 0, NULL, 0, NULL, filter_children,  MENU_COUNT(filter_children), "F4" },
    { "OUTPUTS",       PARAM_TYPE_FOLDER, NULL, 0, 0, 0, NULL, 0, NULL, outputs_children, MENU_COUNT(outputs_children), "F5" },
    { "INPUTS",        PARAM_TYPE_FOLDER, NULL, 0, 0, 0, NULL, 0, NULL, inputs_children,  MENU_COUNT(inputs_children), "F6" },
    { "COMMUNICATION", PARAM_TYPE_FOLDER, NULL, 0, 0, 0, NULL, 0, NULL, comm_children,    MENU_COUNT(comm_children), "F7" },
    { "SYSTEM",        PARAM_TYPE_FOLDER, NULL, 0, 0, 0, NULL, 0, NULL, system_children,  MENU_COUNT(system_children), "F8" },
};

const uint8_t MENU_ROOT_COUNT = MENU_COUNT(menu_root);

static int32_t Params_ClampI32(int32_t value, int32_t min, int32_t max)
{
    if (value < min)
    {
        return min;
    }
    if (value > max)
    {
        return max;
    }
    return value;
}

void Params_LoadDefaults(void)
{
    p_max_weight     = 30000;
    p_increment_idx  = 5;
    p_units          = 0;
    p_cal_method     = 1;
    p_cell_count     = 1;
    p_cell_cap       = 10000;
    p_sens_mvv       = 2.0f;
    p_zero_offset    = 0;
    p_span_cal       = 4095;
    p_span_test_wt   = 0;
    p_est_deadload   = 0;
    p_range_mode     = 0;
    p_r2_max         = 0;
    p_r2_inc         = 5;
    p_r3_max         = 0;
    p_r3_inc         = 5;
    p_grav_cal       = 9.80665f;
    p_grav_use       = 9.80665f;
    p_lin_idx        = 0;
    {
        uint8_t i;
        for (i = 0U; i < 5U; i++)
        {
            p_lin_w[i] = 0;
            p_lin_c[i] = 0;
        }
    }
    p_net_mode       = 0;
    p_preset_tare    = 0;
    p_peak_hold      = 0;
    p_max_zero_pct   = 2;
    p_power_on_zero  = 0;
    p_zero_track     = 0;
    p_filter_level   = 0;
    Params_ApplyFilterLevel(0);
    p_out1_fn = p_out2_fn = p_out3_fn = 0;
    p_out1_trip = p_out2_trip = p_out3_trip = 0;
    p_out1_hyst = p_out2_hyst = p_out3_hyst = 0;
    p_out1_track = p_out2_track = p_out3_track = 0;
    p_out1_delay = p_out2_delay = p_out3_delay = 0;
    p_out1_nc = p_out2_nc = p_out3_nc = 0;
    p_in1_fn         = 1;
    p_in2_fn         = 3;
    p_ser_baud       = 0;
    p_ser_protocol   = 0;
    p_ser_addr       = 1;
    p_ser_parity     = 0;
    p_ser_stop       = 1;
    p_ser_update     = 10;
    p_can_protocol   = 1;
    p_can_baud       = 1;
    p_can_node       = 1;
    p_eth_protocol   = 1;
    p_eth_dhcp       = 0;
    p_eth_ip0        = 192;
    p_eth_ip1        = 168;
    p_eth_ip2        = 1;
    p_eth_ip3        = 100;
    p_eth_mask0      = 255;
    p_eth_mask1      = 255;
    p_eth_mask2      = 255;
    p_eth_mask3      = 255;
    p_eth_gw0        = 192;
    p_eth_gw1        = 168;
    p_eth_gw2        = 1;
    p_eth_gw3        = 1;
    p_eth_port       = 502;
    p_ana_type       = 0;
    p_ana_track      = 0;
    p_ana_wmin       = 0;
    p_ana_wmax       = 30000;
    p_ana_trim_min   = 0.0f;
    p_ana_trim_max   = 0.0f;
    p_ana_test       = 0;
    p_test_out1 = p_test_out2 = p_test_out3 = 0;
}

void Params_FillRecord(ParamStoreRecord *rec)
{
    if (rec == NULL)
    {
        return;
    }

    rec->payload.max_weight     = p_max_weight;
    rec->payload.increment_idx  = p_increment_idx;
    rec->payload.units          = p_units;
    rec->payload.cal_method     = p_cal_method;
    rec->payload.cell_count     = p_cell_count;
    rec->payload.cell_cap       = p_cell_cap;
    rec->payload.sens_mvv_x10000 = (int32_t)(p_sens_mvv * 10000.0f);
    rec->payload.zero_offset    = p_zero_offset;
    rec->payload.span_cal       = p_span_cal;
    rec->payload.span_test_wt   = p_span_test_wt;
    rec->payload.est_deadload   = p_est_deadload;
    rec->payload.filter_speed   = p_filter_level;
    rec->payload.stable_tenths  = p_stable_tenths;
    rec->payload.filt_bandwidth = p_filt_bandwidth;
    rec->payload.filt_intensity = p_filt_intensity;
    rec->payload.filt_spike     = p_filt_spike;
    rec->payload.zero_track     = p_zero_track;
    rec->payload.max_zero_pct   = p_max_zero_pct;
    rec->payload.power_on_zero  = p_power_on_zero;
    rec->payload.preset_tare    = p_preset_tare;
    rec->payload.peak_hold      = p_peak_hold;
    rec->payload.net_mode       = p_net_mode;
    rec->payload.ser_baud       = p_ser_baud;
    rec->payload.ser_protocol   = p_ser_protocol;
    rec->payload.ser_addr       = p_ser_addr;
    rec->payload.ser_parity     = p_ser_parity;
    rec->payload.ser_stop       = p_ser_stop;
    rec->payload.ser_update     = p_ser_update;
    rec->payload.in1_fn         = p_in1_fn;
    rec->payload.in2_fn         = p_in2_fn;
    rec->payload.ana_type       = p_ana_type;
    rec->payload.ana_track      = p_ana_track;
    rec->payload.ana_wmin       = p_ana_wmin;
    rec->payload.ana_wmax       = p_ana_wmax;
    rec->payload.ana_trim_min_x1000 = (int32_t)(p_ana_trim_min * 1000.0f);
    rec->payload.ana_trim_max_x1000 = (int32_t)(p_ana_trim_max * 1000.0f);
    rec->payload.can_protocol   = p_can_protocol;
    rec->payload.can_baud       = p_can_baud;
    rec->payload.can_node       = p_can_node;
    rec->payload.eth_protocol   = p_eth_protocol;
    rec->payload.eth_dhcp       = p_eth_dhcp;
    rec->payload.eth_ip0        = p_eth_ip0;
    rec->payload.eth_ip1        = p_eth_ip1;
    rec->payload.eth_ip2        = p_eth_ip2;
    rec->payload.eth_ip3        = p_eth_ip3;
    rec->payload.eth_mask0      = p_eth_mask0;
    rec->payload.eth_mask1      = p_eth_mask1;
    rec->payload.eth_mask2      = p_eth_mask2;
    rec->payload.eth_mask3      = p_eth_mask3;
    rec->payload.eth_gw0        = p_eth_gw0;
    rec->payload.eth_gw1        = p_eth_gw1;
    rec->payload.eth_gw2        = p_eth_gw2;
    rec->payload.eth_gw3        = p_eth_gw3;
    rec->payload.eth_port       = p_eth_port;
    rec->payload.grav_cal_x100000 = (int32_t)(p_grav_cal * 100000.0f);
    rec->payload.grav_use_x100000 = (int32_t)(p_grav_use * 100000.0f);
    rec->payload.range_mode     = p_range_mode;
    rec->payload.r2_max         = p_r2_max;
    rec->payload.r2_inc_idx     = p_r2_inc;
    rec->payload.r3_max         = p_r3_max;
    rec->payload.r3_inc_idx     = p_r3_inc;
    rec->payload.lin_count_idx  = p_lin_idx;
    rec->payload.lin_w0         = p_lin_w[0];
    rec->payload.lin_w1         = p_lin_w[1];
    rec->payload.lin_w2         = p_lin_w[2];
    rec->payload.lin_w3         = p_lin_w[3];
    rec->payload.lin_w4         = p_lin_w[4];
    rec->payload.lin_c0         = p_lin_c[0];
    rec->payload.lin_c1         = p_lin_c[1];
    rec->payload.lin_c2         = p_lin_c[2];
    rec->payload.lin_c3         = p_lin_c[3];
    rec->payload.lin_c4         = p_lin_c[4];
}

void Params_ApplyRecord(const ParamStoreRecord *rec)
{
    const ParamStorePayload *p;

    if (rec == NULL)
    {
        return;
    }

    p = &rec->payload;
    p_max_weight     = Params_ClampI32(p->max_weight, 0, 999999);
    p_increment_idx  = Params_ClampI32(p->increment_idx, 0, 12);
    p_units          = Params_ClampI32(p->units, 0, 3);
    p_cal_method     = Params_ClampI32(p->cal_method, 0, 1);
    p_cell_count     = Params_ClampI32(p->cell_count, 1, 8);
    p_cell_cap       = Params_ClampI32(p->cell_cap, 1, 999999);
    p_sens_mvv       = (float)p->sens_mvv_x10000 / 10000.0f;
    if (p_sens_mvv < 0.5f)
    {
        p_sens_mvv = 0.5f;
    }
    if (p_sens_mvv > 7.0f)
    {
        p_sens_mvv = 7.0f;
    }
    p_zero_offset    = p->zero_offset;
    p_span_cal       = p->span_cal;
    p_span_test_wt   = Params_ClampI32(p->span_test_wt, 0, 999999);
    p_est_deadload   = Params_ClampI32(p->est_deadload, 0, 999999);
    p_filter_level   = Params_ClampI32(p->filter_speed, 0, 5);
    p_stable_tenths  = Params_ClampI32(p->stable_tenths, 2, 50);
    p_filt_bandwidth = Params_ClampI32(p->filt_bandwidth, 1, 10);
    p_filt_intensity = Params_ClampI32(p->filt_intensity, 1, 10);
    p_filt_spike     = Params_ClampI32(p->filt_spike, 0, 10);
    p_zero_track     = Params_ClampI32(p->zero_track, 0, 3);
    p_max_zero_pct   = Params_ClampI32(p->max_zero_pct, 0, 20);
    p_power_on_zero  = Params_ClampI32(p->power_on_zero, 0, 1);
    p_preset_tare    = Params_ClampI32(p->preset_tare, 0, 999999);
    p_peak_hold      = Params_ClampI32(p->peak_hold, 0, 1);
    p_net_mode       = Params_ClampI32(p->net_mode, 0, 1);
    p_ser_baud       = Params_ClampI32(p->ser_baud, 0, 4);
    p_ser_protocol   = Params_ClampI32(p->ser_protocol, 0, 3);
    p_ser_addr       = Params_ClampI32(p->ser_addr, 1, 99);
    p_ser_parity     = Params_ClampI32(p->ser_parity, 0, 2);
    p_ser_stop       = Params_ClampI32(p->ser_stop, 1, 2);
    p_ser_update     = Params_ClampI32(p->ser_update, 10, 50);
    p_in1_fn         = Params_ClampI32(p->in1_fn, 0, 6);
    p_in2_fn         = Params_ClampI32(p->in2_fn, 0, 6);
    p_ana_type       = Params_ClampI32(p->ana_type, 0, 5);
    p_ana_track      = Params_ClampI32(p->ana_track, 0, 1);
    p_ana_wmin       = Params_ClampI32(p->ana_wmin, 0, 999999);
    p_ana_wmax       = Params_ClampI32(p->ana_wmax, 0, 999999);
    p_ana_trim_min   = (float)p->ana_trim_min_x1000 / 1000.0f;
    p_ana_trim_max   = (float)p->ana_trim_max_x1000 / 1000.0f;
    if (p_ana_trim_min < -5.0f)
    {
        p_ana_trim_min = -5.0f;
    }
    if (p_ana_trim_min > 5.0f)
    {
        p_ana_trim_min = 5.0f;
    }
    if (p_ana_trim_max < -5.0f)
    {
        p_ana_trim_max = -5.0f;
    }
    if (p_ana_trim_max > 5.0f)
    {
        p_ana_trim_max = 5.0f;
    }
    p_can_protocol   = Params_ClampI32(p->can_protocol, 0, 2);
    p_can_baud       = Params_ClampI32(p->can_baud, 0, 3);
    p_can_node       = Params_ClampI32(p->can_node, 1, 127);
    p_eth_protocol   = Params_ClampI32(p->eth_protocol, 0, 2);
    p_eth_dhcp       = Params_ClampI32(p->eth_dhcp, 0, 1);
    p_eth_ip0        = Params_ClampI32(p->eth_ip0, 0, 255);
    p_eth_ip1        = Params_ClampI32(p->eth_ip1, 0, 255);
    p_eth_ip2        = Params_ClampI32(p->eth_ip2, 0, 255);
    p_eth_ip3        = Params_ClampI32(p->eth_ip3, 0, 255);
    p_eth_mask0      = Params_ClampI32(p->eth_mask0, 0, 255);
    p_eth_mask1      = Params_ClampI32(p->eth_mask1, 0, 255);
    p_eth_mask2      = Params_ClampI32(p->eth_mask2, 0, 255);
    p_eth_mask3      = Params_ClampI32(p->eth_mask3, 0, 255);
    p_eth_gw0        = Params_ClampI32(p->eth_gw0, 0, 255);
    p_eth_gw1        = Params_ClampI32(p->eth_gw1, 0, 255);
    p_eth_gw2        = Params_ClampI32(p->eth_gw2, 0, 255);
    p_eth_gw3        = Params_ClampI32(p->eth_gw3, 0, 255);
    p_eth_port       = Params_ClampI32(p->eth_port, 1, 65535);
    p_grav_cal       = (float)p->grav_cal_x100000 / 100000.0f;
    p_grav_use       = (float)p->grav_use_x100000 / 100000.0f;
    if (p_grav_cal < 0.0f)
    {
        p_grav_cal = 0.0f;
    }
    if (p_grav_cal > 9.99999f)
    {
        p_grav_cal = 9.99999f;
    }
    if (p_grav_use < 0.0f)
    {
        p_grav_use = 0.0f;
    }
    if (p_grav_use > 9.99999f)
    {
        p_grav_use = 9.99999f;
    }
    p_range_mode     = Params_ClampI32(p->range_mode, 0, 2);
    p_r2_max         = Params_ClampI32(p->r2_max, 0, 999999);
    p_r2_inc         = Params_ClampI32(p->r2_inc_idx, 0, 12);
    p_r3_max         = Params_ClampI32(p->r3_max, 0, 999999);
    p_r3_inc         = Params_ClampI32(p->r3_inc_idx, 0, 12);
    p_lin_idx        = Params_ClampI32(p->lin_count_idx, 0, 4);
    p_lin_w[0]       = Params_ClampI32(p->lin_w0, 0, 999999);
    p_lin_w[1]       = Params_ClampI32(p->lin_w1, 0, 999999);
    p_lin_w[2]       = Params_ClampI32(p->lin_w2, 0, 999999);
    p_lin_w[3]       = Params_ClampI32(p->lin_w3, 0, 999999);
    p_lin_w[4]       = Params_ClampI32(p->lin_w4, 0, 999999);
    p_lin_c[0]       = p->lin_c0;
    p_lin_c[1]       = p->lin_c1;
    p_lin_c[2]       = p->lin_c2;
    p_lin_c[3]       = p->lin_c3;
    p_lin_c[4]       = p->lin_c4;
}

void Params_Init(void)
{
    ParamStoreRecord rec;

    ParamStorage_Init();
    if (ParamStorage_Read(&rec) == 0)
    {
        Params_ApplyRecord(&rec);
    }
    else
    {
        Params_LoadDefaults();
    }
}

int Params_Save(void)
{
    ParamStoreRecord rec;

    Params_FillRecord(&rec);
    return ParamStorage_Write(&rec);
}

void Params_RestoreFromStorage(void)
{
    ParamStoreRecord rec;

    if (ParamStorage_Read(&rec) == 0)
    {
        Params_ApplyRecord(&rec);
    }
}

void Params_DoFactoryCal(void)
{
    Params_LoadDefaults();
    Params_Save();
}

void Params_ApplyZeroCalib(void)
{
    (void)Scale_CaptureZero();
}

void Params_ApplySpanCalib(void)
{
    (void)Scale_CaptureSpan();
}

static void Params_ResolveIncrementIdx(int32_t idx, uint8_t *dp, int32_t *step)
{
    if (idx < 0)
    {
        idx = 0;
    }
    if (idx > 12)
    {
        idx = 12;
    }

    *dp = increment_decimals[idx];
    *step = increment_steps[idx];
}

static int32_t Params_IncrementIdxForWeight(int32_t weight)
{
    int32_t mag = (weight < 0) ? -weight : weight;

    if (p_range_mode >= 2 && p_r3_max > 0 && mag > p_r2_max)
    {
        return p_r3_inc;
    }
    if (p_range_mode >= 1 && p_r2_max > 0 && mag > p_max_weight)
    {
        return p_r2_inc;
    }
    return p_increment_idx;
}

int32_t Params_GetCapacity(void)
{
    return Params_GetMaxWeight();
}

int32_t Params_GetMaxWeight(void)
{
    if (p_range_mode >= 2 && p_r3_max > 0)
    {
        return p_r3_max;
    }
    if (p_range_mode >= 1 && p_r2_max > 0)
    {
        return p_r2_max;
    }
    return p_max_weight;
}

int32_t Params_GetResolution(void)
{
    uint8_t dp;
    int32_t step;

    Params_ResolveIncrementIdx(p_increment_idx, &dp, &step);
    return step;
}

int32_t Params_GetResolutionFor(int32_t weight)
{
    uint8_t dp;
    int32_t step;

    Params_ResolveIncrementIdx(Params_IncrementIdxForWeight(weight), &dp, &step);
    return step;
}

int32_t Params_GetRangeMode(void)
{
    return p_range_mode;
}

int32_t Params_GetRangeMax(int32_t range)
{
    if (range <= 0)
    {
        return p_max_weight;
    }
    if (range == 1)
    {
        return p_r2_max;
    }
    return p_r3_max;
}

int32_t Params_GetRangeInc(int32_t range)
{
    if (range <= 0)
    {
        return p_increment_idx;
    }
    if (range == 1)
    {
        return p_r2_inc;
    }
    return p_r3_inc;
}

void Params_ApplyRangeSetup(int32_t mode, const int32_t *maxs, const int32_t *incs)
{
    if (maxs == NULL || incs == NULL)
    {
        return;
    }

    p_range_mode     = Params_ClampI32(mode, 0, 2);
    p_max_weight     = Params_ClampI32(maxs[0], 0, 999999);
    p_increment_idx  = Params_ClampI32(incs[0], 0, 12);
    p_r2_max         = Params_ClampI32(maxs[1], 0, 999999);
    p_r2_inc         = Params_ClampI32(incs[1], 0, 12);
    p_r3_max         = Params_ClampI32(maxs[2], 0, 999999);
    p_r3_inc         = Params_ClampI32(incs[2], 0, 12);
    Params_Save();
}

const char *Params_RangeModeLabel(int32_t idx)
{
    if (idx < 0 || idx > 2)
    {
        idx = 0;
    }
    return range_mode_labels[idx];
}

const char *Params_IncrementLabel(int32_t idx)
{
    if (idx < 0)
    {
        idx = 0;
    }
    if (idx > 12)
    {
        idx = 12;
    }
    return increment_labels[idx];
}

uint8_t Params_IncrementCount(void)
{
    return MENU_COUNT(increment_labels);
}

int32_t Params_GetUnits(void)
{
    return p_units;
}

int32_t Params_GetDecimalPt(void)
{
    uint8_t dp;
    int32_t step;

    Params_ResolveIncrementIdx(p_increment_idx, &dp, &step);
    return (int32_t)dp;
}

int32_t Params_GetDecimalPtFor(int32_t weight)
{
    uint8_t dp;
    int32_t step;

    Params_ResolveIncrementIdx(Params_IncrementIdxForWeight(weight), &dp, &step);
    return (int32_t)dp;
}

int32_t Params_GetZeroOffset(void)
{
    return p_zero_offset;
}

int32_t Params_GetSpanCal(void)
{
    return p_span_cal;
}

int32_t Params_GetSpanTestWeight(void)
{
    return p_span_test_wt;
}

int32_t *Params_SpanCalPtr(void)
{
    return &p_span_cal;
}

int32_t Params_GetDigitalFilter(void)
{
    int32_t taps = 11 - Params_GetFilterBandwidth();

    if (taps < 1)
    {
        taps = 1;
    }
    if (taps > 10)
    {
        taps = 10;
    }
    return taps;
}

int32_t Params_GetFilterBandwidth(void)
{
    return p_filt_bandwidth;
}

int32_t Params_GetFilterIntensity(void)
{
    return p_filt_intensity;
}

int32_t Params_GetFilterSpike(void)
{
    return p_filt_spike;
}

void Params_ApplyFilterLevel(int32_t level)
{
    if (level < 0 || level > 4)
    {
        p_filter_level = 5;
        return;
    }

    p_filter_level   = level;
    p_filt_bandwidth = filt_preset_bw[level];
    p_filt_intensity = filt_preset_intensity[level];
    p_filt_spike     = filt_preset_spike[level];
    p_stable_tenths  = filt_preset_stable[level];
}

void Params_MarkFilterCustom(void)
{
    p_filter_level = 5;
}

int32_t Params_GetStability(void)
{
    return p_stable_tenths;
}

int32_t Params_GetZeroTrack(void)
{
    return p_zero_track;
}

int32_t Params_GetCalMethod(void)
{
    return p_cal_method;
}

void Params_SetCalMethodPhysical(void)
{
    if (p_cal_method != 1) {
        p_cal_method = 1;
    }
}

int32_t Params_GetCellCount(void)
{
    return p_cell_count;
}

int32_t Params_GetCellCap(void)
{
    return p_cell_cap;
}

float Params_GetSensMvv(void)
{
    return p_sens_mvv;
}

int32_t Params_GetEstDeadLoad(void)
{
    return p_est_deadload;
}

int32_t Params_GetMaxZeroPct(void)
{
    return p_max_zero_pct;
}

int32_t Params_GetPowerOnZero(void)
{
    return p_power_on_zero;
}

int32_t Params_GetPresetTare(void)
{
    return p_preset_tare;
}

int32_t Params_GetPeakHold(void)
{
    return p_peak_hold;
}

int32_t Params_GetNetMode(void)
{
    return p_net_mode;
}

int32_t Params_GetOutFn(int32_t channel)
{
    if (channel == 1)
    {
        return p_out1_fn;
    }
    if (channel == 2)
    {
        return p_out2_fn;
    }
    if (channel == 3)
    {
        return p_out3_fn;
    }
    return 0;
}

static int32_t Params_Pick3(int32_t channel, int32_t a, int32_t b, int32_t c)
{
    if (channel == 1)
    {
        return a;
    }
    if (channel == 2)
    {
        return b;
    }
    if (channel == 3)
    {
        return c;
    }
    return 0;
}

int32_t Params_GetOutTrip(int32_t channel)
{
    return Params_Pick3(channel, p_out1_trip, p_out2_trip, p_out3_trip);
}

int32_t Params_GetOutHyst(int32_t channel)
{
    return Params_Pick3(channel, p_out1_hyst, p_out2_hyst, p_out3_hyst);
}

int32_t Params_GetOutTrack(int32_t channel)
{
    return Params_Pick3(channel, p_out1_track, p_out2_track, p_out3_track);
}

int32_t Params_GetOutDelay(int32_t channel)
{
    return Params_Pick3(channel, p_out1_delay, p_out2_delay, p_out3_delay);
}

int32_t Params_GetOutNc(int32_t channel)
{
    return Params_Pick3(channel, p_out1_nc, p_out2_nc, p_out3_nc);
}

int32_t Params_GetInFn(int32_t channel)
{
    if (channel == 1)
    {
        return p_in1_fn;
    }
    if (channel == 2)
    {
        return p_in2_fn;
    }
    return 0;
}

int32_t Params_GetTestOut(int32_t channel)
{
    return Params_Pick3(channel, p_test_out1, p_test_out2, p_test_out3);
}

void Params_SetInputStat(int32_t mask)
{
    p_input_stat = mask;
}

void Params_SetPresetTare(int32_t value)
{
    p_preset_tare = Params_ClampI32(value, 0, 999999);
}

int32_t Params_GetSerBaud(void)
{
    return p_ser_baud;
}

int32_t Params_GetSerAddr(void)
{
    return p_ser_addr;
}

int32_t Params_GetSerParity(void)
{
    return p_ser_parity;
}

int32_t Params_GetSerStop(void)
{
    return p_ser_stop;
}

int32_t Params_GetSerUpdate(void)
{
    return p_ser_update;
}

int32_t Params_GetCanBaud(void)
{
    return p_can_baud;
}

int32_t Params_GetCanNode(void)
{
    return p_can_node;
}

void Params_GetEthIp(uint8_t ip[4])
{
    if (ip == NULL)
    {
        return;
    }
    ip[0] = (uint8_t)p_eth_ip0;
    ip[1] = (uint8_t)p_eth_ip1;
    ip[2] = (uint8_t)p_eth_ip2;
    ip[3] = (uint8_t)p_eth_ip3;
}

void Params_GetEthMask(uint8_t mask[4])
{
    if (mask == NULL)
    {
        return;
    }
    mask[0] = (uint8_t)p_eth_mask0;
    mask[1] = (uint8_t)p_eth_mask1;
    mask[2] = (uint8_t)p_eth_mask2;
    mask[3] = (uint8_t)p_eth_mask3;
}

void Params_GetEthGw(uint8_t gw[4])
{
    if (gw == NULL)
    {
        return;
    }
    gw[0] = (uint8_t)p_eth_gw0;
    gw[1] = (uint8_t)p_eth_gw1;
    gw[2] = (uint8_t)p_eth_gw2;
    gw[3] = (uint8_t)p_eth_gw3;
}

int32_t Params_GetEthPort(void)
{
    return p_eth_port;
}

const char *Params_GetOutFnLabel(int32_t channel)
{
    int32_t fn = Params_GetOutFn(channel);

    if (fn < 0 || fn > 4)
    {
        fn = 0;
    }
    return out_fn_labels[fn];
}

const char *Params_GetInFnLabel(int32_t channel)
{
    int32_t fn;

    if (channel == 1)
    {
        fn = p_in1_fn;
    }
    else if (channel == 2)
    {
        fn = p_in2_fn;
    }
    else
    {
        fn = 0;
    }

    if (fn < 0 || fn > 6)
    {
        fn = 0;
    }
    return in_fn_labels[fn];
}

const char *Params_GetAnaTypeLabel(void)
{
    int32_t t = p_ana_type;

    if (t < 0 || t > 5)
    {
        t = 0;
    }
    return ana_list_labels[t];
}

const char *Params_GetRs485ProtoLabel(void)
{
    int32_t p = p_ser_protocol;

    if (p < 0 || p > 3)
    {
        p = 0;
    }
    return proto_list_labels[p];
}

const char *Params_GetCanProtoLabel(void)
{
    int32_t p = p_can_protocol;

    if (p < 0 || p > 2)
    {
        p = 0;
    }
    return can_list_labels[p];
}

const char *Params_GetEthProtoLabel(void)
{
    int32_t p = p_eth_protocol;

    if (p < 0 || p > 2)
    {
        p = 0;
    }
    return eth_list_labels[p];
}

int32_t Params_GetRs485Protocol(void)
{
    return p_ser_protocol;
}

int32_t Params_GetCanProtocol(void)
{
    return p_can_protocol;
}

int32_t Params_GetEthProtocol(void)
{
    return p_eth_protocol;
}

int32_t Params_GetEthDhcp(void)
{
    return p_eth_dhcp;
}

int32_t Params_GetStableTenths(void)
{
    return p_stable_tenths;
}

int32_t Params_GetAnaType(void)
{
    return p_ana_type;
}

int32_t Params_GetAnaTrack(void)
{
    return p_ana_track;
}

int32_t Params_GetAnaWeightMin(void)
{
    return p_ana_wmin;
}

int32_t Params_GetAnaWeightMax(void)
{
    return p_ana_wmax;
}

float Params_GetAnaTrimMin(void)
{
    return p_ana_trim_min;
}

float Params_GetAnaTrimMax(void)
{
    return p_ana_trim_max;
}

int32_t Params_GetAnaTest(void)
{
    return p_ana_test;
}

float Params_GetGravCal(void)
{
    return p_grav_cal;
}

float Params_GetGravUse(void)
{
    return p_grav_use;
}

int32_t Params_GetLinPointCount(void)
{
    if (p_lin_idx <= 0)
    {
        return 0;
    }
    return p_lin_idx + 1;
}

int32_t Params_GetLinCountIdx(void)
{
    return p_lin_idx;
}

void Params_SetLinCountIdx(int32_t idx)
{
    p_lin_idx = Params_ClampI32(idx, 0, 4);
    Params_Save();
}

void Params_SetLinWeight(int32_t index, int32_t weight)
{
    if (index < 0 || index > 4)
    {
        return;
    }

    p_lin_w[index] = Params_ClampI32(weight, 0, 999999);
    Params_Save();
}

int32_t *Params_LinCountsPtr(int32_t index)
{
    if (index < 0 || index > 4)
    {
        return NULL;
    }

    return &p_lin_c[index];
}

void Params_GetLinPoint(int32_t index, int32_t *weight, int32_t *counts)
{
    if (index < 0 || index > 4)
    {
        if (weight != NULL)
        {
            *weight = 0;
        }
        if (counts != NULL)
        {
            *counts = 0;
        }
        return;
    }

    if (weight != NULL)
    {
        *weight = p_lin_w[index];
    }
    if (counts != NULL)
    {
        *counts = p_lin_c[index];
    }
}

const char *Params_GetUnitsLabel(void)
{
    if (p_units < 0 || p_units >= (int32_t)MENU_COUNT(units_labels))
    {
        return units_labels[0];
    }

    return units_labels[p_units];
}

void Params_SetZeroOffset(int32_t value)
{
    p_zero_offset = value;
}

void Params_SetSpanCal(int32_t value)
{
    p_span_cal = value;
}

void Params_SetSpanTestWeight(int32_t value)
{
    p_span_test_wt = Params_ClampI32(value, 0, 999999);
}

void Params_SetNetMode(int32_t value)
{
    p_net_mode = (value != 0) ? 1 : 0;
}

void Params_UpdateLiveValues(int32_t weight, int32_t raw_adc)
{
    p_live_weight = weight;
    p_raw_adc     = raw_adc;
    p_cell_mv     = Scale_GetCellMillivolts();
}
