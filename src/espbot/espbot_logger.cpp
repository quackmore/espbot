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
#include "mem.h"
#include "osapi.h"
#include <stdarg.h>
#include "user_interface.h"
}

extern "C"
{
    int ets_vsprintf(char *str, const char *format, va_list argptr);
    int ets_vsnprintf(char *buffer, unsigned int sizeOfBuffer, const char *format, va_list argptr);
}

#include "espbot_config.hpp"
#include "espbot_diagnostic.hpp"
#include "espbot_event_codes.h"
#include "espbot_global.hpp"
#include "espbot_json.hpp"
#include "espbot_list.hpp"
#include "espbot_logger.hpp"
#include "espbot_mem_mon.hpp"
#include "espbot_mem_sections.h"
#include "espbot_utils.hpp"
#include "spiffs_esp8266.hpp"

/*
 * LOGGER
 */

// global vars for espbot_logger_macros.h
char logger_buffer[LOGGER_BUF_SIZE];

char logger_msg_fmt[] IROM_TEXT ALIGNED_4 = "%s %s%s";
char logger_newline[] IROM_TEXT ALIGNED_4 = "\n";
char logger_fatal_str[] IROM_TEXT ALIGNED_4 = "[FATAL]";
char logger_fatal_fmt[] IROM_TEXT ALIGNED_4 = "{\"time\":\"%s\",\"msg\":\"[FATAL] %s\"}";
char logger_heap_exhausted[] IROM_TEXT ALIGNED_4 = "[FATAL] heap memory exhausted!";
char logger_error_str[] IROM_TEXT ALIGNED_4 = "[ERROR]";
char logger_error_fmt[] IROM_TEXT ALIGNED_4 = "{\"time\":\"%s\",\"msg\":\"[ERROR] %s\"}";
char logger_warn_str[] IROM_TEXT ALIGNED_4 = "[WARNING]";
char logger_warn_fmt[] IROM_TEXT ALIGNED_4 = "{\"time\":\"%s\",\"msg\":\"[WARNING] %s\"}";
char logger_info_str[] IROM_TEXT ALIGNED_4 = "[INFO]";
char logger_info_fmt[] IROM_TEXT ALIGNED_4 = "{\"time\":\"%s\",\"msg\":\"[INFO] %s\"}";
char logger_debug_str[] IROM_TEXT ALIGNED_4 = "[DEBUG]";
char logger_trace_str[] IROM_TEXT ALIGNED_4 = "[TRACE]";
char logger_all_str[] IROM_TEXT ALIGNED_4 = "[ALL]";
// end of global vars for espbot_logger_macros.h

int Logger::restore_cfg(void)
{
    File_to_json cfgfile("logger.cfg");
    espmem.stack_mon();

    if (cfgfile.exists())
    {
        if (cfgfile.find_string("logger_serial_level"))
        {
            esp_diag.error(LOGGER_RESTORE_CFG_INCOMPLETE);
            // esplog.error("Logger::restore_cfg - available configuration is incomplete\n");
            return CFG_ERROR;
        }
        m_serial_level = atoi(cfgfile.get_value());
        if (cfgfile.find_string("logger_memory_level"))
        {
            esp_diag.error(LOGGER_RESTORE_CFG_INCOMPLETE);
            // esplog.error("Logger::restore_cfg - available configuration is incomplete\n");
            return CFG_ERROR;
        }
        m_memory_level = atoi(cfgfile.get_value());
        return CFG_OK;
    }
    else
    {
        esp_diag.warn(LOGGER_RESTORE_CFG_FILE_NOT_FOUND);
        // esplog.warn("Logger::restore_cfg - cfg file not found\n");
        return CFG_ERROR;
    }
}

int Logger::saved_cfg_not_updated(void)
{
    File_to_json cfgfile("logger.cfg");
    espmem.stack_mon();

    if (cfgfile.exists())
    {
        if (cfgfile.find_string("logger_serial_level"))
        {
            esp_diag.error(LOGGER_SAVED_CFG_NOT_UPDATED_INCOMPLETE);
            // esplog.error("Logger::saved_cfg_not_updated - available configuration is incomplete\n");
            return CFG_ERROR;
        }
        if (m_serial_level != atoi(cfgfile.get_value()))
        {
            return CFG_REQUIRES_UPDATE;
        }
        if (cfgfile.find_string("logger_memory_level"))
        {
            esp_diag.error(LOGGER_SAVED_CFG_NOT_UPDATED_INCOMPLETE);
            // esplog.error("Logger::saved_cfg_not_updated - available configuration is incomplete\n");
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

int Logger::save_cfg(void)
{
    if (saved_cfg_not_updated() != CFG_REQUIRES_UPDATE)
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
                esp_diag.error(LOGGER_SAVE_CFG_HEAP_EXHAUSTED);
                // esplog.error("Logger::save_cfg - not enough heap memory available (%d)\n", 64);
                return CFG_ERROR;
            }
        }
        else
        {
            esp_diag.error(LOGGER_SAVE_CFG_CANNOT_OPEN_FILE);
            // esplog.error("Logger::save_cfg - cannot open logger.cfg\n");
            return CFG_ERROR;
        }
    }
    else
    {
        esp_diag.error(LOGGER_SAVE_CFG_FS_NOT_AVAILABLE);
        // esplog.error("Logger::save_cfg - file system not available\n");
        return CFG_ERROR;
    }
    return CFG_OK;
}

