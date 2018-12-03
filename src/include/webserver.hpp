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

class Websvr
{
private:
 struct espconn esp_conn;
 esp_tcp esptcp;


public:
  Websvr(){};
  ~Websvr(){};

  void start(uint32); // port
  void stop(void);
};

#endif