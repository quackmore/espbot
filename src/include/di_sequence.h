/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */
#ifndef __DI_SEQUENCE_H__
#define __DI_SEQUENCE_H__

#include "c_types.h"

#define ESPBOT_MEM 1 // will use espbot_2.0 zalloc and free \
                     // undefine this if you want to use sdk os_zalloc and os_free

struct di_seq
{
    // please initialize these using new_sequence
    int di_pin;
    int pulse_max_count;
    void (*end_sequence_callack)(void *);
    void *end_sequence_callack_param;
    os_timer_t timeout_timer;

    // arrays will be allocated here for the reading results
    char *pulse_level;
    uint32 *pulse_duration;

    // do not initialize these are private members
    int pulse_count;
    os_timer_t timeout_timer;
    int current_pulse;
};

struct do_seq *new_sequence(int pin, int num_pulses, int timeout_val); // allocating heap memory
void free_sequence(struct do_seq *seq);               // freeing allocated memory
void set_sequence_cb(struct do_seq *seq, void (*cb)(void *), void *cb_param);

void sequence_clear(struct do_seq *seq); // will clear the pulse sequence only

int get_sequence_length(struct do_seq *seq);
char get_sequence_pulse_level(struct do_seq *seq, int idx);
uint32 get_sequence_pulse_duration(struct do_seq *seq, int idx);

void read_sequence_ms(struct do_seq *seq);
void read_sequence_us(struct do_seq *seq);


#endif