/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */
// SDK includes
extern "C"
{
#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "user_interface.h"
}

#include "debug.hpp"
#include "espbot_global.hpp"
#include "gpio.hpp"
#include "esp8266_spiffs.hpp"
#include "logger.hpp"
#include "json.hpp"
#include "config.hpp"

/*
 * Digital I/O map for MODEMCU, WEMOS D1 mini
 * 
 *    gpio   pin   mux
 * -----------------------
 * D1 GPIO5  pin24 GPIO5_U
 * D2 GPIO4  pin16 GPIO4_U
 * D3 GPIO0  pin15 GPIO0_U
 * D4 GPIO2  pin14 GPIO2_U
 * D5 GPIO14 pin9  MTMS_U
 * D6 GPIO12 pin10 MTDI_U
 * D7 GPIO13 pin12 MTCK_U
 * D8 GPIO15 pin13 MTDO_U
 */

#define ESPBOT_D1_NUM 5
#define ESPBOT_D2_NUM 4
#define ESPBOT_D3_NUM 0
#define ESPBOT_D4_NUM 2
#define ESPBOT_D5_NUM 14
#define ESPBOT_D6_NUM 12
#define ESPBOT_D7_NUM 13
#define ESPBOT_D8_NUM 15

#define ESPBOT_D1_MUX PERIPHS_IO_MUX_GPIO5_U
#define ESPBOT_D1_FUNC FUNC_GPIO5
#define ESPBOT_D2_MUX PERIPHS_IO_MUX_GPIO4_U
#define ESPBOT_D2_FUNC FUNC_GPIO4
#define ESPBOT_D3_MUX PERIPHS_IO_MUX_GPIO0_U
#define ESPBOT_D3_FUNC FUNC_GPIO0
#define ESPBOT_D4_MUX PERIPHS_IO_MUX_GPIO2_U
#define ESPBOT_D4_FUNC FUNC_GPIO2
#define ESPBOT_D5_MUX PERIPHS_IO_MUX_MTMS_U
#define ESPBOT_D5_FUNC FUNC_GPIO14
#define ESPBOT_D6_MUX PERIPHS_IO_MUX_MTDI_U
#define ESPBOT_D6_FUNC FUNC_GPIO12
#define ESPBOT_D7_MUX PERIPHS_IO_MUX_MTCK_U
#define ESPBOT_D7_FUNC FUNC_GPIO13
#define ESPBOT_D8_MUX PERIPHS_IO_MUX_MTDO_U
#define ESPBOT_D8_FUNC FUNC_GPIO15

#define ESPBOT_INPUT_GET(input_reg, gpio_no) ((input_reg >> gpio_no) & BIT0)

static int ICACHE_FLASH_ATTR gpio_getNum(int idx)
{
    esplog.all("gpio_getNum\n");
    int num;
    if (idx >= ESPBOT_D1 && idx <= ESPBOT_D8)
    {
        switch (idx)
        {
        case ESPBOT_D1:
            num = ESPBOT_D1_NUM;
            break;
        case ESPBOT_D2:
            num = ESPBOT_D2_NUM;
            break;
        case ESPBOT_D3:
            num = ESPBOT_D3_NUM;
            break;
        case ESPBOT_D4:
            num = ESPBOT_D4_NUM;
            break;
        case ESPBOT_D5:
            num = ESPBOT_D5_NUM;
            break;
        case ESPBOT_D6:
            num = ESPBOT_D6_NUM;
            break;
        case ESPBOT_D7:
            num = ESPBOT_D7_NUM;
            break;
        case ESPBOT_D8:
            num = ESPBOT_D8_NUM;
            break;
        default:
            break;
        }
        return num;
    }
    else
    {
        esplog.error("Gpio::set_as_input: wrong gpio index.\n");
        return ESPBOT_GPIO_WRONG_IDX;
    }
}

void ICACHE_FLASH_ATTR Gpio::init(void)
{
    esplog.all("Gpio::init\n");
    m_gpio_provisioned = 0;
    m_gpio_config = 0;
}

