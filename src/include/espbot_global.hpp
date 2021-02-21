/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */
#ifndef __ESPBOT_GLOBAL_HPP__
#define __ESPBOT_GLOBAL_HPP__

extern "C"
{
#include "ip_addr.h"
}

#include "espbot.hpp"
#include "espbot_mem_mon.hpp"
#include "espbot_ota.hpp"
#include "espbot_webserver.hpp"

extern char *espbot_release;
extern Esp_mem espmem;
extern Espbot espbot;
extern Websvr espwebsvr;
extern Ota_upgrade esp_ota;

#endif
