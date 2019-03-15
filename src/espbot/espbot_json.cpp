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
#include "osapi.h"
}

#include "espbot_json.hpp"
#include "espbot_global.hpp"

ICACHE_FLASH_ATTR Json_str::Json_str(char *t_str, int t_len)
{
    esplog.all("Json_str::Json_str\n");
    m_str = t_str;
    m_str_len = t_len;
    m_cursor = m_str;
    m_cur_pair_string = NULL;
    m_cur_pair_string_len = 0;
    m_cur_pair_value_type = JSON_TYPE_ERR;
    m_cur_pair_value = NULL; //
    m_cur_pair_value_len = 0;
}

char ICACHE_FLASH_ATTR *Json_str::find_object_end(char *t_str)
{
    esplog.all("Json_str::find_object_end\n");
    int paired_brackets = 0;
    char *tmp_ptr = t_str;
    espmem.stack_mon();
    while (*tmp_ptr) // looking for '{' and '}'
    {
        if (*tmp_ptr == '{')
            paired_brackets++;
        if (*tmp_ptr == '}')
        {
            paired_brackets--;
            if (paired_brackets == 0)
                return tmp_ptr;
        }
        tmp_ptr++;
    }
    if ((*tmp_ptr == 0) || (paired_brackets > 0))
        return NULL;
}

int ICACHE_FLASH_ATTR Json_str::syntax_check(void)
{
    esplog.all("Json_str::syntax_check\n");
    char *ptr = m_cursor;
    bool another_pair_found;
    espmem.stack_mon();

    m_cur_pair_value_type = JSON_TYPE_ERR;
    while ((ptr - m_str) < m_str_len) // looking for starting '{'
    {
        if (*ptr == '{')
            break;
        if ((*ptr != ' ') && (*ptr != '\r') && (*ptr != '\n'))
            return (ptr - m_str);
        ptr++;
    }
    if ((ptr - m_str) == m_str_len)
        return (ptr - m_str);
    ptr++;
    do
    {
        while ((ptr - m_str) < m_str_len) // looking for starting '"'
        {
            if (*ptr == '"')
                break;
            if ((*ptr != ' ') && (*ptr != '\r') && (*ptr != '\n'))
                return (ptr - m_str + 1);
            ptr++;
        }
        if ((ptr - m_str) == m_str_len)
            return (ptr - m_str + 1);
        ptr++;
        if ((ptr - m_str) < m_str_len) // check that after a '"' actually a string start
        {
            if ((*ptr == '"') || (*ptr == '{') || (*ptr == '}') || (*ptr == ':') || (*ptr == ','))
                return (ptr - m_str + 1);
        }
        else
            return (ptr - m_str + 1);
        ptr++;
        while ((ptr - m_str) < m_str_len) // looking for ending '"'
        {
            if (*ptr == '"')
                break;
            if ((*ptr == '{') || (*ptr == '}') || (*ptr == ':') || (*ptr == ','))
                return (ptr - m_str + 1);
            ptr++;
        }
        if ((ptr - m_str) == m_str_len)
            return (ptr - m_str + 1);
        ptr++;
        while ((ptr - m_str) < m_str_len) // looking for ':'
        {
            if (*ptr == ':')
                break;
            if ((*ptr != ' ') && (*ptr != '\r') && (*ptr != '\n'))
                return (ptr - m_str + 1);
            ptr++;
        }
        if ((ptr - m_str) == m_str_len)
            return (ptr - m_str + 1);
        ptr++;
        while ((ptr - m_str) < m_str_len) // looking for starting '"' or number or object
        {
            if (*ptr == '"') // found a string
            {
                m_cur_pair_value_type = JSON_STRING;
                break;
            }
            if ((*ptr >= '0') && (*ptr <= '9')) // found a number
            {
                m_cur_pair_value_type = JSON_INTEGER;
                break;
            }
            if (*ptr == '{') // found an object
            {
                m_cur_pair_value_type = JSON_OBJECT;
                break;
            }
            if ((*ptr != ' ') && (*ptr != '\r') && (*ptr != '\n'))
                return (ptr - m_str + 1);
            ptr++;
        }
        if ((ptr - m_str) == m_str_len)
            return (ptr - m_str + 1);
        if (m_cur_pair_value_type == JSON_INTEGER)
        {
            ptr++;
            while ((ptr - m_str) < m_str_len) // looking for number end
            {
                if ((*ptr == ',') || (*ptr == '}') || (*ptr == ' ') || (*ptr == '\r') || (*ptr == 'n'))
                    break;
                if ((*ptr >= '0') && (*ptr <= '9'))
                {
                    ptr++;
                    continue;
                }
                return (ptr - m_str + 1);
            }
            if ((ptr - m_str) == m_str_len)
                return (ptr - m_str + 1);
            if ((*ptr == ',') || (*ptr == '}'))
                ptr--; // to make it like string end
        }
        else if (m_cur_pair_value_type == JSON_OBJECT)
        {
            // char *object_start = ptr;
            // char *object_end = find_object_end(object_start);
            // Json_str json_str(object_start, ((object_end - object_start) + 1));
            // int res = json_str.syntax_check();
            // if (res > JSON_SINTAX_OK)
            //     return (ptr - m_str + res);

            char *object_end = find_object_end(ptr);
            Json_str json_str(ptr, ((object_end - ptr) + 1));
            int res = json_str.syntax_check();
            espmem.stack_mon();
            if (res > JSON_SINTAX_OK)
                return (ptr - m_str + res);
            ptr = object_end;
        }
        else
        {
            ptr++;
            if ((ptr - m_str) < m_str_len) // check that after a '"' actually a string start
            {
                if ((*ptr == '"') || (*ptr == '{') || (*ptr == '}') || (*ptr == ':') || (*ptr == ','))
                    return (ptr - m_str + 1);
            }
            else
                return (ptr - m_str + 1);
            ptr++;
            while ((ptr - m_str) < m_str_len) // looking for ending '"'
            {
                if (*ptr == '"')
                    break;
                if ((*ptr == '{') || (*ptr == '}') || (*ptr == ':') || (*ptr == ','))
                    return (ptr - m_str + 1);
                ptr++;
            }
            if ((ptr - m_str) == m_str_len)
                return (ptr - m_str + 1);
        }
        ptr++;
        another_pair_found = false;
        while ((ptr - m_str) < m_str_len) // looking for '}' or ','
        {
            if (*ptr == '}')
                break;
            if (*ptr == ',')
            {
                another_pair_found = true;
                break;
            }
            if ((*ptr != ' ') && (*ptr != '\r') && (*ptr != '\n'))
                return (ptr - m_str + 1);
            ptr++;
        }
        if ((ptr - m_str) == m_str_len)
            return (ptr - m_str + 1);
        if (another_pair_found)
            ptr++;
    } while (another_pair_found);
    ptr++;
    while ((ptr - m_str) < m_str_len) // looking for end of string
    {
        if ((*ptr != ' ') && (*ptr != '\r') && (*ptr != '\n'))
            return (ptr - m_str + 1);
        ptr++;
    }
    return -1; // the syntax is fine
}

