#ifndef ANALOG_DAC_H
#define ANALOG_DAC_H

#include <stdint.h>

void AnalogDac_Init(void);
void AnalogDac_Write(uint16_t counts);
void AnalogDac_SetType(int32_t ana_type);

#endif
