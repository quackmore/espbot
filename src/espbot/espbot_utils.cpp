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
#include "mem.h"
#include "user_interface.h"
#include "ip_addr.h"
}

#include "espbot_utils.hpp"
#include "espbot_global.hpp"
#include "espbot_debug.hpp"

int ICACHE_FLASH_ATTR atoh(char *str)
{
    esplog.all("atoh\n");
    int idx = 0;
    int power = 16;
    int tmpvalue;
    int result = 0;
    espmem.stack_mon();
    while (str[idx] != '\0')
    {
        tmpvalue = 0;
        if (str[idx] >= 'a' && str[idx] <= 'f')
            tmpvalue = str[idx] - 'a' + 10;
        else if (str[idx] >= 'A' && str[idx] <= 'F')
            tmpvalue = str[idx] - 'A' + 10;
        else if (str[idx] >= '0' && str[idx] <= '9')
            tmpvalue = str[idx] - '0';
        if (idx == 0)
            result = tmpvalue;
        else
            result = result * power + tmpvalue;
        idx++;
    }
    return result;
}

void ICACHE_FLASH_ATTR decodeUrlStr(char *str)
{
    esplog.all("decodeUrlStr\n");
    char *tmpptr = str;
    char hexchar[3];
    int idx = 0;
    hexchar[2] = 0;
    espmem.stack_mon();
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

void ICACHE_FLASH_ATTR atoipaddr(struct ip_addr *ip, char *str)
{
    esplog.all("atoipaddr\n");
    char *tmp_ptr = str;
    char *tmp_str, *end_ptr;
    int len;
    int cnt = 0;
    int tmp_ip[4];
    espmem.stack_mon();
    while (*tmp_ptr == ' ')
        *tmp_ptr++;
    do
    {
        if (cnt < 3)
            end_ptr = (char *)os_strstr(tmp_ptr, ".");
        else
            end_ptr = str + os_strlen(str);
        if (end_ptr == NULL)
        {
            esplog.trace("str_to_ipaddr - cannot find separator dot\n");
            IP4_ADDR(ip, 1, 1, 1, 1);
            return;
        }
        len = end_ptr - tmp_ptr;
        tmp_str = (char *)esp_zalloc(len + 1);
        if (tmp_str == NULL)
        {
            esplog.error("str_to_ipaddr - not enough heap memory\n");
            IP4_ADDR(ip, 1, 1, 1, 1);
            return;
        }
        os_strncpy(tmp_str, tmp_ptr, len);
        tmp_ip[cnt] = atoi(tmp_str);
        esp_free(tmp_str);
        tmp_ptr = end_ptr + 1;
        cnt++;
    } while (cnt <= 3);
    IP4_ADDR(ip, tmp_ip[0], tmp_ip[1], tmp_ip[2], tmp_ip[3]);
}

int ICACHE_FLASH_ATTR get_rand_int(int max_value)
{
    esplog.all("get_rand_int\n");
    float value = (((float)os_random()) / ((float) __UINT32_MAX__)) * ((float)max_value);
    return (int)value;
}

char ICACHE_FLASH_ATTR *f2str(char *str, float value, int decimals)
{
    int32 value_int = (int32)value;
    float value_dec = value - value_int;
    if (value_dec < 0)
        value_dec = -(value_dec);
    int idx;
    int pow = 1;
    for (idx = 0; idx < decimals; idx++)
        pow = pow * 10;
    os_sprintf(str, "%d.%d", (int32)value, (int32)(value_dec * pow));
    return str;
}

ICACHE_FLASH_ATTR Heap_chunk::Heap_chunk(int t_len, Free_opt t_to_be_free)
{
    esplog.all("Heap_chunk::Heap_chunk\n");
    ref = (char *)esp_zalloc(t_len + 1);
    m_to_be_free = t_to_be_free;
}

ICACHE_FLASH_ATTR Heap_chunk::~Heap_chunk()
{
    esplog.all("Heap_chunk::~Heap_chunk\n");
    if (m_to_be_free == free)
        if (ref)
            esp_free(ref);
}

int ICACHE_FLASH_ATTR Heap_chunk::len(void)
{
    esplog.all("Heap_chunk::len\n");
    return os_strlen(ref);
}