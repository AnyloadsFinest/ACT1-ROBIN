#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include "driver/i2c_master.h"

void Display_Init(i2c_master_bus_handle_t bus);
void Display_Clear(void);
void Display_Update(void);
void Display_SetCursor(uint8_t x, uint8_t y);
void Display_WriteStringBig(const char *str, uint8_t scale);
void Display_ShowAnyload(void);
void Display_ShowWeight(int32_t weight);
void Display_ShowWeightFormatted(int32_t weight, uint8_t decimal_pt, const char *units);
void Display_FormatWeight(int32_t weight, uint8_t decimal_pt, char *out, uint8_t out_size);
void Display_WriteStringBigTight(const char *str, uint8_t scale);

void Display_WriteStringScaled(const char *str, uint8_t scale_x, uint8_t scale_y, uint8_t extra_space);
void Display_WriteStringScaledColor(const char *str, uint8_t scale_x, uint8_t scale_y, uint8_t extra_space, uint8_t color);
void Display_FillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t color);
void Display_DrawRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t color);
void Display_DrawProgressBar(uint8_t x, uint8_t y, uint8_t width, uint8_t height, uint8_t percent);
void Display_PlayBootAnimation(void);

#endif