Json_pair_type ICACHE_FLASH_ATTR Json_str::find_next_pair(void)
{
    esplog.all("Json_str::find_next_pair\n");
    m_cur_pair_string = NULL;
    m_cur_pair_string_len = 0;
    m_cur_pair_value_type = JSON_TYPE_ERR;
    m_cur_pair_value = NULL;
    m_cur_pair_value_len = 0;
    if (m_cursor == m_str)
    {
        while ((m_cursor - m_str) < m_str_len) // looking for starting '{'
        {
            if (*m_cursor == '{')
                break;
            if ((*m_cursor != ' ') && (*m_cursor != '\r') && (*m_cursor != '\n'))
                return JSON_ERR;
            m_cursor++;
        }
        m_cursor++;
    }
    if ((m_cursor - m_str) == m_str_len)
        return JSON_ERR;
    while ((m_cursor - m_str) < m_str_len) // looking for starting '"'
    {
        if (*m_cursor == '"')
            break;
        if ((*m_cursor != ' ') && (*m_cursor != '\r') && (*m_cursor != '\n'))
            return JSON_ERR;
        m_cursor++;
    }
    if ((m_cursor - m_str) == m_str_len)
        return JSON_ERR;
    m_cursor++;
    if ((m_cursor - m_str) < m_str_len) // check that after a '"' actually a string start
    {
        if ((*m_cursor == '"') || (*m_cursor == '{') || (*m_cursor == '}') || (*m_cursor == ':') || (*m_cursor == ','))
            return JSON_ERR;
        else
            m_cur_pair_string = m_cursor;
    }
    else
        return JSON_ERR;
    m_cursor++;
    while ((m_cursor - m_str) < m_str_len) // looking for ending '"'
    {
        if (*m_cursor == '"')
        {
            m_cur_pair_string_len = (m_cursor - m_cur_pair_string);
            break;
        }
        if ((*m_cursor == '{') || (*m_cursor == '}') || (*m_cursor == ':') || (*m_cursor == ','))
            return JSON_ERR;
        m_cursor++;
    }
    if ((m_cursor - m_str) == m_str_len)
        return JSON_ERR;
    m_cursor++;
    while ((m_cursor - m_str) < m_str_len) // looking for ':'
    {
        if (*m_cursor == ':')
            break;
        if ((*m_cursor != ' ') && (*m_cursor != '\r') && (*m_cursor != '\n'))
            return JSON_ERR;
        m_cursor++;
    }
    if ((m_cursor - m_str) == m_str_len)
        return JSON_ERR;
    m_cursor++;
    while ((m_cursor - m_str) < m_str_len) // looking for starting '"' or number or object
    {
        if (*m_cursor == '"') // found a string
        {
            m_cur_pair_value_type = JSON_STRING;
            break;
        }
        if ((*m_cursor >= '0') && (*m_cursor <= '9')) // found a number
        {
            m_cur_pair_value_type = JSON_INTEGER;
            m_cur_pair_value = m_cursor;
            break;
        }
        if (*m_cursor == '{') // found an object
        {
            m_cur_pair_value_type = JSON_OBJECT;
            m_cur_pair_value = m_cursor;
            break;
        }
        if ((*m_cursor != ' ') && (*m_cursor != '\r') && (*m_cursor != '\n'))
            return JSON_ERR;
        m_cursor++;
    }
    if ((m_cursor - m_str) == m_str_len)
        return JSON_ERR;
    if (m_cur_pair_value_type == JSON_INTEGER)
    {
        m_cursor++;
        while ((m_cursor - m_str) < m_str_len) // looking for number end
        {
            if ((*m_cursor == ',') || (*m_cursor == '}') || (*m_cursor == ' ') || (*m_cursor == '\r') || (*m_cursor == 'n'))
            {
                m_cur_pair_value_len = (m_cursor - m_cur_pair_value);
                break;
            }
            if ((*m_cursor >= '0') && (*m_cursor <= '9'))
            {
                m_cursor++;
                continue;
            }
            return JSON_ERR;
        }
        if ((m_cursor - m_str) == m_str_len)
            return JSON_ERR;
        if ((*m_cursor == ',') || (*m_cursor == '}'))
            m_cursor--; // to make it like string end
    }
    else if (m_cur_pair_value_type == JSON_OBJECT)
    {
        m_cursor = find_object_end(m_cursor);
        m_cur_pair_value_len = m_cursor - m_cur_pair_value;
    }
    else
    {
        m_cursor++;
        if ((m_cursor - m_str) < m_str_len) // check that after a '"' actually a string start
        {
            if ((*m_cursor == '"') || (*m_cursor == '{') || (*m_cursor == '}') || (*m_cursor == ':') || (*m_cursor == ','))
                return JSON_ERR;
        }
        else
            return JSON_ERR;
        m_cur_pair_value = m_cursor;
        m_cursor++;
        while ((m_cursor - m_str) < m_str_len) // looking for ending '"'
        {
            if (*m_cursor == '"')
            {
                m_cur_pair_value_len = m_cursor - m_cur_pair_value;
                break;
            }
            if ((*m_cursor == '{') || (*m_cursor == '}') || (*m_cursor == ':') || (*m_cursor == ','))
                return JSON_ERR;
            m_cursor++;
        }
        if ((m_cursor - m_str) == m_str_len)
            return JSON_ERR;
    }
    m_cursor++;
    while ((m_cursor - m_str) < m_str_len) // looking for ending '}' or ','
    {
        if ((*m_cursor == '}') || (*m_cursor == ','))
            break;
        if ((*m_cursor != ' ') && (*m_cursor != '\r') && (*m_cursor != '\n'))
            return JSON_ERR;
        m_cursor++;
    }
    if ((m_cursor - m_str) == m_str_len)
        return JSON_ERR;
    m_cursor++; // eventually pointing to a new pair
    if (m_cur_pair_string)
        return JSON_NEW_PAIR_FOUND; // found a new pair
    else
        return JSON_NO_NEW_PAIR_FOUND;
}

