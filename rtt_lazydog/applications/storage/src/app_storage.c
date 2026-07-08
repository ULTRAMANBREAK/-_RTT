/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>
#include <string.h>
#include <fal.h>

#include "app_config.h"
#include "app_storage.h"

#define DBG_TAG "app.storage"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#if defined(RT_USING_FINSH)
#include <finsh.h>
#endif

#define APP_STORAGE_PARTITION      "param"
#define APP_STORAGE_MAGIC          0x53544731U
#define APP_STORAGE_VERSION        2U
#define APP_STORAGE_WRITTEN        1U
#define APP_STORAGE_ERASE_SIZE     4096

#ifndef APP_NFC_UNLOCK_UID0
#define APP_NFC_UNLOCK_UID0        0xDE
#define APP_NFC_UNLOCK_UID1        0xAD
#define APP_NFC_UNLOCK_UID2        0xBE
#define APP_NFC_UNLOCK_UID3        0xEF
#endif

typedef struct
{
    rt_uint32_t magic;
    rt_uint16_t version;
    rt_uint8_t written_flag;
    rt_uint8_t clip_count;
    app_storage_clip_binding_t clips[APP_STORAGE_CLIP_MAX];
    app_storage_device_config_t config;
    rt_uint32_t checksum;
} app_storage_store_t;

static const rt_uint8_t app_storage_unlock_uid[APP_STORAGE_RFID_UID_LEN] = {
    APP_NFC_UNLOCK_UID0,
    APP_NFC_UNLOCK_UID1,
    APP_NFC_UNLOCK_UID2,
    APP_NFC_UNLOCK_UID3,
};

static const app_storage_clip_binding_t app_storage_default_clips[APP_STORAGE_CLIP_MAX] = {
    {1, {0x88, 0x04, 0xCF, 0xFB}, "", "", APP_STORAGE_CLIP_STATUS_UNBOUND, 0},
    {2, {0x88, 0x04, 0xBE, 0x27}, "", "", APP_STORAGE_CLIP_STATUS_UNBOUND, 0},
    {3, {0x88, 0x04, 0xD9, 0x5C}, "", "", APP_STORAGE_CLIP_STATUS_UNBOUND, 0},
    {4, {0x88, 0x04, 0x29, 0xA2}, "", "", APP_STORAGE_CLIP_STATUS_UNBOUND, 0},
};

static const app_storage_device_config_t app_storage_default_config = {
    APP_SENSOR_OVERSPEED_LIMIT_X100,
    APP_RADAR_WARN_DISTANCE_M,
    APP_RADAR_WARN_FLASH_AFTER_MS,
    APP_SENSOR_FALL_TILT_MIN_MG,
    600,
    "LDOG-001",
};

static app_storage_clip_binding_t app_storage_clips[APP_STORAGE_CLIP_MAX];
static app_storage_clip_pending_action_t app_storage_clip_pending_actions[APP_STORAGE_CLIP_MAX];
static app_storage_device_config_t app_storage_config;
static const struct fal_partition *app_storage_part = RT_NULL;
static rt_uint8_t app_storage_clip_count = 0;
static rt_bool_t app_storage_ready = RT_FALSE;

static rt_uint32_t app_storage_checksum(const app_storage_store_t *store)
{
    const rt_uint8_t *bytes = (const rt_uint8_t *)store;
    rt_size_t size = sizeof(*store) - sizeof(store->checksum);
    rt_uint32_t checksum = 0;
    rt_size_t i;

    for (i = 0; i < size; i++)
    {
        checksum += bytes[i];
    }

    return checksum;
}

static void app_storage_load_defaults(void)
{
    memcpy(app_storage_clips, app_storage_default_clips, sizeof(app_storage_default_clips));
    rt_memset(app_storage_clip_pending_actions, 0, sizeof(app_storage_clip_pending_actions));
    memcpy(&app_storage_config, &app_storage_default_config, sizeof(app_storage_config));
    app_storage_clip_count = APP_STORAGE_CLIP_MAX;
}

