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
#include "user_interface.h"
#include "hw_timer.h"
#include "esp8266_io.h"
#include "do_sequence.h"
#include "dio_task.h"

#ifdef ESPBOT
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

struct do_seq *new_do_seq(int pin, int num_pulses)
{
    struct do_seq *seq = (struct do_seq *)call_espbot_zalloc(sizeof(struct do_seq));
    seq->do_pin = pin;
    seq->pulse_max_count = num_pulses;
    seq->pulse_level = (char *)call_espbot_zalloc(sizeof(char) * num_pulses);
    seq->pulse_duration = (uint32 *)call_espbot_zalloc(sizeof(uint32) * num_pulses);
    return seq;
}

void free_do_seq(struct do_seq *seq)
{
    call_espbot_free(seq->pulse_level);
    call_espbot_free(seq->pulse_duration);
    call_espbot_free(seq);
}

void set_do_seq_cb(struct do_seq *seq, void (*cb)(void *), void *cb_param, CB_call_type cb_call)
{
    seq->end_sequence_callack = cb;
    seq->end_sequence_callack_param = cb_param;
    seq->callback_direct = cb_call;
}

void ICACHE_FLASH_ATTR out_seq_clear(struct do_seq *seq)
{
    int idx = 0;
    seq->pulse_count = 0;
    for (idx = 0; idx < seq->pulse_max_count; idx++)
    {
        seq->pulse_level[idx] = ESPBOT_LOW;
        seq->pulse_duration[idx] = 0;
    }
}

void ICACHE_FLASH_ATTR out_seq_add(struct do_seq *seq, char level, uint32 duration)
{
    if (seq->pulse_count < seq->pulse_max_count)
    {
        seq->pulse_level[seq->pulse_count] = level;
        seq->pulse_duration[seq->pulse_count] = duration;
        seq->pulse_count++;
    }
}

int ICACHE_FLASH_ATTR get_do_seq_length(struct do_seq *seq)
{
    return seq->pulse_count;
}

char ICACHE_FLASH_ATTR get_do_seq_pulse_level(struct do_seq *seq, int idx)
{
    if (idx < seq->pulse_max_count)
        return seq->pulse_level[idx];
    else
        return -1;
}

uint32 ICACHE_FLASH_ATTR get_do_seq_pulse_duration(struct do_seq *seq, int idx)
{
    if (idx < seq->pulse_max_count)
        return seq->pulse_duration[idx];
    else
        return -1;
}

//
// sequence execution
//

//
// ms pulses (SW timers)
//

static void output_pulse(struct do_seq *seq)
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
        if (seq->callback_direct == direct)
            seq->end_sequence_callack(seq->end_sequence_callack_param);
        else
            system_os_post(USER_TASK_PRIO_2, SIG_DO_SEQ_COMPLETED, (os_param_t)seq);
    }
    // the output_pulse function execution takes 8 us
    // during sequence pulse (while the timer is armed)
}

void ICACHE_FLASH_ATTR exe_do_seq_ms(struct do_seq *seq)
{
    os_timer_disarm(&(seq->pulse_timer));
    os_timer_setfn(&(seq->pulse_timer), (os_timer_func_t *)output_pulse, seq);
    seq->current_pulse = 0;
    // save the current status of digital output
    seq->dig_output_initial_value = GPIO_INPUT_GET(seq->do_pin);
    output_pulse(seq);
}

//
// us pulses (HW timers)
//

static struct do_seq *us_seq;
static int us_current_pulse;
static int us_pulse_max_count;
static int us_do_pin;
static char *us_pulse_level;
static uint32 *us_pulse_duration;

static void output_pulse_us(void)
{
    if (us_current_pulse < us_pulse_max_count)
    {
        // executing sequence pulses
        hw_timer_arm(us_pulse_duration[us_current_pulse]);
        GPIO_OUTPUT_SET(us_do_pin, us_pulse_level[us_current_pulse]);
        us_current_pulse++;
    }
    else
    {
        // restoring original digital output status
        GPIO_OUTPUT_SET(us_seq->do_pin, us_seq->dig_output_initial_value);
        hw_timer_disarm();
        // this is an isr function, better don't call the end sequence function here
        if (us_seq->callback_direct == direct)
            us_seq->end_sequence_callack(us_seq->end_sequence_callack_param);
        else
            system_os_post(USER_TASK_PRIO_2, SIG_DO_SEQ_COMPLETED, (os_param_t)us_seq);
    }
    // the output_pulse function execution takes 2-3 us
    // during sequence pulse (while the timer is armed)
    // triggering the end sequence callback takes 19-20 us
    // the callback will be executed by the task after 50-60 us
}

void ICACHE_FLASH_ATTR exe_do_seq_us(struct do_seq *seq)
{
    hw_timer_set_func(output_pulse_us);
    hw_timer_init(FRC1_SOURCE, 0);
    us_seq = seq;
    us_current_pulse = 0;
    us_pulse_max_count = seq->pulse_max_count;
    us_do_pin = seq->do_pin;
    us_pulse_level = seq->pulse_level;
    us_pulse_duration = seq->pulse_duration;
    // os_timer_disarm(&(seq->pulse_timer));
    // os_timer_setfn(&(seq->pulse_timer),
    //                (os_timer_func_t *)seq->end_sequence_callack,
    //                seq->end_sequence_callack_param);

    // save the current status of digital output
    us_seq->dig_output_initial_value = GPIO_INPUT_GET(us_seq->do_pin);
    output_pulse_us();
}