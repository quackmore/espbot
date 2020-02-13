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
#include "eagle_soc.h"
#include "esp8266_io.h"
#include "gpio.h"
}

#include "espbot_config.hpp"
#include "espbot_diagnostic.hpp"
#include "espbot_event_codes.h"
#include "espbot_global.hpp"
#include "espbot_profiler.hpp"
#include "espbot_utils.hpp"

void Espbot_diag::init(void)
{
    int idx;
    for (idx = 0; idx < EVNT_QUEUE_SIZE; idx++)
    {
        _evnt_queue[idx].timestamp = 0;
        _evnt_queue[idx].ack = 0;
        _evnt_queue[idx].type = 0;
        _evnt_queue[idx].code = 0;
        _evnt_queue[idx].value = 0;
    }
    _last_event = EVNT_QUEUE_SIZE - 1;
    // default cfg
    _diag_led_mask = DIAG_LED_DISABLED;
    _serial_log_mask = EVNT_DEBUG | EVNT_INFO | EVNT_WARN | EVNT_ERROR | EVNT_FATAL;
    // restore cfg from flash if available
    if (restore_cfg())
    {
        esp_diag.warn(DIAG_INIT_DEFAULT_CFG);
        WARN("Espbot_diag::init default cfg");
    }
    // switch off the diag led
    if (_diag_led_mask)
    {
        PIN_FUNC_SELECT(gpio_MUX(DIA_LED), gpio_FUNC(DIA_LED));
        GPIO_OUTPUT_SET(gpio_NUM(DIA_LED), ESPBOT_HIGH);
    }
}

inline void Espbot_diag::add_event(char type, int code, uint32 value)
{
    // Profiler("DIA: add-event"); => 4-5 us
    int idx = _last_event + 1;
    if (idx >= EVNT_QUEUE_SIZE)
        idx = 0;
    _evnt_queue[idx].timestamp = esp_sntp.get_timestamp();
    _evnt_queue[idx].ack = 0;
    _evnt_queue[idx].type = type;
    _evnt_queue[idx].code = code;
    _evnt_queue[idx].value = value;
    _last_event = idx;
    // switch on the diag led
    if (_diag_led_mask & type)
        GPIO_OUTPUT_SET(gpio_NUM(DIA_LED), ESPBOT_LOW);
}

void Espbot_diag::fatal(int code, uint32 value)
{
    add_event(EVNT_FATAL, code, value);
}

void Espbot_diag::error(int code, uint32 value)
{
    add_event(EVNT_ERROR, code, value);
}

void Espbot_diag::warn(int code, uint32 value)
{
    add_event(EVNT_WARN, code, value);
}

void Espbot_diag::info(int code, uint32 value)
{
    add_event(EVNT_INFO, code, value);
}

void Espbot_diag::debug(int code, uint32 value)
{
    add_event(EVNT_DEBUG, code, value);
}

void Espbot_diag::trace(int code, uint32 value)
{
    add_event(EVNT_TRACE, code, value);
}

int Espbot_diag::get_max_events_count(void)
{
    return EVNT_QUEUE_SIZE;
}

struct dia_event *Espbot_diag::get_event(int idx)
{
    // avoid index greater than array size
    idx = idx % EVNT_QUEUE_SIZE;
    int index = _last_event - idx;
    if (index < 0)
        index = EVNT_QUEUE_SIZE + index;
    if (_evnt_queue[index].type != 0)
        return &_evnt_queue[index];
    else
        return NULL;
}

int Espbot_diag::get_unack_events(void)
{
    int idx;
    int counter = 0;
    for (idx = 0; idx < EVNT_QUEUE_SIZE; idx++)
    {
        if (_evnt_queue[idx].type != 0)
            if (_evnt_queue[idx].ack == 0)
                counter++;
    }
    return counter;
}

void Espbot_diag::ack_events(void)
{
    int idx;
    for (idx = 0; idx < EVNT_QUEUE_SIZE; idx++)
    {
        if (_evnt_queue[idx].type != 0)
            if (_evnt_queue[idx].ack == 0)
                _evnt_queue[idx].ack = 1;
    }
    // switch off the diag led
    if (_diag_led_mask)
        GPIO_OUTPUT_SET(gpio_NUM(DIA_LED), ESPBOT_HIGH);
}

