/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>
#include <rthw.h>
#include <stm32f4xx_hal.h>

#include "app_config.h"
#include "ws2812.h"

#define DBG_TAG "ws2812"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#if defined(RT_USING_FINSH)
#include <finsh.h>
#endif

#define lod_g(...)                    LOG_I(__VA_ARGS__)

#define WS2812_PORT                   APP_WS2812_PORT
#define WS2812_PIN                    APP_WS2812_PIN
#define WS2812_LEFT_PIN               APP_WS2812_LEFT_PIN
#define WS2812_RIGHT_PIN              APP_WS2812_RIGHT_PIN
#define WS2812_GPIO_CLK_ENABLE()      APP_WS2812_GPIO_CLK_ENABLE()
#define WS2812_GPIO_HIGH(pin_mask)    (WS2812_PORT->BSRR = (rt_uint32_t)(pin_mask))
#define WS2812_GPIO_LOW(pin_mask)     (WS2812_PORT->BSRR = (rt_uint32_t)(pin_mask) << 16U)

#define WS2812_LED_BYTES              3
#define WS2812_DEMO_LED_COUNT         8
#define WS2812_MAX_LED_COUNT          APP_WS2812_MAX_LED_COUNT
#define WS2812_RESET_US               80
#define WS2812_AUTO_DEMO_ENABLE       0
#define WS2812_RADAR_CHANNELS         3
#define WS2812_RADAR_SIDE_LEFT        0
#define WS2812_RADAR_SIDE_RIGHT       1
#define WS2812_RADAR_SIDE_COUNT       2
#define WS2812_SIDE_MASK_LEFT         (1U << WS2812_RADAR_SIDE_LEFT)
#define WS2812_SIDE_MASK_RIGHT        (1U << WS2812_RADAR_SIDE_RIGHT)
#define WS2812_SIDE_MASK_BOTH         (WS2812_SIDE_MASK_LEFT | WS2812_SIDE_MASK_RIGHT)
#define WS2812_RADAR_THREAD_NAME      "radar_led"
#define WS2812_RADAR_THREAD_STACK     1024
#define WS2812_RADAR_THREAD_PRIORITY  21
#define WS2812_RADAR_THREAD_TICK      10
#define WS2812_RADAR_POLL_MS          50
#define WS2812_RADAR_DISTANCE_NONE    0xFFU

static rt_uint32_t ws2812_cycles_per_us = 0;
static rt_uint32_t ws2812_t0h_cycles = 0;
static rt_uint32_t ws2812_t1h_cycles = 0;
static rt_uint32_t ws2812_bit_cycles = 0;
static rt_uint32_t ws2812_reset_cycles = 0;
static rt_bool_t ws2812_ready = RT_FALSE;
static struct rt_mutex ws2812_lock;
static rt_bool_t ws2812_lock_ready = RT_FALSE;

typedef struct
{
    rt_bool_t active;
    rt_uint8_t distance_m;
    rt_tick_t updated_tick;
} ws2812_radar_channel_t;

static ws2812_radar_channel_t ws2812_radar_channels[WS2812_RADAR_CHANNELS];
static rt_bool_t ws2812_radar_visible = RT_FALSE;
static rt_tick_t ws2812_radar_toggle_tick = 0;
static rt_bool_t ws2812_motion_active = RT_FALSE;
static rt_uint8_t ws2812_motion_mask = 0U;
static rt_tick_t ws2812_motion_until = 0;
static rt_uint8_t ws2812_output_last_mask = 0xFFU;
static rt_uint8_t ws2812_output_last_red = 0xFFU;
static rt_uint8_t ws2812_output_last_green = 0xFFU;
static rt_uint8_t ws2812_output_last_blue = 0xFFU;

static void ws2812_delay_cycles(rt_uint32_t cycles)
{
    rt_uint32_t start;

    if (cycles == 0U)
    {
        return;
    }

    start = DWT->CYCCNT;
    while ((rt_uint32_t)(DWT->CYCCNT - start) < cycles)
    {
        __NOP();
    }
}

