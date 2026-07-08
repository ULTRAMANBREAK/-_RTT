#ifndef APPLICATIONS_UART4_APP_H__
#define APPLICATIONS_UART4_APP_H__

#include <rtthread.h>
#include "app_config.h"

#define UART4_APP_RFID_UID_LEN 4
#define UART4_APP_DEVICE_NAME        APP_UART4_DEVICE_NAME
#define UART4_APP_THREAD_NAME        "u4_mux"
#define UART4_APP_THREAD_STACK_SIZE  2048
#define UART4_APP_THREAD_PRIORITY    12
#define UART4_APP_THREAD_TICK        20
#define UART4_APP_RX_BUF_SIZE        256
#define UART4_APP_STREAM_BUF_SIZE    256
#define UART4_APP_FRAME_MIN_LEN      10
#define UART4_APP_TARGET_SLOTS       3
#define UART4_APP_RAW_LOG_MAX        16
#define UART4_APP_RADAR_CHANNELS     APP_UART4_RADAR_CHANNELS
#define UART4_APP_RFID_CHANNEL       APP_UART4_RFID_CHANNEL
#define UART4_APP_MUX_CHANNELS       APP_UART4_MUX_CHANNELS
#define UART4_APP_MUX_PIN_B          APP_UART4_MUX_PIN_B
#define UART4_APP_MUX_PIN_A          APP_UART4_MUX_PIN_A
#define UART4_APP_MUX_SETTLE_MS      APP_UART4_MUX_SETTLE_MS
#define UART4_APP_RADAR_CHANNEL_HOLD_MS APP_UART4_RADAR_CHANNEL_HOLD_MS
#define UART4_APP_RFID_CHANNEL_HOLD_MS  APP_UART4_RFID_CHANNEL_HOLD_MS
#define UART4_APP_RX_WAIT_MS            APP_UART4_RX_WAIT_MS
#define UART4_APP_DEST_BUTTON_PIN       APP_UART4_DEST_BUTTON_PIN
#define UART4_APP_DEST_BUTTON_ACTIVE_LEVEL APP_UART4_DEST_BUTTON_ACTIVE_LEVEL
#define UART4_APP_DEST_BUTTON_DEBOUNCE_MS APP_UART4_DEST_BUTTON_DEBOUNCE_MS

#define RC522_UID_LEN UART4_APP_RFID_UID_LEN

//typedef uart4_app_rfid_card_cb_t rc522_uart_card_cb_t;

#define rc522_uart_get_last_uid       uart4_app_rfid_get_last_uid
#define rc522_uart_set_card_callback  uart4_app_rfid_set_card_callback
#define rc522_uart_parse_buffer       uart4_app_rfid_parse_buffer

typedef void (*uart4_app_rfid_card_cb_t)(const rt_uint8_t uid[UART4_APP_RFID_UID_LEN]);

int uart4_app_init(void);
rt_bool_t uart4_app_is_arrived_mode(void);
void uart4_app_enter_arrived_mode(void);
void uart4_app_enter_drive_mode(void);
int lm2451_init(void);
int rc522_uart_init(void);
rt_bool_t uart4_app_rfid_get_last_uid(rt_uint8_t uid[UART4_APP_RFID_UID_LEN]);
void uart4_app_rfid_set_card_callback(uart4_app_rfid_card_cb_t callback);
void uart4_app_rfid_parse_buffer(const rt_uint8_t *buf, rt_size_t len);
void rc522_status(void);
void rc522_mock_card(void);

#endif
