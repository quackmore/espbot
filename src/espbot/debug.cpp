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
#include <stdarg.h>
#include "osapi.h"
#include "mem.h"
#include "user_interface.h"
}
extern "C"
{
    int ets_vsprintf(char *str, const char *format, va_list argptr);
    int ets_vsnprintf(char *buffer, unsigned int sizeOfBuffer, const char *format, va_list argptr);
}

#include "debug.hpp"
#include "espbot_global.hpp"
#include "esp8266_spiffs.hpp"
#include "espbot_utils.hpp"
#include "json.hpp"
#include "config.hpp"

/*
 * DEBUGGER
 */

void ICACHE_FLASH_ATTR Dbggr::init(void)
{
    m_min_heap_size = 65535;
    os_printf("[INFO]: Dbggr::init complete\n");
}

void ICACHE_FLASH_ATTR Dbggr::check_heap_size(void)
{
    uint32 currentHeap = system_get_free_heap_size();
    if (m_min_heap_size > currentHeap)
        m_min_heap_size = currentHeap;
}
uint32 ICACHE_FLASH_ATTR Dbggr::get_mim_heap_size(void)
{
    return m_min_heap_size;
}

/*
 * LOGGER
 */

#define DEBUG_CFG_FILE_SIZE 128

int ICACHE_FLASH_ATTR Logger::restore_cfg(void)
{
    File_to_json cfgfile("logger.cfg");
    if (cfgfile.exists())
    {
        if (cfgfile.find_string("serial_level"))
        {
            esplog.error("Logger::restore_cfg - available configuration is incomplete\n");
            return CFG_ERROR;
        }
        m_serial_level = atoi(cfgfile.get_value());
        if (cfgfile.find_string("file_level"))
        {
            esplog.error("Logger::restore_cfg - available configuration is incomplete\n");
            return CFG_ERROR;
        }
        m_file_level = atoi(cfgfile.get_value());
        return CFG_OK;
    }
    else
    {
        esplog.info("Logger::restore_cfg - cfg file not found\n");
        return CFG_ERROR;
    }
}

int ICACHE_FLASH_ATTR Logger::saved_cfg_not_update(void)
{
    File_to_json cfgfile("logger.cfg");
    if (cfgfile.exists())
    {
        if (cfgfile.find_string("serial_level"))
        {
            esplog.error("Logger::saved_cfg_not_update - available configuration is incomplete\n");
            return CFG_ERROR;
        }
        if (m_serial_level != atoi(cfgfile.get_value()))
        {
            return CFG_REQUIRES_UPDATE;
        }
        if (cfgfile.find_string("file_level"))
        {
            esplog.error("Logger::saved_cfg_not_update - available configuration is incomplete\n");
            return CFG_ERROR;
        }
        if (m_file_level != atoi(cfgfile.get_value()))
        {
            return CFG_REQUIRES_UPDATE;
        }
        return CFG_OK;
    }
    else
    {
        return CFG_REQUIRES_UPDATE;
    }
}

int ICACHE_FLASH_ATTR Logger::save_cfg(void)
{
    if (saved_cfg_not_update() != CFG_REQUIRES_UPDATE)
        return CFG_OK;
    if (espfs.is_available())
    {
        Ffile cfgfile(&espfs, "logger.cfg");
        if (cfgfile.is_available())
        {
            cfgfile.clear();
            char *buffer = (char *)os_zalloc(DEBUG_CFG_FILE_SIZE);
            if (buffer)
            {
                os_sprintf(buffer, "{\"serial_level\": %d,\"file_level\": %d}", m_serial_level, m_file_level);
                cfgfile.n_append(buffer, os_strlen(buffer));
                os_free(buffer);
            }
            else
            {
                esplog.error("Logger::save_cfg - not enough heap memory available\n");
                return CFG_ERROR;
            }
        }
        else
        {
            esplog.error("Logger::save_cfg - cannot open logger.cfg\n");
            return CFG_ERROR;
        }
    }
    else
    {
        esplog.error("Logger::save_cfg - file system not available\n");
        return CFG_ERROR;
    }
    return CFG_OK;
}

