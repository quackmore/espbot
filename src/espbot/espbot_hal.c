/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */

//
// most of the following code comes from Espressif esp-gdbstub
// https://github.com/espressif/esp-gdbstub
// with great contributions from Cesanta's guys
// (https://backend.cesanta.com/blog/esp8266-gdb.shtml is totally worth a reading)
//
// the gdb stub has been stripped from everything except the exception handling
// the only purpose here is to catch the exception
// and save a stack dump to persistent memory
// to make it available later for debugging purpose
//

#include "c_types.h"
#include "osapi.h"
#include "user_interface.h"
#include "espbot_mem_macros.h"
#include "espbot_rtc_mem_map.h"

// From xtensa/corebits.h
#define EXCCAUSE_ILLEGAL 0                /* Illegal Instruction */
#define EXCCAUSE_SYSCALL 1                /* System Call (SYSCALL instruction) */
#define EXCCAUSE_INSTR_ERROR 2            /* Instruction Fetch Error */
#define EXCCAUSE_LOAD_STORE_ERROR 3       /* Load Store Error */
#define EXCCAUSE_DIVIDE_BY_ZERO 6         /* Integer Divide by Zero */
#define EXCCAUSE_UNALIGNED 9              /* Unaligned Load or Store */
#define EXCCAUSE_INSTR_DATA_ERROR 12      /* PIF Data Error on Instruction Fetch (RB-200x and later) */
#define EXCCAUSE_LOAD_STORE_DATA_ERROR 13 /* PIF Data Error on Load or Store (RB-200x and later) */
#define EXCCAUSE_INSTR_ADDR_ERROR 14      /* PIF Address Error on Instruction Fetch (RB-200x and later) */
#define EXCCAUSE_LOAD_STORE_ADDR_ERROR 15 /* PIF Address Error on Load or Store (RB-200x and later) */
#define EXCCAUSE_INSTR_PROHIBITED 20      /* Cache Attribute does not allow Instruction Fetch */
#define EXCCAUSE_LOAD_PROHIBITED 28       /* Cache Attribute does not allow Load */
#define EXCCAUSE_STORE_PROHIBITED 29      /* Cache Attribute does not allow Store */

//
// EXCCAUSE_LOAD_STORE_ERROR was firing during normal execution...
// so kept it out...
//

static int exno[] = {EXCCAUSE_ILLEGAL,
                     EXCCAUSE_SYSCALL,
                     EXCCAUSE_INSTR_ERROR,
                     EXCCAUSE_DIVIDE_BY_ZERO,
                     EXCCAUSE_UNALIGNED,
                     EXCCAUSE_INSTR_DATA_ERROR,
                     EXCCAUSE_LOAD_STORE_DATA_ERROR,
                     EXCCAUSE_INSTR_ADDR_ERROR,
                     EXCCAUSE_LOAD_STORE_ADDR_ERROR,
                     EXCCAUSE_INSTR_PROHIBITED,
                     EXCCAUSE_LOAD_PROHIBITED,
                     EXCCAUSE_STORE_PROHIBITED};

// From xtruntime-frames.h
struct XTensa_exception_frame_s
{
    uint32_t pc;    //
    uint32_t ps;    //
    uint32_t sar;   //
    uint32_t vpri;  //
    uint32_t a0;    // return address
    uint32_t a[14]; // a2..a15
};

void _xtos_set_exception_handler(int cause, void(exhandler)(struct XTensa_exception_frame_s *frame));

//Non-OS exception handler. Gets called by the Xtensa HAL.
static void custom_exception_handler(struct XTensa_exception_frame_s *frame)
{
    //
    // code for managing the exception
    //

    // returning this function would trigger the exception again
    // and that's not what I want,
    // so let's have some rest and let the watchdog do its job...
    while (1)
        ;
}

// The OS-less SDK uses the Xtensa HAL to handle exceptions.
// The following will replace the default exception handler
// with the custom one
void install_custom_exceptions()
{
    int i;
    for (i = 0; i < (sizeof(exno) / sizeof(exno[0])); i++)
    {
        _xtos_set_exception_handler(exno[i], custom_exception_handler);
    }
}

// there is a weak symbol for system_restart_hook
// into libmain.a
// 00000338 W system_restart_hook
//
// that in the assemly file shows up like this:
//
// 4022ea90 <system_restart_hook>:
// 4022ea90:       f00d            ret.n
// 4022ea92:       ff0000          excw
// 4022ea95:       ffff00          excw
// 4022ea98:       07a120          excw
//
// the following will override the weak symbol to dump relevant stack content
// in case of an exception or software watchdog trigger
// following informations will e saved to RTC memory (up to RTC_STACKDUMP_LEN uint32_t)
// - stack pointer
// - stack content that likely refer to code address (PC return address)
//

