#include <rtthread.h>
#include <string.h>

#include "app_config.h"
#include "uart4_app.h"
#include "rc522_parser.h"
#include "uart_app.h"
#include "app_storage.h"
#include "nrf24_app.h"

#define DBG_TAG "uart4.rc522"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#if defined(RT_USING_FINSH)
#include <finsh.h>
#endif

#define lod_g(...) LOG_I(__VA_ARGS__)

static const rt_uint8_t rc522_frame_head[] = {
    0x04, 0x0C, 0x02, 0x30, 0x00, 0x04, 0x00
};

static rt_uint8_t rc522_match_index = 0;
static rt_uint8_t rc522_uid_index = 0;
static rt_uint8_t rc522_uid_temp[UART4_APP_RFID_UID_LEN];
static rt_uint8_t rc522_last_uid[UART4_APP_RFID_UID_LEN];
static rt_uint8_t rc522_last_unlock_uid[UART4_APP_RFID_UID_LEN];
static rt_bool_t rc522_has_uid = RT_FALSE;
static rt_bool_t rc522_has_unlock = RT_FALSE;
static uart4_app_rfid_card_cb_t rc522_card_callback = RT_NULL;
static rt_uint32_t rc522_rx_total = 0;

static void rc522_log_raw(rt_uint8_t channel, const rt_uint8_t *buf, rt_size_t len)
{
#if APP_UART4_RFID_RAW_LOG_ENABLE
    char hex[(UART4_APP_RAW_LOG_MAX * 3) + 1];
    rt_size_t count;
    rt_size_t i;

    if (buf == RT_NULL || len == 0)
    {
        return;
    }

    count = (len > UART4_APP_RAW_LOG_MAX) ? UART4_APP_RAW_LOG_MAX : len;
    for (i = 0; i < count; i++)
    {
        rt_snprintf(&hex[i * 3], sizeof(hex) - (i * 3), "%02X ", buf[i]);
    }
    hex[count * 3] = '\0';

    rc522_rx_total += len;
    lod_g("RFID ch%u raw rx len=%u total=%u data=%s",
          (unsigned int)channel,
          (unsigned int)len,
          (unsigned int)rc522_rx_total,
          hex);
#else
    RT_UNUSED(channel);
    RT_UNUSED(buf);
    rc522_rx_total += len;
#endif
}


static void rc522_print_uid(const char *prefix, const rt_uint8_t uid[UART4_APP_RFID_UID_LEN])
{
    rt_kprintf("%s uid=%02X %02X %02X %02X\r\n", prefix, uid[0], uid[1], uid[2], uid[3]);
}
static void rc522_log_uid(const char *prefix, const rt_uint8_t uid[UART4_APP_RFID_UID_LEN])
{
#if APP_UART4_RFID_UID_LOG_ENABLE
    lod_g("%s uid=%02X %02X %02X %02X", prefix, uid[0], uid[1], uid[2], uid[3]);
#else
    RT_UNUSED(prefix);
    RT_UNUSED(uid);
#endif
}

#if APP_DELIVERY_TEST_MODE == 1
static void rc522_make_test_text(char *buf, rt_size_t size, const char *prefix, rt_uint8_t card_num)
{
    rt_snprintf(buf, size, "%s-%02u", prefix, (unsigned int)card_num);
}

static void rc522_update_test_binding(rt_uint8_t card_num, rt_bool_t unbind)
{
    char order_id[APP_STORAGE_ORDER_ID_LEN];
    char dest_id[APP_STORAGE_DEST_ID_LEN];

    if (unbind == RT_TRUE)
    {
        app_storage_finish_clip_binding(card_num);
        return;
    }

    rc522_make_test_text(order_id, sizeof(order_id), "TEST", card_num);
    rc522_make_test_text(dest_id, sizeof(dest_id), "DEST", card_num);
    app_storage_update_clip_binding(card_num,
                                    APP_STORAGE_CLIP_STATUS_BOUND,
                                    order_id,
                                    dest_id,
                                    rt_tick_get());
}
#endif

