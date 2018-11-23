/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */
#ifndef __ESPBOT_UTILS_HPP__
#define __ESPBOT_UTILS_HPP__

extern "C"
{
#include "osapi.h"
}

int ICACHE_FLASH_ATTR atoi(char *);
int ICACHE_FLASH_ATTR atoh(char *);
void ICACHE_FLASH_ATTR decodeUrlStr(char *);


#endif