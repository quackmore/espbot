/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */
extern "C"
{
#include "c_types.h"
#include "driver_uart.h"
#include "eagle_soc.h"
#include "esp8266_io.h"
#include "gpio.h"
#include "user_interface.h"
}

#include "espbot_cfgfile.hpp"
#include "espbot_diagnostic.hpp"
#include "espbot_event_codes.h"
#include "espbot_global.hpp"
#include "espbot_profiler.hpp"
#include "espbot_utils.hpp"

static struct
{
    struct dia_event evnt[EVNT_QUEUE_SIZE];
    int last;
} dia_event_queue;

static struct
{
    uint32 uart_0_bitrate;
    bool sdk_print_enabled;
    char led_mask; // bitmask 00?? ????
                   //           || ||||_ 1 -> show FATAL events on led
                   //           || |||__ 1 -> show ERROR events on led
                   //           || ||___ 1 -> show WARNING events on led
                   //           || |____ 1 -> show INFO events on led
                   //           ||______ 1 -> show DEBUG events on led
                   //           |_______ 1 -> show TRACE events on led
                   // set diag_led_mask to 0 will avoid espbot using any led
    char serial_log_mask;
} dia_cfg;

bool diag_log_err_type(int)
{
    return (dia_cfg.serial_log_mask & EVNT_FATAL);
}

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

inline void dia_add_event(char type, int code, uint32 value)
{
    // Profiler("DIA: add-event"); => 4-5 us
    int idx = dia_event_queue.last + 1;
    if (idx >= EVNT_QUEUE_SIZE)
        idx = 0;
    dia_event_queue.evnt[idx].timestamp = esp_time.get_timestamp();
    dia_event_queue.evnt[idx].ack = 0;
    dia_event_queue.evnt[idx].type = type;
    dia_event_queue.evnt[idx].code = code;
    dia_event_queue.evnt[idx].value = value;
    dia_event_queue.last = idx;
    // switch on the diag led
    if (dia_cfg.led_mask & type)
        esp_gpio.set(DIA_LED, ESPBOT_LOW);
    // GPIO_OUTPUT_SET(gpio_NUM(DIA_LED), ESPBOT_LOW);
}

void dia_fatal_evnt(int code, uint32 value)
{
    dia_add_event(EVNT_FATAL, code, value);
}

void dia_error_evnt(int code, uint32 value)
{
    dia_add_event(EVNT_ERROR, code, value);
}

void dia_warn_evnt(int code, uint32 value)
{
    dia_add_event(EVNT_WARN, code, value);
}

void dia_info_evnt(int code, uint32 value)
{
    dia_add_event(EVNT_INFO, code, value);
}

void dia_debug_evnt(int code, uint32 value)
{
    dia_add_event(EVNT_DEBUG, code, value);
}

void dia_trace_evnt(int code, uint32 value)
{
    dia_add_event(EVNT_TRACE, code, value);
}

int dia_get_max_events_count(void)
{
    return EVNT_QUEUE_SIZE;
}

struct dia_event *dia_get_event(int idx)
{
    // avoid index greater than array size
    idx = idx % EVNT_QUEUE_SIZE;
    int index = dia_event_queue.last - idx;
    if (index < 0)
        index = EVNT_QUEUE_SIZE + index;
    if (dia_event_queue.evnt[index].type != 0)
        return &dia_event_queue.evnt[index];
    else
        return NULL;
}

int dia_get_unack_events(void)
{
    int idx;
    int counter = 0;
    for (idx = 0; idx < EVNT_QUEUE_SIZE; idx++)
    {
        if (dia_event_queue.evnt[idx].type != 0)
            if (dia_event_queue.evnt[idx].ack == 0)
                counter++;
    }
    return counter;
}

void dia_ack_events(void)
{
    int idx;
    for (idx = 0; idx < EVNT_QUEUE_SIZE; idx++)
    {
        if (dia_event_queue.evnt[idx].type != 0)
            if (dia_event_queue.evnt[idx].ack == 0)
                dia_event_queue.evnt[idx].ack = 1;
    }
    // switch off the diag led
    if (dia_cfg.led_mask)
        esp_gpio.set(DIA_LED, ESPBOT_HIGH);
    // GPIO_OUTPUT_SET(gpio_NUM(DIA_LED), ESPBOT_HIGH);
}

