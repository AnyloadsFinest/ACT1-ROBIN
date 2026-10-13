#include "menu_priv.h"

static const char * const lin_wiz_count_labels[] = { "OFF", "2", "3", "4", "5" };

void Menu_StartLinWizard(void)
{
    lin_wiz_active = 1U;
    lin_wiz_point = 0U;
    edit_i32 = Params_GetLinCountIdx();
    menu_state = MENU_STATE_LIN_COUNT;
}

void Menu_LinNextPoint(void)
{
    int32_t weight = 0;
    int32_t counts = 0;

    Params_GetLinPoint((int32_t)lin_wiz_point, &weight, &counts);
    edit_i32 = weight;
    menu_state = MENU_STATE_LIN_WEIGHT;
}

void Menu_LinFinish(void)
{
    lin_wiz_active = 0U;
    Menu_ShowSavedConfirm();
}

void Menu_StartRangeWizard(void)
{
    uint8_t i;

    range_wiz_active = 1U;
    range_wiz_mode = Params_GetRangeMode();
    if (range_wiz_mode < 0)
    {
        range_wiz_mode = 0;
    }
    if (range_wiz_mode > 2)
    {
        range_wiz_mode = 2;
    }
    range_wiz_idx = 0U;

    for (i = 0U; i < 3U; i++)
    {
        range_wiz_max[i] = Params_GetRangeMax((int32_t)i);
        range_wiz_inc[i] = Params_GetRangeInc((int32_t)i);
    }
    if (range_wiz_max[1] <= range_wiz_max[0])
    {
        range_wiz_max[1] = range_wiz_max[0] + 10;
    }
    if (range_wiz_max[2] <= range_wiz_max[1])
    {
        range_wiz_max[2] = range_wiz_max[1] + 10;
    }

    edit_i32 = range_wiz_mode;
    option_window = 0U;
    menu_state = MENU_STATE_RANGE_COUNT;
}

static void Menu_RangeGotoMax(void)
{
    edit_i32 = range_wiz_max[range_wiz_idx];
    menu_state = MENU_STATE_RANGE_MAX;
}

static void Menu_RangeGotoInc(void)
{
    edit_i32 = range_wiz_inc[range_wiz_idx];
    if (edit_i32 < 0)
    {
        edit_i32 = 0;
    }
    if (edit_i32 >= (int32_t)Params_IncrementCount())
    {
        edit_i32 = (int32_t)Params_IncrementCount() - 1;
    }
    option_window = 0U;
    if (edit_i32 >= (int32_t)MENU_VISIBLE_ROWS)
    {
        option_window = (uint8_t)edit_i32 - MENU_VISIBLE_ROWS + 1U;
    }
    menu_state = MENU_STATE_RANGE_INC;
}

static void Menu_RangeFinish(void)
{
    range_wiz_active = 0U;
    Params_ApplyRangeSetup(range_wiz_mode, range_wiz_max, range_wiz_inc);
    Menu_ShowSavedConfirm();
}

static void Menu_ChangeRangeCount(int8_t dir)
{
    edit_i32 += (int32_t)dir;
    if (edit_i32 < 0)
    {
        edit_i32 = 2;
    }
    if (edit_i32 > 2)
    {
        edit_i32 = 0;
    }
}

static void Menu_ChangeRangeMax(int8_t dir)
{
    edit_i32 += (int32_t)dir * 10;
    if (edit_i32 < 0)
    {
        edit_i32 = 0;
    }
    if (edit_i32 > 999999)
    {
        edit_i32 = 999999;
    }
}

static void Menu_ChangeRangeInc(int8_t dir)
{
    int32_t count = (int32_t)Params_IncrementCount();

    edit_i32 += (int32_t)dir;
    if (edit_i32 < 0)
    {
        edit_i32 = count - 1;
    }
    if (edit_i32 >= count)
    {
        edit_i32 = 0;
    }
}

