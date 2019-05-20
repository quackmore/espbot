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

#include "espbot_queue.hpp"

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

class Http_header
{
public:
  Http_header();
  ~Http_header();
  int m_code;
  char *m_content_type;
  char *m_acrh;
  char *m_origin;
  int m_content_length;
  int m_content_range_start;
  int m_content_range_end;
  int m_content_range_total;
};

struct http_response
{
  struct espconn *p_espconn;
  char *msg;
};

struct http_split_response
{
  struct espconn *p_espconn;
  char *content;
  int content_size;
  int content_transferred;
  void (*action_function)(struct http_split_response *);
};

extern Queue<struct http_split_response> *pending_response;

void webserver_check_pending_response(void);

char *error_msg(int code);                 // returns error code description
char *json_error_msg(int code, char *msg); // returns error code as a json obj

// quick format response and send
//    free_msg must be false when passing a "string" allocated into text or data segment
//    free_msg must be true when passing an heap allocated string
void response(struct espconn *p_espconn, int code, char *content_type, char *msg, bool free_msg);

// or
// format header string
char *format_header(class Http_header *);
// send_response will take care of splitting the message according to the buffer size
// and will repeadetely call send_response_buffer
// will try to free the msg buffer after is has been sent
void send_response(struct espconn *p_espconn, char *msg);
// send_response_buffer will manage calling espconn_send avoiding new calls before completion
void send_response_buffer(struct espconn *p_espconn, char *msg);

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
  HTTP_OPTIONS,
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
  char *acrh;
  char *origin;
  int h_content_len;
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

  void init(void);
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