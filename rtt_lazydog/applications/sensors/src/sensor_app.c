/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>
#include <rtdevice.h>

#include "sensor_app.h"
#include "app_config.h"
#include "atgm336h.h"
#include "buzzer.h"
#include "ws2812.h"
#include "uart_app.h"
#include "app_storage.h"
#include "icm20608.h"
#include "aht10.h"
#include "ap3216c.h"
#include "sensor_dallas_dht11.h"

#define DBG_TAG "sensor.app"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#define ICM20608_I2C_BUS_NAME   "i2c2"
#define AHT_I2C_BUS_NAME        "i2c3"
#define AP3216C_I2C_BUS_NAME    "i2c2"

#define SENSOR_THREAD_STACK_SIZE    2048
#define SENSOR_THREAD_PRIORITY      15
#define SENSOR_THREAD_TICK          10
#define SENSOR_MQ_DEPTH             4

#if APP_SENSOR_DHT11_CHANNELS != UART_APP_TEMP_CHANNELS
#error "DHT11 channel count must match UART temp channel count"
#endif

static rt_mq_t sensor_mq = RT_NULL;
static struct rt_mutex sensor_motion_lock;
static rt_bool_t sensor_motion_lock_ready = RT_FALSE;
static sensor_motion_status_t sensor_motion_latest;
static rt_uint32_t sensor_overspeed_limit_x100 = APP_SENSOR_OVERSPEED_LIMIT_X100;
static rt_uint32_t sensor_fall_tilt_min_mg = APP_SENSOR_FALL_TILT_MIN_MG;

static void sensor_load_storage_config(void)
{
    app_storage_device_config_t config;

    if (app_storage_get_device_config(&config) != RT_EOK)
    {
        return;
    }

    if (config.overspeed_limit_x100 > 0)
    {
        sensor_overspeed_limit_x100 = config.overspeed_limit_x100;
    }

    if (config.fall_tilt_min_mg > 0)
    {
        sensor_fall_tilt_min_mg = config.fall_tilt_min_mg;
    }
}

typedef struct
{
    const char *name;
    rt_base_t pin;
} sensor_dht11_channel_t;

static const sensor_dht11_channel_t sensor_dht11_channels[APP_SENSOR_DHT11_CHANNELS] = {
    {"FL", APP_SENSOR_DHT11_FL_PIN},
    {"FR", APP_SENSOR_DHT11_FR_PIN},
    {"BL", APP_SENSOR_DHT11_BL_PIN},
    {"BR", APP_SENSOR_DHT11_BR_PIN},
};

rt_mq_t sensor_app_get_mq(void)
{
    return sensor_mq;
}

const char *sensor_motion_name(sensor_motion_t motion)
{
    switch (motion)
    {
    case SENSOR_MOTION_NORMAL:
        return "normal";
    case SENSOR_MOTION_TURN_LEFT:
        return "turn_left";
    case SENSOR_MOTION_TURN_RIGHT:
        return "turn_right";
    case SENSOR_MOTION_FALL:
        return "fall";
    case SENSOR_MOTION_UNKNOWN:
    default:
        return "unknown";
    }
}

rt_err_t sensor_app_get_motion(sensor_motion_status_t *status)
{
    if (status == RT_NULL)
    {
        return -RT_EINVAL;
    }

    if (!sensor_motion_lock_ready)
    {
        return -RT_ERROR;
    }

    rt_mutex_take(&sensor_motion_lock, RT_WAITING_FOREVER);
    *status = sensor_motion_latest;
    rt_mutex_release(&sensor_motion_lock);

    return status->valid ? RT_EOK : -RT_ERROR;
}

static rt_uint16_t sensor_abs_raw_to_mg(rt_int16_t raw)
{
    rt_int32_t value = raw;

    if (value < 0)
    {
        value = -value;
    }

    return (rt_uint16_t)((value * 1000) / APP_SENSOR_ACCEL_1G_RAW);
}

