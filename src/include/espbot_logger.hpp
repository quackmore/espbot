/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */
#ifndef __LOGGER_HPP__
#define __LOGGER_HPP__

extern "C"
{
#include "c_types.h"
}

#include "espbot_list.hpp"

#define LOG_LEV_OFF 0
#define LOG_LEV_FATAL 1
#define LOG_LEV_ERROR 2
#define LOG_LEV_WARN 3
#define LOG_LEV_INFO 4
#define LOG_LEV_DEBUG 5
#define LOG_LEV_TRACE 6
#define LOG_LEV_ALL 7
#define LOGGER_BUF_SIZE 256

// found somewhere on www.esp8266.com
// 3fffeb30 and 3fffffff is the designated area for the user stack

#define DEBUG_MAX_SAVED_ERRORS 10
#define DEBUG_MAX_FILE_SAVED_ERRORS 20

#define CFG_OK 0
#define CFG_REQUIRES_UPDATE 1
#define CFG_ERROR 2

class Logger
{
private:
  int m_serial_level;
  int m_memory_level;

  List<char> *m_log;

  int restore_cfg(void);          // return CFG_OK on success, otherwise CFG_ERROR
  int saved_cfg_not_updated(void); // return CFG_OK when cfg does not require update
                                  // return CFG_REQUIRES_UPDATE when cfg require update
                                  // return CFG_ERROR otherwise
  int save_cfg(void);             // return CFG_OK on success, otherwise CFG_ERROR

public:
  Logger(){};
  ~Logger(){};

  void init_cfg();
  void essential_init();
  int get_serial_level(void);
  int get_memory_level(void);
  void set_levels(char t_serial_level, char t_memory_level);

  void fatal(const char *, ...);
  void error(const char *, ...);
  void warn(const char *, ...);
  void info(const char *, ...);
  void debug(const char *, ...);
  void trace(const char *, ...);
  void all(const char *, ...);

  char *get_log_head();
  char *get_log_next();
  void add_log(const char *);
  int get_log_size();
};

#endif