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
#include "osapi.h"
#include "user_interface.h"
}

#include "espbot.hpp"
#include "espbot_config.hpp"
#include "espbot_diagnostic.hpp"
#include "espbot_event_codes.h"
#include "espbot_global.hpp"
#include "espbot_json.hpp"
#include "espbot_mem_mon.hpp"
#include "espbot_utils.hpp"
#include "espbot_wifi.hpp"

// connection management vars
static struct softap_config ap_config;
static char station_ssid[32];
static char station_pwd[64];
// static bool timeout_timer_active;
// static os_timer_t station_connect_timeout;
static os_timer_t wait_before_reconnect;
static uint32 stamode_connecting;
static bool stamode_connected;

// scan config and result
static int ap_count;
static char *ap_list;

bool Wifi::is_connected(void)
{
    return stamode_connected;
}

void wifi_event_handler(System_Event_t *evt)
{
    static ip_addr old_ip = {0};
    switch (evt->event)
    {
    case EVENT_STAMODE_CONNECTED:
        esp_diag.info(WIFI_CONNECTED);
        INFO("connected to %s ch %d",
             evt->event_info.connected.ssid,
             evt->event_info.connected.channel);
        break;
    case EVENT_STAMODE_DISCONNECTED:
        if (stamode_connected || (stamode_connecting == 1))
        {
            // just log one disconnection, avoid errors flood...
            // so only if it was connected or never connected to an AP
            esp_diag.error(WIFI_DISCONNECTED, evt->event_info.disconnected.reason);
            ERROR("disconnected from %s rsn %d",
                  evt->event_info.disconnected.ssid,
                  evt->event_info.disconnected.reason);
            system_os_post(USER_TASK_PRIO_0, SIG_STAMODE_DISCONNECTED, '0'); // informing everybody of
                                                                             // disconnection from AP
            stamode_connected = false;
        }
        if (wifi_get_opmode() == STATION_MODE)
        {
            Wifi::set_stationap();
            system_os_post(USER_TASK_PRIO_0, SIG_SOFTAPMODE_READY, '0');
        }
        os_timer_disarm(&wait_before_reconnect);
        os_timer_arm(&wait_before_reconnect, WIFI_WAIT_BEFORE_RECONNECT, 0);
        break;
    case EVENT_STAMODE_AUTHMODE_CHANGE:
        esp_diag.info(WIFI_AUTHMODE_CHANGE, evt->event_info.auth_change.new_mode);
        INFO("wifi auth %d -> %d",
             evt->event_info.auth_change.old_mode,
             evt->event_info.auth_change.new_mode);
        break;
    case EVENT_STAMODE_DHCP_TIMEOUT:
        esp_diag.warn(WIFI_DHCP_TIMEOUT);
        WARN("dhcp timeout");
        // DEBUG("ESPBOT WIFI [STATION]: dhcp timeout, ip:" IPSTR ",mask:" IPSTR ",gw:" IPSTR,
        //       IP2STR(&evt->event_info.got_ip.ip),
        //       IP2STR(&evt->event_info.got_ip.mask),
        //       IP2STR(&evt->event_info.got_ip.gw));
        break;
    case EVENT_STAMODE_GOT_IP:
        if (stamode_connected)
        {
            // there was no EVENT_STAMODE_DISCONNECTED
            // likely a lease renewal or similar
            // checkout if the IP address changed
            if (old_ip.addr != evt->event_info.got_ip.ip.addr)
                // informing everybody that the IP address changed
                system_os_post(USER_TASK_PRIO_0, SIG_STAMODE_GOT_IP, GOT_IP_ALREADY_CONNECTED);
        }
        else
        {
            // now it's really 'connected' to AP
            stamode_connected = true;
            stamode_connecting = 1; // was connected at least one time

            os_memcpy(&old_ip, &evt->event_info.got_ip.ip, sizeof(struct ip_addr));
            esp_diag.info(WIFI_GOT_IP);
            INFO("got IP " IPSTR " " IPSTR " " IPSTR,
                 IP2STR(&evt->event_info.got_ip.ip),
                 IP2STR(&evt->event_info.got_ip.mask),
                 IP2STR(&evt->event_info.got_ip.gw));
            // station connected to AP and got an IP address
            // whichever was wifi mode now AP mode is no longer required
            // stop_connect_timeout_timer();
            wifi_set_opmode_current(STATION_MODE);
            // informing everybody of successfully connection to AP
            system_os_post(USER_TASK_PRIO_0, SIG_STAMODE_GOT_IP, GOT_IP_AFTER_CONNECTION);
            // time to update flash configuration for (eventually) saving ssid and password
            Wifi::save_cfg();
        }
        break;
    case EVENT_SOFTAPMODE_STACONNECTED:
        esp_diag.info(WIFI_STA_CONNECTED);
        INFO(MACSTR " connected (AID %d)",
             MAC2STR(evt->event_info.sta_connected.mac),
             evt->event_info.sta_connected.aid);
        // system_os_post(USER_TASK_PRIO_0, SIG_SOFTAPMODE_STACONNECTED, '0'); // informing everybody that
        // a station connected to ESP8266
        break;
    case EVENT_SOFTAPMODE_STADISCONNECTED:
        esp_diag.info(WIFI_STA_DISCONNECTED);
        INFO(MACSTR " disconnected (AID %d)",
             MAC2STR(evt->event_info.sta_disconnected.mac),
             evt->event_info.sta_disconnected.aid);
        // system_os_post(USER_TASK_PRIO_0, SIG_SOFTAPMODE_STADISCONNECTED, '0'); // informing everybody of
        // a station disconnected from ESP8266
        break;
    case EVENT_SOFTAPMODE_PROBEREQRECVED:
        TRACE("AP probed");
        break;
    case EVENT_OPMODE_CHANGED:
        esp_diag.info(WIFI_OPMODE_CHANGED, wifi_get_opmode());
        switch (wifi_get_opmode())
        {
        case STATION_MODE:
            INFO("wifi -> STA");
            break;
        case SOFTAP_MODE:
            INFO("wifi -> SOFTAP");
            break;
        case STATIONAP_MODE:
            INFO("wifi -> STATIONAP");
            break;
        default:
            break;
        }
        break;
    case EVENT_SOFTAPMODE_DISTRIBUTE_STA_IP:
        TRACE(IPSTR " assigned to " MACSTR " (aid %d)",
              IP2STR(&evt->event_info.distribute_sta_ip.ip),
              MAC2STR(evt->event_info.distribute_sta_ip.mac),
              evt->event_info.distribute_sta_ip.aid);
        break;
    default:
        TRACE("unknown event %x", evt->event);
        break;
    }
}

