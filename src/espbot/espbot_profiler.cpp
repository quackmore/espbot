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
#include "osapi.h"
#include "user_interface.h"
}

#include "espbot_profiler.hpp"

Profiler::Profiler(char *t_str)
{
    m_msg = t_str;
    m_start_time_us = system_get_time();
}

Profiler::~Profiler()
{
    m_stop_time_us = system_get_time();
    os_printf_plus("ESPBOT PROFILER: %s: %d us\n", m_msg, (m_stop_time_us - m_start_time_us));
}
