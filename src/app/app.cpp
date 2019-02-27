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
#include "mem.h"
#include "dio_task.h"
#include "esp8266_io.h"
}

#include "app.hpp"
#include "dht.hpp"

Dht dht22;

void ICACHE_FLASH_ATTR app_init_before_wifi(void)
{
    init_dio_task();
    dht22.init(ESPBOT_D2, DHT22, 5, 30);
}

void ICACHE_FLASH_ATTR app_init_after_wifi(void)
{
}

void ICACHE_FLASH_ATTR app_deinit_on_wifi_disconnect()
{
}