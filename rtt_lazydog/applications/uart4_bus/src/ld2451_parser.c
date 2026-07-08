#include <rtthread.h>
#include <string.h>

#include "uart4_app.h"
#include "ld2451_parser.h"
#include "ws2812.h"
#include "uart_app.h"
#include "app_storage.h"

#define DBG_TAG "uart4.ld2451"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#define lod_g(...) LOG_I(__VA_ARGS__)

static const rt_uint8_t ld2451_frame_head[4] = {0xF4, 0xF3, 0xF2, 0xF1};
static const rt_uint8_t ld2451_frame_tail[4] = {0xF8, 0xF7, 0xF6, 0xF5};

static rt_uint8_t ld2451_stream_buf[UART4_APP_RADAR_CHANNELS][UART4_APP_STREAM_BUF_SIZE];
static rt_size_t ld2451_stream_len[UART4_APP_RADAR_CHANNELS] = {0};
static rt_uint32_t ld2451_rx_total[UART4_APP_RADAR_CHANNELS] = {0};

static rt_uint8_t ld2451_get_warn_distance_m(void)
{
    app_storage_device_config_t config;

    if ((app_storage_get_device_config(&config) == RT_EOK) && (config.radar_warn_distance_m > 0))
    {
        return config.radar_warn_distance_m;
    }

    return APP_RADAR_WARN_DISTANCE_M;
}
static rt_uint16_t u16le(const rt_uint8_t *p)
{
    return (rt_uint16_t)p[0] | ((rt_uint16_t)p[1] << 8);
}

static rt_int8_t ld2451_angle_to_degree(rt_uint8_t raw)
{
    return (rt_int8_t)((rt_int16_t)raw - 0x80);
}

static void ld2451_log_raw(rt_uint8_t channel, const rt_uint8_t *buf, rt_size_t len)
{
#if APP_UART4_RAW_LOG_ENABLE
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

    ld2451_rx_total[channel] += len;
    lod_g("LD2451 ch%u raw rx len=%u total=%u data=%s",
          (unsigned int)channel,
          (unsigned int)len,
          (unsigned int)ld2451_rx_total[channel],
          hex);
#else
    RT_UNUSED(buf);
    ld2451_rx_total[channel] += len;
#endif
}

static void ld2451_log_target(rt_uint8_t channel, rt_uint8_t index, const rt_uint8_t *slot)
{
#if APP_UART4_DATA_LOG_ENABLE
    rt_int8_t angle;
    rt_uint8_t distance;
    rt_uint8_t speed_dir;
    rt_uint8_t speed_value;
    rt_uint8_t snr;
    const char *direction_text;

    angle = ld2451_angle_to_degree(slot[0]);
    distance = slot[1];
    speed_dir = slot[2];
    speed_value = slot[3];
    snr = slot[4];
    direction_text = (speed_dir == 0x01) ? "approaching" : "receding";

    lod_g("LD2451 ch%u target%u angle=%d distance=%um direction=%s speed=%ukm/h snr=%u",
          (unsigned int)channel,
          (unsigned int)index + 1,
          (int)angle,
          (unsigned int)distance,
          direction_text,
          (unsigned int)speed_value,
          (unsigned int)snr);
#else
    RT_UNUSED(channel);
    RT_UNUSED(index);
    RT_UNUSED(slot);
#endif
}

static void ld2451_log_frame(rt_uint8_t channel, const rt_uint8_t *payload, rt_uint16_t payload_len)
{
    rt_uint8_t target_count;
    rt_uint8_t alarm_flag;
    rt_uint8_t slot_count;
    rt_uint8_t nearest_distance;
    rt_uint8_t i;
    rt_uint16_t expected_payload_len;

    if (payload_len == 0)
    {
        //lod_g("LD2451 ch%u frame no target", (unsigned int)channel);
        ws2812_radar_report(channel, RT_FALSE, 0U);
        return;
    }

    if (payload_len < 2)
    {
        lod_g("LD2451 ch%u frame too short payload_len=%u",
              (unsigned int)channel,
              (unsigned int)payload_len);
        return;
    }

    target_count = payload[0];
    alarm_flag = payload[1];
    expected_payload_len = (rt_uint16_t)(2 + (5 * target_count));
    if (payload_len != expected_payload_len)
    {
        lod_g("LD2451 ch%u frame payload mismatch len=%u expected=%u target_count=%u",
              (unsigned int)channel,
              (unsigned int)payload_len,
              (unsigned int)expected_payload_len,
              (unsigned int)target_count);
        return;
    }

    slot_count = target_count;
    if (slot_count > UART4_APP_TARGET_SLOTS)
    {
        slot_count = UART4_APP_TARGET_SLOTS;
    }

#if !APP_UART4_DATA_LOG_ENABLE
    RT_UNUSED(alarm_flag);
#endif

    nearest_distance = 0xFFU;
    for (i = 0; i < slot_count; i++)
    {
        const rt_uint8_t *slot = &payload[2 + (i * 5)];

        if (slot[1] < nearest_distance)
        {
            nearest_distance = slot[1];
        }
    }

    ws2812_radar_report(channel,
                        (nearest_distance < ld2451_get_warn_distance_m()) ? RT_TRUE : RT_FALSE,
                        nearest_distance);

#if APP_UART4_DATA_LOG_ENABLE
    lod_g("LD2451 ch%u frame target_count=%u alarm=0x%02X",
          (unsigned int)channel,
          (unsigned int)target_count,
          (unsigned int)alarm_flag);

    for (i = 0; i < slot_count; i++)
    {
        const rt_uint8_t *slot = &payload[2 + (i * 5)];
        ld2451_log_target(channel, i, slot);
    }
#endif
}

