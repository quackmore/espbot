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
#include "osapi.h"
#include "user_interface.h"
#include "mem.h"
}

#include "wifi.hpp"
#include "espbot.hpp"
#include "espbot_global.hpp"
#include "debug.hpp"
#include "json.hpp"
#include "espbot_utils.hpp"

bool ICACHE_FLASH_ATTR Wifi::is_timeout_timer_active(void)
{
    return m_timeout_timer_active;
}

void ICACHE_FLASH_ATTR Wifi::start_connect_timeout_timer(void)
{
    os_timer_arm(&m_station_connect_timeout, WIFI_CONNECT_TIMEOUT, 0);
    m_timeout_timer_active = true;
}

void ICACHE_FLASH_ATTR Wifi::stop_connect_timeout_timer(void)
{
    os_timer_disarm(&m_station_connect_timeout);
}

void ICACHE_FLASH_ATTR Wifi::start_wait_before_reconnect_timer(void)
{
    os_timer_arm(&m_wait_before_reconnect, WIFI_WAIT_BEFORE_RECONNECT, 0);
}

void ICACHE_FLASH_ATTR wifi_event_handler(System_Event_t *evt)
{
    uint32 dummy;

    switch (evt->event)
    {
    case EVENT_STAMODE_CONNECTED:
        esplog.info("connected to ssid %s, channel %d\n",
                    evt->event_info.connected.ssid,
                    evt->event_info.connected.channel);
        break;
    case EVENT_STAMODE_DISCONNECTED:
        esplog.info("disconnect from ssid %s, reason %d\n",
                    evt->event_info.disconnected.ssid,
                    evt->event_info.disconnected.reason);
        system_os_post(USER_TASK_PRIO_0, SIG_STAMODE_DISCONNECTED, '0'); // informing everybody of
                                                                         // disconnection from AP
        if (!espwifi.is_timeout_timer_active())
        {
            espwifi.start_connect_timeout_timer();
            esplog.info("will switch to SOFTAP in 10 seconds but keep trying to reconnect ...\n");
        }
        espwifi.start_wait_before_reconnect_timer();
        break;
    case EVENT_STAMODE_AUTHMODE_CHANGE:
        esplog.info("authmode change: %d -> %d\n",
                    evt->event_info.auth_change.old_mode,
                    evt->event_info.auth_change.new_mode);
        break;
    case EVENT_STAMODE_DHCP_TIMEOUT:
        esplog.info("ESPBOT WIFI [STATION]: dhcp timeout, ip:" IPSTR ",mask:" IPSTR ",gw:" IPSTR,
                    IP2STR(&evt->event_info.got_ip.ip),
                    IP2STR(&evt->event_info.got_ip.mask),
                    IP2STR(&evt->event_info.got_ip.gw));
        os_printf("\n");
        break;
    case EVENT_STAMODE_GOT_IP:
        esplog.info("got IP:" IPSTR ",mask:" IPSTR ",gw:" IPSTR,
                    IP2STR(&evt->event_info.got_ip.ip),
                    IP2STR(&evt->event_info.got_ip.mask),
                    IP2STR(&evt->event_info.got_ip.gw));
        os_printf("\n");
        // station connected to AP and got an IP address
        // whichever was wifi mode now AP mode is no longer required
        esplog.info("ESP8266 connected as station to %s\n", espwifi.station_get_ssid());
        espwifi.stop_connect_timeout_timer();
        wifi_set_opmode_current(STATION_MODE);
        system_os_post(USER_TASK_PRIO_0, SIG_STAMODE_GOT_IP, '0'); // informing everybody of
                                                                   // successfully connection to AP
        // time to update flash configuration for (eventually) saving ssid and password
        espwifi.save_station_cfg();
        break;
    case EVENT_SOFTAPMODE_STACONNECTED:
        esplog.info("station: " MACSTR " join, AID = %d\n",
                    MAC2STR(evt->event_info.sta_connected.mac),
                    evt->event_info.sta_connected.aid);
        system_os_post(USER_TASK_PRIO_0, SIG_SOFTAPMODE_STACONNECTED, '0'); // informing everybody that
                                                                            // a station connected to ESP8266
        break;
    case EVENT_SOFTAPMODE_STADISCONNECTED:
        esplog.info("station: " MACSTR " leave, AID = %d\n",
                    MAC2STR(evt->event_info.sta_disconnected.mac),
                    evt->event_info.sta_disconnected.aid);
        system_os_post(USER_TASK_PRIO_0, SIG_SOFTAPMODE_STADISCONNECTED, '0'); // informing everybody of
                                                                               // a station disconnected from ESP8266
        break;
    case EVENT_SOFTAPMODE_PROBEREQRECVED:
        esplog.info("softAP received a probe request\n");
        break;
    case EVENT_OPMODE_CHANGED:
        switch (wifi_get_opmode())
        {
        case STATION_MODE:
            esplog.info("wifi mode changed to STATION_MODE\n");
            break;
        case SOFTAP_MODE:
            esplog.info("wifi mode changed to SOFTAP_MODE\n");
            break;
        case STATIONAP_MODE:
            esplog.info("wifi mode changed to STATIONAP_MODE\n");
            break;
        default:
            break;
        }
        break;
    case EVENT_SOFTAPMODE_DISTRIBUTE_STA_IP:
        esplog.info("aid %d =>" MACSTR " => " IPSTR "\r\n",
                    evt->event_info.distribute_sta_ip.aid,
                    MAC2STR(evt->event_info.distribute_sta_ip.mac),
                    IP2STR(&evt->event_info.distribute_sta_ip.ip));
        break;
    default:
        esplog.info("unknown event %x\n", evt->event);
        break;
    }
}

