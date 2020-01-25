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
#include "espbot_global.hpp"
#include "espbot_logger.hpp"
#include "espbot_mem_mon.hpp"
#include "espbot_ota.hpp"
#include "espbot_utils.hpp"

void Ota_upgrade::init(void)
{
    esplog.all("Ota_upgrade::init\n");
    if (restore_cfg() != CFG_OK)
    {
        // something went wrong while loading flash config
        set_host("0.0.0.0");
        m_port = 0;
        m_path = new char[2];
        os_strncpy(m_path, "/", 1);
        m_check_version = false;
        m_reboot_on_completion = false;
        esplog.warn("Ota_upgrade::init - starting with default configuration\n");
    }
    m_status = OTA_IDLE;
}

void Ota_upgrade::set_host(char *t_str)
{
    esplog.all("Ota_upgrade::set_host\n");
    os_strncpy(m_host_str, t_str, 15);
    atoipaddr(&m_host, t_str);
}

void Ota_upgrade::set_port(char *t_str)
{
    esplog.all("Ota_upgrade::set_port\n");
    m_port = atoi(t_str);
}

void Ota_upgrade::set_path(char *t_str)
{
    esplog.all("Ota_upgrade::set_path\n");
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
        esplog.error("Ota_upgrade::set_path: not enough heap memory (%d)\n", os_strlen(t_str));
    }
}

void Ota_upgrade::set_check_version(char *t_str)
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

void Ota_upgrade::set_reboot_on_completion(char *t_str)
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

char *Ota_upgrade::get_host(void)
{
    esplog.all("Ota_upgrade::get_host\n");
    return m_host_str;
}

int Ota_upgrade::get_port(void)
{
    esplog.all("Ota_upgrade::get_port\n");
    return m_port;
}

char *Ota_upgrade::get_path(void)
{
    esplog.all("Ota_upgrade::get_path\n");
    return m_path;
}

char *Ota_upgrade::get_check_version(void)
{
    esplog.all("Ota_upgrade::get_check_version\n");
    if (m_check_version)
        return "true";
    else
        return "false";
}

char *Ota_upgrade::get_reboot_on_completion(void)
{
    esplog.all("Ota_upgrade::get_reboot_on_completion\n");
    if (m_reboot_on_completion)
        return "true";
    else
        return "false";
}

// upgrade

void Ota_upgrade::ota_completed_cb(void *arg)
{
    esplog.all("Ota_upgrade::ota_completed_cb\n");
    uint8 u_flag = system_upgrade_flag_check();

    if (u_flag == UPGRADE_FLAG_FINISH)
    {
        esp_ota.m_status = OTA_SUCCESS;
    }
    else
    {
        esp_ota.m_status = OTA_FAILED;
        esplog.trace("Ota_upgrade::ota_completed_cb - cannot complete upgrade\n");
    }
}

void Ota_upgrade::ota_timer_function(void *arg)
{
    esplog.all("Ota_upgrade::ota_timer_function\n");

    char *binary_file;
    static struct upgrade_server_info *upgrade_svr = NULL;
    static char *url = NULL;
    // static struct espconn *pespconn = NULL;
    espmem.stack_mon();

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
            binary_file = "user2.bin";
            break;
        case UPGRADE_FW_BIN2:
            binary_file = "user1.bin";
            break;
        default:
            esplog.error("OTA: cannot find userbin number\n");
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
        os_sprintf(url,
                   "GET %s%s HTTP/1.1\r\nHost: %s:%d\r\n"
                   "Connection: close\r\n"
                   "\r\n",
                   esp_ota.m_path, binary_file, esp_ota.m_host_str, esp_ota.m_port);
        esplog.trace("Ota_upgrade::ota_timer_function - %s\n", url);
        upgrade_svr->url = (uint8 *)url;
        if (system_upgrade_start(upgrade_svr) == false)
        {
            esplog.error("OTA cannot start upgrade\n");
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
        esplog.info("OTA successfully completed\n");
        if (esp_ota.m_reboot_on_completion)
        {
            esplog.trace("OTA - rebooting after completion\n");
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
        esplog.error("OTA failed\n");
        esp_ota.m_status = OTA_IDLE;
        break;
    }
    default:
        break;
    }
}

void Ota_upgrade::start_upgrade(void)
{
    esplog.all("Ota_upgrade::start_upgrade\n");
    if ((m_status == OTA_IDLE) || (m_status == OTA_SUCCESS) || (m_status == OTA_FAILED))
    {
        os_timer_setfn(&m_ota_timer, (os_timer_func_t *)&Ota_upgrade::ota_timer_function, NULL);
        os_timer_arm(&m_ota_timer, 200, 0);
    }
    else
    {
        esplog.error("Ota_upgrade::start_upgrade called while OTA in progress\n");
    }
}

Ota_status_type Ota_upgrade::get_status(void)
{
    esplog.all("Ota_upgrade::get_status\n");
    return m_status;
}

int Ota_upgrade::restore_cfg(void)
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
        esplog.warn("Ota_upgrade::restore_cfg - cfg file not found\n");
        return CFG_ERROR;
    }
}

int Ota_upgrade::saved_cfg_not_update(void)
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

int Ota_upgrade::save_cfg(void)
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
            Heap_chunk buffer(90 +
                              16 +
                              6 +
                              os_strlen(get_path()) +
                              10);
            espmem.stack_mon();
            if (buffer.ref)
            {
                os_sprintf(buffer.ref,
                           "{\"host\":\"%s\",\"port\":%d,\"path\":\"%s\",\"check_version\":\"%s\",\"reboot_on_completion\":\"%s\"}",
                           get_host(),
                           get_port(),
                           get_path(),
                           get_check_version(),
                           get_reboot_on_completion());
                cfgfile.n_append(buffer.ref, os_strlen(buffer.ref));
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