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

#include "espbot_config.hpp"
#include "espbot_diagnostic.hpp"
#include "espbot_event_codes.h"
#include "espbot_global.hpp"
#include "espbot_gpio.hpp"
#include "espbot_json.hpp"
#include "espbot_mem_mon.hpp"
#include "spiffs_esp8266.hpp"

#define ESPBOT_INPUT_GET(input_reg, gpio_no) ((input_reg >> gpio_no) & BIT0)

static int gpio_getNum(int idx)
{
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
        dia_warn_evnt(GPIO_GETNUM_WRONG_INDEX);
        ERROR("Gpio::set_as_input wrong index");
        return ESPBOT_GPIO_WRONG_IDX;
    }
}

void Gpio::init(void)
{
    m_gpio_provisioned = 0;
    m_gpio_config = 0;
}

int Gpio::config(int t_idx, int t_type)
{
    if ((t_idx >= ESPBOT_D1) && (t_idx <= ESPBOT_D8))
    {
        m_gpio_provisioned = m_gpio_provisioned | (0x00000001 << t_idx);
        if (t_type == ESPBOT_GPIO_INPUT)
            m_gpio_config = ~((~m_gpio_config) | (0x00000001 << t_idx));
        else if (t_type == ESPBOT_GPIO_OUTPUT)
            m_gpio_config = m_gpio_config | (0x00000001 << t_idx);
        else
        {
            dia_warn_evnt(GPIO_CONFIG_WRONG_TYPE);
            ERROR("Gpio::config wrong type");
            return ESPBOT_GPIO_WRONG_TYPE;
        }

        switch (t_idx)
        {
        case ESPBOT_D1:
            PIN_FUNC_SELECT(ESPBOT_D1_MUX, ESPBOT_D1_FUNC);
            if (t_type == ESPBOT_GPIO_INPUT)
                GPIO_DIS_OUTPUT(ESPBOT_D1_NUM);
            break;
        case ESPBOT_D2:
            PIN_FUNC_SELECT(ESPBOT_D2_MUX, ESPBOT_D2_FUNC);
            if (t_type == ESPBOT_GPIO_INPUT)
                GPIO_DIS_OUTPUT(ESPBOT_D2_NUM);
            break;
        case ESPBOT_D3:
            PIN_FUNC_SELECT(ESPBOT_D3_MUX, ESPBOT_D3_FUNC);
            if (t_type == ESPBOT_GPIO_INPUT)
                GPIO_DIS_OUTPUT(ESPBOT_D3_NUM);
            break;
        case ESPBOT_D4:
            PIN_FUNC_SELECT(ESPBOT_D4_MUX, ESPBOT_D4_FUNC);
            if (t_type == ESPBOT_GPIO_INPUT)
                GPIO_DIS_OUTPUT(ESPBOT_D4_NUM);
            break;
        case ESPBOT_D5:
            PIN_FUNC_SELECT(ESPBOT_D5_MUX, ESPBOT_D5_FUNC);
            if (t_type == ESPBOT_GPIO_INPUT)
                GPIO_DIS_OUTPUT(ESPBOT_D5_NUM);
            break;
        case ESPBOT_D6:
            PIN_FUNC_SELECT(ESPBOT_D6_MUX, ESPBOT_D6_FUNC);
            if (t_type == ESPBOT_GPIO_INPUT)
                GPIO_DIS_OUTPUT(ESPBOT_D6_NUM);
            break;
        case ESPBOT_D7:
            PIN_FUNC_SELECT(ESPBOT_D7_MUX, ESPBOT_D7_FUNC);
            if (t_type == ESPBOT_GPIO_INPUT)
                GPIO_DIS_OUTPUT(ESPBOT_D7_NUM);
            break;
        case ESPBOT_D8:
            PIN_FUNC_SELECT(ESPBOT_D8_MUX, ESPBOT_D8_FUNC);
            if (t_type == ESPBOT_GPIO_INPUT)
                GPIO_DIS_OUTPUT(ESPBOT_D8_NUM);
            break;
        default:
            break;
        }
        return ESPBOT_GPIO_OK;
    }
    else
    {
        dia_warn_evnt(GPIO_CONFIG_WRONG_INDEX);
        ERROR("Gpio::config wrong index");
        return ESPBOT_GPIO_WRONG_IDX;
    }
}

int Gpio::unconfig(int t_idx)
{
    if ((t_idx >= ESPBOT_D1) && (t_idx <= ESPBOT_D8))
    {
        m_gpio_provisioned = ~((~m_gpio_provisioned) | (0x00000001 << t_idx));
        return ESPBOT_GPIO_OK;
    }
    else
    {
        dia_warn_evnt(GPIO_UNCONFIG_WRONG_INDEX);
        ERROR("Gpio::unconfig wrong gpio index");
        return ESPBOT_GPIO_WRONG_IDX;
    }
}

int Gpio::get_config(int t_idx)
{
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
        dia_warn_evnt(GPIO_GET_CONFIG_WRONG_INDEX);
        ERROR("Gpio::get_config wrong index");
        return ESPBOT_GPIO_WRONG_IDX;
    }
}

int Gpio::read(int t_idx)
{
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
        dia_warn_evnt(GPIO_READ_WRONG_INDEX);
        ERROR("Gpio::read wrong index");
        return ESPBOT_GPIO_WRONG_IDX;
    }
}

int Gpio::set(int t_idx, int t_value)
{
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
                    dia_warn_evnt(GPIO_SET_WRONG_LEVEL);
                    ERROR("Gpio::set wrong level");
                    return ESPBOT_GPIO_WRONG_LVL;
                }
            }
            else
            {
                dia_warn_evnt(GPIO_SET_CANNOT_CHANGE);
                ERROR("Gpio::set cannot change input");
                return ESPBOT_GPIO_CANNOT_CHANGE_INPUT;
            }
        }
        else
            return ESPBOT_GPIO_UNPROVISIONED;
    }
    else
    {
        dia_warn_evnt(GPIO_GETNUM_WRONG_INDEX);
        ERROR("Gpio::set wrong index");
        return ESPBOT_GPIO_WRONG_IDX;
    }
}