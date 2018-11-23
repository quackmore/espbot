/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */
#ifndef __DEBUG_HPP__
#define __DEBUG_HPP__

extern "C"
{
#include "c_types.h"
}

#define LOG_OFF 0
#define LOG_FATAL 1
#define LOG_ERROR 2
#define LOG_WARN 3
#define LOG_INFO 4
#define LOG_DEBUG 5
#define LOG_TRACE 6
#define LOG_ALL 7
#define LOG_BUF_SIZE 256

// found somewhere on www.esp8266.com
// 3fffeb30 and 3fffffff is the designated area for the user stack

#define DEBUG_MAX_SAVED_ERRORS 20
#define DEBUG_MAX_FILE_SAVED_ERRORS 20

class Dbggr
{
private:
  uint32 m_min_heap_size;

public:
  Dbggr(){};
  ~Dbggr(){};

  void init(void);
  void check_heap_size(void);
  uint32 get_mim_heap_size(void);
};

class Logger
{
private:
  int m_serial_level;
  int m_file_level;

  int get_saved_cfg(void);
  int save_cfg(void);

public:
  Logger(){};
  ~Logger(){};

  void init();
  int get_serial_level(void);
  int get_file_level(void);
  void set_levels(char t_serial_level,char t_file_level);

  void fatal(const char *, ...);
  void error(const char *, ...);
  void warn(const char *, ...);
  void info(const char *, ...);
  void debug(const char *, ...);
  void trace(const char *, ...);
  void all(const char *, ...);
};

class Profiler
{
private:
  char *m_msg;
  uint32_t m_start_time_us;
  uint32_t m_stop_time_us;

public:
  Profiler(char *); // pass the message to be printed
                    // constructor will start the timer
  ~Profiler();      // destructor will stop the timer and print elapsed msg to serial
  // EXAMPLE
  // {
  //    Profiler esp_profiler;
  //    ...
  //    this is the code you want to profile
  //    place it into a block
  //    and declare a Profiler object at the beginning
  //    ...
  // }
};

#endif