/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "atgm336h.h"

#define DBG_TAG "atgm336h"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#define log_g(...)                  LOG_I(__VA_ARGS__)

static rt_device_t atgm_dev = RT_NULL;
static struct rt_semaphore atgm_rx_sem;
static char atgm_line_buf[ATGM336H_LINE_BUF_SIZE];
static rt_size_t atgm_line_len = 0;
static rt_bool_t atgm_rx_seen = RT_FALSE;
static rt_bool_t atgm_speed_valid = RT_FALSE;
static long atgm_last_speed_kmh_x100 = 0;
static long atgm_last_nonzero_speed_kmh_x100 = 0;
static rt_tick_t atgm_last_nonzero_speed_tick = 0;
static char atgm_last_speed_knots[16] = {0};
static char atgm_last_course[16] = {0};
static char atgm_last_time[16] = {0};
static char atgm_last_date[16] = {0};

static void atgm_copy_field(char *dst, rt_size_t dst_size, const char *src)
{
    if (dst == RT_NULL || dst_size == 0)
    {
        return;
    }

    if (src == RT_NULL)
    {
        src = "";
    }

    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static long atgm_knots_to_kmh_x100(const char *knots)
{
    double value;

    if (knots == RT_NULL || knots[0] == '\0')
    {
        return 0;
    }

    value = atof(knots) * 185.2;
    return (long)(value + 0.5);
}

static void atgm_format_x100(char *buf, rt_size_t buf_size, long value)
{
    long abs_value = (value >= 0) ? value : -value;

    if (buf == RT_NULL || buf_size == 0)
    {
        return;
    }

    rt_snprintf(buf,
                buf_size,
                "%s%ld.%02ld",
                (value < 0) ? "-" : "",
                abs_value / 100L,
                abs_value % 100L);
}

static rt_bool_t atgm_checksum_ok(const char *sentence)
{
    const char *star;
    rt_uint8_t checksum = 0;
    char expected_hex[3] = {0};
    long expected;
    const char *p;

    if (sentence == RT_NULL || sentence[0] != '$')
    {
        return RT_FALSE;
    }

    star = strchr(sentence, '*');
    if (star == RT_NULL || strlen(star + 1) < 2)
    {
        return RT_FALSE;
    }

    for (p = sentence + 1; p < star; p++)
    {
        checksum ^= (rt_uint8_t)(*p);
    }

    expected_hex[0] = star[1];
    expected_hex[1] = star[2];
    expected = strtol(expected_hex, RT_NULL, 16);
    return checksum == (rt_uint8_t)expected;
}

static void atgm_log_rmc(char *sentence)
{
    char *fields[16] = {0};
    int field_count = 0;
    char *token;
    char *saveptr = RT_NULL;
    const char *status;
    const char *time_str;
    const char *speed_str;
    const char *course_str;
    const char *date_str;
    char speed_kmh[16];

    if (!atgm_checksum_ok(sentence))
    {
        return;
    }

    token = strtok_r(sentence, ",", &saveptr);
    while (token != RT_NULL && field_count < (int)(sizeof(fields) / sizeof(fields[0])))
    {
        char *star = strchr(token, '*');
        if (star != RT_NULL)
        {
            *star = '\0';
        }
        fields[field_count++] = token;
        token = strtok_r(RT_NULL, ",", &saveptr);
    }

    if (field_count < 10)
    {
        return;
    }

    time_str = fields[1];
    status = fields[2];
    speed_str = fields[7];
    course_str = fields[8];
    date_str = fields[9];

    if (status[0] != 'A')
    {
        atgm_speed_valid = RT_FALSE;
#if APP_GPS_DATA_LOG_ENABLE
        log_g("ATGM336H speed unavailable time=%s date=%s status=%s", time_str, date_str, status);
#else
        RT_UNUSED(time_str);
        RT_UNUSED(date_str);
        RT_UNUSED(status);
#endif
        return;
    }

    {
        long raw_speed_kmh_x100 = atgm_knots_to_kmh_x100(speed_str);
        rt_tick_t now = rt_tick_get();

        if (raw_speed_kmh_x100 > 0)
        {
            atgm_last_nonzero_speed_kmh_x100 = raw_speed_kmh_x100;
            atgm_last_nonzero_speed_tick = now;
            atgm_last_speed_kmh_x100 = raw_speed_kmh_x100;
        }
        else if ((atgm_last_nonzero_speed_kmh_x100 > 0) &&
                 ((rt_tick_t)(now - atgm_last_nonzero_speed_tick) <= rt_tick_from_millisecond(APP_GPS_SPEED_ZERO_HOLD_MS)))
        {
            atgm_last_speed_kmh_x100 = atgm_last_nonzero_speed_kmh_x100;
        }
        else
        {
            atgm_last_speed_kmh_x100 = 0;
        }
    }

    atgm_copy_field(atgm_last_speed_knots, sizeof(atgm_last_speed_knots), speed_str);
    atgm_copy_field(atgm_last_course, sizeof(atgm_last_course), course_str);
    atgm_copy_field(atgm_last_time, sizeof(atgm_last_time), time_str);
    atgm_copy_field(atgm_last_date, sizeof(atgm_last_date), date_str);
    atgm_speed_valid = RT_TRUE;

#if APP_GPS_DATA_LOG_ENABLE
    atgm_format_x100(speed_kmh, sizeof(speed_kmh), atgm_last_speed_kmh_x100);
    log_g("ATGM336H speed=%skm/h raw=%skn course=%s time=%s date=%s",
          speed_kmh, atgm_last_speed_knots, atgm_last_course, atgm_last_time, atgm_last_date);
#else
    RT_UNUSED(time_str);
    RT_UNUSED(speed_str);
    RT_UNUSED(course_str);
    RT_UNUSED(date_str);
    RT_UNUSED(speed_kmh);
#endif
}

static void atgm_expire_held_speed(void)
{
    if ((atgm_last_speed_kmh_x100 > 0) &&
        (atgm_last_nonzero_speed_kmh_x100 > 0) &&
        ((rt_tick_t)(rt_tick_get() - atgm_last_nonzero_speed_tick) > rt_tick_from_millisecond(APP_GPS_SPEED_ZERO_HOLD_MS)))
    {
        atgm_last_speed_kmh_x100 = 0;
        atgm_last_nonzero_speed_kmh_x100 = 0;
    }
}

static void atgm_parse_sentence(char *line)
{
    char sentence_copy[ATGM336H_LINE_BUF_SIZE];

    if (line == RT_NULL || line[0] != '$')
    {
        return;
    }

    strncpy(sentence_copy, line, sizeof(sentence_copy) - 1);
    sentence_copy[sizeof(sentence_copy) - 1] = '\0';

    if (strncmp(sentence_copy, "$GNRMC", 6) == 0 || strncmp(sentence_copy, "$GPRMC", 6) == 0)
    {
        atgm_log_rmc(sentence_copy);
    }
}

int atgm336h_speed_status(void)
{
    char speed_kmh[16];

    if (atgm_speed_valid == RT_FALSE)
    {
        rt_kprintf("ATGM336H speed unavailable\r\n");
        return -RT_ERROR;
    }

    atgm_expire_held_speed();
    atgm_format_x100(speed_kmh, sizeof(speed_kmh), atgm_last_speed_kmh_x100);
    rt_kprintf("ATGM336H speed=%skm/h raw=%skn course=%s time=%s date=%s\r\n",
               speed_kmh,
               atgm_last_speed_knots,
               atgm_last_course,
               atgm_last_time,
               atgm_last_date);
    return RT_EOK;
}
MSH_CMD_EXPORT(atgm336h_speed_status, show atgm336h last speed);

rt_err_t atgm336h_get_speed_kmh_x100(long *speed_kmh_x100)
{
    if (speed_kmh_x100 == RT_NULL)
    {
        return -RT_EINVAL;
    }

    if (atgm_speed_valid == RT_FALSE)
    {
        return -RT_ERROR;
    }

    atgm_expire_held_speed();
    *speed_kmh_x100 = atgm_last_speed_kmh_x100;
    return RT_EOK;
}

static rt_err_t atgm_rx_ind(rt_device_t dev, rt_size_t size)
{
    RT_UNUSED(dev);
    RT_UNUSED(size);
    rt_sem_release(&atgm_rx_sem);
    return RT_EOK;
}

static void atgm_thread_entry(void *parameter)
{
    rt_uint8_t rx_buf[ATGM336H_RX_BUF_SIZE];

    RT_UNUSED(parameter);

    while (1)
    {
        rt_size_t rx_len;
        rt_size_t i;

        rx_len = rt_device_read(atgm_dev, 0, rx_buf, sizeof(rx_buf));
        if (rx_len == 0)
        {
            rt_sem_take(&atgm_rx_sem, RT_WAITING_FOREVER);
            continue;
        }

        if (atgm_rx_seen == RT_FALSE)
        {
            atgm_rx_seen = RT_TRUE;
#if APP_SPEED_DEBUG_LOG_ENABLE
            rt_kprintf("[gps] ATGM336H rx first data len=%u\r\n", (unsigned int)rx_len);
#elif APP_GPS_DATA_LOG_ENABLE
            log_g("ATGM336H rx first data len=%u", (unsigned int)rx_len);
#endif
        }

        for (i = 0; i < rx_len; i++)
        {
            char ch = (char)rx_buf[i];

            if (ch == '\r')
            {
                continue;
            }
            if (ch == '\n')
            {
                atgm_line_buf[atgm_line_len] = '\0';
                atgm_parse_sentence(atgm_line_buf);
                atgm_line_len = 0;
                continue;
            }

            if (atgm_line_len < (ATGM336H_LINE_BUF_SIZE - 1))
            {
                atgm_line_buf[atgm_line_len++] = ch;
            }
            else
            {
                atgm_line_len = 0;
            }
        }
    }
}

int atgm336h_init(void)
{
    rt_err_t result;
    rt_thread_t thread;
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;

    atgm_dev = rt_device_find(ATGM336H_DEVICE_NAME);
    if (atgm_dev == RT_NULL)
    {
        LOG_E("ATGM336H uart2 device not found");
        return -RT_ERROR;
    }

    result = rt_sem_init(&atgm_rx_sem, "gpxrx", 0, RT_IPC_FLAG_FIFO);
    if (result != RT_EOK)
    {
        LOG_E("ATGM336H rx semaphore init failed: %d", result);
        return result;
    }

    result = rt_device_open(atgm_dev, RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_DMA_RX);
    if (result != RT_EOK)
    {
        LOG_E("ATGM336H open failed: %d", result);
        rt_sem_detach(&atgm_rx_sem);
        atgm_dev = RT_NULL;
        return result;
    }

    config.baud_rate = BAUD_RATE_9600;
    result = rt_device_control(atgm_dev, RT_DEVICE_CTRL_CONFIG, &config);
    if (result != RT_EOK)
    {
        LOG_E("ATGM336H set baud failed: %d", result);
        rt_device_close(atgm_dev);
        rt_sem_detach(&atgm_rx_sem);
        atgm_dev = RT_NULL;
        return result;
    }

    rt_device_set_rx_indicate(atgm_dev, atgm_rx_ind);

    thread = rt_thread_create(ATGM336H_THREAD_NAME,
                              atgm_thread_entry,
                              RT_NULL,
                              ATGM336H_THREAD_STACK_SIZE,
                              ATGM336H_THREAD_PRIORITY,
                              ATGM336H_THREAD_TICK);
    if (thread == RT_NULL)
    {
        LOG_E("ATGM336H thread create failed");
        rt_device_close(atgm_dev);
        rt_sem_detach(&atgm_rx_sem);
        atgm_dev = RT_NULL;
        return -RT_ERROR;
    }

    rt_thread_startup(thread);
#if APP_SPEED_DEBUG_LOG_ENABLE
    rt_kprintf("[gps] ATGM336H started uart2@9600\r\n");
#endif
    return RT_EOK;
}
//INIT_APP_EXPORT(atgm336h_init);
