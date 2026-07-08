#ifndef APPLICATIONS_APP_CONFIG_H__
#define APPLICATIONS_APP_CONFIG_H__

#include <rtthread.h>
#include <board.h>
#include <stm32f4xx_hal.h>

#define APP_DELIVERY_TEST_MODE           1
#define APP_NFC_UNLOCK_ENABLE            1
#define APP_NFC_UNLOCK_UID0              0xDE
#define APP_NFC_UNLOCK_UID1              0xAD
#define APP_NFC_UNLOCK_UID2              0xBE
#define APP_NFC_UNLOCK_UID3              0xEF
#define APP_UART4_DEVICE_NAME             "uart4"
#define APP_UART4_MUX_PIN_B               GET_PIN(D, 10)
#define APP_UART4_MUX_PIN_A               GET_PIN(D, 9)
#define APP_UART4_RADAR_CHANNELS          3
#define APP_UART4_RFID_CHANNEL            3
#define APP_UART4_MUX_CHANNELS            4
#define APP_UART4_DEST_BUTTON_PIN         GET_PIN(C, 0)
#define APP_UART4_DEST_BUTTON_ACTIVE_LEVEL PIN_LOW
#define APP_UART4_DEST_BUTTON_DEBOUNCE_MS 50
#define APP_UART4_MUX_SETTLE_MS           5
#define APP_UART4_RADAR_CHANNEL_HOLD_MS   120
#define APP_UART4_RFID_CHANNEL_HOLD_MS    300
#define APP_UART4_RX_WAIT_MS              5
#define APP_UART4_RFID_UID_LOG_ENABLE     0
#define APP_UART4_RFID_RAW_LOG_ENABLE     0
#define APP_UART4_RAW_LOG_ENABLE          0
#define APP_UART4_DATA_LOG_ENABLE         0
#define APP_GPS_DATA_LOG_ENABLE           0
#define APP_SPEED_DEBUG_LOG_ENABLE        1
#define APP_GPS_SPEED_ZERO_HOLD_MS       5000

#define APP_SENSOR_REFRESH_MS             100
#define APP_SENSOR_ENV_REFRESH_MS         1000
#define APP_SENSOR_DHT11_CHANNELS         4
#define APP_SENSOR_DHT11_FL_PIN           GET_PIN(A, 8)
#define APP_SENSOR_DHT11_FR_PIN           GET_PIN(B, 14)
#define APP_SENSOR_DHT11_BL_PIN           GET_PIN(B, 15)
#define APP_SENSOR_DHT11_BR_PIN           GET_PIN(E, 4)
#define APP_SENSOR_DHT11_LOG_ENABLE       0
#define APP_SENSOR_ACCEL_1G_RAW           16384
#define APP_SENSOR_GYRO_LSB_PER_DPS       131
#define APP_SENSOR_GYRO_Z_RIGHT_SIGN      -1
#define APP_SENSOR_TURN_ACC_X_LEFT_RAW   3900
#define APP_SENSOR_TURN_ACC_X_RIGHT_RAW  (-3900)
#define APP_SENSOR_TURN_LIGHT_HOLD_MS    5000
#define APP_SENSOR_FALL_ENABLE            1
#define APP_SENSOR_FALL_Z_MAX_MG          500
#define APP_SENSOR_FALL_XY_MIN_MG         800
#define APP_SENSOR_FALL_CONFIRM_COUNT     3
#define APP_SENSOR_OVERSPEED_ENABLE       1
#define APP_SENSOR_OVERSPEED_LIMIT_X100   2500
#define APP_SENSOR_OVERSPEED_HYST_X100    200
#define APP_SENSOR_OVERSPEED_HOLD_MS      1000
#define APP_SENSOR_MOTION_LOG_ENABLE      1
#define APP_SENSOR_FALL_BUZZER_HOLD_MS    5000
#define APP_SENSOR_FALL_TILT_MIN_MG           850

#define APP_SERVO_LOCK_PIN                GET_PIN(B, 2)
#define APP_SERVO_LOCK_DEFAULT_FREQ_HZ    50
#define APP_SERVO_LOCK_MIN_FREQ_HZ        20
#define APP_SERVO_LOCK_MAX_FREQ_HZ        100
#define APP_SERVO_LOCK_MIN_US             500
#define APP_SERVO_LOCK_NEUTRAL_US         1500
#define APP_SERVO_LOCK_MAX_US             2500
#define APP_SERVO_LOCK_MIN_ANGLE_DEG      0
#define APP_SERVO_LOCK_MAX_ANGLE_DEG      180
#define APP_SERVO_LOCK_CLOSE_ANGLE_DEG    20
#define APP_SERVO_LOCK_OPEN_ANGLE_DEG     110
#define APP_SERVO_LOCK_THREAD_STACK_SIZE  768
#define APP_SERVO_LOCK_THREAD_PRIORITY    18
#define APP_SERVO_LOCK_THREAD_TICK        10
#define APP_BUZZER_PIN                    GET_PIN(B, 0)
#define APP_BUZZER_ACTIVE_HIGH            1
#define APP_BUZZER_POLL_MS                20
#define APP_BUZZER_TURN_PERIOD_MS         500
#define APP_BUZZER_TURN_ON_MS             80
#define APP_BUZZER_FALL_PERIOD_MS         240
#define APP_BUZZER_FALL_ON_MS             120
#define APP_BUZZER_WARN_PERIOD_MS         360
#define APP_BUZZER_WARN_ON_MS             90

#define APP_WS2812_PORT                   GPIOE
#define APP_WS2812_LEFT_PIN               GPIO_PIN_2
#define APP_WS2812_RIGHT_PIN              GPIO_PIN_5
#define APP_WS2812_PIN                    (APP_WS2812_LEFT_PIN | APP_WS2812_RIGHT_PIN)
#define APP_WS2812_LEFT_LED_COUNT         55
#define APP_WS2812_RIGHT_LED_COUNT        50
#define APP_WS2812_MAX_LED_COUNT          59
#define APP_WS2812_GPIO_CLK_ENABLE()      __HAL_RCC_GPIOE_CLK_ENABLE()
#define APP_WS2811_LOG_ENABLE             1

#define APP_RADAR_WARN_DISTANCE_M         5
#define APP_RADAR_WARN_FLASH_AFTER_MS     2000
#define APP_RADAR_WARN_NO_DATA_HOLD_MS    3000
#define APP_RADAR_WARN_COLOR_R            0x20
#define APP_RADAR_WARN_COLOR_G            0x00
#define APP_RADAR_WARN_COLOR_B            0x00
#define APP_MOTION_TURN_COLOR_R           0x00
#define APP_MOTION_TURN_COLOR_G           0x00
#define APP_MOTION_TURN_COLOR_B           0x20

#define APP_NRF24_SPI_BUS_NAME            "spi1"
#define APP_NRF24_SPI_DEVICE_NAME         "spi10"
#define APP_NRF24_CSN_GPIO                GPIOA
#define APP_NRF24_CSN_PIN                 GPIO_PIN_4
#define APP_NRF24_CE_PIN                  GET_PIN(E, 3)
#define APP_NRF24_CLIP_RF_CHANNEL         40
#define APP_NRF24_CLIP_PAYLOAD_SIZE       1
#define APP_NRF24_CLIP_CMD_OFF            0x00
#define APP_NRF24_CLIP_CMD_ON             0x01
#define APP_NRF24_CLIP_CMD_PULSE_1S       0x02
#define APP_NRF24_CLIP_CMD_BLINK          0x03
#define APP_NRF24_CLIP_CMD_TOGGLE         0x04

#endif
