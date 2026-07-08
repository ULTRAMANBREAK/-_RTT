/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>

#include "uart4_app.h"
#include "sensor_app.h"
#include "lcd_app.h"
#include "uart_app.h"
#include "atgm336h.h"
#include "ws2812.h"
#include "buzzer.h"
#include "nrf24_app.h"
#include "servo_lock.h"
#include "app_storage.h"

#define DBG_TAG "app.start"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#define APP_START_THREAD_NAME       "app_start"
#define APP_START_THREAD_STACK_SIZE 2048
#define APP_START_THREAD_PRIORITY   17
#define APP_START_THREAD_TICK       20

typedef int (*app_start_init_fn_t)(void);

typedef struct
{
    const char *name;
    app_start_init_fn_t init;
} app_start_item_t;

static const app_start_item_t app_start_items[] = {
    {"storage", app_storage_init},
    {"uart3", uart_app_init},
    {"ws2812", ws2812_init},
    {"buzzer", buzzer_init},
    {"servo_lock", servo_lock_init},
    {"gps", atgm336h_init},
    {"uart4", uart4_app_init},
    {"sensor", sensor_app_init},
    {"lcd", lcd_app_init},
    {"nrf24", nrf24_app_init},
};

static void app_start_run_item(const app_start_item_t *item)
{
    rt_err_t result;

    result = item->init();
    if (result != RT_EOK)
    {
        LOG_E("start %s: failed %d", item->name, result);
    }
}

static void app_startup_entry(void *parameter)
{
    rt_size_t i;

    RT_UNUSED(parameter);

    for (i = 0; i < sizeof(app_start_items) / sizeof(app_start_items[0]); i++)
    {
        app_start_run_item(&app_start_items[i]);
    }
}

int app_startup_init(void)
{
    rt_thread_t thread;

    thread = rt_thread_create(APP_START_THREAD_NAME,
                              app_startup_entry,
                              RT_NULL,
                              APP_START_THREAD_STACK_SIZE,
                              APP_START_THREAD_PRIORITY,
                              APP_START_THREAD_TICK);
    if (thread == RT_NULL)
    {
        LOG_E("app startup thread create failed");
        return -RT_ERROR;
    }

    rt_thread_startup(thread);
    return RT_EOK;
}
INIT_APP_EXPORT(app_startup_init);
