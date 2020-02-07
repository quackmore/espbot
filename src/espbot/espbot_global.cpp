/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */

#include "espbot_global.hpp"

/*
 *  APP_RELEASE is coming from git
 *  'git --no-pager describe --tags --always --dirty'
 *  and is generated by the Makefile
 */

#ifndef APP_RELEASE
#define APP_RELEASE "Unavailable"
#endif

char *espbot_release = APP_RELEASE;

Flashfs espfs;
Espbot_diag esp_diag;
Esp_mem espmem;
#ifdef DEBUG_TRACE
Logger esplog;
#endif
Espbot espbot;
Mdns esp_mDns;
Sntp esp_sntp;
Websvr espwebsvr;
Ota_upgrade esp_ota;
Gpio esp_gpio;