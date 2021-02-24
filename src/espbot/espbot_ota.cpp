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
#include "ip_addr.h"
#include "mem.h"
#include "osapi.h"
#include "upgrade.h"
#include "user_interface.h"
}

#include "espbot.hpp"
#include "espbot_cfgfile.hpp"
#include "espbot_diagnostic.hpp"
#include "espbot_event_codes.h"
#include "espbot_mem_mon.hpp"
#include "espbot_ota.hpp"
#include "espbot_utils.hpp"
#include "espbot_http_client.hpp"

static struct
{
    Ota_status_type status;
    Ota_status_type last_result;
    int rel_cmp_result;
    void (*cb_on_completion)(void *);
    void *cb_param;
} ota_state;

static struct
{
    char host_str[16];
    ip_addr host;
    unsigned int port;
    char path[128];
    bool check_version;
    bool reboot_on_completion;
} ota_cfg;

void ota_set_host(char *t_str)
{
    os_memset(ota_cfg.host_str, 0, 16);
    os_strncpy(ota_cfg.host_str, t_str, 15);
    atoipaddr(&ota_cfg.host, t_str);
}

void ota_set_port(unsigned int val)
{
    ota_cfg.port = val;
}

void ota_set_path(char *t_str)
{
    if (os_strlen(t_str) > 127)
    {
        dia_warn_evnt(OTA_PATH_TRUNCATED, 127);
        WARN("OTA set_path path truncated to 127 chars");
    }
    os_memset(ota_cfg.path, 0, 128);
    os_strncpy(ota_cfg.path, t_str, 127);
}

void ota_set_check_version(bool enabled)
{
    if (enabled)
        ota_cfg.check_version = true;
    else
        ota_cfg.check_version = false;
}

void ota_set_reboot_on_completion(bool enabled)
{
    if (enabled)
        ota_cfg.reboot_on_completion = true;
    else
        ota_cfg.reboot_on_completion = false;
}

Ota_status_type ota_get_status(void)
{
    return ota_state.status;
}

Ota_status_type ota_get_last_result(void)
{
    return ota_state.last_result;
}

void ota_set_cb_on_completion(void (*fun)(void *))
{
    ota_state.cb_on_completion = fun;
}

void ota_set_cb_param(void *param)
{
    ota_state.cb_param = param;
}

static char *readable_ota_status(Ota_status_type status)
{
    switch (status)
    {
    case OTA_idle:
        return (char *)f_str("OTA_idle");
    case OTA_version_checking:
        return (char *)f_str("OTA_version_checking");
    case OTA_version_checked:
        return (char *)f_str("OTA_version_checked");
    case OTA_upgrading:
        return (char *)f_str("OTA_upgrading");
    case OTA_success:
        return (char *)f_str("OTA_success");
    case OTA_failed:
        return (char *)f_str("OTA_failed");
    case OTA_already_to_the_lastest:
        return (char *)f_str("OTA_already_to_the_lastest");
    default:
        return (char *)f_str("UNKNOWN");
    }
}

//
// checking binary version on the OTA http server
//

static Http_clt *ota_client = NULL;
static struct upgrade_server_info *upgrade_svr = NULL;
static char *url = NULL;
extern char *app_release;

static void ota_engine(void);

static bool release_format_chk(char *avail_rel)
{
    if (avail_rel[0] == 'v')
        return true;
    else
        return false;
}

