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
#include "c_types.h"
#include "ip_addr.h"
#include "espconn.h"
}

#include "espbot.hpp"
#include "espbot_config.hpp"
#include "espbot_diagnostic.hpp"
#include "espbot_event_codes.h"
#include "espbot_global.hpp"
#include "espbot_mdns.hpp"
#include "espbot_utils.hpp"

void Mdns::init(void)
{
    _enabled = false;
    _running = false;

    if (restore_cfg())
    {
        esp_diag.warn(MDNS_INIT_DEFAULT_CFG);
        WARN("Mdns::init no cfg available");
    }
}

void Mdns::start(char *app_alias)
{
    if (_enabled && !_running)
    {
        struct ip_info ipconfig;
        wifi_get_ip_info(STATION_IF, &ipconfig);
        _info.host_name = espbot.get_name();
        _info.ipAddr = ipconfig.ip.addr;
        _info.server_name = espbot.get_name();
        _info.server_port = SERVER_PORT;
        _info.txt_data[0] = app_alias;
        espconn_mdns_init(&_info);
        _running = true;
        esp_diag.info(MDNS_START);
        INFO("mDns started");
    }
}

void Mdns::stop(void)
{
    if (_running)
    {
        espconn_mdns_close();
        _running = false;
        esp_diag.info(MDNS_STOP);
        INFO("mDns ended");
    }
}

void Mdns::enable(void)
{
    if (!_enabled)
    {
        _enabled = true;
        this->start(espbot.get_name());
    }
}

void Mdns::disable(void)
{
    _enabled = false;
    this->stop();
}

bool Mdns::is_enabled(void)
{
    return _enabled;
}

#define MDNS_FILENAME f_str("mdns.cfg")

int Mdns::restore_cfg(void)
{
    ALL("Mdns::restore_cfg");
    return CFG_OK;
    //    File_to_json cfgfile(MDNS_FILENAME);
    //    espmem.stack_mon();
    //    if (!cfgfile.exists())
    //    {
    //        WARN("Mdns::restore_cfg file not found");
    //        return CFG_ERROR;
    //    }
    //    if (cfgfile.find_string(f_str("enabled")))
    //    {
    //        esp_diag.error(MDNS_RESTORE_CFG_INCOMPLETE);
    //        ERROR("Mdns::restore_cfg incomplete cfg");
    //        return CFG_ERROR;
    //    }
    //    _enabled = atoi(cfgfile.get_value());
    //    return CFG_OK;
}

int Mdns::saved_cfg_not_updated(void)
{
    ALL("Mdns::saved_cfg_not_updated");
    return CFG_OK;
    //    File_to_json cfgfile(MDNS_FILENAME);
    //    espmem.stack_mon();
    //    if (!cfgfile.exists())
    //    {
    //        return CFG_REQUIRES_UPDATE;
    //    }
    //    if (cfgfile.find_string(f_str("enabled")))
    //    {
    //        esp_diag.error(MDNS_SAVED_CFG_NOT_UPDATED_INCOMPLETE);
    //        ERROR("Mdns::saved_cfg_not_updated incomplete cfg");
    //        return CFG_ERROR;
    //    }
    //    if (_enabled != atoi(cfgfile.get_value()))
    //    {
    //        return CFG_REQUIRES_UPDATE;
    //    }
    //    return CFG_OK;
}

int Mdns::save_cfg(void)
{
    ALL("Mdns::save_cfg");
    return CFG_OK;
//    if (saved_cfg_not_updated() != CFG_REQUIRES_UPDATE)
//        return CFG_OK;
//    if (!espfs.is_available())
//    {
//        esp_diag.error(MDNS_SAVE_CFG_FS_NOT_AVAILABLE);
//        ERROR("Mdns::save_cfg FS not available");
//        return CFG_ERROR;
//    }
//    Ffile cfgfile(&espfs, (char *)MDNS_FILENAME);
//    if (!cfgfile.is_available())
//    {
//        esp_diag.error(MDNS_SAVE_CFG_CANNOT_OPEN_FILE);
//        ERROR("Mdns::save_cfg cannot open file");
//        return CFG_ERROR;
//    }
//    cfgfile.clear();
//    // "{"enabled": 0}" // 15 chars
//    char buffer[37];
//    espmem.stack_mon();
//    fs_sprintf(buffer,
//               "{\"enabled\": %d}",
//               _enabled);
//    cfgfile.n_append(buffer, os_strlen(buffer));
//    return CFG_OK;
}