static void Menu_RenderRangeChoiceList(const char *header, uint8_t use_inc_labels,
                                      uint8_t count, const char *footer)
{
    char line[32];
    uint8_t i;
    uint8_t idx;
    uint8_t row_y = 16;
    const char *label;

    if (edit_i32 < 0)
    {
        edit_i32 = 0;
    }
    if (edit_i32 >= (int32_t)count)
    {
        edit_i32 = (int32_t)count - 1;
    }

    if (edit_i32 < (int32_t)option_window)
    {
        option_window = (uint8_t)edit_i32;
    }
    else if (edit_i32 >= (int32_t)(option_window + MENU_VISIBLE_ROWS))
    {
        option_window = (uint8_t)edit_i32 - MENU_VISIBLE_ROWS + 1U;
    }

    Display_Clear();
    Menu_DrawLine(0, 0, header);

    for (i = 0U; i < MENU_VISIBLE_ROWS; i++)
    {
        idx = (uint8_t)(option_window + i);
        if (idx >= count)
        {
            break;
        }

        if (use_inc_labels != 0U)
        {
            label = Params_IncrementLabel((int32_t)idx);
        }
        else
        {
            label = Params_RangeModeLabel((int32_t)idx);
        }

        snprintf(line, sizeof(line), "%c %s",
                 (idx == (uint8_t)edit_i32) ? '>' : ' ',
                 label);
        Menu_DrawLine(0, row_y, line);
        row_y += 14;
    }

    Menu_DrawAt(footer, 0U, OPT_FOOTER_Y, 1, 1, 1);
    Display_Update();
}

void Menu_RenderRangeCount(void)
{
    const MenuNode *node = Menu_CurrentNode();
    char title[32];

    if (node != NULL)
    {
        Menu_NodeTitle(node, title, sizeof(title));
    }
    else
    {
        snprintf(title, sizeof(title), "MULTI RANGE");
    }

    if (edit_i32 <= 0)
    {
        Menu_RenderRangeChoiceList(title, 0U, 3U, "ENT SAVE  LEFT ESC");
    }
    else
    {
        Menu_RenderRangeChoiceList(title, 0U, 3U, "ENT NEXT  LEFT ESC");
    }
}

void Menu_RenderRangeMax(void)
{
    char hdr[32];
    char value_str[20];

    snprintf(hdr, sizeof(hdr), "RANGE %u MAX",
             (unsigned)(range_wiz_idx + 1U));
    snprintf(value_str, sizeof(value_str), "%ld", (long)edit_i32);

    Display_Clear();
    Menu_DrawLine(0, 0, hdr);
    Menu_DrawLine(0, 16, "MAX");
    Display_SetCursor(0, 32);
    Display_WriteStringScaled(value_str, 2, 2, 2);
    Menu_DrawLine(0, 52, "ENT NEXT  LEFT BACK");
    Display_Update();
}

void Menu_RenderRangeInc(void)
{
    char hdr[24];
    const char *footer;

    snprintf(hdr, sizeof(hdr), "RANGE %u INC",
             (unsigned)(range_wiz_idx + 1U));
    footer = (range_wiz_idx < (uint8_t)range_wiz_mode)
             ? "ENT NEXT  LEFT BACK"
             : "ENT SAVE  LEFT BACK";
    Menu_RenderRangeChoiceList(hdr, 1U, Params_IncrementCount(), footer);
}

static void Menu_ChangeLinCount(int8_t dir)
{
    edit_i32 += (int32_t)dir;
    if (edit_i32 < 0)
    {
        edit_i32 = 4;
    }
    if (edit_i32 > 4)
    {
        edit_i32 = 0;
    }
}

static void Menu_ChangeLinWeight(int8_t dir)
{
    edit_i32 += (int32_t)dir;
    if (edit_i32 < 0)
    {
        edit_i32 = 0;
    }
    if (edit_i32 > 999999)
    {
        edit_i32 = 999999;
    }
}

