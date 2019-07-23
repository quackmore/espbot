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
#include "ets_sys.h"
#include "osapi.h"
#include "mem.h"
#include "user_interface.h"
}
// local includes

#include "espbot_debug.hpp"
#include "espbot_global.hpp"
#include "espbot.hpp"
#include "spiffs_esp8266.hpp"
#include "espbot_logger.hpp"
#include "espbot_json.hpp"
#include "espbot_config.hpp"
#include "espbot_gpio.hpp"
#include "espbot_utils.hpp"
#include "espbot_http.hpp"
#include "espbot_webclient.hpp"
#include "app.hpp"

static void print_greetings(void)
{
    os_printf_plus("Hello there! Espbot started\n");
    os_printf_plus("Chip ID        : %d\n", system_get_chip_id());
    os_printf_plus("SDK version    : %s\n", system_get_sdk_version());
    os_printf_plus("Boot version   : %d\n", system_get_boot_version());
    os_printf_plus("Espbot version : %s\n", espbot_release);
    os_printf_plus("---------------------------------------------------\n");
    os_printf_plus("Memory map\n");
    system_print_meminfo();
    os_printf_plus("---------------------------------------------------\n");
}

static void espbot_coordinator_task(os_event_t *e)
{
    esplog.all("espbot_coordinator_task\n");
    switch (e->sig)
    {
    case SIG_STAMODE_GOT_IP:
        // [wifi station] got IP
        esp_sntp.start();
        espwebsvr.stop(); // in case there was a web server listening on esp AP interface
        espwebsvr.start(80);
        app_init_after_wifi();
        break;
    case SIG_STAMODE_DISCONNECTED:
        // [wifi station] disconnected
        esp_sntp.stop();
        app_deinit_on_wifi_disconnect();
        break;
    case SIG_SOFTAPMODE_STACONNECTED:
        // [wifi station+AP] station connected
        break;
    case SIG_SOFTAPMODE_STADISCONNECTED:
        // [wifi station+AP] station disconnected
        break;
    case SIG_HTTP_CHECK_PENDING_RESPONSE:
        // getting here from webserver after send callback completed
        http_check_pending_send();
        break;
    default:
        break;
    }
}

static void heartbeat_cb(void)
{
    esplog.all("heartbeat_cb\n");
    esplog.debug("ESPBOT HEARTBEAT: ---------------------------------------------------\n");
    uint32 current_timestamp = esp_sntp.get_timestamp();
    esplog.debug("ESPBOT HEARTBEAT: [%d] [UTC+1] %s\n", current_timestamp, esp_sntp.get_timestr(current_timestamp));
    esplog.debug("ESPBOT HEARTBEAT: Available heap size: %d\n", system_get_free_heap_size());
}

uint32 Espbot::get_chip_id(void)
{
    esplog.all("Espbot::get_chip_id\n");
    return system_get_chip_id();
}

uint8 Espbot::get_boot_version(void)
{
    esplog.all("Espbot::get_boot_version\n");
    return system_get_boot_version();
}

const char *Espbot::get_sdk_version(void)
{
    esplog.all("Espbot::get_sdk_version\n");
    return system_get_sdk_version();
}

char *Espbot::get_version(void)
{
    esplog.all("Espbot::get_version\n");
    return (char *)espbot_release;
}

char *Espbot::get_name(void)
{
    esplog.all("Espbot::get_name\n");
    return m_name;
}

void Espbot::set_name(char *t_name)
{
    esplog.all("Espbot::set_name\n");
    os_memset(m_name, 0, 32);
    if (os_strlen(t_name) > 31)
    {
        esplog.warn("Espbot::set_name: truncating name to 31 characters\n");
    }
    os_strncpy(m_name, t_name, 31);
    save_cfg();
}

// make espbot_init available to user_main.c
extern "C" void espbot_init(void);

void espbot_init(void)
{
    espmem.init();
    esplog.essential_init();
    print_greetings();

    espfs.init();
    esplog.init_cfg();
    espbot.init();
    esp_ota.init();
    http_init();
    espwebsvr.init();
    init_webclients_data_stuctures();
    esp_gpio.init();
    app_init_before_wifi();

    Wifi::init();
}

