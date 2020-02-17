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
#include "driver_uart.h"
#include "ets_sys.h"
#include "mem.h"
#include "osapi.h"
#include "user_interface.h"
}
// local includes

#include "app.hpp"
#include "espbot.hpp"
#include "espbot_config.hpp"
#include "espbot_cron.hpp"
#include "espbot_diagnostic.hpp"
#include "espbot_event_codes.h"
#include "espbot_global.hpp"
#include "espbot_gpio.hpp"
#include "espbot_mem_mon.hpp"
#include "espbot_http.hpp"
#include "espbot_json.hpp"
#include "espbot_logger.hpp"
#include "espbot_utils.hpp"
#include "espbot_webclient.hpp"
#include "spiffs_esp8266.hpp"

static void print_greetings(void)
{
    fs_printf("Hello there! Espbot started\n");
    fs_printf("Chip ID        : %d\n", system_get_chip_id());
    fs_printf("SDK version    : %s\n", system_get_sdk_version());
    fs_printf("Boot version   : %d\n", system_get_boot_version());
    fs_printf("Espbot version : %s\n", espbot_release);
    fs_printf("---------------------------------------------------\n");
    fs_printf("Memory map\n");
    system_print_meminfo();
    fs_printf("---------------------------------------------------\n");
}

static void espbot_coordinator_task(os_event_t *e)
{
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
    case SIG_NEXT_FUNCTION:
        // execute a function
        {
            void (*command)(void) = (void (*)(void))e->par;
            if (command)
                command();
        }
        break;
    default:
        break;
    }
}

static void heartbeat_cb(void)
{
    TRACE("ESPBOT HEARTBEAT: ---------------------------------------------------");
    uint32 current_timestamp = esp_sntp.get_timestamp();
    TRACE("ESPBOT HEARTBEAT: [%d] [UTC+1] %s", current_timestamp, esp_sntp.get_timestr(current_timestamp));
    TRACE("ESPBOT HEARTBEAT: Available heap size: %d", system_get_free_heap_size());
}

uint32 Espbot::get_chip_id(void)
{
    return system_get_chip_id();
}

uint8 Espbot::get_boot_version(void)
{
    return system_get_boot_version();
}

const char *Espbot::get_sdk_version(void)
{
    return system_get_sdk_version();
}

char *Espbot::get_version(void)
{
    return (char *)espbot_release;
}

char *Espbot::get_name(void)
{
    return _name;
}

void Espbot::set_name(char *t_name)
{
    os_memset(_name, 0, 33);
    if (os_strlen(t_name) > 32)
    {
        esp_diag.warn(ESPOT_SET_NAME_TRUNCATED);
        WARN("Espbot::set_name truncating name to 32 characters");
    }
    os_strncpy(_name, t_name, 32);
    save_cfg();
}

bool Espbot::mdns_enabled(void)
{
    return _mdns_enabled;
}

void Espbot::enable_mdns(void)
{
    _mdns_enabled = true;
    save_cfg();
}

void Espbot::disable_mdns(void)
{
    _mdns_enabled = false;
    save_cfg();
}

// make espbot_init available to user_main.c
extern "C" void espbot_init(void);

void espbot_init(void)
{
    // uart_init(BIT_RATE_74880, BIT_RATE_74880);
    // uart_init(BIT_RATE_115200, BIT_RATE_115200);
    uart_init(BIT_RATE_460800, BIT_RATE_460800);
    system_set_os_print(1); // enable log print
    espmem.init();
    print_greetings();

    espfs.init();
    esp_diag.init();
    espbot.init();
    esp_ota.init();
    http_init();
    espwebsvr.init();
    init_webclients_data_stuctures();
    esp_gpio.init();
    cron_init();
    app_init_before_wifi();

    Wifi::init();
}

int Espbot::restore_cfg(void)
{
    ALL("Espbot::restore_cfg");
    File_to_json cfgfile(f_str("espbot.cfg"));
    espmem.stack_mon();
    if (cfgfile.exists())
    {
        if (cfgfile.find_string(f_str("espbot_name")))
        {
            esp_diag.error(ESPBOT_RESTORE_CFG_INCOMPLETE);
            ERROR("Espbot::restore_cfg incomplete cfg");
            return CFG_ERROR;
        }
        set_name(cfgfile.get_value());
        return CFG_OK;
    }
    else
    {
        WARN("Espbot::restore_cfg file not found");
        return CFG_ERROR;
    }
}

int Espbot::saved_cfg_not_update(void)
{
    ALL("Espbot::saved_cfg_not_update");
    File_to_json cfgfile(f_str("espbot.cfg"));
    espmem.stack_mon();
    if (cfgfile.exists())
    {
        if (cfgfile.find_string(f_str("espbot_name")))
        {
            esp_diag.error(ESPBOT_SAVED_CFG_NOT_UPDATE_INCOMPLETE);
            ERROR("Espbot::saved_cfg_not_update incomplete cfg");
            return CFG_ERROR;
        }
        if (os_strcmp(_name, cfgfile.get_value()))
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
    ALL("Espbot::save_cfg");
    if (saved_cfg_not_update() != CFG_REQUIRES_UPDATE)
        return CFG_OK;
    if (espfs.is_available())
    {
        Ffile cfgfile(&espfs, (char *)f_str("espbot.cfg"));
        if (cfgfile.is_available())
        {
            cfgfile.clear();
            Heap_chunk buffer(64);
            espmem.stack_mon();
            if (buffer.ref)
            {
                fs_sprintf(buffer.ref, "{\"espbot_name\": \"%s\"}", _name);
                cfgfile.n_append(buffer.ref, os_strlen(buffer.ref));
            }
            else
            {
                esp_diag.error(ESPBOT_SAVE_CFG_HEAP_EXHAUSTED, 64);
                ERROR("Espbot::save_cfg heap exhausted %d", 64);
                return CFG_ERROR;
            }
        }
        else
        {
            esp_diag.error(ESPBOT_SAVE_CFG_CANNOT_OPEN_FILE);
            ERROR("Espbot::save_cfg cannot open file");
            return CFG_ERROR;
        }
    }
    else
    {
        esp_diag.error(ESPBOT_SAVE_CFG_FS_NOT_AVAILABLE);
        ERROR("Espbot::save_cfg FS not available");
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
    graceful_rst_counter++;
    espbot.reset((int)t_reset);
}

void Espbot::reset(int t_reset)
{
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
    // set default name
    fs_sprintf(_name, "ESPBOT-%d", system_get_chip_id());
    _mdns_enabled = false;

    if (restore_cfg())
    {
        esp_diag.warn(ESPBOT_INIT_DEFAULT_CFG);
        WARN("Espbot::init no cfg available, espbot name set to %s", get_name());
    }
    os_timer_disarm(&graceful_rst_timer);

    // REPLACED BY A CRON JOB (2020-02-17)
    //
    // start an heartbeat timer 
    // os_timer_disarm(&_heartbeat);
    // os_timer_setfn(&_heartbeat, (os_timer_func_t *)heartbeat_cb, NULL);
    // os_timer_arm(&_heartbeat, HEARTBEAT_PERIOD, 1);

    // setup the task
    _queue = new os_event_t[QUEUE_LEN];
    system_os_task(espbot_coordinator_task, USER_TASK_PRIO_0, _queue, QUEUE_LEN);
}

// execute a function from a task
void subsequent_function(void (*fun)(void))
{
    system_os_post(USER_TASK_PRIO_0, SIG_NEXT_FUNCTION, (ETSParam)fun); // informing everybody of
}