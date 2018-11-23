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
#include "osapi.h"
//#include "mem.h"
#include "user_interface.h"
}

#include "espbot_utils.hpp"

int ICACHE_FLASH_ATTR atoi(char *str)
{
    int idx = 0;
    int power = 1;
    char tmpvalue;
    char result = 0;
    while (str[idx] != '\0')
    {
        tmpvalue = 0;
        if (str[idx] >= '0' && str[idx] <= '9')
            tmpvalue = str[idx] - '0';
        result = result * power + tmpvalue;
        idx++;
        power = power * 10;
    }
    return result;
}

int ICACHE_FLASH_ATTR atoh(char *str)
{
    int idx = 0;
    int power = 1;
    char tmpvalue;
    char result = 0;
    while (str[idx] != '\0')
    {
        tmpvalue = 0;
        if (str[idx] >= 'a' && str[idx] <= 'f')
            tmpvalue = str[idx] - 'a' + 10;
        else if (str[idx] >= 'A' && str[idx] <= 'F')
            tmpvalue = str[idx] - 'A' + 10;
        else if (str[idx] >= '0' && str[idx] <= '9')
            tmpvalue = str[idx] - '0';
        result = result * power + tmpvalue;
        idx++;
        power = power * 16;
    }
    return result;
}

void ICACHE_FLASH_ATTR decodeUrlStr(char *str)
{
    char *tmpptr = str;
    char hexchar[3];
    int idx = 0;
    hexchar[2] = 0;
    while (str[idx] != '\0')
    {
        if (str[idx] == '%')
        {
            hexchar[0] = str[idx + 1];
            hexchar[1] = str[idx + 2];
            *tmpptr++ = atoh(hexchar);
            idx += 3;
        }
        else
        {
            *tmpptr++ = str[idx++];
        }
    }
    *tmpptr = '\0';
}
