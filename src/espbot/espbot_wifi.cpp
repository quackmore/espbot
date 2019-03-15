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

#include "espbot_wifi.hpp"
#include "espbot.hpp"
#include "espbot_global.hpp"
#include "espbot_logger.hpp"
#include "espbot_json.hpp"
#include "espbot_utils.hpp"
#include "espbot_debug.hpp"
#include "espbot_config.hpp"

bool ICACHE_FLASH_ATTR Wifi::is_timeout_timer_active(void)
{
    esplog.all("Wifi::is_timeout_timer_active\n");
    return m_timeout_timer_active;
}

void ICACHE_FLASH_ATTR Wifi::start_connect_timeout_timer(void)
{
    esplog.all("Wifi::start_connect_timeout_timer\n");
    os_timer_arm(&m_station_connect_timeout, WIFI_CONNECT_TIMEOUT, 0);
    m_timeout_timer_active = true;
}

void ICACHE_FLASH_ATTR Wifi::stop_connect_timeout_timer(void)
{
    esplog.all("Wifi::stop_connect_timeout_timer\n");
    os_timer_disarm(&m_station_connect_timeout);
}

void ICACHE_FLASH_ATTR Wifi::start_wait_before_reconnect_timer(void)
{
    esplog.all("Wifi::start_wait_before_reconnect_timer\n");
    os_timer_arm(&m_wait_before_reconnect, WIFI_WAIT_BEFORE_RECONNECT, 0);
}

static void ICACHE_FLASH_ATTR switch_to_stationap(void)
{
    if (wifi_get_opmode() == STATIONAP_MODE)
        return;
    else
        Wifi::set_stationap();
}

void ICACHE_FLASH_ATTR wifi_event_handler(System_Event_t *evt)
{
    esplog.all("Wifi::wifi_event_handler\n");
    uint32 dummy;
    espmem.stack_mon();

    switch (evt->event)
    {
    case EVENT_STAMODE_CONNECTED:
        esplog.debug("connected to ssid %s, channel %d\n",
                     evt->event_info.connected.ssid,
                     evt->event_info.connected.channel);
        break;
    case EVENT_STAMODE_DISCONNECTED:
        esplog.debug("disconnect from ssid %s, reason %d\n",
                     evt->event_info.disconnected.ssid,
                     evt->event_info.disconnected.reason);
        system_os_post(USER_TASK_PRIO_0, SIG_STAMODE_DISCONNECTED, '0'); // informing everybody of
                                                                         // disconnection from AP
        // if (!espwifi.is_timeout_timer_active())
        // {
        //     espwifi.start_connect_timeout_timer();
        //     esplog.debug("will switch to SOFTAP in 10 seconds but keep trying to reconnect ...\n");
        // }
        // espwifi.start_wait_before_reconnect_timer();
        if (espwifi.is_timeout_timer_active())
            espwifi.stop_connect_timeout_timer();
        switch_to_stationap();
        espwifi.start_wait_before_reconnect_timer();
        break;
    case EVENT_STAMODE_AUTHMODE_CHANGE:
        esplog.debug("authmode change: %d -> %d\n",
                     evt->event_info.auth_change.old_mode,
                     evt->event_info.auth_change.new_mode);
        break;
    case EVENT_STAMODE_DHCP_TIMEOUT:
        esplog.debug("ESPBOT WIFI [STATION]: dhcp timeout, ip:" IPSTR ",mask:" IPSTR ",gw:" IPSTR,
                     IP2STR(&evt->event_info.got_ip.ip),
                     IP2STR(&evt->event_info.got_ip.mask),
                     IP2STR(&evt->event_info.got_ip.gw));
        os_printf("\n");
        break;
    case EVENT_STAMODE_GOT_IP:
        esplog.debug("got IP:" IPSTR ",mask:" IPSTR ",gw:" IPSTR,
                     IP2STR(&evt->event_info.got_ip.ip),
                     IP2STR(&evt->event_info.got_ip.mask),
                     IP2STR(&evt->event_info.got_ip.gw));
        os_printf("\n");
        // station connected to AP and got an IP address
        // whichever was wifi mode now AP mode is no longer required
        esplog.debug("ESP8266 connected as station to %s\n", espwifi.station_get_ssid());
        espwifi.stop_connect_timeout_timer();
        wifi_set_opmode_current(STATION_MODE);
        system_os_post(USER_TASK_PRIO_0, SIG_STAMODE_GOT_IP, '0'); // informing everybody of
                                                                   // successfully connection to AP
        // time to update flash configuration for (eventually) saving ssid and password
        espwifi.save_cfg();
        break;
    case EVENT_SOFTAPMODE_STACONNECTED:
        esplog.debug("station: " MACSTR " join, AID = %d\n",
                     MAC2STR(evt->event_info.sta_connected.mac),
                     evt->event_info.sta_connected.aid);
        system_os_post(USER_TASK_PRIO_0, SIG_SOFTAPMODE_STACONNECTED, '0'); // informing everybody that
                                                                            // a station connected to ESP8266
        break;
    case EVENT_SOFTAPMODE_STADISCONNECTED:
        esplog.debug("station: " MACSTR " leave, AID = %d\n",
                     MAC2STR(evt->event_info.sta_disconnected.mac),
                     evt->event_info.sta_disconnected.aid);
        system_os_post(USER_TASK_PRIO_0, SIG_SOFTAPMODE_STADISCONNECTED, '0'); // informing everybody of
                                                                               // a station disconnected from ESP8266
        break;
    case EVENT_SOFTAPMODE_PROBEREQRECVED:
        esplog.debug("softAP received a probe request\n");
        break;
    case EVENT_OPMODE_CHANGED:
        switch (wifi_get_opmode())
        {
        case STATION_MODE:
            esplog.debug("wifi mode changed to STATION_MODE\n");
            break;
        case SOFTAP_MODE:
            esplog.debug("wifi mode changed to SOFTAP_MODE\n");
            break;
        case STATIONAP_MODE:
            esplog.debug("wifi mode changed to STATIONAP_MODE\n");
            break;
        default:
            break;
        }
        break;
    case EVENT_SOFTAPMODE_DISTRIBUTE_STA_IP:
        esplog.debug("aid %d =>" MACSTR " => " IPSTR "\r\n",
                     evt->event_info.distribute_sta_ip.aid,
                     MAC2STR(evt->event_info.distribute_sta_ip.mac),
                     IP2STR(&evt->event_info.distribute_sta_ip.ip));
        break;
    default:
        esplog.debug("unknown event %x\n", evt->event);
        break;
    }
}

