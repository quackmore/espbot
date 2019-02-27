/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */

#include "ets_sys.h"
#include "gpio.h"
#include "esp8266_io.h"

int gpio_NUM(int idx)
{
    switch (idx)
    {
        case ESPBOT_D1:
            return ESPBOT_D1_NUM;
        case ESPBOT_D2:
            return ESPBOT_D2_NUM;
        case ESPBOT_D3:
            return ESPBOT_D3_NUM;
        case ESPBOT_D4:
            return ESPBOT_D4_NUM;
        case ESPBOT_D5:
            return ESPBOT_D5_NUM;
        case ESPBOT_D6:
            return ESPBOT_D6_NUM;
        case ESPBOT_D7:
            return ESPBOT_D7_NUM;
        case ESPBOT_D8:
            return ESPBOT_D8_NUM;
        default:
            return -1;
    }
}

int gpio_MUX(int idx)
{
    switch (idx)
    {
        case ESPBOT_D1:
            return ESPBOT_D1_MUX;
        case ESPBOT_D2:
            return ESPBOT_D2_MUX;
        case ESPBOT_D3:
            return ESPBOT_D3_MUX;
        case ESPBOT_D4:
            return ESPBOT_D4_MUX;
        case ESPBOT_D5:
            return ESPBOT_D5_MUX;
        case ESPBOT_D6:
            return ESPBOT_D6_MUX;
        case ESPBOT_D7:
            return ESPBOT_D7_MUX;
        case ESPBOT_D8:
            return ESPBOT_D8_MUX;
        default:
            return -1;
    }
}

int gpio_FUNC(int idx)
{
    switch (idx)
    {
        case ESPBOT_D1:
            return ESPBOT_D1_FUNC;
        case ESPBOT_D2:
            return ESPBOT_D2_FUNC;
        case ESPBOT_D3:
            return ESPBOT_D3_FUNC;
        case ESPBOT_D4:
            return ESPBOT_D4_FUNC;
        case ESPBOT_D5:
            return ESPBOT_D5_FUNC;
        case ESPBOT_D6:
            return ESPBOT_D6_FUNC;
        case ESPBOT_D7:
            return ESPBOT_D7_FUNC;
        case ESPBOT_D8:
            return ESPBOT_D8_FUNC;
        default:
            return -1;
    }
}