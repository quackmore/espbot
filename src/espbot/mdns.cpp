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
#include "c_types.h"
#include "ip_addr.h"
#include "espconn.h"
}

#include "mdns.hpp"
#include "espbot.hpp"
#include "espbot_global.hpp"
#include "logger.hpp"

void ICACHE_FLASH_ATTR Mdns::start(char *app_alias)
{
    esplog.all("Mdns::start\n");
    struct ip_info ipconfig;
    wifi_get_ip_info(STATION_IF, &ipconfig);
    m_info.host_name = espbot.get_name();
    m_info.ipAddr = ipconfig.ip.addr;
    m_info.server_name = espbot.get_name();
    m_info.server_port = SERVER_PORT;
    m_info.txt_data[0] = app_alias;
    espconn_mdns_init(&m_info);
    esplog.debug("mDns started\n");
}

void ICACHE_FLASH_ATTR Mdns::stop(void)
{
    esplog.all("Mdns::stop\n");
    espconn_mdns_close();
    esplog.debug("mDns ended\n");
}