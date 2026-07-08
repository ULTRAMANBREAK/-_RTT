#ifndef APPLICATIONS_SERVO_LOCK_H__
#define APPLICATIONS_SERVO_LOCK_H__

#include <rtthread.h>

int servo_lock_init(void);
rt_err_t servo_lock_set_angle(rt_uint16_t angle_deg);
rt_err_t servo_lock_set_pulse_us(rt_uint16_t pulse_us);
rt_err_t servo_lock_set_freq_hz(rt_uint16_t freq_hz);
rt_err_t servo_lock_enable(rt_bool_t enable);

#endif