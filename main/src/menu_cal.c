#include "menu_priv.h"
#include "esp_log.h"

static const char *TAG = "menu_cal";
static uint8_t cal_last_failed;
static uint8_t cal_capture_done;

void Menu_RenderCalPrompt(void)
{
    const MenuNode *node = Menu_CurrentNode();

    Display_Clear();

    if (lin_wiz_active)
    {
        char hdr[20];

        snprintf(hdr, sizeof(hdr), "POINT %u OF %ld",
                 (unsigned)(lin_wiz_point + 1U),
                 (long)Params_GetLinPointCount());
        Menu_DrawLine(0, 0, hdr);
    }
    else if (node != NULL)
    {
        char title[32];

        Menu_NodeTitle(node, title, sizeof(title));
        Menu_DrawLine(0, 0, title);
    }

    Display_SetCursor(0, 20);
    if (cal_mode == CAL_MODE_ZERO)
    {
        Display_WriteStringScaled("EMPTY SCALE", 1, 1, 1);
    }
    else if (lin_wiz_active)
    {
        Display_WriteStringScaled("PLACE TEST WT", 1, 1, 1);
    }
    else if (cal_mode == CAL_MODE_SPAN)
    {
        char load[24];
        int32_t tw = Params_GetSpanTestWeight();

        if (tw <= 0)
        {
            tw = Params_GetMaxWeight();
        }
        snprintf(load, sizeof(load), "PLACE %ld g", (long)tw);
        Display_WriteStringScaled(load, 1, 1, 1);
    }
    else
    {
        Display_WriteStringScaled("PLACE TEST WT", 1, 1, 1);
    }

    Display_SetCursor(0, 34);
    Display_WriteStringScaled("THEN PRESS ENTER", 1, 1, 1);
    Display_SetCursor(0, 52);
    Display_WriteStringScaled("LEFT=CANCEL", 1, 1, 1);
    Display_Update();
}

void Menu_RenderCalActive(void)
{
    char line[8];
    const char *title;

    Display_Clear();

    if (lin_wiz_active)
    {
        title = "POINT CAL";
    }
    else
    {
        title = (cal_mode == CAL_MODE_ZERO) ? "ZERO CAL" : "SPAN CAL";
    }

    if (cal_phase == ZERO_PHASE_COUNTDOWN)
    {
        Display_SetCursor(0, 0);
        Display_WriteStringScaled(title, 1, 1, 1);
        line[0] = (char)('0' + cal_countdown);
        line[1] = '\0';
        Display_SetCursor(52, 22);
        Display_WriteStringScaled(line, 3, 3, 3);
    }
    else if (cal_phase == ZERO_PHASE_PROGRESS)
    {
        Display_SetCursor(0, 0);
        Display_WriteStringScaled("CALIBRATING", 1, 1, 1);
        Display_DrawProgressBar(8, 40, 112, 10, cal_progress);
        snprintf(line, sizeof(line), "%u%%", (unsigned)cal_progress);
        Display_SetCursor(52, 24);
        Display_WriteStringScaled(line, 1, 1, 1);
    }
    else
    {
        Menu_DrawCentered(title, 8, 2, 2, 2);
        if (cal_last_failed != 0U)
        {
            const char *why = Scale_GetLastCalFailMsg();

            Menu_DrawCentered("FAILED", 28, 2, 2, 2);
            if (why != NULL && why[0] != '\0')
            {
                Menu_DrawCentered(why, 44, 1, 1, 1);
            }
        }
        else
        {
            Menu_DrawCentered("SAVED", 36, 3, 3, 3);
        }
    }

    Display_Update();
}

void Menu_StartCal(void)
{
    menu_state = MENU_STATE_CAL_ACTIVE;
    cal_phase = ZERO_PHASE_COUNTDOWN;
    cal_countdown = ZERO_COUNTDOWN_START;
    cal_progress = 0;
    cal_phase_start_ms = HAL_GetTick();
    cal_last_failed = 0U;
    cal_capture_done = 0U;
    Scale_ResetFilterForCal();
    Menu_Render();
}