int ICACHE_FLASH_ATTR Gpio::config(int t_idx, int t_type)
{
    esplog.all("Gpio::config\n");
    if ((t_idx >= ESPBOT_D1) && (t_idx <= ESPBOT_D8))
    {
        m_gpio_provisioned = m_gpio_provisioned | (0x00000001 << t_idx);
        if (t_type == ESPBOT_GPIO_INPUT)
            m_gpio_config = ~((~m_gpio_config) | (0x00000001 << t_idx));
        else if (t_type == ESPBOT_GPIO_OUTPUT)
            m_gpio_config = m_gpio_config | (0x00000001 << t_idx);
        else
        {
            esplog.error("Gpio::config: wrong gpio type\n");
            return ESPBOT_GPIO_WRONG_TYPE;
        }

        switch (t_idx)
        {
        case ESPBOT_D1:
            PIN_FUNC_SELECT(ESPBOT_D1_MUX, ESPBOT_D1_FUNC);
            if (t_type == ESPBOT_GPIO_INPUT)
                GPIO_DIS_OUTPUT(ESPBOT_D1_NUM);
            else
                GPIO_OUTPUT_SET(ESPBOT_D1_NUM, ESPBOT_LOW);
            break;
        case ESPBOT_D2:
            PIN_FUNC_SELECT(ESPBOT_D2_MUX, ESPBOT_D2_FUNC);
            if (t_type == ESPBOT_GPIO_INPUT)
                GPIO_DIS_OUTPUT(ESPBOT_D2_NUM);
            else
                GPIO_OUTPUT_SET(ESPBOT_D2_NUM, ESPBOT_LOW);
            break;
        case ESPBOT_D3:
            PIN_FUNC_SELECT(ESPBOT_D3_MUX, ESPBOT_D3_FUNC);
            if (t_type == ESPBOT_GPIO_INPUT)
                GPIO_DIS_OUTPUT(ESPBOT_D3_NUM);
            else
                GPIO_OUTPUT_SET(ESPBOT_D3_NUM, ESPBOT_LOW);
            break;
        case ESPBOT_D4:
            PIN_FUNC_SELECT(ESPBOT_D4_MUX, ESPBOT_D4_FUNC);
            if (t_type == ESPBOT_GPIO_INPUT)
                GPIO_DIS_OUTPUT(ESPBOT_D4_NUM);
            else
                GPIO_OUTPUT_SET(ESPBOT_D4_NUM, ESPBOT_LOW);
            break;
        case ESPBOT_D5:
            PIN_FUNC_SELECT(ESPBOT_D5_MUX, ESPBOT_D5_FUNC);
            if (t_type == ESPBOT_GPIO_INPUT)
                GPIO_DIS_OUTPUT(ESPBOT_D5_NUM);
            else
                GPIO_OUTPUT_SET(ESPBOT_D5_NUM, ESPBOT_LOW);
            break;
        case ESPBOT_D6:
            PIN_FUNC_SELECT(ESPBOT_D6_MUX, ESPBOT_D6_FUNC);
            if (t_type == ESPBOT_GPIO_INPUT)
                GPIO_DIS_OUTPUT(ESPBOT_D6_NUM);
            else
                GPIO_OUTPUT_SET(ESPBOT_D6_NUM, ESPBOT_LOW);
            break;
        case ESPBOT_D7:
            PIN_FUNC_SELECT(ESPBOT_D7_MUX, ESPBOT_D7_FUNC);
            if (t_type == ESPBOT_GPIO_INPUT)
                GPIO_DIS_OUTPUT(ESPBOT_D7_NUM);
            else
                GPIO_OUTPUT_SET(ESPBOT_D7_NUM, ESPBOT_LOW);
            break;
        case ESPBOT_D8:
            PIN_FUNC_SELECT(ESPBOT_D8_MUX, ESPBOT_D8_FUNC);
            if (t_type == ESPBOT_GPIO_INPUT)
                GPIO_DIS_OUTPUT(ESPBOT_D8_NUM);
            else
                GPIO_OUTPUT_SET(ESPBOT_D8_NUM, ESPBOT_LOW);
            break;
        default:
            break;
        }
        return ESPBOT_GPIO_OK;
    }
    else
    {
        esplog.error("Gpio::config: wrong gpio index\n");
        return ESPBOT_GPIO_WRONG_IDX;
    }
}

