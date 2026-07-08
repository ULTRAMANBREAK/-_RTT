#ifndef APPLICATIONS_ATGM336H_H__
#define APPLICATIONS_ATGM336H_H__

#define ATGM336H_DEVICE_NAME        "uart2"
#define ATGM336H_THREAD_NAME        "atgm336h"
#define ATGM336H_THREAD_STACK_SIZE  2048
#define ATGM336H_THREAD_PRIORITY    13
#define ATGM336H_THREAD_TICK        100
#define ATGM336H_RX_BUF_SIZE        128
#define ATGM336H_LINE_BUF_SIZE      160

#include <rtthread.h>

int atgm336h_init(void);
int atgm336h_speed_status(void);
rt_err_t atgm336h_get_speed_kmh_x100(long *speed_kmh_x100);

#endif
