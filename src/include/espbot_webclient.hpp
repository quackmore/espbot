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

typedef struct
{
  int http_code;
  int content_range_start;
  int content_range_end;
  int content_range_size;
  int content_len;
  char *body;
} Html_parsed_response;

typedef enum
{
  WEBCLNT_RESPONSE_READY = 0,
  WEBCLNT_DISCONNECTED,
  WEBCLNT_CONNECTING,
  WEBCLNT_CONNECTED,
  WEBCLNT_WAITING_RESPONSE,
  WEBCLNT_CONNECT_FAILURE,
  WEBCLNT_CONNECT_TIMEOUT,
  WEBCLNT_RESPONSE_ERROR,
  WEBCLNT_RESPONSE_TIMEOUT
} Webclnt_status_type;

class Webclnt
{
private:
  struct espconn m_esp_conn;
  esp_tcp m_esptcp;
  Webclnt_status_type m_status;
  char *m_msg;

public:
  Webclnt(){};
  ~Webclnt(){};

  void init(void);
  void connect(struct ip_addr, uint32);
  void connected(void);
  void disconnect(void);
  void send_req(char *);
  Webclnt_status_type get_status(void);
  void update_status(Webclnt_status_type);
  Html_parsed_response *m_response;
  void free_response(void);
};

#endif