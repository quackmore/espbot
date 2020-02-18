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

void Ota_upgrade::init(void)
{
    if (restore_cfg() != CFG_OK)
    {
        // something went wrong while loading flash config
        set_host("0.0.0.0");
        m_port = 0;
        m_path = new char[2];
        os_strncpy(m_path, "/", 1);
        m_check_version = false;
        m_reboot_on_completion = false;
        esp_diag.warn(OTA_INIT_DEFAULT_CFG);
        WARN("OTA init starting with default configuration");
    }
    m_status = OTA_IDLE;
}

void Ota_upgrade::set_host(char *t_str)
{
    os_strncpy(m_host_str, t_str, 15);
    atoipaddr(&m_host, t_str);
}

void Ota_upgrade::set_port(char *t_str)
{
    m_port = atoi(t_str);
}

void Ota_upgrade::set_path(char *t_str)
{
    if (m_path)
    {
        delete[] m_path;
        m_path = NULL;
    }
    m_path = new char[os_strlen(t_str) + 1];
    if (m_path)
    {
        os_strncpy(m_path, t_str, os_strlen(t_str));
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
        m_check_version = true;
        return;
    }
    if ((os_strncmp(t_str, f_str("false"), 5) == 0) || (os_strncmp(t_str, f_str("False"), 5) == 0))
    {
        m_check_version = false;
        return;
    }
    esp_diag.error(OTA_SET_CHECK_VERSION_UNKNOWN_VALUE);
    ERROR("set_check_version bad value (%s)", t_str);
}

void Ota_upgrade::set_reboot_on_completion(char *t_str)
{
    if ((os_strncmp(t_str, f_str("true"), 4) == 0) || (os_strncmp(t_str, f_str("True"), 4) == 0))
    {
        m_reboot_on_completion = true;
        return;
    }
    if ((os_strncmp(t_str, f_str("false"), 5) == 0) || (os_strncmp(t_str, f_str("False"), 5) == 0))
    {
        m_reboot_on_completion = false;
        return;
    }
    esp_diag.error(OTA_SET_REBOOT_ON_COMPLETION_UNKNOWN_VALUE);
    ERROR("set_reboot_on_completion bad value (%s)", t_str);
}

char *Ota_upgrade::get_host(void)
{
    return m_host_str;
}

int Ota_upgrade::get_port(void)
{
    return m_port;
}

char *Ota_upgrade::get_path(void)
{
    return m_path;
}

char *Ota_upgrade::get_check_version(void)
{
    if (m_check_version)
        return (char *) f_str("true");
    else
        return (char *) f_str("false");
}

char *Ota_upgrade::get_reboot_on_completion(void)
{
    if (m_reboot_on_completion)
        return (char *) f_str("true");
    else
        return (char *) f_str("false");
}

// upgrade

void Ota_upgrade::ota_completed_cb(void *arg)
{
    ALL("ota_completed_cb");
    uint8 u_flag = system_upgrade_flag_check();

    if (u_flag == UPGRADE_FLAG_FINISH)
    {
        esp_ota.m_status = OTA_SUCCESS;
    }
    else
    {
        esp_ota.m_status = OTA_FAILED;
        esp_diag.error(OTA_FAILURE);
        ERROR("ota_completed_cb cannot complete upgrade");
    }
}