uint8_t Menu_HandleCalPrompt(MenuButton btn)
{
    if (btn == MENU_BTN_ENTER)
    {
        Menu_StartCal();
        return 0U;
    }

    if (btn == MENU_BTN_LEFT)
    {
        menu_state = lin_wiz_active ? MENU_STATE_LIN_WEIGHT : MENU_STATE_BROWSE;
        return 1U;
    }

    return 0U;
}

void Menu_CalTick(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t elapsed = now - cal_phase_start_ms;
    uint8_t next_countdown;
    uint8_t next_progress;

    if (cal_phase == ZERO_PHASE_COUNTDOWN)
    {
        if (elapsed >= (ZERO_COUNTDOWN_START * ZERO_COUNTDOWN_STEP_MS))
        {
            cal_phase = ZERO_PHASE_PROGRESS;
            cal_progress = 0;
            cal_phase_start_ms = now;
            Menu_Render();
            return;
        }

        next_countdown = ZERO_COUNTDOWN_START - (uint8_t)(elapsed / ZERO_COUNTDOWN_STEP_MS);
        if (next_countdown == 0U)
        {
            next_countdown = 1U;
        }

        if (next_countdown != cal_countdown)
        {
            cal_countdown = next_countdown;
            Menu_Render();
        }
    }
    else if (cal_phase == ZERO_PHASE_PROGRESS)
    {
        {
            uint32_t progress_ms = ZERO_PROGRESS_MS;

            if (cal_mode == CAL_MODE_SPAN)
            {
                progress_ms = 5000U;
            }
            next_progress = (uint8_t)((elapsed * 100U) / progress_ms);
        }
        if (next_progress > 100U)
        {
            next_progress = 100U;
        }

        if (next_progress != cal_progress)
        {
            cal_progress = next_progress;
            Menu_Render();
        }

        if (cal_progress >= 100U && cal_capture_done == 0U)
        {
            cal_capture_done = 1U;
            cal_last_failed = 0U;

            if (cal_mode == CAL_MODE_ZERO)
            {
                if (Scale_CaptureZero() == 0U)
                {
                    cal_last_failed = 1U;
                }
            }
            else if (lin_wiz_active)
            {
                if (Scale_CaptureAdc(Params_LinCountsPtr((int32_t)lin_wiz_point)) == 0U)
                {
                    cal_last_failed = 1U;
                }
            }
            else if (cal_mode == CAL_MODE_SPAN)
            {
                if (Scale_CaptureSpan() == 0U)
                {
                    cal_last_failed = 1U;
                }
            }
            else
            {
                const MenuNode *node = Menu_CurrentNode();

                if (node != NULL && node->value != NULL)
                {
                    if (Scale_CaptureAdc((int32_t *)node->value) == 0U)
                    {
                        cal_last_failed = 1U;
                    }
                }
            }

            if (cal_last_failed != 0U)
            {
                ESP_LOGE(TAG, "cal capture failed (check load / wiring)");
            }

            cal_phase = ZERO_PHASE_COMPLETE;
            cal_phase_start_ms = now;
            Menu_Render();
        }
    }
    else
    {
        uint32_t hold_ms = (cal_last_failed != 0U) ? ZERO_FAIL_COMPLETE_MS : ZERO_COMPLETE_MS;

        if (elapsed < hold_ms)
        {
            return;
        }

        if (lin_wiz_active)
        {
            lin_wiz_point++;
            if (lin_wiz_point >= (uint8_t)Params_GetLinPointCount())
            {
                Menu_LinFinish();
            }
            else
            {
                Menu_LinNextPoint();
            }
        }
        else
        {
            menu_state = MENU_STATE_BROWSE;
        }
        Menu_Render();
    }
}
