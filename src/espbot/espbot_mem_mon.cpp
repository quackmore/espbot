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
#include "espbot_hal.h"
}

#include "espbot.hpp"
#include "espbot_diagnostic.hpp"
#include "espbot_event_codes.h"
#include "espbot_mem_mon.hpp"
#include "espbot_timedate.hpp"
#include "espbot_utils.hpp"

static uint32 stack_min_addr;
static uint32 stack_max_addr;
static uint32 heap_start_addr;
static uint32 max_heap_size;
static uint32 min_heap_size;
static uint32 heap_objs;
static uint32 max_heap_objs;

void mem_mon_init(void)
{
    uint32 first_stack_var;
    void *heap_var = os_zalloc(1);

    // stack vars
    stack_min_addr = (uint32)&first_stack_var;
    stack_max_addr = (uint32)&first_stack_var;

    // init heap infos and data structures
    heap_start_addr = (uint32)heap_var;
    if (heap_var)
        os_free(heap_var);
    max_heap_size = system_get_free_heap_size();
    min_heap_size = max_heap_size;
    heap_objs = 0;
    max_heap_objs = 0;

    mem_mon_stack();
}

void mem_mon_heap(void)
{
    uint32 currentHeap = system_get_free_heap_size();
    if (min_heap_size > currentHeap)
        min_heap_size = currentHeap;
}

void mem_mon_stack(void)
{
    uint32 stack_var_addr = (uint32)&stack_var_addr;
    if (stack_var_addr > stack_max_addr)
        stack_max_addr = stack_var_addr;
    if (stack_var_addr < stack_min_addr)
        stack_min_addr = stack_var_addr;
}

void *espbot_zalloc(size_t size)
{
    void *addr = os_zalloc(size);
    if (addr)
    {
        heap_objs++;
        if (heap_objs > max_heap_objs)
            max_heap_objs = heap_objs;
    }
    else
    {
        dia_fatal_evnt(MEM_MON_HEAP_EXHAUSTED);
        FATAL("espbot_zalloc heap exhausted");
    }
    uint32 currentHeap = system_get_free_heap_size();
    if (min_heap_size > currentHeap)
        min_heap_size = currentHeap;
    return addr;
}

void espbot_free(void *addr)
{
    heap_objs--;
    os_free(addr);
}

char *mem_mon_json_stringify(char *dest, int len)
{
    // {"stack_max_addr":"3FFFFE00","stack_min_addr":"3FFFE660","heap_start_addr":"3FFF17A8","heap_free_size":41368,"heap_max_size":43096,"heap_min_size":39488,"heap_objs":99999,"heap_max_objs":99999}

    int msg_len = 193 + 1;
    char *msg;
    if (dest == NULL)
    {
        msg = new char[msg_len];
        if (msg == NULL)
        {
            dia_error_evnt(MEM_MON_STRINGIFY_HEAP_EXHAUSTED, msg_len);
            ERROR("mem_mon_json_stringify heap exhausted [%d]", msg_len);
            return NULL;
        }
    }
    else
    {
        msg = dest;
        if (len < msg_len)
        {
            *msg = 0;
            return msg;
        }
    }
    fs_sprintf(msg,
               "{\"stack_max_addr\":\"%X\",\"stack_min_addr\":\"%X\",",
               stack_max_addr,
               stack_min_addr);
    fs_sprintf((msg + os_strlen(msg)),
               "\"heap_start_addr\":\"%X\",\"heap_free_size\":%d,",
               heap_start_addr,
               system_get_free_heap_size());
    fs_sprintf((msg + os_strlen(msg)),
               "\"heap_max_size\":%d,\"heap_min_size\":%d,",
               max_heap_size,
               min_heap_size);
    fs_sprintf((msg + os_strlen(msg)),
               "\"heap_objs\":%d,\"heap_max_objs\":%d}",
               heap_objs,
               max_heap_objs);
    mem_mon_stack();
    return msg;
}

char *mem_dump_json_stringify(char *address_str, int dump_len, char *dest, int len)
{
    // {"address":"3FFE8950","length":00000,"content":""}
    int msg_len = 50 + dump_len + 1;
    char *msg;
    if (dest == NULL)
    {
        msg = new char[msg_len];
        if (msg == NULL)
        {
            dia_error_evnt(MEM_DUMP_STRINGIFY_HEAP_EXHAUSTED, msg_len);
            ERROR("mem_dump_json_stringify heap exhausted [%d]", msg_len);
            return NULL;
        }
    }
    else
    {
        msg = dest;
        if (len < msg_len)
        {
            *msg = 0;
            return msg;
        }
    }
    char *address = (char *)atoh(address_str);
    fs_sprintf(msg,
               "{\"address\":\"%X\",\"length\":%d,\"content\":\"",
               address,
               dump_len);
    int cnt;
    for (cnt = 0; cnt < dump_len; cnt++)
        os_sprintf(msg + os_strlen(msg), "%c", *(address + cnt));
    fs_sprintf(msg + os_strlen(msg), "\"}");
    mem_mon_stack();
    return msg;
}