static void ws2812_dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void ws2812_timing_init(void)
{
    if (SystemCoreClock == 0U)
    {
        SystemCoreClockUpdate();
    }

    ws2812_cycles_per_us = SystemCoreClock / 1000000U;
    if (ws2812_cycles_per_us == 0U)
    {
        ws2812_cycles_per_us = 1U;
    }

    ws2812_t0h_cycles = (ws2812_cycles_per_us * 35U) / 100U;
    ws2812_t1h_cycles = (ws2812_cycles_per_us * 70U) / 100U;
    ws2812_bit_cycles = (ws2812_cycles_per_us * 125U) / 100U;
    ws2812_reset_cycles = ws2812_cycles_per_us * WS2812_RESET_US;

    if (ws2812_t0h_cycles == 0U) ws2812_t0h_cycles = 1U;
    if (ws2812_t1h_cycles == 0U) ws2812_t1h_cycles = 1U;
    if (ws2812_bit_cycles == 0U) ws2812_bit_cycles = 1U;
    if (ws2812_reset_cycles == 0U) ws2812_reset_cycles = 1U;
}

static void ws2812_send_bit(rt_uint32_t pin_mask, rt_bool_t bit)
{
    rt_uint32_t start;
    rt_uint32_t high_cycles = bit ? ws2812_t1h_cycles : ws2812_t0h_cycles;

    WS2812_GPIO_HIGH(pin_mask);
    start = DWT->CYCCNT;
    while ((rt_uint32_t)(DWT->CYCCNT - start) < high_cycles)
    {
        __NOP();
    }

    WS2812_GPIO_LOW(pin_mask);
    while ((rt_uint32_t)(DWT->CYCCNT - start) < ws2812_bit_cycles)
    {
        __NOP();
    }
}

static void ws2812_send_byte(rt_uint32_t pin_mask, rt_uint8_t value)
{
    rt_uint8_t bit;

    for (bit = 0; bit < 8U; bit++)
    {
        ws2812_send_bit(pin_mask, (value & (0x80U >> bit)) != 0U);
    }
}

static rt_err_t ws2812_send_frame_unlocked(const rt_uint8_t *grb, rt_size_t led_count, rt_uint32_t pin_mask)
{
    rt_base_t level;
    rt_size_t i;

    if (ws2812_ready == RT_FALSE)
    {
        return -RT_ERROR;
    }

    if (grb == RT_NULL || led_count == 0U)
    {
        return -RT_ERROR;
    }

    if (pin_mask == 0U)
    {
        return -RT_ERROR;
    }

    level = rt_hw_interrupt_disable();

    for (i = 0; i < led_count; i++)
    {
        const rt_uint8_t *pixel = &grb[i * WS2812_LED_BYTES];

        ws2812_send_byte(pin_mask, pixel[0]);
        ws2812_send_byte(pin_mask, pixel[1]);
        ws2812_send_byte(pin_mask, pixel[2]);
    }

    WS2812_GPIO_LOW(pin_mask);
    ws2812_delay_cycles(ws2812_reset_cycles);

    rt_hw_interrupt_enable(level);

    return RT_EOK;
}

static rt_err_t ws2812_send_frame(const rt_uint8_t *grb, rt_size_t led_count, rt_uint32_t pin_mask)
{
    rt_err_t result;

    if (ws2812_lock_ready == RT_TRUE)
    {
        result = rt_mutex_take(&ws2812_lock, rt_tick_from_millisecond(100));
        if (result != RT_EOK)
        {
            return result;
        }
    }

    result = ws2812_send_frame_unlocked(grb, led_count, pin_mask);

    if (ws2812_lock_ready == RT_TRUE)
    {
        rt_mutex_release(&ws2812_lock);
    }

    return result;
}

rt_err_t ws2812_show_grb(const rt_uint8_t *grb, rt_size_t led_count)
{
    rt_err_t result;

    result = ws2812_send_frame(grb, led_count, WS2812_PIN);
    if (result != RT_EOK)
    {
        LOG_E("WS2812 show grb failed: %d", result);
    }

    return result;
}

rt_err_t ws2812_show_rgb(rt_uint8_t red, rt_uint8_t green, rt_uint8_t blue)
{
    rt_uint8_t grb[WS2812_LED_BYTES];

    grb[0] = red;
    grb[1] = blue;
    grb[2] = green;

    return ws2812_show_grb(grb, 1U);
}

static void ws2812_make_rgb_frame(rt_uint8_t *grb,
                                  rt_uint8_t red,
                                  rt_uint8_t green,
                                  rt_uint8_t blue,
                                  rt_size_t led_count)
{
    rt_size_t i;

    for (i = 0; i < led_count; i++)
    {
        grb[(i * WS2812_LED_BYTES) + 0] = red;
        grb[(i * WS2812_LED_BYTES) + 1] = blue;
        grb[(i * WS2812_LED_BYTES) + 2] = green;
    }
}

