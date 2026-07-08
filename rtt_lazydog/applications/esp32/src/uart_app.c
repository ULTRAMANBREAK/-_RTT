/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>

#include "app_config.h"
#include "uart_app.h"
#include "atgm336h.h"
#include "nrf24_app.h"
#include "app_storage.h"
#include "uart4_app.h"
#include "servo_lock.h"

#define DBG_TAG "esp32.uart"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#define ESP32_UART_DEVICE_NAME  "uart3"
#define ESP32_UART_STACK_SIZE   1024
#define ESP32_UART_PRIORITY     18
#define ESP32_UART_TICK         10
#define ESP32_UART_BUF_SIZE     64
#define ESP32_UART_LINE_SIZE    160
#define ESP32_UART_TX_TIMEOUT   rt_tick_from_millisecond(100)

static rt_device_t esp32_uart_dev = RT_NULL;
static struct rt_semaphore esp32_uart_rx_sem;
static struct rt_mutex esp32_uart_tx_lock;
static rt_bool_t esp32_uart_tx_lock_ready = RT_FALSE;
static char esp32_uart_line[ESP32_UART_LINE_SIZE];
static rt_size_t esp32_uart_line_len = 0;
static rt_bool_t esp32_temp_valid[UART_APP_TEMP_CHANNELS] = {RT_FALSE};
static rt_int32_t esp32_temp_x10[UART_APP_TEMP_CHANNELS] = {0};

static rt_err_t esp32_uart_rx_ind(rt_device_t dev, rt_size_t size)
{
    RT_UNUSED(dev);
    RT_UNUSED(size);

    rt_sem_release(&esp32_uart_rx_sem);
    return RT_EOK;
}

static rt_err_t esp32_uart_write_text(const char *text)
{
    rt_size_t len;
    rt_size_t written;
    rt_err_t result = RT_EOK;

    if (esp32_uart_dev == RT_NULL || text == RT_NULL)
    {
        return -RT_ERROR;
    }

    len = rt_strlen(text);
    if (len == 0)
    {
        return RT_EOK;
    }

    if (esp32_uart_tx_lock_ready == RT_TRUE)
    {
        result = rt_mutex_take(&esp32_uart_tx_lock, ESP32_UART_TX_TIMEOUT);
        if (result != RT_EOK)
        {
            return result;
        }
    }

    written = rt_device_write(esp32_uart_dev, 0, text, len);
    if (esp32_uart_tx_lock_ready == RT_TRUE)
    {
        rt_mutex_release(&esp32_uart_tx_lock);
    }
    return (written == len) ? RT_EOK : -RT_ERROR;
}

rt_err_t uart_app_send_line(const char *line)
{
    rt_err_t result;

    result = esp32_uart_write_text(line);
    if (result != RT_EOK)
    {
        return result;
    }

    return esp32_uart_write_text("\r\n");
}

static long esp32_kmh_x100_to_mps_x10(long speed_kmh_x100)
{
    return (speed_kmh_x100 >= 0) ? ((speed_kmh_x100 + 18) / 36) : ((speed_kmh_x100 - 18) / 36);
}

static void esp32_format_x10(char *buf, rt_size_t size, long value_x10)
{
    const char *sign = "";

    if (value_x10 < 0)
    {
        sign = "-";
        value_x10 = -value_x10;
    }

    rt_snprintf(buf, size, "%s%ld.%ld", sign, value_x10 / 10, value_x10 % 10);
}

static rt_err_t esp32_uart_send_speed_response(void)
{
    char line[64];
    char speed_text[16];
    long speed_x100 = 0;
    long speed_x10 = 0;
    rt_err_t gps_result;

    gps_result = atgm336h_get_speed_kmh_x100(&speed_x100);
    if (gps_result == RT_EOK)
    {
        speed_x10 = esp32_kmh_x100_to_mps_x10(speed_x100);
    }

    esp32_format_x10(speed_text, sizeof(speed_text), speed_x10);
#if APP_SPEED_DEBUG_LOG_ENABLE
    {
        static rt_bool_t last_log_ready = RT_FALSE;
        static rt_bool_t last_log_valid = RT_FALSE;
        static long last_log_speed_x10 = 0;
        rt_bool_t current_valid = (gps_result == RT_EOK) ? RT_TRUE : RT_FALSE;

        if ((last_log_ready == RT_FALSE) ||
            (last_log_valid != current_valid) ||
            (last_log_speed_x10 != speed_x10))
        {
            if (current_valid == RT_TRUE)
            {
                rt_kprintf("[speed] gps=%ld.%02ldkm/h mps=%s\r\n",
                           speed_x100 / 100L,
                           speed_x100 % 100L,
                           speed_text);
            }
            else
            {
                rt_kprintf("[speed] gps=invalid mps=%s\r\n", speed_text);
            }
            last_log_ready = RT_TRUE;
            last_log_valid = current_valid;
            last_log_speed_x10 = speed_x10;
        }
    }
#endif
    rt_snprintf(line, sizeof(line), "{\"type\":\"speed\",\"speed\":%s}", speed_text);
    return uart_app_send_line(line);
}

