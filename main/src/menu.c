#include "menu_priv.h"
#include "analog_out.h"
#include "inputs.h"
#include "board.h"

uint8_t   menu_active = 0;
MenuState menu_state = MENU_STATE_BROWSE;
MenuFrame menu_stack[MENU_STACK_MAX];
uint8_t   menu_stack_depth = 0;
CalMode   cal_mode = CAL_MODE_ZERO;
ZeroPhase cal_phase = ZERO_PHASE_COUNTDOWN;
uint32_t  cal_phase_start_ms = 0;
uint8_t   cal_countdown = ZERO_COUNTDOWN_START;
uint8_t   cal_progress = 0;
uint32_t  save_confirm_start_ms = 0;
int32_t   edit_i32 = 0;
uint8_t   edit_digit_idx = 0U;
float     edit_f32 = 0.0f;
uint8_t   option_window = 0U;
uint8_t   lin_wiz_active = 0U;
uint8_t   lin_wiz_point  = 0U;
uint8_t   range_wiz_active = 0U;
int32_t   range_wiz_mode   = 0;
uint8_t   range_wiz_idx    = 0U;
int32_t   range_wiz_max[3] = { 0, 0, 0 };
int32_t   range_wiz_inc[3] = { 0, 0, 0 };

static void Menu_StartEdit(void);
static void Menu_SaveEdit(void);
static void Menu_ChangeValue(int8_t dir);
static void Menu_StartOptionEdit(void);
static void Menu_ChangeOption(int8_t dir);
static void Menu_StartOnOffEdit(void);
static void Menu_RenderBrowse(void);
static void Menu_RenderEditValue(void);
static void Menu_RenderOptionSelect(void);
static void Menu_RenderOnOffToggle(void);
static void Menu_RenderLiveView(void);
static void Menu_RenderSavedConfirm(void);
static void Menu_ResetStack(void);
static void Menu_PushFrame(const MenuNode *items, uint8_t count);
static void Menu_PopFrame(void);
static void Menu_GetHeader(char *buf, uint8_t size);
static uint8_t Menu_IsItemVisible(const MenuNode *node);
static uint8_t Menu_NextVisibleIndex(const MenuFrame *frame, uint8_t start, int8_t dir);
static void Menu_SyncVisibleWindow(MenuFrame *frame);
static void Menu_BrowseUp(void);
static void Menu_BrowseDown(void);
static uint8_t Menu_OpenNode(const MenuNode *node);
static const char *Menu_BrowseSuffix(const MenuNode *item);

MenuFrame *Menu_CurrentFrame(void)
{
    if (menu_stack_depth == 0U)
    {
        return NULL;
    }

    return &menu_stack[menu_stack_depth - 1U];
}

const MenuNode *Menu_CurrentNode(void)
{
    MenuFrame *frame = Menu_CurrentFrame();

    if (frame == NULL || frame->count == 0U)
    {
        return NULL;
    }

    return &frame->items[frame->selected];
}

static void Menu_ResetStack(void)
{
    menu_stack_depth = 1U;
    menu_stack[0].items    = menu_root;
    menu_stack[0].count    = MENU_ROOT_COUNT;
    menu_stack[0].selected = 0U;
    menu_stack[0].window   = 0U;
}

static void Menu_PushFrame(const MenuNode *items, uint8_t count)
{
    if (menu_stack_depth >= MENU_STACK_MAX || count == 0U)
    {
        return;
    }

    menu_stack[menu_stack_depth].items    = items;
    menu_stack[menu_stack_depth].count    = count;
    menu_stack[menu_stack_depth].selected = 0U;
    menu_stack[menu_stack_depth].window   = 0U;
    menu_stack_depth++;
}

static void Menu_PopFrame(void)
{
    if (menu_stack_depth > 1U)
    {
        menu_stack_depth--;
    }
}

void Menu_DrawCentered(const char *str, uint8_t y, uint8_t scale_x, uint8_t scale_y, uint8_t extra)
{
    size_t len = strlen(str);
    uint8_t w = (uint8_t)(len * ((5U * scale_x) + extra));
    uint8_t x = (w >= OLED_DISPLAY_WIDTH) ? 0U : (uint8_t)((OLED_DISPLAY_WIDTH - w) / 2U);

    Display_SetCursor(x, y);
    Display_WriteStringScaled(str, scale_x, scale_y, extra);
}

void Menu_DrawAt(const char *str, uint8_t x, uint8_t y, uint8_t scale_x, uint8_t scale_y, uint8_t extra)
{
    Display_SetCursor(x, y);
    Display_WriteStringScaled(str, scale_x, scale_y, extra);
}

void Menu_DrawLine(uint8_t x, uint8_t y, const char *str)
{
    Display_SetCursor(x, y);
    Display_WriteStringScaled(str, 1, 1, 1);
}