void Wifi::set_stationap(void)
{
    ALL("Wifi::set_stationap");
    struct ip_info ap_ip;
    struct dhcps_lease dhcp_lease;
    espmem.stack_mon();

    // switch to SOFTAP otherwise wifi_softap_set_config_current won't work
    // wifi_set_opmode_current(SOFTAP_MODE);
    // now switch to STATIONAP
    wifi_set_opmode_current(STATIONAP_MODE);
    if (!wifi_softap_set_config_current(&ap_config))
    {
        esp_diag.error(WIFI_SETAP_ERROR);
        ERROR("Wifi::set_stationap failed to set AP cfg");
    }

    wifi_softap_dhcps_stop();
    IP4_ADDR(&ap_ip.ip, 192, 168, 10, 1);
    IP4_ADDR(&ap_ip.gw, 192, 168, 10, 1);
    IP4_ADDR(&ap_ip.netmask, 255, 255, 255, 0);
    wifi_set_ip_info(SOFTAP_IF, &ap_ip);
    IP4_ADDR(&dhcp_lease.start_ip, 192, 168, 10, 100);
    IP4_ADDR(&dhcp_lease.end_ip, 192, 168, 10, 103);
    wifi_softap_set_dhcps_lease(&dhcp_lease);
    wifi_softap_dhcps_start();

    // get the cfg so that logs show the real AP cfg
    struct softap_config log_ap;
    wifi_softap_get_config(&log_ap);
    TRACE("Wi-Fi working as AP");
    TRACE("AP config: SSID:        %s", log_ap.ssid);
    TRACE("AP config: Password:    %s", log_ap.password);
    TRACE("AP config: channel:     %d", log_ap.channel);
    switch (log_ap.authmode)
    {
    case AUTH_OPEN:
        TRACE("AP config: Security:    Disabled");
        break;
    case AUTH_WEP:
        TRACE("AP config: Security:    WEP");
        break;
    case AUTH_WPA_PSK:
        TRACE("AP config: Security:    WPA_PSK");
        break;
    case AUTH_WPA2_PSK:
        TRACE("AP config: Security:    WPA2_PSK");
        break;
    case AUTH_WPA_WPA2_PSK:
        TRACE("AP config: Security:    WPA_WPA2_PSK");
        break;
    default:
        TRACE("AP config: Security:    Unknown");
        break;
    }
}