static rt_err_t ws2812_fill_rgb_mask(rt_uint32_t pin_mask,
                                     rt_uint8_t red,
                                     rt_uint8_t green,
                                     rt_uint8_t blue,
                                     rt_size_t led_count)
{
    rt_uint8_t grb[WS2812_MAX_LED_COUNT * WS2812_LED_BYTES];

    if (led_count == 0U || led_count > WS2812_MAX_LED_COUNT)
    {
        return -RT_ERROR;
    }

    ws2812_make_rgb_frame(grb, red, green, blue, led_count);

    return ws2812_send_frame(grb, led_count, pin_mask);
}

rt_err_t ws2812_fill_rgb(rt_uint8_t red, rt_uint8_t green, rt_uint8_t blue, rt_size_t led_count)
{
    return ws2812_fill_rgb_mask(WS2812_PIN, red, green, blue, led_count);
}

static rt_size_t ws2812_side_led_count(rt_uint8_t side)
{
    return (side == WS2812_RADAR_SIDE_LEFT) ? APP_WS2812_LEFT_LED_COUNT : APP_WS2812_RIGHT_LED_COUNT;
}

static rt_err_t ws2812_set_side_rgb(rt_uint8_t side,
                                    rt_bool_t visible,
                                    rt_uint8_t red,
                                    rt_uint8_t green,
                                    rt_uint8_t blue)
{
    rt_uint32_t pin_mask;

    pin_mask = (side == WS2812_RADAR_SIDE_LEFT) ? WS2812_LEFT_PIN : WS2812_RIGHT_PIN;
    if (visible == RT_FALSE)
    {
        red = 0U;
        green = 0U;
        blue = 0U;
    }
    return ws2812_fill_rgb_mask(pin_mask, red, green, blue, ws2812_side_led_count(side));
}

static void ws2812_output_invalidate(void)
{
    ws2812_output_last_mask = 0xFFU;
    ws2812_output_last_red = 0xFFU;
    ws2812_output_last_green = 0xFFU;
    ws2812_output_last_blue = 0xFFU;
}

static void ws2812_apply_output(rt_uint8_t side_mask,
                                rt_uint8_t red,
                                rt_uint8_t green,
                                rt_uint8_t blue,
                                const char *source)
{
    rt_err_t left_result;
    rt_err_t right_result;

    if ((side_mask == ws2812_output_last_mask) &&
        (red == ws2812_output_last_red) &&
        (green == ws2812_output_last_green) &&
        (blue == ws2812_output_last_blue))
    {
        return;
    }

#if APP_WS2811_LOG_ENABLE
    LOG_W("WS2811 out src=%s mask=0x%02x rgb=%02x,%02x,%02x",
          source,
          side_mask,
          red,
          green,
          blue);
#endif

    left_result = ws2812_set_side_rgb(WS2812_RADAR_SIDE_LEFT,
                                      (side_mask & WS2812_SIDE_MASK_LEFT) != 0U,
                                      red,
                                      green,
                                      blue);
    right_result = ws2812_set_side_rgb(WS2812_RADAR_SIDE_RIGHT,
                                       (side_mask & WS2812_SIDE_MASK_RIGHT) != 0U,
                                       red,
                                       green,
                                       blue);

#if APP_WS2811_LOG_ENABLE
    if ((left_result != RT_EOK) || (right_result != RT_EOK))
    {
        LOG_W("WS2811 send result left=%d right=%d", left_result, right_result);
    }
#endif

    if ((left_result == RT_EOK) && (right_result == RT_EOK))
    {
        ws2812_output_last_mask = side_mask;
        ws2812_output_last_red = red;
        ws2812_output_last_green = green;
        ws2812_output_last_blue = blue;
    }
}

static rt_uint16_t ws2812_radar_interval_ms(rt_uint8_t distance_m)
{
    if (distance_m <= 1U)
    {
        return 120U;
    }
    if (distance_m <= 2U)
    {
        return 200U;
    }
    if (distance_m <= 3U)
    {
        return 350U;
    }
    return 600U;
}

