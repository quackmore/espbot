/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */
#ifndef __DIAGNOSTIC_HPP__
#define __DIAGNOSTIC_HPP__

extern "C"
{
#include "c_types.h"
}

#define EVNT_FATAL 0x01
#define EVNT_ERROR 0x02
#define EVNT_WARN 0x04
#define EVNT_INFO 0x08
#define EVNT_DEBUG 0x10
#define EVNT_TRACE 0x20

#define EVNT_QUEUE_SIZE 40 // 40 * sizeof(strut dia_event) => 480 bytes

#define DIA_LED ESPBOT_D4

struct dia_event
{
  uint32 timestamp;
  char ack;
  char type;
  int code;
  uint32 value;
};

class Espbot_diag
{
private:
  struct dia_event _evnt_queue[EVNT_QUEUE_SIZE];
  int _event_count;
  char _last_event;
  char _diag_led_mask;  // bitmask 00?? ????
                        //           || ||||_ 1 -> show FATAL events on led
                        //           || |||__ 1 -> show ERROR events on led
                        //           || ||___ 1 -> show WARNING events on led
                        //           || |____ 1 -> show INFO events on led
                        //           ||______ 1 -> show DEBUG events on led
                        //           |_______ 1 -> show TRACE events on led
                        // setting _diag_led_mask will avoid any led control
  void add_event(char type, int code, uint32 value);

public:
  Espbot_diag(){};
  ~Espbot_diag(){};

  void init(void);

  void fatal(int code, uint32 value = 0); // calling one of these functions
  void error(int code, uint32 value = 0); // will add a new event to the event queue
  void warn(int code, uint32 value = 0);  // the new event will be marked as
  void info(int code, uint32 value = 0);  // not acknoledged
  void debug(int code, uint32 value = 0); // and the diagnostic led will be turned on
  void trace(int code, uint32 value = 0); // (if enabled)

  int get_max_events_count(void);           // useful for iterations
  struct dia_event *get_event(int idx = 0); // return a pointer to an event (NULL if no event exists)
                                            // idx=0 -> last event
                                            // idx=1 -> previous event
  int get_unack_events(void);               // return the count of not acknoeledge events
  void ack_events(void);                    // acknoledge all the saved events
  char get_led_mask(void);                  //
  void set_led_mask(char);                  //
};

#endif