void Logger::essential_init(void)
{
    m_serial_level = LOG_LEV_INFO;
    m_memory_level = LOG_LEV_ERROR;
    m_log = (List<char> *)new List<char>(20, delete_content);
}

void Logger::init_cfg(void)
{
    if (restore_cfg())
    {
        esp_diag.warn(LOGGER_INIT_CFG_DEFAULT_CFG);
        // esplog.warn("Logger::init - starting with default configuration\n");
    }
}

int Logger::get_serial_level(void)
{
    return m_serial_level;
}

int Logger::get_memory_level(void)
{
    return m_memory_level;
}

void Logger::set_levels(char t_serial_level, char t_memory_level)
{
    if ((m_serial_level != t_serial_level) || (m_memory_level != t_memory_level))
    {
        m_serial_level = t_serial_level;
        m_memory_level = t_memory_level;
        save_cfg();
    }
}

void Logger::add_log(const char *msg)
{
    m_log->push_back((char *)msg, override_when_full);
}

void Logger::fatal(const char *t_format, ...)
{
    if ((m_serial_level >= LOG_LEV_FATAL) || (m_memory_level >= LOG_LEV_FATAL))
    {
        va_list al;
        espmem.stack_mon();
        va_start(al, t_format);
        Heap_chunk tmp_buffer(LOGGER_BUF_SIZE);
        if (tmp_buffer.ref == NULL)
        {
            va_end(al);
            return;
        }
        ets_vsnprintf(tmp_buffer.ref, (LOGGER_BUF_SIZE - 1), t_format, al);
        va_end(al);
        if (m_serial_level >= LOG_LEV_FATAL)
            os_printf_plus("[FATAL] %s", tmp_buffer.ref);
        if (m_memory_level >= LOG_LEV_FATAL)
        {
            uint32 timestamp = esp_time.get_timestamp();
            Heap_chunk json_ptr(32 + 24 + os_strlen(tmp_buffer.ref), dont_free);
            if (json_ptr.ref == NULL)
                return;
            // eliminate final '\n' character
            char *tmp_ptr = os_strstr(tmp_buffer.ref, "\n");
            if (tmp_ptr)
                *tmp_ptr = '\0';
            os_sprintf(json_ptr.ref,
                       "{\"time\":\"%s\",\"msg\":\"[FATAL] %s\"}",
                       esp_time.get_timestr(timestamp),
                       tmp_buffer.ref);
            m_log->push_back(json_ptr.ref, override_when_full);
        }
    }
}

void Logger::error(const char *t_format, ...)
{
    if ((m_serial_level >= LOG_LEV_ERROR) || (m_memory_level >= LOG_LEV_ERROR))
    {
        va_list al;
        espmem.stack_mon();
        va_start(al, t_format);
        Heap_chunk tmp_buffer(LOGGER_BUF_SIZE);
        if (tmp_buffer.ref == NULL)
        {
            va_end(al);
            return;
        }
        ets_vsnprintf(tmp_buffer.ref, (LOGGER_BUF_SIZE - 1), t_format, al);
        va_end(al);
        if (m_serial_level >= LOG_LEV_ERROR)
            os_printf_plus("[ERROR] %s", tmp_buffer.ref);
        if (m_memory_level >= LOG_LEV_ERROR)
        {
            uint32 timestamp = esp_time.get_timestamp();
            Heap_chunk json_ptr(32 + 24 + os_strlen(tmp_buffer.ref), dont_free);
            if (json_ptr.ref == NULL)
                return;
            // eliminate final '\n' character
            char *tmp_ptr = os_strstr(tmp_buffer.ref, "\n");
            if (tmp_ptr)
                *tmp_ptr = '\0';
            os_sprintf(json_ptr.ref,
                       "{\"time\":\"%s\",\"msg\":\"[ERROR] %s\"}",
                       esp_time.get_timestr(timestamp),
                       tmp_buffer.ref);
            m_log->push_back(json_ptr.ref, override_when_full);
        }
    }
}