void Menu_NodeTitle(const MenuNode *node, char *buf, uint8_t size)
{
    if (buf == NULL || size == 0U)
    {
        return;
    }

    if (node == NULL)
    {
        buf[0] = '\0';
        return;
    }

    if (node->fcode != NULL && node->fcode[0] != '\0')
    {
        snprintf(buf, size, "%s %s", node->fcode, node->name);
    }
    else
    {
        snprintf(buf, size, "%s", node->name);
    }
}

static void Menu_GetHeader(char *buf, uint8_t size)
{
    MenuFrame *frame;
    const MenuNode *node;

    if (menu_stack_depth <= 1U)
    {
        snprintf(buf, size, "MAIN MENU");
        return;
    }

    if (menu_stack_depth == 2U)
    {
        Menu_NodeTitle(&menu_root[menu_stack[0].selected], buf, size);
        return;
    }

    frame = &menu_stack[menu_stack_depth - 2U];
    node = &frame->items[frame->selected];
    Menu_NodeTitle(node, buf, size);
}

void Menu_Enter(void)
{
    menu_active = 1;
    menu_state  = MENU_STATE_BROWSE;
    Menu_ResetStack();
    Menu_Render();
}

void Menu_EnterOptionDemo(void)
{
    menu_active = 1;
    menu_stack_depth = 2U;
    menu_stack[0].items    = menu_root;
    menu_stack[0].count    = MENU_ROOT_COUNT;
    menu_stack[0].selected = 0U;
    menu_stack[0].window   = 0U;
    menu_stack[1].items    = menu_root[0].children;
    menu_stack[1].count    = menu_root[0].child_count;
    menu_stack[1].selected = 1U;
    menu_stack[1].window   = 0U;
    Menu_StartOptionEdit();
    menu_state = MENU_STATE_OPTION_SELECT;
    Menu_Render();
}

void Menu_EnterOnOffDemo(void)
{
    menu_active = 1;
    menu_stack_depth = 2U;
    menu_stack[0].items    = menu_root;
    menu_stack[0].count    = MENU_ROOT_COUNT;
    menu_stack[0].selected = 2U;
    menu_stack[0].window   = 0U;
    menu_stack[1].items    = menu_root[2].children;
    menu_stack[1].count    = menu_root[2].child_count;
    menu_stack[1].selected = 1U;
    menu_stack[1].window   = 0U;
    Menu_StartOnOffEdit();
    menu_state = MENU_STATE_ONOFF_TOGGLE;
    Menu_Render();
}

void Menu_Exit(void)
{
    lin_wiz_active = 0U;
    range_wiz_active = 0U;
    menu_state = MENU_STATE_BROWSE;
    menu_active = 0;
    Params_Save();
}

uint8_t Menu_IsActive(void)
{
    return menu_active;
}

uint8_t Menu_GetLevelIndex(void)
{
    if (menu_stack_depth == 0U)
    {
        return 0U;
    }

    return menu_stack[0].selected;
}

static uint8_t Menu_HandleNone(MenuButton btn)
{
    (void)btn;
    return 0U;
}

static uint8_t Menu_HandleBrowse(MenuButton btn)
{
    MenuFrame *frame = Menu_CurrentFrame();

    if (frame == NULL)
    {
        return 0U;
    }

    switch (btn)
    {
    case MENU_BTN_UP:
        Menu_BrowseUp();
        return 1U;
    case MENU_BTN_DOWN:
        Menu_BrowseDown();
        return 1U;
    case MENU_BTN_RIGHT:
    case MENU_BTN_ENTER:
        return Menu_OpenNode(Menu_CurrentNode());
    case MENU_BTN_LEFT:
        if (menu_stack_depth <= 1U)
        {
            Menu_Exit();
            return 0U;
        }
        Menu_PopFrame();
        return 1U;
    default:
        return 0U;
    }
}

static uint8_t Menu_NodeUsesDigitEdit(const MenuNode *node)
{
    if (node == NULL || node->name == NULL)
    {
        return 0U;
    }
    return (strcmp(node->name, "SPAN TEST WT") == 0 ||
            strcmp(node->name, "MAX WEIGHT") == 0) ? 1U : 0U;
}

static uint8_t Menu_HandleEditValue(MenuButton btn)
{
    const MenuNode *node = Menu_CurrentNode();
    uint8_t enter_action = 0U;

    if (Menu_NodeUsesDigitEdit(node))
    {
        if (Menu_HandleDigitEdit(btn, &enter_action))
        {
            if (enter_action == 1U)
            {
                Menu_SaveEdit();
                Menu_ShowSavedConfirm();
            }
            return 1U;
        }
        return 0U;
    }

    switch (btn)
    {
    case MENU_BTN_UP:
        Menu_ChangeValue(+1);
        return 1U;
    case MENU_BTN_DOWN:
        Menu_ChangeValue(-1);
        return 1U;
    case MENU_BTN_ENTER:
        Menu_SaveEdit();
        Menu_ShowSavedConfirm();
        return 1U;
    case MENU_BTN_LEFT:
        menu_state = MENU_STATE_BROWSE;
        return 1U;
    default:
        return 0U;
    }
}

