/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */
#ifndef __LOGGER_MACROS_HPP__
#define __LOGGER_MACROS_HPP__

#include "espbot_logger.hpp"
#include "espbot_mem_sections.h"
#include "espbot_utils.hpp"

extern char logger_buffer[];
extern char logger_msg_fmt[];
extern char logger_fatal_str[];
extern char logger_newline[];
extern char logger_fatal_fmt[];
extern char logger_heap_exhausted[];
extern char logger_error_str[];
extern char logger_error_fmt[];
extern char logger_warn_str[];
extern char logger_warn_fmt[];
extern char logger_info_str[];
extern char logger_info_fmt[];
extern char logger_debug_str[];
extern char logger_trace_str[];
extern char logger_all_str[];

#define LOG_FATAL(fmt, ...)                                                                                                      \
  {                                                                                                                              \
    static const char logger_str[] IROM_TEXT ALIGNED_4 = fmt;                                                                    \
    if ((esplog.get_serial_level() >= LOG_LEV_FATAL) || (esplog.get_memory_level() >= LOG_LEV_FATAL))                            \
      os_snprintf_plus(logger_buffer, (LOGGER_BUF_SIZE - 1), logger_str, ##__VA_ARGS__);                                         \
    if (esplog.get_serial_level() >= LOG_LEV_FATAL)                                                                              \
      os_printf_plus(logger_msg_fmt, logger_fatal_str, logger_buffer, logger_newline);                                           \
    if (esplog.get_memory_level() >= LOG_LEV_FATAL)                                                                              \
    {                                                                                                                            \
      uint32 timestamp = esp_sntp.get_timestamp();                                                                               \
      Heap_chunk json_ptr(32 + 24 + os_strlen(logger_buffer), dont_free);                                                        \
      if (json_ptr.ref != NULL)                                                                                                  \
      {                                                                                                                          \
        os_snprintf_plus(json_ptr.ref, (LOGGER_BUF_SIZE - 1), logger_fatal_fmt, esp_sntp.get_timestr(timestamp), logger_buffer); \
        esplog.add_log(json_ptr.ref);                                                                                            \
      }                                                                                                                          \
      else                                                                                                                       \
        os_printf_plus(logger_heap_exhausted);                                                                                   \
    }                                                                                                                            \
  }

#define LOG_ERROR(fmt, ...)                                                                                                      \
  {                                                                                                                              \
    static const char logger_str[] IROM_TEXT ALIGNED_4 = fmt;                                                                    \
    if ((esplog.get_serial_level() >= LOG_LEV_ERROR) || (esplog.get_memory_level() >= LOG_LEV_ERROR))                            \
      os_snprintf_plus(logger_buffer, (LOGGER_BUF_SIZE - 1), logger_str, ##__VA_ARGS__);                                         \
    if (esplog.get_serial_level() >= LOG_LEV_ERROR)                                                                              \
      os_printf_plus(logger_msg_fmt, logger_error_str, logger_buffer, logger_newline);                                           \
    if (esplog.get_memory_level() >= LOG_LEV_ERROR)                                                                              \
    {                                                                                                                            \
      uint32 timestamp = esp_sntp.get_timestamp();                                                                               \
      Heap_chunk json_ptr(32 + 24 + os_strlen(logger_buffer), dont_free);                                                        \
      if (json_ptr.ref != NULL)                                                                                                  \
      {                                                                                                                          \
        os_snprintf_plus(json_ptr.ref, (LOGGER_BUF_SIZE - 1), logger_error_fmt, esp_sntp.get_timestr(timestamp), logger_buffer); \
        esplog.add_log(json_ptr.ref);                                                                                            \
      }                                                                                                                          \
      else                                                                                                                       \
        os_printf_plus(logger_heap_exhausted);                                                                                   \
    }                                                                                                                            \
  }

#define LOG_WARNING(fmt, ...)                                                                                                   \
  {                                                                                                                             \
    static const char logger_str[] IROM_TEXT ALIGNED_4 = fmt;                                                                   \
    if ((esplog.get_serial_level() >= LOG_LEV_WARN) || (esplog.get_memory_level() >= LOG_LEV_WARN))                             \
      os_snprintf_plus(logger_buffer, (LOGGER_BUF_SIZE - 1), logger_str, ##__VA_ARGS__);                                        \
    if (esplog.get_serial_level() >= LOG_LEV_WARN)                                                                              \
      os_printf_plus(logger_msg_fmt, logger_warn_str, logger_buffer, logger_newline);                                           \
    if (esplog.get_memory_level() >= LOG_LEV_WARN)                                                                              \
    {                                                                                                                           \
      uint32 timestamp = esp_sntp.get_timestamp();                                                                              \
      Heap_chunk json_ptr(32 + 24 + os_strlen(logger_buffer), dont_free);                                                       \
      if (json_ptr.ref != NULL)                                                                                                 \
      {                                                                                                                         \
        os_snprintf_plus(json_ptr.ref, (LOGGER_BUF_SIZE - 1), logger_warn_fmt, esp_sntp.get_timestr(timestamp), logger_buffer); \
        esplog.add_log(json_ptr.ref);                                                                                           \
      }                                                                                                                         \
      else                                                                                                                      \
        os_printf_plus(logger_heap_exhausted);                                                                                  \
    }                                                                                                                           \
  }

#define LOG_INFO(fmt, ...)                                                                                                      \
  {                                                                                                                             \
    static const char logger_str[] IROM_TEXT ALIGNED_4 = fmt;                                                                   \
    if ((esplog.get_serial_level() >= LOG_LEV_INFO) || (esplog.get_memory_level() >= LOG_LEV_INFO))                             \
      os_snprintf_plus(logger_buffer, (LOGGER_BUF_SIZE - 1), logger_str, ##__VA_ARGS__);                                        \
    if (esplog.get_serial_level() >= LOG_LEV_INFO)                                                                              \
      os_printf_plus(logger_msg_fmt, logger_info_str, logger_buffer, logger_newline);                                           \
    if (esplog.get_memory_level() >= LOG_LEV_INFO)                                                                              \
    {                                                                                                                           \
      uint32 timestamp = esp_sntp.get_timestamp();                                                                              \
      Heap_chunk json_ptr(32 + 24 + os_strlen(logger_buffer), dont_free);                                                       \
      if (json_ptr.ref != NULL)                                                                                                 \
      {                                                                                                                         \
        os_snprintf_plus(json_ptr.ref, (LOGGER_BUF_SIZE - 1), logger_info_fmt, esp_sntp.get_timestr(timestamp), logger_buffer); \
        esplog.add_log(json_ptr.ref);                                                                                           \
      }                                                                                                                         \
      else                                                                                                                      \
        os_printf_plus(logger_heap_exhausted);                                                                                  \
    }                                                                                                                           \
  }

#define LOG_DEBUG(fmt, ...)                                                              \
  {                                                                                      \
    static const char logger_str[] IROM_TEXT ALIGNED_4 = fmt;                            \
    if ((esplog.get_serial_level() >= LOG_LEV_DEBUG))                                    \
    {                                                                                    \
      os_snprintf_plus(logger_buffer, (LOGGER_BUF_SIZE - 1), logger_str, ##__VA_ARGS__); \
      os_printf_plus(logger_msg_fmt, logger_debug_str, logger_buffer, logger_newline);   \
    }                                                                                    \
  }

#define LOG_TRACE(fmt, ...)                                                              \
  {                                                                                      \
    static const char logger_str[] IROM_TEXT ALIGNED_4 = fmt;                            \
    if ((esplog.get_serial_level() >= LOG_LEV_TRACE))                                    \
    {                                                                                    \
      os_snprintf_plus(logger_buffer, (LOGGER_BUF_SIZE - 1), logger_str, ##__VA_ARGS__); \
      os_printf_plus(logger_msg_fmt, logger_trace_str, logger_buffer, logger_newline);   \
    }                                                                                    \
  }

#define LOG_ALL(fmt, ...)                                                                \
  {                                                                                      \
    static const char logger_str[] IROM_TEXT ALIGNED_4 = fmt;                            \
    if ((esplog.get_serial_level() >= LOG_LEV_ALL))                                      \
    {                                                                                    \
      os_snprintf_plus(logger_buffer, (LOGGER_BUF_SIZE - 1), logger_str, ##__VA_ARGS__); \
      os_printf_plus(logger_msg_fmt, logger_all_str, logger_buffer, logger_newline);     \
    }                                                                                    \
  }

#undef os_sprintf
#define os_sprintf(buf, fmt, ...)                             \
  {                                                           \
    static const char logger_str[] IROM_TEXT ALIGNED_4 = fmt; \
    os_sprintf_plus(buf, logger_str, ##__VA_ARGS__);          \
  }

#undef os_snprintf
#define os_snprintf(buf, fmt, ...)                            \
  {                                                           \
    static const char logger_str[] IROM_TEXT ALIGNED_4 = fmt; \
    os_snprintf_plus(buf, logger_str, ##__VA_ARGS__);         \
  }

#ifdef DEBUG_TRACE
#define debug(fmt, ...)                 \
  {                                     \
    os_printf_plus(fmt, ##__VA_ARGS__); \
  }
#else
#define debug(fmt, ...)
#endif

#ifdef DEBUG_TRACE
#define e_log(EVENT, evnt_code, param) \
  {                                    \
    event_log(ERROR, err_code, param); \
    os_printf(fmt, ##__VA_ARGS__);     \
  }
#else
#define e_log(EVENT, evnt_code, param) \
  {                                      \
    event_log(ERROR, err_code, param);   \
  }
#endif

#endif