#ifndef APPLICATIONS_UART4_MUX_H__
#define APPLICATIONS_UART4_MUX_H__

#include <rtthread.h>

void uart4_mux_gpio_init(void);
void uart4_mux_select(rt_uint8_t channel);

#endif
