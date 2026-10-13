#ifndef OUTPUTS_H
#define OUTPUTS_H

#include <stdint.h>

void    Outputs_Init(void);
void    Outputs_Tick(void);
uint8_t Outputs_Eval(int32_t channel);

#endif
