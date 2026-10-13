#ifndef INPUTS_H
#define INPUTS_H

#include <stdint.h>

void    Inputs_Init(void);
void    Inputs_Tick(void);
uint8_t Inputs_GetLiveMask(void);

#endif
