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
#include "espbot_release.h"
#include "ip_addr.h"
#include "upgrade.h"
}

#include "espbot_global.hpp"
#include "logger.hpp"
#include "ota_upgrade.hpp"
#include "espbot_utils.hpp"
#include "debug.hpp"
#include "config.hpp"

void ICACHE_FLASH_ATTR Ota_upgrade::init(void)
{
    esplog.all("Ota_upgrade::init\n");
    if (restore_cfg() != CFG_OK) // something went wrong while loading flash config
    {
        set_host("0.0.0.0");
        m_port = 0;
        m_path = (char *)esp_zalloc(2);
        os_strncpy(m_path, "/", 1);
        m_check_version = false;
        m_reboot_on_completion = false;
    }
    m_status = OTA_IDLE;
}

void ICACHE_FLASH_ATTR Ota_upgrade::set_host(char *t_str)
{
    esplog.all("Ota_upgrade::set_host\n");
    os_strncpy(m_host_str, t_str, 15);
    atoipaddr(&m_host, t_str);
}

void ICACHE_FLASH_ATTR Ota_upgrade::set_port(char *t_str)
{
    esplog.all("Ota_upgrade::set_port\n");
    m_port = atoi(t_str);
}

void ICACHE_FLASH_ATTR Ota_upgrade::set_path(char *t_str)
{
    esplog.all("Ota_upgrade::set_path\n");
    if (m_path)
    {
        esp_free(m_path);
        m_path = NULL;
    }
    m_path = (char *)esp_zalloc(os_strlen(t_str) + 1);
    if (m_path)
    {
        os_strncpy(m_path, t_str, os_strlen(t_str));
    }
    else
    {
        esplog.error("Ota_upgrade::set_path: not enough heap memory (%d)\n", os_strlen(t_str));
    }
}

void ICACHE_FLASH_ATTR Ota_upgrade::set_check_version(char *t_str)
{
    esplog.all("Ota_upgrade::set_check_version\n");
    if ((os_strncmp(t_str, "true", 4) == 0) || (os_strncmp(t_str, "True", 4) == 0))
    {
        m_check_version = true;
        return;
    }
    if ((os_strncmp(t_str, "false", 5) == 0) || (os_strncmp(t_str, "False", 5) == 0))
    {
        m_check_version = false;
        return;
    }
    esplog.error("Ota_upgrade::set_check_version: cannot assign 'm_check_version' with value (%s)\n", t_str);
}

void ICACHE_FLASH_ATTR Ota_upgrade::set_reboot_on_completion(char *t_str)
{
    esplog.all("Ota_upgrade::set_reboot_on_completion\n");
    if ((os_strncmp(t_str, "true", 4) == 0) || (os_strncmp(t_str, "True", 4) == 0))
    {
        m_reboot_on_completion = true;
        return;
    }
    if ((os_strncmp(t_str, "false", 5) == 0) || (os_strncmp(t_str, "False", 5) == 0))
    {
        m_reboot_on_completion = false;
        return;
    }
    esplog.error("Ota_upgrade::set_check_version: cannot assign 'm_reboot_on_completion' with value (%s)\n", t_str);
}

char ICACHE_FLASH_ATTR *Ota_upgrade::get_host(void)
{
    esplog.all("Ota_upgrade::get_host\n");
    return m_host_str;
}

int ICACHE_FLASH_ATTR Ota_upgrade::get_port(void)
{
    esplog.all("Ota_upgrade::get_port\n");
    return m_port;
}

char ICACHE_FLASH_ATTR *Ota_upgrade::get_path(void)
{
    esplog.all("Ota_upgrade::get_path\n");
    return m_path;
}

char ICACHE_FLASH_ATTR *Ota_upgrade::get_check_version(void)
{
    esplog.all("Ota_upgrade::get_check_version\n");
    if (m_check_version)
        return "true";
    else
        return "false";
}

char ICACHE_FLASH_ATTR *Ota_upgrade::get_reboot_on_completion(void)
{
    esplog.all("Ota_upgrade::get_reboot_on_completion\n");
    if (m_reboot_on_completion)
        return "true";
    else
        return "false";
}

// upgrade