static rt_err_t esp32_uart_send_temp_response(void)
{
    char line[128];
    char temp_text[UART_APP_TEMP_CHANNELS][16];
    rt_size_t i;

    for (i = 0; i < UART_APP_TEMP_CHANNELS; i++)
    {
        rt_int32_t temp_x10 = esp32_temp_valid[i] ? esp32_temp_x10[i] : 0;
        esp32_format_x10(temp_text[i], sizeof(temp_text[i]), temp_x10);
    }

    rt_snprintf(line,
                sizeof(line),
                "{\"type\":\"temp\",\"FL\":%s,\"FR\":%s,\"BL\":%s,\"BR\":%s}",
                temp_text[0],
                temp_text[1],
                temp_text[2],
                temp_text[3]);
    return uart_app_send_line(line);
}

static rt_bool_t esp32_uart_parse_card_id(const char *line, rt_uint8_t *card_num)
{
    const char *p;
    int value;

    if (line == RT_NULL || card_num == RT_NULL)
    {
        return RT_FALSE;
    }

    p = strstr(line, "\"cardId\":\"");
    if (p == RT_NULL)
    {
        return RT_FALSE;
    }
    p += 10;

    if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9' || p[2] != '\"')
    {
        return RT_FALSE;
    }

    value = ((p[0] - '0') * 10) + (p[1] - '0');
    if (value < 1 || value > 4)
    {
        return RT_FALSE;
    }

    *card_num = (rt_uint8_t)value;
    return RT_TRUE;
}

static rt_err_t esp32_uart_mark_arrived(rt_uint8_t card_num)
{
    rt_err_t result;

    result = app_storage_set_clip_pending_action(card_num, APP_STORAGE_CLIP_PENDING_BIND);
    if (result != RT_EOK)
    {
        return result;
    }

#if APP_DELIVERY_TEST_MODE == 1
    result = app_storage_update_clip_binding(card_num,
                                             APP_STORAGE_CLIP_STATUS_ARRIVED,
                                             RT_NULL,
                                             RT_NULL,
                                             rt_tick_get());
    if (result != RT_EOK)
    {
        return result;
    }
#else
    RT_UNUSED(card_num);
#endif

    uart4_app_enter_arrived_mode();
    return RT_EOK;
}

static rt_err_t esp32_uart_mark_delivery(rt_uint8_t card_num)
{
    rt_err_t result;

    result = app_storage_set_clip_pending_action(card_num, APP_STORAGE_CLIP_PENDING_UNBIND);
    if (result == RT_EOK)
    {
        uart4_app_enter_arrived_mode();
    }

    return result;
}

static rt_err_t esp32_uart_handle_lock(const char *line, const char **action_name)
{
    rt_uint16_t angle_deg;

    if (action_name == RT_NULL)
    {
        return -RT_EINVAL;
    }

    if (strstr(line, "\"action\":\"open\"") != RT_NULL)
    {
        *action_name = "OPEN";
        angle_deg = APP_SERVO_LOCK_OPEN_ANGLE_DEG;
    }
    else if (strstr(line, "\"action\":\"close\"") != RT_NULL)
    {
        *action_name = "CLOSE";
        angle_deg = APP_SERVO_LOCK_CLOSE_ANGLE_DEG;
    }
    else
    {
        *action_name = RT_NULL;
        return -RT_EINVAL;
    }

    return servo_lock_set_angle(angle_deg);
}

