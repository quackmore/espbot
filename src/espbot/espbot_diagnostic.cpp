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

#include "espbot_diagnostic.hpp"
#include "espbot_global.hpp"

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
    _diag_led_enabled = true;
    // switch off the diag led
    PIN_FUNC_SELECT(gpio_MUX(DIA_LED), gpio_FUNC(DIA_LED));
    GPIO_OUTPUT_SET(gpio_NUM(DIA_LED), ESPBOT_HIGH);
}

inline void Espbot_diag::add_event(char type, char code, uint32 value)
{
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
    if (_diag_led_enabled)
        GPIO_OUTPUT_SET(gpio_NUM(DIA_LED), ESPBOT_LOW);
}

void Espbot_diag::fatal(char code, uint32 value)
{
    add_event(EVNT_FATAL, code, value);
}

void Espbot_diag::error(char code, uint32 value)
{
    add_event(EVNT_ERROR, code, value);
}

void Espbot_diag::warn(char code, uint32 value)
{
    add_event(EVNT_WARN, code, value);
}

void Espbot_diag::info(char code, uint32 value)
{
    add_event(EVNT_INFO, code, value);
}

void Espbot_diag::debug(char code, uint32 value)
{
    add_event(EVNT_DEBUG, code, value);
}

void Espbot_diag::trace(char code, uint32 value)
{
    add_event(EVNT_TRACE, code, value);
}

void Espbot_diag::all(char code, uint32 value)
{
    add_event(EVNT_ALL, code, value);
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
    if (_diag_led_enabled)
        GPIO_OUTPUT_SET(gpio_NUM(DIA_LED), ESPBOT_HIGH);
}