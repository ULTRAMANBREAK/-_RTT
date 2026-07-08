/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>

#include "drv_lcd.h"
#include "sensor_app.h"
#include "lcd_app.h"

#define DBG_TAG "lcd.app"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#define LCD_FONT_SIZE           16
#define LCD_LINE_STEP           18
#define LCD_THREAD_STACK_SIZE   2048
#define LCD_THREAD_PRIORITY     16
#define LCD_THREAD_TICK         10

static int float_to_fixed1(float value)
{
    if (value >= 0.0f)
    {
        return (int)(value * 10.0f + 0.5f);
    }

    return (int)(value * 10.0f - 0.5f);
}

static void lcd_show_fixed1(rt_uint16_t x, rt_uint16_t y, const char *prefix, float value, const char *unit)
{
    int scaled = float_to_fixed1(value);

    if (scaled < 0)
    {
        scaled = -scaled;
        lcd_show_string(x, y, LCD_FONT_SIZE, "%s-%d.%d%s", prefix, scaled / 10, scaled % 10, unit);
    }
    else
    {
        lcd_show_string(x, y, LCD_FONT_SIZE, "%s%d.%d%s", prefix, scaled / 10, scaled % 10, unit);
    }
}

static void lcd_show_angle_x10(rt_uint16_t x, rt_uint16_t y, rt_int16_t angle_x10)
{
    rt_int16_t abs_angle = angle_x10;

    if (abs_angle < 0)
    {
        abs_angle = (rt_int16_t)(-abs_angle);
        lcd_show_string(x, y, LCD_FONT_SIZE, "ANG:-%d.%ddeg", abs_angle / 10, abs_angle % 10);
    }
    else
    {
        lcd_show_string(x, y, LCD_FONT_SIZE, "ANG:%d.%ddeg", abs_angle / 10, abs_angle % 10);
    }
}

static void lcd_show_err(rt_uint16_t y, const char *label)
{
    lcd_show_string(0, y, LCD_FONT_SIZE, "%s: ERR", label);
}

static void lcd_show_startup(void)
{
    lcd_set_color(WHITE, BLACK);
    lcd_clear(WHITE);
    lcd_fill(0, 0, LCD_W - 1, LCD_LINE_STEP * 3, BLUE);
    lcd_set_color(BLUE, WHITE);
    lcd_show_string(0, 0, LCD_FONT_SIZE, "Sensor LCD Monitor");
    lcd_show_string(0, LCD_LINE_STEP, LCD_FONT_SIZE, "LCD/backlight OK");
    lcd_show_string(0, LCD_LINE_STEP * 2, LCD_FONT_SIZE, "Sensors ready");
    rt_thread_mdelay(800);
    lcd_set_color(WHITE, BLACK);
}

static void lcd_show_sensor_msg(const sensor_msg_t *msg)
{
    lcd_clear(WHITE);
    lcd_show_string(0, 0, LCD_FONT_SIZE, "Sensor LCD Monitor");
    lcd_show_string(0, LCD_LINE_STEP, LCD_FONT_SIZE, "Refresh:%lu", (unsigned long)msg->tick);

    if (msg->icm_ok)
    {
        lcd_show_string(0, LCD_LINE_STEP * 2, LCD_FONT_SIZE, "ICM ACC X:%6d", msg->accel_x);
        lcd_show_string(0, LCD_LINE_STEP * 3, LCD_FONT_SIZE, "ICM ACC Y:%6d", msg->accel_y);
        lcd_show_string(0, LCD_LINE_STEP * 4, LCD_FONT_SIZE, "ICM ACC Z:%6d", msg->accel_z);
        lcd_show_string(0, LCD_LINE_STEP * 5, LCD_FONT_SIZE, "MOT:%s", sensor_motion_name(msg->motion));
        lcd_show_angle_x10(0, LCD_LINE_STEP * 6, msg->turn_angle_deg_x10);
        if (msg->speed_valid)
        {
            lcd_show_string(0,
                            LCD_LINE_STEP * 7,
                            LCD_FONT_SIZE,
                            "SPD:%ld.%02ld OS:%u",
                            msg->speed_kmh_x100 / 100L,
                            msg->speed_kmh_x100 % 100L,
                            msg->overspeed);
        }
        else
        {
            lcd_show_string(0, LCD_LINE_STEP * 7, LCD_FONT_SIZE, "SPD:-- OS:%u", msg->overspeed);
        }
    }
    else
    {
        lcd_show_err(LCD_LINE_STEP * 2, "ICM ACC");
        lcd_show_err(LCD_LINE_STEP * 3, "ICM ACC");
        lcd_show_err(LCD_LINE_STEP * 4, "ICM ACC");
        lcd_show_err(LCD_LINE_STEP * 5, "MOT");
        lcd_show_err(LCD_LINE_STEP * 6, "ANG");
        lcd_show_err(LCD_LINE_STEP * 7, "FALL");
    }

    if (msg->aht_ok)
    {
        lcd_show_fixed1(0, LCD_LINE_STEP * 8, "AHT T:", msg->temperature, "C");
        lcd_show_fixed1(0, LCD_LINE_STEP * 9, "AHT H:", msg->humidity, "%");
    }
    else
    {
        lcd_show_err(LCD_LINE_STEP * 8, "AHT T");
        lcd_show_err(LCD_LINE_STEP * 9, "AHT H");
    }

    if (msg->ap_ok)
    {
        lcd_show_fixed1(0, LCD_LINE_STEP * 10, "AP L:", msg->ambient_light, "lx");
        lcd_show_string(0, LCD_LINE_STEP * 11, LCD_FONT_SIZE, "AP P:%u", (unsigned int)msg->proximity);
    }
    else
    {
        lcd_show_err(LCD_LINE_STEP * 10, "AP L");
        lcd_show_err(LCD_LINE_STEP * 11, "AP P");
    }
}

static void lcd_app_entry(void *parameter)
{
    rt_mq_t sensor_mq;
    sensor_msg_t msg;

    RT_UNUSED(parameter);

    while (drv_lcd_app_init() != RT_EOK)
    {
        LOG_E("LCD init failed");
        rt_thread_mdelay(1000);
    }

    lcd_show_startup();

    do
    {
        sensor_mq = sensor_app_get_mq();
        if (sensor_mq == RT_NULL)
        {
            rt_thread_mdelay(100);
        }
    } while (sensor_mq == RT_NULL);

    while (1)
    {
        if (rt_mq_recv(sensor_mq, &msg, sizeof(msg), RT_WAITING_FOREVER) != RT_EOK)
        {
            continue;
        }

        lcd_show_sensor_msg(&msg);
    }
}

int lcd_app_init(void)
{
    rt_thread_t thread;

    thread = rt_thread_create("lcd_app",
                              lcd_app_entry,
                              RT_NULL,
                              LCD_THREAD_STACK_SIZE,
                              LCD_THREAD_PRIORITY,
                              LCD_THREAD_TICK);
    if (thread == RT_NULL)
    {
        LOG_E("lcd thread create failed");
        return -RT_ERROR;
    }

    rt_thread_startup(thread);
    return RT_EOK;
}
//INIT_APP_EXPORT(lcd_app_init);