static uint8_t Menu_HandleOptionSelect(MenuButton btn)
{
    switch (btn)
    {
    case MENU_BTN_UP:
        Menu_ChangeOption(-1);
        return 1U;
    case MENU_BTN_DOWN:
    case MENU_BTN_RIGHT:
        Menu_ChangeOption(+1);
        return 1U;
    case MENU_BTN_LEFT:
        menu_state = MENU_STATE_BROWSE;
        return 1U;
    case MENU_BTN_ENTER:
        Menu_SaveEdit();
        Menu_ShowSavedConfirm();
        return 1U;
    default:
        return 0U;
    }
}

static uint8_t Menu_HandleOnOffToggle(MenuButton btn)
{
    switch (btn)
    {
    case MENU_BTN_UP:
        edit_i32 = 1;
        return 1U;
    case MENU_BTN_DOWN:
        edit_i32 = 0;
        return 1U;
    case MENU_BTN_ENTER:
        Menu_SaveEdit();
        Menu_ShowSavedConfirm();
        return 1U;
    case MENU_BTN_LEFT:
        menu_state = MENU_STATE_BROWSE;
        return 1U;
    default:
        return 0U;
    }
}

static uint8_t Menu_HandleLiveView(MenuButton btn)
{
    if (btn == MENU_BTN_LEFT)
    {
        menu_state = MENU_STATE_BROWSE;
        return 1U;
    }
    return 0U;
}

static void Menu_TickSaved(void)
{
    if ((HAL_GetTick() - save_confirm_start_ms) >= SAVED_CONFIRM_MS)
    {
        menu_state = MENU_STATE_BROWSE;
        Menu_Render();
    }
}

static void Menu_TickLive(void)
{
    Menu_RenderLiveView();
}

typedef void    (*MenuRenderFn)(void);
typedef uint8_t (*MenuBtnFn)(MenuButton btn);
typedef void    (*MenuTickFn)(void);

typedef struct
{
    MenuRenderFn render;
    MenuBtnFn    on_btn;
    MenuTickFn   tick;
} MenuStateOps;

static const MenuStateOps menu_ops[MENU_STATE_COUNT] = {
    [MENU_STATE_BROWSE]        = { Menu_RenderBrowse,        Menu_HandleBrowse,        NULL },
    [MENU_STATE_EDIT_VALUE]    = { Menu_RenderEditValue,     Menu_HandleEditValue,     NULL },
    [MENU_STATE_OPTION_SELECT] = { Menu_RenderOptionSelect,  Menu_HandleOptionSelect,  NULL },
    [MENU_STATE_ONOFF_TOGGLE]  = { Menu_RenderOnOffToggle,   Menu_HandleOnOffToggle,   NULL },
    [MENU_STATE_CAL_PROMPT]    = { Menu_RenderCalPrompt,     Menu_HandleCalPrompt,     NULL },
    [MENU_STATE_SPAN_WEIGHT]   = { Menu_RenderSpanWeight,    Menu_HandleSpanWeight,    NULL },
    [MENU_STATE_CAL_ACTIVE]    = { Menu_RenderCalActive,     Menu_HandleNone,          Menu_CalTick },
    [MENU_STATE_LIVE_VIEW]     = { Menu_RenderLiveView,      Menu_HandleLiveView,      Menu_TickLive },
    [MENU_STATE_SAVED_CONFIRM] = { Menu_RenderSavedConfirm,  Menu_HandleNone,          Menu_TickSaved },
    [MENU_STATE_LIN_COUNT]     = { Menu_RenderLinCount,      Menu_HandleLinCount,      NULL },
    [MENU_STATE_LIN_WEIGHT]    = { Menu_RenderLinWeight,     Menu_HandleLinWeight,     NULL },
    [MENU_STATE_LIN_PROMPT]    = { Menu_RenderLinPrompt,     Menu_HandleLinPrompt,     NULL },
    [MENU_STATE_RANGE_COUNT]   = { Menu_RenderRangeCount,    Menu_HandleRangeCount,    NULL },
    [MENU_STATE_RANGE_MAX]     = { Menu_RenderRangeMax,      Menu_HandleRangeMax,      NULL },
    [MENU_STATE_RANGE_INC]     = { Menu_RenderRangeInc,      Menu_HandleRangeInc,      NULL },
};

typedef uint8_t (*MenuOpenFn)(const MenuNode *node);

static uint8_t Menu_OpenFolder(const MenuNode *node)
{
    Menu_PushFrame(node->children, node->child_count);
    return 1U;
}

