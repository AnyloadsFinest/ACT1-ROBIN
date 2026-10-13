#include "remote_cmd.h"
#include "params.h"
#include "scale.h"
#include "display.h"

static uint8_t s_input_peak_hold = 0U;

RemoteStatus RemoteCmd_Zero(void)
{
    Scale_DoZero();
    return REMOTE_OK;
}

RemoteStatus RemoteCmd_Tare(void)
{
    Scale_DoTare();
    return REMOTE_OK;
}

RemoteStatus RemoteCmd_ClearTare(void)
{
    Scale_ClearTare();
    return REMOTE_OK;
}

RemoteStatus RemoteCmd_SetNet(uint8_t net)
{
    Params_SetNetMode(net != 0U ? 1 : 0);
    return REMOTE_OK;
}

RemoteStatus RemoteCmd_SetPresetTare(int32_t value)
{
    Params_SetPresetTare(value);
    return REMOTE_OK;
}

RemoteStatus RemoteCmd_ResetPeak(void)
{
    Scale_ResetPeak();
    return REMOTE_OK;
}

RemoteStatus RemoteCmd_HoldPeak(uint8_t hold)
{
    s_input_peak_hold = (hold != 0U) ? 1U : 0U;
    Scale_SetInputPeakHold(s_input_peak_hold);
    return REMOTE_OK;
}

int32_t RemoteCmd_GetGross(void)
{
    return Scale_GetGrossWeight();
}

int32_t RemoteCmd_GetNet(void)
{
    return Scale_GetNetWeight();
}

int32_t RemoteCmd_GetTare(void)
{
    return Scale_GetTareWeight();
}

int32_t RemoteCmd_GetDisplay(void)
{
    return Scale_GetDisplayWeight();
}

uint8_t RemoteCmd_GetStatus(void)
{
    uint8_t bits = 0U;

    if (Scale_IsStable())
    {
        bits |= 0x01U;
    }
    if (Scale_IsAtZero())
    {
        bits |= 0x02U;
    }
    if (Scale_IsNet())
    {
        bits |= 0x04U;
    }
    if (Scale_IsOverload())
    {
        bits |= 0x08U;
    }
    return bits;
}

void RemoteCmd_FormatWeight(int32_t weight, char *buf, size_t buf_size)
{
    if (buf == NULL || buf_size == 0U)
    {
        return;
    }

    Display_FormatWeight(weight,
                         (uint8_t)Params_GetDecimalPtFor(weight),
                         buf,
                         (uint8_t)buf_size);
}