static rt_uint8_t ws2812_radar_channel_side_mask(rt_uint8_t channel)
{
    if (channel == 0U)
    {
        return WS2812_SIDE_MASK_LEFT;
    }
    if (channel == 1U)
    {
        return WS2812_SIDE_MASK_BOTH;
    }
    if (channel == 2U)
    {
        return WS2812_SIDE_MASK_RIGHT;
    }
    return 0U;
}

static rt_uint8_t ws2812_radar_snapshot_mask(rt_uint8_t *nearest_distance)
{
    rt_tick_t now;
    rt_tick_t no_data_hold_ticks;
    rt_tick_t selected_tick = 0;
    rt_uint8_t selected_channel = 0U;
    rt_uint8_t selected_distance = WS2812_RADAR_DISTANCE_NONE;
    rt_uint8_t channel;
    rt_bool_t found = RT_FALSE;

    if (nearest_distance != RT_NULL)
    {
        *nearest_distance = WS2812_RADAR_DISTANCE_NONE;
    }

    now = rt_tick_get();
    no_data_hold_ticks = rt_tick_from_millisecond(APP_RADAR_WARN_NO_DATA_HOLD_MS);

    if (ws2812_lock_ready == RT_TRUE)
    {
        rt_mutex_take(&ws2812_lock, RT_WAITING_FOREVER);
    }

    for (channel = 0; channel < WS2812_RADAR_CHANNELS; channel++)
    {
        ws2812_radar_channel_t *state = &ws2812_radar_channels[channel];

        if (state->active == RT_FALSE)
        {
            continue;
        }

        if ((rt_tick_t)(now - state->updated_tick) > no_data_hold_ticks)
        {
            state->active = RT_FALSE;
            state->distance_m = WS2812_RADAR_DISTANCE_NONE;
            continue;
        }

        if ((found == RT_FALSE) ||
            (state->distance_m < selected_distance) ||
            ((state->distance_m == selected_distance) && ((rt_int32_t)(state->updated_tick - selected_tick) > 0)))
        {
            found = RT_TRUE;
            selected_channel = channel;
            selected_distance = state->distance_m;
            selected_tick = state->updated_tick;
        }
    }

    if (ws2812_lock_ready == RT_TRUE)
    {
        rt_mutex_release(&ws2812_lock);
    }

    if ((found == RT_TRUE) && (nearest_distance != RT_NULL))
    {
        *nearest_distance = selected_distance;
    }

    return found ? ws2812_radar_channel_side_mask(selected_channel) : 0U;
}
static void ws2812_radar_thread_entry(void *parameter)
{
    RT_UNUSED(parameter);

    while (1)
    {
        rt_uint8_t radar_mask;
        rt_uint8_t radar_distance = WS2812_RADAR_DISTANCE_NONE;
        rt_uint8_t side_mask;
        rt_uint8_t red = 0U;
        rt_uint8_t green = 0U;
        rt_uint8_t blue = 0U;
        rt_tick_t now;
        rt_bool_t motion_active;
        rt_uint8_t motion_mask;
        rt_tick_t motion_until;
        const char *source = "off";

        radar_mask = ws2812_radar_snapshot_mask(&radar_distance);
        side_mask = radar_mask;
        now = rt_tick_get();
        if (radar_mask != 0U)
        {
            rt_tick_t interval_ticks;

            interval_ticks = rt_tick_from_millisecond(ws2812_radar_interval_ms(radar_distance));
            if (ws2812_radar_toggle_tick == 0)
            {
                ws2812_radar_visible = RT_TRUE;
                ws2812_radar_toggle_tick = now;
            }
            else if ((rt_tick_t)(now - ws2812_radar_toggle_tick) >= interval_ticks)
            {
                ws2812_radar_visible = !ws2812_radar_visible;
                ws2812_radar_toggle_tick = now;
            }

            if (ws2812_radar_visible == RT_FALSE)
            {
                side_mask = 0U;
            }
            red = APP_RADAR_WARN_COLOR_R;
            green = APP_RADAR_WARN_COLOR_G;
            blue = APP_RADAR_WARN_COLOR_B;
            source = "radar";
        }
        else
        {
            ws2812_radar_visible = RT_FALSE;
            ws2812_radar_toggle_tick = 0;

            rt_mutex_take(&ws2812_lock, RT_WAITING_FOREVER);
            motion_active = ws2812_motion_active;
            motion_mask = ws2812_motion_mask;
            motion_until = ws2812_motion_until;
            rt_mutex_release(&ws2812_lock);

            if (motion_active == RT_TRUE)
            {
                if ((rt_int32_t)(motion_until - now) > 0)
                {
                    side_mask = motion_mask;
                    red = APP_MOTION_TURN_COLOR_R;
                    green = APP_MOTION_TURN_COLOR_G;
                    blue = APP_MOTION_TURN_COLOR_B;
                    source = "motion";
                }
                else
                {
                    rt_mutex_take(&ws2812_lock, RT_WAITING_FOREVER);
                    ws2812_motion_active = RT_FALSE;
                    ws2812_motion_mask = 0U;
                    rt_mutex_release(&ws2812_lock);
                }
            }
        }

        ws2812_apply_output(side_mask, red, green, blue, source);
        rt_thread_mdelay(WS2812_RADAR_POLL_MS);
    }
}
void ws2812_radar_report(rt_uint8_t channel, rt_bool_t active, rt_uint8_t distance_m)
{
    if (channel >= WS2812_RADAR_CHANNELS || ws2812_lock_ready == RT_FALSE)
    {
        return;
    }

    rt_mutex_take(&ws2812_lock, RT_WAITING_FOREVER);
#if APP_WS2811_LOG_ENABLE
    if ((ws2812_radar_channels[channel].active != active) ||
        ((active == RT_TRUE) && (ws2812_radar_channels[channel].distance_m != distance_m)))
    {
        LOG_W("WS2811 radar report ch=%u active=%u distance=%um",
              (unsigned int)channel,
              (active == RT_TRUE) ? 1U : 0U,
              (unsigned int)distance_m);
    }
#endif
    ws2812_radar_channels[channel].active = active;
    ws2812_radar_channels[channel].distance_m = (active == RT_TRUE) ? distance_m : WS2812_RADAR_DISTANCE_NONE;
    ws2812_radar_channels[channel].updated_tick = rt_tick_get();
    rt_mutex_release(&ws2812_lock);
}