void Wifi::connect(void)
{
    ALL("Wifi::connect");
    struct station_config stationConf;

    if (os_strlen(station_ssid) == 0)
    {
        esp_diag.error(WIFI_CONNECT_NO_SSID_AVAILABLE);
        ERROR("Wifi::connect no ssid available");
        return;
    }
    bool result = wifi_station_ap_number_set(1);
    result = wifi_station_set_reconnect_policy(false);
    if (wifi_station_get_auto_connect() != 0)
        result = wifi_station_set_auto_connect(0);

    // setup station
    os_memset(&stationConf, 0, sizeof(stationConf));
    os_memcpy(stationConf.ssid, station_ssid, 32);
    os_memcpy(stationConf.password, station_pwd, 64);
    stationConf.bssid_set = 0;
    wifi_station_set_config_current(&stationConf);
    wifi_station_set_hostname(espbot.get_name());

    // connect
    stamode_connecting++;
    wifi_station_connect();
    espmem.stack_mon();
}

static int restore_cfg(void)
{
    ALL("Wifi::restore_cfg");
    File_to_json cfgfile(f_str("wifi.cfg"));
    espmem.stack_mon();
    if (cfgfile.exists())
    {
        if (cfgfile.find_string(f_str("station_ssid")))
        {
            esp_diag.info(WIFI_RESTORE_CFG_NO_SSID_FOUND);
            INFO("Wifi::restore_cfg no SSID found");
        }
        else
        {
            os_memset(station_ssid, 0, 32);
            os_strncpy(station_ssid, cfgfile.get_value(), 31);
        }
        if (cfgfile.find_string(f_str("station_pwd")))
        {
            esp_diag.info(WIFI_RESTORE_CFG_NO_PWD_FOUND);
            INFO("Wifi::restore_cfg no PWD found");
        }
        else
        {
            os_memset(station_pwd, 0, 64);
            os_strncpy(station_pwd, cfgfile.get_value(), 63);
        }
        if (cfgfile.find_string(f_str("ap_pwd")))
        {
            esp_diag.info(WIFI_RESTORE_CFG_AP_DEFAULT_PWD);
            INFO("Wifi::restore_cfg AP default password");
        }
        else
        {
            os_memset((char *)ap_config.password, 0, 64);
            os_strncpy((char *)ap_config.password, cfgfile.get_value(), 63);
            if (0 == os_strcmp((char *)ap_config.password, f_str("espbot123456")))
            {
                esp_diag.info(WIFI_RESTORE_CFG_AP_DEFAULT_PWD);
                INFO("Wifi::restore_cfg AP default password");
            }
            else
            {
                esp_diag.info(WIFI_RESTORE_CFG_AP_CUSTOM_PWD);
                INFO("Wifi::restore_cfg AP custom password");
            }
        }
        if (cfgfile.find_string(f_str("ap_channel")))
        {
            esp_diag.info(WIFI_RESTORE_CFG_AP_CH, 1);
            INFO("Wifi::restore_cfg AP channel: %d", 1);
        }
        else
        {
            int channel = atoi(cfgfile.get_value());
            if ((channel < 1) || (channel > 11))
            {
                esp_diag.error(WIFI_RESTORE_CFG_AP_CH_OOR, channel);
                ERROR("Wifi::restore_cfg AP channel out of range (%)", channel);
                channel = 1;
            }
            ap_config.channel = channel;
            esp_diag.info(WIFI_RESTORE_CFG_AP_CH, channel);
            INFO("Wifi::restore_cfg AP channel: %d", channel);
        }
        return CFG_OK;
    }
    else
    {
        esp_diag.info(WIFI_RESTORE_CFG_FILE_NOT_FOUND);
        INFO("Wifi::restore_cfg file not found");
        return CFG_ERROR;
    }
}