static uint8_t Menu_OpenZero(const MenuNode *node)
{
    (void)node;
    cal_mode = CAL_MODE_ZERO;
    menu_state = MENU_STATE_CAL_PROMPT;
    return 1U;
}

static uint8_t Menu_OpenSpan(const MenuNode *node)
{
    (void)node;
    cal_mode = CAL_MODE_SPAN;
    edit_i32 = Params_GetSpanTestWeight();
    if (edit_i32 <= 0)
    {
        edit_i32 = Params_GetMaxWeight();
    }
    if (edit_i32 <= 0)
    {
        edit_i32 = 1;
    }
    Menu_DigitEditSetLimits(1, 999999);
    Menu_DigitEditResetCursor();
    menu_state = MENU_STATE_SPAN_WEIGHT;
    return 1U;
}

static uint8_t Menu_OpenRange(const MenuNode *node)
{
    (void)node;
    Menu_StartRangeWizard();
    return 1U;
}

static uint8_t Menu_OpenLin(const MenuNode *node)
{
    (void)node;
    Menu_StartLinWizard();
    return 1U;
}

static uint8_t Menu_OpenAction(const MenuNode *node)
{
    if (node->action != NULL)
    {
        node->action();
    }
    Menu_ShowSavedConfirm();
    return 1U;
}

static uint8_t Menu_OpenLive(const MenuNode *node)
{
    (void)node;
    menu_state = MENU_STATE_LIVE_VIEW;
    return 1U;
}

static uint8_t Menu_OpenOption(const MenuNode *node)
{
    (void)node;
    Menu_StartOptionEdit();
    menu_state = MENU_STATE_OPTION_SELECT;
    return 1U;
}

static uint8_t Menu_OpenOnOff(const MenuNode *node)
{
    (void)node;
    Menu_StartOnOffEdit();
    menu_state = MENU_STATE_ONOFF_TOGGLE;
    return 1U;
}

static uint8_t Menu_OpenEdit(const MenuNode *node)
{
    (void)node;
    Menu_StartEdit();
    menu_state = MENU_STATE_EDIT_VALUE;
    return 1U;
}

static const MenuOpenFn open_by_type[] = {
    [PARAM_TYPE_FOLDER]       = Menu_OpenFolder,
    [PARAM_TYPE_I32]          = Menu_OpenEdit,
    [PARAM_TYPE_FLOAT]        = Menu_OpenEdit,
    [PARAM_TYPE_ZERO_CALIB]   = Menu_OpenZero,
    [PARAM_TYPE_SPAN_CALIB]   = Menu_OpenSpan,
    [PARAM_TYPE_OPTION]       = Menu_OpenOption,
    [PARAM_TYPE_ONOFF]        = Menu_OpenOnOff,
    [PARAM_TYPE_ACTION]       = Menu_OpenAction,
    [PARAM_TYPE_LIVE]         = Menu_OpenLive,
    [PARAM_TYPE_LIN_WIZARD]   = Menu_OpenLin,
    [PARAM_TYPE_RANGE_WIZARD] = Menu_OpenRange,
};

static uint8_t Menu_OpenNode(const MenuNode *node)
{
    unsigned idx;

    if (node == NULL)
    {
        return 0U;
    }

    idx = (unsigned)node->type;
    if (idx >= (unsigned)(sizeof(open_by_type) / sizeof(open_by_type[0])) ||
        open_by_type[idx] == NULL)
    {
        return Menu_OpenEdit(node);
    }

    return open_by_type[idx](node);
}

typedef struct
{
    const char *code;
    const char *(*label)(void);
} MenuSuffixMap;

static const char *Menu_SufOut1(void) { return Params_GetOutFnLabel(1); }
static const char *Menu_SufOut2(void) { return Params_GetOutFnLabel(2); }
static const char *Menu_SufOut3(void) { return Params_GetOutFnLabel(3); }
static const char *Menu_SufIn1(void)  { return Params_GetInFnLabel(1); }
static const char *Menu_SufIn2(void)  { return Params_GetInFnLabel(2); }
static const char *Menu_SufAna(void)  { return Params_GetAnaTypeLabel(); }
static const char *Menu_SufRs485(void){ return Params_GetRs485ProtoLabel(); }
static const char *Menu_SufCan(void)  { return Params_GetCanProtoLabel(); }
static const char *Menu_SufEth(void)  { return Params_GetEthProtoLabel(); }

static const MenuSuffixMap browse_suffix[] = {
    { "F5.1", Menu_SufOut1 },
    { "F5.2", Menu_SufOut2 },
    { "F5.3", Menu_SufOut3 },
    { "F6.1", Menu_SufIn1 },
    { "F6.2", Menu_SufIn2 },
    { "F7.1", Menu_SufAna },
    { "F7.2", Menu_SufRs485 },
    { "F7.3", Menu_SufCan },
    { "F7.4", Menu_SufEth },
};