void Menu_RenderLinCount(void)
{
    const char *label;

    if (edit_i32 < 0 || edit_i32 > 4)
    {
        edit_i32 = 0;
    }

    label = lin_wiz_count_labels[edit_i32];

    Display_Clear();
    {
        const MenuNode *node = Menu_CurrentNode();

        if (node != NULL)
        {
            char title[32];

            Menu_NodeTitle(node, title, sizeof(title));
            Menu_DrawLine(0, 0, title);
        }
        else
        {
            Menu_DrawLine(0, 0, "MULTI POINT");
        }
    }
    Menu_DrawLine(0, 16, "HOW MANY");
    Display_SetCursor(0, 32);
    Display_WriteStringScaled(label, 2, 2, 2);
    Menu_DrawLine(0, 52, "ENT NEXT  LEFT ESC");
    Display_Update();
}

void Menu_RenderSpanWeight(void)
{
    Menu_RenderDigitEdit("SPAN TEST WT", edit_i32);
}

uint8_t Menu_HandleSpanWeight(MenuButton btn)
{
    uint8_t enter_action = 0U;

    if (btn == MENU_BTN_LEFT)
    {
        menu_state = MENU_STATE_BROWSE;
        return 1U;
    }

    if (Menu_HandleDigitEdit(btn, &enter_action))
    {
        if (enter_action != 0U)
        {
            if (edit_i32 <= 0)
            {
                return 0U;
            }
            Params_SetSpanTestWeight(edit_i32);
            Params_Save();
            menu_state = MENU_STATE_CAL_PROMPT;
        }
        Menu_RenderSpanWeight();
        return 1U;
    }

    return 0U;
}

void Menu_RenderLinWeight(void)
{
    char hdr[20];
    char value_str[20];

    snprintf(hdr, sizeof(hdr), "POINT %u OF %ld",
             (unsigned)(lin_wiz_point + 1U),
             (long)Params_GetLinPointCount());
    snprintf(value_str, sizeof(value_str), "%ld", (long)edit_i32);

    Display_Clear();
    Menu_DrawLine(0, 0, hdr);
    Menu_DrawLine(0, 16, "TEST WT");
    Display_SetCursor(0, 32);
    Display_WriteStringScaled(value_str, 2, 2, 2);
    Menu_DrawLine(0, 52, "ENT NEXT  LEFT BACK");
    Display_Update();
}

void Menu_RenderLinPrompt(void)
{
    char hdr[20];

    snprintf(hdr, sizeof(hdr), "POINT %u OF %ld",
             (unsigned)(lin_wiz_point + 1U),
             (long)Params_GetLinPointCount());

    Display_Clear();
    Menu_DrawLine(0, 0, hdr);
    Menu_DrawLine(0, 20, "PLACE TEST WT");
    Menu_DrawLine(0, 34, "THEN PRESS ENTER");
    Menu_DrawLine(0, 52, "LEFT=BACK");
    Display_Update();
}

uint8_t Menu_HandleLinCount(MenuButton btn)
{
    switch (btn)
    {
    case MENU_BTN_LEFT:
        lin_wiz_active = 0U;
        menu_state = MENU_STATE_BROWSE;
        return 1U;
    case MENU_BTN_RIGHT:
    case MENU_BTN_UP:
        Menu_ChangeLinCount(+1);
        return 1U;
    case MENU_BTN_DOWN:
        Menu_ChangeLinCount(-1);
        return 1U;
    case MENU_BTN_ENTER:
        Params_SetLinCountIdx(edit_i32);
        if (Params_GetLinPointCount() < 2)
        {
            Menu_LinFinish();
        }
        else
        {
            lin_wiz_point = 0U;
            Menu_LinNextPoint();
        }
        return 1U;
    default:
        return 0U;
    }
}

