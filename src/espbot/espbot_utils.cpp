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
}

#include "espbot_utils.hpp"
#include "espbot_global.hpp"

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

ICACHE_FLASH_ATTR Str_list::Str_list(int t_max_size)
{
    m_size = 0;
    m_max_size = t_max_size;
    m_head = NULL;
    m_tail = NULL;
    m_cursor = NULL;
}

ICACHE_FLASH_ATTR Str_list::~Str_list()
{
    for (int idx = 0; idx < m_size; idx++)
        pop_front();
}

void ICACHE_FLASH_ATTR Str_list::init(int t_max_size)
{
    m_size = 0;
    m_max_size = t_max_size;
    m_head = NULL;
    m_tail = NULL;
    m_cursor = NULL;
}

int ICACHE_FLASH_ATTR Str_list::size(void)
{
    return m_size;
}

void ICACHE_FLASH_ATTR Str_list::push_back(char *t_str, bool t_to_be_free)
{
    if (m_size == m_max_size)
        pop_front();
    struct List_el *el_ptr = (struct List_el *)os_zalloc(sizeof(struct List_el));
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
    struct List_el *el_ptr = m_head;
    if (m_head)
    {
        m_head = m_head->next;
        if (m_head)
            m_head->prev = NULL;
    }
    if ((m_tail == m_head) && (m_tail)) // there is only one element in the list
        m_tail = NULL;
    if ((el_ptr->content) && (el_ptr->to_be_free))
        os_free(el_ptr->content);
    os_free(el_ptr);
    m_size--;
}

char ICACHE_FLASH_ATTR *Str_list::get_head(void)
{
    m_cursor = m_head;
    if (m_head)
        return m_head->content;
    else
        return NULL;
}

char ICACHE_FLASH_ATTR *Str_list::get_tail(void)
{
    m_cursor = m_tail;
    if (m_tail)
        return m_tail->content;
    else
        return NULL;
}

char ICACHE_FLASH_ATTR *Str_list::next(void)
{
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