static const char *Menu_BrowseSuffix(const MenuNode *item)
{
    uint8_t i;

    if (item->type == PARAM_TYPE_ONOFF && item->value != NULL)
    {
        return (*(int32_t *)item->value != 0) ? "ON" : "OFF";
    }

    if (item->fcode == NULL)
    {
        return NULL;
    }

    for (i = 0U; i < (uint8_t)(sizeof(browse_suffix) / sizeof(browse_suffix[0])); i++)
    {
        if (strcmp(item->fcode, browse_suffix[i].code) == 0)
        {
            return browse_suffix[i].label();
        }
    }

    return NULL;
}

void Menu_HandleButton(MenuButton btn)
{
    const MenuStateOps *ops;

    if (!menu_active || (unsigned)menu_state >= MENU_STATE_COUNT)
    {
        return;
    }

    ops = &menu_ops[menu_state];
    if (ops->on_btn != NULL && ops->on_btn(btn) != 0U)
    {
        Menu_Render();
    }
}

void Menu_Render(void)
{
    const MenuStateOps *ops;

    if (!menu_active || (unsigned)menu_state >= MENU_STATE_COUNT)
    {
        return;
    }

    ops = &menu_ops[menu_state];
    if (ops->render != NULL)
    {
        ops->render();
    }
}

void Menu_Tick(void)
{
    const MenuStateOps *ops;

    if (!menu_active || (unsigned)menu_state >= MENU_STATE_COUNT)
    {
        return;
    }

    ops = &menu_ops[menu_state];
    if (ops->tick != NULL)
    {
        ops->tick();
    }
}

static void Menu_RenderBrowse(void)
{
    MenuFrame *frame = Menu_CurrentFrame();
    char line[48];
    char header[32];
    uint8_t i;
    uint8_t item_idx;
    uint8_t row_y = 16;

    if (frame == NULL)
    {
        return;
    }

    Display_Clear();
    Menu_GetHeader(header, sizeof(header));
    Menu_DrawLine(0, 0, header);

    i = 0U;
    for (item_idx = frame->window; item_idx < frame->count; item_idx++)
    {
        char mark;
        const char *code;
        const char *suffix = NULL;
        const MenuNode *item;

        if (!Menu_IsItemVisible(&frame->items[item_idx]))
        {
            continue;
        }

        item = &frame->items[item_idx];
        mark = (item_idx == frame->selected) ? '>' : ' ';
        code = item->fcode;

        suffix = Menu_BrowseSuffix(item);

        if ((code != NULL) && (code[0] != '\0'))
        {
            if (suffix != NULL)
            {
                snprintf(line, sizeof(line), "%c %s %s %s", mark, code, item->name, suffix);
            }
            else
            {
                snprintf(line, sizeof(line), "%c %s %s", mark, code, item->name);
            }
        }
        else if (suffix != NULL)
        {
            snprintf(line, sizeof(line), "%c %s %s", mark, item->name, suffix);
        }
        else
        {
            snprintf(line, sizeof(line), "%c %s", mark, item->name);
        }

        Menu_DrawLine(0, row_y, line);
        row_y = (uint8_t)(row_y + 16U);
        i++;
        if (i >= MENU_VISIBLE_ROWS)
        {
            break;
        }
    }

    Display_Update();
}

void Menu_ShowSavedConfirm(void)
{
    menu_state = MENU_STATE_SAVED_CONFIRM;
    save_confirm_start_ms = HAL_GetTick();
}

static void Menu_RenderSavedConfirm(void)
{
    const MenuNode *node = Menu_CurrentNode();
    uint8_t name_scale;
    uint8_t name_extra;

    Display_Clear();

    if (node != NULL)
    {
        if (strlen(node->name) > 10U)
        {
            name_scale = 1U;
            name_extra = 1U;
        }
        else
        {
            name_scale = 2U;
            name_extra = 2U;
        }

        Menu_DrawCentered(node->name, 8, name_scale, name_scale, name_extra);
        Menu_DrawCentered("SAVED", 36, 3, 3, 3);
    }

    Display_Update();
}

