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

Json_str::Json_str(char *t_str, int t_len)
{
    _str = t_str;
    _str_len = t_len;
    _cursor = _str;
    _pair_string = NULL;
    _pair_string_len = 0;
    _pair_value_type = JSON_TYPE_ERR;
    _pair_value = NULL; //
    _pair_value_len = 0;
}

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

int Json_str::syntax_check(void)
{
    char *ptr = _cursor;
    bool another_pair_found = false;
    espmem.stack_mon();

    _pair_value_type = JSON_TYPE_ERR;
    while ((ptr - _str) < _str_len) // looking for starting '{'
    {
        if (*ptr == '{')
            break;
        if ((*ptr != ' ') && (*ptr != '\r') && (*ptr != '\n'))
            return (ptr - _str);
        ptr++;
    }
    if ((ptr - _str) == _str_len)
        return (ptr - _str);
    ptr++;
    do
    {
        while ((ptr - _str) < _str_len) // looking for starting '"'
        {
            if (*ptr == '"')
                break;
            if ((*ptr == '}') && (!another_pair_found))
                return -1; // fine, this is an empty object
            if ((*ptr != ' ') && (*ptr != '\r') && (*ptr != '\n'))
                return (ptr - _str + 1);
            ptr++;
        }
        if ((ptr - _str) == _str_len)
            return (ptr - _str + 1);
        ptr++;
        if ((ptr - _str) < _str_len) // check that after a '"' actually a string start
        {
            if ((*ptr == '"') || (*ptr == '{') || (*ptr == '}') || (*ptr == '[') || (*ptr == ']') || (*ptr == ':') || (*ptr == ','))
                return (ptr - _str + 1);
        }
        else
            return (ptr - _str + 1);
        ptr++;
        while ((ptr - _str) < _str_len) // looking for ending '"'
        {
            if (*ptr == '"')
                break;
            if ((*ptr == '{') || (*ptr == '}') || (*ptr == '[') || (*ptr == ']') || (*ptr == ':') || (*ptr == ','))
                return (ptr - _str + 1);
            ptr++;
        }
        if ((ptr - _str) == _str_len)
            return (ptr - _str + 1);
        ptr++;
        while ((ptr - _str) < _str_len) // looking for ':'
        {
            if (*ptr == ':')
                break;
            if ((*ptr != ' ') && (*ptr != '\r') && (*ptr != '\n'))
                return (ptr - _str + 1);
            ptr++;
        }
        if ((ptr - _str) == _str_len)
            return (ptr - _str + 1);
        ptr++;
        while ((ptr - _str) < _str_len) // looking for starting '"' or number or object
        {
            if (*ptr == '"') // found a string
            {
                _pair_value_type = JSON_STRING;
                break;
            }
            if (((*ptr >= '0') && (*ptr <= '9')) || (*ptr == '-')) // found a number
            {
                _pair_value_type = JSON_INTEGER;
                break;
            }
            if (*ptr == '{') // found an object
            {
                _pair_value_type = JSON_OBJECT;
                break;
            }
            if (*ptr == '[') // found an array
            {
                _pair_value_type = JSON_ARRAY;
                break;
            }
            if ((*ptr != ' ') && (*ptr != '\r') && (*ptr != '\n'))
                return (ptr - _str + 1);
            ptr++;
        }
        if ((ptr - _str) == _str_len)
            return (ptr - _str + 1);
        if (_pair_value_type == JSON_INTEGER)
        {
            ptr++;
            while ((ptr - _str) < _str_len) // looking for number end
            {
                if ((*ptr == ',') || (*ptr == '}') || (*ptr == ' ') || (*ptr == '\r') || (*ptr == '\n'))
                    break;
                if (((*ptr >= '0') && (*ptr <= '9')) || (*ptr == '.'))
                {
                    ptr++;
                    continue;
                }
                return (ptr - _str + 1);
            }
            if ((ptr - _str) == _str_len)
                return (ptr - _str + 1);
            if ((*ptr == ',') || (*ptr == '}'))
                ptr--; // to make it like string end
        }
        else if (_pair_value_type == JSON_OBJECT)
        {
            char *object_end = find_object_end(ptr);
            Json_str json_str(ptr, ((object_end - ptr) + 1));
            int res = json_str.syntax_check();
            espmem.stack_mon();
            if (res > JSON_SINTAX_OK)
                return (ptr - _str + res);
            ptr = object_end;
        }
        else if (_pair_value_type == JSON_ARRAY)
        {
            char *array_end = find_array_end(ptr);
            Json_array_str array_str(ptr, ((array_end - ptr) + 1));
            int res = array_str.syntax_check();
            espmem.stack_mon();
            if (res > JSON_SINTAX_OK)
                return (ptr - _str + res);
            ptr = array_end;
        }
        else
        {
            ptr++;
            if ((ptr - _str) < _str_len) // check that after a '"' actually a string start
            {
                if ((*ptr == '{') || (*ptr == '}') || (*ptr == '[') || (*ptr == ']') || (*ptr == ':') || (*ptr == ','))
                    return (ptr - _str + 1);
            }
            else
                return (ptr - _str + 1);
            while ((ptr - _str) < _str_len) // looking for ending '"'
            {
                if (*ptr == '"')
                    break;
                if ((*ptr == '{') || (*ptr == '}') || (*ptr == '[') || (*ptr == ']') || (*ptr == ':') || (*ptr == ','))
                    return (ptr - _str + 1);
                ptr++;
            }
            if ((ptr - _str) == _str_len)
                return (ptr - _str + 1);
        }
        ptr++;
        another_pair_found = false;
        while ((ptr - _str) < _str_len) // looking for '}' or ','
        {
            if (*ptr == '}')
                break;
            if (*ptr == ',')
            {
                another_pair_found = true;
                break;
            }
            if ((*ptr != ' ') && (*ptr != '\r') && (*ptr != '\n'))
                return (ptr - _str + 1);
            ptr++;
        }
        if ((ptr - _str) == _str_len)
            return (ptr - _str + 1);
        if (another_pair_found)
            ptr++;
    } while (another_pair_found);
    ptr++;
    while ((ptr - _str) < _str_len) // looking for end of string
    {
        if ((*ptr != ' ') && (*ptr != '\r') && (*ptr != '\n'))
            return (ptr - _str + 1);
        ptr++;
    }
    return -1; // the syntax is fine
}

