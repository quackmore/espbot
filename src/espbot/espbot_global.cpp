/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */

#include "espbot_global.hpp"

// global variables for esp8266 
Str_list esp_last_errors(20); // actually this won't work
                              // really set in espbot.cpp espbot_init
Flashfs espfs;
Dbggr espdebug;
Logger esplog;
Espbot espbot;
Wifi espwifi;
Websvr espwebsvr;

