/* DHT library

MIT license
written by Adafruit Industries
*/
/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> modified this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */

#ifndef __DHT_H__
#define __DHT_H__

extern "C"
{
#include "c_types.h"
#include "osapi.h"
}

typedef enum
{
  DHT11 = 11,
  DHT22 = 2,
  DHT21 = 21,
  AM2301 = 21
} Dht_type;

typedef enum
{
  Celsius = 0,
  Fahrenheit
} Temp_scale;

class Dht
{
public:
  Dht();
  ~Dht();
  // pin           => the gpio pin D1.. D8
  // type          => the sensor type (e.g. DHT22)
  // poll_interval => in seconds (min 2 seconds)
  //                  init will start polling the sensor every poll_interval seconds
  // buffer_length => samples history
  //                  each sample inlude: - timestamp
  //                                      - temperature value
  //                                      - humidity value
  //                                      - invalid flag (true meaning the values are not affordable)
  void init(int pin, Dht_type type, int poll_interval, int buffer_length);
  float get_temperature(Temp_scale scale, int idx = 0); // idx = 0 => latest sample
  float get_humidity(int idx = 0);                      // idx = 1 => previous sample
  uint32_t get_timestamp(int idx = 0);                  // ...
  bool get_invalid(int idx = 0);                        // ...

  // this is private but into public section
  // for making variables accessible to timer callback function
  uint8_t m_data[5];
  int m_pin;
  Dht_type m_type;
  int m_poll_interval;
  os_timer_t m_poll_timer;
  struct do_seq *m_dht_out_sequence;
  struct di_seq *m_dht_in_sequence;
  int *m_temperature_buffer;
  int *m_humidity_buffer;
  bool *m_invalid_buffer;
  uint32_t *m_timestamp_buffer;
  int m_max_buffer_size;
  int m_buffer_idx;
  bool m_retry;
};

#endif