Json_pair_type Json_str::find_next_pair(void)
{
    _pair_string = NULL;
    _pair_string_len = 0;
    _pair_value_type = JSON_TYPE_ERR;
    _pair_value = NULL;
    _pair_value_len = 0;
    if (_cursor == _str)
    {
        while ((_cursor - _str) < _str_len) // looking for starting '{'
        {
            if (*_cursor == '{')
                break;
            if ((*_cursor != ' ') && (*_cursor != '\r') && (*_cursor != '\n'))
                return JSON_ERR;
            _cursor++;
        }
        _cursor++;
    }
    if ((_cursor - _str) == _str_len)
        return JSON_ERR;
    while ((_cursor - _str) < _str_len) // looking for starting '"'
    {
        if (*_cursor == '"')
            break;
        if ((*_cursor != ' ') && (*_cursor != '\r') && (*_cursor != '\n'))
            return JSON_ERR;
        _cursor++;
    }
    if ((_cursor - _str) == _str_len)
        return JSON_NO_NEW_PAIR_FOUND;
    _cursor++;
    if ((_cursor - _str) < _str_len) // check that after a '"' actually a string start
    {
        if ((*_cursor == '"') || (*_cursor == '{') || (*_cursor == '}') || (*_cursor == '[') || (*_cursor == ']') || (*_cursor == ':') || (*_cursor == ','))
            return JSON_ERR;
        else
            _pair_string = _cursor;
    }
    else
        return JSON_ERR;
    _cursor++;
    while ((_cursor - _str) < _str_len) // looking for ending '"'
    {
        if (*_cursor == '"')
        {
            _pair_string_len = (_cursor - _pair_string);
            break;
        }
        if ((*_cursor == '{') || (*_cursor == '}') || (*_cursor == '[') || (*_cursor == ']') || (*_cursor == ':') || (*_cursor == ','))
            return JSON_ERR;
        _cursor++;
    }
    if ((_cursor - _str) == _str_len)
        return JSON_ERR;
    _cursor++;
    while ((_cursor - _str) < _str_len) // looking for ':'
    {
        if (*_cursor == ':')
            break;
        if ((*_cursor != ' ') && (*_cursor != '\r') && (*_cursor != '\n'))
            return JSON_ERR;
        _cursor++;
    }
    if ((_cursor - _str) == _str_len)
        return JSON_ERR;
    _cursor++;
    while ((_cursor - _str) < _str_len) // looking for starting '"' or number or object
    {
        if (*_cursor == '"') // found a string
        {
            _pair_value_type = JSON_STRING;
            break;
        }
        if (((*_cursor >= '0') && (*_cursor <= '9')) || (*_cursor == '-')) // found a number
        {
            _pair_value_type = JSON_INTEGER;
            _pair_value = _cursor;
            break;
        }
        if (*_cursor == '{') // found an object
        {
            _pair_value_type = JSON_OBJECT;
            _pair_value = _cursor;
            break;
        }
        if (*_cursor == '[') // found an array
        {
            _pair_value_type = JSON_ARRAY;
            _pair_value = _cursor;
            break;
        }
        if ((*_cursor != ' ') && (*_cursor != '\r') && (*_cursor != '\n'))
            return JSON_ERR;
        _cursor++;
    }
    if ((_cursor - _str) == _str_len)
        return JSON_ERR;
    if (_pair_value_type == JSON_INTEGER)
    {
        _cursor++;
        while ((_cursor - _str) < _str_len) // looking for number end
        {
            if ((*_cursor == ',') || (*_cursor == '}') || (*_cursor == ' ') || (*_cursor == '\r') || (*_cursor == '\n'))
            {
                _pair_value_len = (_cursor - _pair_value);
                break;
            }
            if (((*_cursor >= '0') && (*_cursor <= '9')) || (*_cursor == '.'))
            {
                _cursor++;
                continue;
            }
            return JSON_ERR;
        }
        if ((_cursor - _str) == _str_len)
            return JSON_ERR;
        if ((*_cursor == ',') || (*_cursor == '}'))
            _cursor--; // to make it like string end
    }
    else if (_pair_value_type == JSON_OBJECT)
    {
        _cursor = find_object_end(_cursor);
        _pair_value_len = _cursor - _pair_value + 1;
    }
    else if (_pair_value_type == JSON_ARRAY)
    {
        _cursor = find_array_end(_cursor);
        _pair_value_len = _cursor - _pair_value + 1;
    }
    else
    {
        _cursor++;
        if ((_cursor - _str) < _str_len) // check that after a '"' actually a string start
        {
            if ((*_cursor == '{') || (*_cursor == '}') || (*_cursor == '[') || (*_cursor == ']') || (*_cursor == ':') || (*_cursor == ','))
                return JSON_ERR;
        }
        else
            return JSON_ERR;
        _pair_value = _cursor;
        while ((_cursor - _str) < _str_len) // looking for ending '"'
        {
            if (*_cursor == '"')
            {
                _pair_value_len = _cursor - _pair_value;
                break;
            }
            if ((*_cursor == '{') || (*_cursor == '}') || (*_cursor == '[') || (*_cursor == ']') || (*_cursor == ':') || (*_cursor == ','))
                return JSON_ERR;
            _cursor++;
        }
        if ((_cursor - _str) == _str_len)
            return JSON_ERR;
    }
    _cursor++;
    while ((_cursor - _str) < _str_len) // looking for ending '}' or ','
    {
        if ((*_cursor == '}') || (*_cursor == ','))
            break;
        if ((*_cursor != ' ') && (*_cursor != '\r') && (*_cursor != '\n'))
            return JSON_ERR;
        _cursor++;
    }
    if ((_cursor - _str) == _str_len)
        return JSON_ERR;
    _cursor++; // eventually pointing to a new pair
    if (_pair_string)
        return JSON_NEW_PAIR_FOUND; // found a new pair
    else
        return JSON_NO_NEW_PAIR_FOUND;
}

