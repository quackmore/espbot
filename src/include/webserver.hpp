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

char *error_msg(int code);                 // returns error code description
char *json_error_msg(int code, char *msg); // returns error code as a json obj
void response(struct espconn *p_espconn, int code, char *content_type, char *msg, bool free_msg);

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

typedef enum
{
  up = 0,
  down
} Websvr_status;

struct http_route_struct
{
  char *url;
  int url_n_check;
  Html_methods method;
  char *(controller)(Html_parsed_req *);
};

class Websvr
{
public:
  Websvr(){};
  ~Websvr(){};

  void start(uint32); // port
  void stop(void);
  Websvr_status get_status(void);

  void add_route(struct http_route_struct);

private:
  Websvr_status m_status;
  struct espconn m_esp_conn;
  esp_tcp m_esptcp;

  struct http_route_struct *m_http_routes;
  int m_http_routes_cnt;
};

#endif