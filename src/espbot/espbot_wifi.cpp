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
#include "espbot_cfgfile.hpp"
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

void wifi_event_handler(System_Event_t *evt)
{
    static ip_addr old_ip = {0};
    switch (evt->event)
    {
    case EVENT_STAMODE_CONNECTED:
        dia_info_evnt(WIFI_CONNECTED);
        INFO("connected to %s ch %d",
             evt->event_info.connected.ssid,
             evt->event_info.connected.channel);
        break;
    case EVENT_STAMODE_DISCONNECTED:
        if (stamode_connected || (stamode_connecting == 1))
        {
            // just log one disconnection, avoid errors flood...
            // so only if it was connected or never connected to an AP
            if (evt->event_info.disconnected.reason == REASON_ASSOC_LEAVE)
            {
                dia_info_evnt(WIFI_DISCONNECTED, evt->event_info.disconnected.reason);
                INFO("disconnected from %s rsn %d",
                     evt->event_info.disconnected.ssid,
                     evt->event_info.disconnected.reason);
                stamode_connecting = 0; // never connected
            }
            else
            {
                dia_error_evnt(WIFI_DISCONNECTED, evt->event_info.disconnected.reason);
                ERROR("disconnected from %s rsn %d",
                      evt->event_info.disconnected.ssid,
                      evt->event_info.disconnected.reason);
            }
            system_os_post(USER_TASK_PRIO_0, SIG_STAMODE_DISCONNECTED, '0'); // informing everybody of
                                                                             // disconnection from AP
            stamode_connected = false;
        }
        if (wifi_get_opmode() == STATION_MODE)
        {
            espwifi_work_as_ap();
            system_os_post(USER_TASK_PRIO_0, SIG_SOFTAPMODE_READY, '0');
        }
        os_timer_disarm(&wait_before_reconnect);
        os_timer_arm(&wait_before_reconnect, WIFI_WAIT_BEFORE_RECONNECT, 0);
        break;
    case EVENT_STAMODE_AUTHMODE_CHANGE:
        dia_info_evnt(WIFI_AUTHMODE_CHANGE, evt->event_info.auth_change.new_mode);
        INFO("wifi auth %d -> %d",
             evt->event_info.auth_change.old_mode,
             evt->event_info.auth_change.new_mode);
        break;
    case EVENT_STAMODE_DHCP_TIMEOUT:
        dia_warn_evnt(WIFI_DHCP_TIMEOUT);
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
            dia_info_evnt(WIFI_GOT_IP);
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
            espwifi_cfg_save();
        }
        break;
    case EVENT_SOFTAPMODE_STACONNECTED:
        dia_info_evnt(WIFI_STA_CONNECTED);
        INFO(MACSTR " connected (AID %d)",
             MAC2STR(evt->event_info.sta_connected.mac),
             evt->event_info.sta_connected.aid);
        // system_os_post(USER_TASK_PRIO_0, SIG_SOFTAPMODE_STACONNECTED, '0'); // informing everybody that
        // a station connected to ESP8266
        break;
    case EVENT_SOFTAPMODE_STADISCONNECTED:
        dia_info_evnt(WIFI_STA_DISCONNECTED);
        INFO(MACSTR " disconnected (AID %d)",
             MAC2STR(evt->event_info.sta_disconnected.mac),
             evt->event_info.sta_disconnected.aid);
        // system_os_post(USER_TASK_PRIO_0, SIG_SOFTAPMODE_STADISCONNECTED, '0'); // informing everybody of
        // a station disconnected from ESP8266
        break;
    case EVENT_SOFTAPMODE_PROBEREQRECVED:
        // TRACE("AP probed");
        break;
    case EVENT_OPMODE_CHANGED:
        dia_info_evnt(WIFI_OPMODE_CHANGED, wifi_get_opmode());
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

void espwifi_get_ip_address(struct ip_info *t_ip)
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

void espwifi_work_as_ap(void)
{
    ALL("espwifi_work_as_ap");
    struct ip_info ap_ip;
    struct dhcps_lease dhcp_lease;
    espmem.stack_mon();

    // switch to SOFTAP otherwise wifi_softap_set_config_current won't work
    // wifi_set_opmode_current(SOFTAP_MODE);
    // now switch to STATIONAP
    wifi_set_opmode_current(STATIONAP_MODE);
    if (!wifi_softap_set_config_current(&ap_config))
    {
        dia_error_evnt(WIFI_SETAP_ERROR);
        ERROR("espwifi_work_as_ap failed to set AP cfg");
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

void espwifi_connect_to_ap(void)
{
    ALL("espwifi_connect_to_ap");
    struct station_config stationConf;

    if (os_strlen(station_ssid) == 0)
    {
        dia_error_evnt(WIFI_CONNECT_NO_SSID_AVAILABLE);
        ERROR("espwifi_connect_to_ap no ssid available");
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

bool espwifi_is_connected(void)
{
    return stamode_connected;
}

// SCAN
static void (*scan_completed_cb)(void *) = NULL;
static void *scan_completed_param = NULL;

static void fill_in_ap_list(void *arg, STATUS status)
{
    ALL("fill_in_ap_list");
    // delete previuos results
    espwifi_free_ap_list();
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
            dia_error_evnt(WIFI_AP_LIST_HEAP_EXHAUSTED, 33 * ap_count);
            ERROR("espwifi_fill_in_ap_list heap exhausted %d", 33 * ap_count);
        }
    }
    else
    {
        dia_error_evnt(WIFI_AP_LIST_CANNOT_COMPLETE_SCAN);
        ERROR("espwifi_fill_in_ap_list cannot complete ap scan");
    }
    if (scan_completed_cb)
        scan_completed_cb(scan_completed_param);
}

void espwifi_scan_for_ap(struct scan_config *config, void (*callback)(void *), void *param)
{
    scan_completed_cb = callback;
    scan_completed_param = param;
    wifi_station_scan(config, (scan_done_cb_t)fill_in_ap_list);
}

int espwifi_get_ap_count(void)
{
    return ap_count;
}

char *espwifi_get_ap_name(int t_idx)
{
    if (t_idx < ap_count)
        return (ap_list + (33 * t_idx));
    else
        return "";
}

void espwifi_free_ap_list(void)
{
    ap_count = 0;
    if (ap_list)
    {
        delete[] ap_list;
        ap_list = NULL;
    }
}

// CONFIG

void espwifi_station_set_ssid(char *t_str, int t_len)
{
    ALL("espwifi_station_set_ssid");
    os_memset(station_ssid, 0, 32);
    if (t_len > 31)
    {
        dia_warn_evnt(WIFI_TRUNCATING_STRING_TO_31_CHAR);
        WARN("espwifi_station_set_ssid: truncating ssid to 31 characters");
        os_strncpy(station_ssid, t_str, 31);
    }
    else
    {
        os_strncpy(station_ssid, t_str, t_len);
    }
}

char *espwifi_station_get_ssid(void)
{
    return station_ssid;
}

void espwifi_station_set_pwd(char *t_str, int t_len)
{
    ALL("espwifi_station_set_pwd");
    os_memset(station_pwd, 0, 64);
    if (t_len > 63)
    {
        dia_warn_evnt(WIFI_TRUNCATING_STRING_TO_63_CHAR);
        WARN("espwifi_station_set_pwd: truncating pwd to 63 characters");
        os_strncpy(station_pwd, t_str, 63);
    }
    else
    {
        os_strncpy(station_pwd, t_str, t_len);
    }
}

void espwifi_ap_set_pwd(char *t_str, int t_len)
{
    ALL("espwifi_ap_set_pwd");
    os_memset(ap_config.password, 0, 64);
    if (t_len > 63)
    {
        dia_warn_evnt(WIFI_TRUNCATING_STRING_TO_63_CHAR);
        WARN("espwifi_ap_set_pwd: truncating pwd to 63 characters");
        os_strncpy((char *)ap_config.password, t_str, 63);
    }
    else
    {
        os_strncpy((char *)ap_config.password, t_str, t_len);
    }
    // in case wifi is already in STATIONAP_MODE update config
    if (wifi_get_opmode() != STATION_MODE)
    {
        espwifi_work_as_ap();
    }
}

void espwifi_ap_set_ch(int ch)
{
    ALL("espwifi_ap_set_ch");
    if ((ch < 1) || (ch > 11))
    {
        dia_error_evnt(WIFI_AP_SET_CH_OOR, ch);
        ERROR("espwifi_ap_set_ch: %d is OOR, AP channel set to 1");
        ap_config.channel = 1;
    }
    else
    {
        ap_config.channel = ch;
    }
    // in case wifi is already in STATIONAP_MODE update config
    if (wifi_get_opmode() == STATIONAP_MODE)
    {
        espwifi_work_as_ap();
    }
}

#define WIFI_CFG_FILENAME ((char *)f_str("wifi.cfg"))

static int wifi_cfg_restore(void)
{
    ALL("espwifi_wifi_cfg_restore");
    if (!Espfile::exists(WIFI_CFG_FILENAME))
        return CFG_cantRestore;
    Cfgfile cfgfile(WIFI_CFG_FILENAME);
    if (cfgfile.getErr() != JSON_noerr)
    {
        dia_error_evnt(WIFI_CFG_RESTORE_ERROR);
        ERROR("wifi_cfg_restore error");
        return CFG_error;
    }
    espmem.stack_mon();
    os_memset(station_ssid, 0, 32);
    cfgfile.getStr(f_str("station_ssid"), station_ssid, 32);
    if (cfgfile.getErr() == JSON_notFound)
    {
        dia_info_evnt(WIFI_CFG_RESTORE_NO_SSID_FOUND);
        INFO("espwifi_wifi_cfg_restore no SSID found");
        cfgfile.clearErr();
    }
    os_memset(station_pwd, 0, 64);
    cfgfile.getStr(f_str("station_pwd"), station_pwd, 64);
    if (cfgfile.getErr() == JSON_notFound)
    {
        dia_info_evnt(WIFI_CFG_RESTORE_NO_PWD_FOUND);
        INFO("espwifi_wifi_cfg_restore no PWD found");
        cfgfile.clearErr();
    }
    int channel = cfgfile.getInt(f_str("ap_channel"));
    if (cfgfile.getErr() == JSON_notFound)
    {
        dia_info_evnt(WIFI_CFG_RESTORE_AP_CH, 1);
        INFO("espwifi_wifi_cfg_restore AP channel: %d", 1);
    }
    if (cfgfile.getErr() == JSON_noerr)
    {
        if ((channel < 1) || (channel > 11))
        {
            dia_error_evnt(WIFI_CFG_RESTORE_AP_CH_OOR, channel);
            ERROR("espwifi_wifi_cfg_restore AP channel out of range (%)", channel);
            channel = 1;
        }
        ap_config.channel = channel;
        dia_info_evnt(WIFI_CFG_RESTORE_AP_CH, channel);
        INFO("espwifi_wifi_cfg_restore AP channel: %d", channel);
    }
    char ap_password[64];
    os_memset(ap_password, 0, 64);
    cfgfile.getStr(f_str("ap_pwd"), ap_password, 64);
    if (cfgfile.getErr() == JSON_notFound)
    {
        dia_info_evnt(WIFI_CFG_RESTORE_AP_DEFAULT_PWD);
        INFO("espwifi_wifi_cfg_restore AP default password");
    }
    if (cfgfile.getErr() == JSON_noerr)
    {
        os_memset((char *)ap_config.password, 0, 64);
        os_strncpy((char *)ap_config.password, ap_password, 63);
        if (0 == os_strcmp((char *)ap_config.password, f_str("espbot123456")))
        {
            dia_info_evnt(WIFI_CFG_RESTORE_AP_DEFAULT_PWD);
            INFO("espwifi_wifi_cfg_restore AP default password");
        }
        else
        {
            dia_info_evnt(WIFI_CFG_RESTORE_AP_CUSTOM_PWD);
            INFO("espwifi_wifi_cfg_restore AP custom password: %s", ap_password);
        }
    }
    return CFG_ok;
}

static int wifi_cfg_uptodate(void)
{
    ALL("espwifi_wifi_cfg_uptodate");
    if (!Espfile::exists(WIFI_CFG_FILENAME))
    {
        return CFG_notUpdated;
    }
    Cfgfile cfgfile(WIFI_CFG_FILENAME);
    espmem.stack_mon();
    char st_ssid[32];
    cfgfile.getStr(f_str("station_ssid"), st_ssid, 32);
    char st_pwd[64];
    cfgfile.getStr(f_str("station_pwd"), st_pwd, 64);
    char ap_pwd[64];
    cfgfile.getStr(f_str("ap_pwd"), ap_pwd, 64);
    int ap_channel = cfgfile.getInt(f_str("ap_channel"));
    if (cfgfile.getErr() != JSON_noerr)
    {
        // no need to arise an error, the cfg file will be overwritten
        // dia_error_evnt(WIFI_CFG_UPTODATE_ERROR);
        // ERROR("wifi_cfg_uptodate error");
        return CFG_error;
    }
    if (os_strcmp(station_ssid, st_ssid) ||
        os_strcmp(station_pwd, st_pwd) ||
        os_strcmp((const char *)ap_config.password, ap_pwd) ||
        (ap_channel != ap_config.channel))
    {
        return CFG_notUpdated;
    }
    return CFG_ok;
}

int espwifi_cfg_save(void)
{
    ALL("espwifi_cfg_save");
    if (wifi_cfg_uptodate() == CFG_ok)
        return CFG_ok;
    Cfgfile cfgfile(WIFI_CFG_FILENAME);
    espmem.stack_mon();
    if (cfgfile.clear() != SPIFFS_OK)
        return CFG_error;

    char str[225];
    espwifi_cfg_json_stringify(str, 225);
    int res = cfgfile.n_append(str, os_strlen(str));
    if (res < SPIFFS_OK)
        return CFG_error;
    return CFG_ok;
}

char *espwifi_cfg_json_stringify(char *dest, int len)
{
    // {"station_ssid":"","station_pwd":"","ap_channel":,"ap_pwd":""}
    int msg_len = 62 + 32 + 64 + 2 + 64 + 1;
    char *msg;
    if (dest == NULL)
    {
        msg = new char[msg_len];
        if (msg == NULL)
        {
            dia_error_evnt(WIFI_CFG_STRINGIFY_HEAP_EXHAUSTED, msg_len);
            ERROR("espwifi_cfg_json_stringify heap exhausted [%d]", msg_len);
            return NULL;
        }
    }
    else
    {
        msg = dest;
        if (len < msg_len)
        {
            *msg = 0;
            return msg;
        }
    }
    fs_sprintf(msg,
               "{\"station_ssid\":\"%s\",\"station_pwd\":\"%s\",\"ap_channel\":%d,\"ap_pwd\":\"%s\"}",
               station_ssid,
               station_pwd,
               ap_config.channel,
               (char *)ap_config.password);
    return msg;
}

char *espwifi_status_json_stringify(char *dest, int len)
{
    // {"op_mode":"STATION","SSID":"","ip_address":"123.123.123.123"}
    int msg_len = 62 + 32 + 1;
    char *msg;
    if (dest == NULL)
    {
        msg = new char[msg_len];
        if (msg == NULL)
        {
            dia_error_evnt(WIFI_STATUS_STRINGIFY_HEAP_EXHAUSTED, msg_len);
            ERROR("espwifi_status_json_stringify heap exhausted [%d]", msg_len);
            return NULL;
        }
    }
    else
    {
        msg = dest;
        if (len < msg_len)
        {
            *msg = 0;
            return msg;
        }
    }
    switch (wifi_get_opmode())
    {
    case STATION_MODE:
        fs_sprintf(msg, "{\"op_mode\":\"STATION\",\"SSID\":\"%s\",", espwifi_station_get_ssid());
        break;
    case SOFTAP_MODE:
        fs_sprintf(msg, "{\"op_mode\":\"AP\",\"SSID\":\"%s\",", espbot.get_name());
        break;
    case STATIONAP_MODE:
        fs_sprintf(msg, "{\"op_mode\":\"AP\",\"SSID\":\"%s\",", espbot.get_name());
        break;
    default:
        break;
    }
    char *ptr = msg + os_strlen(msg);
    struct ip_info tmp_ip;
    espwifi_get_ip_address(&tmp_ip);
    char *ip_ptr = (char *)&tmp_ip.ip.addr;
    fs_sprintf(ptr, "\"ip_address\":\"%d.%d.%d.%d\"}", ip_ptr[0], ip_ptr[1], ip_ptr[2], ip_ptr[3]);
    return msg;
}

char *espwifi_scan_results_json_stringify(char *dest, int len)
{
    // {"AP_count":,"AP_SSIDs":["",]}
    int msg_len = 27 + (32 + 3) * espwifi_get_ap_count();
    char *msg;
    if (dest == NULL)
    {
        msg = new char[msg_len];
        if (msg == NULL)
        {
            dia_error_evnt(WIFI_SCAN_RESULTS_STRINGIFY_HEAP_EXHAUSTED, msg_len);
            ERROR("espwifi_scan_results_json_stringify heap exhausted [%d]", msg_len);
            return NULL;
        }
    }
    else
    {
        msg = dest;
        if (len < msg_len)
        {
            *msg = 0;
            return msg;
        }
    }
    char *tmp_ptr;
    fs_sprintf(msg, "{\"AP_count\":%d,\"AP_SSIDs\":[", espwifi_get_ap_count());
    for (int idx = 0; idx < espwifi_get_ap_count(); idx++)
    {
        tmp_ptr = msg + os_strlen(msg);
        if (idx > 0)
            *(tmp_ptr++) = ',';
        os_sprintf(tmp_ptr, "\"%s\"", espwifi_get_ap_name(idx));
    }
    tmp_ptr = msg + os_strlen(msg);
    fs_sprintf(tmp_ptr, "]}");
    espwifi_free_ap_list();
    espmem.stack_mon();
    return msg;
}

void espwifi_init()
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
    os_timer_setfn(&wait_before_reconnect, (os_timer_func_t *)&espwifi_connect_to_ap, NULL);
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
    wifi_cfg_restore();

    // start as SOFTAP and try to switch to STATION
    // this will ensure that softap and station configurations are set by espbot
    // otherwise default configurations by NON OS SDK are used
    espwifi_work_as_ap(); // make effective the restored configration
    // signal that SOFTAPMODE is ready
    system_os_post(USER_TASK_PRIO_0, SIG_SOFTAPMODE_READY, '0');
    if (os_strlen(station_ssid) > 0)
        espwifi_connect_to_ap();
}