void ws2812_motion_report(rt_bool_t left_active,
                          rt_bool_t right_active,
                          rt_bool_t flash,
                          rt_uint32_t hold_ms)
{
    rt_bool_t was_active;
    rt_bool_t mask_changed;
    rt_uint8_t old_mask;
    rt_uint8_t new_mask = 0U;

    if (ws2812_lock_ready == RT_FALSE)
    {
        return;
    }

    if (left_active == RT_TRUE)
    {
        new_mask |= WS2812_SIDE_MASK_LEFT;
    }
    if (right_active == RT_TRUE)
    {
        new_mask |= WS2812_SIDE_MASK_RIGHT;
    }

    rt_mutex_take(&ws2812_lock, RT_WAITING_FOREVER);
    was_active = ws2812_motion_active;
    old_mask = ws2812_motion_mask;
    if (new_mask == 0U)
    {
        ws2812_motion_active = RT_FALSE;
        ws2812_motion_mask = 0U;
        ws2812_motion_until = 0;
        if ((was_active == RT_TRUE) || (old_mask != 0U))
        {
            ws2812_output_invalidate();
        }
    }
    else
    {
        mask_changed = (was_active == RT_FALSE) || (old_mask != new_mask);
#if APP_WS2811_LOG_ENABLE
        if (mask_changed == RT_TRUE)
        {
            LOG_W("WS2811 motion report left=%u right=%u flash=%u hold=%lu mask=0x%02x",
                  left_active,
                  right_active,
                  flash,
                  (unsigned long)hold_ms,
                  new_mask);
        }
#endif
        RT_UNUSED(flash);
        ws2812_motion_active = RT_TRUE;
        ws2812_motion_mask = new_mask;
        ws2812_motion_until = rt_tick_get() + rt_tick_from_millisecond(hold_ms);
        if (mask_changed == RT_TRUE)
        {
            ws2812_output_invalidate();
        }
    }
    rt_mutex_release(&ws2812_lock);
}
void ws2812_demo(void)
{
    ws2812_fill_rgb(0x00, 0x20, 0x00, WS2812_DEMO_LED_COUNT);
    rt_thread_mdelay(500);

    ws2812_fill_rgb(0x20, 0x00, 0x00, WS2812_DEMO_LED_COUNT);
    rt_thread_mdelay(500);

    ws2812_fill_rgb(0x00, 0x00, 0x20, WS2812_DEMO_LED_COUNT);
    rt_thread_mdelay(500);

    ws2812_fill_rgb(0x00, 0x00, 0x00, WS2812_DEMO_LED_COUNT);
}

