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
static bool timeout_timer_active;
static os_timer_t station_connect_timeout;
static os_timer_t wait_before_reconnect;

// scan config and result
static int ap_count;
static char *ap_list;

static bool is_timeout_timer_active(void)
{
    return timeout_timer_active;
}

static void start_connect_timeout_timer(void)
{
    os_timer_arm(&station_connect_timeout, WIFI_CONNECT_TIMEOUT, 0);
    timeout_timer_active = true;
}

static void stop_connect_timeout_timer(void)
{
    os_timer_disarm(&station_connect_timeout);
    timeout_timer_active = false;
}

static void switch_to_stationap(void)
{
    if (wifi_get_opmode() == STATIONAP_MODE)
        return;
    else
        Wifi::set_stationap();
}

// DEBUG
// static Profiler *connect_profiler;

void wifi_event_handler(System_Event_t *evt)
{
    switch (evt->event)
    {
    case EVENT_STAMODE_CONNECTED:
        esp_diag.info(WIFI_CONNECTED);
        INFO("connected to %s ch %d",
             evt->event_info.connected.ssid,
             evt->event_info.connected.channel);
        break;
    case EVENT_STAMODE_DISCONNECTED:
        esp_diag.info(WIFI_DISCONNECTED);
        INFO("disconnected from %s rsn %d",
             evt->event_info.disconnected.ssid,
             evt->event_info.disconnected.reason);
        system_os_post(USER_TASK_PRIO_0, SIG_STAMODE_DISCONNECTED, '0'); // informing everybody of
                                                                         // disconnection from AP
        if (is_timeout_timer_active())
            stop_connect_timeout_timer();
        switch_to_stationap();
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
        esp_diag.info(WIFI_GOT_IP);
        INFO("got IP" IPSTR " " IPSTR " " IPSTR,
             IP2STR(&evt->event_info.got_ip.ip),
             IP2STR(&evt->event_info.got_ip.mask),
             IP2STR(&evt->event_info.got_ip.gw));
        // station connected to AP and got an IP address
        // whichever was wifi mode now AP mode is no longer required
        stop_connect_timeout_timer();
        wifi_set_opmode_current(STATION_MODE);
        system_os_post(USER_TASK_PRIO_0, SIG_STAMODE_GOT_IP, '0'); // informing everybody of
                                                                   // successfully connection to AP
        // time to update flash configuration for (eventually) saving ssid and password
        Wifi::save_cfg();
        break;
    case EVENT_SOFTAPMODE_STACONNECTED:
        esp_diag.info(WIFI_STA_CONNECTED);
        INFO(MACSTR " connected (AID %d)",
             MAC2STR(evt->event_info.sta_connected.mac),
             evt->event_info.sta_connected.aid);
        system_os_post(USER_TASK_PRIO_0, SIG_SOFTAPMODE_STACONNECTED, '0'); // informing everybody that
                                                                            // a station connected to ESP8266
        break;
    case EVENT_SOFTAPMODE_STADISCONNECTED:
        esp_diag.info(WIFI_STA_DISCONNECTED);
        INFO(MACSTR " disconnected (AID %d)",
             MAC2STR(evt->event_info.sta_disconnected.mac),
             evt->event_info.sta_disconnected.aid);
        system_os_post(USER_TASK_PRIO_0, SIG_SOFTAPMODE_STADISCONNECTED, '0'); // informing everybody of
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

    wifi_set_opmode_current(STATIONAP_MODE);
    wifi_softap_set_config(&ap_config);

    wifi_softap_dhcps_stop();
    IP4_ADDR(&ap_ip.ip, 192, 168, 10, 1);
    IP4_ADDR(&ap_ip.gw, 192, 168, 10, 1);
    IP4_ADDR(&ap_ip.netmask, 255, 255, 255, 0);
    wifi_set_ip_info(SOFTAP_IF, &ap_ip);
    IP4_ADDR(&dhcp_lease.start_ip, 192, 168, 10, 100);
    IP4_ADDR(&dhcp_lease.end_ip, 192, 168, 10, 103);
    wifi_softap_set_dhcps_lease(&dhcp_lease);
    wifi_softap_dhcps_start();

    // TRACE("Wi-Fi working as AP");
    // TRACE("AP config: SSID:        %s", ap_config.ssid);
    // TRACE("AP config: Password:    %s", ap_config.password);
    // TRACE("AP config: channel:     %d", ap_config.channel);
    // switch (ap_config.authmode)
    // {
    // case AUTH_OPEN:
    //     TRACE("AP config: Security:    Disabled");
    //     break;
    // case AUTH_WEP:
    //     TRACE("AP config: Security:    WEP");
    //     break;
    // case AUTH_WPA_PSK:
    //     TRACE("AP config: Security:    WPA_PSK");
    //     break;
    // case AUTH_WPA2_PSK:
    //     TRACE("AP config: Security:    WPA2_PSK");
    //     break;
    // case AUTH_WPA_WPA2_PSK:
    //     TRACE("AP config: Security:    WPA_WPA2_PSK");
    //     break;
    // default:
    //     TRACE("AP config: Security:    Unknown");
    //     break;
    // }
    // now start the webserver
    espwebsvr.stop(); // in case there was a web server listening on esp station interface
    espwebsvr.start(80);
}

void Wifi::connect(void)
{
    ALL("Wifi::connect");
    struct station_config stationConf;

    if (os_strlen(station_ssid) == 0 || os_strlen(station_pwd) == 0)
    {
        esp_diag.error(WIFI_CONNECT_NO_SSID_OR_PASSWORD_AVAILABLE);
        ERROR("Wifi::connect no ssid or password available");
        return;
    }
    bool result = wifi_station_ap_number_set(1);
    result = wifi_station_set_reconnect_policy(false);
    if (wifi_station_get_auto_connect() != 0)
        result = wifi_station_set_auto_connect(0);

    // disconnect ... just in case
    wifi_station_disconnect();
    // setup station
    os_memset(&stationConf, 0, sizeof(stationConf));
    os_memcpy(stationConf.ssid, station_ssid, 32);
    os_memcpy(stationConf.password, station_pwd, 64);
    stationConf.bssid_set = 0;
    wifi_station_set_config_current(&stationConf);
    wifi_station_set_hostname(espbot.get_name());
    // connect
    // connect_profiler = new Profiler("connect");
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
            esp_diag.error(WIFI_RESTORE_CFG_INCOMPLETE);
            ERROR("Wifi::restore_cfg incomplete cfg");
            return CFG_ERROR;
        }
        os_memset(station_ssid, 0, 32);
        os_strncpy(station_ssid, cfgfile.get_value(), 31);
        if (cfgfile.find_string(f_str("station_pwd")))
        {
            esp_diag.error(WIFI_RESTORE_CFG_INCOMPLETE);
            ERROR("Wifi::restore_cfg incomplete cfg");
            return CFG_ERROR;
        }
        os_memset(station_pwd, 0, 64);
        os_strncpy(station_pwd, cfgfile.get_value(), 63);
        return CFG_OK;
    }
    else
    {
        esp_diag.error(WIFI_RESTORE_CFG_FILE_NOT_FOUND);
        WARN("Wifi::restore_cfg file not found");
        return CFG_ERROR;
    }
}