static void app_storage_fill_store(app_storage_store_t *store)
{
    rt_memset(store, 0xFF, sizeof(*store));
    store->magic = APP_STORAGE_MAGIC;
    store->version = APP_STORAGE_VERSION;
    store->written_flag = APP_STORAGE_WRITTEN;
    store->clip_count = app_storage_clip_count;
    memcpy(store->clips, app_storage_clips, sizeof(app_storage_clips));
    memcpy(&store->config, &app_storage_config, sizeof(store->config));
    store->checksum = app_storage_checksum(store);
}

static rt_bool_t app_storage_store_valid(const app_storage_store_t *store)
{
    if ((store->magic != APP_STORAGE_MAGIC) ||
        (store->version != APP_STORAGE_VERSION) ||
        (store->written_flag != APP_STORAGE_WRITTEN) ||
        (store->clip_count == 0) ||
        (store->clip_count > APP_STORAGE_CLIP_MAX))
    {
        return RT_FALSE;
    }

    return (store->checksum == app_storage_checksum(store)) ? RT_TRUE : RT_FALSE;
}

static rt_err_t app_storage_write_current(void)
{
    app_storage_store_t store;

    if (app_storage_part == RT_NULL)
    {
        return -RT_ERROR;
    }

    app_storage_fill_store(&store);
    if (fal_partition_erase(app_storage_part, 0, APP_STORAGE_ERASE_SIZE) < 0)
    {
        LOG_E("storage flash erase failed");
        return -RT_ERROR;
    }

    if (fal_partition_write(app_storage_part, 0, (const rt_uint8_t *)&store, sizeof(store)) != sizeof(store))
    {
        LOG_E("storage flash write failed");
        return -RT_ERROR;
    }

    return RT_EOK;
}

static rt_int32_t app_storage_find_clip_index(rt_uint8_t card_num)
{
    rt_size_t i;

    for (i = 0; i < app_storage_clip_count; i++)
    {
        if (app_storage_clips[i].card_num == card_num)
        {
            return (rt_int32_t)i;
        }
    }

    return -1;
}

