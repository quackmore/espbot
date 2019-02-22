/* DHT library

MIT license
written by Adafruit Industries
*/
/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> modified this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */

extern "C"
{
#include "c_types.h"
#include "osapi.h"
#include "gpio.h"
#include "mem.h"
#include "user_interface.h"
#include "esp8266_io.h"
#include "do_sequence.h"
#include "di_sequence.h"

#ifdef ESPBOT
    // these are espbot_2.0 memory management methods
    // https://github.com/quackmore/espbot_2.0
    extern void *call_espbot_zalloc(size_t size);
    extern void call_espbot_free(void *addr);
#else
#define call_espbot_zalloc(a) os_zalloc(a)
#define call_espbot_free(a) os_free(a)
#define getTimestamp() system_get_time();
#endif
}

#include "dht.hpp"

#ifdef ESPBOT

#include "espbot_global.hpp"
#define PRINT_FATAL(...) esplog.fatal(__VA_ARGS__)
#define PRINT_ERROR(...) esplog.error(__VA_ARGS__)
#define PRINT_WARN(...) esplog.warn(__VA_ARGS__)
#define PRINT_INFO(...) esplog.info(__VA_ARGS__)
#define PRINT_DEBUG(...) esplog.debug(__VA_ARGS__)
#define PRINT_TRACE(...) esplog.trace(__VA_ARGS__)
#define PRINT_ALL(...) esplog.all(__VA_ARGS__)
#define getTimestamp() esp_sntp.get_timestamp();

#else

#define PRINT_FATAL(...) os_printf(__VA_ARGS__)
#define PRINT_ERROR(...) os_printf(__VA_ARGS__)
#define PRINT_WARN(...) os_printf(__VA_ARGS__)
#define PRINT_INFO(...) os_printf(__VA_ARGS__)
#define PRINT_DEBUG(...) os_printf(__VA_ARGS__)
#define PRINT_TRACE(...) os_printf(__VA_ARGS__)
#define PRINT_ALL(...) os_printf(__VA_ARGS__)

#endif