void ws2812_left_all(void)
{
    ws2812_fill_rgb_mask(WS2812_LEFT_PIN, 0x00, 0x20, 0x00, APP_WS2812_LEFT_LED_COUNT);
}

void ws2812_right_all(void)
{
    ws2812_fill_rgb_mask(WS2812_RIGHT_PIN, 0x00, 0x20, 0x00, APP_WS2812_RIGHT_LED_COUNT);
}

void ws2812_left_red(void)
{
    ws2812_fill_rgb_mask(WS2812_LEFT_PIN, 0x20, 0x00, 0x00, APP_WS2812_LEFT_LED_COUNT);
}

void ws2812_left_green(void)
{
    ws2812_fill_rgb_mask(WS2812_LEFT_PIN, 0x00, 0x20, 0x00, APP_WS2812_LEFT_LED_COUNT);
}

void ws2812_left_blue(void)
{
    ws2812_fill_rgb_mask(WS2812_LEFT_PIN, 0x00, 0x00, 0x20, APP_WS2812_LEFT_LED_COUNT);
}

void ws2812_left_white(void)
{
    ws2812_fill_rgb_mask(WS2812_LEFT_PIN, 0x10, 0x10, 0x10, APP_WS2812_LEFT_LED_COUNT);
}

void ws2812_right_red(void)
{
    ws2812_fill_rgb_mask(WS2812_RIGHT_PIN, 0x20, 0x00, 0x00, APP_WS2812_RIGHT_LED_COUNT);
}

void ws2812_right_green(void)
{
    ws2812_fill_rgb_mask(WS2812_RIGHT_PIN, 0x00, 0x20, 0x00, APP_WS2812_RIGHT_LED_COUNT);
}

void ws2812_right_blue(void)
{
    ws2812_fill_rgb_mask(WS2812_RIGHT_PIN, 0x00, 0x00, 0x20, APP_WS2812_RIGHT_LED_COUNT);
}

void ws2812_right_white(void)
{
    ws2812_fill_rgb_mask(WS2812_RIGHT_PIN, 0x10, 0x10, 0x10, APP_WS2812_RIGHT_LED_COUNT);
}

void ws2812_motion_left_test(void)
{
    ws2812_motion_report(RT_TRUE, RT_FALSE, RT_FALSE, APP_SENSOR_TURN_LIGHT_HOLD_MS);
}

void ws2812_motion_right_test(void)
{
    ws2812_motion_report(RT_FALSE, RT_TRUE, RT_FALSE, APP_SENSOR_TURN_LIGHT_HOLD_MS);
}

void ws2812_all_off(void)
{
    ws2812_fill_rgb_mask(WS2812_LEFT_PIN, 0x00, 0x00, 0x00, APP_WS2812_LEFT_LED_COUNT);
    ws2812_fill_rgb_mask(WS2812_RIGHT_PIN, 0x00, 0x00, 0x00, APP_WS2812_RIGHT_LED_COUNT);
}

void ws2812_right_pin_high(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    WS2812_GPIO_CLK_ENABLE();
    gpio_init.Pin = WS2812_RIGHT_PIN;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(WS2812_PORT, &gpio_init);
    WS2812_GPIO_HIGH(WS2812_RIGHT_PIN);
}

void ws2812_right_pin_low(void)
{
    WS2812_GPIO_LOW(WS2812_RIGHT_PIN);
}

void ws2812_status(void)
{
    rt_tick_t now;
    rt_uint8_t channel;

    now = rt_tick_get();
    rt_kprintf("WS2811 ready=%u motion=%u mask=0x%02x remain=%ld out_mask=0x%02x rgb=%02x,%02x,%02x\r\n",
               ws2812_ready,
               ws2812_motion_active,
               ws2812_motion_mask,
               (long)((rt_int32_t)(ws2812_motion_until - now)),
               ws2812_output_last_mask,
               ws2812_output_last_red,
               ws2812_output_last_green,
               ws2812_output_last_blue);
    for (channel = 0; channel < WS2812_RADAR_CHANNELS; channel++)
    {
        rt_kprintf("  radar ch%u active=%u age=%lu\r\n",
                   channel,
                   ws2812_radar_channels[channel].active,
                   (unsigned long)(now - ws2812_radar_channels[channel].updated_tick));
    }
}
void ws2812_gpio_status(void)
{
    rt_kprintf("GPIOE MODER=0x%08x OTYPER=0x%08x OSPEEDR=0x%08x PUPDR=0x%08x IDR=0x%08x ODR=0x%08x AFR0=0x%08x\n",
               (unsigned int)GPIOE->MODER,
               (unsigned int)GPIOE->OTYPER,
               (unsigned int)GPIOE->OSPEEDR,
               (unsigned int)GPIOE->PUPDR,
               (unsigned int)GPIOE->IDR,
               (unsigned int)GPIOE->ODR,
               (unsigned int)GPIOE->AFR[0]);
}

