#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "app_config.h"
#include "servo_lock.h"

#define DBG_TAG "servo.lock"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

static struct rt_mutex servo_lock_mutex;
static rt_bool_t servo_lock_mutex_ready = RT_FALSE;
static rt_bool_t servo_lock_enabled = RT_FALSE;
static rt_uint16_t servo_lock_pulse_us = APP_SERVO_LOCK_NEUTRAL_US;
static rt_uint16_t servo_lock_freq_hz = APP_SERVO_LOCK_DEFAULT_FREQ_HZ;

static rt_uint16_t servo_lock_clamp_u16(rt_uint16_t value, rt_uint16_t min_value, rt_uint16_t max_value)
{
    if (value < min_value)
    {
        return min_value;
    }
    if (value > max_value)
    {
        return max_value;
    }
    return value;
}

static rt_uint16_t servo_lock_angle_to_pulse(rt_uint16_t angle_deg)
{
    rt_uint32_t span;
    rt_uint32_t pulse;

    angle_deg = servo_lock_clamp_u16(angle_deg, APP_SERVO_LOCK_MIN_ANGLE_DEG, APP_SERVO_LOCK_MAX_ANGLE_DEG);
    span = APP_SERVO_LOCK_MAX_US - APP_SERVO_LOCK_MIN_US;
    pulse = APP_SERVO_LOCK_MIN_US + ((span * angle_deg) / APP_SERVO_LOCK_MAX_ANGLE_DEG);

    return (rt_uint16_t)pulse;
}

static rt_uint32_t servo_lock_period_us(rt_uint16_t freq_hz)
{
    if (freq_hz == 0U)
    {
        freq_hz = APP_SERVO_LOCK_DEFAULT_FREQ_HZ;
    }
    return 1000000UL / freq_hz;
}

static void servo_lock_delay_us(rt_uint32_t delay_us)
{
    rt_uint32_t delay_ms = delay_us / 1000U;
    rt_uint32_t remain_us = delay_us % 1000U;

    if (delay_ms > 0U)
    {
        rt_thread_mdelay(delay_ms);
    }
    if (remain_us > 0U)
    {
        rt_hw_us_delay(remain_us);
    }
}

rt_err_t servo_lock_set_pulse_us(rt_uint16_t pulse_us)
{
    pulse_us = servo_lock_clamp_u16(pulse_us, APP_SERVO_LOCK_MIN_US, APP_SERVO_LOCK_MAX_US);

    if (servo_lock_mutex_ready == RT_TRUE)
    {
        rt_mutex_take(&servo_lock_mutex, RT_WAITING_FOREVER);
    }
    servo_lock_pulse_us = pulse_us;
    servo_lock_enabled = RT_TRUE;
    if (servo_lock_mutex_ready == RT_TRUE)
    {
        rt_mutex_release(&servo_lock_mutex);
    }

    return RT_EOK;
}

rt_err_t servo_lock_set_angle(rt_uint16_t angle_deg)
{
    return servo_lock_set_pulse_us(servo_lock_angle_to_pulse(angle_deg));
}

rt_err_t servo_lock_set_freq_hz(rt_uint16_t freq_hz)
{
    freq_hz = servo_lock_clamp_u16(freq_hz, APP_SERVO_LOCK_MIN_FREQ_HZ, APP_SERVO_LOCK_MAX_FREQ_HZ);

    if (servo_lock_mutex_ready == RT_TRUE)
    {
        rt_mutex_take(&servo_lock_mutex, RT_WAITING_FOREVER);
    }
    servo_lock_freq_hz = freq_hz;
    if (servo_lock_mutex_ready == RT_TRUE)
    {
        rt_mutex_release(&servo_lock_mutex);
    }

    return RT_EOK;
}

rt_err_t servo_lock_enable(rt_bool_t enable)
{
    if (servo_lock_mutex_ready == RT_TRUE)
    {
        rt_mutex_take(&servo_lock_mutex, RT_WAITING_FOREVER);
    }
    servo_lock_enabled = enable ? RT_TRUE : RT_FALSE;
    if (servo_lock_enabled == RT_FALSE)
    {
        rt_pin_write(APP_SERVO_LOCK_PIN, PIN_LOW);
    }
    if (servo_lock_mutex_ready == RT_TRUE)
    {
        rt_mutex_release(&servo_lock_mutex);
    }

    return RT_EOK;
}

static void servo_lock_snapshot(rt_bool_t *enabled, rt_uint16_t *pulse_us, rt_uint16_t *freq_hz)
{
    if (servo_lock_mutex_ready == RT_TRUE)
    {
        rt_mutex_take(&servo_lock_mutex, RT_WAITING_FOREVER);
    }
    *enabled = servo_lock_enabled;
    *pulse_us = servo_lock_pulse_us;
    *freq_hz = servo_lock_freq_hz;
    if (servo_lock_mutex_ready == RT_TRUE)
    {
        rt_mutex_release(&servo_lock_mutex);
    }
}

