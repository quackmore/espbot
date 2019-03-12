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


#include "debug.hpp"
#include "espbot.hpp"
#include "esp8266_spiffs.hpp"
#include "logger.hpp"
#include "wifi.hpp"
#include "mdns.hpp"
#include "sntp.hpp"
#include "webserver.hpp"
#include "webclient.hpp"
#include "ota_upgrade.hpp"
#include "gpio.hpp"
#include "dht.hpp"

extern char *espbot_release;
extern Str_list esp_event_log;
extern Flashfs espfs;
extern Esp_mem espmem;
extern Logger esplog;
extern Espbot espbot;
extern Wifi espwifi;
extern Mdns esp_mDns;
extern Sntp esp_sntp;
extern Websvr espwebsvr;
extern Webclnt espwebclnt;
extern Ota_upgrade esp_ota;
extern Gpio esp_gpio;

#endif
