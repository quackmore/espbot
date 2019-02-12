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
#include "di_sequence.h"
#include "dio_task.h"

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

struct di_seq *new_di_seq(int pin, int num_pulses, int timeout_val, Di_timeout_unit timeout_unit)
{
    struct di_seq *seq = (struct di_seq *)call_espbot_zalloc(sizeof(struct di_seq));
    seq->di_pin = pin;
    seq->pulse_max_count = num_pulses + 1;
    seq->timeout_val = timeout_val;
    seq->timeout_unit = timeout_unit;
    seq->pulse_level = (char *)call_espbot_zalloc(sizeof(char) * (num_pulses + 1));
    seq->pulse_duration = (uint32 *)call_espbot_zalloc(sizeof(uint32) * (num_pulses + 1));
    return seq;
}

void free_di_seq(struct di_seq *seq)
{
    call_espbot_free(seq->pulse_level);
    call_espbot_free(seq->pulse_duration);
    call_espbot_free(seq);
}

void set_di_seq_cb(struct di_seq *seq, void (*cb)(void *), void *cb_param)
{
    seq->end_sequence_callack = cb;
    seq->end_sequence_callack_param = cb_param;
}

void ICACHE_FLASH_ATTR seq_di_clear(struct di_seq *seq)
{
    int idx = 0;
    seq->current_pulse = 0;
    for (idx = 0; idx < seq->pulse_max_count; idx++)
    {
        seq->pulse_level[idx] = ESPBOT_LOW;
        seq->pulse_duration[idx] = 0;
    }
}

int ICACHE_FLASH_ATTR get_di_seq_length(struct di_seq *seq)
{
    return ((seq->current_pulse - 1));
}

char ICACHE_FLASH_ATTR get_di_seq_pulse_level(struct di_seq *seq, int idx)
{
    if (idx < seq->pulse_max_count)
        return seq->pulse_level[idx];
    else
        return -1;
}

uint32 ICACHE_FLASH_ATTR get_di_seq_pulse_duration(struct di_seq *seq, int idx)
{
    if (idx < (seq->pulse_max_count - 1))
        return (seq->pulse_duration[idx + 1] - seq->pulse_duration[idx]);
    else
        return -1;
}

//
// recording sequence
//

static void input_pulse(struct di_seq *seq)
{
    uint32 gpio_status;

    // clear interrupt status (checkout sdk API docs)
    gpio_status = GPIO_REG_READ(GPIO_STATUS_ADDRESS);
    GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, gpio_status);

    // check the allocated memory boundary
    // before acquiring new samples
    if (seq->current_pulse < seq->pulse_max_count)
    {
        seq->pulse_duration[seq->current_pulse] = system_get_time();
        seq->pulse_level[seq->current_pulse] = GPIO_INPUT_GET(seq->di_pin);
        seq->current_pulse++;
        if (seq->current_pulse == seq->pulse_max_count)
        {
            // sequence completed
            // disable the interrupt and
            // signal the sequence has been acquired
            gpio_pin_intr_state_set(seq->di_pin, GPIO_PIN_INTR_DISABLE);
            system_os_post(USER_TASK_PRIO_2, SIG_DI_SEQ_COMPLETED, (os_param_t)seq);
        }
    }
    // isr function takes 1-2 us 
    //                    5-6 us when system_os_post is called
}

static void ICACHE_FLASH_ATTR input_reading_timeout_ms(struct di_seq *seq)
{
    os_timer_disarm(&(seq->timeout_timer));
    seq->ended_by_timeout = true;
    system_os_post(USER_TASK_PRIO_2, SIG_DI_SEQ_COMPLETED, (os_param_t)seq);
}

static struct di_seq *us_seq;

static void ICACHE_FLASH_ATTR input_reading_timeout_us(void)
{
    hw_timer_disarm();
    us_seq->ended_by_timeout = true;
    system_os_post(USER_TASK_PRIO_2, SIG_DI_SEQ_COMPLETED, (os_param_t)us_seq);
}

void ICACHE_FLASH_ATTR read_di_sequence(struct di_seq *seq)
{
    seq->current_pulse = 0;
    seq->ended_by_timeout = false;

    if (seq->timeout_unit == TIMEOUT_MS)
    {
        os_timer_disarm(&(seq->timeout_timer));
        os_timer_setfn(&(seq->timeout_timer), (os_timer_func_t *)input_reading_timeout_ms, seq);
        os_timer_arm(&(seq->timeout_timer), seq->timeout_val, 0);
    }
    else
    {
        us_seq = seq;
        hw_timer_set_func(input_reading_timeout_us);
        hw_timer_init(FRC1_SOURCE, 0);
        hw_timer_arm(seq->timeout_val);
    }

    // enable interrupt on GPIO selected pin for any edge
    ETS_GPIO_INTR_ATTACH((ets_isr_t)input_pulse, seq);
    gpio_pin_intr_state_set(seq->di_pin, GPIO_PIN_INTR_ANYEDGE);
    ETS_GPIO_INTR_ENABLE();
}

void ICACHE_FLASH_ATTR stop_di_sequence_timeout(struct di_seq *seq)
{
    if (seq->timeout_unit == TIMEOUT_MS)
        os_timer_disarm(&(seq->timeout_timer));
    else
        hw_timer_disarm();
}