/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "uart4_app.h"
#include "uart4_mux.h"
#include "ld2451_parser.h"
#include "rc522_parser.h"

#define DBG_TAG "uart4.app"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#define lod_g(...) LOG_E(__VA_ARGS__)

static rt_device_t uart4_dev = RT_NULL;
static struct rt_semaphore uart4_rx_sem;
static rt_bool_t uart4_started = RT_FALSE;

typedef enum
{
    UART4_POLL_MODE_DRIVE = 0,
    UART4_POLL_MODE_ARRIVED,
} uart4_poll_mode_t;

static uart4_poll_mode_t uart4_poll_mode = UART4_POLL_MODE_DRIVE;
static rt_bool_t uart4_button_last_down = RT_FALSE;
static rt_tick_t uart4_button_last_toggle = 0;

rt_bool_t uart4_app_is_arrived_mode(void)
{
    return (uart4_poll_mode == UART4_POLL_MODE_ARRIVED) ? RT_TRUE : RT_FALSE;
}

static void uart4_app_set_poll_mode(uart4_poll_mode_t mode, const char *reason)
{
    RT_UNUSED(reason);

    if (uart4_poll_mode == mode)
    {
        return;
    }

    uart4_poll_mode = mode;
}

void uart4_app_enter_arrived_mode(void)
{
    uart4_app_set_poll_mode(UART4_POLL_MODE_ARRIVED, "uart3");
}

void uart4_app_enter_drive_mode(void)
{
    uart4_app_set_poll_mode(UART4_POLL_MODE_DRIVE, "rc522");
}

static rt_err_t uart4_rx_ind(rt_device_t dev, rt_size_t size)
{
    RT_UNUSED(dev);
    RT_UNUSED(size);

    rt_sem_release(&uart4_rx_sem);
    return RT_EOK;
}

static void uart4_button_init(void)
{
    rt_pin_mode(UART4_APP_DEST_BUTTON_PIN, PIN_MODE_INPUT_PULLUP);
    uart4_button_last_down = (rt_pin_read(UART4_APP_DEST_BUTTON_PIN) == UART4_APP_DEST_BUTTON_ACTIVE_LEVEL) ? RT_TRUE : RT_FALSE;
    uart4_button_last_toggle = rt_tick_get();
}

static void uart4_button_update(void)
{
    rt_bool_t down;
    rt_tick_t now;

    down = (rt_pin_read(UART4_APP_DEST_BUTTON_PIN) == UART4_APP_DEST_BUTTON_ACTIVE_LEVEL) ? RT_TRUE : RT_FALSE;
    now = rt_tick_get();

    if ((down == RT_TRUE) && (uart4_button_last_down == RT_FALSE) &&
        ((rt_tick_t)(now - uart4_button_last_toggle) >= rt_tick_from_millisecond(UART4_APP_DEST_BUTTON_DEBOUNCE_MS)))
    {
        uart4_app_set_poll_mode((uart4_poll_mode == UART4_POLL_MODE_DRIVE) ? UART4_POLL_MODE_ARRIVED : UART4_POLL_MODE_DRIVE,
                                "button");
        uart4_button_last_toggle = now;
    }

    uart4_button_last_down = down;
}
static void uart4_flush(void)
{
    rt_uint8_t rx_buf[UART4_APP_RX_BUF_SIZE];

    while (rt_device_read(uart4_dev, 0, rx_buf, sizeof(rx_buf)) > 0)
    {
    }
}

static void uart4_dispatch_rx(rt_uint8_t channel, const rt_uint8_t *rx_buf, rt_size_t rx_len)
{
    if (channel == UART4_APP_RFID_CHANNEL)
    {
        rc522_parser_input(channel, rx_buf, rx_len);
        return;
    }

    if (channel < UART4_APP_RADAR_CHANNELS)
    {
        ld2451_parser_input(channel, rx_buf, rx_len);
    }
}

