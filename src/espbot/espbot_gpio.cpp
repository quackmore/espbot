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

#include "espbot_debug.hpp"
#include "espbot_global.hpp"
#include "espbot_gpio.hpp"
#include "spiffs_esp8266.hpp"
#include "espbot_logger.hpp"
#include "espbot_json.hpp"
#include "espbot_config.hpp"


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