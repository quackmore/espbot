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

#include "espbot_config.hpp"
#include "espbot_diagnostic.hpp"
#include "espbot_event_codes.h"
#include "espbot_global.hpp"
#include "espbot_mem_mon.hpp"
#include "espbot_ota.hpp"
#include "espbot_utils.hpp"
#include "espbot_webclient.hpp"

void Ota_upgrade::init(void)
{
    _rel_cmp_result = 0;
    if (restore_cfg() != CFG_OK)
    {
        // something went wrong while loading flash config
        set_host("0.0.0.0");
        _port = 0;
        _path = new char[2];
        os_strncpy(_path, "/", 1);
        _check_version = false;
        _reboot_on_completion = false;
        esp_diag.warn(OTA_INIT_DEFAULT_CFG);
        WARN("OTA init starting with default configuration");
    }
    _status = OTA_IDLE;
}

void Ota_upgrade::set_host(char *t_str)
{
    os_strncpy(_host_str, t_str, 15);
    atoipaddr(&_host, t_str);
}

void Ota_upgrade::set_port(char *t_str)
{
    _port = atoi(t_str);
}

void Ota_upgrade::set_path(char *t_str)
{
    if (_path)
    {
        delete[] _path;
        _path = NULL;
    }
    _path = new char[os_strlen(t_str) + 1];
    if (_path)
    {
        os_strncpy(_path, t_str, os_strlen(t_str));
    }
    else
    {
        esp_diag.error(OTA_SET_PATH_HEAP_EXHAUSTED, os_strlen(t_str));
        ERROR("OTA set_path heap exhausted %d", os_strlen(t_str));
    }
}

void Ota_upgrade::set_check_version(char *t_str)
{
    if ((os_strncmp(t_str, f_str("true"), 4) == 0) || (os_strncmp(t_str, f_str("True"), 4) == 0))
    {
        _check_version = true;
        return;
    }
    if ((os_strncmp(t_str, f_str("false"), 5) == 0) || (os_strncmp(t_str, f_str("False"), 5) == 0))
    {
        _check_version = false;
        return;
    }
    esp_diag.error(OTA_SET_CHECK_VERSION_UNKNOWN_VALUE);
    ERROR("set_check_version bad value (%s)", t_str);
}

void Ota_upgrade::set_reboot_on_completion(char *t_str)
{
    if ((os_strncmp(t_str, f_str("true"), 4) == 0) || (os_strncmp(t_str, f_str("True"), 4) == 0))
    {
        _reboot_on_completion = true;
        return;
    }
    if ((os_strncmp(t_str, f_str("false"), 5) == 0) || (os_strncmp(t_str, f_str("False"), 5) == 0))
    {
        _reboot_on_completion = false;
        return;
    }
    esp_diag.error(OTA_SET_REBOOT_ON_COMPLETION_UNKNOWN_VALUE);
    ERROR("set_reboot_on_completion bad value (%s)", t_str);
}

char *Ota_upgrade::get_host(void)
{
    return _host_str;
}

int Ota_upgrade::get_port(void)
{
    return _port;
}

char *Ota_upgrade::get_path(void)
{
    return _path;
}

char *Ota_upgrade::get_check_version(void)
{
    if (_check_version)
        return (char *)f_str("true");
    else
        return (char *)f_str("false");
}

char *Ota_upgrade::get_reboot_on_completion(void)
{
    if (_reboot_on_completion)
        return (char *)f_str("true");
    else
        return (char *)f_str("false");
}

// upgrade

