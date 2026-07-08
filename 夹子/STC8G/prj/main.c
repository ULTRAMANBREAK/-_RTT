#include "STC8G.h"
#include <naf24l01.h>

#define uint8 unsigned char
#define uint16 unsigned int

sbit LED = P3^1;

#define CLIP_LED_CMD_OFF       0x00
#define CLIP_LED_CMD_ON        0x01
#define CLIP_LED_CMD_PULSE_1S  0x02
#define CLIP_LED_CMD_BLINK     0x03
#define CLIP_LED_CMD_TOGGLE    0x04

#define LED_TASK_PERIOD_MS       10
#define LED_BLINK_INTERVAL_TICKS 25  /* 250 ms */
#define LED_PULSE_1S_TICKS       100 /* 1000 ms */

typedef enum
{
    LED_MODE_OFF = 0,
    LED_MODE_ON,
    LED_MODE_PULSE,
    LED_MODE_BLINK
} led_mode_t;

static bit led_state = 0;
static led_mode_t led_mode = LED_MODE_OFF;
static uint16 led_tick = 0;

void LED_IO_Init(void)
{
    /* P3.1 LED: push-pull output. */
    P3M1 &= ~0x02;
    P3M0 |= 0x02;
}

void LED_Set(bit on)
{
    LED = on ? 1 : 0;
    led_state = on;
}

void LED_Toggle(void)
{
    LED_Set(!led_state);
}

void LED_Blink(uint8 times)
{
    uint8 i;

    for(i = 0; i < times; i++)
    {
        LED_Set(1);
        Delay_ms(120);
        LED_Set(0);
        Delay_ms(120);
    }
    Delay_ms(200);
    Delay_ms(200);
}

void LED_Exec_Command(uint8 cmd)
{
    switch(cmd)
    {
    case CLIP_LED_CMD_OFF:
        led_mode = LED_MODE_OFF;
        led_tick = 0;
        LED_Set(0);
        break;

    case CLIP_LED_CMD_ON:
        led_mode = LED_MODE_ON;
        led_tick = 0;
        LED_Set(1);
        break;

    case CLIP_LED_CMD_PULSE_1S:
        led_mode = LED_MODE_PULSE;
        led_tick = LED_PULSE_1S_TICKS;
        LED_Set(1);
        break;

    case CLIP_LED_CMD_BLINK:
        led_mode = LED_MODE_BLINK;
        led_tick = 0;
        LED_Set(1);
        break;

    case CLIP_LED_CMD_TOGGLE:
        led_mode = LED_MODE_ON;
        led_tick = 0;
        LED_Toggle();
        if(!led_state)
        {
            led_mode = LED_MODE_OFF;
        }
        break;

    default:
        break;
    }
}

void LED_Task(void)
{
    switch(led_mode)
    {
    case LED_MODE_PULSE:
        if(led_tick > 0)
        {
            led_tick--;
        }
        else
        {
            led_mode = LED_MODE_OFF;
            LED_Set(0);
        }
        break;

    case LED_MODE_BLINK:
        led_tick++;
        if(led_tick >= LED_BLINK_INTERVAL_TICKS)
        {
            led_tick = 0;
            LED_Toggle();
        }
        break;

    case LED_MODE_ON:
        if(!led_state)
        {
            LED_Set(1);
        }
        break;

    case LED_MODE_OFF:
    default:
        if(led_state)
        {
            LED_Set(0);
        }
        break;
    }
}

void main()
{
    uint8 buf[1];

    LED_IO_Init();
    NRF_IO_Init();

    LED_Set(0);
    LED_Blink(2); /* boot OK: MCU is running and LED pin can be controlled */

    NRF_Init();
    if(NRF_Check())
    {
        LED_Blink(3); /* SPI OK and NRF address is ready */
    }
    else
    {
        while(1)
        {
            LED_Blink(1); /* SPI fail: check NRF wiring, IO mode and power */
            Delay_ms(200);
            Delay_ms(200);
            Delay_ms(200);
        }
    }

    LED_Exec_Command(CLIP_LED_CMD_OFF);

    while(1)
    {
        if(NRF_Receive(buf))
        {
            LED_Exec_Command(buf[0]);
        }

        LED_Task();
        Delay_ms(LED_TASK_PERIOD_MS);
    }
}