static void servo_lock_thread_entry(void *parameter)
{
    RT_UNUSED(parameter);

    while (1)
    {
        rt_bool_t enabled;
        rt_uint16_t pulse_us;
        rt_uint16_t freq_hz;
        rt_uint32_t period_us;

        servo_lock_snapshot(&enabled, &pulse_us, &freq_hz);
        if (enabled == RT_FALSE)
        {
            rt_pin_write(APP_SERVO_LOCK_PIN, PIN_LOW);
            rt_thread_mdelay(20);
            continue;
        }

        period_us = servo_lock_period_us(freq_hz);
        if (pulse_us >= period_us)
        {
            pulse_us = (rt_uint16_t)(period_us - 100U);
        }

        rt_pin_write(APP_SERVO_LOCK_PIN, PIN_HIGH);
        rt_hw_us_delay(pulse_us);
        rt_pin_write(APP_SERVO_LOCK_PIN, PIN_LOW);
        servo_lock_delay_us(period_us - pulse_us);
    }
}

static void servo_lock_print_usage(void)
{
    rt_kprintf("usage: servo_lock <on|off|open|close|angle 0-180|pulse 500-2500|freq 20-100|status>\r\n");
}

static void servo_lock_print_status(void)
{
    rt_bool_t enabled;
    rt_uint16_t pulse_us;
    rt_uint16_t freq_hz;

    servo_lock_snapshot(&enabled, &pulse_us, &freq_hz);
    rt_kprintf("servo_lock pin=PB2 enabled=%u freq=%uHz pulse=%uus period=%luus\r\n",
               enabled ? 1U : 0U,
               freq_hz,
               pulse_us,
               servo_lock_period_us(freq_hz));
}

static int servo_lock(int argc, char *argv[])
{
    long value;

    if (argc < 2)
    {
        servo_lock_print_usage();
        return -RT_EINVAL;
    }

    if (strcmp(argv[1], "on") == 0)
    {
        servo_lock_enable(RT_TRUE);
    }
    else if (strcmp(argv[1], "off") == 0)
    {
        servo_lock_enable(RT_FALSE);
    }
    else if (strcmp(argv[1], "open") == 0)
    {
        servo_lock_set_angle(APP_SERVO_LOCK_OPEN_ANGLE_DEG);
    }
    else if (strcmp(argv[1], "close") == 0)
    {
        servo_lock_set_angle(APP_SERVO_LOCK_CLOSE_ANGLE_DEG);
    }
    else if (strcmp(argv[1], "angle") == 0)
    {
        if (argc < 3)
        {
            servo_lock_print_usage();
            return -RT_EINVAL;
        }
        value = strtol(argv[2], RT_NULL, 0);
        if (value < APP_SERVO_LOCK_MIN_ANGLE_DEG || value > APP_SERVO_LOCK_MAX_ANGLE_DEG)
        {
            servo_lock_print_usage();
            return -RT_EINVAL;
        }
        servo_lock_set_angle((rt_uint16_t)value);
    }
    else if (strcmp(argv[1], "pulse") == 0)
    {
        if (argc < 3)
        {
            servo_lock_print_usage();
            return -RT_EINVAL;
        }
        value = strtol(argv[2], RT_NULL, 0);
        if (value < APP_SERVO_LOCK_MIN_US || value > APP_SERVO_LOCK_MAX_US)
        {
            servo_lock_print_usage();
            return -RT_EINVAL;
        }
        servo_lock_set_pulse_us((rt_uint16_t)value);
    }
    else if (strcmp(argv[1], "freq") == 0)
    {
        if (argc < 3)
        {
            servo_lock_print_usage();
            return -RT_EINVAL;
        }
        value = strtol(argv[2], RT_NULL, 0);
        if (value < APP_SERVO_LOCK_MIN_FREQ_HZ || value > APP_SERVO_LOCK_MAX_FREQ_HZ)
        {
            servo_lock_print_usage();
            return -RT_EINVAL;
        }
        servo_lock_set_freq_hz((rt_uint16_t)value);
    }
    else if (strcmp(argv[1], "status") != 0)
    {
        servo_lock_print_usage();
        return -RT_EINVAL;
    }

    servo_lock_print_status();
    return RT_EOK;
}
MSH_CMD_EXPORT(servo_lock, control PB2 software PWM servo lock);

int servo_lock_init(void)
{
    rt_thread_t thread;
    rt_err_t result;

    rt_pin_mode(APP_SERVO_LOCK_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(APP_SERVO_LOCK_PIN, PIN_LOW);

    if (servo_lock_mutex_ready == RT_FALSE)
    {
        result = rt_mutex_init(&servo_lock_mutex, "svlock", RT_IPC_FLAG_FIFO);
        if (result != RT_EOK)
        {
            LOG_E("servo lock mutex init failed");
            return result;
        }
        servo_lock_mutex_ready = RT_TRUE;
    }

    thread = rt_thread_create("servo_lock",
                              servo_lock_thread_entry,
                              RT_NULL,
                              APP_SERVO_LOCK_THREAD_STACK_SIZE,
                              APP_SERVO_LOCK_THREAD_PRIORITY,
                              APP_SERVO_LOCK_THREAD_TICK);
    if (thread == RT_NULL)
    {
        LOG_E("servo lock thread create failed");
        return -RT_ERROR;
    }

    rt_thread_startup(thread);
    return RT_EOK;
}