static rt_uint16_t sensor_motion_calc_tilt_mg(rt_uint16_t accel_x_mg, rt_uint16_t accel_y_mg)
{
    rt_uint16_t max_v;
    rt_uint16_t min_v;

    if (accel_x_mg >= accel_y_mg)
    {
        max_v = accel_x_mg;
        min_v = accel_y_mg;
    }
    else
    {
        max_v = accel_y_mg;
        min_v = accel_x_mg;
    }

    return (rt_uint16_t)(max_v + (min_v >> 1));
}

static rt_int16_t sensor_tilt_mg_to_angle_x10(rt_uint16_t tilt_mg)
{
    static const rt_uint16_t tilt_table_mg[] = {0, 174, 342, 500, 643, 766, 866, 940, 985, 1000};
    static const rt_int16_t angle_table_x10[] = {0, 100, 200, 300, 400, 500, 600, 700, 800, 900};
    rt_size_t i;

    if (tilt_mg >= 1000U)
    {
        return 900;
    }

    for (i = 1; i < sizeof(tilt_table_mg) / sizeof(tilt_table_mg[0]); i++)
    {
        if (tilt_mg <= tilt_table_mg[i])
        {
            rt_uint16_t mg0 = tilt_table_mg[i - 1];
            rt_uint16_t mg1 = tilt_table_mg[i];
            rt_int16_t deg0 = angle_table_x10[i - 1];
            rt_int16_t deg1 = angle_table_x10[i];

            return (rt_int16_t)(deg0 + (((rt_int32_t)(tilt_mg - mg0) * (deg1 - deg0)) / (mg1 - mg0)));
        }
    }

    return 0;
}
static rt_int16_t sensor_gyro_raw_to_dps(rt_int16_t raw)
{
    rt_int32_t value = raw;

    value *= APP_SENSOR_GYRO_Z_RIGHT_SIGN;
    return (rt_int16_t)(value / APP_SENSOR_GYRO_LSB_PER_DPS);
}

