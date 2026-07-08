#include <naf24l01.h>
#include <intrins.h>

sbit CE   = P3^0; /* nRF24L01 CE */
sbit CSN  = P5^5; /* nRF24L01 CSN */
sbit SCK  = P3^2; /* nRF24L01 SCK */
sbit MISO = P3^3; /* nRF24L01 MISO */
sbit MOSI = P5^4; /* nRF24L01 MOSI */

#define CONFIG      0x00
#define EN_AA       0x01
#define EN_RXADDR   0x02
#define SETUP_AW    0x03
#define RF_CH       0x05
#define RF_SETUP    0x06
#define STATUS      0x07
#define RX_ADDR_P0  0x0A
#define RX_PW_P0    0x11

#define RX_DR       0x40

void NRF_IO_Init(void)
{
    /* P3.0 CE and P3.2 SCK: push-pull output. P3.3 MISO: high impedance input. */
    P3M1 &= ~0x05;
    P3M0 |= 0x05;
    P3M1 |= 0x08;
    P3M0 &= ~0x08;

    /* P5.4 MOSI and P5.5 CSN: push-pull output. */
    P5M1 &= ~0x30;
    P5M0 |= 0x30;

    CE = 0;
    CSN = 1;
    SCK = 0;
    MOSI = 0;
}

void Delay_ms(uint8 ms)
{
    uint8 i, j;

    while(ms--)
    {
        for(i = 0; i < 200; i++)
        {
            for(j = 0; j < 20; j++)
            {
                _nop_();
            }
        }
    }
}

uint8 SPI_RW(uint8 dat)
{
    uint8 i;

    for(i = 0; i < 8; i++)
    {
        MOSI = (dat & 0x80);
        dat <<= 1;

        SCK = 1;
        _nop_();

        if(MISO) dat |= 0x01;

        SCK = 0;
        _nop_();
    }

    return dat;
}

void NRF_Write_Reg(uint8 reg, uint8 value)
{
    CSN = 0;
    SPI_RW(reg | NRF_WRITE_REG);
    SPI_RW(value);
    CSN = 1;
}

uint8 NRF_Read_Reg(uint8 reg)
{
    uint8 value;

    CSN = 0;
    SPI_RW(reg);
    value = SPI_RW(NOP);
    CSN = 1;

    return value;
}

void NRF_Write_Buf(uint8 reg, uint8 *buf, uint8 len)
{
    uint8 i;

    CSN = 0;
    SPI_RW(reg | NRF_WRITE_REG);
    for(i = 0; i < len; i++)
    {
        SPI_RW(buf[i]);
    }
    CSN = 1;
}

void NRF_Init(void)
{
    uint8 addr[5] = {'L', 'E', 'D', '0', (uint8)('0' + CLIP_ID)};

    NRF_IO_Init();

    CE = 0;
    CSN = 1;
    SCK = 0;

    NRF_Write_Reg(CONFIG, 0x0F);
    Delay_ms(2);
    NRF_Write_Reg(EN_AA, 0x00);
    NRF_Write_Reg(EN_RXADDR, 0x01);
    NRF_Write_Reg(SETUP_AW, 0x03);
    NRF_Write_Reg(RF_CH, 40);
    NRF_Write_Reg(RF_SETUP, 0x06);

    NRF_Write_Buf(RX_ADDR_P0, addr, 5);
    NRF_Write_Reg(RX_PW_P0, 1);
    NRF_Write_Reg(STATUS, 0x70);

    CSN = 0;
    SPI_RW(FLUSH_RX);
    CSN = 1;

    CE = 1;
}

uint8 NRF_Check(void)
{
    uint8 ok = 1;

    CE = 0;
    Delay_ms(2);

    NRF_Write_Reg(RF_CH, 40);
    if(NRF_Read_Reg(RF_CH) != 40)
    {
        ok = 0;
    }
    else
    {
        NRF_Write_Reg(RF_SETUP, 0x06);
        if(NRF_Read_Reg(RF_SETUP) != 0x06)
        {
            ok = 0;
        }
    }

    CE = 1;
    Delay_ms(2);

    return ok;
}

uint8 NRF_Receive(uint8 *buf)
{
    uint8 status;

    status = NRF_Read_Reg(STATUS);

    if(status & RX_DR)
    {
        CSN = 0;
        SPI_RW(RD_RX_PLOAD);
        buf[0] = SPI_RW(NOP);
        CSN = 1;

        NRF_Write_Reg(STATUS, RX_DR);
        return 1;
    }

    return 0;
}
