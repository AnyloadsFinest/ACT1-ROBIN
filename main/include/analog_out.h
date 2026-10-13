#ifndef ANALOG_OUT_H
#define ANALOG_OUT_H

#include <stddef.h>
#include <stdint.h>

void     AnalogOut_Init(void);
void     AnalogOut_Tick(void);
float    AnalogOut_GetValue(void);
uint16_t AnalogOut_GetCounts(void);
uint8_t  AnalogOut_IsCurrent(void);
void     AnalogOut_FormatLive(char *buf, size_t buf_size);

#endif