static rt_int16_t sensor_abs_i16(rt_int16_t value)
{
    return (value >= 0) ? value : (rt_int16_t)(-value);
}
static sensor_motion_t sensor_motion_judge_acc_x_turn(const sensor_msg_t *msg)
{
    static rt_tick_t turn_mutex_until = 0;
    rt_tick_t now = rt_tick_get();
    sensor_motion_t motion = SENSOR_MOTION_NORMAL;

    if ((rt_int32_t)(turn_mutex_until - now) > 0)
    {
        return SENSOR_MOTION_NORMAL;
    }

    if (msg->accel_x >= APP_SENSOR_TURN_ACC_X_LEFT_RAW)
    {
        motion = SENSOR_MOTION_TURN_LEFT;
    }
    else if (msg->accel_x <= APP_SENSOR_TURN_ACC_X_RIGHT_RAW)
    {
        motion = SENSOR_MOTION_TURN_RIGHT;
    }

    if (motion != SENSOR_MOTION_NORMAL)
    {
        turn_mutex_until = now + rt_tick_from_millisecond(APP_SENSOR_TURN_LIGHT_HOLD_MS);
    }

    return motion;
}
static sensor_motion_t sensor_motion_judge(sensor_msg_t *msg)
{
    static sensor_motion_t last_motion = SENSOR_MOTION_UNKNOWN;
    static rt_uint8_t fall_count = 0;
    static rt_bool_t overspeed_active = RT_FALSE;
    rt_bool_t fall_sample;
    rt_bool_t speed_ok = RT_FALSE;
    long speed_kmh_x100 = 0;
    sensor_motion_t motion = SENSOR_MOTION_NORMAL;
    rt_err_t result;

    msg->accel_x_mg = sensor_abs_raw_to_mg(msg->accel_x);
    msg->accel_y_mg = sensor_abs_raw_to_mg(msg->accel_y);
    msg->accel_z_mg = sensor_abs_raw_to_mg(msg->accel_z);
    msg->tilt_mg = sensor_motion_calc_tilt_mg(msg->accel_x_mg, msg->accel_y_mg);
    msg->gyro_z_dps = sensor_gyro_raw_to_dps(msg->gyro_z);
    msg->speed_valid = RT_FALSE;
    msg->speed_kmh_x100 = 0;
    msg->overspeed = RT_FALSE;

    result = atgm336h_get_speed_kmh_x100(&speed_kmh_x100);
    if (result == RT_EOK)
    {
        msg->speed_valid = RT_TRUE;
        msg->speed_kmh_x100 = speed_kmh_x100;
        speed_ok = RT_TRUE;
    }

#if APP_SENSOR_OVERSPEED_ENABLE
    if (speed_ok)
    {
        if (speed_kmh_x100 >= (long)sensor_overspeed_limit_x100)
        {
            overspeed_active = RT_TRUE;
        }
        else if (speed_kmh_x100 <= (long)((sensor_overspeed_limit_x100 > APP_SENSOR_OVERSPEED_HYST_X100) ? (sensor_overspeed_limit_x100 - APP_SENSOR_OVERSPEED_HYST_X100) : 0))
        {
            overspeed_active = RT_FALSE;
        }
        msg->overspeed = overspeed_active;
    }
    else
    {
        overspeed_active = RT_FALSE;
    }
#endif


    fall_sample = RT_FALSE;
#if APP_SENSOR_FALL_ENABLE
    fall_sample = (msg->tilt_mg >= sensor_fall_tilt_min_mg) ||
                  ((msg->accel_z_mg <= APP_SENSOR_FALL_Z_MAX_MG) &&
                   ((msg->accel_x_mg >= APP_SENSOR_FALL_XY_MIN_MG) ||
                    (msg->accel_y_mg >= APP_SENSOR_FALL_XY_MIN_MG)));
#endif
    if (fall_sample)
    {
        if (fall_count < APP_SENSOR_FALL_CONFIRM_COUNT)
        {
            fall_count++;
        }
    }
    else
    {
        fall_count = 0;
    }

    msg->fall_detected = (fall_count >= APP_SENSOR_FALL_CONFIRM_COUNT);
    if (fall_sample || msg->fall_detected)
    {
        msg->turn_angle_deg_x10 = sensor_tilt_mg_to_angle_x10(msg->tilt_mg);
    }

    if (msg->fall_detected)
    {
        motion = SENSOR_MOTION_FALL;
    }
    else
    {
        motion = sensor_motion_judge_acc_x_turn(msg);
    }

#if APP_SENSOR_MOTION_LOG_ENABLE
    if (motion != last_motion)
    {
        rt_int16_t angle_abs = sensor_abs_i16(msg->turn_angle_deg_x10);

        LOG_W("motion %s speed=%ld.%02ldkm/h angle=%s%d.%ddeg tilt=%umg gyro_z=%ddps acc_mg x=%u y=%u z=%u",
              sensor_motion_name(motion),
              speed_kmh_x100 / 100L,
              speed_kmh_x100 % 100L,
              (msg->turn_angle_deg_x10 < 0) ? "-" : "",
              angle_abs / 10,
              angle_abs % 10,
              msg->tilt_mg,
              msg->gyro_z_dps,
              msg->accel_x_mg,
              msg->accel_y_mg,
              msg->accel_z_mg);
    }
#endif

    last_motion = motion;
    return motion;
}

static void sensor_motion_update_latest(const sensor_msg_t *msg)
{
    sensor_motion_status_t status;

    if (!sensor_motion_lock_ready)
    {
        return;
    }

    rt_memset(&status, 0, sizeof(status));
    status.valid = msg->icm_ok;
    status.motion = msg->motion;
    status.accel_x = msg->accel_x;
    status.accel_y = msg->accel_y;
    status.accel_z = msg->accel_z;
    status.gyro_z = msg->gyro_z;
    status.gyro_z_dps = msg->gyro_z_dps;
    status.accel_x_mg = msg->accel_x_mg;
    status.accel_y_mg = msg->accel_y_mg;
    status.accel_z_mg = msg->accel_z_mg;
    status.tilt_mg = msg->tilt_mg;
    status.turn_angle_deg_x10 = msg->turn_angle_deg_x10;
    status.speed_valid = msg->speed_valid;
    status.speed_kmh_x100 = msg->speed_kmh_x100;
    status.overspeed = msg->overspeed;
    status.fall_detected = msg->fall_detected;
    status.tick = msg->tick;

    rt_mutex_take(&sensor_motion_lock, RT_WAITING_FOREVER);
    sensor_motion_latest = status;
    rt_mutex_release(&sensor_motion_lock);
}