void ICACHE_FLASH_ATTR Wifi::switch_to_stationap(void)
{
    struct ip_info ap_ip;
    struct dhcps_lease dhcp_lease;

    espwifi.m_timeout_timer_active = false;

    wifi_set_opmode_current(STATIONAP_MODE);
    wifi_softap_set_config(&espwifi.m_ap_config);

    wifi_softap_dhcps_stop();
    IP4_ADDR(&ap_ip.ip, 192, 168, 10, 1);
    IP4_ADDR(&ap_ip.gw, 192, 168, 10, 1);
    IP4_ADDR(&ap_ip.netmask, 255, 255, 255, 0);
    wifi_set_ip_info(SOFTAP_IF, &ap_ip);
    IP4_ADDR(&dhcp_lease.start_ip, 192, 168, 10, 100);
    IP4_ADDR(&dhcp_lease.end_ip, 192, 168, 10, 103);
    wifi_softap_set_dhcps_lease(&dhcp_lease);
    wifi_softap_dhcps_start();

    esplog.info("Wi-Fi working as AP\n");
    esplog.info("AP config: SSID:        %s\n", espwifi.m_ap_config.ssid);
    esplog.info("AP config: Password:    %s\n", espwifi.m_ap_config.password);
    esplog.info("AP config: channel:     %d\n", espwifi.m_ap_config.channel);
    switch (espwifi.m_ap_config.authmode)
    {
    case AUTH_OPEN:
        esplog.info("AP config: Security:    Disabled\n");
        break;
    case AUTH_WEP:
        esplog.info("AP config: Security:    WEP\n");
        break;
    case AUTH_WPA_PSK:
        esplog.info("AP config: Security:    WPA_PSK\n");
        break;
    case AUTH_WPA2_PSK:
        esplog.info("AP config: Security:    WPA2_PSK\n");
        break;
    case AUTH_WPA_WPA2_PSK:
        esplog.info("AP config: Security:    WPA_WPA2_PSK\n");
        break;
    default:
        esplog.info("AP config: Security:    Unknown\n");
        break;
    }
}

void ICACHE_FLASH_ATTR Wifi::connect(void)
{
    struct station_config stationConf;

    if (os_strlen(espwifi.m_station_ssid) == 0 || os_strlen(espwifi.m_station_pwd) == 0)
    {
        esplog.error("Wifi::connect: no ssid or password available\n");
        return;
    }
    os_memset(&stationConf, 0, sizeof(stationConf));
    os_memcpy(stationConf.ssid, espwifi.m_station_ssid, 32);
    os_memcpy(stationConf.password, espwifi.m_station_pwd, 64);
    stationConf.bssid_set = 0;
    wifi_station_set_config_current(&stationConf);
    wifi_station_set_hostname(espbot.get_name());
    if (wifi_station_get_auto_connect() != 0)
    {
        wifi_station_set_reconnect_policy(0);
        wifi_station_set_auto_connect(0);
    }
    wifi_station_connect();
}

void ICACHE_FLASH_ATTR Wifi::init()
{
    os_strncpy((char *)m_ap_config.ssid, espbot.get_name(), 32); // uint8 ssid[32];
    os_strcpy((char *)m_ap_config.password, "espbot123456");     // uint8 password[64];
    m_ap_config.ssid_len = 0;                                    // uint8 ssid_len;
    m_ap_config.channel = 1;                                     // uint8 channel;
    m_ap_config.authmode = AUTH_WPA2_PSK;                        // uint8 authmode;
    m_ap_config.ssid_hidden = 0;                                 // uint8 ssid_hidden;
    m_ap_config.max_connection = 4;                              // uint8 max_connection;
    m_ap_config.beacon_interval = 100;                           // uint16 beacon_interval;

    if (get_station_saved_cfg()) // something went wrong while loading flash config
    {
        esplog.info("Wifi::init setting null station config\n");
        os_memset(m_station_ssid, 0, 32);
        os_memset(m_station_pwd, 0, 32);
    }

    m_timeout_timer_active = false;

    os_timer_disarm(&m_station_connect_timeout);
    os_timer_setfn(&m_station_connect_timeout, (os_timer_func_t *)&Wifi::switch_to_stationap, NULL);
    os_timer_disarm(&m_wait_before_reconnect);
    os_timer_setfn(&m_wait_before_reconnect, (os_timer_func_t *)&Wifi::connect, NULL);

    wifi_set_phy_mode(PHY_MODE_11N);
    wifi_set_event_handler_cb((wifi_event_handler_cb_t)wifi_event_handler);

    // start as SOFTAP and switch to STATION if a valid station configuration is found in flash
    // this will ensure that softap and station configurations are set by espbot
    // otherwise default configurations by NON OS SDK are used
    Wifi::switch_to_stationap();
    Wifi::connect();
}

