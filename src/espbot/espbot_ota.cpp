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
        os_strncpy(_path, f_str("/"), 1);
        _check_version = false;
        _reboot_on_completion = false;
        dia_warn_evnt(OTA_INIT_DEFAULT_CFG);
        WARN("OTA init starting with default configuration");
    }
    _status = OTA_IDLE;
    _last_result = OTA_IDLE;
    _cb_on_completion = NULL;
    _cb_param = NULL;
}

void Ota_upgrade::set_host(char *t_str)
{
    os_strncpy(_host_str, t_str, 15);
    atoipaddr(&_host, t_str);
}

void Ota_upgrade::set_port(unsigned int val)
{
    _port = val;
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
        dia_error_evnt(OTA_SET_PATH_HEAP_EXHAUSTED, os_strlen(t_str));
        ERROR("OTA set_path heap exhausted %d", os_strlen(t_str));
    }
}

void Ota_upgrade::set_check_version(bool enabled)
{
    if (enabled)
        _check_version = true;
    else
        _check_version = false;
}

void Ota_upgrade::set_reboot_on_completion(bool enabled)
{
    if (enabled)
        _reboot_on_completion = true;
    else
        _reboot_on_completion = false;
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

bool Ota_upgrade::get_check_version(void)
{
    return _check_version;
}

bool Ota_upgrade::get_reboot_on_completion(void)
{
    return _reboot_on_completion;
}

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

Ota_status_type Ota_upgrade::get_status(void)
{
    return _status;
}

void Ota_upgrade::set_status(Ota_status_type val)
{
    _status = val;
}

Ota_status_type Ota_upgrade::get_last_result(void)
{
    return _last_result;
}

void Ota_upgrade::set_last_result(Ota_status_type val)
{
    _last_result = val;
}

void Ota_upgrade::cb_on_completion(void)
{
    if (_cb_on_completion)
        _cb_on_completion(_cb_param);
}

void Ota_upgrade::set_cb_on_completion(void (*fun)(void *))
{
    _cb_on_completion = fun;
}
void Ota_upgrade::set_cb_param(void *param)
{
    _cb_param = param;
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
                dia_error_evnt(OTA_CHECK_VERSION_BAD_FORMAT);
                ERROR("check_version bad version format");
                esp_ota.set_status(OTA_FAILED);
            }
        }
        else
        {
            dia_error_evnt(OTA_CHECK_VERSION_EMPTY_RES);
            ERROR("check_version empty response");
            esp_ota.set_status(OTA_FAILED);
        }
        break;
    default:
        dia_error_evnt(OTA_CHECK_VERSION_UNEXPECTED_WEBCLIENT_STATUS, ota_client->get_status());
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
            ERROR("ota_ask_version - heap exausted [%d]", req_len);
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
        dia_error_evnt(OTA_ASK_VERSION_UNEXPECTED_WEBCLIENT_STATUS, ota_client->get_status());
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
    next_function(ota_engine);
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
        esp_ota.set_last_result(OTA_IDLE);
        if (esp_ota.get_check_version())
        {
            esp_ota.set_status(OTA_VERSION_CHECKING);
            ota_client = new Webclnt;
            ota_client->connect(*esp_ota.get_host_ip(), esp_ota.get_port(), ota_ask_version, NULL, 6000);
        }
        else
        {
            // no version check required, upgrade anyway
            esp_ota.set_status(OTA_UPGRADING);
            next_function(ota_engine);
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
        next_function(ota_engine);
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
            dia_error_evnt(OTA_TIMER_FUNCTION_USERBIN_ID_UNKNOWN);
            ERROR("OTA: bad userbin number");
            esp_ota.set_status(OTA_FAILED);
            next_function(ota_engine);
            return;
        }
        upgrade_svr = new struct upgrade_server_info;
        if (upgrade_svr == NULL)
        {
            dia_error_evnt(OTA_ENGINE_HEAP_EXHAUSTED, sizeof(upgrade_server_info));
            ERROR("ota_engine heap exhausted %d", sizeof(upgrade_server_info));
            esp_ota.set_status(OTA_FAILED);
            next_function(ota_engine);
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
            dia_error_evnt(OTA_ENGINE_HEAP_EXHAUSTED, url_len);
            ERROR("OTA save_cfg heap exhausted %d", url_len);
            esp_ota.set_status(OTA_FAILED);
            next_function(ota_engine);
            return;
        }
        *((uint32 *)(upgrade_svr->ip)) = esp_ota.get_host_ip()->addr;
        upgrade_svr->port = esp_ota.get_port();
        upgrade_svr->check_times = 10000;
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
            dia_error_evnt(OTA_CANNOT_START_UPGRADE);
            ERROR("OTA cannot start upgrade");
            esp_ota.set_status(OTA_FAILED);
        }
        break;
    }
    case OTA_SUCCESS:
    {
        esp_ota.set_last_result(OTA_SUCCESS);
        esp_ota.cb_on_completion();
        ota_cleanup();
        dia_info_evnt(OTA_SUCCESSFULLY_COMPLETED);
        INFO("OTA successfully completed");
        if (esp_ota.reboot_on_completion())
        {
            dia_debug_evnt(OTA_REBOOTING_AFTER_COMPLETION);
            DEBUG("OTA - rebooting after completion");
            espbot_reset(ESPBOT_rebootAfterOta);
        }
        esp_ota.set_status(OTA_IDLE);
        break;
    }
    case OTA_FAILED:
    {
        esp_ota.set_last_result(OTA_FAILED);
        esp_ota.cb_on_completion();
        ota_cleanup();
        dia_error_evnt(OTA_FAILURE);
        ERROR("OTA failed");
        esp_ota.set_status(OTA_IDLE);
        break;
    }
    case OTA_ALREADY_TO_THE_LATEST:
    {
        esp_ota.set_last_result(OTA_ALREADY_TO_THE_LATEST);
        esp_ota.cb_on_completion();
        ota_cleanup();
        dia_info_evnt(OTA_UP_TO_DATE);
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

int Ota_upgrade::restore_cfg(void)
{
    set_host((char *)f_str("192.168.1.201"));
    set_port(20090);
    set_path((char *)f_str("/"));
    set_check_version(false);
    set_reboot_on_completion(true);
    return CFG_OK;
    //    ALL("Ota_upgrade::restore_cfg");
    //    File_to_json cfgfile(f_str("ota.cfg"));
    //    espmem.stack_mon();
    //    if (cfgfile.exists())
    //    {
    //        if (cfgfile.find_string(f_str("host")))
    //        {
    //            dia_error_evnt(OTA_RESTORE_CFG_INCOMPLETE);
    //            ERROR("OTA restore_cfg cannot find 'host'");
    //            return CFG_ERROR;
    //        }
    //        set_host(cfgfile.get_value());
    //        if (cfgfile.find_string(f_str("port")))
    //        {
    //            dia_error_evnt(OTA_RESTORE_CFG_INCOMPLETE);
    //            ERROR("OTA restore_cfg cannot find 'port'");
    //            return CFG_ERROR;
    //        }
    //        set_port(atoi(cfgfile.get_value()));
    //        if (cfgfile.find_string(f_str("path")))
    //        {
    //            dia_error_evnt(OTA_RESTORE_CFG_INCOMPLETE);
    //            ERROR("OTA restore_cfg cannot find 'path'");
    //            return CFG_ERROR;
    //        }
    //        set_path(cfgfile.get_value());
    //        if (cfgfile.find_string(f_str("check_version")))
    //        {
    //            dia_error_evnt(OTA_RESTORE_CFG_INCOMPLETE);
    //            ERROR("OTA restore_cfg cannot find 'check_version'");
    //            return CFG_ERROR;
    //        }
    //        set_check_version((bool)atoi(cfgfile.get_value()));
    //        if (cfgfile.find_string(f_str("reboot_on_completion")))
    //        {
    //            dia_error_evnt(OTA_RESTORE_CFG_INCOMPLETE);
    //            ERROR("OTA restore_cfg cannot find 'reboot_on_completion'");
    //            return CFG_ERROR;
    //        }
    //        set_reboot_on_completion((bool)atoi(cfgfile.get_value()));
    //        return CFG_OK;
    //    }
    //    else
    //    {
    //        dia_warn_evnt(OTA_RESTORE_CFG_FILE_NOT_FOUND);
    //        WARN("OTA restore_cfg file not found");
    //        return CFG_ERROR;
    //    }
}

int Ota_upgrade::saved_cfg_not_updated(void)
{
    ALL("saved_cfg_not_updated");
    return CFG_OK;
    //    File_to_json cfgfile(f_str("ota.cfg"));
    //    espmem.stack_mon();
    //    if (cfgfile.exists())
    //    {
    //        if (cfgfile.find_string(f_str("host")))
    //        {
    //            dia_error_evnt(OTA_SAVED_CFG_NOT_UPDATED_INCOMPLETE);
    //            ERROR("OTA saved_cfg_not_updated cannot find 'host'");
    //            return CFG_ERROR;
    //        }
    //        if (os_strcmp(get_host(), cfgfile.get_value()))
    //        {
    //            return CFG_REQUIRES_UPDATE;
    //        }
    //        if (cfgfile.find_string(f_str("port")))
    //        {
    //            dia_error_evnt(OTA_SAVED_CFG_NOT_UPDATED_INCOMPLETE);
    //            ERROR("OTA saved_cfg_not_updated cannot find 'port'");
    //            return CFG_ERROR;
    //        }
    //        if (_port != (atoi(cfgfile.get_value())))
    //        {
    //            return CFG_REQUIRES_UPDATE;
    //        }
    //        if (cfgfile.find_string(f_str("path")))
    //        {
    //            dia_error_evnt(OTA_SAVED_CFG_NOT_UPDATED_INCOMPLETE);
    //            ERROR("OTA saved_cfg_not_updated cannot find 'path'");
    //            return CFG_ERROR;
    //        }
    //        if (os_strcmp(get_path(), cfgfile.get_value()))
    //        {
    //            return CFG_REQUIRES_UPDATE;
    //        }
    //        if (cfgfile.find_string(f_str("check_version")))
    //        {
    //            dia_error_evnt(OTA_SAVED_CFG_NOT_UPDATED_INCOMPLETE);
    //            ERROR("OTA saved_cfg_not_updated cannot find 'check_version'");
    //            return CFG_ERROR;
    //        }
    //        if (get_check_version() != atoi(cfgfile.get_value()))
    //        {
    //            return CFG_REQUIRES_UPDATE;
    //        }
    //        if (cfgfile.find_string(f_str("reboot_on_completion")))
    //        {
    //            dia_error_evnt(OTA_SAVED_CFG_NOT_UPDATED_INCOMPLETE);
    //            ERROR("OTA saved_cfg_not_updated cannot find 'reboot_on_completion'");
    //            return CFG_ERROR;
    //        }
    //        if (get_reboot_on_completion() != atoi(cfgfile.get_value()))
    //        {
    //            return CFG_REQUIRES_UPDATE;
    //        }
    //        return CFG_OK;
    //    }
    //    else
    //    {
    //        return CFG_REQUIRES_UPDATE;
    //    }
}

int Ota_upgrade::save_cfg(void)
{
    ALL("save_cfg");
    return CFG_OK;
//    if (saved_cfg_not_updated() != CFG_REQUIRES_UPDATE)
//        return CFG_OK;
//    if (!espfs.is_available())
//    {
//        dia_error_evnt(OTA_SAVE_CFG_FS_NOT_AVAILABLE);
//        ERROR("OTA save_cfg file system not available");
//        return CFG_ERROR;
//    }
//    Ffile cfgfile(&espfs, (char *)f_str("ota.cfg"));
//    if (!cfgfile.is_available())
//    {
//        dia_error_evnt(OTA_SAVE_CFG_CANNOT_OPEN_FILE);
//        ERROR("OTA save_cfg cannot open file");
//        return CFG_ERROR;
//    }
//    cfgfile.clear();
//    // {"host":"","port":,"path":"","check_version":,"reboot_on_completion":}
//    int buffer_len = 70 + 15 + 5 + os_strlen(get_path()) + 1 + 1 + 1;
//    Heap_chunk buffer(buffer_len);
//    espmem.stack_mon();
//    if (buffer.ref == NULL)
//    {
//        dia_error_evnt(OTA_SAVE_CFG_HEAP_EXHAUSTED, buffer_len);
//        ERROR("OTA save_cfg heap exhausted %d", buffer_len);
//        return CFG_ERROR;
//    }
//    // using fs_sprintf twice to keep fmt len lower that 70 chars
//    fs_sprintf(buffer.ref,
//               "{\"host\":\"%s\",\"port\":%d,\"path\":\"%s\",",
//               get_host(),
//               get_port(),
//               get_path());
//    fs_sprintf((buffer.ref + os_strlen(buffer.ref)),
//               "\"check_version\":%d,\"reboot_on_completion\":%d}",
//               get_check_version(),
//               get_reboot_on_completion());
//    cfgfile.n_append(buffer.ref, os_strlen(buffer.ref));
//    return CFG_OK;
}