void ICACHE_FLASH_ATTR Wifi::set_stationap(void)
{
    esplog.all("Wifi::set_stationap\n");
    struct ip_info ap_ip;
    struct dhcps_lease dhcp_lease;
    espmem.stack_mon();

    // espwifi.m_timeout_timer_active = false;

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

    esplog.debug("Wi-Fi working as AP\n");
    esplog.debug("AP config: SSID:        %s\n", espwifi.m_ap_config.ssid);
    esplog.debug("AP config: Password:    %s\n", espwifi.m_ap_config.password);
    esplog.debug("AP config: channel:     %d\n", espwifi.m_ap_config.channel);
    switch (espwifi.m_ap_config.authmode)
    {
    case AUTH_OPEN:
        esplog.debug("AP config: Security:    Disabled\n");
        break;
    case AUTH_WEP:
        esplog.debug("AP config: Security:    WEP\n");
        break;
    case AUTH_WPA_PSK:
        esplog.debug("AP config: Security:    WPA_PSK\n");
        break;
    case AUTH_WPA2_PSK:
        esplog.debug("AP config: Security:    WPA2_PSK\n");
        break;
    case AUTH_WPA_WPA2_PSK:
        esplog.debug("AP config: Security:    WPA_WPA2_PSK\n");
        break;
    default:
        esplog.debug("AP config: Security:    Unknown\n");
        break;
    }
    // now start the webserver
    espwebsvr.stop(); // in case there was a web server listening on esp station interface
    espwebsvr.start(80);
}

void ICACHE_FLASH_ATTR Wifi::connect(void)
{
    esplog.all("Wifi::connect\n");
    struct station_config stationConf;

    if (os_strlen(espwifi.m_station_ssid) == 0 || os_strlen(espwifi.m_station_pwd) == 0)
    {
        esplog.error("Wifi::connect: no ssid or password available\n");
        return;
    }
    bool result = wifi_station_ap_number_set(1);
    result = wifi_station_set_reconnect_policy(false);
    result = wifi_station_set_auto_connect(0);

    // disconnect ... just in case
    wifi_station_disconnect();
    // setup station
    os_memset(&stationConf, 0, sizeof(stationConf));
    os_memcpy(stationConf.ssid, espwifi.m_station_ssid, 32);
    os_memcpy(stationConf.password, espwifi.m_station_pwd, 64);
    stationConf.bssid_set = 0;
    wifi_station_set_config_current(&stationConf);
    wifi_station_set_hostname(espbot.get_name());
    // connect
    wifi_station_connect();
    espmem.stack_mon();
}

