/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */

#include "WProgram.h"

void interrupts(void)
{
    ets_intr_unlock();
}

void noInterrupts(void)
{
    ets_intr_lock();
}

void delay(uint32);
void delayMicroseconds(uint32);
uint32_t millis(void);
uint32_t microsecondsToClockCycles(uint32_t);