void dia_set_led_mask(char mask)
{
    dia_cfg.led_mask = mask;
    if (dia_cfg.led_mask)
    {
        esp_gpio.config(DIA_LED, ESPBOT_GPIO_OUTPUT);
        esp_gpio.set(DIA_LED, ESPBOT_HIGH);
        // PIN_FUNC_SELECT(gpio_MUX(DIA_LED), gpio_FUNC(DIA_LED));
        // GPIO_OUTPUT_SET(gpio_NUM(DIA_LED), ESPBOT_HIGH);
        //
        // check the event journal for events that need to be reported on the LED
        // but weren't yet
        int idx;
        for (idx = 0; idx < EVNT_QUEUE_SIZE; idx++)
        {
            if (dia_event_queue.evnt[idx].type)
                if (dia_cfg.led_mask & dia_event_queue.evnt[idx].type)
                    esp_gpio.set(DIA_LED, ESPBOT_LOW);
        }
    }
    else
    {
        esp_gpio.unconfig(DIA_LED);
    }
}

void dia_set_serial_log_mask(char mask)
{
    dia_cfg.serial_log_mask = mask;
}

bool dia_set_uart_0_bitrate(uint32 val)
{
    uint32 old_value;
    switch (val)
    {
    case BIT_RATE_300:
    case BIT_RATE_600:
    case BIT_RATE_1200:
    case BIT_RATE_2400:
    case BIT_RATE_4800:
    case BIT_RATE_9600:
    case BIT_RATE_19200:
    case BIT_RATE_38400:
    case BIT_RATE_57600:
    case BIT_RATE_74880:
    case BIT_RATE_115200:
    case BIT_RATE_230400:
    case BIT_RATE_460800:
    case BIT_RATE_921600:
    case BIT_RATE_1843200:
    case BIT_RATE_3686400:
        dia_cfg.uart_0_bitrate = val;
        if (dia_cfg.uart_0_bitrate != old_value)
            uart_init((UartBautRate)dia_cfg.uart_0_bitrate, (UartBautRate)dia_cfg.uart_0_bitrate);
        return true;
    default:
        return false;
    }
}

void dia_set_sdk_print_enabled(bool val)
{
    dia_cfg.sdk_print_enabled = val;
    system_set_os_print(dia_cfg.sdk_print_enabled);
}