rt_err_t uart_app_send_nfc(const rt_uint8_t uid[UART_APP_RFID_UID_LEN], rt_bool_t unbind)
{
    char line[96];
    rt_uint8_t card_num;

    if (uid == RT_NULL)
    {
        return -RT_EINVAL;
    }

    card_num = app_storage_uid_to_card_num(uid);
    rt_snprintf(line,
                sizeof(line),
                "{\"type\":\"nfc\",\"action\":\"%s\",\"cardId\":\"%02u\"}",
                unbind ? "UNBIND" : "BIND",
                (unsigned int)card_num);
    return uart_app_send_line(line);
}

rt_err_t uart_app_send_rfid_uid(const rt_uint8_t uid[UART_APP_RFID_UID_LEN])
{
    return uart_app_send_nfc(uid, RT_FALSE);
}

rt_err_t uart_app_send_fall(void)
{
    return uart_app_send_line("{\"type\":\"fall\"}");
}

rt_err_t uart_app_send_theft(void)
{
    return uart_app_send_line("{\"type\":\"theft\"}");
}

rt_err_t uart_app_send_unlock(void)
{
    return uart_app_send_line("{\"type\":\"unlock\"}");
}

void uart_app_update_temp(rt_bool_t valid, rt_int32_t temperature_x10)
{
    rt_size_t i;

    for (i = 0; i < UART_APP_TEMP_CHANNELS; i++)
    {
        esp32_temp_valid[i] = valid;
        if (valid == RT_TRUE)
        {
            esp32_temp_x10[i] = temperature_x10;
        }
    }
}

void uart_app_update_temps(const rt_bool_t valid[UART_APP_TEMP_CHANNELS],
                           const rt_int32_t temperature_x10[UART_APP_TEMP_CHANNELS])
{
    rt_size_t i;

    if ((valid == RT_NULL) || (temperature_x10 == RT_NULL))
    {
        return;
    }

    for (i = 0; i < UART_APP_TEMP_CHANNELS; i++)
    {
        esp32_temp_valid[i] = valid[i];
        if (valid[i] == RT_TRUE)
        {
            esp32_temp_x10[i] = temperature_x10[i];
        }
    }
}

static char *esp32_uart_trim(char *text)
{
    char *end;

    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')
    {
        text++;
    }

    end = text + rt_strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
    {
        end--;
    }
    *end = '\0';
    return text;
}

static void esp32_uart_handle_line(char *line)
{
    line = esp32_uart_trim(line);

    if (rt_strcmp(line, "PING") == 0)
    {
        esp32_uart_write_text("OK PONG\r\n");
        return;
    }

    if ((strstr(line, "\"type\":\"req\"") != RT_NULL) &&
        (strstr(line, "\"want\":\"speed\"") != RT_NULL))
    {
        esp32_uart_send_speed_response();
        return;
    }

    if ((strstr(line, "\"type\":\"req\"") != RT_NULL) &&
        (strstr(line, "\"want\":\"temp\"") != RT_NULL))
    {
        esp32_uart_send_temp_response();
        return;
    }

    if (strstr(line, "\"type\":\"lock\"") != RT_NULL)
    {
        const char *action_name;
        rt_err_t result;

        result = esp32_uart_handle_lock(line, &action_name);
        if (result == -RT_EINVAL)
        {
            esp32_uart_write_text("ERR LOCK ACTION\r\n");
            return;
        }
        if (result != RT_EOK)
        {
            esp32_uart_write_text("ERR LOCK SERVO\r\n");
            return;
        }
        esp32_uart_write_text((rt_strcmp(action_name, "OPEN") == 0) ? "OK LOCK OPEN\r\n" : "OK LOCK CLOSE\r\n");
        return;
    }

    if (strstr(line, "\"type\":\"arrived\"") != RT_NULL)
    {
        rt_uint8_t card_num;

        if (esp32_uart_parse_card_id(line, &card_num) == RT_FALSE)
        {
            esp32_uart_write_text("ERR ARRIVED CARD\r\n");
            return;
        }

        if (esp32_uart_mark_arrived(card_num) != RT_EOK)
        {
            esp32_uart_write_text("ERR ARRIVED STORE\r\n");
            return;
        }

        esp32_uart_write_text("OK ARRIVED\r\n");
        return;
    }

    if (strstr(line, "\"type\":\"blink\"") != RT_NULL)
    {
        rt_uint8_t card_num;

        if (esp32_uart_parse_card_id(line, &card_num) == RT_FALSE)
        {
            esp32_uart_write_text("ERR BLINK CARD\r\n");
            return;
        }
        LOG_I("clip blink cardId=%02u", (unsigned int)card_num);
        if (esp32_uart_mark_delivery(card_num) != RT_EOK)
        {
            esp32_uart_write_text("ERR BLINK STORE\r\n");
            return;
        }
        nrf24_clip_send_cmd_to(card_num, APP_NRF24_CLIP_CMD_BLINK);
        return;
    }

    LOG_W("ESP32 unknown command: %s", line);
    esp32_uart_write_text("ERR UNKNOWN CMD\r\n");
}