static void sensor_motion_report_esp32(const sensor_msg_t *msg)
{
    static rt_bool_t last_fall = RT_FALSE;

    if ((msg->fall_detected == RT_TRUE) && (last_fall == RT_FALSE))
    {
        uart_app_send_fall();
    }
    last_fall = msg->fall_detected;
}

static void sensor_motion_update_buzzer(const sensor_msg_t *msg)
{
    static buzzer_pattern_t hold_pattern = BUZZER_PATTERN_OFF;
    static rt_tick_t hold_until = 0;
    buzzer_pattern_t pattern;
    rt_tick_t now = rt_tick_get();

    if (msg->fall_detected == RT_TRUE)
    {
        hold_pattern = BUZZER_PATTERN_FALL;
        hold_until = now + rt_tick_from_millisecond(APP_SENSOR_FALL_BUZZER_HOLD_MS);
    }
    else if (msg->overspeed == RT_TRUE)
    {
        hold_pattern = BUZZER_PATTERN_WARN;
        hold_until = now + rt_tick_from_millisecond(APP_SENSOR_OVERSPEED_HOLD_MS);
    }
    else
    {
        switch (msg->motion)
        {
        case SENSOR_MOTION_TURN_LEFT:
        case SENSOR_MOTION_TURN_RIGHT:
            break;
        case SENSOR_MOTION_FALL:
            hold_pattern = BUZZER_PATTERN_FALL;
            hold_until = now + rt_tick_from_millisecond(APP_SENSOR_FALL_BUZZER_HOLD_MS);
            break;
        case SENSOR_MOTION_NORMAL:
        case SENSOR_MOTION_UNKNOWN:
        default:
            break;
        }
    }

    if ((hold_pattern != BUZZER_PATTERN_OFF) && ((rt_int32_t)(hold_until - now) > 0))
    {
        pattern = hold_pattern;
    }
    else
    {
        hold_pattern = BUZZER_PATTERN_OFF;
        pattern = BUZZER_PATTERN_OFF;
    }

    buzzer_set_pattern(pattern);
}

static void sensor_motion_update_lights(sensor_motion_t motion)
{
    switch (motion)
    {
    case SENSOR_MOTION_TURN_LEFT:
        ws2812_motion_report(RT_TRUE, RT_FALSE, RT_FALSE, APP_SENSOR_TURN_LIGHT_HOLD_MS);
        break;
    case SENSOR_MOTION_TURN_RIGHT:
        ws2812_motion_report(RT_FALSE, RT_TRUE, RT_FALSE, APP_SENSOR_TURN_LIGHT_HOLD_MS);
        break;
    case SENSOR_MOTION_FALL:
        ws2812_motion_report(RT_TRUE, RT_TRUE, RT_TRUE, APP_SENSOR_FALL_BUZZER_HOLD_MS);
        break;
    case SENSOR_MOTION_NORMAL:
    case SENSOR_MOTION_UNKNOWN:
    default:
        break;
    }
}

static rt_int32_t sensor_dht11_temp_to_x10(rt_int8_t value)
{
    return (rt_int32_t)value * 10;
}

static void sensor_dht11_init_all(rt_bool_t valid[APP_SENSOR_DHT11_CHANNELS])
{
    rt_size_t i;

    for (i = 0; i < APP_SENSOR_DHT11_CHANNELS; i++)
    {
        if (dht11_init(sensor_dht11_channels[i].pin) == CONNECT_SUCCESS)
        {
            valid[i] = RT_TRUE;
        }
        else
        {
            valid[i] = RT_FALSE;
            LOG_E("DHT11 %s init failed", sensor_dht11_channels[i].name);
        }
    }
}

