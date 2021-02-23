/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */

#include "espbot_cfgfile.hpp"
#include "espbot_diagnostic.hpp"
#include "espbot_event_codes.h"

static char *get_file_content(Cfgfile *ptr, char *&str, char *filename)
{
  int len = Espfile::size(filename) + 1;
  char *tmp_ptr = new char[len];
  if (tmp_ptr)
  {
    os_memset(tmp_ptr, 0, len);
    ptr->n_read(tmp_ptr, len - 1);
  }
  else
  {
    dia_error_evnt(CFGFILE_HEAP_EXHAUSTED, len);
    ERROR("Cfgfile heap exhasted [%d]", len);
    tmp_ptr = (char *) f_str("");
  }
  str = tmp_ptr;
  // TRACE("%s content: %s", filename, tmp_ptr);
  return tmp_ptr;
}

Cfgfile::Cfgfile(char *filename)
    : Espfile(filename)
    , JSONP(get_file_content(this, this->_json_str, filename))
{
}

Cfgfile::~Cfgfile()
{
  if (_json_str)
    delete _json_str;
}