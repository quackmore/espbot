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
#include "debug.hpp"

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

ICACHE_FLASH_ATTR Str_list::Str_list(int t_max_size)
{
    esplog.all("Str_list::Str_list\n");
    m_size = 0;
    m_max_size = t_max_size;
    m_head = NULL;
    m_tail = NULL;
    m_cursor = NULL;
}

ICACHE_FLASH_ATTR Str_list::~Str_list()
{
    esplog.all("Str_list::~Str_list\n");
    for (int idx = 0; idx < m_size; idx++)
        pop_front();
}

void ICACHE_FLASH_ATTR Str_list::init(int t_max_size)
{
    esplog.all("Str_list::init\n");
    m_size = 0;
    m_max_size = t_max_size;
    m_head = NULL;
    m_tail = NULL;
    m_cursor = NULL;
}

int ICACHE_FLASH_ATTR Str_list::size(void)
{
    esplog.all("Str_list::size\n");
    return m_size;
}

void ICACHE_FLASH_ATTR Str_list::push_back(char *t_str, bool t_to_be_free)
{
    esplog.all("Str_list::push_back\n");
    if (m_size == m_max_size)
        pop_front();
    struct List_el *el_ptr = (struct List_el *)esp_zalloc(sizeof(struct List_el));
    espmem.stack_mon();
    if (el_ptr)
    {
        m_size++;
        el_ptr->next = NULL;
        el_ptr->prev = m_tail;
        el_ptr->content = t_str;
        el_ptr->to_be_free = t_to_be_free;

        if (m_tail)
            m_tail->next = el_ptr;
        m_tail = el_ptr;

        if (m_head == NULL)
            m_head = el_ptr;
    }
    else
    {
        esplog.error("Str_list::push_back - not enough heap size (%d)", sizeof(struct List_el));
    }
}

void ICACHE_FLASH_ATTR Str_list::pop_front(void)
{
    esplog.all("Str_list::pop_front\n");
    struct List_el *el_ptr = m_head;
    espmem.stack_mon();
    if (m_head)
    {
        m_head = m_head->next;
        if (m_head)
            m_head->prev = NULL;
    }
    if ((m_tail == m_head) && (m_tail)) // there is only one element in the list
        m_tail = NULL;
    if ((el_ptr->content) && (el_ptr->to_be_free))
        esp_free(el_ptr->content);
    esp_free(el_ptr);
    m_size--;
}

char ICACHE_FLASH_ATTR *Str_list::get_head(void)
{
    esplog.all("Str_list::get_head\n");
    m_cursor = m_head;
    if (m_head)
        return m_head->content;
    else
        return NULL;
}

char ICACHE_FLASH_ATTR *Str_list::get_tail(void)
{
    esplog.all("Str_list::get_tail\n");
    m_cursor = m_tail;
    if (m_tail)
        return m_tail->content;
    else
        return NULL;
}

char ICACHE_FLASH_ATTR *Str_list::next(void)
{
    esplog.all("Str_list::next\n");
    if (m_cursor)
    {
        m_cursor = m_cursor->next;
        if (m_cursor)
            return m_cursor->content;
        else
            return NULL;
    }
    else
        return NULL;
}

char ICACHE_FLASH_ATTR *Str_list::prev(void)
{
    esplog.all("Str_list::prev\n");
    if (m_cursor)
    {
        m_cursor = m_cursor->prev;
        if (m_cursor)
            return m_cursor->content;
        else
            return NULL;
    }
    else
        return NULL;
}

ICACHE_FLASH_ATTR String::String(int t_len)
{
    esplog.all("String::String()\n");
    ref = (char *)esp_zalloc(t_len + 1);
    m_to_be_free = true;
}

ICACHE_FLASH_ATTR String::String(int t_len, bool t_to_be_free)
{
    esplog.all("String::String(,)\n");
    ref = (char *)esp_zalloc(t_len + 1);
    m_to_be_free = t_to_be_free;
}

ICACHE_FLASH_ATTR String::~String()
{
    esplog.all("String::~String\n");
    if (m_to_be_free)
        if (ref)
            esp_free(ref);
}

int ICACHE_FLASH_ATTR String::len(void)
{
    esplog.all("String::len\n");
    return os_strlen(ref);
}