// void Ota_upgrade::ota_completed_cb(void *arg)
// {
//     ALL("ota_completed_cb");
//     uint8 u_flag = system_upgrade_flag_check();
// 
//     if (u_flag == UPGRADE_FLAG_FINISH)
//     {
//         esp_ota._status = OTA_SUCCESS;
//     }
//     else
//     {
//         esp_ota._status = OTA_FAILED;
//         esp_diag.error(OTA_FAILURE);
//         ERROR("ota_completed_cb cannot complete upgrade");
//     }
// }
// 
// void Ota_upgrade::ota_timer_function(void *arg)
// {
//     char *binary_file;
//     static struct upgrade_server_info *upgrade_svr = NULL;
//     static char *url = NULL;
//     // static struct espconn *pespconn = NULL;
//     espmem.stack_mon();
// 
//     // os_printf("OTA STATUS: %d\n", esp_ota._status);
// 
//     switch (esp_ota._status)
//     {
//     case OTA_IDLE:
//     {
//         if (esp_ota._check_version)
//         {
//             esp_ota._status = OTA_VERSION_CHECKING;
//             // start web client
//         }
//         else
//             esp_ota._status = OTA_VERSION_CHECKED;
//         os_timer_arm(&esp_ota._ota_timer, 200, 0);
//         break;
//     }
//     case OTA_VERSION_CHECKING:
//     {
//         os_timer_arm(&esp_ota._ota_timer, 200, 0);
//         break;
//     }
//     case OTA_VERSION_CHECKED:
//     {
//         uint8_t userBin = system_upgrade_userbin_check();
//         switch (userBin)
//         {
//         case UPGRADE_FW_BIN1:
//             binary_file = (char *)f_str("user2.bin");
//             break;
//         case UPGRADE_FW_BIN2:
//             binary_file = (char *)f_str("user1.bin");
//             break;
//         default:
//             esp_diag.error(OTA_TIMER_FUNCTION_USERBIN_ID_UNKNOWN);
//             ERROR("OTA: bad userbin number");
//             esp_ota._status = OTA_FAILED;
//             os_timer_arm(&esp_ota._ota_timer, 200, 0);
//             return;
//         }
//         upgrade_svr = new struct upgrade_server_info;
//         url = new char[56 + 15 + 6 + os_strlen(esp_ota._path)];
//         *((uint32 *)(upgrade_svr->ip)) = esp_ota._host.addr;
//         upgrade_svr->port = esp_ota._port;
//         upgrade_svr->check_times = 60000;
//         upgrade_svr->check_cb = &Ota_upgrade::ota_completed_cb;
//         // upgrade_svr->pespconn = pespconn;
//         fs_sprintf(url,
//                    "GET %s%s HTTP/1.1\r\nHost: %s:%d\r\n"
//                    "Connection: close\r\n"
//                    "\r\n",
//                    esp_ota._path, binary_file, esp_ota._host_str, esp_ota._port);
//         TRACE("ota_timer_function url %s", url);
//         upgrade_svr->url = (uint8 *)url;
//         if (system_upgrade_start(upgrade_svr) == false)
//         {
//             esp_diag.error(OTA_CANNOT_START_UPGRADE);
//             ERROR("OTA cannot start upgrade");
//             esp_ota._status = OTA_FAILED;
//         }
//         else
//         {
//             esp_ota._status = OTA_UPGRADING;
//         }
//         os_timer_arm(&esp_ota._ota_timer, 500, 0);
//         break;
//     }
//     case OTA_UPGRADING:
//     {
//         os_timer_arm(&esp_ota._ota_timer, 500, 0);
//         break;
//     }
//     case OTA_SUCCESS:
//     {
//         if (upgrade_svr)
//         {
//             delete upgrade_svr;
//             upgrade_svr = NULL;
//         }
//         if (url)
//         {
//             delete[] url;
//             url = NULL;
//         }
//         // if (pespconn)
//         // {
//         //     esp_free(pespconn);
//         //     pespconn = NULL;
//         // }
//         esp_diag.info(OTA_SUCCESSFULLY_COMPLETED);
//         INFO("OTA successfully completed");
//         if (esp_ota._reboot_on_completion)
//         {
//             esp_diag.debug(OTA_REBOOTING_AFTER_COMPLETION);
//             DEBUG("OTA - rebooting after completion");
//             espbot.reset(ESP_OTA_REBOOT);
//         }
//         esp_ota._status = OTA_IDLE;
//         break;
//     }
//     case OTA_FAILED:
//     {
//         if (upgrade_svr)
//         {
//             delete upgrade_svr;
//             upgrade_svr = NULL;
//         }
//         if (url)
//         {
//             delete[] url;
//             url = NULL;
//         }
//         // if (pespconn)
//         // {
//         //     esp_free(pespconn);
//         //     pespconn = NULL;
//         // }
//         // ERROR("OTA failed\n");
//         esp_ota._status = OTA_IDLE;
//         break;
//     }
//     default:
//         break;
//     }
// }

