#ifndef APPLICATIONS_WS2812_H__
#define APPLICATIONS_WS2812_H__

#include <rtthread.h>

int ws2812_init(void);
rt_err_t ws2812_show_rgb(rt_uint8_t red, rt_uint8_t green, rt_uint8_t blue);
rt_err_t ws2812_fill_rgb(rt_uint8_t red, rt_uint8_t green, rt_uint8_t blue, rt_size_t led_count);
rt_err_t ws2812_show_grb(const rt_uint8_t *grb, rt_size_t led_count);
void ws2812_radar_report(rt_uint8_t channel, rt_bool_t active, rt_uint8_t distance_m);
void ws2812_motion_report(rt_bool_t left_active,
                          rt_bool_t right_active,
                          rt_bool_t flash,
                          rt_uint32_t hold_ms);
void ws2812_demo(void);

#endif