char *Json_str::get_cur_pair_string(void)
{
    return _pair_string;
}

int Json_str::get_cur_pair_string_len(void)
{
    return _pair_string_len;
}

Json_value_type Json_str::get_cur_pair_value_type(void)
{
    return _pair_value_type;
}

char *Json_str::get_cur_pair_value(void)
{
    return _pair_value;
}

int Json_str::get_cur_pair_value_len(void)
{
    return _pair_value_len;
}

char *Json_str::get_cursor(void)
{
    return _cursor;
}

Json_pair_type Json_str::find_pair(const char *t_string)
{
    _cursor = _str;
    if (syntax_check() == JSON_SINTAX_OK)
    {
        while (find_next_pair() == JSON_NEW_PAIR_FOUND)
        {
            if (os_strncmp(get_cur_pair_string(), t_string, os_strlen(t_string)) == 0)
                return JSON_NEW_PAIR_FOUND;
        }
        return JSON_NO_NEW_PAIR_FOUND;
    }
    else
        return JSON_ERR;
}

Json_array_str::Json_array_str(char *t_str, int t_len)
{
    _str = t_str;
    _str_len = t_len;
    _cursor = _str;
    _el_count = 0;
    _cur_elem = -1;
    char *_el = NULL;
    int _el_len = 0;
    Json_value_type _el_type = JSON_TYPE_ERR;
}