uint8_t Menu_HandleLinWeight(MenuButton btn)
{
    switch (btn)
    {
    case MENU_BTN_UP:
        Menu_ChangeLinWeight(+1);
        return 1U;
    case MENU_BTN_DOWN:
        Menu_ChangeLinWeight(-1);
        return 1U;
    case MENU_BTN_LEFT:
        menu_state = MENU_STATE_LIN_COUNT;
        edit_i32 = Params_GetLinCountIdx();
        return 1U;
    case MENU_BTN_ENTER:
        if (edit_i32 <= 0)
        {
            return 0U;
        }
        Params_SetLinWeight((int32_t)lin_wiz_point, edit_i32);
        menu_state = MENU_STATE_LIN_PROMPT;
        return 1U;
    default:
        return 0U;
    }
}

uint8_t Menu_HandleLinPrompt(MenuButton btn)
{
    if (btn == MENU_BTN_ENTER)
    {
        cal_mode = CAL_MODE_SPAN;
        Menu_StartCal();
        return 0U;
    }
    if (btn == MENU_BTN_LEFT)
    {
        menu_state = MENU_STATE_LIN_WEIGHT;
        return 1U;
    }
    return 0U;
}

uint8_t Menu_HandleRangeCount(MenuButton btn)
{
    switch (btn)
    {
    case MENU_BTN_LEFT:
        range_wiz_active = 0U;
        menu_state = MENU_STATE_BROWSE;
        return 1U;
    case MENU_BTN_UP:
        Menu_ChangeRangeCount(-1);
        return 1U;
    case MENU_BTN_DOWN:
    case MENU_BTN_RIGHT:
        Menu_ChangeRangeCount(+1);
        return 1U;
    case MENU_BTN_ENTER:
        range_wiz_mode = edit_i32;
        if (range_wiz_mode <= 0)
        {
            Menu_RangeFinish();
        }
        else
        {
            range_wiz_idx = 1U;
            Menu_RangeGotoMax();
        }
        return 1U;
    default:
        return 0U;
    }
}

uint8_t Menu_HandleRangeMax(MenuButton btn)
{
    switch (btn)
    {
    case MENU_BTN_UP:
        Menu_ChangeRangeMax(+1);
        return 1U;
    case MENU_BTN_DOWN:
        Menu_ChangeRangeMax(-1);
        return 1U;
    case MENU_BTN_LEFT:
        if (range_wiz_idx <= 1U)
        {
            edit_i32 = range_wiz_mode;
            option_window = 0U;
            menu_state = MENU_STATE_RANGE_COUNT;
        }
        else
        {
            range_wiz_idx--;
            Menu_RangeGotoInc();
        }
        return 1U;
    case MENU_BTN_ENTER:
        if (edit_i32 <= 0)
        {
            return 0U;
        }
        if ((range_wiz_idx > 0U) &&
            (edit_i32 <= range_wiz_max[range_wiz_idx - 1U]))
        {
            edit_i32 = range_wiz_max[range_wiz_idx - 1U] + 10;
            return 1U;
        }
        range_wiz_max[range_wiz_idx] = edit_i32;
        Menu_RangeGotoInc();
        return 1U;
    default:
        return 0U;
    }
}

uint8_t Menu_HandleRangeInc(MenuButton btn)
{
    switch (btn)
    {
    case MENU_BTN_UP:
        Menu_ChangeRangeInc(-1);
        return 1U;
    case MENU_BTN_DOWN:
    case MENU_BTN_RIGHT:
        Menu_ChangeRangeInc(+1);
        return 1U;
    case MENU_BTN_LEFT:
        Menu_RangeGotoMax();
        return 1U;
    case MENU_BTN_ENTER:
        range_wiz_inc[range_wiz_idx] = edit_i32;
        if (range_wiz_idx < (uint8_t)range_wiz_mode)
        {
            range_wiz_idx++;
            Menu_RangeGotoMax();
        }
        else
        {
            Menu_RangeFinish();
        }
        return 1U;
    default:
        return 0U;
    }
}
