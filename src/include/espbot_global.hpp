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


#include "espbot.hpp"
#include "esp8266_spiffs.hpp"
#include "debug.hpp"
#include "wifi.hpp"
#include "webserver.hpp"

extern Str_list esp_last_errors;
extern Flashfs espfs;
extern Dbggr espdebug;
extern Logger esplog;
extern Espbot espbot;
extern Wifi espwifi;
extern Websvr espwebsvr;

#endif