static int release_cmp(char *cur_rel, char *avail_rel)
{
    ALL("release_cmp");
    // v1.0-5-g3ef3ca3 -> 1.0-5
    char *cur_ptr = cur_rel;
    char *avail_ptr = avail_rel;
    char *tmp_sep;
    char *tmp_ptr;
    int tmp_len;
    int idx, result;
    char cur_str[10];
    char avail_str[10];
    mem_mon_stack();
    // discard v
    cur_ptr++;
    avail_ptr++;

    TRACE("OTA   current v. %s", cur_rel);
    TRACE("OTA available v. %s", avail_rel);

    for (idx = 0; idx < 3; idx++)
    {
        // initialize substrings
        os_memset(cur_str, 0, 10);
        os_memset(avail_str, 0, 10);
        switch (idx)
        {
        case 0:
            tmp_sep = (char *)f_str(".");
            break;
        case 1:
        case 2:
            tmp_sep = (char *)f_str("-");
            break;
        default:
            break;
        }
        // find substrings into current release
        tmp_ptr = os_strstr(cur_ptr, tmp_sep);
        if (tmp_ptr == NULL)
            return 1; // cannot find separator, return (cur_release > avail_release)
        tmp_len = tmp_ptr - cur_ptr;
        os_strncpy(cur_str, cur_ptr, tmp_len);
        TRACE("OTA   current v. (sub) %s", cur_str);
        cur_ptr = tmp_ptr + 1;
        // find substrings into available release
        tmp_ptr = os_strstr(avail_ptr, tmp_sep);
        if (tmp_ptr == NULL)
            return 1; // cannot find separator, return (cur_release > avail_release)
        tmp_len = tmp_ptr - avail_ptr;
        if (tmp_len > 9)
            return 1; // bad format, return (cur_release > avail_release)
        os_strncpy(avail_str, avail_ptr, tmp_len);
        TRACE("OTA available v. (sub) %s", avail_str);
        avail_ptr = tmp_ptr + 1;
        // compare substrings
        result = os_strcmp(cur_str, avail_str);
        mem_mon_stack();
        if (result == 0)
            continue;
        else
            return result;
    }
    return 0;
}

static void check_for_new_release_cleanup(void *param)
{
    ALL("check_for_new_release_cleanup");
    if (ota_client)
    {
        delete ota_client;
        ota_client = NULL;
    }
    next_function(ota_engine);
}

static void check_version(void *param)
{
    ALL("check_version");
    switch (ota_client->get_status())
    {
    case HTTP_CLT_RESPONSE_READY:
        if (ota_client->parsed_response->body)
        {
            TRACE("check_version OTA server bin version: %s", ota_client->parsed_response->body);
            if (release_format_chk(ota_client->parsed_response->body))
            {
                ota_state.rel_cmp_result = release_cmp(app_release, ota_client->parsed_response->body);
                ota_state.status = OTA_version_checked;
            }
            else
            {
                dia_error_evnt(OTA_CHECK_VERSION_BAD_FORMAT);
                ERROR("check_version bad version format");
                ota_state.status = OTA_failed;
            }
        }
        else
        {
            dia_error_evnt(OTA_CHECK_VERSION_EMPTY_RES);
            ERROR("check_version empty response");
            ota_state.status = OTA_failed;
        }
        break;
    default:
        dia_error_evnt(OTA_CHECK_VERSION_UNEXPECTED_WEBCLIENT_STATUS, ota_client->get_status());
        ERROR("check_version unexpected http client status %d", ota_client->get_status());
        ota_state.status = OTA_failed;
        break;
    }
    ota_client->disconnect(check_for_new_release_cleanup, NULL);
    mem_mon_stack();
}

static void ota_ask_version(void *param)
{
    ALL("ota_ask_version");
    switch (ota_client->get_status())
    {
    case HTTP_CLT_CONNECTED:
    {
        // "GET version.txt HTTP/1.1rnHost: 111.222.333.444rnrn" 52 chars
        int req_len = 52 + os_strlen(ota_cfg.path);
        Heap_chunk req(req_len);
        if (req.ref == NULL)
        {
            ERROR("ota_ask_version - heap exausted [%d]", req_len);
            ota_state.status = OTA_failed;
            ota_client->disconnect(check_for_new_release_cleanup, NULL);
            break;
        }
        fs_sprintf(req.ref,
                   "GET %sversion.txt HTTP/1.1\r\nHost: %s\r\n\r\n",
                   ota_cfg.path, ota_cfg.host_str);
        ota_client->send_req(req.ref, os_strlen(req.ref), check_version, NULL);
    }
    break;
    default:
        dia_error_evnt(OTA_ASK_VERSION_UNEXPECTED_WEBCLIENT_STATUS, ota_client->get_status());
        ERROR("ota_ask_version unexpected http client status %d", ota_client->get_status());
        ota_state.status = OTA_failed;
        ota_client->disconnect(check_for_new_release_cleanup, NULL);
        break;
    }
    mem_mon_stack();
}

