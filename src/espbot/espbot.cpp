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
#include "ets_sys.h"
#include "osapi.h"
#include "mem.h"
#include "user_interface.h"
}
// local includes
extern "C"
{
#include "uart.h"
#include "espbot_release.h"
}

#include "espbot.hpp"

static void ICACHE_FLASH_ATTR print_greetings(void)
{
    P_INFO("\n");
    P_INFO("\n");
    P_INFO("\n"); // early os_printf always fails ...
    P_INFO("Hello there! Espbot started\n");
    P_INFO("Chip ID        : %d\n", system_get_chip_id());
    P_INFO("SDK version    : %s\n", system_get_sdk_version());
    P_INFO("Boot version   : %d\n", system_get_boot_version());
    P_INFO("Espbot version : %s\n", ESPBOT_RELEASE); // git ESPBOT_RELEASE description
                                                     // generated by Makefile
    P_DEBUG("---------------------------------------------------\n");
    P_DEBUG("Memory map\n");
    system_print_meminfo();
    P_DEBUG("---------------------------------------------------\n");
}

static void ICACHE_FLASH_ATTR espbot_task(os_event_t *e)
{
    switch (e->sig)
    {
    case SIG_STAMODE_GOT_IP:
        // [wifi station] got IP
        break;
    case SIG_STAMODE_DISCONNECTED:
        // [wifi station] disconnected
        break;
    case SIG_SOFTAPMODE_STACONNECTED:
        // [wifi station+AP] station connected
        break;
    default:
        break;
    }
}

static void ICACHE_FLASH_ATTR heartbeat_cb(void)
{
    P_DEBUG("ESPBOT HEARTBEAT: ---------------------------------------------------\n");
    P_DEBUG("ESPBOT HEARTBEAT: Available heap size: %d\n", system_get_free_heap_size());
}

void ICACHE_FLASH_ATTR Espbot::init(void)
{
    uart_init(BIT_RATE_115200, BIT_RATE_115200);

    system_set_os_print(1); // enable log print

    // set default name
    os_sprintf(m_name, "ESPBOT-%d", system_get_chip_id());

    print_greetings();

    // setup the task
    m_queue = (os_event_t *)os_malloc(sizeof(os_event_t) * QUEUE_LEN);
    system_os_task(espbot_task, USER_TASK_PRIO_0, m_queue, QUEUE_LEN);

    // start an heartbeat timer
    os_timer_disarm(&m_heartbeat);
    os_timer_setfn(&m_heartbeat, (os_timer_func_t *)heartbeat_cb, NULL);
    os_timer_arm(&m_heartbeat, HEARTBEAT_PERIOD, 1);

    // now start the wifi
}

uint32 ICACHE_FLASH_ATTR Espbot::get_chip_id(void)
{
    return system_get_chip_id();
}

uint8 ICACHE_FLASH_ATTR Espbot::get_boot_version(void)
{
    return system_get_boot_version();
}

const char ICACHE_FLASH_ATTR *Espbot::get_sdk_version(void)
{
    return system_get_sdk_version();
}

char ICACHE_FLASH_ATTR *Espbot::get_version(void)
{
    return (char *)ESPBOT_RELEASE;
}

char ICACHE_FLASH_ATTR *Espbot::get_name(void)
{
    return m_name;
}

void ICACHE_FLASH_ATTR Espbot::set_name(char *t_name)
{
    os_memset(m_name, 0, 32);
    os_strncpy(m_name, t_name, 31);
}

Espbot espbot;

// dirty reference to esp8266_spiffs_test.cpp ...
void run_tests(void);

void ICACHE_FLASH_ATTR espbot_init(void)
{
    espbot.init();

    // run some spiffs test
    // start a 20 seconds timer for connecting a terminal to the serial port ...

    os_timer_t wait_20_secs;
    os_timer_disarm(&wait_20_secs);
    os_timer_setfn(&wait_20_secs, (os_timer_func_t *)run_tests, NULL);
    os_timer_arm(&wait_20_secs, 20000, 0);

}