void ICACHE_FLASH_ATTR Wifi::init()
{
    esplog.all("Wifi::init\n");
    os_strncpy((char *)m_ap_config.ssid, espbot.get_name(), 32); // uint8 ssid[32];
    os_strcpy((char *)m_ap_config.password, "espbot123456");     // uint8 password[64];
    m_ap_config.ssid_len = 0;                                    // uint8 ssid_len;
    m_ap_config.channel = 1;                                     // uint8 channel;
    m_ap_config.authmode = AUTH_WPA2_PSK;                        // uint8 authmode;
    m_ap_config.ssid_hidden = 0;                                 // uint8 ssid_hidden;
    m_ap_config.max_connection = 4;                              // uint8 max_connection;
    m_ap_config.beacon_interval = 100;                           // uint16 beacon_interval;

    if (restore_cfg() != CFG_OK) // something went wrong while loading flash config
    {
        esplog.warn("Wifi::init setting null station config\n");
        os_memset(m_station_ssid, 0, 32);
        os_memset(m_station_pwd, 0, 32);
    }

    m_scan_config = NULL; // will scan for all AP with no filter on channel or ssid
    m_ap_count = 0;
    m_ap_list = NULL;
    m_scan_completed = false;

    m_timeout_timer_active = false;

    os_timer_disarm(&m_station_connect_timeout);
    os_timer_setfn(&m_station_connect_timeout, (os_timer_func_t *)switch_to_stationap, NULL);
    os_timer_disarm(&m_wait_before_reconnect);
    os_timer_setfn(&m_wait_before_reconnect, (os_timer_func_t *)&Wifi::connect, NULL);

    wifi_station_set_reconnect_policy(false);
    wifi_set_phy_mode(PHY_MODE_11N);
    wifi_set_event_handler_cb((wifi_event_handler_cb_t)wifi_event_handler);

    // start as SOFTAP and try to switch to STATION
    // this will ensure that softap and station configurations are set by espbot
    // otherwise default configurations by NON OS SDK are used
    Wifi::set_stationap();
    Wifi::connect();
}

char ICACHE_FLASH_ATTR *Wifi::station_get_ssid(void)
{
    esplog.all("Wifi::station_get_ssid\n");
    return m_station_ssid;
}

char ICACHE_FLASH_ATTR *Wifi::station_get_password(void)
{
    esplog.all("Wifi::station_get_password\n");
    return m_station_pwd;
}

int ICACHE_FLASH_ATTR Wifi::restore_cfg(void)
{
    esplog.all("Wifi::restore_cfg\n");
    File_to_json cfgfile("wifi.cfg");
    espmem.stack_mon();
    if (cfgfile.exists())
    {
        if (cfgfile.find_string("station_ssid"))
        {
            esplog.error("Wifi::restore_cfg - available configuration is incomplete\n");
            return CFG_ERROR;
        }
        os_memset(m_station_ssid, 0, 32);
        os_strncpy(m_station_ssid, cfgfile.get_value(), 31);
        if (cfgfile.find_string("station_pwd"))
        {
            esplog.error("Wifi::restore_cfg - available configuration is incomplete\n");
            return CFG_ERROR;
        }
        os_memset(m_station_pwd, 0, 64);
        os_strncpy(m_station_pwd, cfgfile.get_value(), 63);
        return CFG_OK;
    }
    else
    {
        esplog.warn("Wifi::restore_cfg - cfg file not found\n");
        return CFG_ERROR;
    }
}

int ICACHE_FLASH_ATTR Wifi::saved_cfg_not_update(void)
{
    esplog.all("Wifi::saved_cfg_not_update\n");
    File_to_json cfgfile("wifi.cfg");
    espmem.stack_mon();
    if (cfgfile.exists())
    {
        if (cfgfile.find_string("station_ssid"))
        {
            esplog.error("Wifi::saved_cfg_not_update - available configuration is incomplete\n");
            return CFG_ERROR;
        }
        if (os_strcmp(m_station_ssid, cfgfile.get_value()))
        {
            return CFG_REQUIRES_UPDATE;
        }
        if (cfgfile.find_string("station_pwd"))
        {
            esplog.error("Wifi::saved_cfg_not_update - available configuration is incomplete\n");
            return CFG_ERROR;
        }
        if (os_strcmp(m_station_pwd, cfgfile.get_value()))
        {
            return CFG_REQUIRES_UPDATE;
        }
        return CFG_OK;
    }
    else
    {
        return CFG_REQUIRES_UPDATE;
    }
}

