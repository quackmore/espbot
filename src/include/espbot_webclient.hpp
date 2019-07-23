/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */

#ifndef __WEBCLIENT_HPP__
#define __WEBCLIENT_HPP__

extern "C"
{
#include "c_types.h"
#include "espconn.h"
}

#include "espbot_http.hpp"

#define WEBCLNT_CONNECTION_TIMEOUT 10000
#define WEBCLNT_SEND_REQ_TIMEOUT 2000

typedef enum
{
  WEBCLNT_DISCONNECTED = 1,
  WEBCLNT_CONNECT_FAILURE,
  WEBCLNT_CONNECT_TIMEOUT,
  WEBCLNT_CONNECTING,
  WEBCLNT_CONNECTED,
  WEBCLNT_CANNOT_SEND_REQUEST,
  WEBCLNT_WAITING_RESPONSE,
  WEBCLNT_RESPONSE_ERROR,
  WEBCLNT_RESPONSE_TIMEOUT,
  WEBCLNT_RESPONSE_READY
} Webclnt_status_type;

class Webclnt
{
private:
  struct espconn m_esp_conn;
  esp_tcp m_esptcp;
  struct ip_addr m_host;
  uint32 m_port;
  Webclnt_status_type m_status;

  void (*m_completed_func)(void *);
  void *m_param;
  void format_request(char *);

public:
  Webclnt();
  ~Webclnt();

  os_timer_t m_connect_timeout_timer;
  os_timer_t m_send_req_timeout_timer;

  char *request;
  Http_parsed_response *parsed_response;

  // connect will temporary change webclient status to WEBCLNT_CONNECTING
  // and will end up into one of the following:
  // WEBCLNT_CONNECT_FAILURE: espconn_connect failed (sigh!)
  // WEBCLNT_CONNECTED
  // WEBCLNT_CONNECT_TIMEOUT
  // WEBCLNT_DISCONNECTED (??) not sure so just in case
  void connect(struct ip_addr, uint32, void (*completed_func)(void *), void *param);

  // disconnect will change webclient status to WEBCLNT_DISCONNECTED
  void disconnect(void (*completed_func)(void *), void *param);

  // send_req will only act if status is WEBCLNT_CONNECTED or WEBCLNT_RESPONSE_READY
  // otherwise an error will be logged and status will be set to WEBCLNT_CANNOT_SEND_REQUEST
  //
  // send_req will temporary change webclient status to WEBCLNT_WAITING_RESPONSE
  // and will end up into one of the following:
  // WEBCLNT_CONNECT_FAILURE: espconn_connect failed (sigh!)
  // WEBCLNT_CONNECTED
  // WEBCLNT_CONNECT_TIMEOUT
  // WEBCLNT_DISCONNECTED (??) not sure so just in case
  void send_req(char *msg, void (*completed_func)(void *), void *param);

  Webclnt_status_type get_status(void);

  void update_status(Webclnt_status_type);

  void call_completed_func(void);

  void print_status(void);
};

// Init the webclient <-> espconn association data strucures
void init_webclients_data_stuctures(void);

#endif