static void app_storage_copy_text(char *dst, rt_size_t dst_size, const char *src)
{
    if ((dst == RT_NULL) || (dst_size == 0) || (src == RT_NULL))
    {
        return;
    }

    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

int app_storage_init(void)
{
    const struct fal_partition *part;
    app_storage_store_t store;

    if (app_storage_ready == RT_TRUE)
    {
        return RT_EOK;
    }

    app_storage_load_defaults();
    app_storage_ready = RT_TRUE;

    fal_init();
    part = fal_partition_find(APP_STORAGE_PARTITION);
    if (part == RT_NULL)
    {
        LOG_W("storage partition %s not found, use defaults", APP_STORAGE_PARTITION);
        return RT_EOK;
    }
    app_storage_part = part;

    if ((fal_partition_read(part, 0, (rt_uint8_t *)&store, sizeof(store)) == sizeof(store)) &&
        (app_storage_store_valid(&store) == RT_TRUE))
    {
        memcpy(app_storage_clips, store.clips, sizeof(app_storage_clips));
        memcpy(&app_storage_config, &store.config, sizeof(app_storage_config));
        app_storage_clip_count = store.clip_count;
        return RT_EOK;
    }

    if (app_storage_write_current() != RT_EOK)
    {
        LOG_W("storage default write failed, use defaults");
    }

    return RT_EOK;
}

rt_bool_t app_storage_uid_is_unlock(const rt_uint8_t uid[APP_STORAGE_RFID_UID_LEN])
{
#if APP_NFC_UNLOCK_ENABLE
    if (uid == RT_NULL)
    {
        return RT_FALSE;
    }

    return (memcmp(uid, app_storage_unlock_uid, APP_STORAGE_RFID_UID_LEN) == 0) ? RT_TRUE : RT_FALSE;
#else
    RT_UNUSED(uid);
    return RT_FALSE;
#endif
}

rt_err_t app_storage_find_card_by_uid(const rt_uint8_t uid[APP_STORAGE_RFID_UID_LEN], rt_uint8_t *card_num)
{
    rt_size_t i;

    if ((uid == RT_NULL) || (card_num == RT_NULL))
    {
        return -RT_EINVAL;
    }

    if (app_storage_ready == RT_FALSE)
    {
        app_storage_init();
    }

    for (i = 0; i < app_storage_clip_count; i++)
    {
        if (memcmp(uid, app_storage_clips[i].uid, APP_STORAGE_RFID_UID_LEN) == 0)
        {
            *card_num = app_storage_clips[i].card_num;
            return RT_EOK;
        }
    }

    return -RT_ERROR;
}

rt_uint8_t app_storage_uid_to_card_num(const rt_uint8_t uid[APP_STORAGE_RFID_UID_LEN])
{
    rt_uint8_t card_num;

    if (app_storage_find_card_by_uid(uid, &card_num) == RT_EOK)
    {
        return card_num;
    }

    if (uid != RT_NULL)
    {
        LOG_W("unknown NFC uid=%02X%02X%02X%02X, fallback cardId=01", uid[0], uid[1], uid[2], uid[3]);
    }
    return 1;
}

rt_err_t app_storage_get_clip_binding(rt_uint8_t card_num, app_storage_clip_binding_t *binding)
{
    rt_int32_t index;

    if (binding == RT_NULL)
    {
        return -RT_EINVAL;
    }

    if (app_storage_ready == RT_FALSE)
    {
        app_storage_init();
    }

    index = app_storage_find_clip_index(card_num);
    if (index < 0)
    {
        return -RT_ERROR;
    }

    *binding = app_storage_clips[index];
    return RT_EOK;
}

rt_err_t app_storage_get_clip_bindings(const app_storage_clip_binding_t **bindings, rt_uint8_t *count)
{
    if ((bindings == RT_NULL) || (count == RT_NULL))
    {
        return -RT_EINVAL;
    }

    if (app_storage_ready == RT_FALSE)
    {
        app_storage_init();
    }

    *bindings = app_storage_clips;
    *count = app_storage_clip_count;
    return RT_EOK;
}

rt_err_t app_storage_update_clip_binding(rt_uint8_t card_num,
                                         rt_uint8_t status,
                                         const char *order_id,
                                         const char *dest_id,
                                         rt_uint32_t bind_time)
{
    rt_int32_t index;
    rt_err_t result;

    if (app_storage_ready == RT_FALSE)
    {
        app_storage_init();
    }

    index = app_storage_find_clip_index(card_num);
    if (index < 0)
    {
        return -RT_ERROR;
    }

    app_storage_clips[index].status = status;
    app_storage_clips[index].bind_time = bind_time;
    app_storage_copy_text(app_storage_clips[index].order_id, sizeof(app_storage_clips[index].order_id), order_id);
    app_storage_copy_text(app_storage_clips[index].dest_id, sizeof(app_storage_clips[index].dest_id), dest_id);

    result = app_storage_write_current();
    if (result != RT_EOK)
    {
        LOG_W("clip %u storage persist failed, RAM state kept", (unsigned int)card_num);
    }

    return RT_EOK;
}

rt_err_t app_storage_finish_clip_binding(rt_uint8_t card_num)
{
    return app_storage_update_clip_binding(card_num,
                                           APP_STORAGE_CLIP_STATUS_DONE,
                                           RT_NULL,
                                           RT_NULL,
                                           rt_tick_get());
}

rt_err_t app_storage_clear_clip_binding(rt_uint8_t card_num)
{
    return app_storage_update_clip_binding(card_num,
                                           APP_STORAGE_CLIP_STATUS_UNBOUND,
                                           "",
                                           "",
                                           0);
}

rt_err_t app_storage_set_clip_pending_action(rt_uint8_t card_num, app_storage_clip_pending_action_t action)
{
    rt_int32_t index;

    if ((action != APP_STORAGE_CLIP_PENDING_NONE) &&
        (action != APP_STORAGE_CLIP_PENDING_BIND) &&
        (action != APP_STORAGE_CLIP_PENDING_UNBIND))
    {
        return -RT_EINVAL;
    }

    if (app_storage_ready == RT_FALSE)
    {
        app_storage_init();
    }

    index = app_storage_find_clip_index(card_num);
    if (index < 0)
    {
        return -RT_ERROR;
    }

    app_storage_clip_pending_actions[index] = action;
    return RT_EOK;
}

rt_err_t app_storage_get_clip_pending_action(rt_uint8_t card_num, app_storage_clip_pending_action_t *action)
{
    rt_int32_t index;

    if (action == RT_NULL)
    {
        return -RT_EINVAL;
    }

    if (app_storage_ready == RT_FALSE)
    {
        app_storage_init();
    }

    index = app_storage_find_clip_index(card_num);
    if (index < 0)
    {
        return -RT_ERROR;
    }

    *action = app_storage_clip_pending_actions[index];
    return RT_EOK;
}

rt_err_t app_storage_clear_clip_pending_action(rt_uint8_t card_num)
{
    return app_storage_set_clip_pending_action(card_num, APP_STORAGE_CLIP_PENDING_NONE);
}

rt_err_t app_storage_get_device_config(app_storage_device_config_t *config)
{
    if (config == RT_NULL)
    {
        return -RT_EINVAL;
    }

    if (app_storage_ready == RT_FALSE)
    {
        app_storage_init();
    }

    memcpy(config, &app_storage_config, sizeof(*config));
    return RT_EOK;
}

const char *app_storage_clip_status_name(rt_uint8_t status)
{
    switch (status)
    {
    case APP_STORAGE_CLIP_STATUS_UNBOUND:
        return "unbound";
    case APP_STORAGE_CLIP_STATUS_BOUND:
        return "bound";
    case APP_STORAGE_CLIP_STATUS_ARRIVED:
        return "arrived";

    case APP_STORAGE_CLIP_STATUS_DONE:
        return "done";
    default:
        return "unknown";
    }
}

#if defined(RT_USING_FINSH)
static void app_storage_print_clip(const app_storage_clip_binding_t *clip)
{
    rt_kprintf("clip=%02u uid=%02X%02X%02X%02X status=%s order=%s dest=%s bind=%lu\r\n",
               (unsigned int)clip->card_num,
               clip->uid[0],
               clip->uid[1],
               clip->uid[2],
               clip->uid[3],
               app_storage_clip_status_name(clip->status),
               clip->order_id,
               clip->dest_id,
               (unsigned long)clip->bind_time);
}

static void clip_bindings(void)
{
    const app_storage_clip_binding_t *clips;
    rt_uint8_t count;
    rt_uint8_t i;

    if (app_storage_get_clip_bindings(&clips, &count) != RT_EOK)
    {
        rt_kprintf("clip bindings unavailable\r\n");
        return;
    }

    for (i = 0; i < count; i++)
    {
        app_storage_print_clip(&clips[i]);
    }
}
MSH_CMD_EXPORT(clip_bindings, show persisted clip binding table);

static void clip_binding_release(int argc, char *argv[])
{
    rt_uint8_t card_num;

    if (argc != 2 || argv[1] == RT_NULL ||
        argv[1][0] < '0' || argv[1][0] > '9' ||
        argv[1][1] < '0' || argv[1][1] > '9' ||
        argv[1][2] != '\0')
    {
        rt_kprintf("usage: clip_binding_release <01-04>\r\n");
        return;
    }

    card_num = (rt_uint8_t)(((argv[1][0] - '0') * 10) + (argv[1][1] - '0'));
    if (app_storage_clear_clip_binding(card_num) != RT_EOK)
    {
        rt_kprintf("clip %02u release failed\r\n", (unsigned int)card_num);
        return;
    }

    rt_kprintf("clip %02u released\r\n", (unsigned int)card_num);
}
MSH_CMD_EXPORT(clip_binding_release, release clip binding to unbound);

static void clip_bindings_reset(void)
{
    app_storage_load_defaults();
    if (app_storage_write_current() != RT_EOK)
    {
        rt_kprintf("clip bindings reset in RAM only\r\n");
        return;
    }

    rt_kprintf("clip bindings reset\r\n");
}
MSH_CMD_EXPORT(clip_bindings_reset, reset clip binding defaults);
#endif