// void Ota_upgrade::start_upgrade(void)
// {
//     if ((_status == OTA_IDLE) || (_status == OTA_SUCCESS) || (_status == OTA_FAILED))
//     {
//         os_timer_setfn(&_ota_timer, (os_timer_func_t *)&Ota_upgrade::ota_timer_function, NULL);
//         os_timer_arm(&_ota_timer, 200, 0);
//     }
//     else
//     {
//         esp_diag.warn(OTA_START_UPGRADE_CALLED_WHILE_OTA_IN_PROGRESS);
//         WARN("start_upgrade called while OTA in progress");
//     }
// }

Ota_status_type Ota_upgrade::get_status(void)
{
    return _status;
}

//
// NEW OTA
//

ip_addr *Ota_upgrade::get_host_ip(void)
{
    return &_host;
}

bool Ota_upgrade::check_version(void)
{
    return _check_version;
}

bool Ota_upgrade::reboot_on_completion(void)
{
    return _reboot_on_completion;
}

void Ota_upgrade::set_status(Ota_status_type val)
{
    _status = val;
}

void Ota_upgrade::set_rel_cmp_result(int val)
{
    _rel_cmp_result = val;
}

int Ota_upgrade::get_rel_cmp_result(void)
{
    return _rel_cmp_result;
}

static char *readable_ota_status(Ota_status_type status)
{
    switch (status)
    {
    case OTA_IDLE:
        return (char *)f_str("OTA_IDLE");
    case OTA_VERSION_CHECKING:
        return (char *)f_str("OTA_VERSION_CHECKING");
    case OTA_VERSION_CHECKED:
        return (char *)f_str("OTA_VERSION_CHECKED");
    case OTA_UPGRADING:
        return (char *)f_str("OTA_UPGRADING");
    case OTA_SUCCESS:
        return (char *)f_str("OTA_SUCCESS");
    case OTA_FAILED:
        return (char *)f_str("OTA_FAILED");
    case OTA_ALREADY_TO_THE_LATEST:
        return (char *)f_str("OTA_ALREADY_TO_THE_LATEST");
    default:
        return (char *)f_str("UNKNOWN");
    }
}

//
// checking binary version on the OTA web server
//

static Webclnt *ota_client = NULL;
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
    espmem.stack_mon();
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
            tmp_sep = ".";
            break;
        case 1:
        case 2:
            tmp_sep = "-";
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
    subsequent_function(ota_engine);
}

static void check_version(void *param)
{
    ALL("check_version");
    switch (ota_client->get_status())
    {
    case WEBCLNT_RESPONSE_READY:
        if (ota_client->parsed_response->body)
        {
            TRACE("check_version OTA server bin version: %s", ota_client->parsed_response->body);
            if (release_format_chk(ota_client->parsed_response->body))
            {
                esp_ota.set_rel_cmp_result(release_cmp(app_release, ota_client->parsed_response->body));
                esp_ota.set_status(OTA_VERSION_CHECKED);
            }
            else
            {
                esp_diag.error(OTA_CHECK_VERSION_BAD_FORMAT);
                ERROR("check_version bad version format");
                esp_ota.set_status(OTA_FAILED);
            }
        }
        break;
    default:
        esp_diag.error(OTA_CHECK_VERSION_UNEXPECTED_WEBCLIENT_STATUS, ota_client->get_status());
        ERROR("check_version unexpected webclient status %d", ota_client->get_status());
        esp_ota.set_status(OTA_FAILED);
        break;
    }
    ota_client->disconnect(check_for_new_release_cleanup, NULL);
}

static void ota_ask_version(void *param)
{
    ALL("ota_ask_version");
    switch (ota_client->get_status())
    {
    case WEBCLNT_CONNECTED:
    {
        // "GET version.txt HTTP/1.1rnHost: 111.222.333.444rnrn" 52 chars
        int req_len = 52 + os_strlen(esp_ota.get_path());
        Heap_chunk req(req_len);
        if (req.ref == NULL)
        {
            ERROR("%s - heap exausted [%d]", __FUNCTION__, req_len);
            esp_ota.set_status(OTA_FAILED);
            ota_client->disconnect(check_for_new_release_cleanup, NULL);
            break;
        }
        fs_sprintf(req.ref,
                   "GET %sversion.txt HTTP/1.1\r\nHost: %s\r\n\r\n",
                   esp_ota.get_path(), esp_ota.get_host());
        ota_client->send_req(req.ref, os_strlen(req.ref), check_version, NULL);
    }
    break;
    default:
        esp_diag.error(OTA_ASK_VERSION_UNEXPECTED_WEBCLIENT_STATUS, ota_client->get_status());
        ERROR("ota_ask_version unexpected webclient status %d", ota_client->get_status());
        esp_ota.set_status(OTA_FAILED);
        ota_client->disconnect(check_for_new_release_cleanup, NULL);
        break;
    }
}