char *mem_dump_hex_json_stringify(char *address_str, int dump_len, char *dest, int len)
{
    // {"address":"3FFE8950","length":00000,"content":""}
    // {"address":"3FFE8950","length":00000,"content":" 33 2E 30 2E 34 28 39 35 33 32 63 65 62 29 0 0 70 76 50 6F 72 74 4D 61"}

    int msg_len = 50 + (dump_len * 3) + 1;
    char *msg;
    if (dest == NULL)
    {
        msg = new char[msg_len];
        if (msg == NULL)
        {
            dia_error_evnt(MEM_DUMP_HEX_STRINGIFY_HEAP_EXHAUSTED, msg_len);
            ERROR("mem_dump_json_stringify heap exhausted [%d]", msg_len);
            return NULL;
        }
    }
    else
    {
        msg = dest;
        if (len < msg_len)
        {
            *msg = 0;
            return msg;
        }
    }
    char *address = (char *)atoh(address_str);
    fs_sprintf(msg,
               "{\"address\":\"%X\",\"length\":%d,\"content\":\"",
               address,
               dump_len);
    int cnt;
    for (cnt = 0; cnt < dump_len; cnt++)
        os_sprintf(msg + os_strlen(msg), " %X", *(address + cnt));
    fs_sprintf(msg + os_strlen(msg), "\"}");
    mem_mon_stack();
    return msg;
}

char *mem_last_reset_json_stringify(char *dest, int len)
{
    // enum rst_reason
    // {
    //     REASON_DEFAULT_RST = 0,
    //     REASON_WDT_RST = 1,
    //     REASON_EXCEPTION_RST = 2,
    //     REASON_SOFT_WDT_RST = 3,
    //     REASON_SOFT_RESTART = 4,
    //     REASON_DEEP_SLEEP_AWAKE = 5,
    //     REASON_EXT_SYS_RST = 6
    // };
    // struct rst_info
    // {
    //     uint32 reason;
    //     uint32 exccause;
    //     uint32 epc1;
    //     uint32 epc2;
    //     uint32 epc3;
    //     uint32 excvaddr;
    //     uint32 depc;
    // };
    // {"date":"","reason":"","exccause":"","epc1":"","epc2":"","epc3":"","evcvaddr":"","depc":"","sp":"","spDump":[]}
    // array item "01234567",
    int msg_len = 111 + 24 + 7 * 8 + 11 * 19 + 1;
    char *msg;
    if (dest == NULL)
    {
        msg = new char[msg_len];
        if (msg == NULL)
        {
            dia_error_evnt(MEM_LAST_RESET_STRINGIFY_HEAP_EXHAUSTED, msg_len);
            ERROR("mem_last_reset_json_stringify heap exhausted [%d]", msg_len);
            return NULL;
        }
    }
    else
    {
        msg = dest;
        if (len < msg_len)
        {
            *msg = 0;
            return msg;
        }
    }
    struct rst_info *last_rst = system_get_rst_info();
    fs_sprintf(msg,
               "{\"date\":\"%s\","
               "\"reason\":\"%X\","
               "\"exccause\":\"%X\","
               "\"epc1\":\"%X\",",
               timedate_get_timestr(espbot_get_last_reboot_time()),
               last_rst->reason,
               last_rst->exccause,
               last_rst->epc1);
    fs_sprintf(msg + os_strlen(msg),
               "\"epc2\":\"%X\","
               "\"epc3\":\"%X\","
               "\"evcvaddr\":\"%X\","
               "\"depc\":\"%X\",",
               last_rst->epc2,
               last_rst->epc3,
               last_rst->excvaddr,
               last_rst->depc);
    fs_sprintf(msg + os_strlen(msg),
               "\"sp\":\"%X\","
               "\"spDump\":[",
               get_last_crash_SP());
    uint32 address;
    int res = get_last_crash_stack_dump(0, &address);
    while (res == 0)
    {
        fs_sprintf(msg + os_strlen(msg), "\"%X\",", address);
        res = get_last_crash_stack_dump(1, &address);
    }
    fs_sprintf(msg + os_strlen(msg) - 1, "]}");
    mem_mon_stack();
    return msg;
}


#ifdef ESPBOT
// C++ wrapper

extern "C" void *call_espbot_zalloc(size_t size) // wrapper function
{
    return espbot_zalloc(size);
}

extern "C" void call_espbot_free(void *addr) // wrapper function
{
    espbot_free(addr);
}

#endif