#include "menu_priv.h"

static const int32_t s_digit_place[] = { 100000, 10000, 1000, 100, 10, 1 };

#define DIGIT_SCALE_X   2U
#define DIGIT_SCALE_Y   2U
#define DIGIT_EXTRA     2U
#define DIGIT_Y         22U
#define DIGIT_STEP      ((5U * DIGIT_SCALE_X) + DIGIT_EXTRA)
#define DIGIT_MARK_Y    40U

static int32_t s_digit_min = 1;
static int32_t s_digit_max = 999999;

void Menu_DigitEditSetLimits(int32_t min_v, int32_t max_v)
{
    if (min_v < 0)
    {
        min_v = 0;
    }
    if (max_v > 999999)
    {
        max_v = 999999;
    }
    if (min_v > max_v)
    {
        min_v = max_v;
    }
    s_digit_min = min_v;
    s_digit_max = max_v;
}

void Menu_DigitEditResetCursor(void)
{
    edit_digit_idx = 2U;
}

void Menu_DigitEditClamp(void)
{
    if (edit_i32 < s_digit_min)
    {
        edit_i32 = s_digit_min;
    }
    if (edit_i32 > s_digit_max)
    {
        edit_i32 = s_digit_max;
    }
}

static void Menu_DigitEditBump(int8_t dir)
{
    int32_t place;
    int32_t cur;
    int32_t next;

    if (edit_digit_idx >= 6U)
    {
        edit_digit_idx = 5U;
    }

    place = s_digit_place[edit_digit_idx];
    cur = (edit_i32 / place) % 10;
    next = (cur + (int32_t)dir + 10) % 10;
    edit_i32 = edit_i32 - (cur * place) + (next * place);
    Menu_DigitEditClamp();
}

static void Menu_DrawOneDigit(char c, uint8_t x, uint8_t selected)
{
    char one[2];

    one[0] = c;
    one[1] = '\0';

    if (selected)
    {
        Display_DrawRect(x, (uint8_t)(DIGIT_Y - 1U), (uint8_t)(DIGIT_STEP - 1U), 18U, 1U);
    }

    Display_SetCursor(x, DIGIT_Y);
    Display_WriteStringScaled(one, DIGIT_SCALE_X, DIGIT_SCALE_Y, DIGIT_EXTRA);
}

void Menu_RenderDigitEdit(const char *title, int32_t value)
{
    char num[8];
    uint8_t i;
    uint8_t sel_x;

    if (value < 0)
    {
        value = 0;
    }
    if (value > 999999)
    {
        value = 999999;
    }

    snprintf(num, sizeof(num), "%06ld", (long)value);

    Display_Clear();
    Menu_DrawLine(0, 0, title);

    for (i = 0U; i < 6U; i++)
    {
        Menu_DrawOneDigit(num[i], (uint8_t)(i * DIGIT_STEP), (uint8_t)(i == edit_digit_idx));
    }

    sel_x = (uint8_t)(edit_digit_idx * DIGIT_STEP + 4U);
    Display_SetCursor(sel_x, DIGIT_MARK_Y);
    Display_WriteStringScaled("^", 1U, 1U, 0U);

    Menu_DrawLine(0, 52, "LR=POS UD=VAL ENT=OK");
    Display_Update();
}

uint8_t Menu_HandleDigitEdit(MenuButton btn, uint8_t *enter_action)
{
    if (enter_action != NULL)
    {
        *enter_action = 0U;
    }

    switch (btn)
    {
    case MENU_BTN_LEFT:
        if (edit_digit_idx == 0U)
        {
            edit_digit_idx = 5U;
        }
        else
        {
            edit_digit_idx--;
        }
        return 1U;
    case MENU_BTN_RIGHT:
        edit_digit_idx = (edit_digit_idx + 1U) % 6U;
        return 1U;
    case MENU_BTN_UP:
        Menu_DigitEditBump(+1);
        return 1U;
    case MENU_BTN_DOWN:
        Menu_DigitEditBump(-1);
        return 1U;
    case MENU_BTN_ENTER:
        Menu_DigitEditClamp();
        if (enter_action != NULL)
        {
            *enter_action = 1U;
        }
        return 1U;
    default:
        return 0U;
    }
}