static void ota_completed_cb(void *arg)
{
    ALL("ota_completed_cb");
    uint8 u_flag = system_upgrade_flag_check();

    if (u_flag == UPGRADE_FLAG_FINISH)
    {
        esp_ota.set_status(OTA_SUCCESS);
    }
    else
    {
        esp_ota.set_status(OTA_FAILED);
    }
    subsequent_function(ota_engine);
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
    TRACE("OTA status -> %s", readable_ota_status(esp_ota.get_status()));
    switch (esp_ota.get_status())
    {
    case OTA_IDLE:
    {
        if (os_strcmp(esp_ota.get_check_version(), "true") == 0)
        {
            esp_ota.set_status(OTA_VERSION_CHECKING);
            ota_client = new Webclnt;
            ota_client->connect(*esp_ota.get_host_ip(), esp_ota.get_port(), ota_ask_version, NULL);
        }
        else
        {
            // no version check required, upgrade anyway
            esp_ota.set_status(OTA_UPGRADING);
            subsequent_function(ota_engine);
        }
        break;
    }
    case OTA_VERSION_CHECKED:
    {
        if (esp_ota.get_rel_cmp_result() < 0)
        {
            // current release is lower than the available one
            esp_ota.set_status(OTA_UPGRADING);
        }
        else
        {
            esp_ota.set_status(OTA_ALREADY_TO_THE_LATEST);
        }
        subsequent_function(ota_engine);
        break;
    }
    case OTA_UPGRADING:
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
            esp_diag.error(OTA_TIMER_FUNCTION_USERBIN_ID_UNKNOWN);
            ERROR("OTA: bad userbin number");
            esp_ota.set_status(OTA_FAILED);
            subsequent_function(ota_engine);
            return;
        }
        upgrade_svr = new struct upgrade_server_info;
        if (upgrade_svr == NULL)
        {
            esp_diag.error(OTA_ENGINE_HEAP_EXHAUSTED, sizeof(upgrade_server_info));
            ERROR("OTA save_cfg heap exhausted %d", sizeof(upgrade_server_info));
            esp_ota.set_status(OTA_FAILED);
            subsequent_function(ota_engine);
            return;
        }
        // "GET  HTTP/1.1rnHost: :rnConnection: closernrn"
        int url_len = 46 + // format string
                      15 + // ip
                      5 +  // port
                      os_strlen(esp_ota.get_path()) +
                      os_strlen(binary_file);
        url = new char[url_len];
        if (url == NULL)
        {
            esp_diag.error(OTA_ENGINE_HEAP_EXHAUSTED, url_len);
            ERROR("OTA save_cfg heap exhausted %d", url_len);
            esp_ota.set_status(OTA_FAILED);
            subsequent_function(ota_engine);
            return;
        }
        *((uint32 *)(upgrade_svr->ip)) = esp_ota.get_host_ip()->addr;
        upgrade_svr->port = esp_ota.get_port();
        upgrade_svr->check_times = 60000;
        upgrade_svr->check_cb = ota_completed_cb;
        // upgrade_svr->pespconn = pespconn;
        fs_sprintf(url,
                   "GET %s%s HTTP/1.1\r\nHost: %s:%d\r\n"
                   "Connection: close\r\n"
                   "\r\n",
                   esp_ota.get_path(), binary_file, esp_ota.get_host(), esp_ota.get_port());
        TRACE("ota_engine url %s", url);
        upgrade_svr->url = (uint8 *)url;
        if (system_upgrade_start(upgrade_svr) == false)
        {
            esp_diag.error(OTA_CANNOT_START_UPGRADE);
            ERROR("OTA cannot start upgrade");
            esp_ota.set_status(OTA_FAILED);
        }
        break;
    }
    case OTA_SUCCESS:
    {
        ota_cleanup();
        esp_diag.info(OTA_SUCCESSFULLY_COMPLETED);
        INFO("OTA successfully completed");
        if (esp_ota.reboot_on_completion())
        {
            esp_diag.debug(OTA_REBOOTING_AFTER_COMPLETION);
            DEBUG("OTA - rebooting after completion");
            espbot.reset(ESP_OTA_REBOOT);
        }
        esp_ota.set_status(OTA_IDLE);
        break;
    }
    case OTA_FAILED:
    {
        ota_cleanup();
        esp_diag.error(OTA_FAILURE);
        ERROR("OTA failed");
        esp_ota.set_status(OTA_IDLE);
        break;
    }
    case OTA_ALREADY_TO_THE_LATEST:
    {
        ota_cleanup();
        esp_diag.info(OTA_UP_TO_DATE);
        INFO("OTA everything up to date");
        esp_ota.set_status(OTA_IDLE);
        break;
    }
    case OTA_VERSION_CHECKING:
    {
        break;
    }
    default:
        break;
    }
    espmem.stack_mon();
}

