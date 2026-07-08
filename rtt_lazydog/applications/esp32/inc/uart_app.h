/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APPLICATIONS_UART_APP_H__
#define APPLICATIONS_UART_APP_H__

#include <rtthread.h>

#define UART_APP_RFID_UID_LEN 4
#define UART_APP_TEMP_CHANNELS 4

int uart_app_init(void);
rt_err_t uart_app_send_line(const char *line);
rt_err_t uart_app_send_rfid_uid(const rt_uint8_t uid[UART_APP_RFID_UID_LEN]);
rt_err_t uart_app_send_nfc(const rt_uint8_t uid[UART_APP_RFID_UID_LEN], rt_bool_t unbind);
rt_err_t uart_app_send_fall(void);
rt_err_t uart_app_send_theft(void);
rt_err_t uart_app_send_unlock(void);
void uart_app_update_temp(rt_bool_t valid, rt_int32_t temperature_x10);
void uart_app_update_temps(const rt_bool_t valid[UART_APP_TEMP_CHANNELS],
                           const rt_int32_t temperature_x10[UART_APP_TEMP_CHANNELS]);

#endif