static void rc522_on_unlock_ready(const rt_uint8_t uid[UART4_APP_RFID_UID_LEN])
{
    memcpy(rc522_last_unlock_uid, uid, UART4_APP_RFID_UID_LEN);
    rc522_has_unlock = RT_TRUE;
    rc522_log_uid("RC522 unlock", uid);
    uart_app_send_unlock();
}

static rt_bool_t rc522_should_unbind(rt_uint8_t card_num)
{
    app_storage_clip_pending_action_t action;

    if (app_storage_get_clip_pending_action(card_num, &action) != RT_EOK)
    {
        return RT_FALSE;
    }

    return (action == APP_STORAGE_CLIP_PENDING_UNBIND) ? RT_TRUE : RT_FALSE;
}

static void rc522_on_clip_ready(const rt_uint8_t uid[UART4_APP_RFID_UID_LEN], rt_uint8_t card_num)
{
    rt_bool_t unbind;

    memcpy(rc522_last_uid, uid, UART4_APP_RFID_UID_LEN);
    rc522_has_uid = RT_TRUE;
    rc522_log_uid("RC522 card", uid);

    unbind = rc522_should_unbind(card_num);
#if APP_DELIVERY_TEST_MODE == 1
    rc522_update_test_binding(card_num, unbind);
#endif
    if (uart_app_send_nfc(uid, unbind) == RT_EOK)
    {
        app_storage_clear_clip_pending_action(card_num);
        if (unbind == RT_TRUE)
        {
            rt_err_t result = nrf24_clip_send_cmd_to(card_num, APP_NRF24_CLIP_CMD_OFF);
            if (result != RT_EOK)
            {
                rt_kprintf("[rc522] clip %02u off failed: %d\r\n", (unsigned int)card_num, result);
            }
        }
        uart4_app_enter_drive_mode();
    }

    if (rc522_card_callback != RT_NULL)
    {
        rc522_card_callback(uid);
    }
}

static void rc522_on_uid_ready(const rt_uint8_t uid[UART4_APP_RFID_UID_LEN])
{
    rt_uint8_t card_num;

    if (app_storage_uid_is_unlock(uid) == RT_TRUE)
    {
        rc522_on_unlock_ready(uid);
        return;
    }

    if (app_storage_find_card_by_uid(uid, &card_num) != RT_EOK)
    {
        LOG_W("unknown NFC uid=%02X%02X%02X%02X", uid[0], uid[1], uid[2], uid[3]);
        return;
    }

    rc522_on_clip_ready(uid, card_num);
}

static rt_bool_t rc522_head_byte_match(rt_uint8_t index, rt_uint8_t value)
{
    if (index == 5 && (value == 0x04 || value == 0x44))
    {
        return RT_TRUE;
    }

    return (value == rc522_frame_head[index]) ? RT_TRUE : RT_FALSE;
}

static void rc522_parse_byte(rt_uint8_t value)
{
    if (rc522_match_index < sizeof(rc522_frame_head))
    {
        if (rc522_head_byte_match(rc522_match_index, value) == RT_TRUE)
        {
            rc522_match_index++;
            rc522_uid_index = 0;
            return;
        }

        rc522_match_index = (value == rc522_frame_head[0]) ? 1 : 0;
        rc522_uid_index = 0;
        return;
    }

    rc522_uid_temp[rc522_uid_index++] = value;
    if (rc522_uid_index >= UART4_APP_RFID_UID_LEN)
    {
        rc522_on_uid_ready(rc522_uid_temp);
        rc522_match_index = 0;
        rc522_uid_index = 0;
    }
}

void uart4_app_rfid_parse_buffer(const rt_uint8_t *buf, rt_size_t len)
{
    rt_size_t i;

    if (buf == RT_NULL || len == 0)
    {
        return;
    }

    for (i = 0; i < len; i++)
    {
        rc522_parse_byte(buf[i]);
    }
}