static void uart4_poll_channel(rt_uint8_t channel, rt_uint32_t hold_ms)
{
    rt_uint8_t rx_buf[UART4_APP_RX_BUF_SIZE];
    rt_tick_t end_tick;
    rt_size_t rx_len;

    uart4_mux_select(channel);
    if (channel < UART4_APP_RADAR_CHANNELS)
    {
        ld2451_parser_reset(channel);
    }

    uart4_flush();
    end_tick = rt_tick_get() + rt_tick_from_millisecond(hold_ms);

    while ((rt_int32_t)(end_tick - rt_tick_get()) > 0)
    {
        uart4_button_update();

        rx_len = rt_device_read(uart4_dev, 0, rx_buf, sizeof(rx_buf));
        if (rx_len == 0)
        {
            rt_sem_take(&uart4_rx_sem, rt_tick_from_millisecond(UART4_APP_RX_WAIT_MS));
            continue;
        }

        uart4_dispatch_rx(channel, rx_buf, rx_len);
    }
}

static void uart4_thread_entry(void *parameter)
{
    RT_UNUSED(parameter);

    uart4_mux_select(0);

    while (1)
    {
        rt_uint8_t channel;

        uart4_button_update();
        if (uart4_poll_mode == UART4_POLL_MODE_ARRIVED)
        {
            uart4_poll_channel(UART4_APP_RFID_CHANNEL, UART4_APP_RFID_CHANNEL_HOLD_MS);
            continue;
        }

        for (channel = 0; channel < UART4_APP_RADAR_CHANNELS; channel++)
        {
            uart4_poll_channel(channel, UART4_APP_RADAR_CHANNEL_HOLD_MS);
            if (uart4_poll_mode != UART4_POLL_MODE_DRIVE)
            {
                break;
            }
        }
    }
}

int uart4_app_init(void)
{
    rt_err_t result;
    rt_thread_t thread;
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;

    if (uart4_started == RT_TRUE)
    {
        return RT_EOK;
    }

    uart4_dev = rt_device_find(UART4_APP_DEVICE_NAME);
    if (uart4_dev == RT_NULL)
    {
        lod_g("UART4 device not found");
        return -RT_ERROR;
    }

    uart4_mux_gpio_init();
    uart4_button_init();
    uart4_mux_select(0);

    result = rt_sem_init(&uart4_rx_sem, "u4rx", 0, RT_IPC_FLAG_FIFO);
    if (result != RT_EOK)
    {
        lod_g("UART4 rx semaphore init failed: %d", result);
        return result;
    }

    config.baud_rate = BAUD_RATE_115200;
    result = rt_device_control(uart4_dev, RT_DEVICE_CTRL_CONFIG, &config);
    if (result != RT_EOK)
    {
        lod_g("UART4 set baud failed: %d", result);
        rt_sem_detach(&uart4_rx_sem);
        uart4_dev = RT_NULL;
        return result;
    }

    result = rt_device_open(uart4_dev, RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_DMA_RX);
    if (result != RT_EOK)
    {
        lod_g("UART4 open failed: %d", result);
        rt_sem_detach(&uart4_rx_sem);
        uart4_dev = RT_NULL;
        return result;
    }

    rt_device_set_rx_indicate(uart4_dev, uart4_rx_ind);

    thread = rt_thread_create(UART4_APP_THREAD_NAME,
                              uart4_thread_entry,
                              RT_NULL,
                              UART4_APP_THREAD_STACK_SIZE,
                              UART4_APP_THREAD_PRIORITY,
                              UART4_APP_THREAD_TICK);
    if (thread == RT_NULL)
    {
        lod_g("UART4 mux thread create failed");
        rt_device_close(uart4_dev);
        rt_sem_detach(&uart4_rx_sem);
        uart4_dev = RT_NULL;
        return -RT_ERROR;
    }

    uart4_started = RT_TRUE;
    rt_thread_startup(thread);
    return RT_EOK;
}

//INIT_APP_EXPORT(uart4_app_init);

int lm2451_init(void)
{
    return uart4_app_init();
}

int rc522_uart_init(void)
{
    return uart4_app_init();
}
