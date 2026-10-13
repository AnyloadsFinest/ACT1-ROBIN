#ifndef DIG_IO_H
#define DIG_IO_H

#include <stdint.h>

void    DigIo_Init(void);
void    DigIo_SetOut(int32_t channel, uint8_t on);
uint8_t DigIo_GetOut(int32_t channel);
uint8_t DigIo_GetIn(int32_t channel);
uint8_t DigIo_GetInMask(void);

#endif
