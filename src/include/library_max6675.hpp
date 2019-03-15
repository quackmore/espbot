/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> modified this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */

#ifndef __MAX6675_HPP__
#define __MAX6675_HPP__

extern "C"
{
#include "c_types.h"
#include "osapi.h"
}

#include "library_common_types.hpp"

class Max6675
{
public:
  Max6675();
  ~Max6675();
  // pin           => the gpio pin D1.. D8
  // poll_interval => in seconds (min 2 seconds)
  //                  init will start polling the sensor every poll_interval seconds
  // buffer_length => samples history
  //                  each sample inlude: - timestamp
  //                                      - temperature value
  //                                      - humidity value
  //                                      - invalid flag (true meaning the values are not affordable)
  void init(int cs_pin, int sck_pin, int so_pin, int poll_interval, int buffer_length);
  float get_temperature(Temp_scale scale, int idx = 0); // idx = 0 => latest sample
  uint32_t get_timestamp(int idx = 0);                  // ...
  bool get_invalid(int idx = 0);                        // ...
  
  // this is private but into public section
  // for making variables accessible to timer callback function
  uint16_t m_data;
  int m_cs;
  int m_sck;
  int m_so;
  int m_bit_counter;
  os_timer_t m_read_timer;
  int m_poll_interval;
  os_timer_t m_poll_timer;
  int *m_temperature_buffer;
  uint32_t *m_timestamp_buffer;
  bool *m_invalid_buffer;
  int m_max_buffer_size;
  int m_buffer_idx;
  bool m_retry;
};

#endif