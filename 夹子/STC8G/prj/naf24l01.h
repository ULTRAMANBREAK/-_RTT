#include "STC8G.h"

#define NRF_READ_REG    0x00
#define NRF_WRITE_REG   0x20
#define RD_RX_PLOAD     0x61
#define FLUSH_RX        0xE2
#define NOP             0xFF
#define uint8 unsigned char

#ifndef CLIP_ID
#define CLIP_ID 1
#endif

void NRF_Write_Reg(uint8 reg, uint8 value);
uint8 NRF_Read_Reg(uint8 reg);
void NRF_Write_Buf(uint8 reg, uint8 *buf, uint8 len);
void NRF_IO_Init(void);
void NRF_Init(void);
uint8 NRF_Check(void);
uint8 NRF_Receive(uint8 *buf);
void Delay_ms(uint8 ms);