void Logger::warn(const char *t_format, ...)
{
    if ((m_serial_level >= LOG_LEV_WARN) || (m_memory_level >= LOG_LEV_WARN))
    {
        va_list al;
        espmem.stack_mon();
        va_start(al, t_format);
        Heap_chunk tmp_buffer(LOGGER_BUF_SIZE);
        if (tmp_buffer.ref == NULL)
        {
            va_end(al);
            return;
        }
        ets_vsnprintf(tmp_buffer.ref, (LOGGER_BUF_SIZE - 1), t_format, al);
        va_end(al);
        if (m_serial_level >= LOG_LEV_WARN)
            os_printf_plus("[WARN] %s", tmp_buffer.ref);
        if (m_memory_level >= LOG_LEV_WARN)
        {
            uint32 timestamp = esp_time.get_timestamp();
            Heap_chunk json_ptr(32 + 24 + os_strlen(tmp_buffer.ref), dont_free);
            if (json_ptr.ref == NULL)
                return;
            // eliminate final '\n' character
            char *tmp_ptr = os_strstr(tmp_buffer.ref, "\n");
            if (tmp_ptr)
                *tmp_ptr = '\0';
            os_sprintf(json_ptr.ref,
                       "{\"time\":\"%s\",\"msg\":\"[WARN] %s\"}",
                       esp_time.get_timestr(timestamp),
                       tmp_buffer.ref);
            m_log->push_back(json_ptr.ref, override_when_full);
        }
    }
}

void Logger::info(const char *t_format, ...)
{
    if ((m_serial_level >= LOG_LEV_INFO) || (m_memory_level >= LOG_LEV_INFO))
    {
        va_list al;
        espmem.stack_mon();
        va_start(al, t_format);
        Heap_chunk tmp_buffer(LOGGER_BUF_SIZE);
        if (tmp_buffer.ref == NULL)
        {
            va_end(al);
            return;
        }
        ets_vsnprintf(tmp_buffer.ref, (LOGGER_BUF_SIZE - 1), t_format, al);
        va_end(al);
        if (m_serial_level >= LOG_LEV_INFO)
            os_printf_plus("[INFO] %s", tmp_buffer.ref);
        if (m_memory_level >= LOG_LEV_INFO)
        {
            uint32 timestamp = esp_time.get_timestamp();
            Heap_chunk json_ptr(32 + 24 + os_strlen(tmp_buffer.ref), dont_free);
            if (json_ptr.ref == NULL)
                return;
            // eliminate final '\n' character
            char *tmp_ptr = os_strstr(tmp_buffer.ref, "\n");
            if (tmp_ptr)
                *tmp_ptr = '\0';
            os_sprintf(json_ptr.ref,
                       "{\"time\":\"%s\",\"msg\":\"[INFO] %s\"}",
                       esp_time.get_timestr(timestamp),
                       tmp_buffer.ref);
            m_log->push_back(json_ptr.ref, override_when_full);
        }
    }
}

void Logger::debug(const char *t_format, ...)
{
    if (m_serial_level < LOG_LEV_DEBUG)
        return;
    else
    {
        va_list al;
        espmem.stack_mon();
        va_start(al, t_format);
        Heap_chunk tmp_buffer(LOGGER_BUF_SIZE);
        if (tmp_buffer.ref == NULL)
        {
            va_end(al);
            return;
        }
        ets_vsnprintf(tmp_buffer.ref, (LOGGER_BUF_SIZE - 1), t_format, al);
        va_end(al);
        os_printf_plus("[DEBUG] %s", tmp_buffer.ref);
    }
}

void Logger::trace(const char *t_format, ...)
{
    if (m_serial_level < LOG_LEV_TRACE)
        return;
    else
    {
        va_list al;
        espmem.stack_mon();
        va_start(al, t_format);
        Heap_chunk tmp_buffer(LOGGER_BUF_SIZE);
        if (tmp_buffer.ref == NULL)
        {
            va_end(al);
            return;
        }
        ets_vsnprintf(tmp_buffer.ref, (LOGGER_BUF_SIZE - 1), t_format, al);
        va_end(al);
        os_printf_plus("[TRACE] %s", tmp_buffer.ref);
    }
}

void Logger::all(const char *t_format, ...)
{
    if (m_serial_level < LOG_LEV_ALL)
        return;
    else
    {
        va_list al;
        va_start(al, t_format);
        Heap_chunk tmp_buffer(LOGGER_BUF_SIZE);
        if (tmp_buffer.ref == NULL)
        {
            va_end(al);
            return;
        }
        ets_vsnprintf(tmp_buffer.ref, (LOGGER_BUF_SIZE - 1), t_format, al);
        va_end(al);
        os_printf_plus("[ALL] %s", tmp_buffer.ref);
    }
}

char *Logger::get_log_head()
{
    return m_log->front();
}

char *Logger::get_log_next()
{
    return m_log->next();
}

int Logger::get_log_size()
{
    return m_log->size();
}