void Ota_upgrade::ota_timer_function(void *arg)
{
    char *binary_file;
    static struct upgrade_server_info *upgrade_svr = NULL;
    static char *url = NULL;
    // static struct espconn *pespconn = NULL;
    espmem.stack_mon();

    // os_printf("OTA STATUS: %d\n", esp_ota.m_status);

    switch (esp_ota.m_status)
    {
    case OTA_IDLE:
    {
        if (esp_ota.m_check_version)
        {
            esp_ota.m_status = OTA_VERSION_CHECKING;
            // start web client
        }
        else
            esp_ota.m_status = OTA_VERSION_CHECKED;
        os_timer_arm(&esp_ota.m_ota_timer, 200, 0);
        break;
    }
    case OTA_VERSION_CHECKING:
    {
        os_timer_arm(&esp_ota.m_ota_timer, 200, 0);
        break;
    }
    case OTA_VERSION_CHECKED:
    {
        uint8_t userBin = system_upgrade_userbin_check();
        switch (userBin)
        {
        case UPGRADE_FW_BIN1:
            binary_file = (char *) f_str("user2.bin");
            break;
        case UPGRADE_FW_BIN2:
            binary_file = (char *) f_str("user1.bin");
            break;
        default:
            esp_diag.error(OTA_TIMER_FUNCTION_USERBIN_ID_UNKNOWN);
            ERROR("OTA: bad userbin number");
            esp_ota.m_status = OTA_FAILED;
            os_timer_arm(&esp_ota.m_ota_timer, 200, 0);
            return;
        }
        upgrade_svr = new struct upgrade_server_info;
        url = new char[56 + 15 + 6 + os_strlen(esp_ota.m_path)];
        *((uint32 *)(upgrade_svr->ip)) = esp_ota.m_host.addr;
        upgrade_svr->port = esp_ota.m_port;
        upgrade_svr->check_times = 60000;
        upgrade_svr->check_cb = &Ota_upgrade::ota_completed_cb;
        // upgrade_svr->pespconn = pespconn;
        fs_sprintf(url,
                   "GET %s%s HTTP/1.1\r\nHost: %s:%d\r\n"
                   "Connection: close\r\n"
                   "\r\n",
                   esp_ota.m_path, binary_file, esp_ota.m_host_str, esp_ota.m_port);
        TRACE("ota_timer_function url %s", url);
        upgrade_svr->url = (uint8 *)url;
        if (system_upgrade_start(upgrade_svr) == false)
        {
            esp_diag.error(OTA_CANNOT_START_UPGRADE);
            ERROR("OTA cannot start upgrade");
            esp_ota.m_status = OTA_FAILED;
        }
        else
        {
            esp_ota.m_status = OTA_STARTED;
        }
        os_timer_arm(&esp_ota.m_ota_timer, 500, 0);
        break;
    }
    case OTA_STARTED:
    {
        os_timer_arm(&esp_ota.m_ota_timer, 500, 0);
        break;
    }
    case OTA_SUCCESS:
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
        // if (pespconn)
        // {
        //     esp_free(pespconn);
        //     pespconn = NULL;
        // }
        esp_diag.info(OTA_SUCCESSFULLY_COMPLETED);
        INFO("OTA successfully completed");
        if (esp_ota.m_reboot_on_completion)
        {
            esp_diag.debug(OTA_REBOOTING_AFTER_COMPLETION);
            DEBUG("OTA - rebooting after completion");
            espbot.reset(ESP_OTA_REBOOT);
        }
        esp_ota.m_status = OTA_IDLE;
        break;
    }
    case OTA_FAILED:
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
        // if (pespconn)
        // {
        //     esp_free(pespconn);
        //     pespconn = NULL;
        // }
        // ERROR("OTA failed\n");
        esp_ota.m_status = OTA_IDLE;
        break;
    }
    default:
        break;
    }
}

void Ota_upgrade::start_upgrade(void)
{
    if ((m_status == OTA_IDLE) || (m_status == OTA_SUCCESS) || (m_status == OTA_FAILED))
    {
        os_timer_setfn(&m_ota_timer, (os_timer_func_t *)&Ota_upgrade::ota_timer_function, NULL);
        os_timer_arm(&m_ota_timer, 200, 0);
    }
    else
    {
        esp_diag.warn(OTA_START_UPGRADE_CALLED_WHILE_OTA_IN_PROGRESS);
        WARN("start_upgrade called while OTA in progress");
    }
}

Ota_status_type Ota_upgrade::get_status(void)
{
    return m_status;
}

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
        if (m_port != (atoi(cfgfile.get_value())))
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
    Ffile cfgfile(&espfs, (char *) f_str("ota.cfg"));
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