static int saved_cfg_not_updated(void)
{
    ALL("Wifi::saved_cfg_not_updated");
    File_to_json cfgfile(f_str("wifi.cfg"));
    espmem.stack_mon();
    if (cfgfile.exists())
    {
        if (cfgfile.find_string(f_str("station_ssid")))
        {
            esp_diag.error(WIFI_SAVED_CFG_NOT_UPDATED_INCOMPLETE);
            ERROR("Wifi::saved_cfg_not_updated incomplete cfg");
            return CFG_ERROR;
        }
        if (os_strcmp(station_ssid, cfgfile.get_value()))
        {
            return CFG_REQUIRES_UPDATE;
        }
        if (cfgfile.find_string(f_str("station_pwd")))
        {
            esp_diag.error(WIFI_SAVED_CFG_NOT_UPDATED_INCOMPLETE);
            ERROR("Wifi::saved_cfg_not_updated incomplete cfg");
            return CFG_ERROR;
        }
        if (os_strcmp(station_pwd, cfgfile.get_value()))
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

void Wifi::init()
{
    os_strncpy((char *)ap_config.ssid, espbot.get_name(), 32); // uint8 ssid[32];
    os_strcpy((char *)ap_config.password, "espbot123456");     // uint8 password[64];
    ap_config.ssid_len = 0;                                    // uint8 ssid_len;
    ap_config.channel = 1;                                     // uint8 channel;
    ap_config.authmode = AUTH_WPA2_PSK;                        // uint8 authmode;
    ap_config.ssid_hidden = 0;                                 // uint8 ssid_hidden;
    ap_config.max_connection = 4;                              // uint8 max_connection;
    ap_config.beacon_interval = 100;                           // uint16 beacon_interval;

    if (restore_cfg() != CFG_OK) // something went wrong while loading flash config
    {
        esp_diag.warn(WIFI_INIT_CFG_DEFAULT_CFG);
        WARN("Wifi::init setting null station cfg");
        os_memset(station_ssid, 0, 32);
        os_memset(station_pwd, 0, 32);
    }

    ap_count = 0;
    ap_list = NULL;

    timeout_timer_active = false;

    os_timer_disarm(&station_connect_timeout);
    os_timer_setfn(&station_connect_timeout, (os_timer_func_t *)switch_to_stationap, NULL);
    os_timer_disarm(&wait_before_reconnect);
    os_timer_setfn(&wait_before_reconnect, (os_timer_func_t *)&Wifi::connect, NULL);

    if (wifi_station_get_auto_connect() != 0)
        wifi_station_set_auto_connect(0);

    wifi_station_set_reconnect_policy(false);
    if (wifi_get_phy_mode() != PHY_MODE_11N)
        wifi_set_phy_mode(PHY_MODE_11N);
    wifi_set_event_handler_cb((wifi_event_handler_cb_t)wifi_event_handler);

    // start as SOFTAP and try to switch to STATION
    // this will ensure that softap and station configurations are set by espbot
    // otherwise default configurations by NON OS SDK are used
    Wifi::set_stationap();
    Wifi::connect();
}

char *Wifi::station_get_ssid(void)
{
    return station_ssid;
}

char *Wifi::station_get_password(void)
{
    return station_pwd;
}

int Wifi::save_cfg(void)
{
    ALL("Wifi::saved_cfg");
    if (saved_cfg_not_updated() != CFG_REQUIRES_UPDATE)
        return CFG_OK;
    if (espfs.is_available())
    {
        Ffile cfgfile(&espfs, (char *)f_str("wifi.cfg"));
        espmem.stack_mon();
        if (cfgfile.is_available())
        {
            cfgfile.clear();
            // {"station_ssid": "","station_pwd": ""}
            // 38 + 1 + 33 + 65 = 137
            Heap_chunk buffer(137);
            if (buffer.ref)
            {
                fs_sprintf(buffer.ref,
                           "{\"station_ssid\": \"%s\",\"station_pwd\": \"%s\"}",
                           station_ssid,
                           station_pwd);
                cfgfile.n_append(buffer.ref, os_strlen(buffer.ref));
            }
            else
            {
                esp_diag.error(WIFI_SAVE_CFG_HEAP_EXHAUSTED, 137);
                ERROR("Wifi::save_cfg heap exhausted %d", 137);
                return CFG_ERROR;
            }
        }
        else
        {
            esp_diag.error(WIFI_SAVE_CFG_CANNOT_OPEN_FILE);
            ERROR("Wifi::save_cfg cannot open file");
            return CFG_ERROR;
        }
    }
    else
    {
        esp_diag.error(WIFI_SAVE_CFG_FS_NOT_AVAILABLE);
        ERROR("Wifi::save_cfg FS not available");
        return CFG_ERROR;
    }
    return CFG_OK;
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