int Json_array_str::syntax_check(void)
{
    char *ptr = _cursor;
    bool another_pair_found = false;
    int tmp_elem_count = 1;
    espmem.stack_mon();

    _el_type = JSON_TYPE_ERR;
    while ((ptr - _str) < _str_len) // looking for starting '['
    {
        if (*ptr == '[')
            break;
        if ((*ptr != ' ') && (*ptr != '\r') && (*ptr != '\n'))
            return (ptr - _str);
        ptr++;
    }
    if ((ptr - _str) == _str_len)
        return (ptr - _str);
    ptr++;
    do
    {
        while ((ptr - _str) < _str_len) // looking for starting '"' or number or object
        {
            if ((*ptr == ']') && (!another_pair_found)) // found an empty array
            {
                _el_count = 0;
                return -1; // the syntax is fine
            }
            if (*ptr == '"') // found a string
            {
                _el_type = JSON_STRING;
                break;
            }
            if (((*ptr >= '0') && (*ptr <= '9')) || (*ptr == '-')) // found a number
            {
                _el_type = JSON_INTEGER;
                break;
            }
            if (*ptr == '{') // found an object
            {
                _el_type = JSON_OBJECT;
                break;
            }
            if (*ptr == '[') // found an array
            {
                _el_type = JSON_ARRAY;
                break;
            }
            if ((*ptr != ' ') && (*ptr != '\r') && (*ptr != '\n'))
                return (ptr - _str + 1);
            ptr++;
        }
        if ((ptr - _str) == _str_len)
            return (ptr - _str + 1);
        if (_el_type == JSON_INTEGER)
        {
            ptr++;
            while ((ptr - _str) < _str_len) // looking for number end
            {
                if ((*ptr == ',') || (*ptr == '}') || (*ptr == ']') || (*ptr == ' ') || (*ptr == '\r') || (*ptr == '\n'))
                    break;
                if (((*ptr >= '0') && (*ptr <= '9')) || (*ptr == '.'))
                {
                    ptr++;
                    continue;
                }
                return (ptr - _str + 1);
            }
            if ((ptr - _str) == _str_len)
                return (ptr - _str + 1);
            if ((*ptr == ',') || (*ptr == '}') || (*ptr == ']'))
                ptr--; // to make it like string end
        }
        else if (_el_type == JSON_OBJECT)
        {
            char *object_end = find_object_end(ptr);
            Json_str json_str(ptr, ((object_end - ptr) + 1));
            int res = json_str.syntax_check();
            espmem.stack_mon();
            if (res > JSON_SINTAX_OK)
                return (ptr - _str + res);
            ptr = object_end;
        }
        else if (_el_type == JSON_ARRAY)
        {
            char *array_end = find_array_end(ptr);
            Json_array_str array_str(ptr, ((array_end - ptr) + 1));
            int res = array_str.syntax_check();
            espmem.stack_mon();
            if (res > JSON_SINTAX_OK)
                return (ptr - _str + res);
            ptr = array_end;
        }
        else
        {
            ptr++;
            if ((ptr - _str) < _str_len) // check that after a '"' actually a string start
            {
                if ((*ptr == '{') || (*ptr == '}') || (*ptr == '[') || (*ptr == ']') || (*ptr == ':') || (*ptr == ','))
                    return (ptr - _str + 1);
            }
            else
                return (ptr - _str + 1);
            while ((ptr - _str) < _str_len) // looking for ending '"'
            {
                if (*ptr == '"')
                    break;
                if ((*ptr == '{') || (*ptr == '}') || (*ptr == '[') || (*ptr == ']') || (*ptr == ':') || (*ptr == ','))
                    return (ptr - _str + 1);
                ptr++;
            }
            if ((ptr - _str) == _str_len)
                return (ptr - _str + 1);
        }
        ptr++;
        another_pair_found = false;
        while ((ptr - _str) < _str_len) // looking for ']' or ','
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
                return (ptr - _str + 1);
            ptr++;
        }
        if ((ptr - _str) == _str_len)
            return (ptr - _str + 1);
        if (another_pair_found)
            ptr++;
    } while (another_pair_found);
    ptr++;
    while ((ptr - _str) < _str_len) // looking for end of string
    {
        if ((*ptr != ' ') && (*ptr != '\r') && (*ptr != '\n'))
            return (ptr - _str + 1);
        ptr++;
    }
    _el_count = tmp_elem_count;
    return -1; // the syntax is fine
}

