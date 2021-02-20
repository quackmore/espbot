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
#include "espbot_hal.h"
#include "espbot_mem_mon.hpp"
#include "espbot_http.hpp"
#include "espbot_json.hpp"
#include "espbot_spiffs.hpp"
#include "espbot_utils.hpp"
#include "espbot_webclient.hpp"
#include "espbot_wifi.hpp"
#include "spiffs_esp8266.hpp"

typedef enum
{
    not_running = 0,
    running_on_ap,
    running_on_sta
} espbot_http_status_t;

static void espbot_coordinator_task(os_event_t *e)
{
    static espbot_http_status_t espbot_http_status = not_running;
    switch (e->sig)
    {
    case SIG_STAMODE_GOT_IP:
        // [wifi station] got IP
        if (e->par == GOT_IP_AFTER_CONNECTION)
        {
            // new connection
            esp_time.start_sntp();
            esp_mDns.start(espbot.get_name());
            // check if there is a web server listening on esp AP interface
            if (espbot_http_status != not_running)
                espwebsvr.stop();
            espwebsvr.start(80);
            espbot_http_status = running_on_sta;
            app_init_after_wifi();
        }
        if (e->par == GOT_IP_ALREADY_CONNECTED)
        {
            // dhcp lease renewal
            esp_time.stop_sntp();
            esp_time.start_sntp();
            esp_mDns.stop();
            esp_mDns.start(espbot.get_name());
            if (espbot_http_status != not_running)
                espwebsvr.stop();
            espwebsvr.start(80);
            espbot_http_status = running_on_sta;
        }
        break;
    case SIG_STAMODE_DISCONNECTED:
        // [wifi station] disconnected
        esp_time.stop_sntp();
        esp_mDns.stop();
        // stop the webserver only if it is running on the WIFI STATION interface
        if (espbot_http_status == running_on_sta)
        {
            espwebsvr.stop();
            espbot_http_status = not_running;
        }
        app_deinit_on_wifi_disconnect();
        break;
    case SIG_SOFTAPMODE_STACONNECTED:
        // [wifi station+AP] station connected
        break;
    case SIG_SOFTAPMODE_STADISCONNECTED:
        // [wifi station+AP] station disconnected
        break;
    case SIG_SOFTAPMODE_READY:
        // don't stop the web server if it's already listening on WIFI AP interface
        if (espbot_http_status == running_on_sta)
        {
            espwebsvr.stop();
            espbot_http_status = not_running;
        }
        // don't start the web server if it's already listening on WIFI AP interface
        if (espbot_http_status == not_running)
        {
            espwebsvr.start(80);
            espbot_http_status = running_on_ap;
        }
        app_init_after_wifi();
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
    uint32 current_timestamp = esp_time.get_timestamp();
    TRACE("ESPBOT HEARTBEAT: [%d] [UTC+1] %s", current_timestamp, esp_time.get_timestr(current_timestamp));
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
    os_memset(_name, 0, 32);
    if (os_strlen(t_name) > 31)
    {
        dia_warn_evnt(ESPOT_SET_NAME_TRUNCATED);
        WARN("Espbot::set_name truncating name to 32 characters");
    }
    os_strncpy(_name, t_name, 31);
    save_cfg();
}

// make espbot_init available to user_main.c
extern "C" void espbot_init(void);

void espbot_init(void)
{
    // set up custom exception handler (espbot_hal.c)
    // install_custom_exceptions();
    // uart_init(BIT_RATE_74880, BIT_RATE_74880);
    system_set_os_print(1); // enable log print
    // the previous setting will be overridden in esp_diag.init
    // according to custom values saved in flash
    espmem.init();
    esp_time.init_essential(); // cause diagnostic will use timestamp
    // print_greetings();
    gpio_init();           // cause it's used by diagnostic
    dia_init_essential(); // FS not available yet

    esp_spiffs_mount();
    dia_init_custom(); // FS is available now
    esp_time.init();        // FS is available now
    espbot.init();
    esp_mDns.init();
    esp_ota.init();
    http_init();
    espwebsvr.init();
    init_webclients_data_stuctures();
    cron_init();
    app_init_before_wifi();

    espwifi_init();
}

int Espbot::restore_cfg(void)
{
    ALL("Espbot::restore_cfg");
    return CFG_ERROR;
    File_to_json cfgfile(f_str("espbot.cfg"));
    espmem.stack_mon();
    if (cfgfile.exists())
    {
        if (cfgfile.find_string(f_str("espbot_name")))
        {
            dia_error_evnt(ESPBOT_RESTORE_CFG_INCOMPLETE);
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

int Espbot::saved_cfg_not_updated(void)
{
    ALL("Espbot::saved_cfg_not_updated");
    File_to_json cfgfile(f_str("espbot.cfg"));
    espmem.stack_mon();
    if (cfgfile.exists())
    {
        if (cfgfile.find_string(f_str("espbot_name")))
        {
            dia_error_evnt(ESPBOT_SAVED_CFG_NOT_UPDATED_INCOMPLETE);
            ERROR("Espbot::saved_cfg_not_updated incomplete cfg");
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
    return CFG_OK;
    // if (saved_cfg_not_updated() != CFG_REQUIRES_UPDATE)
    //     return CFG_OK;
    // if (espfs.is_available())
    // {
    //     Ffile cfgfile(&espfs, (char *)f_str("espbot.cfg"));
    //     if (cfgfile.is_available())
    //     {
    //         cfgfile.clear();
    //         Heap_chunk buffer(64);
    //         espmem.stack_mon();
    //         if (buffer.ref)
    //         {
    //             fs_sprintf(buffer.ref, "{\"espbot_name\": \"%s\"}", _name);
    //             cfgfile.n_append(buffer.ref, os_strlen(buffer.ref));
    //         }
    //         else
    //         {
    //             dia_error_evnt(ESPBOT_SAVE_CFG_HEAP_EXHAUSTED, 64);
    //             ERROR("Espbot::save_cfg heap exhausted %d", 64);
    //             return CFG_ERROR;
    //         }
    //     }
    //     else
    //     {
    //         dia_error_evnt(ESPBOT_SAVE_CFG_CANNOT_OPEN_FILE);
    //         ERROR("Espbot::save_cfg cannot open file");
    //         return CFG_ERROR;
    //     }
    // }
    // else
    // {
    //     dia_error_evnt(ESPBOT_SAVE_CFG_FS_NOT_AVAILABLE);
    //     ERROR("Espbot::save_cfg FS not available");
    //     return CFG_ERROR;
    // }
    // return 0;
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
        // waiting for completion of any http com
        os_timer_setfn(&graceful_rst_timer, (os_timer_func_t *)graceful_reset, (void *)t_reset);
        os_timer_arm(&graceful_rst_timer, 300, 0);
        break;
    case 1:
        // stop services over wifi
        cron_stop();
        espwebsvr.stop();
        esp_mDns.stop();
        esp_time.stop_sntp();
        os_timer_setfn(&graceful_rst_timer, (os_timer_func_t *)graceful_reset, (void *)t_reset);
        os_timer_arm(&graceful_rst_timer, 200, 0);
        break;
    case 2:
        // stop wifi
        wifi_set_opmode_current(NULL_MODE);
        os_timer_setfn(&graceful_rst_timer, (os_timer_func_t *)graceful_reset, (void *)t_reset);
        os_timer_arm(&graceful_rst_timer, 100, 0);
        break;
    case 3:
        // reset
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
    if (restore_cfg())
    {
        dia_warn_evnt(ESPBOT_INIT_DEFAULT_CFG);
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
    // keep trace of the reboot time
    _lastRebootTime = esp_time.get_timestamp();
}

uint32 Espbot::get_last_reboot_time(void)
{
    return _lastRebootTime;
}

// execute a function from a task
void subsequent_function(void (*fun)(void))
{
    system_os_post(USER_TASK_PRIO_0, SIG_NEXT_FUNCTION, (ETSParam)fun); // informing everybody of
}