//  OTA_IDLE = 0,
//  OTA_VERSION_CHECKING,
//  OTA_VERSION_CHECK_FAILED,
//  OTA_VERSION_CHECKED,
//  OTA_STARTED,
//  OTA_ENDED,
//  OTA_SUCCESS,
//  OTA_FAILED

void ICACHE_FLASH_ATTR Ota_upgrade::ota_completed_cb(void *arg)
{
    esplog.all("Ota_upgrade::ota_completed_cb\n");
    esp_ota.m_status = OTA_ENDED;
}

void ICACHE_FLASH_ATTR Ota_upgrade::ota_timer_function(void *arg)
{
    esplog.all("Ota_upgrade::ota_timer_function\n");
    switch (esp_ota.m_status)
    {
    case OTA_IDLE:
        break;
    case OTA_VERSION_CHECKING:
        break;
    case OTA_VERSION_CHECK_FAILED:
        break;
    case OTA_VERSION_CHECKED:
        break;
    case OTA_STARTED:
        break;
    case OTA_ENDED:
        break;
    case OTA_SUCCESS:
        break;
    case OTA_FAILED:
        break;
    default:
        break;
    }
}

void ICACHE_FLASH_ATTR Ota_upgrade::start_upgrade(void)
{
    esplog.all("Ota_upgrade::start_upgrade\n");
    if ((m_status == OTA_IDLE) || (m_status == OTA_SUCCESS) || (m_status == OTA_FAILED))
    {
        os_timer_setfn(&m_ota_timer, (os_timer_func_t *)&Ota_upgrade::ota_timer_function, NULL);
        os_timer_arm(&m_ota_timer, 300, 0);
    }
    else
    {
        esplog.error("Ota_upgrade::start_upgrade called while OTA in progress\n");
    }
}

Ota_status_type ICACHE_FLASH_ATTR Ota_upgrade::get_status(void)
{
    esplog.all("Ota_upgrade::get_status\n");
    return m_status;
}

void ICACHE_FLASH_ATTR Ota_upgrade::clear(void)
{
    esplog.all("Ota_upgrade::clear\n");
    m_status = OTA_IDLE;
}

int ICACHE_FLASH_ATTR Ota_upgrade::restore_cfg(void)
{
    esplog.all("Ota_upgrade::restore_cfg\n");
    File_to_json cfgfile("ota.cfg");
    espmem.stack_mon();
    if (cfgfile.exists())
    {
        if (cfgfile.find_string("host"))
        {
            esplog.error("Ota_upgrade::restore_cfg - cannot find 'host'\n");
            return CFG_ERROR;
        }
        set_host(cfgfile.get_value());
        if (cfgfile.find_string("port"))
        {
            esplog.error("Ota_upgrade::restore_cfg - cannot find 'port'\n");
            return CFG_ERROR;
        }
        set_port(cfgfile.get_value());
        if (cfgfile.find_string("path"))
        {
            esplog.error("Ota_upgrade::restore_cfg - cannot find 'path'\n");
            return CFG_ERROR;
        }
        set_path(cfgfile.get_value());
        if (cfgfile.find_string("check_version"))
        {
            esplog.error("Ota_upgrade::restore_cfg - cannot find 'check_version'\n");
            return CFG_ERROR;
        }
        set_check_version(cfgfile.get_value());
        if (cfgfile.find_string("reboot_on_completion"))
        {
            esplog.error("Ota_upgrade::restore_cfg - cannot find 'reboot_on_completion'\n");
            return CFG_ERROR;
        }
        set_reboot_on_completion(cfgfile.get_value());
        return CFG_OK;
    }
    else
    {
        esplog.info("Ota_upgrade::restore_cfg - cfg file not found\n");
        return CFG_ERROR;
    }
}