int Json_array_str::size(void)
{
    // don't repeat the ounting every time...
    if (_el_count == 0)
    {
        syntax_check();
    }
    return _el_count;
}

int Json_array_str::find_elem(int idx)
{
    char *ptr = _cursor;
    bool another_elem_found = false;
    int tmp_elem_count = 0;
    espmem.stack_mon();

    _el_type = JSON_TYPE_ERR;
    while ((ptr - _str) < _str_len) // looking for starting '['
    {
        if (*ptr == '[')
            break;
        if ((*ptr != ' ') && (*ptr != '\r') && (*ptr != '\n'))
            return -1;
        ptr++;
    }
    if ((ptr - _str) == _str_len)
        return -1;
    ptr++;
    do
    {
        while ((ptr - _str) < _str_len) // looking for starting '"' or number or object
        {
            if ((*ptr == ']') && (!another_elem_found)) // found an empty array
            {
                return -1;
            }
            if (*ptr == '"') // found a string
            {
                // mark the element start
                _el = ptr + 1;
                _el_type = JSON_STRING;
                break;
            }
            if (((*ptr >= '0') && (*ptr <= '9')) || (*ptr == '-')) // found a number
            {
                // mark the element start
                _el = ptr;
                _el_type = JSON_INTEGER;
                break;
            }
            if (*ptr == '{') // found an object
            {
                // mark the element start
                _el = ptr;
                _el_type = JSON_OBJECT;
                break;
            }
            if (*ptr == '[') // found an array
            {
                // mark the element start
                _el = ptr;
                _el_type = JSON_ARRAY;
                break;
            }
            if ((*ptr != ' ') && (*ptr != '\r') && (*ptr != '\n'))
                return -1;
            ptr++;
        }
        if ((ptr - _str) == _str_len)
            return -1;
        if (_el_type == JSON_INTEGER)
        {
            ptr++;
            while ((ptr - _str) < _str_len) // looking for number end
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
            _el_len = (ptr - _el);
            if ((ptr - _str) == _str_len)
                return -1;
            if ((*ptr == ',') || (*ptr == '}') || (*ptr == ']'))
                ptr--; // to make it like string end
        }
        else if (_el_type == JSON_OBJECT)
        {
            char *object_end = find_object_end(ptr);
            ptr = object_end;
            // calculate the element len
            _el_len = (ptr + 1 - _el);
        }
        else if (_el_type == JSON_ARRAY)
        {
            char *array_end = find_array_end(ptr);
            ptr = array_end;
            // calculate the element len
            _el_len = (ptr + 1 - _el);
        }
        else
        {
            ptr++;
            if ((ptr - _str) < _str_len) // check that after a '"' actually a string start
            {
                if ((*ptr == '{') || (*ptr == '}') || (*ptr == '[') || (*ptr == ']') || (*ptr == ':') || (*ptr == ','))
                    return -1;
            }
            else
                return -1;
            while ((ptr - _str) < _str_len) // looking for ending '"'
            {
                if (*ptr == '"')
                {
                    // calculate the element len
                    _el_len = (ptr - _el);
                    break;
                }
                if ((*ptr == '{') || (*ptr == '}') || (*ptr == '[') || (*ptr == ']') || (*ptr == ':') || (*ptr == ','))
                    return -1;
                ptr++;
            }
            if ((ptr - _str) == _str_len)
                return -1;
        }
        // check if this is the idx elem
        if (tmp_elem_count == idx)
            return idx;
        ptr++;
        another_elem_found = false;
        while ((ptr - _str) < _str_len) // looking for ']' or ','
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
        if ((ptr - _str) == _str_len)
            return -1;
        if (another_elem_found)
            ptr++;
    } while (another_elem_found);
    ptr++;
    while ((ptr - _str) < _str_len) // looking for end of string
    {
        if ((*ptr != ' ') && (*ptr != '\r') && (*ptr != '\n'))
            return -1;
        ptr++;
    }
    return -1; // couldn't find the element idx
}

