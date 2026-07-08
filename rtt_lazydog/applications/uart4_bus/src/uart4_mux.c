#include <rtthread.h>
#include <rtdevice.h>

#include "uart4_app.h"
#include "uart4_mux.h"

void uart4_mux_gpio_init(void)
{
    rt_pin_mode(UART4_APP_MUX_PIN_A, PIN_MODE_OUTPUT);
    rt_pin_mode(UART4_APP_MUX_PIN_B, PIN_MODE_OUTPUT);
}

void uart4_mux_select(rt_uint8_t channel)
{
    rt_uint8_t a_level;
    rt_uint8_t b_level;

    if (channel >= UART4_APP_MUX_CHANNELS)
    {
        channel = 0;
    }

    a_level = (channel & 0x01U) ? PIN_HIGH : PIN_LOW;
    b_level = (channel & 0x02U) ? PIN_HIGH : PIN_LOW;

    rt_pin_write(UART4_APP_MUX_PIN_A, a_level);
    rt_pin_write(UART4_APP_MUX_PIN_B, b_level);
    rt_thread_mdelay(UART4_APP_MUX_SETTLE_MS);
}
