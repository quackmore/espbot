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

#include "espbot_config.hpp"
#include "espbot_diagnostic.hpp"
#include "espbot_event_codes.h"
#include "espbot_global.hpp"
#include "espbot_json.hpp"
#include "espbot_mem_mon.hpp"
#include "spiffs_esp8266.hpp"

#define FILE_TO_JSON_OK 0
#define FILE_TO_JSON_ERROR 1

File_to_json::File_to_json(const char *t_filename)
{
    m_filename = (char *)t_filename;
    m_cache = NULL;
    m_value_str = NULL;
    m_value_len = 0;
}

File_to_json::~File_to_json()
{
    if (m_cache)
        delete[] m_cache;
    if (m_value_str)
        delete[] m_value_str;
}

bool File_to_json::exists(void)
{
    return (Ffile::exists(&espfs, m_filename));
}

int File_to_json::find_string(const char *t_string)
{
    if (m_value_str)
        delete[] m_value_str;
    m_value_str = NULL;
    m_value_len = 0;
    if (!espfs.is_available())
    {
        esp_diag.error(FILE_TO_JSON_FS_NOT_AVAILABLE);
        ERROR("File_to_json::find_string FS not available");
        return FILE_TO_JSON_ERROR;
    }
    if (m_cache == NULL) // file content not cached yet
    {
        if (!Ffile::exists(&espfs, m_filename))
        {
            esp_diag.error(FILE_TO_JSON_FILE_NOT_FOUND);
            ERROR("File_to_json::find_string file not found");
            return FILE_TO_JSON_ERROR;
        }
        Ffile cfgfile(&espfs, m_filename);
        if (!cfgfile.is_available())
        {
            esp_diag.error(FILE_TO_JSON_CANNOT_OPEN_FILE);
            ERROR("File_to_json::find_string cannot open file %s", m_filename);
            return FILE_TO_JSON_ERROR;
        }
        int cache_size = Ffile::size(&espfs, m_filename) + 1;
        m_cache = new char[cache_size];
        if (m_cache == NULL)
        {
            esp_diag.error(FILE_TO_JSON_HEAP_EXHAUSTED, cache_size);
            ERROR("File_to_json::find_string heap exhausted %d", cache_size);
            return FILE_TO_JSON_ERROR;
        }
        cfgfile.n_read(m_cache, Ffile::size(&espfs, m_filename));
    }
    // file content has been cached
    Json_str j_str(m_cache, os_strlen(m_cache));
    if (j_str.syntax_check() != JSON_SINTAX_OK)
    {
        esp_diag.error(FILE_TO_JSON_CANNOT_PARSE_JSON);
        ERROR("File_to_json::find_string json err");
        return FILE_TO_JSON_ERROR;
    }
    while (j_str.find_next_pair() == JSON_NEW_PAIR_FOUND)
    {
        if (os_strlen(t_string) != j_str.get_cur_pair_string_len())
            continue;
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
                esp_diag.error(FILE_TO_JSON_HEAP_EXHAUSTED, (m_value_len + 1));
                ERROR("File_to_json::find_string heap exhausted %d", (m_value_len + 1));
                return FILE_TO_JSON_ERROR;
            }
        }
    }
    // esp_diag.error(FILE_TO_JSON_PAIR_NOT_FOUND);
    DEBUG("File_to_json::find_string %s not found", t_string);
    return FILE_TO_JSON_ERROR;
    espmem.stack_mon();
}

char *File_to_json::get_value(void)
{
    return m_value_str;
}

int File_to_json::get_value_len(void)
{
    return m_value_len;
}