char *Json_array_str::get_elem(int idx)
{
    // check the if size was calculated
    if (_el_count == 0)
    {
        syntax_check();
    }
    // check array boundaries
    if ((idx < 0) || (idx >= _el_count))
    {
        return NULL;
    }
    // already to element idx
    if (idx == _cur_elem)
        return _el;
    // look for the element idx
    _cur_elem = find_elem(idx);
    if (_cur_elem == idx)
    {
        return _el;
    }
    else
    {
        _el = NULL;
        _el_len = 0;
        _el_type = JSON_TYPE_ERR;
        return _el;
    }
}

int Json_array_str::get_elem_len(int idx)
{
    // check the if size was calculated
    if (_el_count == 0)
    {
        syntax_check();
    }
    // check array boundaries
    if ((idx < 0) || (idx >= _el_count))
    {
        return 0;
    }
    // already to element idx
    if (idx == _cur_elem)
        return _el_len;
    // look for the element idx
    _cur_elem = find_elem(idx);
    if (_cur_elem == idx)
    {
        return _el_len;
    }
    else
    {
        _el = NULL;
        _el_len = 0;
        _el_type = JSON_TYPE_ERR;
        return _el_len;
    }
}

Json_value_type Json_array_str::get_elem_type(int idx)
{
    // check the if size was calculated
    if (_el_count == 0)
    {
        syntax_check();
    }
    // check array boundaries
    if ((idx < 0) || (idx >= _el_count))
    {
        return JSON_TYPE_ERR;
    }
    // already to element idx
    if (idx == _cur_elem)
        return _el_type;
    // look for the element idx
    _cur_elem = find_elem(idx);
    if (_cur_elem == idx)
    {
        return _el_type;
    }
    else
    {
        _el = NULL;
        _el_len = 0;
        _el_type = JSON_TYPE_ERR;
        return _el_type;
    }
}

char *Json_array_str::get_cursor(void)
{
    return _cursor;
}