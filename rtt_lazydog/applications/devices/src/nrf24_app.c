#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <drv_spi.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "nrf24_app.h"

#define DBG_TAG "app.nrf24"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#ifdef PKG_USING_NRF24L01
#define NRF24L01_ENABLE_EXT_HEADER
#include "nrf24l01.h"

static nrf24_t nrf24_dev;
static rt_bool_t nrf24_ready = RT_FALSE;
static rt_uint8_t nrf24_clip_current_card = 1;

static rt_err_t nrf24_clip_validate_card(rt_uint8_t card_num)
{
    return (card_num >= 1 && card_num <= 4) ? RT_EOK : -RT_EINVAL;
}

static void nrf24_clip_make_addr(rt_uint8_t card_num, rt_uint8_t addr[5])
{
    addr[0] = 'L';
    addr[1] = 'E';
    addr[2] = 'D';
    addr[3] = '0';
    addr[4] = (rt_uint8_t)('0' + card_num);
}

static void nrf24_clip_fill_config(nrf24_user_cfg_t *config, rt_uint8_t card_num)
{
    rt_uint8_t addr[5];
    int i;

    nrf24_clip_make_addr(card_num, addr);
    nrf24_usercfg_init_default(config);
    config->rf_channel = APP_NRF24_CLIP_RF_CHANNEL;
    config->rf_power = NRF24_RF_POWER_0dBm;
    config->rf_adr = NRF24_ADR_1Mbps;

    for (i = 0; i < 6; i++)
    {
        config->rxpipes[i].enable = 0;
        config->rxpipes[i].enable_aa = 0;
    }

    config->rxpipes[0].enable = 1;
    memcpy(config->tx_addr, addr, sizeof(config->tx_addr));
    memcpy(config->rxpipes[0].addr, addr, sizeof(config->rxpipes[0].addr));
}

static rt_err_t nrf24_clip_setup_ptx(rt_uint8_t card_num)
{
    int result;
    nrf24_user_cfg_t config;

    if (nrf24_clip_validate_card(card_num) != RT_EOK)
    {
        return -RT_EINVAL;
    }

    nrf24_clip_fill_config(&config, card_num);
    result = nrf24_setup_custom(&nrf24_dev,
                                NRF24_ROLE_PTX,
                                &config,
                                nrf24_default_regval_list,
                                nrf24_default_regval_list_num);
    if (result != 0)
    {
        LOG_E("clip setup failed %d addr=LED%02u ch=%u",
              result,
              (unsigned int)card_num,
              APP_NRF24_CLIP_RF_CHANNEL);
        return -RT_ERROR;
    }

    nrf24_write_reg(&nrf24_dev, NRF24_REG_RF_SETUP, 0x06);
    nrf24_write_reg(&nrf24_dev, NRF24_REG_SETUP_RETR, 0x00);
    nrf24_write_reg(&nrf24_dev, NRF24_REG_DYNPD, 0x00);
    nrf24_write_reg(&nrf24_dev, NRF24_REG_FEATURE, 0x00);
    nrf24_clear_all(&nrf24_dev);
    nrf24_radio_off(&nrf24_dev);
    nrf24_clip_current_card = card_num;

    return RT_EOK;
}
#endif

rt_err_t nrf24_clip_send_cmd_to(rt_uint8_t card_num, rt_uint8_t cmd)
{
#ifndef PKG_USING_NRF24L01
    RT_UNUSED(card_num);
    RT_UNUSED(cmd);
    LOG_W("nrf24 package disabled");
    return -RT_ERROR;
#else
    rt_err_t result;
    rt_uint8_t payload[APP_NRF24_CLIP_PAYLOAD_SIZE];
    int i;

    if (nrf24_clip_validate_card(card_num) != RT_EOK)
    {
        return -RT_EINVAL;
    }

    if (nrf24_ready == RT_FALSE)
    {
        result = nrf24_app_init();
        if (result != RT_EOK)
        {
            return result;
        }
    }

    if (nrf24_clip_current_card != card_num)
    {
        result = nrf24_clip_setup_ptx(card_num);
        if (result != RT_EOK)
        {
            return result;
        }
    }

    if (nrf24_role_is_ptx(&nrf24_dev) == 0)
    {
        if (nrf24_role_switch(&nrf24_dev, NRF24_ROLE_PTX) != 0)
        {
            LOG_E("switch to PTX failed");
            return -RT_ERROR;
        }
    }

    nrf24_radio_off(&nrf24_dev);
    nrf24_txfifo_flush(&nrf24_dev);
    nrf24_clear_all_status(&nrf24_dev);

    payload[0] = cmd;
    result = nrf24_txfifo_ptx_write(&nrf24_dev, payload, sizeof(payload));
    if (result != 0)
    {
        LOG_E("clip %02u cmd 0x%02x write tx fifo failed %d", (unsigned int)card_num, cmd, result);
        return -RT_ERROR;
    }

    nrf24_radio_on(&nrf24_dev);

    for (i = 0; i < 50; i++)
    {
        nrf24_status_enum_t status;

        status = nrf24_status_routine(&nrf24_dev, nrf24_read_and_clear_status(&nrf24_dev));
        if (status & NRF24_STA_TX_SENT)
        {
            nrf24_radio_off(&nrf24_dev);
            LOG_I("clip %02u cmd 0x%02x sent", (unsigned int)card_num, cmd);
            return RT_EOK;
        }
        if (status & NRF24_STA_TX_FAIL)
        {
            nrf24_radio_off(&nrf24_dev);
            nrf24_clear_txfail_flag(&nrf24_dev);
            LOG_E("clip %02u cmd 0x%02x tx failed", (unsigned int)card_num, cmd);
            return -RT_ERROR;
        }

        rt_thread_mdelay(2);
    }

    nrf24_radio_off(&nrf24_dev);
    LOG_W("clip %02u cmd 0x%02x sent without TX_DS confirmation", (unsigned int)card_num, cmd);
    return RT_EOK;
#endif
}

