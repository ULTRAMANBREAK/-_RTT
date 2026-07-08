/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APPLICATIONS_APP_STORAGE_H__
#define APPLICATIONS_APP_STORAGE_H__

#include <rtthread.h>

#define APP_STORAGE_RFID_UID_LEN       4
#define APP_STORAGE_CLIP_MAX           4
#define APP_STORAGE_ORDER_ID_LEN       24
#define APP_STORAGE_DEST_ID_LEN        32
#define APP_STORAGE_DEVICE_ID_LEN      24

typedef enum
{
    APP_STORAGE_CLIP_STATUS_UNBOUND = 0,
    APP_STORAGE_CLIP_STATUS_BOUND,
    APP_STORAGE_CLIP_STATUS_ARRIVED,
    APP_STORAGE_CLIP_STATUS_DONE,
} app_storage_clip_status_t;

typedef enum
{
    APP_STORAGE_CLIP_PENDING_NONE = 0,
    APP_STORAGE_CLIP_PENDING_BIND,
    APP_STORAGE_CLIP_PENDING_UNBIND,
} app_storage_clip_pending_action_t;

typedef struct
{
    rt_uint8_t card_num;
    rt_uint8_t uid[APP_STORAGE_RFID_UID_LEN];
    char order_id[APP_STORAGE_ORDER_ID_LEN];
    char dest_id[APP_STORAGE_DEST_ID_LEN];
    rt_uint8_t status;
    rt_uint32_t bind_time;
} app_storage_clip_binding_t;

typedef struct
{
    rt_uint32_t overspeed_limit_x100;
    rt_uint8_t radar_warn_distance_m;
    rt_uint32_t radar_flash_after_ms;
    rt_uint32_t fall_tilt_min_mg;
    rt_int32_t temp_alarm_x10;
    char device_id[APP_STORAGE_DEVICE_ID_LEN];
} app_storage_device_config_t;

int app_storage_init(void);
rt_bool_t app_storage_uid_is_unlock(const rt_uint8_t uid[APP_STORAGE_RFID_UID_LEN]);
rt_err_t app_storage_find_card_by_uid(const rt_uint8_t uid[APP_STORAGE_RFID_UID_LEN], rt_uint8_t *card_num);
rt_uint8_t app_storage_uid_to_card_num(const rt_uint8_t uid[APP_STORAGE_RFID_UID_LEN]);
rt_err_t app_storage_get_clip_binding(rt_uint8_t card_num, app_storage_clip_binding_t *binding);
rt_err_t app_storage_get_clip_bindings(const app_storage_clip_binding_t **bindings, rt_uint8_t *count);
rt_err_t app_storage_update_clip_binding(rt_uint8_t card_num,
                                         rt_uint8_t status,
                                         const char *order_id,
                                         const char *dest_id,
                                         rt_uint32_t bind_time);
rt_err_t app_storage_finish_clip_binding(rt_uint8_t card_num);
rt_err_t app_storage_clear_clip_binding(rt_uint8_t card_num);
rt_err_t app_storage_set_clip_pending_action(rt_uint8_t card_num, app_storage_clip_pending_action_t action);
rt_err_t app_storage_get_clip_pending_action(rt_uint8_t card_num, app_storage_clip_pending_action_t *action);
rt_err_t app_storage_clear_clip_pending_action(rt_uint8_t card_num);
rt_err_t app_storage_get_device_config(app_storage_device_config_t *config);
const char *app_storage_clip_status_name(rt_uint8_t status);

#endif
