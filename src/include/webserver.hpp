/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */

#ifndef __WEBSERVER_HPP__
#define __WEBSERVER_HPP__

extern "C"
{
#include "c_types.h"
#include "espconn.h"
}

#define SERVER_PORT 80

// HTTP status codes
#define HTTP_OK 200
#define HTTP_CREATED 201
#define HTTP_ACCEPTED 202
#define HTTP_BAD_REQUEST 400
#define HTTP_UNAUTHORIZED 401
#define HTTP_FORBIDDEN 403
#define HTTP_NOT_FOUND 404
#define HTTP_SERVER_ERROR 500

#define HTTP_CONTENT_TEXT "text/html"
#define HTTP_CONTENT_JSON "application/json"

//
// variables and methods for managing http responses
// exposed for coding http controllers
//

struct http_header
{
  int code;
  char *content_type;
  int content_length;
  int content_range_start;
  int content_range_end;
  int content_range_total;
};

struct http_response
{
  struct espconn *p_espconn;
  char *msg;
  char *remaining_msg;
  int timer_idx;
};

void free_http_response(struct http_response *);

char *error_msg(int code);                 // returns error code description
char *json_error_msg(int code, char *msg); // returns error code as a json obj
// quick format response and send
//    free_msg must be false when passing a "string" allocated into text or data segment
//    free_msg must be true when passing an heap allocated string
void response(struct espconn *p_espconn, int code, char *content_type, char *msg, bool free_msg);
// or
// format header string
char *format_header(struct http_header *);
// send_response will take care of splitting the message according to the buffer size
// and will repeadetely call send_response_buffer
void send_response(struct http_response *);
// send_response_buffer will manage calling espconn_send avoiding new calls before completion
void send_response_buffer(struct espconn *p_espconn, char *msg);

int get_free_split_msg_timer(void); // return <0 idx when no available timer is found
os_timer_t *get_split_msg_timer(int);
void free_split_msg_timer(int);

//
// variables and methods for managing http requests
// exposed for coding http routes
//

typedef enum
{
  HTTP_GET = 0,
  HTTP_POST,
  HTTP_PUT,
  HTTP_PATCH,
  HTTP_DELETE,
  HTTP_UNDEFINED
} Html_methods;

class Html_parsed_req
{
public:
  Html_parsed_req();
  ~Html_parsed_req();
  bool no_header_message; // tells if HTTP request contains only POST content
                          // (a POST msg was splitted into two different messages
                          // the first with the header the second with the content
                          // e.g. like Safari browser does)
  Html_methods req_method;
  char *url;
  int content_len;
  char *req_content;
};

//
// HTTP server
//

typedef enum
{
  up = 0,
  down
} Websvr_status;

class Websvr
{
public:
  Websvr(){};
  ~Websvr(){};

  void start(uint32); // port
  void stop(void);
  Websvr_status get_status(void);
  void set_response_max_size(int);
  int get_response_max_size(void);

private:
  Websvr_status m_status;
  struct espconn m_esp_conn;
  esp_tcp m_esptcp;
  int m_send_response_max_size;
};

#endif