int ICACHE_FLASH_ATTR Wifi::save_cfg(void)
{
    esplog.all("Wifi::save_cfg\n");
    if (saved_cfg_not_update() != CFG_REQUIRES_UPDATE)
        return CFG_OK;
    if (espfs.is_available())
    {
        Ffile cfgfile(&espfs, "wifi.cfg");
        espmem.stack_mon();
        if (cfgfile.is_available())
        {
            cfgfile.clear();
            char *buffer = (char *)esp_zalloc(200);
            if (buffer)
            {
                os_sprintf(buffer, "{\"station_ssid\": \"%s\",\"station_pwd\": \"%s\"}", espwifi.m_station_ssid, espwifi.m_station_pwd);
                cfgfile.n_append(buffer, os_strlen(buffer));
                esp_free(buffer);
            }
            else
            {
                esplog.error("Wifi::save_cfg - not enough heap memory available\n");
                return CFG_ERROR;
            }
        }
        else
        {
            esplog.error("Wifi::save_cfg - cannot open wifi.cfg\n");
            return CFG_ERROR;
        }
    }
    else
    {
        esplog.error("Wifi::save_cfg - file system not available\n");
        return CFG_ERROR;
    }
    return CFG_OK;
}

void ICACHE_FLASH_ATTR Wifi::station_set_ssid(char *t_str, int t_len)
{
    esplog.all("Wifi::station_set_ssid\n");
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
    esplog.all("Wifi::station_set_ssid\n");
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

void ICACHE_FLASH_ATTR Wifi::scan_for_ap(void)
{
    esplog.all("Wifi::scan_for_ap\n");
    m_scan_completed = false;
    wifi_station_scan(m_scan_config, (scan_done_cb_t)Wifi::scan_completed);
}

bool ICACHE_FLASH_ATTR Wifi::scan_for_ap_completed(void)
{
    esplog.all("Wifi::scan_for_ap_completed\n");
    return m_scan_completed;
}

void ICACHE_FLASH_ATTR Wifi::scan_completed(void *arg, STATUS status)
{
    esplog.all("Wifi::scan_completed\n");
    // delete previuos results
    espwifi.free_ap_list();
    // now check results
    if (status == OK)
    {
        // start counting APs
        struct bss_info *scan_list = (struct bss_info *)arg;
        while (scan_list)
        {
            espwifi.m_ap_count++;
            scan_list = scan_list->next.stqe_next;
        }
        // now store APs SSID
        espwifi.m_ap_list = (char *)esp_zalloc(33 * espwifi.m_ap_count);
        if (espwifi.m_ap_list)
        {
            int idx = 0;
            scan_list = (struct bss_info *)arg;
            while (scan_list)
            {
                os_memcpy((espwifi.m_ap_list + (33 * idx)), (char *)scan_list->ssid, 32);
                scan_list = scan_list->next.stqe_next;
                idx++;
            }
        }
        else
            esplog.error("Wifi::scan_completed - not enough heap memory (%d)\n", 33 * espwifi.m_ap_count);
    }
    else
        esplog.error("Wifi::scan_completed - cannot complete ap scan\n");
    espwifi.m_scan_completed = true;
}

int ICACHE_FLASH_ATTR Wifi::get_ap_count(void)
{
    esplog.all("Wifi::get_ap_count\n");
    return m_ap_count;
}

char ICACHE_FLASH_ATTR *Wifi::get_ap_name(int t_idx)
{
    esplog.all("Wifi::get_ap_name\n");
    if (t_idx < m_ap_count)
        return (m_ap_list + (33 * t_idx));
    else
        return "";
}

void ICACHE_FLASH_ATTR Wifi::free_ap_list(void)
{
    esplog.all("Wifi::free_ap_list\n");
    m_ap_count = 0;
    if (m_ap_list)
    {
        esp_free(m_ap_list);
        m_ap_list = NULL;
    }
}

int ICACHE_FLASH_ATTR Wifi::get_op_mode(void)
{
    return wifi_get_opmode();
}

void ICACHE_FLASH_ATTR Wifi::get_ip_address(struct ip_info *t_ip)
{
    if (wifi_get_opmode() == STATIONAP_MODE)
    {
        wifi_get_ip_info(0x01, t_ip);
    }
    else
    {
        wifi_get_ip_info(0x00, t_ip);
    }
}