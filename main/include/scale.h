#ifndef SCALE_H
#define SCALE_H

#include <stdint.h>

void     Scale_Init(void);
void     Scale_Tick(void);
int32_t  Scale_GetRawAdc(void);
int32_t  Scale_GetFilteredAdc(void);
int32_t  Scale_GetDisplayWeight(void);
int32_t  Scale_GetGrossWeight(void);
int32_t  Scale_GetNetWeight(void);
int32_t  Scale_GetCellMillivolts(void);
const char *Scale_GetUnitsLabel(void);
uint8_t  Scale_IsStable(void);
uint8_t  Scale_IsAtZero(void);
uint8_t  Scale_IsNet(void);
uint8_t  Scale_IsOverload(void);

void Scale_ResetFilterForCal(void);
uint8_t Scale_CaptureZero(void);
/** Returns 1 on success (span delta OK). */
uint8_t Scale_CaptureSpan(void);
/** Returns 1 on success when capturing span cal ADC. */
uint8_t Scale_CaptureAdc(int32_t *dest);
/** Short reason for last failed capture (static buffer; for OLED). */
const char *Scale_GetLastCalFailMsg(void);
void Scale_DoZero(void);
void Scale_DoTare(void);
void Scale_ClearTare(void);
void Scale_ResetPeak(void);
void Scale_SetInputPeakHold(uint8_t hold);
int32_t Scale_GetTareWeight(void);

#endif