rt_err_t nrf24_clip_send_cmd(rt_uint8_t cmd)
{
    return nrf24_clip_send_cmd_to(1, cmd);
}

#if defined(RT_USING_FINSH)
static void nrf24_clip_print_usage(void)
{
    rt_kprintf("usage: nrf24_clip <off|on|pulse|blink|toggle|raw 0-255> [card 1-4]\r\n");
}

static void nrf24_clip(int argc, char *argv[])
{
    rt_uint8_t cmd;
    rt_uint8_t card_num = 1;

    if (argc < 2)
    {
        nrf24_clip_print_usage();
        return;
    }

    if (strcmp(argv[1], "off") == 0)
    {
        cmd = APP_NRF24_CLIP_CMD_OFF;
    }
    else if (strcmp(argv[1], "on") == 0)
    {
        cmd = APP_NRF24_CLIP_CMD_ON;
    }
    else if (strcmp(argv[1], "pulse") == 0)
    {
        cmd = APP_NRF24_CLIP_CMD_PULSE_1S;
    }
    else if (strcmp(argv[1], "blink") == 0)
    {
        cmd = APP_NRF24_CLIP_CMD_BLINK;
    }
    else if (strcmp(argv[1], "toggle") == 0)
    {
        cmd = APP_NRF24_CLIP_CMD_TOGGLE;
    }
    else if (strcmp(argv[1], "raw") == 0)
    {
        long value;

        if (argc < 3)
        {
            nrf24_clip_print_usage();
            return;
        }

        value = strtol(argv[2], RT_NULL, 0);
        if (value < 0 || value > 255)
        {
            nrf24_clip_print_usage();
            return;
        }
        cmd = (rt_uint8_t)value;
    }
    else
    {
        nrf24_clip_print_usage();
        return;
    }

    if (argc >= 3 && strcmp(argv[1], "raw") != 0)
    {
        long card = strtol(argv[2], RT_NULL, 0);
        if (card < 1 || card > 4)
        {
            nrf24_clip_print_usage();
            return;
        }
        card_num = (rt_uint8_t)card;
    }
    else if (argc >= 4 && strcmp(argv[1], "raw") == 0)
    {
        long card = strtol(argv[3], RT_NULL, 0);
        if (card < 1 || card > 4)
        {
            nrf24_clip_print_usage();
            return;
        }
        card_num = (rt_uint8_t)card;
    }

    nrf24_clip_send_cmd_to(card_num, cmd);
}

MSH_CMD_EXPORT(nrf24_clip, send nrf24 clip led command);
#endif

int nrf24_app_init(void)
{
#ifndef PKG_USING_NRF24L01
    LOG_W("nrf24 package disabled");
    return -RT_ERROR;
#else
    rt_err_t result;
    struct rt_device *spi_dev;

    spi_dev = rt_device_find(APP_NRF24_SPI_DEVICE_NAME);
    if (spi_dev == RT_NULL)
    {
        result = rt_hw_spi_device_attach(APP_NRF24_SPI_BUS_NAME,
                                         APP_NRF24_SPI_DEVICE_NAME,
                                         APP_NRF24_CSN_GPIO,
                                         APP_NRF24_CSN_PIN);
        if (result != RT_EOK)
        {
            LOG_E("attach %s on %s csn=PA4 failed %d",
                  APP_NRF24_SPI_DEVICE_NAME,
                  APP_NRF24_SPI_BUS_NAME,
                  result);
            return result;
        }
    }

    result = nrf24_init_ins(&nrf24_dev,
                            APP_NRF24_SPI_DEVICE_NAME,
                            APP_NRF24_CE_PIN);
    if (result != 0)
    {
        LOG_E("init failed %d ce=PE3 csn=PA4", result);
        return -RT_ERROR;
    }

    result = nrf24_clip_setup_ptx(1);
    if (result != RT_EOK)
    {
        LOG_E("setup/check failed %d ce=PE3 csn=PA4", result);
        return -RT_ERROR;
    }

    nrf24_ready = RT_TRUE;
    LOG_I("ready on %s ce=PE3 csn=PA4 addr=LED01 ch=%u ptx",
          APP_NRF24_SPI_DEVICE_NAME,
          APP_NRF24_CLIP_RF_CHANNEL);
    return RT_EOK;
#endif
}