static void ota_completed_cb(void *arg)
{
    ALL("ota_completed_cb");
    uint8 u_flag = system_upgrade_flag_check();

    if (u_flag == UPGRADE_FLAG_FINISH)
    {
        ota_state.status = OTA_success;
    }
    else
    {
        ota_state.status = OTA_failed;
    }
    next_function(ota_engine);
    mem_mon_stack();
}

static void ota_cleanup(void)
{
    if (upgrade_svr)
    {
        delete upgrade_svr;
        upgrade_svr = NULL;
    }
    if (url)
    {
        delete[] url;
        url = NULL;
    }
}

static void ota_engine(void)
{
    TRACE("OTA status -> %s", readable_ota_status(ota_state.status));
    switch (ota_state.status)
    {
    case OTA_idle:
    {
        ota_state.last_result = OTA_idle;
        if (ota_cfg.check_version)
        {
            ota_state.status = OTA_version_checking;
            ota_client = new Http_clt;
            ota_client->connect(ota_cfg.host, ota_cfg.port, ota_ask_version, NULL, 6000);
        }
        else
        {
            // no version check required, upgrade anyway
            ota_state.status = OTA_upgrading;
            next_function(ota_engine);
        }
        break;
    }
    case OTA_version_checked:
    {
        if (ota_state.rel_cmp_result < 0)
        {
            // current release is lower than the available one
            ota_state.status = OTA_upgrading;
        }
        else
        {
            ota_state.status = OTA_already_to_the_lastest;
        }
        next_function(ota_engine);
        break;
    }
    case OTA_upgrading:
    {
        char *binary_file;
        switch (system_upgrade_userbin_check())
        {
        case UPGRADE_FW_BIN1:
            binary_file = (char *)f_str("user2.bin");
            break;
        case UPGRADE_FW_BIN2:
            binary_file = (char *)f_str("user1.bin");
            break;
        default:
            dia_error_evnt(OTA_TIMER_FUNCTION_USERBIN_ID_UNKNOWN);
            ERROR("OTA: bad userbin number");
            ota_state.status = OTA_failed;
            next_function(ota_engine);
            return;
        }
        upgrade_svr = new struct upgrade_server_info;
        if (upgrade_svr == NULL)
        {
            dia_error_evnt(OTA_ENGINE_HEAP_EXHAUSTED, sizeof(upgrade_server_info));
            ERROR("ota_engine heap exhausted %d", sizeof(upgrade_server_info));
            ota_state.status = OTA_failed;
            next_function(ota_engine);
            return;
        }
        // "GET  HTTP/1.1rnHost: :rnConnection: closernrn"
        int url_len = 46 + // format string
                      15 + // ip
                      5 +  // port
                      os_strlen(ota_cfg.path) +
                      os_strlen(binary_file);
        url = new char[url_len];
        if (url == NULL)
        {
            dia_error_evnt(OTA_ENGINE_HEAP_EXHAUSTED, url_len);
            ERROR("OTA save_cfg heap exhausted %d", url_len);
            ota_state.status = OTA_failed;
            next_function(ota_engine);
            return;
        }
        *((uint32 *)(upgrade_svr->ip)) = ota_cfg.host.addr;
        upgrade_svr->port = ota_cfg.port;
        upgrade_svr->check_times = 10000;
        upgrade_svr->check_cb = ota_completed_cb;
        // upgrade_svr->pespconn = pespconn;
        fs_sprintf(url,
                   "GET %s%s HTTP/1.1\r\nHost: %s:%d\r\n"
                   "Connection: close\r\n"
                   "\r\n",
                   ota_cfg.path, binary_file, ota_cfg.host_str, ota_cfg.port);
        TRACE("ota_engine url %s", url);
        upgrade_svr->url = (uint8 *)url;
        if (system_upgrade_start(upgrade_svr) == false)
        {
            dia_error_evnt(OTA_CANNOT_START_UPGRADE);
            ERROR("OTA cannot start upgrade");
            ota_state.status = OTA_failed;
        }
        break;
    }
    case OTA_success:
    {
        ota_state.last_result = OTA_success;
        if (ota_state.cb_on_completion)
            ota_state.cb_on_completion(ota_state.cb_param);
        ota_cleanup();
        dia_info_evnt(OTA_SUCCESSFULLY_COMPLETED);
        INFO("OTA successfully completed");
        if (ota_cfg.reboot_on_completion)
        {
            dia_debug_evnt(OTA_REBOOTING_AFTER_COMPLETION);
            DEBUG("OTA - rebooting after completion");
            espbot_reset(ESPBOT_rebootAfterOta);
        }
        ota_state.status = OTA_idle;
        break;
    }
    case OTA_failed:
    {
        ota_state.last_result = OTA_failed;
        if (ota_state.cb_on_completion)
            ota_state.cb_on_completion(ota_state.cb_param);
        ota_cleanup();
        dia_error_evnt(OTA_FAILURE);
        ERROR("OTA failed");
        ota_state.status = OTA_idle;
        break;
    }
    case OTA_already_to_the_lastest:
    {
        ota_state.last_result = OTA_already_to_the_lastest;
        if (ota_state.cb_on_completion)
            ota_state.cb_on_completion(ota_state.cb_param);
        ota_cleanup();
        dia_info_evnt(OTA_UP_TO_DATE);
        INFO("OTA everything up to date");
        ota_state.status = OTA_idle;
        break;
    }
    case OTA_version_checking:
    {
        break;
    }
    default:
        break;
    }
    mem_mon_stack();
}