void Ota_upgrade::start_upgrade(void)
{
    ALL("start_upgrade");
    if ((_status == OTA_IDLE) || (_status == OTA_ALREADY_TO_THE_LATEST) || (_status == OTA_FAILED))
    {
        _status = OTA_IDLE;
        subsequent_function(ota_engine);
    }
    else
    {
        esp_diag.warn(OTA_START_UPGRADE_CALLED_WHILE_OTA_IN_PROGRESS);
        WARN("start_upgrade called while OTA in progress");
    }
}

//
// CONFIGURATION PERSISTENCE
//

int Ota_upgrade::restore_cfg(void)
{
    ALL("Ota_upgrade::restore_cfg");
    File_to_json cfgfile(f_str("ota.cfg"));
    espmem.stack_mon();
    if (cfgfile.exists())
    {
        if (cfgfile.find_string(f_str("host")))
        {
            esp_diag.error(OTA_RESTORE_CFG_INCOMPLETE);
            ERROR("OTA restore_cfg cannot find 'host'");
            return CFG_ERROR;
        }
        set_host(cfgfile.get_value());
        if (cfgfile.find_string(f_str("port")))
        {
            esp_diag.error(OTA_RESTORE_CFG_INCOMPLETE);
            ERROR("OTA restore_cfg cannot find 'port'");
            return CFG_ERROR;
        }
        set_port(cfgfile.get_value());
        if (cfgfile.find_string(f_str("path")))
        {
            esp_diag.error(OTA_RESTORE_CFG_INCOMPLETE);
            ERROR("OTA restore_cfg cannot find 'path'");
            return CFG_ERROR;
        }
        set_path(cfgfile.get_value());
        if (cfgfile.find_string(f_str("check_version")))
        {
            esp_diag.error(OTA_RESTORE_CFG_INCOMPLETE);
            ERROR("OTA restore_cfg cannot find 'check_version'");
            return CFG_ERROR;
        }
        set_check_version(cfgfile.get_value());
        if (cfgfile.find_string(f_str("reboot_on_completion")))
        {
            esp_diag.error(OTA_RESTORE_CFG_INCOMPLETE);
            ERROR("OTA restore_cfg cannot find 'reboot_on_completion'");
            return CFG_ERROR;
        }
        set_reboot_on_completion(cfgfile.get_value());
        return CFG_OK;
    }
    else
    {
        esp_diag.warn(OTA_RESTORE_CFG_FILE_NOT_FOUND);
        WARN("OTA restore_cfg file not found");
        return CFG_ERROR;
    }
}