static int saved_cfg_not_updated(void)
{
    ALL("Wifi::saved_cfg_not_updated");
    File_to_json cfgfile(f_str("wifi.cfg"));
    espmem.stack_mon();
    if (!cfgfile.exists())
    {
        return CFG_REQUIRES_UPDATE;
    }
    if (cfgfile.find_string(f_str("station_ssid")))
    {
        DEBUG("Wifi::saved_cfg_not_updated incomplete cfg");
        return CFG_REQUIRES_UPDATE;
    }
    if (os_strcmp(station_ssid, cfgfile.get_value()))
    {
        return CFG_REQUIRES_UPDATE;
    }
    if (cfgfile.find_string(f_str("station_pwd")))
    {
        DEBUG("Wifi::saved_cfg_not_updated incomplete cfg");
        return CFG_REQUIRES_UPDATE;
    }
    if (os_strcmp(station_pwd, cfgfile.get_value()))
    {
        return CFG_REQUIRES_UPDATE;
    }
    if (cfgfile.find_string(f_str("ap_pwd")))
    {
        DEBUG("Wifi::saved_cfg_not_updated incomplete cfg");
        return CFG_REQUIRES_UPDATE;
    }
    if (os_strcmp((char *)ap_config.password, cfgfile.get_value()))
    {
        return CFG_REQUIRES_UPDATE;
    }
    if (cfgfile.find_string(f_str("ap_channel")))
    {
        DEBUG("Wifi::saved_cfg_not_updated incomplete cfg");
        return CFG_REQUIRES_UPDATE;
    }
    if (ap_config.channel != atoi(cfgfile.get_value()))
    {
        return CFG_REQUIRES_UPDATE;
    }
    return CFG_OK;
}

int Wifi::save_cfg(void)
{
    ALL("Wifi::saved_cfg");
    // don't write to flash when no update is required
    if (saved_cfg_not_updated() != CFG_REQUIRES_UPDATE)
        return CFG_OK;
    // writes update to flash
    if (!espfs.is_available())
    {
        esp_diag.error(WIFI_SAVE_CFG_FS_NOT_AVAILABLE);
        ERROR("Wifi::save_cfg FS not available");
        return CFG_ERROR;
    }
    Ffile cfgfile(&espfs, (char *)f_str("wifi.cfg"));
    espmem.stack_mon();
    if (!cfgfile.is_available())
    {
        esp_diag.error(WIFI_SAVE_CFG_CANNOT_OPEN_FILE);
        ERROR("Wifi::save_cfg cannot open file");
        return CFG_ERROR;
    }
    cfgfile.clear();
    // {"station_ssid":"","station_pwd":"","ap_pwd":"","ap_channel":11}
    // 64 + 1 + 32 + 64 + 64 + 2 = 137
    Heap_chunk buffer(227);
    if (buffer.ref == NULL)
    {
        esp_diag.error(WIFI_SAVE_CFG_HEAP_EXHAUSTED, 227);
        ERROR("Wifi::save_cfg heap exhausted %d", 227);
        return CFG_ERROR;
    }
    fs_sprintf(buffer.ref,
               "{\"station_ssid\":\"%s\",\"station_pwd\":\"%s\",\"ap_pwd\":\"%s\",\"ap_channel\":%d}",
               station_ssid,
               station_pwd,
               (char *)ap_config.password,
               ap_config.channel);
    cfgfile.n_append(buffer.ref, os_strlen(buffer.ref));
    return CFG_OK;
}

void Wifi::init()
{
    // default AP config
    os_strncpy((char *)ap_config.ssid, espbot.get_name(), 32);    // uint8 ssid[32];
    os_memset((char *)ap_config.password, 0, 64);                 //
    os_strcpy((char *)ap_config.password, f_str("espbot123456")); // uint8 password[64];
    ap_config.ssid_len = os_strlen(espbot.get_name());            // uint8 ssid_len;
    ap_config.channel = 1;                                        // uint8 channel;
    ap_config.authmode = AUTH_WPA2_PSK;                           // uint8 authmode;
    ap_config.ssid_hidden = 0;                                    // uint8 ssid_hidden;
    ap_config.max_connection = 4;                                 // uint8 max_connection;
    ap_config.beacon_interval = 100;                              // uint16 beacon_interval;

    // default STATION config
    wifi_station_set_reconnect_policy(false);
    if (wifi_station_get_auto_connect() != 0)
        wifi_station_set_auto_connect(0);
    os_timer_disarm(&wait_before_reconnect);
    os_timer_setfn(&wait_before_reconnect, (os_timer_func_t *)&Wifi::connect, NULL);
    if (wifi_get_phy_mode() != PHY_MODE_11N)
        wifi_set_phy_mode(PHY_MODE_11N);
    wifi_set_event_handler_cb((wifi_event_handler_cb_t)wifi_event_handler);
    os_memset(station_ssid, 0, 32);
    os_memset(station_pwd, 0, 64);
    stamode_connected = false;
    stamode_connecting = 0; // never connected

    ap_count = 0;
    ap_list = NULL;

    // overwrite AP and STATION config from file, if any...
    restore_cfg();

    // start as SOFTAP and try to switch to STATION
    // this will ensure that softap and station configurations are set by espbot
    // otherwise default configurations by NON OS SDK are used
    Wifi::set_stationap(); // make effective the restored configration
    // signal that SOFTAPMODE is ready
    system_os_post(USER_TASK_PRIO_0, SIG_SOFTAPMODE_READY, '0');
    if (os_strlen(station_ssid) > 0)
        Wifi::connect();
}