char ICACHE_FLASH_ATTR *Json_str::get_cur_pair_string(void)
{
    esplog.all("Json_str::get_cur_pair_string\n");
    return m_cur_pair_string;
}

int ICACHE_FLASH_ATTR Json_str::get_cur_pair_string_len(void)
{
    esplog.all("Json_str::get_cur_pair_string_len\n");
    return m_cur_pair_string_len;
}

Json_value_type ICACHE_FLASH_ATTR Json_str::get_cur_pair_value_type(void)
{
    esplog.all("Json_str::get_cur_pair_value_type\n");
    return m_cur_pair_value_type;
}

char ICACHE_FLASH_ATTR *Json_str::get_cur_pair_value(void)
{
    esplog.all("Json_str::get_cur_pair_value\n");
    return m_cur_pair_value;
}

int ICACHE_FLASH_ATTR Json_str::get_cur_pair_value_len(void)
{
    esplog.all("Json_str::get_cur_pair_value_len\n");
    return m_cur_pair_value_len;
}

// char ICACHE_FLASH_ATTR *Json_str::get_cursor(void)
// {
//     return m_cursor;
// }

Json_pair_type ICACHE_FLASH_ATTR Json_str::find_pair(char *t_string)
{
    esplog.all("Json_str::find_pair\n");
    m_cursor = m_str;
    if (syntax_check() == JSON_SINTAX_OK)
    {
        while (find_next_pair() == JSON_NEW_PAIR_FOUND)
        {
            if (os_strncmp(get_cur_pair_string(), t_string, get_cur_pair_string_len()) == 0)
                return JSON_NEW_PAIR_FOUND;
        }
        return JSON_NO_NEW_PAIR_FOUND;
    }
    else
        return JSON_ERR;
}