static rt_size_t ld2451_find_header(const rt_uint8_t *buf, rt_size_t len)
{
    rt_size_t i;

    if (len < sizeof(ld2451_frame_head))
    {
        return len;
    }

    for (i = 0; i <= len - sizeof(ld2451_frame_head); i++)
    {
        if (memcmp(&buf[i], ld2451_frame_head, sizeof(ld2451_frame_head)) == 0)
        {
            return i;
        }
    }

    return len;
}

void ld2451_parser_reset(rt_uint8_t channel)
{
    if (channel < UART4_APP_RADAR_CHANNELS)
    {
        ld2451_stream_len[channel] = 0;
    }
}

static void ld2451_consume(rt_uint8_t channel, rt_size_t count)
{
    if (count >= ld2451_stream_len[channel])
    {
        ld2451_stream_len[channel] = 0;
        return;
    }

    memmove(ld2451_stream_buf[channel],
            ld2451_stream_buf[channel] + count,
            ld2451_stream_len[channel] - count);
    ld2451_stream_len[channel] -= count;
}

static void ld2451_parse_stream(rt_uint8_t channel)
{
    while (ld2451_stream_len[channel] >= UART4_APP_FRAME_MIN_LEN)
    {
        rt_uint8_t *stream;
        rt_size_t header_pos;
        rt_uint16_t payload_len;
        rt_size_t frame_len;
        const rt_uint8_t *payload;
        const rt_uint8_t *tail;
        rt_uint8_t target_count;
        rt_uint16_t expected_payload_len;

        stream = ld2451_stream_buf[channel];
        header_pos = ld2451_find_header(stream, ld2451_stream_len[channel]);
        if (header_pos == ld2451_stream_len[channel])
        {
            ld2451_consume(channel, ld2451_stream_len[channel] - (sizeof(ld2451_frame_head) - 1));
            return;
        }

        if (header_pos > 0)
        {
            ld2451_consume(channel, header_pos);
            continue;
        }

        payload_len = u16le(&stream[4]);
        frame_len = sizeof(ld2451_frame_head) + 2 + payload_len + sizeof(ld2451_frame_tail);
        if (ld2451_stream_len[channel] < frame_len)
        {
            return;
        }

        payload = &stream[6];
        tail = &stream[6 + payload_len];
        if (memcmp(tail, ld2451_frame_tail, sizeof(ld2451_frame_tail)) != 0)
        {
            lod_g("LD2451 ch%u drop bad tail len=%u",
                  (unsigned int)channel,
                  (unsigned int)payload_len);
            ld2451_consume(channel, 1);
            continue;
        }

        if (payload_len == 0)
        {
            ld2451_log_frame(channel, RT_NULL, 0);
            ld2451_consume(channel, frame_len);
            continue;
        }

        target_count = payload[0];
        expected_payload_len = (rt_uint16_t)(2 + (5 * target_count));
        if (payload_len != expected_payload_len)
        {
            lod_g("LD2451 ch%u drop payload mismatch len=%u expected=%u target_count=%u",
                  (unsigned int)channel,
                  (unsigned int)payload_len,
                  (unsigned int)expected_payload_len,
                  (unsigned int)target_count);
            ld2451_consume(channel, 1);
            continue;
        }

        ld2451_log_frame(channel, payload, payload_len);
        ld2451_consume(channel, frame_len);
    }
}

void ld2451_parser_input(rt_uint8_t channel, const rt_uint8_t *buf, rt_size_t len)
{
    if (channel >= UART4_APP_RADAR_CHANNELS || buf == RT_NULL || len == 0)
    {
        return;
    }

    ld2451_log_raw(channel, buf, len);
    if (len > (sizeof(ld2451_stream_buf[channel]) - ld2451_stream_len[channel]))
    {
        ld2451_stream_len[channel] = 0;
    }

    memcpy(&ld2451_stream_buf[channel][ld2451_stream_len[channel]], buf, len);
    ld2451_stream_len[channel] += len;
    ld2451_parse_stream(channel);
}
