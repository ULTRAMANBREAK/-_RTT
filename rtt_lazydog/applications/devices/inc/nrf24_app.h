#ifndef APPLICATIONS_NRF24_APP_H__
#define APPLICATIONS_NRF24_APP_H__

#include <rtthread.h>

int nrf24_app_init(void);
rt_err_t nrf24_clip_send_cmd(rt_uint8_t cmd);
rt_err_t nrf24_clip_send_cmd_to(rt_uint8_t card_num, rt_uint8_t cmd);

#endif
