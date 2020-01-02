/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */
#ifndef __MEM_SECTIONS_H__
#define __MEM_SECTIONS_H__

#define IRAM __attribute__((section(".iram.text")))
#define IROM_TEXT __attribute__((section(".irom.text")))
#define ALIGNED_4 __attribute__((aligned(4)))

#endif