static void sensor_dht11_copy_latest(sensor_msg_t *msg,
                                      const rt_bool_t valid[APP_SENSOR_DHT11_CHANNELS],
                                      const rt_int8_t temperature[APP_SENSOR_DHT11_CHANNELS],
                                      const rt_uint8_t humidity[APP_SENSOR_DHT11_CHANNELS])
{
    rt_size_t i;

    for (i = 0; i < APP_SENSOR_DHT11_CHANNELS; i++)
    {
        msg->dht11_valid[i] = valid[i];
        msg->dht11_temperature[i] = temperature[i];
        msg->dht11_humidity[i] = humidity[i];
    }
}

static void sensor_dht11_read_all(rt_bool_t valid[APP_SENSOR_DHT11_CHANNELS],
                                  rt_int8_t temperature[APP_SENSOR_DHT11_CHANNELS],
                                  rt_uint8_t humidity[APP_SENSOR_DHT11_CHANNELS],
                                  rt_int32_t temp_x10[APP_SENSOR_DHT11_CHANNELS])
{
    rt_size_t i;

    for (i = 0; i < APP_SENSOR_DHT11_CHANNELS; i++)
    {
        uint8_t temp = 0;
        uint8_t humi = 0;

        if (dht11_read(sensor_dht11_channels[i].pin, &temp, &humi) != CONNECT_SUCCESS)
        {
            valid[i] = RT_FALSE;
            temp_x10[i] = 0;
            continue;
        }

        valid[i] = RT_TRUE;
        temperature[i] = (rt_int8_t)temp;
        humidity[i] = humi;
        temp_x10[i] = sensor_dht11_temp_to_x10(temperature[i]);

#if APP_SENSOR_DHT11_LOG_ENABLE
        LOG_I("DHT11 %s temp=%dC humi=%u%%",
              sensor_dht11_channels[i].name,
              temperature[i],
              humidity[i]);
#endif
    }
}