int ICACHE_FLASH_ATTR Ota_upgrade::saved_cfg_not_update(void)
{
    esplog.all("Ota_upgrade::saved_cfg_not_update\n");
    File_to_json cfgfile("ota.cfg");
    espmem.stack_mon();
    if (cfgfile.exists())
    {
        if (cfgfile.find_string("host"))
        {
            esplog.error("Ota_upgrade::saved_cfg_not_update - cannot find 'host'\n");
            return CFG_ERROR;
        }
        if (os_strcmp(get_host(), cfgfile.get_value()))
        {
            return CFG_REQUIRES_UPDATE;
        }
        if (cfgfile.find_string("port"))
        {
            esplog.error("Ota_upgrade::saved_cfg_not_update - cannot find 'port'\n");
            return CFG_ERROR;
        }
        if (m_port != (atoi(cfgfile.get_value())))
        {
            return CFG_REQUIRES_UPDATE;
        }
        if (cfgfile.find_string("path"))
        {
            esplog.error("Ota_upgrade::saved_cfg_not_update - cannot find 'path'\n");
            return CFG_ERROR;
        }
        if (os_strcmp(get_path(), cfgfile.get_value()))
        {
            return CFG_REQUIRES_UPDATE;
        }
        if (cfgfile.find_string("check_version"))
        {
            esplog.error("Ota_upgrade::saved_cfg_not_update - cannot find 'check_version'\n");
            return CFG_ERROR;
        }
        if (os_strcmp(get_check_version(), cfgfile.get_value()))
        {
            return CFG_REQUIRES_UPDATE;
        }
        if (cfgfile.find_string("reboot_on_completion"))
        {
            esplog.error("Ota_upgrade::saved_cfg_not_update - cannot find 'reboot_on_completion'\n");
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

int ICACHE_FLASH_ATTR Ota_upgrade::save_cfg(void)
{
    esplog.all("Ota_upgrade::save_cfg\n");
    if (saved_cfg_not_update() != CFG_REQUIRES_UPDATE)
        return CFG_OK;
    if (espfs.is_available())
    {
        Ffile cfgfile(&espfs, "ota.cfg");
        if (cfgfile.is_available())
        {
            cfgfile.clear();
            char *buffer = (char *)esp_zalloc(90 +
                                              16 +
                                              6 +
                                              os_strlen(get_path()) +
                                              10);
            espmem.stack_mon();
            if (buffer)
            {
                os_sprintf(buffer,
                           "{\"host\":\"%s\",\"port\":%d,\"path\":\"%s\",\"check_version\":\"%s\",\"reboot_on_completion\":\"%s\"}",
                           get_host(),
                           get_port(),
                           get_path(),
                           get_check_version(),
                           get_reboot_on_completion());
                cfgfile.n_append(buffer, os_strlen(buffer));
                esp_free(buffer);
            }
            else
            {
                esplog.error("Ota_upgrade::save_cfg - not enough heap memory available\n");
                return CFG_ERROR;
            }
        }
        else
        {
            esplog.error("Ota_upgrade::save_cfg - cannot open ota.cfg\n");
            return CFG_ERROR;
        }
    }
    else
    {
        esplog.error("Ota_upgrade::save_cfg - file system not available\n");
        return CFG_ERROR;
    }
    return CFG_OK;
}

/*


enum espbot_upgrade_err_enum
{
    none = 0,
    undefinedUserBin,
    svrVrsOutOfDate,
    cannotStartUpgrade,
    upgradeError
};

static enum espbot_upgrade_err_enum espbot_upgrade_err = none;

char *espbot_upgrade_err_msg[] = {
    "No error!",
    "OTA start: undefined user bin!",
    "OTA start: server version out of date!",
    "OTA start: cannot start upgrade!",
    "OTA check completion: cannot complete FW upgrade!"};

char ICACHE_FLASH_ATTR *espbot_getUpgradeLatestError(void)
{
    return espbot_upgrade_err_msg[espbot_upgrade_err];
}

static void ICACHE_FLASH_ATTR ota_checkCompletion_cb(void *arg)
{
    uint8 u_flag = system_upgrade_flag_check();

    if (u_flag == UPGRADE_FLAG_FINISH)
    {
        espbot_upgrade_status = ESPBOT_UPGRADE_COMPLETED;
        espbot_upgrade_err = none;
        if (upgrade_info.rebootAtCompletion)
        {
            espbot_debug_log(LOW, "ESPBOT OTA: Check completion: success, rebooting!\n");
            uint32 dummy;
            system_os_post(USER_TASK_PRIO_0, SIG_UPGRADE_REBOOT, (os_param_t)dummy);
        }
        else
        {
            espbot_debug_log(LOW, "ESPBOT OTA: Check completion: success\n");
        }
    }
    else
    {
        espbot_debug_log(LOW, "ESPBOT OTA: Check completion: failed\n");
        espbot_upgrade_status = ESPBOT_UPGRADE_FAILED;
        espbot_upgrade_err = upgradeError;
        system_upgrade_deinit();
    }
}

void ICACHE_FLASH_ATTR espbot_fwUpgrade(void)
{
    char *espbot_version = ESPBOT_RELEASE;
    const char *file;

    espbot_upgrade_status = ESPBOT_UPGRADE_STARTED;

    uint8_t userBin = system_upgrade_userbin_check();
    switch (userBin)
    {
    case UPGRADE_FW_BIN1:
        file = "user2.bin";
        break;
    case UPGRADE_FW_BIN2:
        file = "user1.bin";
        break;
    default:
        espbot_debug_log(LOW, "ESPBOT OTA: cannot find userbin number\n");
        espbot_upgrade_status = ESPBOT_UPGRADE_FAILED;
        espbot_upgrade_err = undefinedUserBin;
        return;
    }

    if (upgrade_info.checkVersion)
    {
        if (os_strcmp(upgrade_info.server_version, espbot_version) <= 0)
        {
            espbot_debug_log(LOW, "ESPBOT OTA: won't update, server version:%s out of date. Espbot version %s\n", upgrade_info.server_version, espbot_version);
            espbot_upgrade_status = ESPBOT_UPGRADE_FAILED;
            espbot_upgrade_err = svrVrsOutOfDate;
            return;
        }
        else
        {
            espbot_debug_log(LOW, "ESPBOT OTA: there is an upgrade available with version: %s. Espbot version %s\n", upgrade_info.server_version, espbot_version);
        }
    }
    else
    {
        espbot_debug_log(LOW, "ESPBOT OTA: there is an upgrade available with version: %s. Espbot version %s\n", upgrade_info.server_version, espbot_version);
    }

    struct upgrade_server_info *update = (struct upgrade_server_info *)esp_zalloc(sizeof(struct upgrade_server_info));
    update->pespconn = (struct espconn *)esp_zalloc(sizeof(struct espconn));

    char *tmpStr = esp_zalloc(4);
    int ipField = 0;
    char *ipFieldPtr = upgrade_info.ip;
    char *dotPtr = os_strstr(ipFieldPtr, ".");
    while (ipFieldPtr != NULL && dotPtr != NULL)
    {
        os_strncpy(tmpStr, ipFieldPtr, (dotPtr - ipFieldPtr));
        update->ip[ipField] = atoi(tmpStr);
        ipFieldPtr = dotPtr + 1;
        dotPtr = os_strstr(ipFieldPtr, ".");
        ipField = ipField + 1;
        os_bzero(tmpStr, 4);
    }
    if (ipFieldPtr != NULL && ipField == 3)
    {
        os_strncpy(tmpStr, ipFieldPtr, os_strlen(ipFieldPtr));
        update->ip[ipField] = atoi(tmpStr);
    }
    update->port = upgrade_info.port;

    espbot_debug_log(MID, "ESPBOT OTA: FW upgrade server " IPSTR ":%d. Path: %s%s\n", IP2STR(update->ip), update->port, upgrade_info.path, file);

    update->check_cb = ota_checkCompletion_cb;
    update->check_times = 60000;
    update->url = (uint8 *)esp_zalloc(512);

    os_sprintf((char *)update->url,
               "GET %s%s HTTP/1.1\r\n"
               "Host: " IPSTR ":%d\r\n"
               "Connection: close\r\n"
               "\r\n",
               upgrade_info.path, file, IP2STR(update->ip), update->port);

    if (system_upgrade_start(update) == false)
    {
        espbot_debug_log(LOW, "ESPBOT OTA: cannot start upgrade\n");
        espbot_upgrade_status = ESPBOT_UPGRADE_FAILED;
        espbot_upgrade_err = cannotStartUpgrade;
        esp_free(update->pespconn);
        esp_free(update->url);
        esp_free(update);
    }
    else
    {
        espbot_debug_log(LOW, "ESPBOT OTA: Upgrading...\n");
    }
    espbot_debug_checkHeap();
}
*/