#ifndef PARAMS_H
#define PARAMS_H

#include <stdint.h>
#include "param_storage.h"

typedef enum {
    PARAM_TYPE_FOLDER,
    PARAM_TYPE_I32,
    PARAM_TYPE_FLOAT,
    PARAM_TYPE_ZERO_CALIB,
    PARAM_TYPE_SPAN_CALIB,
    PARAM_TYPE_OPTION,
    PARAM_TYPE_ONOFF,
    PARAM_TYPE_ACTION,
    PARAM_TYPE_LIVE,
    PARAM_TYPE_LIN_WIZARD,
    PARAM_TYPE_RANGE_WIZARD,
} ParamType;

typedef void (*MenuActionFn)(void);

typedef struct MenuNode {
    const char             *name;
    ParamType               type;
    void                   *value;
    float                   min;
    float                   max;
    float                   step;
    const char * const     *option_labels;
    uint8_t                 option_count;
    MenuActionFn            action;
    const struct MenuNode  *children;
    uint8_t                 child_count;
    const char             *fcode;
} MenuNode;

extern const MenuNode menu_root[];
extern const uint8_t  MENU_ROOT_COUNT;

void Params_Init(void);
int  Params_Save(void);
void Params_LoadDefaults(void);
void Params_ApplyRecord(const ParamStoreRecord *rec);
void Params_FillRecord(ParamStoreRecord *rec);
void Params_ApplyZeroCalib(void);
void Params_ApplySpanCalib(void);
void Params_RestoreFromStorage(void);
void Params_DoFactoryCal(void);

int32_t      Params_GetCapacity(void);
int32_t      Params_GetMaxWeight(void);
int32_t      Params_GetResolution(void);
int32_t      Params_GetResolutionFor(int32_t weight);
int32_t      Params_GetRangeMode(void);
int32_t      Params_GetRangeMax(int32_t range);
int32_t      Params_GetRangeInc(int32_t range);
void         Params_ApplyRangeSetup(int32_t mode, const int32_t *maxs, const int32_t *incs);
const char  *Params_RangeModeLabel(int32_t idx);
const char  *Params_IncrementLabel(int32_t idx);
uint8_t      Params_IncrementCount(void);
int32_t      Params_GetUnits(void);
int32_t      Params_GetDecimalPt(void);
int32_t      Params_GetDecimalPtFor(int32_t weight);
int32_t      Params_GetZeroOffset(void);
int32_t      Params_GetSpanCal(void);
int32_t      Params_GetSpanTestWeight(void);
int32_t      Params_GetDigitalFilter(void);
int32_t      Params_GetFilterBandwidth(void);
int32_t      Params_GetFilterIntensity(void);
int32_t      Params_GetFilterSpike(void);
void         Params_ApplyFilterLevel(int32_t level);
void         Params_MarkFilterCustom(void);
int32_t      Params_GetStability(void);
int32_t      Params_GetZeroTrack(void);
int32_t      Params_GetCalMethod(void);
void         Params_SetCalMethodPhysical(void);
int32_t      Params_GetCellCount(void);
int32_t      Params_GetCellCap(void);
float        Params_GetSensMvv(void);
int32_t      Params_GetEstDeadLoad(void);
int32_t      Params_GetMaxZeroPct(void);
int32_t      Params_GetPowerOnZero(void);
int32_t      Params_GetPresetTare(void);
int32_t      Params_GetPeakHold(void);
int32_t      Params_GetNetMode(void);
int32_t      Params_GetOutFn(int32_t channel);
int32_t      Params_GetOutTrip(int32_t channel);
int32_t      Params_GetOutHyst(int32_t channel);
int32_t      Params_GetOutTrack(int32_t channel);
int32_t      Params_GetOutDelay(int32_t channel);
int32_t      Params_GetOutNc(int32_t channel);
int32_t      Params_GetInFn(int32_t channel);
int32_t      Params_GetTestOut(int32_t channel);
void         Params_SetInputStat(int32_t mask);
void         Params_SetPresetTare(int32_t value);
int32_t      Params_GetSerBaud(void);
int32_t      Params_GetSerAddr(void);
int32_t      Params_GetSerParity(void);
int32_t      Params_GetSerStop(void);
int32_t      Params_GetSerUpdate(void);
int32_t      Params_GetCanBaud(void);
int32_t      Params_GetCanNode(void);
void         Params_GetEthIp(uint8_t ip[4]);
void         Params_GetEthMask(uint8_t mask[4]);
void         Params_GetEthGw(uint8_t gw[4]);
int32_t      Params_GetEthPort(void);
const char  *Params_GetOutFnLabel(int32_t channel);
const char  *Params_GetInFnLabel(int32_t channel);
const char  *Params_GetAnaTypeLabel(void);
const char  *Params_GetRs485ProtoLabel(void);
const char  *Params_GetCanProtoLabel(void);
const char  *Params_GetEthProtoLabel(void);
int32_t      Params_GetRs485Protocol(void);
int32_t      Params_GetCanProtocol(void);
int32_t      Params_GetEthProtocol(void);
int32_t      Params_GetEthDhcp(void);
int32_t      Params_GetStableTenths(void);
int32_t      Params_GetAnaType(void);
int32_t      Params_GetAnaTrack(void);
int32_t      Params_GetAnaWeightMin(void);
int32_t      Params_GetAnaWeightMax(void);
float        Params_GetAnaTrimMin(void);
float        Params_GetAnaTrimMax(void);
int32_t      Params_GetAnaTest(void);
float        Params_GetGravCal(void);
float        Params_GetGravUse(void);
int32_t      Params_GetLinPointCount(void);
int32_t      Params_GetLinCountIdx(void);
void         Params_SetLinCountIdx(int32_t idx);
void         Params_SetLinWeight(int32_t index, int32_t weight);
int32_t     *Params_LinCountsPtr(int32_t index);
void         Params_GetLinPoint(int32_t index, int32_t *weight, int32_t *counts);
const char  *Params_GetUnitsLabel(void);

void Params_SetZeroOffset(int32_t value);
void Params_SetSpanCal(int32_t value);
void Params_SetSpanTestWeight(int32_t value);
int32_t *Params_SpanCalPtr(void);
void Params_SetNetMode(int32_t value);

void Params_UpdateLiveValues(int32_t weight, int32_t raw_adc);

#endif