int Ota_upgrade::saved_cfg_not_updated(void)
{
    ALL("saved_cfg_not_updated");
    File_to_json cfgfile(f_str("ota.cfg"));
    espmem.stack_mon();
    if (cfgfile.exists())
    {
        if (cfgfile.find_string(f_str("host")))
        {
            esp_diag.error(OTA_SAVED_CFG_NOT_UPDATED_INCOMPLETE);
            ERROR("OTA saved_cfg_not_updated cannot find 'host'");
            return CFG_ERROR;
        }
        if (os_strcmp(get_host(), cfgfile.get_value()))
        {
            return CFG_REQUIRES_UPDATE;
        }
        if (cfgfile.find_string(f_str("port")))
        {
            esp_diag.error(OTA_SAVED_CFG_NOT_UPDATED_INCOMPLETE);
            ERROR("OTA saved_cfg_not_updated cannot find 'port'");
            return CFG_ERROR;
        }
        if (_port != (atoi(cfgfile.get_value())))
        {
            return CFG_REQUIRES_UPDATE;
        }
        if (cfgfile.find_string(f_str("path")))
        {
            esp_diag.error(OTA_SAVED_CFG_NOT_UPDATED_INCOMPLETE);
            ERROR("OTA saved_cfg_not_updated cannot find 'path'");
            return CFG_ERROR;
        }
        if (os_strcmp(get_path(), cfgfile.get_value()))
        {
            return CFG_REQUIRES_UPDATE;
        }
        if (cfgfile.find_string(f_str("check_version")))
        {
            esp_diag.error(OTA_SAVED_CFG_NOT_UPDATED_INCOMPLETE);
            ERROR("OTA saved_cfg_not_updated cannot find 'check_version'");
            return CFG_ERROR;
        }
        if (os_strcmp(get_check_version(), cfgfile.get_value()))
        {
            return CFG_REQUIRES_UPDATE;
        }
        if (cfgfile.find_string(f_str("reboot_on_completion")))
        {
            esp_diag.error(OTA_SAVED_CFG_NOT_UPDATED_INCOMPLETE);
            ERROR("OTA saved_cfg_not_updated cannot find 'reboot_on_completion'");
            return CFG_ERROR;
        }
        if (os_strcmp(get_reboot_on_completion(), cfgfile.get_value()))
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

int Ota_upgrade::save_cfg(void)
{
    ALL("save_cfg");
    if (saved_cfg_not_updated() != CFG_REQUIRES_UPDATE)
        return CFG_OK;
    if (!espfs.is_available())
    {
        esp_diag.error(OTA_SAVE_CFG_FS_NOT_AVAILABLE);
        ERROR("OTA save_cfg file system not available");
        return CFG_ERROR;
    }
    Ffile cfgfile(&espfs, (char *)f_str("ota.cfg"));
    if (!cfgfile.is_available())
    {
        esp_diag.error(OTA_SAVE_CFG_CANNOT_OPEN_FILE);
        ERROR("OTA save_cfg cannot open file");
        return CFG_ERROR;
    }
    cfgfile.clear();
    int buffer_len = 90 +
                     16 +
                     6 +
                     os_strlen(get_path()) +
                     10;
    Heap_chunk buffer(buffer_len);
    espmem.stack_mon();
    if (buffer.ref == NULL)
    {
        esp_diag.error(OTA_SAVE_CFG_HEAP_EXHAUSTED, buffer_len);
        ERROR("OTA save_cfg heap exhausted %d", buffer_len);
        return CFG_ERROR;
    }
    // using fs_sprintf twice to keep fmt len lower that 70 chars
    fs_sprintf(buffer.ref,
               "{\"host\":\"%s\",\"port\":%d,\"path\":\"%s\",",
               get_host(),
               get_port(),
               get_path());
    fs_sprintf((buffer.ref + os_strlen(buffer.ref)),
               "\"check_version\":\"%s\",\"reboot_on_completion\":\"%s\"}",
               get_check_version(),
               get_reboot_on_completion());
    cfgfile.n_append(buffer.ref, os_strlen(buffer.ref));
    return CFG_OK;
}

/*
//
// binary download
//

// COMPLETE ME:
// bin size
// bin downloaded
// bin starting
// bin ending
// flash partition offset
static char *bin_name = "user1.bin";
static int32 remote_bin_start;
static int32 remote_bin_range = 256;
static int32 bin_start_addr;
static int32 bin_size;

static void download_cleanup(void *param)
{
    esplog.all("%s\n", __FUNCTION__);
    if (ota_client)
        delete ota_client;
    if (ota_request)
        delete[] ota_request;
    subsequent_function(ota_engine);
}

static os_timer_t wait_and_reconnect_timer;

static void ota_wait_and_reconnect(void *param)
{
    system_os_post(USER_TASK_PRIO_0, SIG_OTA_RECONNECT_DOWNLOAD, '0');
}

static void check_bin(void *param)
{
    esplog.all("%s\n", __FUNCTION__);
    switch (ota_client->get_status())
    {
    case WEBCLNT_RESPONSE_READY:
        if (ota_client->parsed_response->content_range_start == remote_bin_start)
        {
            os_printf("     bin range: %d-%d\n", remote_bin_start, (remote_bin_start + remote_bin_range - 1));
            os_printf("min stack addr: %X\n", espmem.get_min_stack_addr());
            os_printf("     free heap: %X\n", system_get_free_heap_size());
            // os_printf("bin range: ");
            // int idx;
            // for (int idx = 0; idx < remote_bin_range; idx++)
            //     os_printf(" %X", ota_client->parsed_response->body[idx]);
            // os_printf("\n");
            // check if download was completed
            if ((remote_bin_start + remote_bin_range) >= bin_size)
            {
                os_printf("%s: OTA download completed\n", __FUNCTION__);
                ota_client->disconnect(download_cleanup, NULL);
                // verify checksum and set OTA status
                esp_ota.set_status(OTA_SUCCESS);
                subsequent_function(ota_engine);
            }
            else
            {
                // DEBUG
                // static bool only_one_time = false;
                // if ((remote_bin_start > 1000) && only_one_time)
                // {
                //     ota_client->disconnect(NULL, NULL);
                //     os_printf("-------- Debug disconnection --------\n");
                //     os_printf("-------- Debug disconnection --------\n");
                //     os_printf("-------- Debug disconnection --------\n");
                //     os_printf("-------- Debug disconnection --------\n");
                //     os_printf("-------- Debug disconnection --------\n");
                //     os_printf("-------- Debug disconnection --------\n");
                //     only_one_time = true;
                //     system_os_post(USER_TASK_PRIO_0, SIG_OTA_CONTINUE_DOWNLOAD, '0');
                //     return;
                // }

                remote_bin_start += remote_bin_range;
                if ((remote_bin_start + remote_bin_range) >= bin_size)
                    remote_bin_range = bin_size - remote_bin_start;
                system_os_post(USER_TASK_PRIO_0, SIG_OTA_CONTINUE_DOWNLOAD, '0');
            }
        }
        else
        {
            esplog.error("%s: received unexpected binary range [%d]\n",
                         __FUNCTION__,
                         ota_client->parsed_response->content_range_start);
            esp_ota.set_status(OTA_FAILED);
            ota_client->disconnect(download_cleanup, NULL);
        }
        break;
    case WEBCLNT_DISCONNECTED:
        esplog.error("%s: Client disconnected while downloading, retrying connection\n", __FUNCTION__);
        os_timer_setfn(&wait_and_reconnect_timer, (os_timer_func_t *)ota_wait_and_reconnect, NULL);
        os_timer_arm(&wait_and_reconnect_timer, 1000, 0);

        // ota_client->connect(esp_ota._host, esp_ota._port, ota_continue_download, NULL);
        break;
    case WEBCLNT_RESPONSE_TIMEOUT:
        esplog.error("%s: Response timeout while downloading, resending request\n", __FUNCTION__);
        system_os_post(USER_TASK_PRIO_0, SIG_OTA_CONTINUE_DOWNLOAD, '0');
        break;
    default:
        esplog.error("%s: OTA failed downloading binary: webclient status is %d\n",
                     __FUNCTION__,
                     ota_client->get_status());
        esp_ota.set_status(OTA_FAILED);
        ota_client->disconnect(download_cleanup, NULL);
        break;
    }
}

static void ota_request_bin(void *param)
{
    esplog.all("%s\n", __FUNCTION__);
    switch (ota_client->get_status())
    {
    case WEBCLNT_CONNECTED:
    case WEBCLNT_RESPONSE_READY:
    case WEBCLNT_RESPONSE_TIMEOUT:
    {
        // COMPLETE ME: integrate with additional fields
        // int ota_request_len = 76 +
        //                       os_strlen(esp_ota.get_path()) +
        //                       os_strlen(bin_name) +
        //                       7 + 7 + 15 + 1;
        // delete[] ota_request;
        // ota_request = new char[ota_request_len];
        // if (ota_request == NULL)
        // {
        //     esplog.error("%s - heap exausted [%d]\n", __FUNCTION__, ota_request_len);
        //     esp_ota.set_status(OTA_FAILED);
        //     ota_client->disconnect(download_cleanup, NULL);
        //     break;
        // }
        os_sprintf(ota_request,
                   "GET %s%s HTTP/1.1\r\n"
                   "Range: bytes=%d-%d\r\n"
                   "Host: %s\r\n"
                   "Connection: keep-alive\r\n\r\n",
                   esp_ota.get_path(),
                   bin_name,
                   remote_bin_start,
                   (remote_bin_start + remote_bin_range - 1),
                   esp_ota.get_host());
        ota_client->send_req(ota_request, check_bin, NULL);
        break;
    }
    case WEBCLNT_DISCONNECTED:
        esplog.error("%s: Client disconnected while downloading\n", __FUNCTION__);
        // system_os_post(USER_TASK_PRIO_0, SIG_OTA_RECONNECT_DOWNLOAD, '0');
        // ota_client->connect(esp_ota._host, esp_ota._port, ota_continue_download, NULL);
        break;
    default:
        esplog.error("%s: OTA requesting binary failed: webclient status is %d\n",
                     __FUNCTION__,
                     ota_client->get_status());
        esp_ota.set_status(OTA_FAILED);
        ota_client->disconnect(download_cleanup, NULL);
        break;
    }
}

void ota_continue_download(void *param)
{
    // system_soft_wdt_feed();
    // delete[] ota_request;
    ota_request_bin(NULL);
}

void ota_reconnect_download(void *param)
{
    static int counter = 0;
    counter++;
    os_printf("-------> reconnect for download [%d]\n", counter);
    ota_client->connect(esp_ota._host, esp_ota._port, ota_continue_download, NULL);
}

static void check_bin_length(void *param)
{
    esplog.all("%s\n", __FUNCTION__);
    switch (ota_client->get_status())
    {
    case WEBCLNT_RESPONSE_READY:
        bin_size = ota_client->parsed_response->content_range_size;
        remote_bin_start = 0;
        esplog.trace("%s: binary size = %d\n", __FUNCTION__, bin_size);
        // COMPLETE ME: erase flash
        delete[] ota_request;
        system_os_post(USER_TASK_PRIO_0, SIG_OTA_CONTINUE_DOWNLOAD, '0');
        break;
    default:
        esplog.error("%s: OTA failed requesting binary length: webclient status is %d\n",
                     __FUNCTION__,
                     ota_client->get_status());
        esp_ota.set_status(OTA_FAILED);
        ota_client->disconnect(download_cleanup, NULL);
        break;
    }
}

static void ota_request_bin_length(void *param)
{
    esplog.all("%s\n", __FUNCTION__);
    switch (ota_client->get_status())
    {
    case WEBCLNT_CONNECTED:
    {
        // int ota_request_len = 75 + os_strlen(esp_ota.get_path()) + os_strlen(bin_name) + 15 + 1;
        // ota_request = new char[ota_request_len];
        // if (ota_request == NULL)
        // {
        //     esplog.error("%s - heap exausted [%d]\n", __FUNCTION__, ota_request_len);
        //     esp_ota.set_status(OTA_FAILED);
        //     ota_client->disconnect(download_cleanup, NULL);
        //     break;
        // }
        // "HEAD %s%s HTTP/1.1\r\nHost: %s\r\n\r\n",
        os_sprintf(ota_request,
                   "GET %s%s HTTP/1.1\r\n"
                   "Range: bytes=0-1\r\n"
                   "Host: %sr\r\n"
                   "Connection: keep-alive\r\n\r\n",
                   esp_ota.get_path(), bin_name, esp_ota.get_host());
        ota_client->send_req(ota_request, check_bin_length, NULL);
        break;
    }
    default:
        esplog.error("%s: failed connecting to the server: webclient status is %d\n",
                     __FUNCTION__,
                     ota_client->get_status());
        esp_ota.set_status(OTA_FAILED);
        ota_client->disconnect(download_cleanup, NULL);
        break;
    }
}

static void ota_cleanup(void)
{
    esplog.all("%s\n", __FUNCTION__);
    os_printf("%s: delete ota_client\n", __FUNCTION__);
    delete ota_client;
    if (ota_request)
        delete[] ota_request;
}
*/