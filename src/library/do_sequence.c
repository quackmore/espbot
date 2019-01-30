/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */

#include "c_types.h"
#include "gpio.h"
#include "osapi.h"
#include "mem.h"
#include "esp8266_io.h"
#include "do_sequence.h"

#ifdef ESPBOT_MEM
// these are espbot_2.0 memory management methods
// https://github.com/quackmore/espbot_2.0
extern void *call_espbot_zalloc(size_t size);
extern void call_espbot_free(void *addr);
#else
// these are Espressif non-os-sdk memory management methods
#define call_espbot_zalloc(a) os_zalloc(a)
#define call_espbot_free(a) os_free(a)
#endif

//
// sequence definition
//

struct do_seq *new_sequence(int pin, int num_pulses)
{
    struct do_seq *seq = (struct do_seq *)call_espbot_zalloc(sizeof(struct do_seq));
    seq->do_pin = pin;
    seq->pulse_max_count = num_pulses;
    seq->pulse_level = (char *)call_espbot_zalloc(sizeof(char) * num_pulses);
    seq->pulse_duration = (uint32 *)call_espbot_zalloc(sizeof(uint32) * num_pulses);
    return seq;
}

void free_sequence(struct do_seq *seq)
{
    call_espbot_free(seq->pulse_level);
    call_espbot_free(seq->pulse_duration);
    call_espbot_free(seq);
}

void set_sequence_cb(struct do_seq *seq, void (*cb)(void *), void *cb_param)
{
    seq->end_sequence_callack = cb;
    seq->end_sequence_callack_param = cb_param;
}

void ICACHE_FLASH_ATTR sequence_clear(struct do_seq *seq)
{
    int idx = 0;
    seq->pulse_count = 0;
    for (idx = 0; idx < seq->pulse_max_count; idx++)
    {
        seq->pulse_level[idx] = ESPBOT_LOW;
        seq->pulse_duration[idx] = 0;
    }
}

void ICACHE_FLASH_ATTR sequence_add(struct do_seq *seq, char level, uint32 duration)
{
    if (seq->pulse_count < seq->pulse_max_count)
    {
        seq->pulse_level[seq->pulse_count] = level;
        seq->pulse_duration[seq->pulse_count] = duration;
        seq->pulse_count++;
    }
}

int ICACHE_FLASH_ATTR get_sequence_length(struct do_seq *seq)
{
    return seq->pulse_count;
}

char ICACHE_FLASH_ATTR get_sequence_pulse_level(struct do_seq *seq, int idx)
{
    if (idx < seq->pulse_max_count)
        return seq->pulse_level[idx];
    else
        return -1;
}

uint32 ICACHE_FLASH_ATTR get_sequence_pulse_duration(struct do_seq *seq, int idx)
{
    if (idx < seq->pulse_max_count)
        return seq->pulse_duration[idx];
    else
        return -1;
}

//
// sequence execution
//

void output_pulse(struct do_seq *seq)
{
    if (seq->current_pulse < seq->pulse_max_count)
    {
        // executing sequence pulses
        os_timer_arm(&(seq->pulse_timer), seq->pulse_duration[seq->current_pulse], 0);
        GPIO_OUTPUT_SET(seq->do_pin, seq->pulse_level[seq->current_pulse]);
        seq->current_pulse++;
    }
    else
    {
        // restoring original digital output status
        os_timer_disarm(&(seq->pulse_timer));
        GPIO_OUTPUT_SET(seq->do_pin, seq->dig_output_initial_value);
        seq->end_sequence_callack(seq->end_sequence_callack_param);
    }
    // the output_pulse function execution takes 8 us
    // during sequence pulse (while the timer is armed)
}

void ICACHE_FLASH_ATTR exe_sequence_ms(struct do_seq *seq)
{
    os_timer_disarm(&(seq->pulse_timer));
    os_timer_setfn(&(seq->pulse_timer), (os_timer_func_t *)output_pulse, seq);
    seq->current_pulse = 0;
    // save the current status of digital output
    seq->dig_output_initial_value = GPIO_INPUT_GET(seq->do_pin);
    output_pulse(seq);
}