void Wifi::station_set_ssid(char *t_str, int t_len)
{
    ALL("Wifi::station_set_ssid");
    os_memset(station_ssid, 0, 32);
    if (t_len > 31)
    {
        esp_diag.warn(WIFI_TRUNCATING_STRING_TO_31_CHAR);
        WARN("Wifi::station_set_ssid: truncating ssid to 31 characters");
        os_strncpy(station_ssid, t_str, 31);
    }
    else
    {
        os_strncpy(station_ssid, t_str, t_len);
    }
}

char *Wifi::station_get_ssid(void)
{
    return station_ssid;
}

void Wifi::station_set_pwd(char *t_str, int t_len)
{
    ALL("Wifi::station_set_pwd");
    os_memset(station_pwd, 0, 64);
    if (t_len > 63)
    {
        esp_diag.warn(WIFI_TRUNCATING_STRING_TO_63_CHAR);
        WARN("Wifi::station_set_pwd: truncating pwd to 63 characters");
        os_strncpy(station_pwd, t_str, 63);
    }
    else
    {
        os_strncpy(station_pwd, t_str, t_len);
    }
}

char *Wifi::station_get_password(void)
{
    return station_pwd;
}

void Wifi::ap_set_pwd(char *t_str, int t_len)
{
    ALL("Wifi::ap_set_pwd");
    os_memset(ap_config.password, 0, 64);
    if (t_len > 63)
    {
        esp_diag.warn(WIFI_TRUNCATING_STRING_TO_63_CHAR);
        WARN("Wifi::ap_set_pwd: truncating pwd to 63 characters");
        os_strncpy((char *)ap_config.password, t_str, 63);
    }
    else
    {
        os_strncpy((char *)ap_config.password, t_str, t_len);
    }
    // in case wifi is already in STATIONAP_MODE update config
    if (wifi_get_opmode() != STATION_MODE)
    {
        Wifi::set_stationap();
    }
}

char *Wifi::ap_get_password(void)
{
    return (char *)ap_config.password;
}

void Wifi::ap_set_ch(int ch)
{
    ALL("Wifi::ap_set_ch");
    if ((ch < 1) || (ch > 11))
    {
        esp_diag.error(WIFI_AP_SET_CH_OOR, ch);
        ERROR("Wifi::ap_set_ch: %d is OOR, AP channel set to 1");
        ap_config.channel = 1;
    }
    else
    {
        ap_config.channel = ch;
    }
    // in case wifi is already in STATIONAP_MODE update config
    if (wifi_get_opmode() == STATIONAP_MODE)
    {
        Wifi::set_stationap();
    }
}

int Wifi::ap_get_ch(void)
{
    return ap_config.channel;
}

static void (*scan_completed_cb)(void *) = NULL;
static void *scan_completed_param = NULL;

