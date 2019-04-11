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

#include "espbot_logger.hpp"
#include "espbot_global.hpp"
#include "espbot_debug.hpp"
#include "spiffs_esp8266.hpp"
#include "espbot_utils.hpp"
#include "espbot_json.hpp"
#include "espbot_config.hpp"

/*
 * LOGGER
 */

#define DEBUG_CFG_FILE_SIZE 128

int ICACHE_FLASH_ATTR Logger::restore_cfg(void)
{
    esplog.all("Logger::restore_cfg\n");
    File_to_json cfgfile("logger.cfg");
    espmem.stack_mon();

    if (cfgfile.exists())
    {
        if (cfgfile.find_string("logger_serial_level"))
        {
            esplog.error("Logger::restore_cfg - available configuration is incomplete\n");
            return CFG_ERROR;
        }
        m_serial_level = atoi(cfgfile.get_value());
        if (cfgfile.find_string("logger_memory_level"))
        {
            esplog.error("Logger::restore_cfg - available configuration is incomplete\n");
            return CFG_ERROR;
        }
        m_memory_level = atoi(cfgfile.get_value());
        return CFG_OK;
    }
    else
    {
        esplog.warn("Logger::restore_cfg - cfg file not found\n");
        return CFG_ERROR;
    }
}

int ICACHE_FLASH_ATTR Logger::saved_cfg_not_update(void)
{
    esplog.all("Logger::saved_cfg_not_update\n");
    File_to_json cfgfile("logger.cfg");
    espmem.stack_mon();

    if (cfgfile.exists())
    {
        if (cfgfile.find_string("logger_serial_level"))
        {
            esplog.error("Logger::saved_cfg_not_update - available configuration is incomplete\n");
            return CFG_ERROR;
        }
        if (m_serial_level != atoi(cfgfile.get_value()))
        {
            return CFG_REQUIRES_UPDATE;
        }
        if (cfgfile.find_string("logger_memory_level"))
        {
            esplog.error("Logger::saved_cfg_not_update - available configuration is incomplete\n");
            return CFG_ERROR;
        }
        if (m_memory_level != atoi(cfgfile.get_value()))
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
    esplog.all("Logger::save_cfg\n");
    if (saved_cfg_not_update() != CFG_REQUIRES_UPDATE)
        return CFG_OK;
    if (espfs.is_available())
    {
        Ffile cfgfile(&espfs, "logger.cfg");
        espmem.stack_mon();
        if (cfgfile.is_available())
        {
            cfgfile.clear();
            Heap_chunk buffer(64);
            espmem.stack_mon();
            if (buffer.ref)
            {
                os_sprintf(buffer.ref, "{\"logger_serial_level\": %d,\"logger_memory_level\": %d}", m_serial_level, m_memory_level);
                cfgfile.n_append(buffer.ref, os_strlen(buffer.ref));
            }
            else
            {
                esplog.error("Logger::save_cfg - not enough heap memory available (%d)\n", 64);
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
    esplog.all("Logger::init\n");
    m_serial_level = LOG_INFO;
    m_memory_level = LOG_ERROR;
    m_log = new List<char>(20, delete_content);
    if (restore_cfg())
    {
        esplog.warn("Logger::init - starting with default configuration\n");
    }
}

int ICACHE_FLASH_ATTR Logger::get_serial_level(void)
{
    esplog.all("Logger::get_serial_level\n");
    return m_serial_level;
}

int ICACHE_FLASH_ATTR Logger::get_memory_level(void)
{
    esplog.all("Logger::get_memory_level\n");
    return m_memory_level;
}

void ICACHE_FLASH_ATTR Logger::set_levels(char t_serial_level, char t_memory_level)
{
    esplog.all("Logger::get_memory_level\n");
    if ((m_serial_level != t_serial_level) || (m_memory_level != t_memory_level))
    {
        m_serial_level = t_serial_level;
        m_memory_level = t_memory_level;
        save_cfg();
    }
}

void ICACHE_FLASH_ATTR Logger::fatal(const char *t_format, ...)
{
    if ((m_serial_level >= LOG_FATAL) || (m_memory_level >= LOG_FATAL))
    {
        char buffer[LOG_BUF_SIZE];
        va_list al;
        espmem.stack_mon();
        va_start(al, t_format);
        ets_vsnprintf(buffer, LOG_BUF_SIZE, t_format, al);
        va_end(al);
        if (m_serial_level >= LOG_FATAL)
            os_printf_plus("[FATAL] %s", buffer);
        if (m_memory_level >= LOG_FATAL)
        {
            uint32 timestamp = esp_sntp.get_timestamp();
            Heap_chunk time_str(27);
            if (time_str.ref)
                os_sprintf(time_str.ref, "%s", esp_sntp.get_timestr(timestamp));
            Heap_chunk json_ptr(29 + 24 + os_strlen(buffer), dont_free);
            if (json_ptr.ref)
            {
                os_sprintf(json_ptr.ref,
                           "{\"time\":\"%s\",\"msg\":\"[FATAL] %s\"}",
                           time_str.ref, buffer);
                m_log->push_back(json_ptr.ref, override_when_full);
            }
        }
    }
}

void ICACHE_FLASH_ATTR Logger::error(const char *t_format, ...)
{
    if ((m_serial_level >= LOG_ERROR) || (m_memory_level >= LOG_ERROR))
    {
        char buffer[LOG_BUF_SIZE];
        va_list al;
        espmem.stack_mon();
        va_start(al, t_format);
        ets_vsnprintf(buffer, LOG_BUF_SIZE, t_format, al);
        va_end(al);
        if (m_serial_level >= LOG_ERROR)
            os_printf_plus("[ERROR] %s", buffer);
        if (m_memory_level >= LOG_ERROR)
        {
            uint32 timestamp = esp_sntp.get_timestamp();
            Heap_chunk time_str(27);
            if (time_str.ref)
                os_sprintf(time_str.ref, "%s", esp_sntp.get_timestr(timestamp));
            Heap_chunk json_ptr(29 + 24 + os_strlen(buffer), dont_free);
            if (json_ptr.ref)
            {
                os_sprintf(json_ptr.ref,
                           "{\"time\":\"%s\",\"msg\":\"[ERROR] %s\"}",
                           time_str.ref, buffer);
                m_log->push_back(json_ptr.ref, override_when_full);
            }
        }
    }
}

void ICACHE_FLASH_ATTR Logger::warn(const char *t_format, ...)
{
    if ((m_serial_level >= LOG_WARN) || (m_memory_level >= LOG_WARN))
    {
        char buffer[LOG_BUF_SIZE];
        va_list al;
        espmem.stack_mon();
        va_start(al, t_format);
        ets_vsnprintf(buffer, LOG_BUF_SIZE, t_format, al);
        va_end(al);
        if (m_serial_level >= LOG_WARN)
            os_printf_plus("[WARN] %s", buffer);
        if (m_memory_level >= LOG_WARN)
        {
            uint32 timestamp = esp_sntp.get_timestamp();
            Heap_chunk time_str(27);
            if (time_str.ref)
                os_sprintf(time_str.ref, "%s", esp_sntp.get_timestr(timestamp));
            Heap_chunk json_ptr(29 + 24 + os_strlen(buffer), dont_free);
            if (json_ptr.ref)
            {
                os_sprintf(json_ptr.ref,
                           "{\"time\":\"%s\",\"msg\":\"[WARN] %s\"}",
                           time_str.ref, buffer);
                m_log->push_back(json_ptr.ref, override_when_full);
            }
        }
    }
}

void ICACHE_FLASH_ATTR Logger::info(const char *t_format, ...)
{
    if ((m_serial_level >= LOG_INFO) || (m_memory_level >= LOG_INFO))
    {
        char buffer[LOG_BUF_SIZE];
        va_list al;
        espmem.stack_mon();
        va_start(al, t_format);
        ets_vsnprintf(buffer, LOG_BUF_SIZE, t_format, al);
        va_end(al);
        if (m_serial_level >= LOG_INFO)
            os_printf_plus("[INFO] %s", buffer);
        if (m_memory_level >= LOG_INFO)
        {
            uint32 timestamp = esp_sntp.get_timestamp();
            Heap_chunk time_str(27);
            if (time_str.ref)
                os_sprintf(time_str.ref, "%s", esp_sntp.get_timestr(timestamp));
            Heap_chunk json_ptr(29 + 24 + os_strlen(buffer), dont_free);
            if (json_ptr.ref)
            {
                os_sprintf(json_ptr.ref,
                           "{\"time\":\"%s\",\"msg\":\"[INFO] %s\"}",
                           time_str.ref, buffer);
                m_log->push_back(json_ptr.ref, override_when_full);
            }
        }
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
        espmem.stack_mon();
        va_start(al, t_format);
        ets_vsnprintf(buffer, LOG_BUF_SIZE, t_format, al);
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
        espmem.stack_mon();
        va_start(al, t_format);
        ets_vsnprintf(buffer, LOG_BUF_SIZE, t_format, al);
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
        ets_vsnprintf(buffer, LOG_BUF_SIZE, t_format, al);
        va_end(al);
        os_printf_plus("[ALL] %s", buffer);
    }
}

char ICACHE_FLASH_ATTR *Logger::get_log_head()
{
    return m_log->front();
}

char ICACHE_FLASH_ATTR *Logger::get_log_next()
{
    return m_log->next();
}

int ICACHE_FLASH_ATTR Logger::get_log_size()
{
    return m_log->size();
}

/*
 * PROFILER
 */

ICACHE_FLASH_ATTR Profiler::Profiler(char *t_str)
{
    esplog.all("Profiler::Profiler\n");
    m_msg = t_str;
    m_start_time_us = system_get_time();
}

ICACHE_FLASH_ATTR Profiler::~Profiler()
{
    esplog.all("Profiler::~Profiler\n");
    m_stop_time_us = system_get_time();
    os_printf("ESPBOT PROFILER: %s: %d us\n", m_msg, (m_stop_time_us - m_start_time_us));
}
