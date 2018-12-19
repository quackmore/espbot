/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */
#ifndef __ESPBOT_HPP__
#define __ESPBOT_HPP__

extern "C"
{
#include "osapi.h"
}

#define SIG_STAMODE_GOT_IP 0
#define SIG_STAMODE_DISCONNECTED 1
#define SIG_SOFTAPMODE_STACONNECTED 2
#define SIG_SOFTAPMODE_STADISCONNECTED 3

class Espbot
{
private:
  char m_name[32];
  // espbot task
  static const int QUEUE_LEN = 8;
  os_event_t *m_queue;
  // heartbeat timer
  static const int HEARTBEAT_PERIOD = 60000;
  os_timer_t m_heartbeat;

  int restore_cfg(void);          // return CFG_OK on success, otherwise CFG_ERROR
  int saved_cfg_not_update(void); // return CFG_OK when cfg does not require update
                                  // return CFG_REQUIRES_UPDATE when cfg require update
                                  // return CFG_ERROR otherwise
  int save_cfg(void);             // return CFG_OK on success, otherwise CFG_ERROR

protected:
public:
  Espbot(){};
  ~Espbot(){};
  void init(void);
  void reset(void);
  uint32 get_chip_id(void);
  uint8 get_boot_version(void);
  const char *get_sdk_version(void);
  char *get_alias(void);
  char *get_version(void);
  char *get_name(void);
  void set_name(char *); // requires string
};

#endif