static void ICACHE_FLASH_ATTR dht_reading_completed(void *param)
{
    Dht *dht_ptr = (Dht *)param;
    if ((dht_ptr->m_dht_in_sequence)->ended_by_timeout)
    {
        PRINT_ERROR("DHT [D%d] reading timeout, %d samples acquired\n", dht_ptr->m_pin, (dht_ptr->m_dht_in_sequence)->current_pulse);
        seq_di_clear(dht_ptr->m_dht_in_sequence);

        // retry
        // if poll_timer_value > 5 times * 2 seconds (minimum poll)
        //if (dht_ptr->m_poll_interval > (5 * 2))
        if (dht_ptr->m_poll_interval > 2)
        {
            // then retry in 2 seconds
            dht_ptr->m_retry = true;
            os_timer_disarm(&(dht_ptr->m_poll_timer));
            os_timer_arm(&(dht_ptr->m_poll_timer), 2 * 1000, 1);
        }
        return;
    }
    else
    {
        // if coming from a retry then restore original timer
        if (dht_ptr->m_retry)
        {
            os_timer_disarm(&(dht_ptr->m_poll_timer));
            os_timer_arm(&(dht_ptr->m_poll_timer), (dht_ptr->m_poll_interval * 1000), 1);
            dht_ptr->m_retry = false;
        }

        // check preparation to send data into read sequence
        uint32 pulse_duration = get_di_seq_pulse_duration(dht_ptr->m_dht_in_sequence, 0);
        if ((pulse_duration < 70) || (pulse_duration > 90))
            PRINT_ERROR("DHT [D%d] reading: missing preparation to send data\n", dht_ptr->m_pin);
        // convert read sequence to m_data
        int idx;
        char bit_idx, byte_idx;
        for (idx = 0; idx < 5; idx++)
            dht_ptr->m_data[idx] = 0;
        bool invalid_data = false;
        for (idx = 2; idx < 81; idx = idx + 2)
        {
            pulse_duration = get_di_seq_pulse_duration(dht_ptr->m_dht_in_sequence, idx);
            bit_idx = ((idx - 2) / 2) % 8;
            byte_idx = ((idx - 2) / 2) / 8;
            // found a 0 => do nothing
            if ((15 < pulse_duration) && (pulse_duration < 38))
                continue;
            // found a 1
            if ((60 < pulse_duration) && (pulse_duration < 84))
            {
                dht_ptr->m_data[byte_idx] |= (0x01 << (7 - bit_idx));
                continue;
            }
            // now what the heck is this? let's try a guess
            // don't mark data as invalid yet, wait for checksum verification
            // invalid_data = true;
            // PRINT_ERROR("DHT [D%d] reading: cannot understand bit (length = %d us)\n", dht_ptr->m_pin, pulse_duration);
            if (pulse_duration > 49)
                dht_ptr->m_data[byte_idx] |= (0x01 << (7 - bit_idx));
        }
        // calculate buffer position
        // don't update the buffer position, someone could be reading ...
        int cur_pos;
        if (dht_ptr->m_buffer_idx == (dht_ptr->m_max_buffer_size - 1))
            cur_pos = 0;
        else
            cur_pos = dht_ptr->m_buffer_idx + 1;
        // check the checksum
        uint8_t checksum = dht_ptr->m_data[0] + dht_ptr->m_data[1] + dht_ptr->m_data[2] + dht_ptr->m_data[3];
        if (checksum != dht_ptr->m_data[4])
        {
            PRINT_ERROR("DHT [D%d] reading: checksum error\n", dht_ptr->m_pin);
            invalid_data = true;
        }
        // set the invalid data flag
        if (invalid_data)
            dht_ptr->m_invalid_buffer[cur_pos] = true;
        else
            dht_ptr->m_invalid_buffer[cur_pos] = false;
        // get the timestamp
        dht_ptr->m_timestamp_buffer[cur_pos] = getTimestamp();
        // convert m_data to buffer values
        // temperature
        switch (dht_ptr->m_type)
        {
        case DHT11:
            dht_ptr->m_temperature_buffer[cur_pos] = dht_ptr->m_data[2];
            break;
        case DHT22:
        case DHT21:
            dht_ptr->m_temperature_buffer[cur_pos] = dht_ptr->m_data[2] & 0x7F;
            dht_ptr->m_temperature_buffer[cur_pos] *= 256;
            dht_ptr->m_temperature_buffer[cur_pos] += dht_ptr->m_data[3];
            if (dht_ptr->m_data[2] & 0x80)
                dht_ptr->m_temperature_buffer[cur_pos] *= -1;
            break;
        }
        // humidity
        switch (dht_ptr->m_type)
        {
        case DHT11:
            dht_ptr->m_humidity_buffer[cur_pos] = dht_ptr->m_data[0];
            break;
        case DHT22:
        case DHT21:
            dht_ptr->m_humidity_buffer[cur_pos] = dht_ptr->m_data[0];
            dht_ptr->m_humidity_buffer[cur_pos] *= 256;
            dht_ptr->m_humidity_buffer[cur_pos] += dht_ptr->m_data[1];
            break;
        }
        // update the buffer position
        if (dht_ptr->m_buffer_idx == (dht_ptr->m_max_buffer_size - 1))
            dht_ptr->m_buffer_idx = 0;
        else
            dht_ptr->m_buffer_idx++;
        seq_di_clear(dht_ptr->m_dht_in_sequence);
    }
}

static void dht_start_completed(void *param)
{
    Dht *dht_ptr = (Dht *)param;
    // start reading from DHT
    // configure Dx as input and set pullup
    PIN_FUNC_SELECT(gpio_MUX(dht_ptr->m_pin), gpio_FUNC(dht_ptr->m_pin));
    PIN_PULLUP_EN(gpio_MUX(dht_ptr->m_pin));
    GPIO_DIS_OUTPUT(gpio_NUM(dht_ptr->m_pin));

    read_di_sequence(dht_ptr->m_dht_in_sequence);

    // free output sequence
    // free_do_seq(dht_ptr->m_dht_out_sequence);
}

static void dht_read(Dht *dht_ptr)
{
    // configure Dx as output and set it High
    PIN_FUNC_SELECT(gpio_MUX(dht_ptr->m_pin), gpio_FUNC(dht_ptr->m_pin));
    GPIO_OUTPUT_SET(gpio_NUM(dht_ptr->m_pin), ESPBOT_HIGH);
    // configure start sequence (1 pulse)
    // High ____                    ____ 20 us ___
    // Low      |_____ 1,5 ms _____|
    if (!dht_ptr->m_dht_out_sequence)
    {
        dht_ptr->m_dht_out_sequence = new_do_seq(gpio_NUM(dht_ptr->m_pin), 2);
        set_do_seq_cb(dht_ptr->m_dht_out_sequence, dht_start_completed, (void *)dht_ptr, direct);
        out_seq_add(dht_ptr->m_dht_out_sequence, ESPBOT_LOW, 1500);
        out_seq_add(dht_ptr->m_dht_out_sequence, ESPBOT_HIGH, 10);
    }
    // prepare input sequence (82 pulses)
    if (!dht_ptr->m_dht_in_sequence)
    {
        dht_ptr->m_dht_in_sequence = new_di_seq(ESPBOT_D2_NUM, 82, 100, TIMEOUT_MS);
        set_di_seq_cb(dht_ptr->m_dht_in_sequence, dht_reading_completed, (void *)dht_ptr, task);
    }
    // Send start sequence
    exe_do_seq_us(dht_ptr->m_dht_out_sequence);
}