static void fill_in_ap_list(void *arg, STATUS status)
{
    ALL("fill_in_ap_list");
    // delete previuos results
    Wifi::free_ap_list();
    // now check results
    if (status == OK)
    {
        // start counting APs
        struct bss_info *scan_list = (struct bss_info *)arg;
        TRACE("WIFI scan result: BSSID              ch  rssi  SSID");
        while (scan_list)
        {
            TRACE("WIFI scan result: %X:%X:%X:%X:%X:%X  %d    %d   %s",
                  scan_list->bssid[0],
                  scan_list->bssid[1],
                  scan_list->bssid[2],
                  scan_list->bssid[3],
                  scan_list->bssid[4],
                  scan_list->bssid[5],
                  scan_list->channel,
                  scan_list->rssi,
                  scan_list->ssid);
            ap_count++;
            scan_list = scan_list->next.stqe_next;
        }
        // now store APs SSID
        ap_list = new char[33 * ap_count];
        if (ap_list)
        {
            int idx = 0;
            scan_list = (struct bss_info *)arg;
            while (scan_list)
            {
                os_memcpy((ap_list + (33 * idx)), (char *)scan_list->ssid, 32);
                scan_list = scan_list->next.stqe_next;
                idx++;
            }
        }
        else
        {
            esp_diag.error(WIFI_AP_LIST_HEAP_EXHAUSTED, 33 * ap_count);
            ERROR("Wifi::fill_in_ap_list heap exhausted %d", 33 * ap_count);
        }
    }
    else
    {
        esp_diag.error(WIFI_AP_LIST_CANNOT_COMPLETE_SCAN);
        ERROR("Wifi::fill_in_ap_list cannot complete ap scan");
    }
    if (scan_completed_cb)
        scan_completed_cb(scan_completed_param);
}

void Wifi::scan_for_ap(struct scan_config *config, void (*callback)(void *), void *param)
{
    scan_completed_cb = callback;
    scan_completed_param = param;
    wifi_station_scan(config, (scan_done_cb_t)fill_in_ap_list);
}

int Wifi::get_ap_count(void)
{
    return ap_count;
}

char *Wifi::get_ap_name(int t_idx)
{
    if (t_idx < ap_count)
        return (ap_list + (33 * t_idx));
    else
        return "";
}

void Wifi::free_ap_list(void)
{
    ap_count = 0;
    if (ap_list)
    {
        delete[] ap_list;
        ap_list = NULL;
    }
}

// static Profiler *fast_scan_profiler;

static struct scan_config qs_cfg;
static struct fast_scan qs_vars;

static void fast_scan_check_results(void *arg, STATUS status)
{
    if (status == OK)
    {
        struct bss_info *scan_list = (struct bss_info *)arg;
        while (scan_list)
        {
            if (0 == os_strncmp((char *)qs_cfg.ssid, (char *)scan_list->ssid, qs_vars.ssid_len))
            {
                if (scan_list->rssi > qs_vars.best_rssi)
                {
                    // update bssid and channel
                    qs_vars.best_rssi = scan_list->rssi;
                    qs_vars.best_channel = scan_list->channel;
                    os_memcpy(qs_vars.best_bssid, scan_list->bssid, 6);
                }
            }
            scan_list = scan_list->next.stqe_next;
        }
    }
    qs_vars.ch_idx++;
    if (qs_vars.ch_idx < qs_vars.ch_count)
    {
        qs_cfg.channel = qs_vars.ch_list[qs_vars.ch_idx];
        wifi_station_scan(&qs_cfg, (scan_done_cb_t)fast_scan_check_results);
    }
    else
    {
        if (qs_vars.callback)
            qs_vars.callback(qs_vars.param);
    }
}

void Wifi::fast_scan_for_best_ap(char *ssid, char *ch_list, char ch_count, void (*callback)(void *), void *param)
{
    // init results
    qs_vars.best_rssi = -128;
    qs_vars.best_channel = 0;
    os_memset(qs_vars.best_bssid, 0, 6);

    // init algo vars
    qs_vars.ssid_len = os_strlen(ssid);
    qs_vars.ch_list = ch_list;
    qs_vars.ch_count = ch_count;
    qs_vars.callback = callback;
    qs_vars.param = param;

    qs_cfg.ssid = (uint8 *)ssid;
    qs_cfg.show_hidden = 0;
    qs_cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    qs_cfg.scan_time.active.min = 0;
    qs_cfg.scan_time.active.max = 30;

    qs_vars.ch_idx = 0;
    if (qs_vars.ch_idx < qs_vars.ch_count)
    {
        qs_cfg.channel = qs_vars.ch_list[qs_vars.ch_idx];
        wifi_station_scan(&qs_cfg, (scan_done_cb_t)fast_scan_check_results);
    }
    else
    {
        if (qs_vars.callback)
            qs_vars.callback(qs_vars.param);
    }
}

struct fast_scan *Wifi::get_fast_scan_results(void)
{
    return &qs_vars;
}

int Wifi::get_op_mode(void)
{
    return wifi_get_opmode();
}

void Wifi::get_ip_address(struct ip_info *t_ip)
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