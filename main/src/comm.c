#include "comm.h"
#include "params.h"
#include "remote_cmd.h"

void Ethernet_Init(void)
{
    /* W5500 SPI. DHCP vs static from Params_GetEthDhcp / GetEthIp. */
}

void Ethernet_Tick(void)
{
}

uint8_t Ethernet_IsLinkUp(void)
{
    return 0U;
}

void Ethernet_GetIp(uint8_t ip[4])
{
    if (ip == NULL)
    {
        return;
    }

    Params_GetEthIp(ip);
}

void ModbusRtu_Init(void)
{
    /* Slave address Params_GetSerAddr(). Map holding regs to RemoteCmd_*. */
}

void ModbusRtu_Tick(void)
{
    /* Parse RTU frames from Rs485_Read, reply with Rs485_Write. */
}

void AsciiContin_Init(void)
{
}

void AsciiContin_Tick(void)
{
    /* Stream gross/net/status at Params_GetSerUpdate() tenths. */
}

void AsciiCmd_Init(void)
{
}

void AsciiCmd_Tick(void)
{
    /* Command/response: zero, tare, read weight — call RemoteCmd_*. */
}

void Canopen_Init(void)
{
    /* Node ID Params_GetCanNode(). PDOs for weight and status. */
}

void Canopen_Tick(void)
{
}

void DeviceNet_Init(void)
{
    /* MAC ID / node from Params_GetCanNode(). */
}

void DeviceNet_Tick(void)
{
}

void ModbusTcp_Init(void)
{
    /* Listen on Params_GetEthPort() (default 502). Same register map as RTU. */
}

void ModbusTcp_Tick(void)
{
}

void EthernetIp_Init(void)
{
    /* Implicit assembly: weight, status, tare. Explicit: RemoteCmd_*. */
}

void EthernetIp_Tick(void)
{
}

void Comm_Init(void)
{
    Rs485_Init();
    CanBus_Init();
    Ethernet_Init();

    switch (Params_GetRs485Protocol())
    {
    case 1:
        ModbusRtu_Init();
        break;
    case 2:
        AsciiContin_Init();
        break;
    case 3:
        AsciiCmd_Init();
        break;
    default:
        break;
    }

    switch (Params_GetCanProtocol())
    {
    case 1:
        Canopen_Init();
        break;
    case 2:
        DeviceNet_Init();
        break;
    default:
        break;
    }

    switch (Params_GetEthProtocol())
    {
    case 1:
        ModbusTcp_Init();
        break;
    case 2:
        EthernetIp_Init();
        break;
    default:
        break;
    }
}

void Comm_Tick(void)
{
    Rs485_Tick();
    CanBus_Tick();
    Ethernet_Tick();

    switch (Params_GetRs485Protocol())
    {
    case 1:
        ModbusRtu_Tick();
        break;
    case 2:
        AsciiContin_Tick();
        break;
    case 3:
        AsciiCmd_Tick();
        break;
    default:
        break;
    }

    switch (Params_GetCanProtocol())
    {
    case 1:
        Canopen_Tick();
        break;
    case 2:
        DeviceNet_Tick();
        break;
    default:
        break;
    }

    switch (Params_GetEthProtocol())
    {
    case 1:
        ModbusTcp_Tick();
        break;
    case 2:
        EthernetIp_Tick();
        break;
    default:
        break;
    }
}