static void esp32_uart_feed_bytes(const rt_uint8_t *buf, rt_size_t len)
{
    rt_size_t i;

    for (i = 0; i < len; i++)
    {
        char ch = (char)buf[i];

        if (ch == '\r' || ch == '\n')
        {
            if (esp32_uart_line_len > 0)
            {
                esp32_uart_line[esp32_uart_line_len] = '\0';
                esp32_uart_handle_line(esp32_uart_line);
                esp32_uart_line_len = 0;
            }
            continue;
        }

        if (esp32_uart_line_len >= (sizeof(esp32_uart_line) - 1))
        {
            esp32_uart_line_len = 0;
            LOG_E("ESP32 command too long");
            esp32_uart_write_text("ERR CMD TOO LONG\r\n");
            continue;
        }

        esp32_uart_line[esp32_uart_line_len++] = ch;
    }
}

static void esp32_uart_entry(void *parameter)
{
    rt_uint8_t rx_buf[ESP32_UART_BUF_SIZE];

    RT_UNUSED(parameter);

    while (1)
    {
        rt_size_t rx_len;

        rx_len = rt_device_read(esp32_uart_dev, 0, rx_buf, sizeof(rx_buf));
        if (rx_len > 0)
        {
            esp32_uart_feed_bytes(rx_buf, rx_len);
            continue;
        }

        rt_sem_take(&esp32_uart_rx_sem, RT_WAITING_FOREVER);
    }
}

int uart_app_init(void)
{
    rt_err_t result;
    rt_thread_t thread;

    app_storage_init();

    esp32_uart_dev = rt_device_find(ESP32_UART_DEVICE_NAME);
    if (esp32_uart_dev == RT_NULL)
    {
        LOG_E("ESP32 UART device not found");
        return -RT_ERROR;
    }

    result = rt_sem_init(&esp32_uart_rx_sem, "e32rx", 0, RT_IPC_FLAG_FIFO);
    if (result != RT_EOK)
    {
        LOG_E("ESP32 UART rx semaphore init failed");
        return result;
    }

    result = rt_mutex_init(&esp32_uart_tx_lock, "e32tx", RT_IPC_FLAG_FIFO);
    if (result != RT_EOK)
    {
        LOG_E("ESP32 UART tx mutex init failed");
        rt_sem_detach(&esp32_uart_rx_sem);
        return result;
    }
    esp32_uart_tx_lock_ready = RT_TRUE;

    result = rt_device_open(esp32_uart_dev, RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_DMA_RX);
    if (result != RT_EOK)
    {
        LOG_E("ESP32 UART open failed: %d", result);
        rt_mutex_detach(&esp32_uart_tx_lock);
        esp32_uart_tx_lock_ready = RT_FALSE;
        rt_sem_detach(&esp32_uart_rx_sem);
        esp32_uart_dev = RT_NULL;
        return result;
    }

    rt_device_set_rx_indicate(esp32_uart_dev, esp32_uart_rx_ind);

    thread = rt_thread_create("e32_uart",
                              esp32_uart_entry,
                              RT_NULL,
                              ESP32_UART_STACK_SIZE,
                              ESP32_UART_PRIORITY,
                              ESP32_UART_TICK);
    if (thread == RT_NULL)
    {
        LOG_E("ESP32 UART thread create failed");
        rt_device_close(esp32_uart_dev);
        rt_mutex_detach(&esp32_uart_tx_lock);
        esp32_uart_tx_lock_ready = RT_FALSE;
        rt_sem_detach(&esp32_uart_rx_sem);
        esp32_uart_dev = RT_NULL;
        return -RT_ERROR;
    }

    rt_thread_startup(thread);
    return RT_EOK;
}
//INIT_APP_EXPORT(uart_app_init);
