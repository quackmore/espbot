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
#include "ip_addr.h"
#include "mem.h"
#include "osapi.h"
#include "user_interface.h"
}

#include "espbot_diagnostic.hpp"
#include "espbot_event_codes.h"
#include "espbot_global.hpp"
#include "espbot_mem_mon.hpp"
#include "espbot_utils.hpp"

int atoh(char *str)
{
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

void decodeUrlStr(char *str)
{
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

void atoipaddr(struct ip_addr *ip, char *str)
{
    char *tmp_ptr = str;
    char *end_ptr;
    int len;
    int cnt = 0;
    int tmp_ip[4];
    char tmp_str[4];
    espmem.stack_mon();
    while (*tmp_ptr == ' ')
        *tmp_ptr++;
    do
    {
        if (cnt < 3)
            end_ptr = (char *)os_strstr(tmp_ptr, f_str("."));
        else
            end_ptr = str + os_strlen(str);
        if (end_ptr == NULL)
        {
            dia_error_evnt(UTILS_CANNOT_PARSE_IP);
            TRACE("atoipaddr cannot parse IP");
            IP4_ADDR(ip, 1, 1, 1, 1);
            return;
        }
        len = end_ptr - tmp_ptr;
        if(len > 3)
        {
            dia_error_evnt(UTILS_CANNOT_PARSE_IP);
            TRACE("atoipaddr cannot parse IP");
            IP4_ADDR(ip, 1, 1, 1, 1);
            return;
        }
        os_memset(tmp_str, 0, 4);
        os_strncpy(tmp_str, tmp_ptr, 3);
        tmp_ip[cnt] = atoi(tmp_str);
        tmp_ptr = end_ptr + 1;
        cnt++;
    } while (cnt <= 3);
    IP4_ADDR(ip, tmp_ip[0], tmp_ip[1], tmp_ip[2], tmp_ip[3]);
}

int get_rand_int(int max_value)
{
    float value = (((float)os_random()) / ((float)__UINT32_MAX__)) * ((float)max_value);
    return (int)value;
}

char *f2str(char *str, float value, int decimals)
{
    int32 value_int = (int32)value;
    float value_dec = value - value_int;
    if (value_dec < 0)
        value_dec = -(value_dec);
    int idx;
    int pow = 1;
    for (idx = 0; idx < decimals; idx++)
        pow = pow * 10;
    fs_sprintf(str, "%d.%d", (int32)value, (int32)(value_dec * pow));
    return str;
}

Heap_chunk::Heap_chunk(int t_len, Free_opt t_to_be_free)
{
    ref = new char[t_len + 1];
    m_to_be_free = t_to_be_free;
}

Heap_chunk::~Heap_chunk()
{
    if (m_to_be_free == free)
        if (ref)
            delete[] ref;
}

int Heap_chunk::len(void)
{
    return os_strlen(ref);
}