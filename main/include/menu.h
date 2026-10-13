#ifndef MENU_H
#define MENU_H

#include <stdint.h>

typedef enum
{
    MENU_BTN_NONE = 0,
    MENU_BTN_LEFT,
    MENU_BTN_RIGHT,
    MENU_BTN_UP,
    MENU_BTN_DOWN,
    MENU_BTN_ENTER
} MenuButton;

void    Menu_Enter(void);
void    Menu_Exit(void);
uint8_t Menu_IsActive(void);
void    Menu_HandleButton(MenuButton btn);
void    Menu_Render(void);
uint8_t Menu_GetLevelIndex(void);
void    Menu_Tick(void);
void    Menu_EnterOptionDemo(void);
void    Menu_EnterOnOffDemo(void);

#endif