int Espbot::restore_cfg(void)
{
    esplog.all("Espbot::restore_cfg\n");
    File_to_json cfgfile("espbot.cfg");
    espmem.stack_mon();
    if (cfgfile.exists())
    {
        if (cfgfile.find_string("espbot_name"))
        {
            esplog.error("Espbot::restore_cfg - available configuration is incomplete\n");
            return CFG_ERROR;
        }
        set_name(cfgfile.get_value());
        return CFG_OK;
    }
    else
    {
        esplog.warn("Espbot::restore_cfg - cfg file not found\n");
        return CFG_ERROR;
    }
}

int Espbot::saved_cfg_not_update(void)
{
    esplog.all("Espbot::saved_cfg_not_update\n");
    File_to_json cfgfile("espbot.cfg");
    espmem.stack_mon();
    if (cfgfile.exists())
    {
        if (cfgfile.find_string("espbot_name"))
        {
            esplog.error("Espbot::saved_cfg_not_update - available configuration is incomplete\n");
            return CFG_ERROR;
        }
        if (os_strcmp(m_name, cfgfile.get_value()))
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

int Espbot::save_cfg(void)
{
    esplog.all("Espbot::save_cfg\n");
    if (saved_cfg_not_update() != CFG_REQUIRES_UPDATE)
        return CFG_OK;
    if (espfs.is_available())
    {
        Ffile cfgfile(&espfs, "espbot.cfg");
        if (cfgfile.is_available())
        {
            cfgfile.clear();
            Heap_chunk buffer(64);
            espmem.stack_mon();
            if (buffer.ref)
            {
                os_sprintf(buffer.ref, "{\"espbot_name\": \"%s\"}", m_name);
                cfgfile.n_append(buffer.ref, os_strlen(buffer.ref));
            }
            else
            {
                esplog.error("Espbot::save_cfg - not enough heap memory available\n");
                return CFG_ERROR;
            }
        }
        else
        {
            esplog.error("Espbot::save_cfg - cannot open espbot.cfg\n");
            return CFG_ERROR;
        }
    }
    else
    {
        esplog.error("Espbot::save_cfg - file system not available\n");
        return CFG_ERROR;
    }
    return 0;
}

// GRACEFUL RESET
// 1) wait 300 ms
// 2) stop the webserver
// 3) wait 200 ms
// 4) stop the wifi
// 5) wait 200 ms
// 6) system_restart

static int graceful_rst_counter = 0;
static os_timer_t graceful_rst_timer;

static void graceful_reset(void *t_reset)
{
    esplog.all("graceful_reset\n");
    graceful_rst_counter++;
    espbot.reset((int)t_reset);
}

void Espbot::reset(int t_reset)
{
    esplog.all("Espbot::reset\n");
    switch (graceful_rst_counter)
    {
    case 0:
        os_timer_setfn(&graceful_rst_timer, (os_timer_func_t *)graceful_reset, (void *)t_reset);
        os_timer_arm(&graceful_rst_timer, 300, 0);
        break;
    case 1:
        espwebsvr.stop();
        esp_mDns.stop();
        esp_sntp.stop();
        os_timer_setfn(&graceful_rst_timer, (os_timer_func_t *)graceful_reset, (void *)t_reset);
        os_timer_arm(&graceful_rst_timer, 200, 0);
        break;
    case 2:
        wifi_set_opmode_current(NULL_MODE);
        os_timer_setfn(&graceful_rst_timer, (os_timer_func_t *)graceful_reset, (void *)t_reset);
        os_timer_arm(&graceful_rst_timer, 200, 0);
        break;
    case 3:
        if (t_reset == ESP_REBOOT)
            system_restart();
        if (t_reset == ESP_OTA_REBOOT)
            system_upgrade_reboot();
        break;
    default:
        break;
    }
}

void Espbot::init(void)
{
    esplog.all("Espbot::init\n");
    // set default name
    os_sprintf(m_name, "ESPBOT-%d", system_get_chip_id());
    if (restore_cfg())
        esplog.warn("no cfg available, espbot name set to %s\n", get_name());

    os_timer_disarm(&graceful_rst_timer);

    // start an heartbeat timer
    os_timer_disarm(&m_heartbeat);
    os_timer_setfn(&m_heartbeat, (os_timer_func_t *)heartbeat_cb, NULL);
    os_timer_arm(&m_heartbeat, HEARTBEAT_PERIOD, 1);

    // setup the task
    m_queue = new os_event_t[QUEUE_LEN];
    system_os_task(espbot_coordinator_task, USER_TASK_PRIO_0, m_queue, QUEUE_LEN);

    esplog.debug("Espbot::init complete\n");
}
