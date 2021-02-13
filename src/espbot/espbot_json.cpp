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
#include "espbot_utils.hpp"

JSONP::JSONP()
{
    _jstr = NULL;
    _len = 0;
    _cursor = _jstr;
    _cur_name = NULL;
    _cur_name_len = 0;
    _cur_type = JSON_unknown;
    _cur_value = NULL;
    _cur_value_len = 0;
    _err = JSON_empty;
}

JSONP::JSONP(char *t_str)
{
    _jstr = t_str;
    _len = os_strlen(_jstr);
    _cursor = _jstr;
    _cur_name = NULL;
    _cur_name_len = 0;
    _cur_type = JSON_unknown;
    _cur_value = NULL;
    _cur_value_len = 0;
    _err = JSON_noerr;
    _err = syntax_check();
}

JSONP::JSONP(char *t_str, int t_len)
{
    _jstr = t_str;
    _len = t_len;
    _cursor = _jstr;
    _cur_name = NULL;
    _cur_name_len = 0;
    _cur_type = JSON_unknown;
    _cur_value = NULL; //
    _cur_value_len = 0;
    _err = JSON_noerr;
    _err = syntax_check();
}

/**
 * @brief 
 * 
 * @param t_str 
 * @return char* 
 */

static char *find_object_end(char *t_str)
{
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

/**
 * @brief 
 * 
 * @param t_str 
 * @return char* 
 */

static char *find_array_end(char *t_str)
{
    int paired_brackets = 0;
    char *tmp_ptr = t_str;
    espmem.stack_mon();
    while (*tmp_ptr) // looking for '[' and ']'
    {
        if (*tmp_ptr == '[')
            paired_brackets++;
        if (*tmp_ptr == ']')
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

/**
 * @brief 
 * 
 * @return int 
 */

int JSONP::syntax_check(void)
{
    char *ptr = _cursor;
    bool another_pair_found = false;
    espmem.stack_mon();

    _cur_type = JSON_unknown;
    while ((ptr - _jstr) < _len) // looking for starting '{'
    {
        if (*ptr == '{')
            break;
        if ((*ptr != ' ') && (*ptr != '\r') && (*ptr != '\n'))
            return (ptr - _jstr);
        ptr++;
    }
    if ((ptr - _jstr) == _len)
        return (ptr - _jstr);
    ptr++;
    do
    {
        while ((ptr - _jstr) < _len) // looking for starting '"'
        {
            if (*ptr == '"')
                break;
            if ((*ptr == '}') && (!another_pair_found))
                return JSON_noerr; // fine, this is an empty object
            if ((*ptr != ' ') && (*ptr != '\r') && (*ptr != '\n'))
                return (ptr - _jstr + 1);
            ptr++;
        }
        if ((ptr - _jstr) == _len)
            return (ptr - _jstr + 1);
        ptr++;
        if ((ptr - _jstr) < _len) // check that after a '"' actually a string start
        {
            if ((*ptr == '"') || (*ptr == '{') || (*ptr == '}') || (*ptr == '[') || (*ptr == ']') || (*ptr == ':') || (*ptr == ','))
                return (ptr - _jstr + 1);
        }
        else
            return (ptr - _jstr + 1);
        ptr++;
        while ((ptr - _jstr) < _len) // looking for ending '"'
        {
            if (*ptr == '"')
                break;
            if ((*ptr == '{') || (*ptr == '}') || (*ptr == '[') || (*ptr == ']') || (*ptr == ':') || (*ptr == ','))
                return (ptr - _jstr + 1);
            ptr++;
        }
        if ((ptr - _jstr) == _len)
            return (ptr - _jstr + 1);
        ptr++;
        while ((ptr - _jstr) < _len) // looking for ':'
        {
            if (*ptr == ':')
                break;
            if ((*ptr != ' ') && (*ptr != '\r') && (*ptr != '\n'))
                return (ptr - _jstr + 1);
            ptr++;
        }
        if ((ptr - _jstr) == _len)
            return (ptr - _jstr + 1);
        ptr++;
        while ((ptr - _jstr) < _len) // looking for starting '"' or number or object
        {
            if (*ptr == '"') // found a string
            {
                _cur_type = JSON_str;
                break;
            }
            if (((*ptr >= '0') && (*ptr <= '9')) || (*ptr == '-')) // found a number
            {
                _cur_type = JSON_num;
                break;
            }
            if (*ptr == '{') // found an object
            {
                _cur_type = JSON_obj;
                break;
            }
            if (*ptr == '[') // found an array
            {
                _cur_type = JSONP_array;
                break;
            }
            if ((*ptr != ' ') && (*ptr != '\r') && (*ptr != '\n'))
                return (ptr - _jstr + 1);
            ptr++;
        }
        if ((ptr - _jstr) == _len)
            return (ptr - _jstr + 1);
        if (_cur_type == JSON_num)
        {
            ptr++;
            while ((ptr - _jstr) < _len) // looking for number end
            {
                if ((*ptr == ',') || (*ptr == '}') || (*ptr == ' ') || (*ptr == '\r') || (*ptr == '\n'))
                    break;
                if (((*ptr >= '0') && (*ptr <= '9')) || (*ptr == '.'))
                {
                    ptr++;
                    continue;
                }
                return (ptr - _jstr + 1);
            }
            if ((ptr - _jstr) == _len)
                return (ptr - _jstr + 1);
            if ((*ptr == ',') || (*ptr == '}'))
                ptr--; // to make it like string end
        }
        else if (_cur_type == JSON_obj)
        {
            char *object_end = find_object_end(ptr);
            JSONP JSONP(ptr, ((object_end - ptr) + 1));
            int res = JSONP.syntax_check();
            espmem.stack_mon();
            if (res > JSON_noerr)
                return (ptr - _jstr + res);
            ptr = object_end;
        }
        else if (_cur_type == JSONP_array)
        {
            char *array_end = find_array_end(ptr);
            JSONP_ARRAY array_str(ptr, ((array_end - ptr) + 1));
            int res = array_str.getErr();
            espmem.stack_mon();
            if (res > JSON_noerr)
                return (ptr - _jstr + res);
            ptr = array_end;
        }
        else
        {
            ptr++;
            if ((ptr - _jstr) < _len) // check that after a '"' actually a string start
            {
                if ((*ptr == '{') || (*ptr == '}') || (*ptr == '[') || (*ptr == ']') || (*ptr == ':') || (*ptr == ','))
                    return (ptr - _jstr + 1);
            }
            else
                return (ptr - _jstr + 1);
            while ((ptr - _jstr) < _len) // looking for ending '"'
            {
                if (*ptr == '"')
                    break;
                if ((*ptr == '{') || (*ptr == '}') || (*ptr == '[') || (*ptr == ']') || (*ptr == ':') || (*ptr == ','))
                    return (ptr - _jstr + 1);
                ptr++;
            }
            if ((ptr - _jstr) == _len)
                return (ptr - _jstr + 1);
        }
        ptr++;
        another_pair_found = false;
        while ((ptr - _jstr) < _len) // looking for '}' or ','
        {
            if (*ptr == '}')
                break;
            if (*ptr == ',')
            {
                another_pair_found = true;
                break;
            }
            if ((*ptr != ' ') && (*ptr != '\r') && (*ptr != '\n'))
                return (ptr - _jstr + 1);
            ptr++;
        }
        if ((ptr - _jstr) == _len)
            return (ptr - _jstr + 1);
        if (another_pair_found)
            ptr++;
    } while (another_pair_found);
    ptr++;
    while ((ptr - _jstr) < _len) // looking for end of string
    {
        if ((*ptr != ' ') && (*ptr != '\r') && (*ptr != '\n'))
            return (ptr - _jstr + 1);
        ptr++;
    }
    return JSON_noerr; // the syntax is fine
}

/**
 * @brief 
 * 
 * @return int 
 */

int JSONP::find_pair(void)
{
    _cur_name = NULL;
    _cur_name_len = 0;
    _cur_type = JSON_unknown;
    _cur_value = NULL;
    _cur_value_len = 0;
    if (_cursor == _jstr)
    {
        while ((_cursor - _jstr) < _len) // looking for starting '{'
        {
            if (*_cursor == '{')
                break;
            if ((*_cursor != ' ') && (*_cursor != '\r') && (*_cursor != '\n'))
                return JSON_pairNotFound;
            _cursor++;
        }
        _cursor++;
    }
    if ((_cursor - _jstr) == _len)
        return JSON_pairNotFound;
    while ((_cursor - _jstr) < _len) // looking for starting '"'
    {
        if (*_cursor == '"')
            break;
        if ((*_cursor != ' ') && (*_cursor != '\r') && (*_cursor != '\n'))
            return JSON_pairNotFound;
        _cursor++;
    }
    if ((_cursor - _jstr) == _len)
        return JSON_pairNotFound;
    _cursor++;
    if ((_cursor - _jstr) < _len) // check that after a '"' actually a string start
    {
        if ((*_cursor == '"') || (*_cursor == '{') || (*_cursor == '}') || (*_cursor == '[') || (*_cursor == ']') || (*_cursor == ':') || (*_cursor == ','))
            return JSON_pairNotFound;
        else
            _cur_name = _cursor;
    }
    else
        return JSON_pairNotFound;
    _cursor++;
    while ((_cursor - _jstr) < _len) // looking for ending '"'
    {
        if (*_cursor == '"')
        {
            _cur_name_len = (_cursor - _cur_name);
            break;
        }
        if ((*_cursor == '{') || (*_cursor == '}') || (*_cursor == '[') || (*_cursor == ']') || (*_cursor == ':') || (*_cursor == ','))
            return JSON_pairNotFound;
        _cursor++;
    }
    if ((_cursor - _jstr) == _len)
        return JSON_pairNotFound;
    _cursor++;
    while ((_cursor - _jstr) < _len) // looking for ':'
    {
        if (*_cursor == ':')
            break;
        if ((*_cursor != ' ') && (*_cursor != '\r') && (*_cursor != '\n'))
            return JSON_pairNotFound;
        _cursor++;
    }
    if ((_cursor - _jstr) == _len)
        return JSON_pairNotFound;
    _cursor++;
    while ((_cursor - _jstr) < _len) // looking for starting '"' or number or object
    {
        if (*_cursor == '"') // found a string
        {
            _cur_type = JSON_str;
            break;
        }
        if (((*_cursor >= '0') && (*_cursor <= '9')) || (*_cursor == '-')) // found a number
        {
            _cur_type = JSON_num;
            _cur_value = _cursor;
            break;
        }
        if (*_cursor == '{') // found an object
        {
            _cur_type = JSON_obj;
            _cur_value = _cursor;
            break;
        }
        if (*_cursor == '[') // found an array
        {
            _cur_type = JSONP_array;
            _cur_value = _cursor;
            break;
        }
        if ((*_cursor != ' ') && (*_cursor != '\r') && (*_cursor != '\n'))
            return JSON_pairNotFound;
        _cursor++;
    }
    if ((_cursor - _jstr) == _len)
        return JSON_pairNotFound;
    if (_cur_type == JSON_num)
    {
        _cursor++;
        while ((_cursor - _jstr) < _len) // looking for number end
        {
            if ((*_cursor == ',') || (*_cursor == '}') || (*_cursor == ' ') || (*_cursor == '\r') || (*_cursor == '\n'))
            {
                _cur_value_len = (_cursor - _cur_value);
                break;
            }
            if (((*_cursor >= '0') && (*_cursor <= '9')) || (*_cursor == '.'))
            {
                _cursor++;
                continue;
            }
            return JSON_pairNotFound;
        }
        if ((_cursor - _jstr) == _len)
            return JSON_pairNotFound;
        if ((*_cursor == ',') || (*_cursor == '}'))
            _cursor--; // to make it like string end
    }
    else if (_cur_type == JSON_obj)
    {
        _cursor = find_object_end(_cursor);
        _cur_value_len = _cursor - _cur_value + 1;
    }
    else if (_cur_type == JSONP_array)
    {
        _cursor = find_array_end(_cursor);
        _cur_value_len = _cursor - _cur_value + 1;
    }
    else
    {
        _cursor++;
        if ((_cursor - _jstr) < _len) // check that after a '"' actually a string start
        {
            if ((*_cursor == '{') || (*_cursor == '}') || (*_cursor == '[') || (*_cursor == ']') || (*_cursor == ':') || (*_cursor == ','))
                return JSON_pairNotFound;
        }
        else
            return JSON_pairNotFound;
        _cur_value = _cursor;
        while ((_cursor - _jstr) < _len) // looking for ending '"'
        {
            if (*_cursor == '"')
            {
                _cur_value_len = _cursor - _cur_value;
                break;
            }
            if ((*_cursor == '{') || (*_cursor == '}') || (*_cursor == '[') || (*_cursor == ']') || (*_cursor == ':') || (*_cursor == ','))
                return JSON_pairNotFound;
            _cursor++;
        }
        if ((_cursor - _jstr) == _len)
            return JSON_pairNotFound;
    }
    _cursor++;
    while ((_cursor - _jstr) < _len) // looking for ending '}' or ','
    {
        if ((*_cursor == '}') || (*_cursor == ','))
            break;
        if ((*_cursor != ' ') && (*_cursor != '\r') && (*_cursor != '\n'))
            return JSON_pairNotFound;
        _cursor++;
    }
    if ((_cursor - _jstr) == _len)
        return JSON_pairNotFound;
    _cursor++; // eventually pointing to a new pair
    if (_cur_name)
        return JSON_pairFound; // found a new pair
    else
        return JSON_pairNotFound;
}

/**
 * @brief 
 * 
 * @param t_string 
 * @return int 
 */

int JSONP::find_key(const char *t_string)
{
    _cursor = _jstr;
    if (syntax_check() != JSON_noerr)
        return JSON_notFound;
    while (find_pair() == JSON_pairFound)
    {
        if ((_cur_name_len == os_strlen(t_string)) &&
            (os_strncmp(_cur_name, t_string, os_strlen(t_string)) == 0))
            return JSON_found;
    }
    return JSON_notFound;
}

int JSONP::getInt(const char *name)
{
    if (_err != JSON_noerr)
        return 0;
    if (find_key(name) == JSON_notFound)
    {
        _err = JSON_notFound;
        return 0;
    }
    if (_cur_type != JSON_num)
    {
        _err = JSON_typeMismatch;
        return 0;
    }
    // check if value is a float
    int idx;
    for (idx = 0; idx < _cur_value_len; idx++)
        if (_cur_value[idx] == '.')
        {
            _err = JSON_typeMismatch;
            return 0;
        }
    char value_str[16];
    espmem.stack_mon();

    os_memset(value_str, 0, 16);
    if (_cur_value_len > 15)
    {
        _err = JSON_typeMismatch;
        return 0;
    }
    os_strncpy(value_str, _cur_value, _cur_value_len);
    return atoi(value_str);
}

float JSONP::getFloat(const char *name)
{
    if (_err != JSON_noerr)
        return 0.0;
    if (find_key(name) == JSON_notFound)
    {
        _err = JSON_notFound;
        return 0.0;
    }
    if (_cur_type != JSON_num)
    {
        _err = JSON_typeMismatch;
        return 0.0;
    }
    char value_str[64];
    espmem.stack_mon();

    os_memset(value_str, 0, 64);
    if (_cur_value_len > 63)
    {
        _err = JSON_typeMismatch;
        return 0.0;
    }
    os_strncpy(value_str, _cur_value, _cur_value_len);
    return atof(value_str);
}

void JSONP::getStr(const char *name, char *dest, int len)
{
    if (_err != JSON_noerr)
        return;
    if (find_key(name) == JSON_notFound)
    {
        _err = JSON_notFound;
        return;
    }
    if (_cur_type != JSON_str)
    {
        _err = JSON_typeMismatch;
        return;
    }
    if (_cur_value_len > len)
    {
        _err = JSON_typeMismatch;
        return;
    }
    os_memset(dest, 0, len);
    os_strncpy(dest, _cur_value, _cur_value_len);
}

int JSONP::getStrlen(const char *name)
{
    if (_err != JSON_noerr)
        return 0;
    if (find_key(name) == JSON_notFound)
    {
        _err = JSON_notFound;
        return 0;
    }
    if (_cur_type != JSON_str)
    {
        _err = JSON_typeMismatch;
        return 0;
    }
    return _cur_value_len;
}

JSONP JSONP::getObj(const char *name)
{
    JSONP empty_obj;
    if (_err != JSON_noerr)
        return empty_obj;
    if (find_key(name) == JSON_notFound)
    {
        _err = JSON_notFound;
        return empty_obj;
    }
    if (_cur_type != JSON_obj)
    {
        _err = JSON_typeMismatch;
        return empty_obj;
    }
    return JSONP(_cur_value, _cur_value_len);
}

JSONP_ARRAY JSONP::getArray(const char *name)
{
    JSONP_ARRAY empty_obj;
    if (_err != JSON_noerr)
        return empty_obj;
    if (find_key(name) == JSON_notFound)
    {
        _err = JSON_notFound;
        return empty_obj;
    }
    if (_cur_type != JSONP_array)
    {
        _err = JSON_typeMismatch;
        return empty_obj;
    }
    return JSONP_ARRAY(_cur_value, _cur_value_len);
}

void JSONP::clearErr(void)
{
    _err = JSON_noerr;
}

int JSONP::getErr(void)
{
    return _err;
}

#ifdef JSON_TEST
void JSONP::getStr(char *str, int str_len)
{
    os_memset(str, 0, str_len);
    if (_len < str_len)
        os_strncpy(str, _jstr, _len);
    else
        os_strncpy(str, _jstr, (str_len - 1));
}
#endif

// ARRAY  #########################################################################

JSONP_ARRAY::JSONP_ARRAY()
{
    _jstr = NULL;
    _len = 0;
    _cursor = _jstr;
    _el_count = 0;
    _cur_id = -1;
    char *_cur_el = NULL;
    int _cur_len = 0;
    Json_value_type _cur_type = JSON_unknown;

    _err = JSON_empty;
}

JSONP_ARRAY::JSONP_ARRAY(char *t_str)
{
    _jstr = t_str;
    _len = os_strlen(t_str);
    _cursor = _jstr;
    _el_count = 0;
    _cur_id = -1;
    char *_cur_el = NULL;
    int _cur_len = 0;
    Json_value_type _cur_type = JSON_unknown;

    _err = JSON_noerr;
    _err = syntax_check();
}

JSONP_ARRAY::JSONP_ARRAY(char *t_str, int t_len)
{
    _jstr = t_str;
    _len = t_len;
    _cursor = _jstr;
    _el_count = 0;
    _cur_id = -1;
    char *_cur_el = NULL;
    int _cur_len = 0;
    Json_value_type _cur_type = JSON_unknown;

    _err = JSON_noerr;
    _err = syntax_check();
}

int JSONP_ARRAY::syntax_check(void)
{
    char *ptr = _cursor;
    bool another_pair_found = false;
    int tmp_elem_count = 1;
    espmem.stack_mon();

    _cur_type = JSON_unknown;
    while ((ptr - _jstr) < _len) // looking for starting '['
    {
        if (*ptr == '[')
            break;
        if ((*ptr != ' ') && (*ptr != '\r') && (*ptr != '\n'))
            return (ptr - _jstr);
        ptr++;
    }
    if ((ptr - _jstr) == _len)
        return (ptr - _jstr);
    ptr++;
    do
    {
        while ((ptr - _jstr) < _len) // looking for starting '"' or number or object
        {
            if ((*ptr == ']') && (!another_pair_found)) // found an empty array
            {
                _el_count = 0;
                return JSON_noerr; // the syntax is fine
            }
            if (*ptr == '"') // found a string
            {
                _cur_type = JSON_str;
                break;
            }
            if (((*ptr >= '0') && (*ptr <= '9')) || (*ptr == '-')) // found a number
            {
                _cur_type = JSON_num;
                break;
            }
            if (*ptr == '{') // found an object
            {
                _cur_type = JSON_obj;
                break;
            }
            if (*ptr == '[') // found an array
            {
                _cur_type = JSONP_array;
                break;
            }
            if ((*ptr != ' ') && (*ptr != '\r') && (*ptr != '\n'))
                return (ptr - _jstr + 1);
            ptr++;
        }
        if ((ptr - _jstr) == _len)
            return (ptr - _jstr + 1);
        if (_cur_type == JSON_num)
        {
            ptr++;
            while ((ptr - _jstr) < _len) // looking for number end
            {
                if ((*ptr == ',') || (*ptr == '}') || (*ptr == ']') || (*ptr == ' ') || (*ptr == '\r') || (*ptr == '\n'))
                    break;
                if (((*ptr >= '0') && (*ptr <= '9')) || (*ptr == '.'))
                {
                    ptr++;
                    continue;
                }
                return (ptr - _jstr + 1);
            }
            if ((ptr - _jstr) == _len)
                return (ptr - _jstr + 1);
            if ((*ptr == ',') || (*ptr == '}') || (*ptr == ']'))
                ptr--; // to make it like string end
        }
        else if (_cur_type == JSON_obj)
        {
            char *object_end = find_object_end(ptr);
            JSONP obj(ptr, ((object_end - ptr) + 1));
            int res = obj.getErr();
            espmem.stack_mon();
            if (res > JSON_noerr)
                return (ptr - _jstr + res);
            ptr = object_end;
        }
        else if (_cur_type == JSONP_array)
        {
            char *array_end = find_array_end(ptr);
            JSONP_ARRAY array_str(ptr, ((array_end - ptr) + 1));
            int res = array_str.syntax_check();
            espmem.stack_mon();
            if (res > JSON_noerr)
                return (ptr - _jstr + res);
            ptr = array_end;
        }
        else
        {
            ptr++;
            if ((ptr - _jstr) < _len) // check that after a '"' actually a string start
            {
                if ((*ptr == '{') || (*ptr == '}') || (*ptr == '[') || (*ptr == ']') || (*ptr == ':') || (*ptr == ','))
                    return (ptr - _jstr + 1);
            }
            else
                return (ptr - _jstr + 1);
            while ((ptr - _jstr) < _len) // looking for ending '"'
            {
                if (*ptr == '"')
                    break;
                if ((*ptr == '{') || (*ptr == '}') || (*ptr == '[') || (*ptr == ']') || (*ptr == ':') || (*ptr == ','))
                    return (ptr - _jstr + 1);
                ptr++;
            }
            if ((ptr - _jstr) == _len)
                return (ptr - _jstr + 1);
        }
        ptr++;
        another_pair_found = false;
        while ((ptr - _jstr) < _len) // looking for ']' or ','
        {
            if (*ptr == ']')
                break;
            if (*ptr == ',')
            {
                another_pair_found = true;
                tmp_elem_count++;
                break;
            }
            if ((*ptr != ' ') && (*ptr != '\r') && (*ptr != '\n'))
                return (ptr - _jstr + 1);
            ptr++;
        }
        if ((ptr - _jstr) == _len)
            return (ptr - _jstr + 1);
        if (another_pair_found)
            ptr++;
    } while (another_pair_found);
    ptr++;
    while ((ptr - _jstr) < _len) // looking for end of string
    {
        if ((*ptr != ' ') && (*ptr != '\r') && (*ptr != '\n'))
            return (ptr - _jstr + 1);
        ptr++;
    }
    _el_count = tmp_elem_count;
    return JSON_noerr; // the syntax is fine
}

int JSONP_ARRAY::len(void)
{
    // don't repeat the counting every time...
    if (_el_count == 0)
    {
        syntax_check();
    }
    return _el_count;
}

int JSONP_ARRAY::find_elem(int idx)
{
    char *ptr = _cursor;
    bool another_elem_found = false;
    int tmp_elem_count = 0;
    espmem.stack_mon();

    _cur_type = JSON_unknown;
    while ((ptr - _jstr) < _len) // looking for starting '['
    {
        if (*ptr == '[')
            break;
        if ((*ptr != ' ') && (*ptr != '\r') && (*ptr != '\n'))
            return -1;
        ptr++;
    }
    if ((ptr - _jstr) == _len)
        return -1;
    ptr++;
    do
    {
        while ((ptr - _jstr) < _len) // looking for starting '"' or number or object
        {
            if ((*ptr == ']') && (!another_elem_found)) // found an empty array
            {
                return -1;
            }
            if (*ptr == '"') // found a string
            {
                // mark the element start
                _cur_el = ptr + 1;
                _cur_type = JSON_str;
                break;
            }
            if (((*ptr >= '0') && (*ptr <= '9')) || (*ptr == '-')) // found a number
            {
                // mark the element start
                _cur_el = ptr;
                _cur_type = JSON_num;
                break;
            }
            if (*ptr == '{') // found an object
            {
                // mark the element start
                _cur_el = ptr;
                _cur_type = JSON_obj;
                break;
            }
            if (*ptr == '[') // found an array
            {
                // mark the element start
                _cur_el = ptr;
                _cur_type = JSONP_array;
                break;
            }
            if ((*ptr != ' ') && (*ptr != '\r') && (*ptr != '\n'))
                return -1;
            ptr++;
        }
        if ((ptr - _jstr) == _len)
            return -1;
        if (_cur_type == JSON_num)
        {
            ptr++;
            while ((ptr - _jstr) < _len) // looking for number end
            {
                if ((*ptr == ',') || (*ptr == '}') || (*ptr == ']') || (*ptr == ' ') || (*ptr == '\r') || (*ptr == '\n'))
                    break;
                if (((*ptr >= '0') && (*ptr <= '9')) || (*ptr == '.'))
                {
                    ptr++;
                    continue;
                }
                return -1;
            }
            // calculate the element len
            _cur_len = (ptr - _cur_el);
            if ((ptr - _jstr) == _len)
                return -1;
            if ((*ptr == ',') || (*ptr == '}') || (*ptr == ']'))
                ptr--; // to make it like string end
        }
        else if (_cur_type == JSON_obj)
        {
            char *object_end = find_object_end(ptr);
            ptr = object_end;
            // calculate the element len
            _cur_len = (ptr + 1 - _cur_el);
        }
        else if (_cur_type == JSONP_array)
        {
            char *array_end = find_array_end(ptr);
            ptr = array_end;
            // calculate the element len
            _cur_len = (ptr + 1 - _cur_el);
        }
        else
        {
            ptr++;
            if ((ptr - _jstr) < _len) // check that after a '"' actually a string start
            {
                if ((*ptr == '{') || (*ptr == '}') || (*ptr == '[') || (*ptr == ']') || (*ptr == ':') || (*ptr == ','))
                    return -1;
            }
            else
                return -1;
            while ((ptr - _jstr) < _len) // looking for ending '"'
            {
                if (*ptr == '"')
                {
                    // calculate the element len
                    _cur_len = (ptr - _cur_el);
                    break;
                }
                if ((*ptr == '{') || (*ptr == '}') || (*ptr == '[') || (*ptr == ']') || (*ptr == ':') || (*ptr == ','))
                    return -1;
                ptr++;
            }
            if ((ptr - _jstr) == _len)
                return -1;
        }
        // check if this is the idx elem
        if (tmp_elem_count == idx)
            return idx;
        ptr++;
        another_elem_found = false;
        while ((ptr - _jstr) < _len) // looking for ']' or ','
        {
            if (*ptr == ']')
                break;
            if (*ptr == ',')
            {
                another_elem_found = true;
                tmp_elem_count++;
                break;
            }
            if ((*ptr != ' ') && (*ptr != '\r') && (*ptr != '\n'))
                return -1;
            ptr++;
        }
        if ((ptr - _jstr) == _len)
            return -1;
        if (another_elem_found)
            ptr++;
    } while (another_elem_found);
    ptr++;
    while ((ptr - _jstr) < _len) // looking for end of string
    {
        if ((*ptr != ' ') && (*ptr != '\r') && (*ptr != '\n'))
            return -1;
        ptr++;
    }
    return -1; // couldn't find the element idx
}

int JSONP_ARRAY::getInt(int idx)
{
    if (_err != JSON_noerr)
        return 0;
    // check array boundaries
    if ((idx < 0) || (idx >= _el_count))
    {
        _err = JSON_outOfBoundary;
        return 0;
    }
    // if not already to element idx
    if (idx != _cur_id)
        // look for the element idx
        _cur_id = find_elem(idx);
    if (_cur_id != idx)
    {
        // not found
        _err = JSON_notFound;
        return 0;
    }
    if (_cur_type != JSON_num)
    {
        _err = JSON_typeMismatch;
        return 0;
    }
    // check if value is a float
    int ii;
    for (ii = 0; ii < _cur_len; ii++)
        if (_cur_el[ii] == '.')
        {
            _err = JSON_typeMismatch;
            return 0;
        }
    char value_str[16];
    espmem.stack_mon();

    os_memset(value_str, 0, 16);
    if (_cur_len > 15)
    {
        _err = JSON_typeMismatch;
        return 0;
    }
    os_strncpy(value_str, _cur_el, _cur_len);
    return atoi(value_str);
}

float JSONP_ARRAY::getFloat(int idx)
{
    if (_err != JSON_noerr)
        return 0.0;
    // check array boundaries
    if ((idx < 0) || (idx >= _el_count))
    {
        _err = JSON_outOfBoundary;
        return 0.0;
    }
    // if not already to element idx
    if (idx != _cur_id)
        // look for the element idx
        _cur_id = find_elem(idx);
    if (_cur_id != idx)
    {
        // not found
        _err = JSON_notFound;
        return 0.0;
    }
    if (_cur_type != JSON_num)
    {
        _err = JSON_typeMismatch;
        return 0.0;
    }
    char value_str[64];
    espmem.stack_mon();

    os_memset(value_str, 0, 64);
    if (_cur_len > 63)
    {
        _err = JSON_typeMismatch;
        return 0.0;
    }
    os_strncpy(value_str, _cur_el, _cur_len);
    return atof(value_str);
}

void JSONP_ARRAY::getStr(int idx, char *dest, int len)
{
    if (_err != JSON_noerr)
        return;
    // check array boundaries
    if ((idx < 0) || (idx >= _el_count))
    {
        _err = JSON_outOfBoundary;
        return;
    }
    // if not already to element idx
    if (idx != _cur_id)
        // look for the element idx
        _cur_id = find_elem(idx);
    if (_cur_id != idx)
    {
        // not found
        _err = JSON_notFound;
        return;
    }
    if (_cur_type != JSON_str)
    {
        _err = JSON_typeMismatch;
        return;
    }
    if (_cur_len > len)
    {
        _err = JSON_typeMismatch;
        return;
    }
    os_memset(dest, 0, len);
    os_strncpy(dest, _cur_el, _cur_len);
}

int JSONP_ARRAY::getStrlen(int idx)
{
    if (_err != JSON_noerr)
        return 0;
    // check array boundaries
    if ((idx < 0) || (idx >= _el_count))
    {
        _err = JSON_outOfBoundary;
        return 0;
    }
    // if not already to element idx
    if (idx != _cur_id)
        // look for the element idx
        _cur_id = find_elem(idx);
    if (_cur_id != idx)
    {
        // not found
        _err = JSON_notFound;
        return 0;
    }
    if (_cur_type != JSON_str)
    {
        _err = JSON_typeMismatch;
        return 0;
    }
    return _cur_len;
}

JSONP JSONP_ARRAY::getObj(int idx)
{
    JSONP empty_obj;
    if (_err != JSON_noerr)
        return empty_obj;
    // check array boundaries
    if ((idx < 0) || (idx >= _el_count))
    {
        _err = JSON_outOfBoundary;
        return empty_obj;
    }
    // if not already to element idx
    if (idx != _cur_id)
        // look for the element idx
        _cur_id = find_elem(idx);
    if (_cur_id != idx)
    {
        // not found
        _err = JSON_notFound;
        return empty_obj;
    }
    if (_cur_type != JSON_obj)
    {
        _err = JSON_typeMismatch;
        return empty_obj;
    }
    return JSONP(_cur_el, _cur_len);
}

JSONP_ARRAY JSONP_ARRAY::getArray(int idx)
{
    JSONP_ARRAY empty_obj;
    if (_err != JSON_noerr)
        return empty_obj;
    // check array boundaries
    if ((idx < 0) || (idx >= _el_count))
    {
        _err = JSON_outOfBoundary;
        return empty_obj;
    }
    // if not already to element idx
    if (idx != _cur_id)
        // look for the element idx
        _cur_id = find_elem(idx);
    if (_cur_id != idx)
    {
        // not found
        _err = JSON_notFound;
        return empty_obj;
    }
    if (_cur_type != JSONP_array)
    {
        _err = JSON_typeMismatch;
        return empty_obj;
    }
    return JSONP_ARRAY(_cur_el, _cur_len);
}

void JSONP_ARRAY::clearErr(void)
{
    _err = JSON_noerr;
}

int JSONP_ARRAY::getErr(void)
{
    return _err;
}

#ifdef JSON_TEST
void JSONP_ARRAY::getStr(char *str, int str_len)
{
    os_memset(str, 0, str_len);
    if (_len < str_len)
        os_strncpy(str, _jstr, _len);
    else
        os_strncpy(str, _jstr, (str_len - 1));
}
#endif