ICACHE_FLASH_ATTR Dht::Dht()
{
}

ICACHE_FLASH_ATTR Dht::~Dht()
{
    if (m_dht_in_sequence)
        free_di_seq(m_dht_in_sequence);
    if (m_dht_out_sequence)
        free_do_seq(m_dht_out_sequence);
    call_espbot_free(m_temperature_buffer);
    call_espbot_free(m_humidity_buffer);
    call_espbot_free(m_invalid_buffer);
    call_espbot_free(m_timestamp_buffer);
}

void ICACHE_FLASH_ATTR Dht::init(int pin, Dht_type type, int poll_interval, int buffer_length)
{
    // init variables
    int idx;
    for (idx = 0; idx < 5; idx++)
        m_data[idx] = 0;
    m_pin = pin;
    m_type = type;
    m_poll_interval = poll_interval;
    if (m_poll_interval < 2)
        m_poll_interval = 2;
    m_dht_out_sequence = NULL;
    m_dht_in_sequence = NULL;
    m_temperature_buffer = (int *)call_espbot_zalloc(buffer_length * sizeof(int));
    for (idx = 0; idx < buffer_length; idx++)
        m_temperature_buffer[idx] = 0;
    m_humidity_buffer = (int *)call_espbot_zalloc(buffer_length * sizeof(int));
    for (idx = 0; idx < buffer_length; idx++)
        m_humidity_buffer[idx] = 0;
    m_invalid_buffer = (bool *)call_espbot_zalloc(buffer_length * sizeof(bool));
    for (idx = 0; idx < buffer_length; idx++)
        m_invalid_buffer[idx] = true;
    m_timestamp_buffer = (uint32_t *)call_espbot_zalloc(buffer_length * sizeof(int));
    for (idx = 0; idx < buffer_length; idx++)
        m_timestamp_buffer[idx] = 0;
    m_max_buffer_size = buffer_length;
    m_buffer_idx = 0;
    m_retry = false;

    // start polling
    os_timer_disarm(&m_poll_timer);
    os_timer_setfn(&m_poll_timer, (os_timer_func_t *)dht_read, this);
    os_timer_arm(&m_poll_timer, m_poll_interval * 1000, 1);
}

float ICACHE_FLASH_ATTR Dht::get_temperature(Temp_scale scale, int idx)
{
    int index = m_buffer_idx;
    while (idx > 0)
    {
        index = index - 1;
        if (index < 0)
            index = m_max_buffer_size - 1;
        idx--;
    }

    switch (m_type)
    {
    case DHT11:
        if (scale == Celsius)
            return m_temperature_buffer[index];
        else if (scale == Fahrenheit)
            return ((m_temperature_buffer[index] * 1.8) + 32);
    case DHT22:
    case DHT21:
        return (m_temperature_buffer[index] * 0.1);
    }
}

float ICACHE_FLASH_ATTR Dht::get_humidity(int idx)
{
    int index = m_buffer_idx;
    while (idx > 0)
    {
        index = index - 1;
        if (index < 0)
            index = m_max_buffer_size - 1;
        idx--;
    }

    switch (m_type)
    {
    case DHT11:
        return m_humidity_buffer[index];
    case DHT22:
    case DHT21:
        return (m_humidity_buffer[index] * 0.1);
    }
}

uint32_t ICACHE_FLASH_ATTR Dht::get_timestamp(int idx)
{
    int index = m_buffer_idx;
    while (idx > 0)
    {
        index = index - 1;
        if (index < 0)
            index = m_max_buffer_size - 1;
        idx--;
    }
    return m_timestamp_buffer[index];
}

bool ICACHE_FLASH_ATTR Dht::get_invalid(int idx)
{
    int index = m_buffer_idx;
    while (idx > 0)
    {
        index = index - 1;
        if (index < 0)
            index = m_max_buffer_size - 1;
        idx--;
    }
    return m_invalid_buffer[index];
}