int ICACHE_FLASH_ATTR Gpio::unconfig(int t_idx)
{
    esplog.all("Gpio::unconfig\n");
    if ((t_idx >= ESPBOT_D1) && (t_idx <= ESPBOT_D8))
    {
        m_gpio_provisioned = ~((~m_gpio_provisioned) | (0x00000001 << t_idx));
        return ESPBOT_GPIO_OK;
    }
    else
    {
        esplog.error("Gpio::unconfig: wrong gpio index\n");
        return ESPBOT_GPIO_WRONG_IDX;
    }
}

int ICACHE_FLASH_ATTR Gpio::get_config(int t_idx)
{
    esplog.all("Gpio::get_config\n");
    if ((t_idx >= ESPBOT_D1) && (t_idx <= ESPBOT_D8))
    {
        if (m_gpio_provisioned & (0x00000001 << t_idx))
        {
            if (m_gpio_config & (0x00000001 << t_idx))
                return ESPBOT_GPIO_OUTPUT;
            else
                return ESPBOT_GPIO_INPUT;
        }
        else
            return ESPBOT_GPIO_UNPROVISIONED;
    }
    else
    {
        esplog.error("Gpio::get_config: wrong gpio index\n");
        return ESPBOT_GPIO_WRONG_IDX;
    }
}

int ICACHE_FLASH_ATTR Gpio::read(int t_idx)
{
    esplog.all("Gpio::read\n");
    if ((t_idx >= ESPBOT_D1) && (t_idx <= ESPBOT_D8))
    {
        if (m_gpio_provisioned & (0x00000001 << t_idx))
        {
            return (ESPBOT_INPUT_GET(gpio_input_get(), gpio_getNum(t_idx)));
        }
        else
        {
            return ESPBOT_GPIO_UNPROVISIONED;
        }
    }
    else
    {
        esplog.error("Gpio::read: wrong gpio index.\n");
        return ESPBOT_GPIO_WRONG_IDX;
    }
}

int ICACHE_FLASH_ATTR Gpio::set(int t_idx, int t_value)
{
    esplog.all("Gpio::set\n");
    if ((t_idx >= ESPBOT_D1) && (t_idx <= ESPBOT_D8))
    {
        if (m_gpio_provisioned & (0x00000001 << t_idx))
        {
            if (m_gpio_config & (0x00000001 << t_idx))
            {
                if ((t_value == ESPBOT_LOW) || (t_value == ESPBOT_HIGH))
                {
                    switch (t_idx)
                    {
                    case ESPBOT_D1:
                        GPIO_OUTPUT_SET(ESPBOT_D1_NUM, t_value);
                        break;
                    case ESPBOT_D2:
                        GPIO_OUTPUT_SET(ESPBOT_D2_NUM, t_value);
                        break;
                    case ESPBOT_D3:
                        GPIO_OUTPUT_SET(ESPBOT_D3_NUM, t_value);
                        break;
                    case ESPBOT_D4:
                        GPIO_OUTPUT_SET(ESPBOT_D4_NUM, t_value);
                        break;
                    case ESPBOT_D5:
                        GPIO_OUTPUT_SET(ESPBOT_D5_NUM, t_value);
                        break;
                    case ESPBOT_D6:
                        GPIO_OUTPUT_SET(ESPBOT_D6_NUM, t_value);
                        break;
                    case ESPBOT_D7:
                        GPIO_OUTPUT_SET(ESPBOT_D7_NUM, t_value);
                        break;
                    case ESPBOT_D8:
                        GPIO_OUTPUT_SET(ESPBOT_D8_NUM, t_value);
                        break;
                    default:
                        break;
                    }
                    return ESPBOT_GPIO_OK;
                }
                else
                {
                    esplog.error("Gpio::set: unknown output level\n");
                    return ESPBOT_GPIO_WRONG_LVL;
                }
            }
            else
            {
                esplog.error("Gpio::set: cannot change input\n");
                return ESPBOT_GPIO_CANNOT_CHANGE_INPUT;
            }
        }
        else
            return ESPBOT_GPIO_UNPROVISIONED;
    }
    else
    {
        esplog.error("Gpio::set: wrong gpio index.\n");
        return ESPBOT_GPIO_WRONG_IDX;
    }
}