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

#include "espbot.hpp"
#include "espbot_diagnostic.hpp"
#include "espbot_event_codes.h"
#include "espbot_global.hpp"
#include "espbot_logger.hpp"
#include "espbot_mdns.hpp"

void Mdns::start(char *app_alias)
{
    if (espbot.mdns_enabled())
    {
        struct ip_info ipconfig;
        wifi_get_ip_info(STATION_IF, &ipconfig);
        m_info.host_name = espbot.get_name();
        m_info.ipAddr = ipconfig.ip.addr;
        m_info.server_name = espbot.get_name();
        m_info.server_port = SERVER_PORT;
        m_info.txt_data[0] = app_alias;
        espconn_mdns_init(&m_info);
        esp_diag.info(MDNS_START);
        INFO("mDns started");
    }
}

void Mdns::stop(void)
{
    espconn_mdns_close();
    esp_diag.info(MDNS_STOP);
    INFO("mDns ended");
}