#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdint.h>

void Buttons_Init(void);
/** Background raw GPIO level logging (independent of main-loop blocking). */
void Buttons_StartMonitorTask(void);
void Buttons_Read(uint8_t *left, uint8_t *right, uint8_t *up, uint8_t *down, uint8_t *enter);
/** Log raw press state (1 = low/active). Call after init to verify pull-ups and wiring. */
void Buttons_LogLevels(void);
/** Log press/release edges to the console (call each poll from main loop). */
void Buttons_LogEdges(uint8_t left, uint8_t right, uint8_t up, uint8_t down, uint8_t enter);

#endif
