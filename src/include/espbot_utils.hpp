/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */
#ifndef __ESPBOT_UTILS_HPP__
#define __ESPBOT_UTILS_HPP__

extern "C"
{
#include "osapi.h"
#include "ip_addr.h"

  int atoi(char *);
  int atoh(char *);
}

// default library
// int atoi(char *);
// int atoh(char *);

// my own
extern "C"
{
  void decodeUrlStr(char *);
  void atoipaddr(struct ip_addr *ip, char *str);
  int get_rand_int(int max_value);
}

class Str_list
{
private:
  struct List_el
  {
    char *content;
    bool to_be_free;
    struct List_el *next;
    struct List_el *prev;
  };
  struct List_el *m_head;
  struct List_el *m_tail;
  struct List_el *m_cursor;
  int m_size;
  int m_max_size; // if -1 there is no max size
                  // if X then once the size grows to up X
                  //      a new push will pop an element to the other side

public:
  Str_list(int t_max_size);
  ~Str_list();
  void init(int t_max_size); // same as constructor
  int size();
  void push_back(char *, bool);
  void pop_front();
  char *get_head();
  char *get_tail();
  char *next();
  char *prev();
};

typedef enum
{
  free = 0,
  dont_free
} Free_opt;

class Heap_chunk
{
private:
  Free_opt m_to_be_free;

public:
  Heap_chunk(int len, Free_opt free_opt = free); // takes string len and
                                                 //     free      - will free memory on variable destruction
                                                 //     dont_free - won't free memory on variable destruction
  ~Heap_chunk();
  char *ref; // string reference
  int len(void);

  //        EXAMPLE:
  //
  //        Heap_chunk tmp_str(128);
  //        if (tmp_str.ref)
  //        {
  //            os_strncpy(tmp_port.ref, "source", source_length);
  //        }
  //        else
  //        {
  //            ... not enough heap memory ...
  //        }
};

#endif