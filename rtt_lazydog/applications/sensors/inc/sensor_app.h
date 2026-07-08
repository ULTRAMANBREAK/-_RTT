/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APPLICATIONS_SENSOR_APP_H__
#define APPLICATIONS_SENSOR_APP_H__

#include <rtthread.h>
#include "app_config.h"

typedef enum
{
    SENSOR_MOTION_UNKNOWN = 0,
    SENSOR_MOTION_NORMAL,
    SENSOR_MOTION_TURN_LEFT,
    SENSOR_MOTION_TURN_RIGHT,
    SENSOR_MOTION_FALL,
} sensor_motion_t;

typedef struct
{
    rt_bool_t icm_ok;
    rt_bool_t aht_ok;
    rt_bool_t ap_ok;
    rt_int16_t accel_x;
    rt_int16_t accel_y;
    rt_int16_t accel_z;
    rt_int16_t gyro_x;
    rt_int16_t gyro_y;
    rt_int16_t gyro_z;
    float temperature;
    float humidity;
    rt_bool_t dht11_valid[APP_SENSOR_DHT11_CHANNELS];
    rt_int8_t dht11_temperature[APP_SENSOR_DHT11_CHANNELS];
    rt_uint8_t dht11_humidity[APP_SENSOR_DHT11_CHANNELS];
    float ambient_light;
    rt_uint16_t proximity;
    sensor_motion_t motion;
    rt_int16_t gyro_z_dps;
    rt_uint16_t accel_x_mg;
    rt_uint16_t accel_y_mg;
    rt_uint16_t accel_z_mg;
    rt_uint16_t tilt_mg;
    rt_int16_t turn_angle_deg_x10;
    rt_bool_t speed_valid;
    long speed_kmh_x100;
    rt_bool_t overspeed;
    rt_bool_t fall_detected;
    rt_uint32_t tick;
} sensor_msg_t;

typedef struct
{
    rt_bool_t valid;
    sensor_motion_t motion;
    rt_int16_t accel_x;
    rt_int16_t accel_y;
    rt_int16_t accel_z;
    rt_int16_t gyro_z;
    rt_int16_t gyro_z_dps;
    rt_uint16_t accel_x_mg;
    rt_uint16_t accel_y_mg;
    rt_uint16_t accel_z_mg;
    rt_uint16_t tilt_mg;
    rt_int16_t turn_angle_deg_x10;
    rt_bool_t speed_valid;
    long speed_kmh_x100;
    rt_bool_t overspeed;
    rt_bool_t fall_detected;
    rt_uint32_t tick;
} sensor_motion_status_t;

rt_mq_t sensor_app_get_mq(void);
rt_err_t sensor_app_get_motion(sensor_motion_status_t *status);
const char *sensor_motion_name(sensor_motion_t motion);
int sensor_app_init(void);

#endif