char ICACHE_FLASH_ATTR *Wifi::station_get_ssid(void)
{
    return m_station_ssid;
}

char ICACHE_FLASH_ATTR *Wifi::station_get_password(void)
{
    return m_station_pwd;
}

int ICACHE_FLASH_ATTR Wifi::get_station_saved_cfg(void)
{
    if (espfs.is_available())
    {
        if (!Ffile::exists("wifi.cfg"))
        {
            esplog.info("Wifi::get_station_saved_cfg - no cfg file found\n");
            return 1;
        }
        Ffile cfgfile(&espfs, "wifi.cfg");
        if (cfgfile.is_available())
        {
            os_printf("Available heap: %d\n", system_get_free_heap_size());
            char *buffer = (char *)os_zalloc(200);
            if (buffer)
            {
                int buf_len = cfgfile.n_read(buffer, 200);
                Json_str cfg_str(buffer, os_strlen(buffer));
                if (cfg_str.syntax_check() == JSON_SINTAX_OK)
                {
                    int cfg_param_checked = 0;
                    while (cfg_str.find_next_pair() == JSON_NEW_PAIR_FOUND)
                    {
                        if (os_strncmp(cfg_str.get_cur_pair_string(), "station_ssid", cfg_str.get_cur_pair_string_len()) == 0)
                        {
                            if (cfg_str.get_cur_pair_value_type() == JSON_STRING)
                            {
                                station_set_ssid(cfg_str.get_cur_pair_value(), cfg_str.get_cur_pair_value_len());
                                cfg_param_checked++;
                            }
                        }
                        if (os_strncmp(cfg_str.get_cur_pair_string(), "station_pwd", cfg_str.get_cur_pair_string_len()) == 0)
                        {
                            if (cfg_str.get_cur_pair_value_type() == JSON_STRING)
                            {
                                station_set_pwd(cfg_str.get_cur_pair_value(), cfg_str.get_cur_pair_value_len());
                                cfg_param_checked++;
                            }
                        }
                    }
                    if (cfg_param_checked != 2) // found the wrong number of parameters
                    {
                        esplog.error("Wifi::get_station_saved_cfg - available configuration is incomplete\n");
                        return 1;
                    }
                }
                else
                {
                    esplog.error("Wifi::get_station_saved_cfg - cannot parse json string\n");
                    return 1;
                }
            }
            else
            {
                esplog.error("Wifi::get_station_saved_cfg - not enough heap memory available\n");
                return 1;
            }
        }
        else
        {
            esplog.error("Wifi::get_station_saved_cfg - cannot open wifi.cfg\n");
            return 1;
        }
    }
    else
    {
        esplog.error("Wifi::get_station_saved_cfg - file system is not available\n");
        return 1;
    }
    return 0;
}

int ICACHE_FLASH_ATTR Wifi::save_station_cfg(void)
{
    if (espfs.is_available())
    {
        Ffile cfgfile(&espfs, "wifi.cfg");
        if (cfgfile.is_available())
        {
            cfgfile.clear();
            char *buffer = (char *)os_zalloc(200);
            if (buffer)
            {
                os_sprintf(buffer, "{\"station_ssid\": %s,\"station_pwd\": %s}", espwifi.m_station_ssid, espwifi.m_station_pwd);
                cfgfile.n_append(buffer, os_strlen(buffer));
            }
            else
            {
                esplog.error("Wifi::save_cfg - not enough heap memory available\n");
                return 1;
            }
            os_free(buffer);
        }
        else
        {
            esplog.error("Wifi::save_cfg - cannot open wifi.cfg\n");
            return 1;
        }
    }
    else
    {
        esplog.error("Wifi::save_cfg - file system not available\n");
        return 1;
    }
    return 0;
}

void ICACHE_FLASH_ATTR Wifi::station_set_ssid(char *t_str, int t_len)
{
    os_memset(m_station_ssid, 0, 32);
    if (t_len > 31)
    {
        esplog.warn("Wifi::station_set_ssid: truncating ssid to 31 characters\n");
        os_strncpy(m_station_ssid, t_str, 31);
    }
    else
    {
        os_strncpy(m_station_ssid, t_str, t_len);
    }
}

void ICACHE_FLASH_ATTR Wifi::station_set_pwd(char *t_str, int t_len)
{
    os_memset(m_station_pwd, 0, 64);
    if (t_len > 63)
    {
        esplog.warn("Wifi::station_set_pwd: truncating pwd to 63 characters\n");
        os_strncpy(m_station_pwd, t_str, 63);
    }
    else
    {
        os_strncpy(m_station_pwd, t_str, t_len);
    }
}
