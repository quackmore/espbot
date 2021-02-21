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
#include "user_interface.h"
}

#include "espbot.hpp"
#include "espbot_cfgfile.hpp"
#include "espbot_diagnostic.hpp"
#include "espbot_event_codes.h"
#include "espbot_global.hpp"
#include "espbot_mdns.hpp"
#include "espbot_utils.hpp"

static struct
{
    bool enabled;
} mdns_cfg;

static struct
{
    bool running;
    struct mdns_info info;
} mdns_state;

#define MDNS_FILENAME ((char *)f_str("mdns.cfg"))

static int mdns_restore_cfg(void)
{
    ALL("mdns_restore_cfg");

    if (!Espfile::exists(MDNS_FILENAME))
        return CFG_cantRestore;
    Cfgfile cfgfile(MDNS_FILENAME);
    espmem.stack_mon();
    int enabled = cfgfile.getInt(f_str("mdns_enabled"));
    if (cfgfile.getErr() != JSON_noerr)
    {
        dia_error_evnt(MDNS_RESTORE_CFG_ERROR);
        ERROR("mdns_restore_cfg error");
        return CFG_error;
    }
    mdns_cfg.enabled = (bool)enabled;
    return CFG_ok;
}

static int mdns_saved_cfg_updated(void)
{
    ALL("mdns_saved_cfg_updated");
    if (!Espfile::exists(MDNS_FILENAME))
    {
        return CFG_notUpdated;
    }
    Cfgfile cfgfile(MDNS_FILENAME);
    espmem.stack_mon();
    int enabled = cfgfile.getInt(f_str("mdns_enabled"));
    if (cfgfile.getErr() != JSON_noerr)
    {
        // no need to arise an error, the cfg file will be overwritten
        // dia_error_evnt(MDNS_SAVED_CFG_UPDATED_ERROR);
        // ERROR("mdns_saved_cfg_updated error");
        return CFG_error;
    }
    if (mdns_cfg.enabled != (bool)enabled)
    {
        return CFG_notUpdated;
    }
    return CFG_ok;
}

char *mdns_cfg_json_stringify(char *dest, int len)
{
    // {"mdns_enabled":0}
    int msg_len = 18 + 1;
    char *msg;
    if (dest == NULL)
    {
        msg = new char[msg_len];
        if (msg == NULL)
        {
            dia_error_evnt(MDNS_CFG_STRINGIFY_HEAP_EXHAUSTED, msg_len);
            ERROR("mdns_cfg_json_stringify heap exhausted [%d]", msg_len);
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
               "{\"mdns_enabled\":%d}",
               mdns_cfg.enabled);
    return msg;
}

int mdns_cfg_save(void)
{
    ALL("mdns_cfg_save");
    if (mdns_saved_cfg_updated() == CFG_ok)
        return CFG_ok;
    Cfgfile cfgfile(MDNS_FILENAME);
    espmem.stack_mon();
    if (cfgfile.clear() != SPIFFS_OK)
        return CFG_error;
    char str[19];
    mdns_cfg_json_stringify(str, 19);
    int res = cfgfile.n_append(str, os_strlen(str));
    if (res < SPIFFS_OK)
        return CFG_error;
    return CFG_ok;
}

void mdns_start(char *app_alias)
{
    if (mdns_cfg.enabled && !mdns_state.running)
    {
        struct ip_info ipconfig;
        wifi_get_ip_info(STATION_IF, &ipconfig);
        mdns_state.info.host_name = espbot.get_name();
        mdns_state.info.ipAddr = ipconfig.ip.addr;
        mdns_state.info.server_name = espbot.get_name();
        mdns_state.info.server_port = SERVER_PORT;
        mdns_state.info.txt_data[0] = app_alias;
        espconn_mdns_init(&mdns_state.info);
        mdns_state.running = true;
        dia_info_evnt(MDNS_START);
        INFO("mDns started");
    }
}

void mdns_stop(void)
{
    if (mdns_state.running)
    {
        espconn_mdns_close();
        mdns_state.running = false;
        dia_info_evnt(MDNS_STOP);
        INFO("mDns ended");
    }
}

void mdns_enable(void)
{
    if (!mdns_cfg.enabled)
    {
        mdns_cfg.enabled = true;
        mdns_start(espbot.get_name());
    }
}

void mdns_disable(void)
{
    mdns_cfg.enabled = false;
    mdns_stop();
}

bool mdns_is_enabled(void)
{
    return mdns_cfg.enabled;
}

void mdns_init(void)
{
    mdns_cfg.enabled = false;
    mdns_state.running = false;

    if (mdns_restore_cfg() != CFG_ok)
    {
        dia_warn_evnt(MDNS_INIT_DEFAULT_CFG);
        WARN("mdns_init no cfg available");
    }
}