static void sensor_read_entry(void *parameter)
{
    icm20608_device_t icm_dev = RT_NULL;
    aht10_device_t aht_dev = RT_NULL;
    ap3216c_device_t ap_dev = RT_NULL;
    rt_err_t result;
    rt_uint32_t refresh_count = 0;
    rt_uint32_t env_divider;
    float latest_temperature = 0.0f;
    float latest_humidity = 0.0f;
    float latest_ambient_light = 0.0f;
    rt_uint16_t latest_proximity = 0;
    rt_bool_t dht11_valid[APP_SENSOR_DHT11_CHANNELS] = {RT_FALSE};
    rt_int8_t dht11_temperature[APP_SENSOR_DHT11_CHANNELS] = {0};
    rt_uint8_t dht11_humidity[APP_SENSOR_DHT11_CHANNELS] = {0};
    rt_int32_t dht11_temp_x10[APP_SENSOR_DHT11_CHANNELS] = {0};

    RT_UNUSED(parameter);

    env_divider = APP_SENSOR_ENV_REFRESH_MS / APP_SENSOR_REFRESH_MS;
    if (env_divider == 0)
    {
        env_divider = 1;
    }

    icm_dev = icm20608_init(ICM20608_I2C_BUS_NAME);
    if (icm_dev == RT_NULL)
    {
        LOG_E("ICM20608 init failed");
    }
    else
    {
        LOG_D("ICM20608 init success");
        result = icm20608_calib_level(icm_dev, 10);
        if (result == RT_EOK)
        {
            LOG_D("ICM20608 calibrate success");
        }
        else
        {
            LOG_E("ICM20608 calibrate failed");
        }
    }

    ap_dev = ap3216c_init(AP3216C_I2C_BUS_NAME);
    if (ap_dev == RT_NULL)
    {
        LOG_E("AP3216C init failed");
    }
    else
    {
        LOG_D("AP3216C init success");
    }

    rt_thread_mdelay(2000);

    aht_dev = aht10_init(AHT_I2C_BUS_NAME);
    if (aht_dev == RT_NULL)
    {
        LOG_E("AHT10/AHT21 init failed");
    }
    else
    {
        LOG_D("AHT10/AHT21 init success");
    }

    sensor_dht11_init_all(dht11_valid);

    while (1)
    {
        sensor_msg_t msg;

        rt_memset(&msg, 0, sizeof(msg));
        msg.icm_ok = (icm_dev != RT_NULL);
        msg.aht_ok = (aht_dev != RT_NULL);
        msg.ap_ok = (ap_dev != RT_NULL);
        msg.tick = ++refresh_count;

        if (icm_dev != RT_NULL)
        {
            result = icm20608_get_accel(icm_dev, &msg.accel_x, &msg.accel_y, &msg.accel_z);
            if (result != RT_EOK)
            {
                LOG_E("ICM20608 read accel failed");
                msg.icm_ok = RT_FALSE;
            }

            result = icm20608_get_gyro(icm_dev, &msg.gyro_x, &msg.gyro_y, &msg.gyro_z);
            if (result != RT_EOK)
            {
                LOG_E("ICM20608 read gyro failed");
                msg.icm_ok = RT_FALSE;
            }

            if (msg.icm_ok)
            {
                msg.motion = sensor_motion_judge(&msg);
            }
        }

        sensor_dht11_copy_latest(&msg, dht11_valid, dht11_temperature, dht11_humidity);
        sensor_motion_update_latest(&msg);
        sensor_motion_report_esp32(&msg);
        sensor_motion_update_buzzer(&msg);
        sensor_motion_update_lights(msg.motion);

        if ((refresh_count == 1) || ((refresh_count % env_divider) == 0))
        {
            if (aht_dev != RT_NULL)
            {
                latest_temperature = aht10_read_temperature(aht_dev);
                latest_humidity = aht10_read_humidity(aht_dev);
            }

            if (ap_dev != RT_NULL)
            {
                latest_ambient_light = ap3216c_read_ambient_light(ap_dev);
                latest_proximity = ap3216c_read_ps_data(ap_dev);
            }

            sensor_dht11_read_all(dht11_valid, dht11_temperature, dht11_humidity, dht11_temp_x10);
            uart_app_update_temps(dht11_valid, dht11_temp_x10);
            sensor_dht11_copy_latest(&msg, dht11_valid, dht11_temperature, dht11_humidity);
        }

        msg.temperature = latest_temperature;
        msg.humidity = latest_humidity;
        msg.ambient_light = latest_ambient_light;
        msg.proximity = latest_proximity;

        result = rt_mq_send(sensor_mq, &msg, sizeof(msg));
        if (result == -RT_EFULL)
        {
            sensor_msg_t dropped;

            if (rt_mq_recv(sensor_mq, &dropped, sizeof(dropped), 0) == RT_EOK)
            {
                result = rt_mq_send(sensor_mq, &msg, sizeof(msg));
            }
        }
        if (result != RT_EOK)
        {
            LOG_E("sensor message send failed: %d", result);
        }

        rt_thread_mdelay(APP_SENSOR_REFRESH_MS);
    }
}

static int sensor_motion_status(void)
{
    sensor_motion_status_t status;
    rt_int16_t angle_abs;

    if (sensor_app_get_motion(&status) != RT_EOK)
    {
        rt_kprintf("sensor motion unavailable\r\n");
        return -RT_ERROR;
    }

    angle_abs = sensor_abs_i16(status.turn_angle_deg_x10);
    rt_kprintf("motion=%s fall=%u overspeed=%u angle=%s%d.%ddeg gyro_z=%d raw=%d acc_raw x=%d y=%d z=%d acc_mg x=%u y=%u z=%u tick=%lu\r\n",
               sensor_motion_name(status.motion),
               status.fall_detected,
               status.overspeed,
               (status.turn_angle_deg_x10 < 0) ? "-" : "",
               angle_abs / 10,
               angle_abs % 10,
               status.gyro_z_dps,
               status.gyro_z,
               status.accel_x,
               status.accel_y,
               status.accel_z,
               status.accel_x_mg,
               status.accel_y_mg,
               status.accel_z_mg,
               (unsigned long)status.tick);
    if (status.speed_valid)
    {
        rt_kprintf("speed=%ld.%02ldkm/h limit=%lu.%02lukm/h tilt=%umg\r\n",
                   status.speed_kmh_x100 / 100L,
                   status.speed_kmh_x100 % 100L,
                   (unsigned long)(sensor_overspeed_limit_x100 / 100),
                   (unsigned long)(sensor_overspeed_limit_x100 % 100),
                   status.tilt_mg);
    }

    return RT_EOK;
}
MSH_CMD_EXPORT(sensor_motion_status, show icm20608 turn/fall status);