#if WS2812_AUTO_DEMO_ENABLE
static void ws2812_auto_demo_entry(void *parameter)
{
    RT_UNUSED(parameter);

    rt_thread_mdelay(1000);
    ws2812_demo();
}
#endif

int ws2812_init(void)
{
    GPIO_InitTypeDef gpio_init = {0};
    rt_thread_t demo_thread;

    WS2812_GPIO_CLK_ENABLE();

    gpio_init.Pin = WS2812_PIN;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(WS2812_PORT, &gpio_init);

    WS2812_GPIO_LOW(WS2812_PIN);
    ws2812_dwt_init();
    ws2812_timing_init();
    if (rt_mutex_init(&ws2812_lock, "wslock", RT_IPC_FLAG_FIFO) == RT_EOK)
    {
        ws2812_lock_ready = RT_TRUE;
    }
    else
    {
        LOG_E("WS2812 lock init failed");
        return -RT_ERROR;
    }

    ws2812_ready = RT_TRUE;

    ws2812_delay_cycles(ws2812_reset_cycles);

    demo_thread = rt_thread_create(WS2812_RADAR_THREAD_NAME,
                                   ws2812_radar_thread_entry,
                                   RT_NULL,
                                   WS2812_RADAR_THREAD_STACK,
                                   WS2812_RADAR_THREAD_PRIORITY,
                                   WS2812_RADAR_THREAD_TICK);
    if (demo_thread != RT_NULL)
    {
        rt_thread_startup(demo_thread);
    }
    else
    {
        LOG_E("WS2812 radar thread create failed");
    }

#if WS2812_AUTO_DEMO_ENABLE
    demo_thread = rt_thread_create("ws2812",
                                   ws2812_auto_demo_entry,
                                   RT_NULL,
                                   1024,
                                   20,
                                   10);
    if (demo_thread != RT_NULL)
    {
        rt_thread_startup(demo_thread);
    }
    else
    {
        LOG_E("WS2812 auto demo thread create failed");
    }
#endif

    return RT_EOK;
}
//INIT_APP_EXPORT(ws2812_init);

#if defined(RT_USING_FINSH)
MSH_CMD_EXPORT(ws2812_demo, ws2812 demo test);
MSH_CMD_EXPORT(ws2812_left_all, ws2812 left all on);
MSH_CMD_EXPORT(ws2812_right_all, ws2812 right all on);
MSH_CMD_EXPORT(ws2812_left_red, ws2812 left red test);
MSH_CMD_EXPORT(ws2812_left_green, ws2812 left green test);
MSH_CMD_EXPORT(ws2812_left_blue, ws2812 left blue test);
MSH_CMD_EXPORT(ws2812_left_white, ws2812 left white test);
MSH_CMD_EXPORT(ws2812_right_red, ws2812 right red test);
MSH_CMD_EXPORT(ws2812_right_green, ws2812 right green test);
MSH_CMD_EXPORT(ws2812_right_blue, ws2812 right blue test);
MSH_CMD_EXPORT(ws2812_right_white, ws2812 right white test);
MSH_CMD_EXPORT(ws2812_motion_left_test, ws2812 motion left path test);
MSH_CMD_EXPORT(ws2812_motion_right_test, ws2812 motion right path test);
MSH_CMD_EXPORT(ws2812_all_off, ws2812 all off);
MSH_CMD_EXPORT(ws2812_right_pin_high, ws2812 right pin high);
MSH_CMD_EXPORT(ws2812_right_pin_low, ws2812 right pin low);
MSH_CMD_EXPORT(ws2812_status, ws2812 state status);
MSH_CMD_EXPORT(ws2812_gpio_status, ws2812 gpio status);
#endif