void rc522_parser_input(rt_uint8_t channel, const rt_uint8_t *buf, rt_size_t len)
{
    rc522_log_raw(channel, buf, len);
    uart4_app_rfid_parse_buffer(buf, len);
}

rt_bool_t uart4_app_rfid_get_last_uid(rt_uint8_t uid[UART4_APP_RFID_UID_LEN])
{
    if (uid == RT_NULL || rc522_has_uid == RT_FALSE)
    {
        return RT_FALSE;
    }

    memcpy(uid, rc522_last_uid, UART4_APP_RFID_UID_LEN);
    return RT_TRUE;
}

void uart4_app_rfid_set_card_callback(uart4_app_rfid_card_cb_t callback)
{
    rc522_card_callback = callback;
}

void rc522_status(void)
{
    if (rc522_has_uid == RT_FALSE)
    {
        LOG_E("RC522 no clip uid received yet");
        return;
    }

    rc522_print_uid("RC522 last clip", rc522_last_uid);
}

void rc522_unlock_status(void)
{
    if (rc522_has_unlock == RT_FALSE)
    {
        LOG_E("RC522 no unlock uid received yet");
        return;
    }

    rc522_print_uid("RC522 last unlock", rc522_last_unlock_uid);
}

static void rc522_mock_uid(const rt_uint8_t uid[UART4_APP_RFID_UID_LEN])
{
    rt_uint8_t mock_frame[] = {
        0x04, 0x0C, 0x02, 0x30, 0x00, 0x04, 0x00,
        0x00, 0x00, 0x00, 0x00
    };

    memcpy(&mock_frame[sizeof(mock_frame) - UART4_APP_RFID_UID_LEN], uid, UART4_APP_RFID_UID_LEN);
    uart4_app_rfid_parse_buffer(mock_frame, sizeof(mock_frame));
}

void rc522_mock_card(void)
{
    static const rt_uint8_t uid[UART4_APP_RFID_UID_LEN] = {0x11, 0x22, 0x33, 0x44};
    rc522_mock_uid(uid);
}

void rc522_mock_unlock(void)
{
    static const rt_uint8_t uid[UART4_APP_RFID_UID_LEN] = {
        APP_NFC_UNLOCK_UID0,
        APP_NFC_UNLOCK_UID1,
        APP_NFC_UNLOCK_UID2,
        APP_NFC_UNLOCK_UID3,
    };
    rc522_mock_uid(uid);
}

void rc522_mock_clip1(void)
{
    static const rt_uint8_t uid[UART4_APP_RFID_UID_LEN] = {0x88, 0x04, 0xCF, 0xFB};
    rc522_mock_uid(uid);
}

void rc522_mock_clip(int argc, char *argv[])
{
    rt_uint8_t card_num;
    app_storage_clip_binding_t binding;

    if (argc != 2 || argv[1] == RT_NULL ||
        argv[1][0] < '0' || argv[1][0] > '9' ||
        argv[1][1] < '0' || argv[1][1] > '9' ||
        argv[1][2] != '\0')
    {
        rt_kprintf("usage: rc522_mock_clip <01-04>\r\n");
        return;
    }

    card_num = (rt_uint8_t)(((argv[1][0] - '0') * 10) + (argv[1][1] - '0'));
    if (app_storage_get_clip_binding(card_num, &binding) != RT_EOK)
    {
        rt_kprintf("clip %02u not found\r\n", (unsigned int)card_num);
        return;
    }

    rc522_mock_uid(binding.uid);
}

#if defined(RT_USING_FINSH)
MSH_CMD_EXPORT(rc522_status, show rc522 last clip uid);
MSH_CMD_EXPORT(rc522_unlock_status, show rc522 last unlock uid);
MSH_CMD_EXPORT(rc522_mock_card, mock unknown rc522 card uid);
MSH_CMD_EXPORT(rc522_mock_unlock, mock unlock rc522 card uid);
MSH_CMD_EXPORT(rc522_mock_clip1, mock clip 01 rc522 card uid);
MSH_CMD_EXPORT(rc522_mock_clip, mock clip rc522 card uid by cardId);
#endif