static int sensor_temp_read(int argc, char *argv[])
{
    rt_size_t i;
    rt_bool_t valid[APP_SENSOR_DHT11_CHANNELS] = {RT_FALSE};
    rt_int8_t temperature[APP_SENSOR_DHT11_CHANNELS] = {0};
    rt_uint8_t humidity[APP_SENSOR_DHT11_CHANNELS] = {0};
    rt_int32_t temp_x10[APP_SENSOR_DHT11_CHANNELS] = {0};

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    for (i = 0; i < APP_SENSOR_DHT11_CHANNELS; i++)
    {
        if (dht11_init(sensor_dht11_channels[i].pin) != CONNECT_SUCCESS)
        {
            valid[i] = RT_FALSE;
            rt_kprintf("DHT11 %s pin=%d init failed\r\n",
                       sensor_dht11_channels[i].name,
                       sensor_dht11_channels[i].pin);
        }
    }

    sensor_dht11_read_all(valid, temperature, humidity, temp_x10);
    uart_app_update_temps(valid, temp_x10);

    for (i = 0; i < APP_SENSOR_DHT11_CHANNELS; i++)
    {
        if (valid[i])
        {
            rt_kprintf("DHT11 %s pin=%d temp=%dC humi=%u%%\r\n",
                       sensor_dht11_channels[i].name,
                       sensor_dht11_channels[i].pin,
                       temperature[i],
                       humidity[i]);
        }
        else
        {
            rt_kprintf("DHT11 %s pin=%d read failed\r\n",
                       sensor_dht11_channels[i].name,
                       sensor_dht11_channels[i].pin);
        }
    }

    return RT_EOK;
}
MSH_CMD_EXPORT(sensor_temp_read, read four DHT11 temperatures);

int sensor_app_init(void)
{
    rt_err_t result;
    rt_thread_t thread;

    sensor_load_storage_config();

    sensor_mq = rt_mq_create("sensmq",
                             sizeof(sensor_msg_t),
                             SENSOR_MQ_DEPTH,
                             RT_IPC_FLAG_FIFO);
    if (sensor_mq == RT_NULL)
    {
        LOG_E("sensor mq create failed");
        return -RT_ERROR;
    }

    if (!sensor_motion_lock_ready)
    {
        result = rt_mutex_init(&sensor_motion_lock, "motlock", RT_IPC_FLAG_FIFO);
        if (result != RT_EOK)
        {
            LOG_E("sensor motion lock init failed");
            rt_mq_delete(sensor_mq);
            sensor_mq = RT_NULL;
            return result;
        }
        sensor_motion_lock_ready = RT_TRUE;
        rt_memset(&sensor_motion_latest, 0, sizeof(sensor_motion_latest));
    }

    thread = rt_thread_create("sensor",
                              sensor_read_entry,
                              RT_NULL,
                              SENSOR_THREAD_STACK_SIZE,
                              SENSOR_THREAD_PRIORITY,
                              SENSOR_THREAD_TICK);
    if (thread == RT_NULL)
    {
        LOG_E("sensor thread create failed");
        rt_mutex_detach(&sensor_motion_lock);
        sensor_motion_lock_ready = RT_FALSE;
        rt_mq_delete(sensor_mq);
        sensor_mq = RT_NULL;
        return -RT_ERROR;
    }

    rt_thread_startup(thread);
    return RT_EOK;
}
//INIT_APP_EXPORT(sensor_app_init);
