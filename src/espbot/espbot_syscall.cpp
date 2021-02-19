/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */

// SDK includes
extern "C"
{
#include "c_types.h"
}

#include "espbot_global.hpp"
#include "espbot_diagnostic.hpp"

extern "C" void __cxa_pure_virtual(void)
{
    WARN("Uh oh, someone called a pure virtual function");
}

void *operator new(size_t size)
{
    return espmem.espbot_zalloc(size);
}

void *operator new[](size_t size)
{
    return espmem.espbot_zalloc(size);
}

void operator delete(void *p)
{
    espmem.espbot_free(p);
}

void operator delete[](void *p)
{
    espmem.espbot_free(p);
}