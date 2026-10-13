#ifndef COMM_H
#define COMM_H

#include <stddef.h>
#include <stdint.h>

void Comm_Init(void);
void Comm_Tick(void);

void Rs485_Init(void);
void Rs485_Tick(void);
int  Rs485_Write(const uint8_t *data, size_t len);
int  Rs485_Read(uint8_t *data, size_t max_len);

void CanBus_Init(void);
void CanBus_Tick(void);
int  CanBus_Send(uint32_t id, const uint8_t *data, uint8_t len);
int  CanBus_Recv(uint32_t *id, uint8_t *data, uint8_t *len);

void Ethernet_Init(void);
void Ethernet_Tick(void);
uint8_t Ethernet_IsLinkUp(void);
void Ethernet_GetIp(uint8_t ip[4]);

void ModbusRtu_Init(void);
void ModbusRtu_Tick(void);
void AsciiContin_Init(void);
void AsciiContin_Tick(void);
void AsciiCmd_Init(void);
void AsciiCmd_Tick(void);
void Canopen_Init(void);
void Canopen_Tick(void);
void DeviceNet_Init(void);
void DeviceNet_Tick(void);
void ModbusTcp_Init(void);
void ModbusTcp_Tick(void);
void EthernetIp_Init(void);
void EthernetIp_Tick(void);

#endif
