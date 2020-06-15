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

#include "espbot_diagnostic.hpp"
#include "espbot_event_codes.h"
#include "espbot_global.hpp"
#include "espbot_mem_mon.hpp"

/*
 * DEBUGGER
 */

void Esp_mem::init(void)
{
    uint32 first_stack_var;
    void *heap_var = os_zalloc(1);

    // stack vars
    _stack_min_addr = (uint32)&first_stack_var;
    _stack_max_addr = (uint32)&first_stack_var;

    // init heap infos and data structures
    _heap_start_addr = (uint32)heap_var;
    if (heap_var)
        os_free(heap_var);
    _max_heap_size = system_get_free_heap_size();
    _min_heap_size = _max_heap_size;
    _heap_objs = 0;
    _max_heap_objs = 0;

    // DEBUG
    // int ii;
    // for (ii = 0; ii < HEAP_ARRAY_SIZE; ii++)
    // {
    //     _heap_array[ii].size = -1;
    //     _heap_array[ii].addr = NULL;
    // }
    stack_mon();
}

void Esp_mem::heap_mon(void)
{
    uint32 currentHeap = system_get_free_heap_size();
    if (_min_heap_size > currentHeap)
        _min_heap_size = currentHeap;
}

void Esp_mem::stack_mon(void)
{
    uint32 stack_var_addr = (uint32)&stack_var_addr;
    if (stack_var_addr > _stack_max_addr)
        _stack_max_addr = stack_var_addr;
    if (stack_var_addr < _stack_min_addr)
        _stack_min_addr = stack_var_addr;
}

void *Esp_mem::espbot_zalloc(size_t size)
{
    void *addr = os_zalloc(size);
    if (addr)
    {
        espmem._heap_objs++;
        if (espmem._heap_objs > espmem._max_heap_objs)
            espmem._max_heap_objs = espmem._heap_objs;
        // DEBUG
        // int idx;
        // for (idx = 0; idx < HEAP_ARRAY_SIZE; idx++)
        // {
        //     if (espmem._heap_array[idx].size == -1)
        //     {
        //         espmem._heap_array[idx].size = size;
        //         espmem._heap_array[idx].addr = addr;
        //         break;
        //     }
        // }
        // espmem.stack_mon();
    }
    else
    {
        esp_diag.fatal(MEM_MON_HEAP_EXHAUSTED);
        FATAL("Esp_mem::espbot_zalloc heap exhausted");
    }
    espmem.heap_mon();
    return addr;
}

void Esp_mem::espbot_free(void *addr)
{
    espmem._heap_objs--;
    os_free(addr);
    // DEBUG
    // int idx;
    // for (idx = 0; idx < HEAP_ARRAY_SIZE; idx++)
    // {
    //     if (espmem._heap_array[idx].addr == addr)
    //     {
    //         // eliminate current item from heap allocated items list
    //         espmem._heap_array[idx].size = -1;
    //         break;
    //     }
    // }
    // espmem.stack_mon();
}

uint32 Esp_mem::get_min_stack_addr(void)
{
    return _stack_min_addr;
}

uint32 Esp_mem::get_max_stack_addr(void)
{
    return _stack_max_addr;
}

uint32 Esp_mem::get_start_heap_addr(void)
{
    return _heap_start_addr;
}

uint32 Esp_mem::get_max_heap_size(void)
{
    return _max_heap_size;
}

uint32 Esp_mem::get_mim_heap_size(void)
{
    return _min_heap_size;
}

// uint32 Esp_mem::get_used_heap_size(void)
// {
//     // int idx;
//     // int used_heap = 0;
//     // stack_mon();
//     // for (idx = 0; idx < HEAP_ARRAY_SIZE; idx++)
//     // {
//     //     if (_heap_array[idx].size > 0)
//     //         used_heap += _heap_array[idx].size;
//     // }
//     // return used_heap;
//     return system_get_free_heap_size();
// }

uint32 Esp_mem::get_heap_objs(void)
{
    return _heap_objs;
}

uint32 Esp_mem::get_max_heap_objs(void)
{
    return _max_heap_objs;
}

// DEBUG
// void Esp_mem::print_heap_objects(void)
// {
//     int idx;
//     int counter = 1;
//     os_printf("heap objects start\n");
//     for (idx = 0; idx < HEAP_ARRAY_SIZE; idx++)
//     {
//         if (_heap_array[idx].size >= 0)
//         {
//             os_printf("#%d -> %X\n", counter, _heap_array[idx].addr);
//             counter++;
//         }
//     }
//     os_printf("heap objects end\n");
// }

// struct heap_item *Esp_mem::get_heap_item(List_item item)
// {
//     static int idx = 0;
//     stack_mon();
//     if (item == first)
//     {
//         for (idx = 0; idx < HEAP_ARRAY_SIZE; idx++)
//             if (_heap_array[idx].size != -1)
//                 break;
//     }
//     else
//     {
//         for (idx = (idx + 1); idx < HEAP_ARRAY_SIZE; idx++)
//             if (_heap_array[idx].size != -1)
//                 break;
//     }
//     if (idx == HEAP_ARRAY_SIZE)
//         return NULL;
//     else
//         return &(_heap_array[idx]);
// }

#ifdef ESPBOT
// C++ wrapper

extern "C" void *call_espbot_zalloc(size_t size) // wrapper function
{
    return espmem.espbot_zalloc(size);
}

extern "C" void call_espbot_free(void *addr) // wrapper function
{
    espmem.espbot_free(addr);
}

#endif