char *dia_cfg_json_stringify(char *dest, int len)
{
    // "{"diag_led_mask":256,"serial_log_mask":256,"uart_0_bitrate":3686400,"sdk_print_enabled":1}"
    int msg_len = 90 + 1;
    char *msg;
    if (dest == NULL)
    {
        msg = new char[msg_len];
        if (msg == NULL)
        {
            dia_error_evnt(DIAG_CFG_STRINGIFY_HEAP_EXHAUSTED);
            ERROR("cron_state_json_stringify heap exhausted");
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
               "{\"diag_led_mask\":%d,\"serial_log_mask\":%d,",
               dia_cfg.led_mask,
               dia_cfg.serial_log_mask);
    fs_sprintf(msg + os_strlen(msg),
               "\"uart_0_bitrate\":%d,\"sdk_print_enabled\":%d}",
               dia_cfg.uart_0_bitrate,
               dia_cfg.sdk_print_enabled);
    return msg;
}

#define DIAG_FILENAME ((char *)f_str("diagnostic.cfg"))

int dia_restore_cfg(void)
{
    ALL("dia_restore_cfg");
    if (!Espfile::exists(DIAG_FILENAME))
        return CFG_cantRestore;
    Cfgfile cfgfile(DIAG_FILENAME);
    espmem.stack_mon();
    int uart_0_bitrate = cfgfile.getInt("uart_0_bitrate");
    int sdk_print_enabled = cfgfile.getInt("sdk_print_enabled");
    int led_mask = cfgfile.getInt("diag_led_mask");
    int serial_log_mask = cfgfile.getInt("serial_log_mask");
    if (cfgfile.getErr() != JSON_noerr)
    {
        dia_error_evnt(DIAG_RESTORE_CFG_ERROR);
        ERROR("dia_restore_cfg error");
        return CFG_error;
    }
    dia_cfg.uart_0_bitrate = (uint32)uart_0_bitrate;
    dia_cfg.sdk_print_enabled = (bool)sdk_print_enabled;
    dia_cfg.led_mask = (char)led_mask;
    dia_cfg.serial_log_mask = (char)serial_log_mask;
    return CFG_ok;
}

static int dia_saved_cfg_updated(void)
{
    ALL("dia_saved_cfg_updated");
    if (!Espfile::exists(DIAG_FILENAME))
    {
        return CFG_notUpdated;
    }
    Cfgfile cfgfile(DIAG_FILENAME);
    espmem.stack_mon();
    int uart_0_bitrate = cfgfile.getInt("uart_0_bitrate");
    int sdk_print_enabled = cfgfile.getInt("sdk_print_enabled");
    int led_mask = cfgfile.getInt("diag_led_mask");
    int serial_log_mask = cfgfile.getInt("serial_log_mask");
    if (cfgfile.getErr() != JSON_noerr)
    {
        dia_error_evnt(DIAG_SAVED_CFG_UPDATED_ERROR);
        ERROR("dia_saved_cfg_updated error");
        return CFG_error;
    }
    if ((dia_cfg.uart_0_bitrate != (uint32)uart_0_bitrate) ||
        (dia_cfg.sdk_print_enabled != (bool)sdk_print_enabled) ||
        (dia_cfg.led_mask != (char)led_mask) ||
        (dia_cfg.serial_log_mask != (char)serial_log_mask))
    {
        return CFG_notUpdated;
    }
    return CFG_ok;
}

int dia_cfg_save(void)
{
    ALL("dia_cfg_save");
    if (dia_saved_cfg_updated() == CFG_ok)
        return CFG_ok;
    Cfgfile cfgfile(DIAG_FILENAME);
    espmem.stack_mon();
    if (cfgfile.clear() != SPIFFS_OK)
        return CFG_error;
    char str[91];
    dia_cfg_json_stringify(str, 91);
    int res = cfgfile.n_append(str, os_strlen(str));
    if (res < SPIFFS_OK)
        return CFG_error;
    return CFG_ok;
}

void dia_init_essential(void)
{
    int idx;
    // default cfg first
    dia_cfg.uart_0_bitrate = BIT_RATE_74880;
    dia_cfg.sdk_print_enabled = 1;
    for (idx = 0; idx < EVNT_QUEUE_SIZE; idx++)
    {
        dia_event_queue.evnt[idx].timestamp = 0;
        dia_event_queue.evnt[idx].ack = 0;
        dia_event_queue.evnt[idx].type = 0;
        dia_event_queue.evnt[idx].code = 0;
        dia_event_queue.evnt[idx].value = 0;
    }
    dia_event_queue.last = EVNT_QUEUE_SIZE - 1;
    // according to the startup sequence
    // File System errors won't be reported on the LED yet
    dia_cfg.led_mask = DIAG_LED_DISABLED;
    dia_cfg.serial_log_mask = EVNT_TRACE | EVNT_DEBUG | EVNT_INFO | EVNT_WARN | EVNT_ERROR | EVNT_FATAL;
}

void dia_init_custom(void)
{
    // restore cfg from flash if available
    if (dia_restore_cfg())
    {
        dia_warn_evnt(DIAG_INIT_DEFAULT_CFG);
        WARN("dia_init default cfg");
    }
    // set the uart printing
    if (dia_cfg.uart_0_bitrate != BIT_RATE_74880)
        uart_init((UartBautRate)dia_cfg.uart_0_bitrate, (UartBautRate)dia_cfg.uart_0_bitrate);
    system_set_os_print(dia_cfg.sdk_print_enabled);
    print_greetings();
    // set the diagnostic led in case it is used
    if (dia_cfg.led_mask)
    {
        esp_gpio.config(DIA_LED, ESPBOT_GPIO_OUTPUT);
        esp_gpio.set(DIA_LED, ESPBOT_HIGH);
        // PIN_FUNC_SELECT(gpio_MUX(DIA_LED), gpio_FUNC(DIA_LED));
        // GPIO_OUTPUT_SET(gpio_NUM(DIA_LED), ESPBOT_HIGH);
        //
        // check the event journal for events that need to be reported on the LED
        // but weren't yet
        int idx;
        for (idx = 0; idx < EVNT_QUEUE_SIZE; idx++)
        {
            if (dia_event_queue.evnt[idx].type)
                if (dia_cfg.led_mask & dia_event_queue.evnt[idx].type)
                    esp_gpio.set(DIA_LED, ESPBOT_LOW);
        }
    }
}