void system_restart_hook()
{
    struct rst_info reset_info;
    // SDK APIs for obtaining rst_info does not work...
    // so copying from RTC memory (credits to Sming's guys)
    system_rtc_mem_read(0, &reset_info, sizeof(reset_info));

    uint32_t sp_offset = 0;

    switch (reset_info.reason)
    {
    case REASON_SOFT_WDT_RST:
        sp_offset = 0xF4;
        break;
    case REASON_EXCEPTION_RST:
        sp_offset = 0xC0;
        break;
    case REASON_WDT_RST: // would have liked this one too, but it's not working
    case REASON_DEFAULT_RST:
    case REASON_SOFT_RESTART:
    case REASON_DEEP_SLEEP_AWAKE:
    case REASON_EXT_SYS_RST:
    default:
        return;
    }

    uint32_t idx;
    uint32_t stack_pointer;
    uint32_t rtc_ptr;
    uint32_t rtc_ptr_blank = 0;

    // use without offset to find the sp_offset ...
    // stack_pointer = (uint32_t)&reset_info;
    stack_pointer = (uint32_t)&reset_info + sp_offset;
    rtc_ptr = RTC_STACKDUMP;
    // save to RTC memory
    // start saving the stack pointer
    system_rtc_mem_write(rtc_ptr, (void *)&stack_pointer, 4);
    rtc_ptr++;

    fs_printf("exception epc->0x%X\n", reset_info.epc1);
    fs_printf("stack dump SP->0x%X\n", stack_pointer);
    fs_printf("address  content\n");

    for (idx = 0; idx < STACK_TRACE_LEN; idx++)
    {
        // using 0x3FFFFFF0 as the highest stack pointer address
        if (stack_pointer > 0x3FFFFFF0)
            break;
        // serial log
        fs_printf("%X %X", stack_pointer, *((uint32_t *)stack_pointer));
        if (*((uint32_t *)stack_pointer) == reset_info.epc1)
            // this is for finding the sp_offset
            fs_printf(" <------ %X\n", (stack_pointer - (uint32_t)&reset_info));
        else
            fs_printf("\n");

        // save to RTC memory
        // save only stack content that possibly refers to code address
        // (the 'return address')
        // save up to RTC_STACKDUMP_LEN
        if ((*((uint32_t *)stack_pointer) >= 0x40200000) &&
            ((rtc_ptr - RTC_STACKDUMP) < RTC_STACKDUMP_LEN))
        {
            system_rtc_mem_write(rtc_ptr, (void *)stack_pointer, 4);
            rtc_ptr++;
        }

        stack_pointer += 4;
    }

    // save to RTC memory
    // fill up any remaining stack trace with 0
    while ((rtc_ptr - RTC_STACKDUMP) < RTC_STACKDUMP_LEN)
    {
        system_rtc_mem_write(rtc_ptr, (void *)&rtc_ptr_blank, 4);
        rtc_ptr++;
    }
}

/**
  * get the stack pointer address recorded on last crash 
  * @return uint32_t - stack pointer address
  */
uint32_t get_last_crash_SP()
{
    uint32_t sp;
    system_rtc_mem_read(RTC_STACKDUMP, &sp, 4);
    return sp;
}

/**
  * get the stack dump content one value at a time
  * 
  * @idx - 0 -> the first stored value
  *      - 1 -> the next value (from last call)
  * @dest - the destination where to copy the value 
  * @return int - 0 - a valid value has been copied
  *             - 1 - no valid value has been copied
  *                   (went beyond the last value)
  */
int get_last_crash_stack_dump(int idx, uint32_t *dest)
{
    static int rtc_ptr = RTC_STACKDUMP;
    uint32_t val = 0;
    if (idx == 0)
    {
        // first stack pointer value stored (excluding the stack pointer address)
        rtc_ptr = RTC_STACKDUMP + 1;
        system_rtc_mem_read(rtc_ptr, dest, 4);
        return 0;
    }
    rtc_ptr++;
    if ((rtc_ptr - RTC_STACKDUMP) >= RTC_STACKDUMP_LEN)
        // rtc_ptr is beyond the max stored value
        return 1;
    system_rtc_mem_read(rtc_ptr, dest, 4);
    return 0;
}

/* 
    may be useful one day...
//Read a byte from the ESP8266 memory.
static unsigned char readbyte(unsigned int p)
{
    int *i = (int *)(p & (~3));
    if (p < 0x20000000 || p >= 0x60000000)
        return -1;
    return *i >> ((p & 3) * 8);
}

//Write a byte to the ESP8266 memory.
static void writeByte(unsigned int p, unsigned char d)
{
    int *i = (int *)(p & (~3));
    if (p < 0x20000000 || p >= 0x60000000)
        return;
    if ((p & 3) == 0)
        *i = (*i & 0xffffff00) | (d << 0);
    if ((p & 3) == 1)
        *i = (*i & 0xffff00ff) | (d << 8);
    if ((p & 3) == 2)
        *i = (*i & 0xff00ffff) | (d << 16);
    if ((p & 3) == 3)
        *i = (*i & 0x00ffffff) | (d << 24);
}

//Returns 1 if it makes sense to write to addr p
static int validWrAddr(int p)
{
    if (p >= 0x3ff00000 && p < 0x40000000)
        return 1;
    if (p >= 0x40100000 && p < 0x40140000)
        return 1;
    if (p >= 0x60000000 && p < 0x60002000)
        return 1;
    return 0;
}
*/