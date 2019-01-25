/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */

extern "C"
{
#include "mem.h"
#include "user_interface.h"
}

#include "debug.hpp"
#include "espbot_global.hpp"

/*
 * DEBUGGER
 */

void ICACHE_FLASH_ATTR Esp_mem::init(void)
{
    uint32 first_stack_var;
    void *heap_var = os_zalloc(1);

    // stack vars
    m_stack_min_addr = (uint32)&first_stack_var;
    m_stack_max_addr = (uint32)&first_stack_var;

    // init heap infos and data structures
    m_heap_start_addr = (uint32)heap_var;
    if (heap_var)
        os_free(heap_var);
    m_max_heap_size = system_get_free_heap_size();
    m_min_heap_size = m_max_heap_size;
    m_heap_objs = 0;
    m_max_heap_objs = 0;

    int ii;
    for (ii = 0; ii < HEAP_ARRAY_SIZE; ii++)
    {
        m_heap_array[ii].size = 0;
        m_heap_array[ii].addr = 0;
        m_heap_array[ii].next_item = ii + 1;
    }
    m_heap_array[HEAP_ARRAY_SIZE - 1].next_item = -1;
    m_first_heap_item = -1;
    m_first_free_heap_item = 0;
    stack_mon();
}

void ICACHE_FLASH_ATTR Esp_mem::heap_mon(void)
{
    uint32 currentHeap = system_get_free_heap_size();
    //esplog.all("Esp_mem::heap_mon: %d\n", currentHeap);
    if (m_min_heap_size > currentHeap)
        m_min_heap_size = currentHeap;
}

void ICACHE_FLASH_ATTR Esp_mem::stack_mon(void)
{
    uint32 stack_var_addr = (uint32)&stack_var_addr;
    if (stack_var_addr > m_stack_max_addr)
        m_stack_max_addr = stack_var_addr;
    if (stack_var_addr < m_stack_min_addr)
        m_stack_min_addr = stack_var_addr;
}

void *Esp_mem::espbot_zalloc(size_t size)
{
    void *addr = os_zalloc(size);
    if (addr)
    {
        espmem.m_heap_objs++;
        if (espmem.m_heap_objs > espmem.m_max_heap_objs)
            espmem.m_max_heap_objs = espmem.m_heap_objs;
        if (espmem.m_first_free_heap_item != -1)
        {
            int tmp_ptr = espmem.m_first_free_heap_item;
            espmem.m_first_free_heap_item = espmem.m_heap_array[espmem.m_first_free_heap_item].next_item;
            espmem.m_heap_array[tmp_ptr].size = size;
            espmem.m_heap_array[tmp_ptr].addr = addr;
            espmem.m_heap_array[tmp_ptr].next_item = espmem.m_first_heap_item;
            espmem.m_first_heap_item = tmp_ptr;
            espmem.stack_mon();
        }
    }
    else
    {
        esplog.fatal("Esp_mem::espbot_zalloc heap exhausted\n");
    }
    espmem.heap_mon();
    return addr;
}

void Esp_mem::espbot_free(void *addr)
{
    espmem.m_heap_objs--;
    os_free(addr);
    int prev_ptr = espmem.m_first_heap_item;
    int tmp_ptr = espmem.m_first_heap_item;
    espmem.stack_mon();
    while (tmp_ptr != -1)
    {
        if (espmem.m_heap_array[tmp_ptr].addr == addr)
        {
            // eliminate cuurent item from heap allocated items list
            if (tmp_ptr == espmem.m_first_heap_item)
                espmem.m_first_heap_item = espmem.m_heap_array[tmp_ptr].next_item;
            else
                espmem.m_heap_array[prev_ptr].next_item = espmem.m_heap_array[tmp_ptr].next_item;

            // clear the heap item
            espmem.m_heap_array[tmp_ptr].size = 0;
            espmem.m_heap_array[tmp_ptr].addr = 0;

            // insert it into free heap items list
            espmem.m_heap_array[tmp_ptr].next_item = espmem.m_first_free_heap_item;
            espmem.m_first_free_heap_item = tmp_ptr;

            break;
        }
        prev_ptr = tmp_ptr;
        tmp_ptr = espmem.m_heap_array[tmp_ptr].next_item;
    }
}

uint32 ICACHE_FLASH_ATTR Esp_mem::get_min_stack_addr(void)
{
    esplog.all("Esp_mem::get_min_stack_addr\n");
    return m_stack_min_addr;
}

uint32 ICACHE_FLASH_ATTR Esp_mem::get_max_stack_addr(void)
{
    esplog.all("Esp_mem::get_max_stack_addr\n");
    return m_stack_max_addr;
}

uint32 ICACHE_FLASH_ATTR Esp_mem::get_start_heap_addr(void)
{
    esplog.all("Esp_mem::get_start_heap_addr\n");
    return m_heap_start_addr;
}

uint32 ICACHE_FLASH_ATTR Esp_mem::get_max_heap_size(void)
{
    esplog.all("Esp_mem::get_max_heap_size\n");
    return m_max_heap_size;
}

uint32 ICACHE_FLASH_ATTR Esp_mem::get_mim_heap_size(void)
{
    esplog.all("Esp_mem::get_mim_heap_size\n");
    return m_min_heap_size;
}

uint32 ICACHE_FLASH_ATTR Esp_mem::get_used_heap_size(void)
{
    esplog.all("Esp_mem::get_used_heap_size\n");
    int used_heap = 0;
    struct heap_item *item_ptr = next_heap_item(0);
    stack_mon();
    while (item_ptr)
    {
        used_heap += item_ptr->size;
        item_ptr = next_heap_item(1);
    }
    return used_heap;
}

uint32 ICACHE_FLASH_ATTR Esp_mem::get_max_heap_objs(void)
{
    esplog.all("Esp_mem::get_max_heap_objs\n");
    return m_max_heap_objs;
}

struct heap_item *Esp_mem::next_heap_item(int value)
{
    esplog.all("next_heap_item\n");
    static struct heap_item *item_ptr = NULL;
    stack_mon();
    if (value == 0)
    {
        if (m_first_heap_item != -1)
            item_ptr = &m_heap_array[m_first_heap_item];
    }
    else
    {
        if (item_ptr)
        {
            if (item_ptr->next_item != -1)
                item_ptr = &m_heap_array[item_ptr->next_item];
            else
                item_ptr = NULL;
        }
    }
    return item_ptr;
}