char Espbot_diag::get_led_mask(void)
{
    return _diag_led_mask;
}

void Espbot_diag::set_led_mask(char mask)
{
    _diag_led_mask = mask;
}

char Espbot_diag::get_serial_log_mask(void)
{
    return _serial_log_mask;
}

void Espbot_diag::set_serial_log_mask(char mask)
{
    _serial_log_mask = mask;
}

#define DIAG_FILENAME f_str("diag_events.cfg")

int Espbot_diag::restore_cfg(void)
{
    ALL("Espbot_diag::restore_cfg");
    File_to_json cfgfile(DIAG_FILENAME);
    espmem.stack_mon();
    if (!cfgfile.exists())
    {
        WARN("Espbot_diag::restore_cfg file not found");
        return CFG_ERROR;
    }
    if (cfgfile.find_string(f_str("diag_led_mask")))
    {
        esp_diag.error(DIAG_RESTORE_CFG_INCOMPLETE);
        ERROR("Espbot_diag::restore_cfg incomplete cfg");
        return CFG_ERROR;
    }
    _diag_led_mask = atoi(cfgfile.get_value());
    if (cfgfile.find_string(f_str("serial_log_mask")))
    {
        esp_diag.error(DIAG_RESTORE_CFG_INCOMPLETE);
        ERROR("Espbot_diag::restore_cfg incomplete cfg");
        return CFG_ERROR;
    }
    _serial_log_mask = atoi(cfgfile.get_value());
    return CFG_OK;
}

int Espbot_diag::saved_cfg_not_update(void)
{
    ALL("Espbot_diag::saved_cfg_not_update");
    File_to_json cfgfile(DIAG_FILENAME);
    espmem.stack_mon();
    if (!cfgfile.exists())
    {
        return CFG_REQUIRES_UPDATE;
    }
    if (cfgfile.find_string(f_str("diag_led_mask")))
    {
        esp_diag.error(DIAG_SAVED_CFG_NOT_UPDATE_INCOMPLETE);
        ERROR("Espbot_diag::saved_cfg_not_update incomplete cfg");
        return CFG_ERROR;
    }
    if (_diag_led_mask != atoi(cfgfile.get_value()))
    {
        return CFG_REQUIRES_UPDATE;
    }
    if (cfgfile.find_string(f_str("serial_log_mask")))
    {
        esp_diag.error(DIAG_SAVED_CFG_NOT_UPDATE_INCOMPLETE);
        ERROR("Espbot_diag::saved_cfg_not_update incomplete cfg");
        return CFG_ERROR;
    }
    if (_serial_log_mask != atoi(cfgfile.get_value()))
    {
        return CFG_REQUIRES_UPDATE;
    }
    return CFG_OK;
}

int Espbot_diag::save_cfg(void)
{
    ALL("Espbot_diag::save_cfg");
    if (saved_cfg_not_update() != CFG_REQUIRES_UPDATE)
        return CFG_OK;
    if (!espfs.is_available())
    {
        esp_diag.error(DIAG_SAVE_CFG_FS_NOT_AVAILABLE);
        ERROR("Espbot_diag::save_cfg FS not available");
        return CFG_ERROR;
    }
    Ffile cfgfile(&espfs, (char *)DIAG_FILENAME);
    if (!cfgfile.is_available())
    {
        esp_diag.error(DIAG_SAVE_CFG_CANNOT_OPEN_FILE);
        ERROR("Espbot_diag::save_cfg cannot open file");
        return CFG_ERROR;
    }
    cfgfile.clear();
    // "{"diag_led_mask": 256, "serial_log_mask": 256}"
    char buffer[47];
    espmem.stack_mon();
    fs_sprintf(buffer,
               "{\"diag_led_mask\": %d, \"serial_log_mask\": %d}",
               _diag_led_mask,
               _serial_log_mask);
    cfgfile.n_append(buffer, os_strlen(buffer));
    return CFG_OK;
}