static void Menu_RenderOptionSelect(void)
{
    const MenuNode *node = Menu_CurrentNode();
    char title[32];
    char line[32];
    uint8_t i;
    uint8_t idx;
    uint8_t row_y = 16;

    if (node == NULL || node->option_count == 0U)
    {
        return;
    }

    if (edit_i32 < 0 || edit_i32 >= (int32_t)node->option_count)
    {
        edit_i32 = 0;
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

    Menu_NodeTitle(node, title, sizeof(title));
    Menu_DrawLine(0U, OPT_HEADER_Y, title);

    for (i = 0U; i < MENU_VISIBLE_ROWS; i++)
    {
        idx = (uint8_t)(option_window + i);
        if (idx >= node->option_count)
        {
            break;
        }

        snprintf(line, sizeof(line), "%c %s",
                 (idx == (uint8_t)edit_i32) ? '>' : ' ',
                 node->option_labels[idx]);
        Menu_DrawLine(0, row_y, line);
        row_y += 14;
    }

    Menu_DrawLine(0U, OPT_FOOTER_Y, "ENT SAVE");

    Display_Update();
}

static void Menu_RenderOnOffToggle(void)
{
    const MenuNode *node = Menu_CurrentNode();
    char title[32];
    char line[16];
    uint8_t i;
    uint8_t row_y = 16;
    const char * const labels[2] = { "ON", "OFF" };
    uint8_t selected;

    if (node == NULL)
    {
        return;
    }

    edit_i32 = (edit_i32 != 0) ? 1 : 0;
    selected = (uint8_t)edit_i32;

    Display_Clear();
    Menu_NodeTitle(node, title, sizeof(title));
    Menu_DrawLine(0U, OPT_HEADER_Y, title);

    for (i = 0U; i < 2U; i++)
    {
        uint8_t value = (uint8_t)(1U - i);

        snprintf(line, sizeof(line), "%c %s",
                 (value == selected) ? '>' : ' ',
                 labels[i]);
        Menu_DrawLine(0, row_y, line);
        row_y += 14;
    }

    Menu_DrawLine(0U, OPT_FOOTER_Y, "ENT SAVE  LEFT ESC");
    Display_Update();
}

static void Menu_RenderLiveView(void)
{
    const MenuNode *node = Menu_CurrentNode();
    char value_str[20];

    if (node == NULL || node->value == NULL)
    {
        return;
    }

    Display_Clear();
    {
        char title[32];

        Menu_NodeTitle(node, title, sizeof(title));
        Menu_DrawLine(0, 0, title);
    }

    if (strcmp(node->name, "RAW COUNTS") == 0)
    {
        snprintf(value_str, sizeof(value_str), "%ld", (long)Scale_GetFilteredAdc());
    }
    else if (strcmp(node->name, "CELL MV") == 0)
    {
        snprintf(value_str, sizeof(value_str), "%ld", (long)(Scale_GetCellMillivolts() / 100));
    }
    else if (strcmp(node->name, "FIRMWARE") == 0)
    {
        snprintf(value_str, sizeof(value_str), "%s", FIRMWARE_VERSION_STR);
    }
    else if (strcmp(node->name, "TEST INPUTS") == 0)
    {
        uint8_t mask = Inputs_GetLiveMask();
        snprintf(value_str, sizeof(value_str), "IN1 %s IN2 %s",
                 (mask & 1U) ? "CLSD" : "OPEN",
                 (mask & 2U) ? "CLSD" : "OPEN");
    }
    else if (strcmp(node->name, "LIVE OUTPUT") == 0)
    {
        AnalogOut_FormatLive(value_str, sizeof(value_str));
    }
    else
    {
        int32_t shown = Scale_GetDisplayWeight();

        Display_FormatWeight(shown,
                             (uint8_t)Params_GetDecimalPtFor(shown),
                             value_str,
                             sizeof(value_str));
    }

    Display_SetCursor(0, 24);
    Display_WriteStringScaled(value_str, 2, 2, 2);

    Display_SetCursor(0, 52);
    Display_WriteStringScaled("LEFT=BACK", 1, 1, 1);

    Display_Update();
}

static void Menu_RenderEditValue(void)
{
    const MenuNode *node = Menu_CurrentNode();
    char value_str[20];

    if (node == NULL)
    {
        return;
    }

    Display_Clear();
    {
        char title[32];

        Menu_NodeTitle(node, title, sizeof(title));
        Menu_DrawLine(0, 0, title);
    }

    if (node->type == PARAM_TYPE_FLOAT)
    {
        if (node->step <= 0.00005f)
        {
            snprintf(value_str, sizeof(value_str), "%.5f", (double)edit_f32);
        }
        else if (node->step <= 0.0005f)
        {
            snprintf(value_str, sizeof(value_str), "%.4f", (double)edit_f32);
        }
        else
        {
            snprintf(value_str, sizeof(value_str), "%.3f", (double)edit_f32);
        }
    }
    else if (Menu_NodeUsesDigitEdit(node))
    {
        char title[32];

        Menu_NodeTitle(node, title, sizeof(title));
        Menu_RenderDigitEdit(title, edit_i32);
        return;
    }
    else
    {
        snprintf(value_str, sizeof(value_str), "%ld", (long)edit_i32);
    }

    Display_SetCursor(0, 24);
    Display_WriteStringScaled(value_str, 2, 2, 2);

    Display_SetCursor(0, 52);
    Display_WriteStringScaled("ENT SAVE  LEFT ESC", 1, 1, 1);

    Display_Update();
}

static void Menu_StartEdit(void)
{
    const MenuNode *node = Menu_CurrentNode();

    if (node == NULL)
    {
        return;
    }

    if (node->type == PARAM_TYPE_FLOAT)
    {
        edit_f32 = *(float *)node->value;
    }
    else
    {
        edit_i32 = *(int32_t *)node->value;
    }

    if (Menu_NodeUsesDigitEdit(node))
    {
        Menu_DigitEditSetLimits((int32_t)node->min, (int32_t)node->max);
        Menu_DigitEditResetCursor();
        if (strcmp(node->name, "SPAN TEST WT") == 0)
        {
            if (edit_i32 <= 0)
            {
                edit_i32 = Params_GetMaxWeight();
            }
            if (edit_i32 <= 0)
            {
                edit_i32 = 1;
            }
        }
        Menu_DigitEditClamp();
    }
}

static void Menu_StartOptionEdit(void)
{
    const MenuNode *node = Menu_CurrentNode();

    if (node == NULL || node->value == NULL || node->option_count == 0U)
    {
        return;
    }

    edit_i32 = *(int32_t *)node->value;

    if (edit_i32 < 0)
    {
        edit_i32 = 0;
    }

    if (edit_i32 >= (int32_t)node->option_count)
    {
        edit_i32 = (int32_t)node->option_count - 1;
    }

    option_window = 0U;
    if (edit_i32 >= (int32_t)MENU_VISIBLE_ROWS)
    {
        option_window = (uint8_t)edit_i32 - MENU_VISIBLE_ROWS + 1U;
    }
}

static void Menu_StartOnOffEdit(void)
{
    const MenuNode *node = Menu_CurrentNode();

    if (node == NULL || node->value == NULL)
    {
        return;
    }

    edit_i32 = *(int32_t *)node->value;
    edit_i32 = (edit_i32 != 0) ? 1 : 0;
}

static void Menu_ChangeOption(int8_t dir)
{
    const MenuNode *node = Menu_CurrentNode();

    if (node == NULL || node->option_count == 0U)
    {
        return;
    }

    if (dir > 0)
    {
        edit_i32++;
        if (edit_i32 >= (int32_t)node->option_count)
        {
            edit_i32 = 0;
        }
    }
    else
    {
        edit_i32--;
        if (edit_i32 < 0)
        {
            edit_i32 = (int32_t)node->option_count - 1;
        }
    }
}

static void Menu_SaveEdit(void)
{
    const MenuNode *node = Menu_CurrentNode();

    if (node == NULL)
    {
        return;
    }

    if (node->type == PARAM_TYPE_FLOAT)
    {
        *(float *)node->value = edit_f32;
    }
    else
    {
        *(int32_t *)node->value = edit_i32;
    }

    if (node->name != NULL)
    {
        if (strcmp(node->name, "FILTER LEVEL") == 0)
        {
            Params_ApplyFilterLevel(edit_i32);
        }
        else if ((strcmp(node->name, "BANDWIDTH") == 0) ||
                 (strcmp(node->name, "INTENSITY") == 0) ||
                 (strcmp(node->name, "SPIKE REJECT") == 0) ||
                 (strcmp(node->name, "STABLE TIME") == 0))
        {
            Params_MarkFilterCustom();
        }
    }

    Params_Save();
    Menu_SyncVisibleWindow(Menu_CurrentFrame());
}

static void Menu_ChangeValue(int8_t dir)
{
    const MenuNode *node = Menu_CurrentNode();

    if (node == NULL)
    {
        return;
    }

    if (node->type == PARAM_TYPE_FLOAT)
    {
        edit_f32 += ((float)dir * node->step);

        if (edit_f32 < node->min)
        {
            edit_f32 = node->min;
        }

        if (edit_f32 > node->max)
        {
            edit_f32 = node->max;
        }
    }
    else
    {
        edit_i32 += (int32_t)((float)dir * node->step);

        if (edit_i32 < (int32_t)node->min)
        {
            edit_i32 = (int32_t)node->min;
        }

        if (edit_i32 > (int32_t)node->max)
        {
            edit_i32 = (int32_t)node->max;
        }
    }
}

static uint8_t Menu_IsItemVisible(const MenuNode *node)
{
    const char *code;
    int32_t fn;
    int32_t item;

    if (node == NULL)
    {
        return 0U;
    }

    code = node->fcode;
    if (code == NULL)
    {
        return 1U;
    }

    if ((code[0] == 'F') && (code[1] == '5') && (code[2] == '.') &&
        (code[3] >= '1') && (code[3] <= '3') && (code[4] == '.') &&
        (code[5] >= '1') && (code[5] <= '6') && (code[6] == '\0'))
    {
        fn = Params_GetOutFn((int32_t)(code[3] - '0'));
        item = (int32_t)(code[5] - '0');
        if (item == 1)
        {
            return 1U;
        }
        if (item == 6)
        {
            return (fn != 0) ? 1U : 0U;
        }
        if ((item >= 2) && (item <= 5))
        {
            return ((fn == 1) || (fn == 2)) ? 1U : 0U;
        }
    }

    if ((code[0] == 'F') && (code[1] == '7') && (code[2] == '.') &&
        (code[4] == '.') && (code[6] == '\0'))
    {
        item = (int32_t)(code[5] - '0');
        if (code[3] == '2')
        {
            if (item == 1)
            {
                return 1U;
            }
            return (Params_GetRs485Protocol() != 0) ? 1U : 0U;
        }
        if (code[3] == '3')
        {
            if (item == 1)
            {
                return 1U;
            }
            return (Params_GetCanProtocol() != 0) ? 1U : 0U;
        }
        if (code[3] == '4')
        {
            if (item == 1)
            {
                return 1U;
            }
            if (Params_GetEthProtocol() == 0)
            {
                return 0U;
            }
            if ((item == 3) || (item == 4) || (item == 5))
            {
                return (Params_GetEthDhcp() == 0) ? 1U : 0U;
            }
            return 1U;
        }
    }

    return 1U;
}

static uint8_t Menu_NextVisibleIndex(const MenuFrame *frame, uint8_t start, int8_t dir)
{
    uint8_t i = start;
    uint8_t guard = 0U;

    if (frame == NULL || frame->count == 0U)
    {
        return start;
    }

    do
    {
        if (dir > 0)
        {
            i = (uint8_t)((i + 1U) % frame->count);
        }
        else
        {
            i = (i == 0U) ? (uint8_t)(frame->count - 1U) : (uint8_t)(i - 1U);
        }
        guard++;
        if (Menu_IsItemVisible(&frame->items[i]))
        {
            return i;
        }
    } while (guard < frame->count);

    return start;
}

static uint8_t Menu_VisibleRank(const MenuFrame *frame, uint8_t abs_idx)
{
    uint8_t i;
    uint8_t rank = 0U;

    if (frame == NULL)
    {
        return 0U;
    }

    for (i = 0U; (i < abs_idx) && (i < frame->count); i++)
    {
        if (Menu_IsItemVisible(&frame->items[i]))
        {
            rank++;
        }
    }

    return rank;
}

static void Menu_SyncVisibleWindow(MenuFrame *frame)
{
    uint8_t i;
    uint8_t vis_sel;
    uint8_t vis_win;
    uint8_t target;
    uint8_t rank;

    if (frame == NULL || frame->count == 0U)
    {
        return;
    }

    if (frame->selected >= frame->count)
    {
        frame->selected = 0U;
    }

    if (!Menu_IsItemVisible(&frame->items[frame->selected]))
    {
        frame->selected = Menu_NextVisibleIndex(frame, frame->selected, +1);
    }

    if (frame->window >= frame->count)
    {
        frame->window = 0U;
    }

    if (!Menu_IsItemVisible(&frame->items[frame->window]))
    {
        for (i = frame->window; i < frame->count; i++)
        {
            if (Menu_IsItemVisible(&frame->items[i]))
            {
                frame->window = i;
                break;
            }
        }
        if (!Menu_IsItemVisible(&frame->items[frame->window]))
        {
            frame->window = frame->selected;
        }
    }

    vis_sel = Menu_VisibleRank(frame, frame->selected);
    vis_win = Menu_VisibleRank(frame, frame->window);

    if (vis_sel < vis_win)
    {
        frame->window = frame->selected;
        return;
    }

    if (vis_sel >= (uint8_t)(vis_win + MENU_VISIBLE_ROWS))
    {
        target = (uint8_t)(vis_sel - MENU_VISIBLE_ROWS + 1U);
        rank = 0U;
        for (i = 0U; i < frame->count; i++)
        {
            if (!Menu_IsItemVisible(&frame->items[i]))
            {
                continue;
            }
            if (rank == target)
            {
                frame->window = i;
                break;
            }
            rank++;
        }
    }
}

static void Menu_BrowseUp(void)
{
    MenuFrame *frame = Menu_CurrentFrame();

    if (frame == NULL)
    {
        return;
    }

    frame->selected = Menu_NextVisibleIndex(frame, frame->selected, -1);
    Menu_SyncVisibleWindow(frame);
}

static void Menu_BrowseDown(void)
{
    MenuFrame *frame = Menu_CurrentFrame();

    if (frame == NULL)
    {
        return;
    }

    frame->selected = Menu_NextVisibleIndex(frame, frame->selected, +1);
    Menu_SyncVisibleWindow(frame);
}
