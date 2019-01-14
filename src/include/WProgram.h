/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */

#ifndef __WPROGRAM_H__
#define __WPROGRAM_H__

//extern "C"
//{
#include "c_types.h"
#include "math.h"
// }

#define boolean bool
#define abs(a) fabs(a)

#define INPUT_PULLUP 0
#define OUTPUT 0
#define HIGH 1
#define LOW 0

void interrupts(void);
void noInterrupts(void);
void pinMode(int, int);
void digitalWrite(int, int);
int digitalRead(int);





void delay(uint32);
void delayMicroseconds(uint32);
uint32_t millis(void);
uint32_t microsecondsToClockCycles(uint32_t);


#endif