/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */

#ifndef __WIFI_HPP__
#define __WIFI_HPP__

extern "C"
{
#include "osapi.h"
#include "user_interface.h"
}
/*
 * max allowed time for connecting to an AP
 */
#define WIFI_CONNECT_TIMEOUT 15000
#define WIFI_WAIT_BEFORE_RECONNECT 3000

class Wifi
{
private:
  struct softap_config m_ap_config;
  char m_station_ssid[32];
  char m_station_pwd[64];

  scan_config *m_scan_config;
  bool m_scan_completed;
  int m_ap_count;
  char *m_ap_list;

  bool m_timeout_timer_active;
  os_timer_t m_station_connect_timeout;
  os_timer_t m_wait_before_reconnect;

  static void scan_completed(void *arg, STATUS status); // static because used as a callback

  int restore_cfg(void);          // return CFG_OK on success, otherwise CFG_ERROR
  int saved_cfg_not_update(void); // return CFG_OK when cfg does not require update
                                  // return CFG_REQUIRES_UPDATE when cfg require update
                                  // return CFG_ERROR otherwise

public:
  Wifi(){};
  ~Wifi(){};

  void init(void); // Will try to start as STATION:
                   //   IF there is no valid STATION cfg
                   //      or failed to connect
                   //   THEN will switch wifi to STATIONAP
                   //        (if there is a valid STATION cfg
                   //         will keep on trying to connect to AP)
                   // While working as STATION, any connection failure
                   // will switch wifi to STATIONAP
                   // Once an ip address is available

  bool is_timeout_timer_active(void);           // public because needed by wifi_event_handler
  void start_connect_timeout_timer(void);       // public because needed by wifi_event_handler
  void stop_connect_timeout_timer(void);        // public because needed by wifi_event_handler
  void start_wait_before_reconnect_timer(void); // public because needed by wifi_event_handler

  void station_set_ssid(char *t_str, int t_len); // won't save configuraion to flash
  void station_set_pwd(char *t_str, int t_len);  // won't save configuraion to flash
  char *station_get_ssid(void);
  char *station_get_password(void);
  int save_cfg(void); // return 0 on success, otherwise 1 save configuration to flash

  static void switch_to_stationap(void); // static because called by timer exhaustion (pointer required)
  static void connect(void);             // static because called by timer exhaustion (pointer required)

  void scan_for_ap(void);           // start a new AP scan
  bool scan_for_ap_completed(void); // return true if AP scan was completed
  int get_ap_count(void);           // return the number of APs found
  char *get_ap_name(int);           // return the name of AP number xx (from 0 to (ap_count-1))
  void free_ap_list(void);
};

#endif