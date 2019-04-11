/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */

// SDK includes
extern "C"
{
#include "mem.h"
}

#include "espbot_debug.hpp"
#include "espbot_global.hpp"
#include "espbot_config.hpp"
#include "espbot_json.hpp"
#include "spiffs_esp8266.hpp"

#define FILE_TO_JSON_OK 0
#define FILE_TO_JSON_ERROR 1

ICACHE_FLASH_ATTR File_to_json::File_to_json(char *t_filename)
{
    esplog.all("File_to_json::File_to_json\n");
    m_filename = t_filename;
    m_cache = NULL;
    m_value_str = NULL;
    m_value_len = 0;
}

ICACHE_FLASH_ATTR File_to_json::~File_to_json()
{
    esplog.all("File_to_json::~File_to_json\n");
    if (m_cache)
        delete [] m_cache;
    if (m_value_str)
        delete [] m_value_str;
}

bool ICACHE_FLASH_ATTR File_to_json::exists(void)
{
    esplog.all("File_to_json::exists\n");
    return (Ffile::exists(&espfs, m_filename));
}

int ICACHE_FLASH_ATTR File_to_json::find_string(char *t_string)
{
    esplog.all("File_to_json::find_string\n");
    if (m_value_str)
        delete [] m_value_str;
    m_value_str = NULL;
    m_value_len = 0;
    if (espfs.is_available())
    {
        if (m_cache == NULL) // file content not cached yet
        {
            if (!Ffile::exists(&espfs, m_filename))
            {
                esplog.error("File_to_json::find_string - file not found\n");
                return FILE_TO_JSON_ERROR;
            }
            Ffile cfgfile(&espfs, m_filename);
            if (cfgfile.is_available())
            {
                m_cache = new char[Ffile::size(&espfs, m_filename) + 1];
                if (m_cache)
                {
                    cfgfile.n_read(m_cache, Ffile::size(&espfs, m_filename));
                }
                else
                {
                    esplog.error("File_to_json::find_string - Not enough heap space\n");
                    return FILE_TO_JSON_ERROR;
                }
            }
            else
            {
                esplog.error("File_to_json::find_string - cannot open file %s\n", m_filename);
                return FILE_TO_JSON_ERROR;
            }
        }
        // file content has been cached
        Json_str j_str(m_cache, os_strlen(m_cache));
        if (j_str.syntax_check() == JSON_SINTAX_OK)
        {
            while (j_str.find_next_pair() == JSON_NEW_PAIR_FOUND)
            {
                if (os_strncmp(t_string, j_str.get_cur_pair_string(), j_str.get_cur_pair_string_len()) == 0)
                {
                    m_value_len = j_str.get_cur_pair_value_len();
                    m_value_str = new char[m_value_len + 1];
                    if (m_value_str)
                    {
                        os_strncpy(m_value_str, j_str.get_cur_pair_value(), j_str.get_cur_pair_value_len());
                        return FILE_TO_JSON_OK;
                    }
                    else
                    {
                        esplog.error("File_to_json::find_string - Not enough heap space\n");
                        return FILE_TO_JSON_ERROR;
                    }
                }
            }
            esplog.error("File_to_json::find_string - string not found\n");
            return FILE_TO_JSON_ERROR;
        }
        else
        {
            esplog.error("File_to_json::find_string - cannot parse json string\n");
            return FILE_TO_JSON_ERROR;
        }
        espmem.stack_mon();
    }
    else
    {
        esplog.error("File_to_json::find_string - file system is not available\n");
        return FILE_TO_JSON_ERROR;
    }
}

char ICACHE_FLASH_ATTR *File_to_json::get_value(void)
{
    esplog.all("File_to_json::get_value\n");
    return m_value_str;
}

int ICACHE_FLASH_ATTR File_to_json::get_value_len(void)
{
    esplog.all("File_to_json::get_value_len\n");
    return m_value_len;
}