void ota_start(void)
{
    ALL("start_upgrade");
    if ((ota_state.status == OTA_idle) ||
        (ota_state.status == OTA_already_to_the_lastest) ||
        (ota_state.status == OTA_failed))
    {
        ota_state.status = OTA_idle;
        next_function(ota_engine);
    }
    else
    {
        dia_warn_evnt(OTA_START_UPGRADE_CALLED_WHILE_OTA_IN_PROGRESS);
        WARN("start_upgrade called while OTA in progress");
    }
}

//
// CONFIGURATION PERSISTENCE
//

#define OTA_FILENAME ((char *)f_str("ota.cfg"))

static int ota_restore_cfg(void)
{
    ALL("ota_restore_cfg");

    if (!Espfile::exists(OTA_FILENAME))
        return CFG_cantRestore;
    Cfgfile cfgfile(OTA_FILENAME);
    char host_str[16];
    os_memset(host_str, 0, 16);
    cfgfile.getStr(f_str("host"), host_str, 16);
    int port = cfgfile.getInt(f_str("port"));
    char path[128];
    os_memset(path, 0, 128);
    cfgfile.getStr(f_str("path"), path, 128);
    int check_version = cfgfile.getInt(f_str("check_version"));
    int reboot_on_completion = cfgfile.getInt(f_str("reboot_on_completion"));
    mem_mon_stack();
    if (cfgfile.getErr() != JSON_noerr)
    {
        dia_error_evnt(OTA_RESTORE_CFG_ERROR);
        ERROR("ota_restore_cfg error");
        mem_mon_stack();
        return CFG_error;
    }
    ota_set_host(host_str);
    ota_cfg.port = port;
    ota_set_path(path);
    ota_cfg.check_version = (bool)check_version;
    ota_cfg.reboot_on_completion = (bool)reboot_on_completion;
    return CFG_ok;
}