void ICACHE_FLASH_ATTR Logger::init(void)
{
    m_serial_level = LOG_INFO;
    m_file_level = LOG_ERROR;
    if (restore_cfg())
    {
        esplog.info("Logger::init - starting with default configuration\n");
    }
}

int ICACHE_FLASH_ATTR Logger::get_serial_level(void)
{
    return m_serial_level;
}

int ICACHE_FLASH_ATTR Logger::get_file_level(void)
{
    return m_file_level;
}

void ICACHE_FLASH_ATTR Logger::set_levels(char t_serial_level, char m_file_level)
{
    if ((m_serial_level != t_serial_level) || (m_file_level != m_file_level))
    {
        m_serial_level = t_serial_level;
        m_file_level = m_file_level;
        save_cfg();
    }
}

void ICACHE_FLASH_ATTR Logger::fatal(const char *t_format, ...)
{
    if (m_serial_level < LOG_FATAL)
        return;
    else
    {
        char buffer[LOG_BUF_SIZE];
        va_list al;
        va_start(al, t_format);
        ets_vsnprintf(buffer, 256, t_format, al);
        va_end(al);
        os_printf_plus("[FATAL] %s", buffer);
    }
}

void ICACHE_FLASH_ATTR Logger::error(const char *t_format, ...)
{
    if (m_serial_level < LOG_ERROR)
        return;
    else
    {
        char buffer[LOG_BUF_SIZE];
        va_list al;
        va_start(al, t_format);
        ets_vsnprintf(buffer, 256, t_format, al);
        va_end(al);
        os_printf_plus("[ERROR] %s", buffer);
    }
}

void ICACHE_FLASH_ATTR Logger::warn(const char *t_format, ...)
{
    if (m_serial_level < LOG_WARN)
        return;
    else
    {
        char buffer[LOG_BUF_SIZE];
        va_list al;
        va_start(al, t_format);
        ets_vsnprintf(buffer, 256, t_format, al);
        va_end(al);
        os_printf_plus("[WARN] %s", buffer);
    }
}

void ICACHE_FLASH_ATTR Logger::info(const char *t_format, ...)
{
    if (m_serial_level < LOG_INFO)
        return;
    else
    {
        char buffer[LOG_BUF_SIZE];
        va_list al;
        va_start(al, t_format);
        ets_vsnprintf(buffer, 256, t_format, al);
        va_end(al);
        os_printf_plus("[INFO] %s", buffer);
    }
}

void ICACHE_FLASH_ATTR Logger::debug(const char *t_format, ...)
{
    if (m_serial_level < LOG_DEBUG)
        return;
    else
    {
        char buffer[LOG_BUF_SIZE];
        va_list al;
        va_start(al, t_format);
        ets_vsnprintf(buffer, 256, t_format, al);
        va_end(al);
        os_printf_plus("[DEBUG] %s", buffer);
    }
}

void ICACHE_FLASH_ATTR Logger::trace(const char *t_format, ...)
{
    if (m_serial_level < LOG_TRACE)
        return;
    else
    {
        char buffer[LOG_BUF_SIZE];
        va_list al;
        va_start(al, t_format);
        ets_vsnprintf(buffer, 256, t_format, al);
        va_end(al);
        os_printf_plus("[TRACE] %s", buffer);
    }
}

void ICACHE_FLASH_ATTR Logger::all(const char *t_format, ...)
{
    if (m_serial_level < LOG_ALL)
        return;
    else
    {
        char buffer[LOG_BUF_SIZE];
        va_list al;
        va_start(al, t_format);
        ets_vsnprintf(buffer, 256, t_format, al);
        va_end(al);
        os_printf_plus("[ALL] %s", buffer);
    }
}

/*
 * PROFILER
 */

ICACHE_FLASH_ATTR Profiler::Profiler(char *t_str)
{
    m_msg = t_str;
    m_start_time_us = system_get_time();
}

ICACHE_FLASH_ATTR Profiler::~Profiler()
{
    m_stop_time_us = system_get_time();
    os_printf("ESPBOT PROFILER: %s: %d\n", m_msg, (m_stop_time_us - m_start_time_us));
}
