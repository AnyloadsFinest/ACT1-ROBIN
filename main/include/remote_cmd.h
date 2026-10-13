#ifndef REMOTE_CMD_H
#define REMOTE_CMD_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    REMOTE_OK = 0,
    REMOTE_ERR,
    REMOTE_DENIED
} RemoteStatus;

RemoteStatus RemoteCmd_Zero(void);
RemoteStatus RemoteCmd_Tare(void);
RemoteStatus RemoteCmd_ClearTare(void);
RemoteStatus RemoteCmd_SetNet(uint8_t net);
RemoteStatus RemoteCmd_SetPresetTare(int32_t value);
RemoteStatus RemoteCmd_ResetPeak(void);
RemoteStatus RemoteCmd_HoldPeak(uint8_t hold);

int32_t RemoteCmd_GetGross(void);
int32_t RemoteCmd_GetNet(void);
int32_t RemoteCmd_GetTare(void);
int32_t RemoteCmd_GetDisplay(void);
uint8_t RemoteCmd_GetStatus(void);
void    RemoteCmd_FormatWeight(int32_t weight, char *buf, size_t buf_size);

#endif