static int ota_saved_cfg_updated(void)
{
    ALL("ota_saved_cfg_updated");
    if (!Espfile::exists(OTA_FILENAME))
    {
        return CFG_notUpdated;
    }
    Cfgfile cfgfile(OTA_FILENAME);
    char host_str[16];
    os_memset(host_str, 0, 16);
    cfgfile.getStr(f_str("host"), host_str, 16);
    int port = cfgfile.getInt(f_str("port"));
    char path[128];
    os_memset(path, 0, 128);
    cfgfile.getStr(f_str("path"), path, 128);
    int check_version = cfgfile.getInt(f_str("check_version"));
    int reboot_on_completion = cfgfile.getInt(f_str("reboot_on_completion"));
    mem_mon_stack();
    if (cfgfile.getErr() != JSON_noerr)
    {
        // no need to raise an error, the cfg file will be overwritten
        // dia_error_evnt(OTA_SAVED_CFG_UPDATED_ERROR);
        // ERROR("ota_saved_cfg_updated error");
        return CFG_error;
    }
    if ((os_strcmp(ota_cfg.host_str, host_str) != 0) ||
        (ota_cfg.port != port) ||
        (os_strcmp(ota_cfg.path, path) != 0) ||
        (ota_cfg.check_version != (bool)check_version) ||
        (ota_cfg.reboot_on_completion != (bool)reboot_on_completion))
    {
        return CFG_notUpdated;
    }
    return CFG_ok;
}

char *ota_cfg_json_stringify(char *dest, int len)
{
    // {"host":"192.168.1.201","port":20090,"path":"","check_version":0,"reboot_on_completion":1}
    // int msg_len = 90 + 128 + 1;
    int msg_len = 90 + os_strlen(ota_cfg.path) + 1;
    char *msg;
    if (dest == NULL)
    {
        msg = new char[msg_len];
        if (msg == NULL)
        {
            dia_error_evnt(OTA_CFG_STRINGIFY_HEAP_EXHAUSTED, msg_len);
            ERROR("ota_cfg_json_stringify heap exhausted [%d]", msg_len);
            return NULL;
        }
    }
    else
    {
        msg = dest;
        if (len < msg_len)
        {
            msg[0] = 0;
            return msg;
        }
    }
    fs_sprintf(msg,
               "{\"host\":\"%s\",\"port\":%d,\"path\":\"%s\",",
               ota_cfg.host_str,
               ota_cfg.port,
               ota_cfg.path);
    fs_sprintf((msg + os_strlen(msg)),
               "\"check_version\":%d,\"reboot_on_completion\":%d}",
               ota_cfg.check_version,
               ota_cfg.reboot_on_completion);
    mem_mon_stack();
    return msg;
}

int ota_cfg_save(void)
{
    ALL("ota_cfg_save");
    if (ota_saved_cfg_updated() == CFG_ok)
        return CFG_ok;
    Cfgfile cfgfile(OTA_FILENAME);
    if (cfgfile.clear() != SPIFFS_OK)
        return CFG_error;
    char str[(90 + 128 + 1)];
    ota_cfg_json_stringify(str, (90 + 128 + 1));
    int res = cfgfile.n_append(str, os_strlen(str));
    mem_mon_stack();
    if (res < SPIFFS_OK)
        return CFG_error;
    return CFG_ok;
}

void ota_init(void)
{
    ota_state.rel_cmp_result = 0;
    if (ota_restore_cfg() != CFG_ok)
    {
        // something went wrong while loading flash config
        ota_set_host("0.0.0.0");
        ota_cfg.port = 0;
        ota_set_path((char *)f_str("/"));
        ota_cfg.check_version = false;
        ota_cfg.reboot_on_completion = false;
        dia_warn_evnt(OTA_INIT_DEFAULT_CFG);
        WARN("OTA init starting with default configuration");
    }
    ota_state.status = OTA_idle;
    ota_state.last_result = OTA_idle;
    ota_state.cb_on_completion = NULL;
    ota_state.cb_param = NULL;
    mem_mon_stack();
}