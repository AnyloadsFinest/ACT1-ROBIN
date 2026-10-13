#ifndef MENU_PRIV_H
#define MENU_PRIV_H

#include "menu.h"
#include "params.h"
#include "display.h"
#include "scale.h"
#include "main.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define OLED_DISPLAY_WIDTH     128U
#define OPT_HEADER_Y           0U
#define OPT_FOOTER_Y           54U
#define MENU_VISIBLE_ROWS      3U
#define MENU_STACK_MAX         6U
#define ZERO_COUNTDOWN_START   3U
#define ZERO_COUNTDOWN_STEP_MS 1000U
#define ZERO_PROGRESS_MS       2000U
#define ZERO_COMPLETE_MS       1500U
#define ZERO_FAIL_COMPLETE_MS  3500U
#define SAVED_CONFIRM_MS       1500U

typedef enum
{
    MENU_STATE_BROWSE = 0,
    MENU_STATE_EDIT_VALUE,
    MENU_STATE_OPTION_SELECT,
    MENU_STATE_ONOFF_TOGGLE,
    MENU_STATE_CAL_PROMPT,
    MENU_STATE_SPAN_WEIGHT,
    MENU_STATE_CAL_ACTIVE,
    MENU_STATE_LIVE_VIEW,
    MENU_STATE_SAVED_CONFIRM,
    MENU_STATE_LIN_COUNT,
    MENU_STATE_LIN_WEIGHT,
    MENU_STATE_LIN_PROMPT,
    MENU_STATE_RANGE_COUNT,
    MENU_STATE_RANGE_MAX,
    MENU_STATE_RANGE_INC,
    MENU_STATE_COUNT
} MenuState;

typedef enum
{
    CAL_MODE_ZERO = 0,
    CAL_MODE_SPAN
} CalMode;

typedef enum
{
    ZERO_PHASE_COUNTDOWN = 0,
    ZERO_PHASE_PROGRESS,
    ZERO_PHASE_COMPLETE
} ZeroPhase;

typedef struct
{
    const MenuNode *items;
    uint8_t         count;
    uint8_t         selected;
    uint8_t         window;
} MenuFrame;

extern uint8_t   menu_active;
extern MenuState menu_state;
extern MenuFrame menu_stack[MENU_STACK_MAX];
extern uint8_t   menu_stack_depth;
extern CalMode   cal_mode;
extern ZeroPhase cal_phase;
extern uint32_t  cal_phase_start_ms;
extern uint8_t   cal_countdown;
extern uint8_t   cal_progress;
extern uint32_t  save_confirm_start_ms;
extern int32_t   edit_i32;
extern uint8_t   edit_digit_idx;
extern float     edit_f32;
extern uint8_t   option_window;
extern uint8_t   lin_wiz_active;
extern uint8_t   lin_wiz_point;
extern uint8_t   range_wiz_active;
extern int32_t   range_wiz_mode;
extern uint8_t   range_wiz_idx;
extern int32_t   range_wiz_max[3];
extern int32_t   range_wiz_inc[3];

MenuFrame       *Menu_CurrentFrame(void);
const MenuNode  *Menu_CurrentNode(void);
void             Menu_DrawCentered(const char *str, uint8_t y, uint8_t scale_x, uint8_t scale_y, uint8_t extra);
void             Menu_DrawAt(const char *str, uint8_t x, uint8_t y, uint8_t scale_x, uint8_t scale_y, uint8_t extra);
void             Menu_DrawLine(uint8_t x, uint8_t y, const char *str);
void             Menu_NodeTitle(const MenuNode *node, char *buf, uint8_t size);
void             Menu_ShowSavedConfirm(void);

void    Menu_StartCal(void);
void    Menu_RenderCalPrompt(void);
void    Menu_RenderCalActive(void);
uint8_t Menu_HandleCalPrompt(MenuButton btn);
void    Menu_CalTick(void);

void    Menu_StartLinWizard(void);
void    Menu_StartRangeWizard(void);
void    Menu_LinNextPoint(void);
void    Menu_LinFinish(void);
void    Menu_RenderLinCount(void);
void    Menu_RenderLinWeight(void);
void    Menu_RenderLinPrompt(void);
void    Menu_RenderSpanWeight(void);
uint8_t Menu_HandleSpanWeight(MenuButton btn);
void    Menu_DigitEditSetLimits(int32_t min_v, int32_t max_v);
void    Menu_DigitEditClamp(void);
void    Menu_DigitEditResetCursor(void);
void    Menu_RenderDigitEdit(const char *title, int32_t value);
/** Returns 1 if handled; sets *enter_action: 0=none, 1=save+browse confirm, 2=save+cal prompt. */
uint8_t Menu_HandleDigitEdit(MenuButton btn, uint8_t *enter_action);
void    Menu_RenderRangeCount(void);
void    Menu_RenderRangeMax(void);
void    Menu_RenderRangeInc(void);
uint8_t Menu_HandleLinCount(MenuButton btn);
uint8_t Menu_HandleLinWeight(MenuButton btn);
uint8_t Menu_HandleLinPrompt(MenuButton btn);
uint8_t Menu_HandleRangeCount(MenuButton btn);
uint8_t Menu_HandleRangeMax(MenuButton btn);
uint8_t Menu_HandleRangeInc(MenuButton btn);

#endif
