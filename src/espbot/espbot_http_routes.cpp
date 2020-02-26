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
#include "ip_addr.h"
#include "mem.h"
#include "osapi.h"
#include "sntp.h"
#include "user_interface.h"
}

#include "app_http_routes.hpp"
#include "espbot.hpp"
#include "espbot_cron.hpp"
#include "espbot_diagnostic.hpp"
#include "espbot_event_codes.h"
#include "espbot_global.hpp"
#include "espbot_http.hpp"
#include "espbot_http_routes.hpp"
#include "espbot_json.hpp"
#include "espbot_mem_mon.hpp"
#include "espbot_timedate.hpp"
#include "espbot_utils.hpp"
#include "espbot_webserver.hpp"

static os_timer_t format_delay_timer;

void init_controllers(void)
{
    os_timer_disarm(&format_delay_timer);
}

static void format_function(void)
{
    ALL("format_function");
    espfs.format();
}

static void wifi_scan_completed_function(void *param)
{
    struct espconn *ptr_espconn = (struct espconn *)param;
    ALL("wifi_scan_completed_function");
    char *scan_list = new char[40 + ((32 + 6) * Wifi::get_ap_count())];
    if (scan_list)
    {
        char *tmp_ptr;
        espmem.stack_mon();
        fs_sprintf(scan_list, "{\"AP_count\": %d,\"AP_SSIDs\":[", Wifi::get_ap_count());
        for (int idx = 0; idx < Wifi::get_ap_count(); idx++)
        {
            tmp_ptr = scan_list + os_strlen(scan_list);
            if (idx > 0)
                *(tmp_ptr++) = ',';
            os_sprintf(tmp_ptr, "\"%s\"", Wifi::get_ap_name(idx));
        }
        tmp_ptr = scan_list + os_strlen(scan_list);
        fs_sprintf(tmp_ptr, "]}");
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, scan_list, true);
        Wifi::free_ap_list();
    }
    else
    {
        esp_diag.error(ROUTES_WIFI_SCAN_COMPLETED_FUNCTION_HEAP_EXHAUSTED, (40 + ((32 + 6) * Wifi::get_ap_count())));
        ERROR("wifi_scan_completed_function heap exhausted %d", (32 + (40 + ((32 + 6) * Wifi::get_ap_count()))));
        // may be the list was too big but there is enough heap memory for a response
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Not enough heap memory"), false);
        Wifi::free_ap_list();
    }
}

static const char *get_file_mime_type(char *filename)
{
    ALL("get_file_mime_type");
    char *ptr;
    ptr = (char *)os_strstr(filename, ".");
    if (ptr == NULL)
        return f_str("text/plain");
    else
    {
        if (os_strcmp(ptr, ".css") == 0)
            return f_str("text/css");
        else if (os_strcmp(ptr, ".txt") == 0)
            return f_str("text/plain");
        else if (os_strcmp(ptr, ".html") == 0)
            return f_str("text/html");
        else if (os_strcmp(ptr, ".js") == 0)
            return f_str("text/javascript");
        else if (os_strcmp(ptr, ".css") == 0)
            return f_str("text/css");
        else
            return f_str("text/plain");
    }
}

static void send_remaining_file(struct http_split_send *p_sr)
{
    ALL("send_remaining_file");
    if (!espfs.is_available())
    {
        http_response(p_sr->p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("File system is not available"), false);
        delete[] p_sr->content;
        return;
    }
    if (!Ffile::exists(&espfs, p_sr->content))
    {
        http_response(p_sr->p_espconn, HTTP_NOT_FOUND, HTTP_CONTENT_JSON, f_str("File not found"), false);
        delete[] p_sr->content;
        return;
    }
    int file_size = Ffile::size(&espfs, p_sr->content);
    Ffile sel_file(&espfs, p_sr->content);
    if (!sel_file.is_available())
    {
        http_response(p_sr->p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Cannot open file"), false);
        delete[] p_sr->content;
        return;
    }
    int remaining_size = p_sr->content_size - p_sr->content_transferred;
    if (remaining_size > get_http_msg_max_size())
    {
        // the remaining file size is bigger than response_max_size
        // will split the remaining file over multiple messages
        int buffer_size = get_http_msg_max_size();
        Heap_chunk buffer(buffer_size, dont_free);
        if (buffer.ref == NULL)
        {
            esp_diag.error(ROUTES_SEND_REMAINING_MSG_HEAP_EXHAUSTED, buffer_size);
            ERROR("send_remaining_file heap exhausted %d", buffer_size);
            http_response(p_sr->p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Not enough heap memory"), false);
            delete[] p_sr->content;
            return;
        }
        struct http_split_send *p_pending_response = new struct http_split_send;
        if (p_pending_response == NULL)
        {
            esp_diag.error(ROUTES_SEND_REMAINING_MSG_HEAP_EXHAUSTED, sizeof(struct http_split_send));
            ERROR("send_remaining_file not heap exhausted %dn", sizeof(struct http_split_send));
            http_response(p_sr->p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Not enough heap memory"), false);
            delete[] p_sr->content;
            delete[] buffer.ref;
            delete[] p_sr->content;
            return;
        }
        sel_file.n_read(buffer.ref, p_sr->content_transferred, buffer_size);
        // setup the remaining message
        p_pending_response->p_espconn = p_sr->p_espconn;
        p_pending_response->content = p_sr->content;
        p_pending_response->content_size = p_sr->content_size;
        p_pending_response->content_transferred = p_sr->content_transferred + buffer_size;
        p_pending_response->action_function = send_remaining_file;
        Queue_err result = pending_split_send->push(p_pending_response);
        if (result == Queue_full)
        {
            esp_diag.error(ROUTES_SEND_REMAINING_MSG_PENDING_RES_QUEUE_FULL);
            ERROR("send_remaining_file full pending res queue");
        }

        TRACE("send_remaining_file: *p_espconn: %X, msg (splitted) len: %d",
              p_sr->p_espconn, buffer_size);
        http_send_buffer(p_sr->p_espconn, buffer.ref, buffer_size);
    }
    else
    {
        // this is the last piece of the message
        Heap_chunk buffer(remaining_size, dont_free);
        if (buffer.ref == NULL)
        {
            esp_diag.error(ROUTES_SEND_REMAINING_MSG_HEAP_EXHAUSTED, remaining_size);
            ERROR("send_remaining_file heap exhausted %d", remaining_size);
            http_response(p_sr->p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Not enough heap memory"), false);
            delete[] p_sr->content;
            return;
        }
        sel_file.n_read(buffer.ref, p_sr->content_transferred, remaining_size);
        TRACE("send_remaining_file: *p_espconn: %X, msg (splitted) len: %d",
              p_sr->p_espconn, remaining_size);
        http_send_buffer(p_sr->p_espconn, buffer.ref, remaining_size);
        delete[] p_sr->content;
    }
}

void return_file(struct espconn *p_espconn, char *filename)
{
    ALL("return_file");
    if (!espfs.is_available())
    {
        http_response(p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("File system is not available"), false);
        return;
    }
    if (!Ffile::exists(&espfs, filename))
    {
        http_response(p_espconn, HTTP_NOT_FOUND, HTTP_CONTENT_JSON, f_str("File not found"), false);
        return;
    }
    int file_size = Ffile::size(&espfs, filename);
    Ffile sel_file(&espfs, filename);
    if (!sel_file.is_available())
    {
        http_response(p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Cannot open file"), false);
        return;
    }
    // let's start with the header
    Http_header header;
    header.m_code = HTTP_OK;
    header.m_content_type = (char *)get_file_mime_type(filename);
    header.m_content_length = file_size;
    header.m_content_range_start = 0;
    header.m_content_range_end = 0;
    header.m_content_range_total = 0;
    char *header_str = http_format_header(&header);
    if (header_str == NULL)
    {
        http_response(p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Not enough heap memory"), false);
        return;
    }
    // ok send the header
    http_send_buffer(p_espconn, header_str, os_strlen(header_str));
    // and now the file
    if (file_size == 0)
        // the file in empty => nothing to do
        return;

    if (file_size > get_http_msg_max_size())
    {
        // will split the file over multiple messages
        // each the size of http_msg_max_size
        int buffer_size = get_http_msg_max_size();
        Heap_chunk buffer(buffer_size, dont_free);
        if (buffer.ref == NULL)
        {
            esp_diag.error(ROUTES_RETURN_FILE_HEAP_EXHAUSTED, buffer_size);
            ERROR("return_file heap exhausted %d", buffer_size);
            http_response(p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Not enough heap memory"), false);
            return;
        }
        Heap_chunk filename_copy(os_strlen(filename), dont_free);
        if (filename_copy.ref == NULL)
        {
            esp_diag.error(ROUTES_RETURN_FILE_HEAP_EXHAUSTED, os_strlen(filename));
            ERROR("return_file heap exhausted %d", os_strlen(filename));
            http_response(p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Not enough heap memory"), false);
            delete[] buffer.ref;
            return;
        }
        struct http_split_send *p_pending_response = new struct http_split_send;
        if (p_pending_response == NULL)
        {
            esp_diag.error(ROUTES_RETURN_FILE_HEAP_EXHAUSTED, sizeof(struct http_split_send));
            ERROR("return_file heap exhausted %d", sizeof(struct http_split_send));
            http_response(p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Not enough heap memory"), false);
            delete[] buffer.ref;
            delete[] filename_copy.ref;
            return;
        }
        os_strncpy(filename_copy.ref, filename, os_strlen(filename));
        sel_file.n_read(buffer.ref, buffer_size);
        // setup the remaining message
        p_pending_response->p_espconn = p_espconn;
        p_pending_response->content = filename_copy.ref;
        p_pending_response->content_size = file_size;
        p_pending_response->content_transferred = buffer_size;
        p_pending_response->action_function = send_remaining_file;
        Queue_err result = pending_split_send->push(p_pending_response);
        if (result == Queue_full)
        {
            esp_diag.error(ROUTES_RETURN_FILE_PENDING_RES_QUEUE_FULL);
            ERROR("return_file full pending response queue");
        }
        // send the file piece
        TRACE("return_file *p_espconn: %X, msg (splitted) len: %d", p_espconn, buffer_size);
        http_send_buffer(p_espconn, buffer.ref, buffer_size);
        espmem.stack_mon();
    }
    else
    {
        // no need to split the file over multiple messages
        Heap_chunk buffer(file_size, dont_free);
        if (buffer.ref == NULL)
        {
            esp_diag.error(ROUTES_RETURN_FILE_HEAP_EXHAUSTED, file_size);
            ERROR("return_file heap exhausted %d", file_size);
            http_response(p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Not enough heap memory"), false);
            return;
        }
        sel_file.n_read(buffer.ref, file_size);
        TRACE("return_file *p_espconn: %X, msg (full) len: %d", p_espconn, file_size);
        http_send_buffer(p_espconn, buffer.ref, file_size);
    }
}

void preflight_response(struct espconn *p_espconn, Http_parsed_req *parsed_req)
{
    ALL("preflight_response");
    Http_header header;
    header.m_code = HTTP_OK;
    header.m_content_type = (char *)get_file_mime_type(HTTP_CONTENT_TEXT);
    header.m_origin = new char[os_strlen(parsed_req->origin) + 1];
    if (header.m_origin == NULL)
    {
        esp_diag.error(ROUTES_PREFLIGHT_RESPONSE_HEAP_EXHAUSTED, (os_strlen(parsed_req->origin) + 1));
        http_response(p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Not enough heap memory"), false);
        return;
    }
    os_strcpy(header.m_origin, parsed_req->origin);
    header.m_acrh = new char[os_strlen(parsed_req->acrh) + 1];
    if (header.m_acrh == NULL)
    {
        esp_diag.error(ROUTES_PREFLIGHT_RESPONSE_HEAP_EXHAUSTED, (os_strlen(parsed_req->acrh) + 1));
        http_response(p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Not enough heap memory"), false);
        return;
    }
    os_strcpy(header.m_acrh, parsed_req->acrh);
    header.m_content_length = 0;
    header.m_content_range_start = 0;
    header.m_content_range_end = 0;
    header.m_content_range_total = 0;
    char *header_str = http_format_header(&header);
    if (header_str == NULL)
    {
        esp_diag.error(ROUTES_PREFLIGHT_RESPONSE_HEAP_EXHAUSTED);
        http_response(p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Not enough heap memory"), false);
        return;
    }
    // ok send the header
    http_send_buffer(p_espconn, header_str, os_strlen(header_str));
}

static void get_api_cron(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("get_api_cron");
    // "{"cron_enabled": 0}" 20 chars
    int msg_len = 20;
    Heap_chunk msg(msg_len, dont_free);
    if (msg.ref)
    {
        fs_sprintf(msg.ref,
                   "{\"cron_enabled\": %d}",
                   cron_enabled());
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
    }
    else
    {
        esp_diag.error(ROUTES_GET_CRON_HEAP_EXHAUSTED, msg_len);
        ERROR("get_api_cron heap exhausted %d", msg_len);
    }
}

static void post_api_cron(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("post_api_cron");
    Json_str cron_cfg(parsed_req->req_content, parsed_req->content_len);
    if (cron_cfg.syntax_check() != JSON_SINTAX_OK)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Json bad syntax"), false);
        return;
    }
    if (cron_cfg.find_pair(f_str("cron_enabled")) != JSON_NEW_PAIR_FOUND)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Cannot find JSON string 'cron_enabled'"), false);
        return;
    }
    if (cron_cfg.get_cur_pair_value_type() != JSON_INTEGER)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("JSON pair with string 'cron_enabled' does not have a INTEGER value type"), false);
        return;
    }
    Heap_chunk tmp_cron_enabled(cron_cfg.get_cur_pair_value_len() + 1);
    if (tmp_cron_enabled.ref)
    {
        os_strncpy(tmp_cron_enabled.ref, cron_cfg.get_cur_pair_value(), cron_cfg.get_cur_pair_value_len());
    }
    else
    {
        esp_diag.error(ROUTES_POST_CRON_HEAP_EXHAUSTED, cron_cfg.get_cur_pair_value_len() + 1);
        ERROR("post_api_cron heap exhausted %d", cron_cfg.get_cur_pair_value_len() + 1);
        return;
    }
    if (atoi(tmp_cron_enabled.ref))
        enable_cron();
    else
        disable_cron();
    save_cron_cfg();

    int msg_len = 20;
    Heap_chunk msg(msg_len, dont_free);
    if (msg.ref)
    {
        fs_sprintf(msg.ref,
                   "{\"cron_enabled\": %d}",
                   cron_enabled());
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
    }
    else
    {
        esp_diag.error(ROUTES_POST_CRON_HEAP_EXHAUSTED, msg_len);
        ERROR("get_api_mdns heap exhausted %d", msg_len);
    }
    espmem.stack_mon();
}

static void get_api_debug_hexmemdump(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("get_api_debug_hexmemdump");
    char *address;
    int length;
    Json_str debug_cfg(parsed_req->req_content, parsed_req->content_len);
    espmem.stack_mon();
    if (debug_cfg.syntax_check() != JSON_SINTAX_OK)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Json bad syntax"), false);
        return;
    }
    if (debug_cfg.find_pair(f_str("address")) != JSON_NEW_PAIR_FOUND)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Cannot find JSON string 'address'"), false);
        return;
    }
    if (debug_cfg.get_cur_pair_value_type() != JSON_STRING)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("JSON pair with string 'address' does not have a string value type"), false);
        return;
    }
    Heap_chunk address_hex_str(debug_cfg.get_cur_pair_value_len() + 1);
    if (address_hex_str.ref)
    {
        os_strncpy(address_hex_str.ref, debug_cfg.get_cur_pair_value(), debug_cfg.get_cur_pair_value_len());
    }
    else
    {
        esp_diag.error(ROUTES_GET_API_DEBUG_HEXMEMDUMP_HEAP_EXHAUSTED, debug_cfg.get_cur_pair_value_len() + 1);
        ERROR("get_api_debug_hexmemdump heap exhausted %d", debug_cfg.get_cur_pair_value_len() + 1);
        return;
    }
    address = (char *)atoh(address_hex_str.ref);
    if (debug_cfg.find_pair(f_str("length")) != JSON_NEW_PAIR_FOUND)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Cannot find JSON string 'length'"), false);
        return;
    }
    if (debug_cfg.get_cur_pair_value_type() != JSON_INTEGER)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("JSON pair with string 'length' does not have an integer value type"), false);
        return;
    }
    Heap_chunk length_str(debug_cfg.get_cur_pair_value_len() + 1);
    if (length_str.ref)
    {
        os_strncpy(length_str.ref, debug_cfg.get_cur_pair_value(), debug_cfg.get_cur_pair_value_len());
    }
    else
    {
        esp_diag.error(ROUTES_GET_API_DEBUG_HEXMEMDUMP_HEAP_EXHAUSTED, debug_cfg.get_cur_pair_value_len() + 1);
        ERROR("get_api_debug_hexmemdump heap exhausted %d", debug_cfg.get_cur_pair_value_len() + 1);
        return;
    }
    length = atoi(length_str.ref);
    espmem.stack_mon();
    int msg_len = 48 + length * 3;
    Heap_chunk msg(msg_len, dont_free);
    if (msg.ref == NULL)
    {
        esp_diag.error(ROUTES_GET_API_DEBUG_HEXMEMDUMP_HEAP_EXHAUSTED, msg_len);
        ERROR("get_api_debug_hexmemdump heap exhausted %d", msg_len);
        return;
    }
    fs_sprintf(msg.ref,
               "{\"address\":\"%X\",\"length\": %d,\"content\":\"",
               address,
               length);
    int cnt;
    char *ptr = msg.ref + os_strlen(msg.ref);
    for (cnt = 0; cnt < length; cnt++)
    {
        os_sprintf(ptr, " %X", *(address + cnt));
        ptr = msg.ref + os_strlen(msg.ref);
    }
    fs_sprintf(msg.ref + os_strlen(msg.ref), "\"}");
    http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
}

static void get_api_debug_memdump(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("get_api_debug_memdump");
    char *address;
    int length;
    Json_str debug_cfg(parsed_req->req_content, parsed_req->content_len);
    espmem.stack_mon();
    if (debug_cfg.syntax_check() != JSON_SINTAX_OK)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Json bad syntax"), false);
        return;
    }
    if (debug_cfg.find_pair(f_str("address")) != JSON_NEW_PAIR_FOUND)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Cannot find JSON string 'address'"), false);
        return;
    }
    if (debug_cfg.get_cur_pair_value_type() != JSON_STRING)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("JSON pair with string 'address' does not have a string value type"), false);
        return;
    }
    Heap_chunk address_hex_str(debug_cfg.get_cur_pair_value_len() + 1);
    if (address_hex_str.ref)
    {
        os_strncpy(address_hex_str.ref, debug_cfg.get_cur_pair_value(), debug_cfg.get_cur_pair_value_len());
    }
    else
    {
        esp_diag.error(ROUTES_GET_API_DEBUG_MEMDUMP_HEAP_EXHAUSTED, debug_cfg.get_cur_pair_value_len() + 1);
        ERROR("get_api_debug_memdump heap exhausted %d", debug_cfg.get_cur_pair_value_len() + 1);
        return;
    }
    address = (char *)atoh(address_hex_str.ref);
    if (debug_cfg.find_pair(f_str("length")) != JSON_NEW_PAIR_FOUND)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Cannot find JSON string 'length'"), false);
        return;
    }
    if (debug_cfg.get_cur_pair_value_type() != JSON_INTEGER)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("JSON pair with string 'length' does not have an integer value type"), false);
        return;
    }
    Heap_chunk length_str(debug_cfg.get_cur_pair_value_len() + 1);
    if (length_str.ref)
    {
        os_strncpy(length_str.ref, debug_cfg.get_cur_pair_value(), debug_cfg.get_cur_pair_value_len());
    }
    else
    {
        esp_diag.error(ROUTES_GET_API_DEBUG_MEMDUMP_HEAP_EXHAUSTED, debug_cfg.get_cur_pair_value_len() + 1);
        ERROR("get_api_debug_memdump heap exhasted %d", debug_cfg.get_cur_pair_value_len() + 1);
        return;
    }
    length = atoi(length_str.ref);
    espmem.stack_mon();
    Heap_chunk msg(48 + length, dont_free);
    if (msg.ref)
    {
        fs_sprintf(msg.ref,
                   "{\"address\":\"%X\",\"length\": %d,\"content\":\"",
                   address,
                   length);
        int cnt;
        char *ptr = msg.ref + os_strlen(msg.ref);
        for (cnt = 0; cnt < length; cnt++)
            os_sprintf(ptr++, "%c", *(address + cnt));
        fs_sprintf(msg.ref + os_strlen(msg.ref), "\"}");
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
    }
    else
    {
        esp_diag.error(ROUTES_GET_API_DEBUG_MEMDUMP_HEAP_EXHAUSTED, 48 + length);
        ERROR("get_api_debug_memdump heap exhausted %d", 48 + length);
    }
}

static void get_api_debug_meminfo(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("get_api_debug_meminfo");
    Heap_chunk msg(166 + 54, dont_free); // formatting string and values
    if (msg.ref == NULL)
    {
        esp_diag.error(ROUTES_GET_API_DEBUG_MEMINFO_HEAP_EXHAUSTED, (166 + 54));
        ERROR("get_api_debug_meminfo heap exhausted %d", (166 + 54));
        return;
    }
    fs_sprintf(msg.ref,
               "{\"stack_max_addr\":\"%X\",\"stack_min_addr\":\"%X\",",
               espmem.get_max_stack_addr(),  //  8
               espmem.get_min_stack_addr()); //  8
    fs_sprintf((msg.ref + os_strlen(msg.ref)),
               "\"heap_start_addr\":\"%X\",\"heap_free_size\": %d,",
               espmem.get_start_heap_addr(), //  8
               system_get_free_heap_size()); //  6
    fs_sprintf((msg.ref + os_strlen(msg.ref)),
               "\"heap_max_size\": %d,\"heap_min_size\": %d,",
               espmem.get_max_heap_size(),  //  6
               espmem.get_mim_heap_size()); //  6
    fs_sprintf((msg.ref + os_strlen(msg.ref)),
               "\"heap_objs\": %d,\"heap_max_objs\": %d}",
               espmem.get_heap_objs(),      //  6
               espmem.get_max_heap_objs()); //  6
    espmem.stack_mon();
    http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
}

static void post_api_diag_ack_events(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("post_api_diag_ack_events");
    esp_diag.ack_events();
    http_response(ptr_espconn, HTTP_CREATED, HTTP_CONTENT_TEXT, f_str("Events acknoledged"), false);
}

static void get_api_diag_cfg(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("get_api_diag_cfg");
    // "{"diag_led_mask": 256,"serial_log_mask": 256}" 46 chars
    int msg_len = 46;
    Heap_chunk msg(msg_len, dont_free);
    if (msg.ref)
    {
        fs_sprintf(msg.ref,
                   "{\"diag_led_mask\": %d,\"serial_log_mask\": %d}",
                   esp_diag.get_led_mask(),
                   esp_diag.get_serial_log_mask());
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
    }
    else
    {
        esp_diag.error(ROUTES_GET_API_DIAG_CFG_HEAP_EXHAUSTED, msg_len);
        ERROR("get_api_diag_cfg heap exhausted %d", msg_len);
    }
}

static void post_api_diag_cfg(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("post_api_diag_cfg");
    Json_str diag_cfg(parsed_req->req_content, parsed_req->content_len);
    if (diag_cfg.syntax_check() != JSON_SINTAX_OK)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Json bad syntax"), false);
        return;
    }
    if (diag_cfg.find_pair(f_str("diag_led_mask")) != JSON_NEW_PAIR_FOUND)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Cannot find JSON string 'diag_led_mask'"), false);
        return;
    }
    if (diag_cfg.get_cur_pair_value_type() != JSON_INTEGER)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("JSON pair with string 'diag_led_mask' does not have a INTEGER value type"), false);
        return;
    }
    Heap_chunk tmp_diag_led_mask(diag_cfg.get_cur_pair_value_len() + 1);
    if (tmp_diag_led_mask.ref)
    {
        os_strncpy(tmp_diag_led_mask.ref, diag_cfg.get_cur_pair_value(), diag_cfg.get_cur_pair_value_len());
    }
    else
    {
        esp_diag.error(ROUTES_POST_API_DIAG_CFG_HEAP_EXHAUSTED, diag_cfg.get_cur_pair_value_len() + 1);
        ERROR("post_api_diag_cfg heap exhausted %d", diag_cfg.get_cur_pair_value_len() + 1);
        return;
    }
    if (diag_cfg.find_pair(f_str("serial_log_mask")) != JSON_NEW_PAIR_FOUND)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Cannot find JSON string 'serial_log_mask'"), false);
        return;
    }
    if (diag_cfg.get_cur_pair_value_type() != JSON_INTEGER)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("JSON pair with string 'serial_log_mask' does not have a INTEGER value type"), false);
        return;
    }
    Heap_chunk tmp_serial_log_mask(diag_cfg.get_cur_pair_value_len() + 1);
    if (tmp_serial_log_mask.ref)
    {
        os_strncpy(tmp_serial_log_mask.ref, diag_cfg.get_cur_pair_value(), diag_cfg.get_cur_pair_value_len());
    }
    else
    {
        esp_diag.error(ROUTES_POST_API_DIAG_CFG_HEAP_EXHAUSTED, diag_cfg.get_cur_pair_value_len() + 1);
        ERROR("post_api_diag_cfg heap exhausted %d", diag_cfg.get_cur_pair_value_len() + 1);
        return;
    }
    esp_diag.set_led_mask(atoi(tmp_diag_led_mask.ref));
    esp_diag.set_serial_log_mask(atoi(tmp_serial_log_mask.ref));
    esp_diag.save_cfg();

    int msg_len = 46;
    Heap_chunk msg(msg_len, dont_free);
    if (msg.ref)
    {
        fs_sprintf(msg.ref,
                   "{\"diag_led_mask\": %d,\"serial_log_mask\": %d}",
                   esp_diag.get_led_mask(),
                   esp_diag.get_serial_log_mask());
        http_response(ptr_espconn, HTTP_CREATED, HTTP_CONTENT_JSON, msg.ref, true);
    }
    else
    {
        esp_diag.error(ROUTES_POST_API_DIAG_CFG_HEAP_EXHAUSTED, msg_len);
        ERROR("post_api_diag_cfg heap exhausted %d", msg_len);
    }
    espmem.stack_mon();
}

static void get_api_diag_events(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("get_api_diag_events");
    // check how much memory needed for last logs
    int evnt_count = 0;
    while (esp_diag.get_event(evnt_count))
    {
        evnt_count++;
        if (evnt_count >= esp_diag.get_max_events_count())
            break;
    }
    // format strings
    // "{"diag_events":["
    // ",{"ts":,"ack":,"type":"","code":"","val":}"
    // "]}"
    int msg_len = 16 +                // formatting string
                  2 +                 // formatting string
                  (evnt_count * 12) + // timestamp
                  (evnt_count * 1) +  // ack
                  (evnt_count * 2) +  // type
                  (evnt_count * 4) +  // code
                  (evnt_count * 12) + // val
                  (evnt_count * 42) + // errors formatting
                  1;                  // just in case
    Heap_chunk msg(msg_len, dont_free);
    if (msg.ref == NULL)
    {
        esp_diag.error(ROUTES_GET_API_DIAG_EVENTS_HEAP_EXHAUSTED, msg_len);
        ERROR("get_api_diag_events heap exhausted %d", msg_len);
    }
    fs_sprintf(msg.ref, "{\"diag_events\":[");
    // now add saved errors
    char *str_ptr;
    struct dia_event *event_ptr;
    int idx = 0;
    // uint32 time_zone_shift = esp_time.get_timezone() * 3600;

    for (idx = 0; idx < evnt_count; idx++)
    {
        event_ptr = esp_diag.get_event(idx);
        str_ptr = msg.ref + os_strlen(msg.ref);
        if (idx == 0)
            fs_sprintf(str_ptr, "{\"ts\":%d,\"ack\":%d,\"type\":\"%X\",\"code\":\"%X\",\"val\":%d}",
                       event_ptr->timestamp,
                       event_ptr->ack,
                       event_ptr->type,
                       event_ptr->code,
                       event_ptr->value);
        else
            fs_sprintf(str_ptr, ",{\"ts\":%d,\"ack\":%d,\"type\":\"%X\",\"code\":\"%X\",\"val\":%d}",
                       event_ptr->timestamp,
                       event_ptr->ack,
                       event_ptr->type,
                       event_ptr->code,
                       event_ptr->value);
    }
    espmem.stack_mon();
    str_ptr = msg.ref + os_strlen(msg.ref);
    fs_sprintf(str_ptr, "]}");
    http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
}

static void get_api_espbot_cfg(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("get_api_espbot_cfg");
    Heap_chunk msg(64, dont_free);
    if (msg.ref)
    {
        fs_sprintf(msg.ref, "{\"espbot_name\":\"%s\"}", espbot.get_name());
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
    }
    else
    {
        esp_diag.error(ROUTES_GET_API_ESPBOT_CFG_HEAP_EXHAUSTED, 64);
        ERROR("get_api_espbot_cfg heap exhausted %d", 64);
    }
}

static void post_api_espbot_cfg(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("post_api_espbot_cfg");
    Json_str espbot_cfg(parsed_req->req_content, parsed_req->content_len);
    espmem.stack_mon();
    if (espbot_cfg.syntax_check() != JSON_SINTAX_OK)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Json bad syntax"), false);
        return;
    }
    if (espbot_cfg.find_pair(f_str("espbot_name")) != JSON_NEW_PAIR_FOUND)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Cannot find JSON string 'espbot_name'"), false);
        return;
    }
    if (espbot_cfg.get_cur_pair_value_type() != JSON_STRING)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("JSON pair with string 'espbot_name' does not have a STRING value type"), false);
        return;
    }
    Heap_chunk tmp_name(espbot_cfg.get_cur_pair_value_len() + 1);
    if (tmp_name.ref)
    {
        os_strncpy(tmp_name.ref, espbot_cfg.get_cur_pair_value(), espbot_cfg.get_cur_pair_value_len());
        espbot.set_name(tmp_name.ref);
    }
    else
    {
        esp_diag.error(ROUTES_POST_API_ESPBOT_CFG_HEAP_EXHAUSTED, espbot_cfg.get_cur_pair_value_len() + 1);
        ERROR("post_api_espbot_cfg heap exhausted %d", espbot_cfg.get_cur_pair_value_len() + 1);
    }
    espmem.stack_mon();
    Heap_chunk msg(64, dont_free);
    if (msg.ref)
    {
        fs_sprintf(msg.ref, "{\"espbot_name\":\"%s\"}", espbot.get_name());
        http_response(ptr_espconn, HTTP_CREATED, HTTP_CONTENT_JSON, msg.ref, true);
    }
    else
    {
        esp_diag.error(ROUTES_POST_API_ESPBOT_CFG_HEAP_EXHAUSTED, 64);
        ERROR("post_api_espbot_cfg heap exhausted %d", 64);
    }
}

static void get_api_fs_info(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("get_api_fs_info");
    if (!espfs.is_available())
    {
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("File system is not available"), false);
        return;
    }
    Heap_chunk msg(128, dont_free);
    if (msg.ref)
    {
        fs_sprintf(msg.ref, "{\"file_system_size\": %d,"
                            "\"file_system_used_size\": %d}",
                   espfs.get_total_size(), espfs.get_used_size());
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
    }
    else
    {
        esp_diag.error(ROUTES_GET_API_FS_INFO_HEAP_EXHAUSTED, 128);
        ERROR("get_api_fs_info heap exhausted %d", 128);
    }
}

static void post_api_fs_format(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("post_api_fs_format");
    if (espfs.is_available())
    {
        http_response(ptr_espconn, HTTP_ACCEPTED, HTTP_CONTENT_TEXT, "", false);
        os_timer_disarm(&format_delay_timer);
        os_timer_setfn(&format_delay_timer, (os_timer_func_t *)format_function, NULL);
        os_timer_arm(&format_delay_timer, 500, 0);
    }
    else
    {
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("File system is not available"), false);
    }
}

static void get_api_files_ls(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("get_api_files_ls");
    if (!espfs.is_available())
    {
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("File system is not available"), false);
        return;
    }
    int file_cnt = 0;
    struct spiffs_dirent *file_ptr = espfs.list(0);
    // count files first
    while (file_ptr)
    {
        file_cnt++;
        file_ptr = espfs.list(1);
    }
    // now prepare the list
    int file_list_len = 32 + (file_cnt * (32 + 3));
    Heap_chunk file_list(file_list_len, dont_free);
    if (file_list.ref)
    {
        char *tmp_ptr;
        fs_sprintf(file_list.ref, "{\"files\":[");
        file_ptr = espfs.list(0);
        while (file_ptr)
        {
            tmp_ptr = file_list.ref + os_strlen(file_list.ref);
            if (tmp_ptr != (file_list.ref + os_strlen("{\"files\":[")))
                *(tmp_ptr++) = ',';
            os_sprintf(tmp_ptr, "\"%s\"", (char *)file_ptr->name);
            file_ptr = espfs.list(1);
        }
        tmp_ptr = file_list.ref + os_strlen(file_list.ref);
        fs_sprintf(tmp_ptr, "]}");
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, file_list.ref, true);
        espmem.stack_mon();
    }
    else
    {
        esp_diag.error(ROUTES_GET_API_FILES_LS_HEAP_EXHAUSTED, file_list_len);
        ERROR("get_api_files_ls heap exhausted %d", file_list_len);
    }
}

static void get_api_files_cat(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("get_api_files_cat");
    char *file_name = parsed_req->url + os_strlen(f_str("/api/files/cat/"));
    if (os_strlen(file_name) == 0)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("No file name provided"), false);
        return;
    }
    return_file(ptr_espconn, file_name);
}

static void post_api_files_delete(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("post_api_files_delete");
    char *file_name = parsed_req->url + os_strlen(f_str("/api/files/delete/"));
    if (os_strlen(file_name) == 0)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("No file name provided"), false);
        return;
    }
    if (!espfs.is_available())
    {
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("File system is not available"), false);
        return;
    }
    if (!Ffile::exists(&espfs, file_name))
    {
        http_response(ptr_espconn, HTTP_NOT_FOUND, HTTP_CONTENT_JSON, f_str("File not found"), false);
        return;
    }
    Ffile sel_file(&espfs, file_name);
    espmem.stack_mon();
    if (sel_file.is_available())
    {
        http_response(ptr_espconn, HTTP_ACCEPTED, HTTP_CONTENT_TEXT, f_str(""), false);
        sel_file.remove();
    }
    else
    {
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Cannot open file"), false);
    }
}

static void post_api_files_create(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("post_api_files_create");
    char *file_name = parsed_req->url + os_strlen(f_str("/api/files/create/"));
    if (os_strlen(file_name) == 0)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("No file name provided"), false);
        return;
    }
    if (!espfs.is_available())
    {
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("File system is not available"), false);
        return;
    }
    if (Ffile::exists(&espfs, file_name))
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("File already exists"), false);
        return;
    }
    Ffile sel_file(&espfs, file_name);
    espmem.stack_mon();
    if (sel_file.is_available())
    {
        sel_file.n_append(parsed_req->req_content, parsed_req->content_len);
        http_response(ptr_espconn, HTTP_CREATED, HTTP_CONTENT_TEXT, f_str(""), false);
    }
    else
    {
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Cannot open file"), false);
    }
}

static void post_api_files_append(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("post_api_files_append");
    char *file_name = parsed_req->url + os_strlen(f_str("/api/files/append/"));
    if (os_strlen(file_name) == 0)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("No file name provided"), false);
        return;
    }
    if (!espfs.is_available())
    {
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("File system is not available"), false);
        return;
    }
    if (!Ffile::exists(&espfs, file_name))
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("File does not exists"), false);
        return;
    }
    Ffile sel_file(&espfs, file_name);
    espmem.stack_mon();
    if (sel_file.is_available())
    {
        sel_file.n_append(parsed_req->req_content, parsed_req->content_len);
        http_response(ptr_espconn, HTTP_CREATED, HTTP_CONTENT_TEXT, f_str(""), false);
    }
    else
    {
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Cannot open file"), false);
    }
}

static void post_api_gpio_cfg(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("post_api_gpio_cfg");
    int gpio_pin;
    int gpio_type;
    Json_str gpio_cfg(parsed_req->req_content, parsed_req->content_len);
    if (gpio_cfg.syntax_check() != JSON_SINTAX_OK)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Json bad syntax"), false);
        return;
    }
    if (gpio_cfg.find_pair(f_str("gpio_pin")) != JSON_NEW_PAIR_FOUND)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Cannot find JSON string 'gpio_pin'"), false);
        return;
    }
    if (gpio_cfg.get_cur_pair_value_type() != JSON_INTEGER)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("JSON pair with string 'gpio_pin' does not have an integer value type"), false);
        return;
    }
    Heap_chunk tmp_pin(gpio_cfg.get_cur_pair_value_len());
    if (tmp_pin.ref)
    {
        os_strncpy(tmp_pin.ref, gpio_cfg.get_cur_pair_value(), gpio_cfg.get_cur_pair_value_len());
    }
    else
    {
        esp_diag.error(ROUTES_POST_API_GPIO_CFG_HEAP_EXHAUSTED, gpio_cfg.get_cur_pair_value_len() + 1);
        ERROR("post_api_gpio_cfg heap exhausted %d", gpio_cfg.get_cur_pair_value_len() + 1);
        return;
    }
    if (gpio_cfg.find_pair(f_str("gpio_type")) != JSON_NEW_PAIR_FOUND)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Cannot find JSON string 'gpio_type'"), false);
        return;
    }
    if (gpio_cfg.get_cur_pair_value_type() != JSON_STRING)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("JSON pair with string 'gpio_type' does not have a string value type"), false);
        return;
    }
    Heap_chunk tmp_type(gpio_cfg.get_cur_pair_value_len());
    if (tmp_type.ref)
    {
        os_strncpy(tmp_type.ref, gpio_cfg.get_cur_pair_value(), gpio_cfg.get_cur_pair_value_len());
    }
    else
    {
        esp_diag.error(ROUTES_POST_API_GPIO_CFG_HEAP_EXHAUSTED, gpio_cfg.get_cur_pair_value_len() + 1);
        ERROR("post_api_gpio_cfg heap exhausted %d", gpio_cfg.get_cur_pair_value_len() + 1);
        return;
    }
    gpio_pin = atoi(tmp_pin.ref);
    if ((os_strcmp(tmp_type.ref, f_str("INPUT")) == 0) || (os_strcmp(tmp_type.ref, f_str("input")) == 0))
        gpio_type = ESPBOT_GPIO_INPUT;
    else if ((os_strcmp(tmp_type.ref, f_str("OUTPUT")) == 0) || (os_strcmp(tmp_type.ref, f_str("output")) == 0))
        gpio_type = ESPBOT_GPIO_OUTPUT;
    else
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Wrong gpio type"), false);
        return;
    }
    if (esp_gpio.config(gpio_pin, gpio_type) == ESPBOT_GPIO_WRONG_IDX)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Wrong gpio pin"), false);
        return;
    }

    Heap_chunk msg(parsed_req->content_len, dont_free);
    if (msg.ref)
    {
        os_strncpy(msg.ref, parsed_req->req_content, parsed_req->content_len);
        http_response(ptr_espconn, HTTP_CREATED, HTTP_CONTENT_JSON, msg.ref, true);
    }
    else
    {
        esp_diag.error(ROUTES_POST_API_GPIO_CFG_HEAP_EXHAUSTED, parsed_req->content_len);
        ERROR("post_api_gpio_cfg heap exhausted %d", parsed_req->content_len);
    }
    espmem.stack_mon();
}

static void post_api_gpio_uncfg(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("post_api_gpio_uncfg");
    int gpio_pin;
    int gpio_type;
    Json_str gpio_cfg(parsed_req->req_content, parsed_req->content_len);
    if (gpio_cfg.syntax_check() != JSON_SINTAX_OK)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Json bad syntax"), false);
        return;
    }
    if (gpio_cfg.find_pair(f_str("gpio_pin")) != JSON_NEW_PAIR_FOUND)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Cannot find JSON string 'gpio_pin'"), false);
        return;
    }
    if (gpio_cfg.get_cur_pair_value_type() != JSON_INTEGER)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("JSON pair with string 'gpio' does not have an integer value type"), false);
        return;
    }
    Heap_chunk tmp_pin(gpio_cfg.get_cur_pair_value_len());
    if (tmp_pin.ref)
    {
        os_strncpy(tmp_pin.ref, gpio_cfg.get_cur_pair_value(), gpio_cfg.get_cur_pair_value_len());
    }
    else
    {
        esp_diag.error(ROUTES_POST_API_GPIO_UNCFG_HEAP_EXHAUSTED, gpio_cfg.get_cur_pair_value_len() + 1);
        ERROR("post_api_gpio_uncfg heap exhausted %d", gpio_cfg.get_cur_pair_value_len() + 1);
        return;
    }
    gpio_pin = atoi(tmp_pin.ref);
    if (esp_gpio.unconfig(gpio_pin) == ESPBOT_GPIO_WRONG_IDX)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Wrong gpio pin"), false);
        return;
    }

    Heap_chunk msg(48, dont_free);
    if (msg.ref)
    {
        fs_sprintf(msg.ref, "{\"gpio_pin\": %d,\"gpio_type\":\"unprovisioned\"}", gpio_pin);
        http_response(ptr_espconn, HTTP_CREATED, HTTP_CONTENT_JSON, msg.ref, true);
    }
    else
    {
        esp_diag.error(ROUTES_POST_API_GPIO_UNCFG_HEAP_EXHAUSTED, gpio_cfg.get_cur_pair_value_len() + 1);
        ERROR("post_api_gpio_uncfg heap exhausted %d", 48);
    }
    espmem.stack_mon();
}

static void get_api_gpio_cfg(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("get_api_gpio_cfg");
    int gpio_pin;
    int result;
    Json_str gpio_cfg(parsed_req->req_content, parsed_req->content_len);
    if (gpio_cfg.syntax_check() != JSON_SINTAX_OK)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Json bad syntax"), false);
        return;
    }
    if (gpio_cfg.find_pair("gpio_pin") != JSON_NEW_PAIR_FOUND)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Cannot find JSON string 'gpio_pin'"), false);
        return;
    }
    if (gpio_cfg.get_cur_pair_value_type() != JSON_INTEGER)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("JSON pair with string 'gpio_pin' does not have an integer value type"), false);
        return;
    }
    Heap_chunk tmp_pin(gpio_cfg.get_cur_pair_value_len());
    if (tmp_pin.ref)
    {
        os_strncpy(tmp_pin.ref, gpio_cfg.get_cur_pair_value(), gpio_cfg.get_cur_pair_value_len());
    }
    else
    {
        esp_diag.error(ROUTES_GET_API_GPIO_CFG_HEAP_EXHAUSTED, gpio_cfg.get_cur_pair_value_len() + 1);
        ERROR("get_api_gpio_cfg heap exhausted %d", gpio_cfg.get_cur_pair_value_len() + 1);
        return;
    }
    gpio_pin = atoi(tmp_pin.ref);
    espmem.stack_mon();

    Heap_chunk msg(48, dont_free);
    if (msg.ref)
    {
        result = esp_gpio.get_config(gpio_pin);
        switch (result)
        {
        case ESPBOT_GPIO_WRONG_IDX:
            http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Wrong gpio pin"), false);
            return;
        case ESPBOT_GPIO_UNPROVISIONED:
            fs_sprintf(msg.ref, "{\"gpio_pin\": %d,\"gpio_type\":\"unprovisioned\"}", gpio_pin);
            http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
            return;
        case ESPBOT_GPIO_INPUT:
            fs_sprintf(msg.ref, "{\"gpio_pin\": %d,\"gpio_type\":\"input\"}", gpio_pin);
            http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
            return;
        case ESPBOT_GPIO_OUTPUT:
            fs_sprintf(msg.ref, "{\"gpio_pin\": %d,\"gpio_type\":\"output\"}", gpio_pin);
            http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
            return;
        default:
            http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Gpio.get_config error"), false);
            return;
        }
    }
    else
    {
        esp_diag.error(ROUTES_GET_API_GPIO_CFG_HEAP_EXHAUSTED, 48);
        ERROR("get_api_gpio_cfg heap exhausted %d", 48);
    }
    espmem.stack_mon();
}

static void get_api_gpio_read(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("get_api_gpio_read");
    int gpio_pin;
    int result;
    Json_str gpio_cfg(parsed_req->req_content, parsed_req->content_len);
    if (gpio_cfg.syntax_check() != JSON_SINTAX_OK)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Json bad syntax"), false);
        return;
    }
    if (gpio_cfg.find_pair("gpio_pin") != JSON_NEW_PAIR_FOUND)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Cannot find JSON string 'gpio_pin'"), false);
        return;
    }
    if (gpio_cfg.get_cur_pair_value_type() != JSON_INTEGER)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("JSON pair with string 'gpio_pin' does not have an integer value type"), false);
        return;
    }
    Heap_chunk tmp_pin(gpio_cfg.get_cur_pair_value_len() + 1);
    if (tmp_pin.ref)
    {
        os_strncpy(tmp_pin.ref, gpio_cfg.get_cur_pair_value(), gpio_cfg.get_cur_pair_value_len());
    }
    else
    {
        esp_diag.error(ROUTES_GET_API_GPIO_READ_HEAP_EXHAUSTED, gpio_cfg.get_cur_pair_value_len() + 1);
        ERROR("get_api_gpio_read heap exhausted %d", gpio_cfg.get_cur_pair_value_len() + 1);
        return;
    }
    gpio_pin = atoi(tmp_pin.ref);

    Heap_chunk msg(48, dont_free);
    if (msg.ref)
    {
        result = esp_gpio.read(gpio_pin);
        switch (result)
        {
        case ESPBOT_GPIO_WRONG_IDX:
            http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Wrong gpio pin"), false);
            return;
        case ESPBOT_GPIO_UNPROVISIONED:
            fs_sprintf(msg.ref, "{\"gpio_pin\": %d,\"gpio_type\":\"unprovisioned\"}", gpio_pin);
            break;
        case ESPBOT_LOW:
            fs_sprintf(msg.ref, "{\"gpio_pin\": %d,\"gpio_status\":\"LOW\"}", gpio_pin);
            break;
        case ESPBOT_HIGH:
            fs_sprintf(msg.ref, "{\"gpio_pin\": %d,\"gpio_status\":\"HIGH\"}", gpio_pin);
            break;
        default:
            break;
        }
    }
    else
    {
        esp_diag.error(ROUTES_GET_API_GPIO_READ_HEAP_EXHAUSTED, 48);
        ERROR("get_api_gpio_read heap exhausted %d", 64);
    }
    http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
    espmem.stack_mon();
}

static void post_api_gpio_set(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("post_api_gpio_set");
    int gpio_pin;
    int output_level;
    int result;
    Json_str gpio_cfg(parsed_req->req_content, parsed_req->content_len);
    if (gpio_cfg.syntax_check() != JSON_SINTAX_OK)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Json bad syntax"), false);
        return;
    }
    if (gpio_cfg.find_pair(f_str("gpio_pin")) != JSON_NEW_PAIR_FOUND)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Cannot find JSON string 'gpio_pin'"), false);
        return;
    }
    if (gpio_cfg.get_cur_pair_value_type() != JSON_INTEGER)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("JSON pair with string 'gpio_pin' does not have an integer value type"), false);
        return;
    }
    Heap_chunk tmp_pin(gpio_cfg.get_cur_pair_value_len() + 1);
    if (tmp_pin.ref)
    {
        os_strncpy(tmp_pin.ref, gpio_cfg.get_cur_pair_value(), gpio_cfg.get_cur_pair_value_len());
    }
    else
    {
        esp_diag.error(ROUTES_GET_API_GPIO_SET_HEAP_EXHAUSTED, gpio_cfg.get_cur_pair_value_len() + 1);
        ERROR("post_api_gpio_set heap exhausted %d", gpio_cfg.get_cur_pair_value_len() + 1);
        return;
    }
    if (gpio_cfg.find_pair(f_str("gpio_status")) != JSON_NEW_PAIR_FOUND)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Cannot find JSON string 'gpio_status'"), false);
        return;
    }
    if (gpio_cfg.get_cur_pair_value_type() != JSON_STRING)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("JSON pair with string 'gpio_status' does not have a string value type"), false);
        return;
    }
    Heap_chunk tmp_level(gpio_cfg.get_cur_pair_value_len() + 1);
    if (tmp_level.ref)
    {
        os_strncpy(tmp_level.ref, gpio_cfg.get_cur_pair_value(), gpio_cfg.get_cur_pair_value_len());
    }
    else
    {
        esp_diag.error(ROUTES_GET_API_GPIO_SET_HEAP_EXHAUSTED, gpio_cfg.get_cur_pair_value_len() + 1);
        ERROR("post_api_gpio_set heap exhausted %d", gpio_cfg.get_cur_pair_value_len() + 1);
        return;
    }
    gpio_pin = atoi(tmp_pin.ref);
    if ((os_strcmp(tmp_level.ref, f_str("LOW")) == 0) || (os_strcmp(tmp_level.ref, f_str("low")) == 0))
        output_level = ESPBOT_LOW;
    else if ((os_strcmp(tmp_level.ref, f_str("HIGH")) == 0) || (os_strcmp(tmp_level.ref, f_str("high")) == 0))
        output_level = ESPBOT_HIGH;
    else
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Wrong gpio level"), false);
        return;
    }

    Heap_chunk msg(48, dont_free);
    if (msg.ref)
    {
        result = esp_gpio.set(gpio_pin, output_level);
        switch (result)
        {
        case ESPBOT_GPIO_WRONG_IDX:
            http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Wrong gpio pin"), false);
            return;
        case ESPBOT_GPIO_UNPROVISIONED:
            fs_sprintf(msg.ref, "{\"gpio_pin\": %d,\"gpio_type\":\"unprovisioned\"}", gpio_pin);
            http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, msg.ref, true);
            return;
        case ESPBOT_GPIO_WRONG_LVL:
            http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Wrong output level"), false);
            return;
        case ESPBOT_GPIO_CANNOT_CHANGE_INPUT:
            http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Cannot change input"), false);
            return;
        case ESPBOT_GPIO_OK:
            os_strncpy(msg.ref, parsed_req->req_content, parsed_req->content_len);
            http_response(ptr_espconn, HTTP_CREATED, HTTP_CONTENT_JSON, msg.ref, true);
            return;
        default:
            break;
        }
    }
    else
    {
        esp_diag.error(ROUTES_GET_API_GPIO_SET_HEAP_EXHAUSTED, 48);
        ERROR("post_api_gpio_set heap exhausted %d", 48);
    }
    espmem.stack_mon();
}

static void get_api_mdns(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("get_api_mdns");
    // "{"mdns_enabled": 0}" 20 chars
    int msg_len = 20;
    Heap_chunk msg(msg_len, dont_free);
    if (msg.ref)
    {
        fs_sprintf(msg.ref,
                   "{\"mdns_enabled\": %d}",
                   esp_mDns.is_enabled());
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
    }
    else
    {
        esp_diag.error(ROUTES_GET_MDNS_HEAP_EXHAUSTED, msg_len);
        ERROR("get_api_mdns heap exhausted %d", msg_len);
    }
}

static void post_api_mdns(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("post_api_mdns");
    Json_str mdns_cfg(parsed_req->req_content, parsed_req->content_len);
    if (mdns_cfg.syntax_check() != JSON_SINTAX_OK)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Json bad syntax"), false);
        return;
    }
    if (mdns_cfg.find_pair(f_str("mdns_enabled")) != JSON_NEW_PAIR_FOUND)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Cannot find JSON string 'mdns_enabled'"), false);
        return;
    }
    if (mdns_cfg.get_cur_pair_value_type() != JSON_INTEGER)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("JSON pair with string 'mdns_enabled' does not have a INTEGER value type"), false);
        return;
    }
    Heap_chunk tmp_mdns_enabled(mdns_cfg.get_cur_pair_value_len() + 1);
    if (tmp_mdns_enabled.ref)
    {
        os_strncpy(tmp_mdns_enabled.ref, mdns_cfg.get_cur_pair_value(), mdns_cfg.get_cur_pair_value_len());
    }
    else
    {
        esp_diag.error(ROUTES_POST_MDNS_HEAP_EXHAUSTED, mdns_cfg.get_cur_pair_value_len() + 1);
        ERROR("post_api_mdns heap exhausted %d", mdns_cfg.get_cur_pair_value_len() + 1);
        return;
    }
    if (atoi(tmp_mdns_enabled.ref))
        esp_mDns.enable();
    else
        esp_mDns.disable();
    esp_mDns.save_cfg();

    int msg_len = 36;
    Heap_chunk msg(msg_len, dont_free);
    if (msg.ref)
    {
        fs_sprintf(msg.ref,
                   "{\"mdns_enabled\": %d}",
                   esp_mDns.is_enabled());
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
    }
    else
    {
        esp_diag.error(ROUTES_POST_MDNS_HEAP_EXHAUSTED, msg_len);
        ERROR("get_api_mdns heap exhausted %d", msg_len);
    }
    espmem.stack_mon();
}

static void get_api_sntp(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("get_api_sntp");
    // "{"sntp_enabled": 0,"timezone": -12}" 36 chars
    int msg_len = 36;
    Heap_chunk msg(msg_len, dont_free);
    if (msg.ref)
    {
        fs_sprintf(msg.ref,
                   "{\"sntp_enabled\": %d,\"timezone\": %d}",
                   esp_time.sntp_enabled(),
                   esp_time.get_timezone());
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
    }
    else
    {
        esp_diag.error(ROUTES_GET_SNTP_HEAP_EXHAUSTED, msg_len);
        ERROR("get_api_sntp heap exhausted %d", msg_len);
    }
}

static void post_api_sntp(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("post_api_sntp");
    Json_str time_date_cfg(parsed_req->req_content, parsed_req->content_len);
    if (time_date_cfg.syntax_check() != JSON_SINTAX_OK)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Json bad syntax"), false);
        return;
    }
    if (time_date_cfg.find_pair(f_str("sntp_enabled")) != JSON_NEW_PAIR_FOUND)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Cannot find JSON string 'sntp_enabled'"), false);
        return;
    }
    if (time_date_cfg.get_cur_pair_value_type() != JSON_INTEGER)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("JSON pair with string 'sntp_enabled' does not have a INTEGER value type"), false);
        return;
    }
    Heap_chunk tmp_sntp_enabled(time_date_cfg.get_cur_pair_value_len() + 1);
    if (tmp_sntp_enabled.ref)
    {
        os_strncpy(tmp_sntp_enabled.ref, time_date_cfg.get_cur_pair_value(), time_date_cfg.get_cur_pair_value_len());
    }
    else
    {
        esp_diag.error(ROUTES_POST_API_SNTP_HEAP_EXHAUSTED, time_date_cfg.get_cur_pair_value_len() + 1);
        ERROR("post_api_sntp heap exhausted %d", time_date_cfg.get_cur_pair_value_len() + 1);
        return;
    }
    if (time_date_cfg.find_pair(f_str("timezone")) != JSON_NEW_PAIR_FOUND)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Cannot find JSON string 'timezone'"), false);
        return;
    }
    if (time_date_cfg.get_cur_pair_value_type() != JSON_INTEGER)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("JSON pair with string 'timezone' does not have a INTEGER value type"), false);
        return;
    }
    Heap_chunk tmp_timezone(time_date_cfg.get_cur_pair_value_len() + 1);
    if (tmp_timezone.ref)
    {
        os_strncpy(tmp_timezone.ref, time_date_cfg.get_cur_pair_value(), time_date_cfg.get_cur_pair_value_len());
    }
    else
    {
        esp_diag.error(ROUTES_POST_API_SNTP_HEAP_EXHAUSTED, time_date_cfg.get_cur_pair_value_len() + 1);
        ERROR("post_api_sntp heap exhausted %d", time_date_cfg.get_cur_pair_value_len() + 1);
        return;
    }
    if (atoi(tmp_sntp_enabled.ref))
        esp_time.enable_sntp();
    else
        esp_time.disable_sntp();
    esp_time.set_timezone(atoi(tmp_timezone.ref));
    esp_time.save_cfg();

    int msg_len = 36;
    Heap_chunk msg(msg_len, dont_free);
    if (msg.ref)
    {
        fs_sprintf(msg.ref,
                   "{\"sntp_enabled\": %d,\"timezone\": %d}",
                   esp_time.sntp_enabled(),
                   esp_time.get_timezone());
        http_response(ptr_espconn, HTTP_CREATED, HTTP_CONTENT_JSON, msg.ref, true);
    }
    else
    {
        esp_diag.error(ROUTES_POST_API_SNTP_HEAP_EXHAUSTED, msg_len);
        ERROR("post_api_sntp heap exhausted %d", msg_len);
    }
    espmem.stack_mon();
}

static void post_api_timedate(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("post_api_timedate");
    Json_str timedate_val(parsed_req->req_content, parsed_req->content_len);
    if (timedate_val.syntax_check() != JSON_SINTAX_OK)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Json bad syntax"), false);
        return;
    }
    if (timedate_val.find_pair(f_str("timedate")) != JSON_NEW_PAIR_FOUND)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Cannot find JSON string 'timedate'"), false);
        return;
    }
    if (timedate_val.get_cur_pair_value_type() != JSON_INTEGER)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("JSON pair with string 'timedate' does not have a INTEGER value type"), false);
        return;
    }
    Heap_chunk tmp_timedate(timedate_val.get_cur_pair_value_len() + 1);
    if (tmp_timedate.ref)
    {
        os_strncpy(tmp_timedate.ref, timedate_val.get_cur_pair_value(), timedate_val.get_cur_pair_value_len());
    }
    else
    {
        esp_diag.error(ROUTES_POST_API_TIMEDATE_HEAP_EXHAUSTED, timedate_val.get_cur_pair_value_len() + 1);
        ERROR("post_api_timedate heap exhausted %d", timedate_val.get_cur_pair_value_len() + 1);
        return;
    }
    esp_time.set_time_manually(atoi(tmp_timedate.ref));
    
    // "{"timedate": 01234567891}"
    int msg_len = 26;
    Heap_chunk msg(msg_len, dont_free);
    if (msg.ref)
    {
        fs_sprintf(msg.ref,
                   "{\"timedate\": %s}",
                   tmp_timedate.ref);
        http_response(ptr_espconn, HTTP_CREATED, HTTP_CONTENT_JSON, msg.ref, true);
    }
    else
    {
        esp_diag.error(ROUTES_POST_API_TIMEDATE_HEAP_EXHAUSTED, msg_len);
        ERROR("post_api_timedate heap exhausted %d", msg_len);
    }
    espmem.stack_mon();
}

static void get_api_ota_info(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("get_api_ota_info");
    Heap_chunk msg(36, dont_free);

    if (msg.ref)
    {
        fs_sprintf(msg.ref, "{\"ota_status\": %d}", esp_ota.get_status());
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
    }
    else
    {
        esp_diag.error(ROUTES_GET_API_OTA_INFO_HEAP_EXHAUSTED, 36);
        ERROR("get_api_ota_info heap exhausted %d", 36);
    }
}

static void get_api_ota_cfg(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("get_api_ota_cfg");
    int msg_len = 90 +
                  16 +
                  6 +
                  os_strlen(esp_ota.get_path()) +
                  10;
    Heap_chunk msg(msg_len, dont_free);
    if (msg.ref)
    {
        fs_sprintf(msg.ref,
                   "{\"host\":\"%s\",\"port\":%d,\"path\":\"%s\",",
                   esp_ota.get_host(),
                   esp_ota.get_port(),
                   esp_ota.get_path());
        fs_sprintf((msg.ref + os_strlen(msg.ref)),
                   "\"check_version\":\"%s\",\"reboot_on_completion\":\"%s\"}",
                   esp_ota.get_check_version(),
                   esp_ota.get_reboot_on_completion());
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
    }
    else
    {
        esp_diag.error(ROUTES_GET_API_OTA_CFG_HEAP_EXHAUSTED, msg_len);
        ERROR("get_api_ota_cfg heap exhausted %d", msg_len);
    }
}

static void post_api_ota_cfg(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("post_api_ota_cfg");
    Json_str ota_cfg(parsed_req->req_content, parsed_req->content_len);
    if (ota_cfg.syntax_check() != JSON_SINTAX_OK)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Json bad syntax"), false);
        return;
    }
    if (ota_cfg.find_pair(f_str("host")) != JSON_NEW_PAIR_FOUND)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Cannot find JSON string 'host'"), false);
        return;
    }
    if (ota_cfg.get_cur_pair_value_type() != JSON_STRING)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("JSON pair with string 'host' does not have a string value type"), false);
        return;
    }
    Heap_chunk tmp_host(ota_cfg.get_cur_pair_value_len() + 1);
    if (tmp_host.ref)
    {
        os_strncpy(tmp_host.ref, ota_cfg.get_cur_pair_value(), ota_cfg.get_cur_pair_value_len());
    }
    else
    {
        esp_diag.error(ROUTES_POST_API_OTA_CFG_HEAP_EXHAUSTED, ota_cfg.get_cur_pair_value_len() + 1);
        ERROR("post_api_ota_cfg heap exhausted %d", ota_cfg.get_cur_pair_value_len() + 1);
        return;
    }
    if (ota_cfg.find_pair(f_str("port")) != JSON_NEW_PAIR_FOUND)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Cannot find JSON string 'port'"), false);
        return;
    }
    if (ota_cfg.get_cur_pair_value_type() != JSON_INTEGER)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("JSON pair with string 'port' does not have an integer value type"), false);
        return;
    }
    Heap_chunk tmp_port(ota_cfg.get_cur_pair_value_len() + 1);
    if (tmp_port.ref)
    {
        os_strncpy(tmp_port.ref, ota_cfg.get_cur_pair_value(), ota_cfg.get_cur_pair_value_len());
    }
    else
    {
        esp_diag.error(ROUTES_POST_API_OTA_CFG_HEAP_EXHAUSTED, ota_cfg.get_cur_pair_value_len() + 1);
        ERROR("post_api_ota_cfg heap exhausted %d", ota_cfg.get_cur_pair_value_len() + 1);
        return;
    }
    if (ota_cfg.find_pair(f_str("path")) != JSON_NEW_PAIR_FOUND)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Cannot find JSON string 'path'"), false);
        return;
    }
    if (ota_cfg.get_cur_pair_value_type() != JSON_STRING)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("JSON pair with string 'path' does not have a string value type"), false);
        return;
    }
    Heap_chunk tmp_path(ota_cfg.get_cur_pair_value_len());
    if (tmp_path.ref)
    {
        os_strncpy(tmp_path.ref, ota_cfg.get_cur_pair_value(), ota_cfg.get_cur_pair_value_len());
    }
    else
    {
        esp_diag.error(ROUTES_POST_API_OTA_CFG_HEAP_EXHAUSTED, ota_cfg.get_cur_pair_value_len() + 1);
        ERROR("post_api_ota_cfg heap exhausted %d", ota_cfg.get_cur_pair_value_len() + 1);
        return;
    }
    if (ota_cfg.find_pair(f_str("check_version")) != JSON_NEW_PAIR_FOUND)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Cannot find JSON string 'check_version'"), false);
        return;
    }
    if (ota_cfg.get_cur_pair_value_type() != JSON_STRING)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("JSON pair with string 'check_version' does not have a string value type"), false);
        return;
    }
    Heap_chunk tmp_check_version(ota_cfg.get_cur_pair_value_len());
    if (tmp_check_version.ref)
    {
        os_strncpy(tmp_check_version.ref, ota_cfg.get_cur_pair_value(), ota_cfg.get_cur_pair_value_len());
    }
    else
    {
        esp_diag.error(ROUTES_POST_API_OTA_CFG_HEAP_EXHAUSTED, ota_cfg.get_cur_pair_value_len() + 1);
        ERROR("post_api_ota_cfg heap exhausted %d", ota_cfg.get_cur_pair_value_len() + 1);
        return;
    }
    if (ota_cfg.find_pair(f_str("reboot_on_completion")) != JSON_NEW_PAIR_FOUND)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Cannot find JSON string 'reboot_on_completion'"), false);
        return;
    }
    if (ota_cfg.get_cur_pair_value_type() != JSON_STRING)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("JSON pair with string 'reboot_on_completion' does not have a string value type"), false);
        return;
    }
    Heap_chunk tmp_reboot_on_completion(ota_cfg.get_cur_pair_value_len());
    if (tmp_reboot_on_completion.ref)
    {
        os_strncpy(tmp_reboot_on_completion.ref, ota_cfg.get_cur_pair_value(), ota_cfg.get_cur_pair_value_len());
    }
    else
    {
        esp_diag.error(ROUTES_POST_API_OTA_CFG_HEAP_EXHAUSTED, ota_cfg.get_cur_pair_value_len() + 1);
        ERROR("post_api_ota_cfg heap exhausted %d", ota_cfg.get_cur_pair_value_len() + 1);
        return;
    }
    esp_ota.set_host(tmp_host.ref);
    esp_ota.set_port(tmp_port.ref);
    esp_ota.set_path(tmp_path.ref);
    esp_ota.set_check_version(tmp_check_version.ref);
    esp_ota.set_reboot_on_completion(tmp_reboot_on_completion.ref);
    esp_ota.save_cfg();

    int msg_len = 85 +
                  16 +
                  6 +
                  os_strlen(esp_ota.get_path()) +
                  os_strlen(esp_ota.get_check_version()) +
                  os_strlen(esp_ota.get_reboot_on_completion());
    Heap_chunk msg(msg_len, dont_free);
    if (msg.ref)
    {
        fs_sprintf(msg.ref,
                   "{\"host\":\"%s\",\"port\":%d,\"path\":\"%s\",",
                   esp_ota.get_host(),
                   esp_ota.get_port(),
                   esp_ota.get_path());
        fs_sprintf((msg.ref + os_strlen(msg.ref)),
                   "\"check_version\":\"%s\",\"reboot_on_completion\":\"%s\"}",
                   esp_ota.get_check_version(),
                   esp_ota.get_reboot_on_completion());
        http_response(ptr_espconn, HTTP_CREATED, HTTP_CONTENT_JSON, msg.ref, true);
    }
    else
    {
        esp_diag.error(ROUTES_POST_API_OTA_CFG_HEAP_EXHAUSTED, msg_len);
        ERROR("post_api_ota_cfg heap exhausted %d", msg_len);
    }
    espmem.stack_mon();
}

static void get_api_wifi_cfg(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("get_api_wifi_cfg");
    Heap_chunk msg(64, dont_free);
    if (msg.ref)
    {
        fs_sprintf(msg.ref,
                   "{\"station_ssid\":\"%s\",\"station_pwd\":\"%s\"}",
                   Wifi::station_get_ssid(),
                   Wifi::station_get_password());
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
    }
    else
    {
        esp_diag.error(ROUTES_GET_API_WIFI_CFG_HEAP_EXHAUSTED, 64);
        ERROR("get_api_wifi_cfg heap exhausted %d", 64);
    }
}

static void post_api_wifi_cfg(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("post_api_wifi_cfg");
    Json_str wifi_cfg(parsed_req->req_content, parsed_req->content_len);
    if (wifi_cfg.syntax_check() != JSON_SINTAX_OK)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Json bad syntax"), false);
        return;
    }
    if (wifi_cfg.find_pair(f_str("station_ssid")) != JSON_NEW_PAIR_FOUND)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Cannot find JSON string 'station_ssid'"), false);
        return;
    }
    if (wifi_cfg.get_cur_pair_value_type() != JSON_STRING)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("JSON pair with string 'station_ssid' does not have a STRING value type"), false);
        return;
    }
    char *tmp_ssid = wifi_cfg.get_cur_pair_value();
    int tmp_ssid_len = wifi_cfg.get_cur_pair_value_len();
    if (wifi_cfg.find_pair(f_str("station_pwd")) != JSON_NEW_PAIR_FOUND)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Cannot find JSON string 'station_pwd'"), false);
        return;
    }
    if (wifi_cfg.get_cur_pair_value_type() != JSON_STRING)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("JSON pair with string 'station_pwd' does not have an integer value type"), false);
        return;
    }
    Wifi::station_set_ssid(tmp_ssid, tmp_ssid_len);
    Wifi::station_set_pwd(wifi_cfg.get_cur_pair_value(), wifi_cfg.get_cur_pair_value_len());
    Wifi::connect();
    espmem.stack_mon();

    Heap_chunk msg(140, dont_free);
    if (msg.ref)
    {
        fs_sprintf(msg.ref,
                   "{\"station_ssid\":\"%s\",\"station_pwd\":\"%s\"}",
                   Wifi::station_get_ssid(),
                   Wifi::station_get_password());
        http_response(ptr_espconn, HTTP_CREATED, HTTP_CONTENT_JSON, msg.ref, true);
    }
    else
    {
        esp_diag.error(ROUTES_POST_API_WIFI_CFG_HEAP_EXHAUSTED, 140);
        ERROR("post_api_wifi_cfg heap exhausted %d", 140);
    }
}

static void get_api_wifi_info(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("get_api_wifi_info");
    Heap_chunk msg(44 + 32 + 42, dont_free);
    if (msg.ref)
    {
        switch (Wifi::get_op_mode())
        {
        case STATION_MODE:
            fs_sprintf(msg.ref, "{\"op_mode\":\"STATION\",\"SSID\":\"%s\",", Wifi::station_get_ssid());
            break;
        case SOFTAP_MODE:
            fs_sprintf(msg.ref, "{\"op_mode\":\"AP\",");
            break;
        case STATIONAP_MODE:
            fs_sprintf(msg.ref, "{\"op_mode\":\"AP\",");
            break;
        default:
            break;
        }
        char *ptr = msg.ref + os_strlen(msg.ref);
        struct ip_info tmp_ip;
        Wifi::get_ip_address(&tmp_ip);
        char *ip_ptr = (char *)&tmp_ip.ip.addr;
        fs_sprintf(ptr, "\"ip_address\":\"%d.%d.%d.%d\"}", ip_ptr[0], ip_ptr[1], ip_ptr[2], ip_ptr[3]);
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
    }
    else
    {
        esp_diag.error(ROUTES_GET_API_WIFI_INFO_HEAP_EXHAUSTED, 44 + 32 + 42);
        ERROR("get_api_wifi_info heap exhausted %d", 44 + 32 + 42);
    }
}

void espbot_http_routes(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("espbot_http_routes");

    if (parsed_req->req_method == HTTP_OPTIONS) // HTTP CORS
    {
        preflight_response(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/"))) && (parsed_req->req_method == HTTP_GET))
    {
        return_file(ptr_espconn, (char *) f_str("index.html"));
        return;
    }
    if ((os_strncmp(parsed_req->url, f_str("/api/"), 5)) && (parsed_req->req_method == HTTP_GET))
    {
        // not an api: look for specified file
        char *file_name = parsed_req->url + os_strlen("/");
        return_file(ptr_espconn, file_name);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/cron"))) && (parsed_req->req_method == HTTP_GET))
    {
        get_api_cron(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/cron"))) && (parsed_req->req_method == HTTP_POST))
    {
        post_api_cron(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/debug/hexmemdump"))) && (parsed_req->req_method == HTTP_GET))
    {
        get_api_debug_hexmemdump(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/debug/memdump"))) && (parsed_req->req_method == HTTP_GET))
    {
        get_api_debug_memdump(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/debug/meminfo"))) && (parsed_req->req_method == HTTP_GET))
    {
        get_api_debug_meminfo(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/diag/ack_events"))) && (parsed_req->req_method == HTTP_POST))
    {
        post_api_diag_ack_events(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/diag/cfg"))) && (parsed_req->req_method == HTTP_GET))
    {
        get_api_diag_cfg(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/diag/cfg"))) && (parsed_req->req_method == HTTP_POST))
    {
        post_api_diag_cfg(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/diag/events"))) && (parsed_req->req_method == HTTP_GET))
    {
        get_api_diag_events(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/espbot/cfg"))) && (parsed_req->req_method == HTTP_GET))
    {
        get_api_espbot_cfg(ptr_espconn, parsed_req);
        return;
    }
    if ((os_strcmp(parsed_req->url, f_str("/api/espbot/cfg")) == 0) && (parsed_req->req_method == HTTP_POST))
    {
        post_api_espbot_cfg(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/espbot/reset"))) && (parsed_req->req_method == HTTP_POST))
    {
        http_response(ptr_espconn, HTTP_ACCEPTED, HTTP_CONTENT_TEXT, "", false);
        espbot.reset(ESP_REBOOT);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/fs/info"))) && (parsed_req->req_method == HTTP_GET))
    {
        get_api_fs_info(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/fs/format"))) && (parsed_req->req_method == HTTP_POST))
    {
        post_api_fs_format(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/files/ls"))) && (parsed_req->req_method == HTTP_GET))
    {
        get_api_files_ls(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strncmp(parsed_req->url, f_str("/api/files/cat/"), os_strlen(f_str("/api/files/cat/")))) && (parsed_req->req_method == HTTP_GET))
    {
        get_api_files_cat(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strncmp(parsed_req->url, f_str("/api/files/delete/"), os_strlen(f_str("/api/files/delete/")))) && (parsed_req->req_method == HTTP_POST))
    {
        post_api_files_delete(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strncmp(parsed_req->url, f_str("/api/files/create/"), os_strlen(f_str("/api/files/create/")))) && (parsed_req->req_method == HTTP_POST))
    {
        post_api_files_create(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strncmp(parsed_req->url, f_str("/api/files/append/"), os_strlen(f_str("/api/files/append/")))) && (parsed_req->req_method == HTTP_POST))
    {
        post_api_files_append(ptr_espconn, parsed_req);
        return;
    }
    if ((os_strcmp(parsed_req->url, f_str("/api/gpio/cfg")) == 0) && (parsed_req->req_method == HTTP_POST))
    {
        post_api_gpio_cfg(ptr_espconn, parsed_req);
        return;
    }
    if ((os_strcmp(parsed_req->url, f_str("/api/gpio/uncfg")) == 0) && (parsed_req->req_method == HTTP_POST))
    {
        post_api_gpio_uncfg(ptr_espconn, parsed_req);
        return;
    }
    if ((os_strcmp(parsed_req->url, f_str("/api/gpio/cfg")) == 0) && (parsed_req->req_method == HTTP_GET))
    {
        get_api_gpio_cfg(ptr_espconn, parsed_req);
        return;
    }
    if ((os_strcmp(parsed_req->url, f_str("/api/gpio/read")) == 0) && (parsed_req->req_method == HTTP_GET))
    {
        get_api_gpio_read(ptr_espconn, parsed_req);
        return;
    }
    if ((os_strcmp(parsed_req->url, f_str("/api/gpio/set")) == 0) && (parsed_req->req_method == HTTP_POST))
    {
        post_api_gpio_set(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/mdns"))) && (parsed_req->req_method == HTTP_GET))
    {
        get_api_mdns(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/mdns"))) && (parsed_req->req_method == HTTP_POST))
    {
        post_api_mdns(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/sntp"))) && (parsed_req->req_method == HTTP_GET))
    {
        get_api_sntp(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/sntp"))) && (parsed_req->req_method == HTTP_POST))
    {
        post_api_sntp(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/timedate"))) && (parsed_req->req_method == HTTP_POST))
    {
        post_api_timedate(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/ota/info"))) && (parsed_req->req_method == HTTP_GET))
    {
        get_api_ota_info(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/ota/cfg"))) && (parsed_req->req_method == HTTP_GET))
    {
        get_api_ota_cfg(ptr_espconn, parsed_req);
        return;
    }
    if ((os_strcmp(parsed_req->url, f_str("/api/ota/cfg")) == 0) && (parsed_req->req_method == HTTP_POST))
    {
        post_api_ota_cfg(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/ota/reboot"))) && (parsed_req->req_method == HTTP_POST))
    {
        http_response(ptr_espconn, HTTP_ACCEPTED, HTTP_CONTENT_TEXT, "", false);
        espbot.reset(ESP_OTA_REBOOT);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/ota/upgrade"))) && (parsed_req->req_method == HTTP_POST))
    {
        http_response(ptr_espconn, HTTP_ACCEPTED, HTTP_CONTENT_TEXT, "", false);
        esp_ota.start_upgrade();
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/wifi/cfg"))) && (parsed_req->req_method == HTTP_GET))
    {
        get_api_wifi_cfg(ptr_espconn, parsed_req);
        return;
    }
    if ((os_strcmp(parsed_req->url, f_str("/api/wifi/cfg")) == 0) && (parsed_req->req_method == HTTP_POST))
    {
        post_api_wifi_cfg(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/wifi/info"))) && (parsed_req->req_method == HTTP_GET))
    {
        get_api_wifi_info(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/wifi/scan"))) && (parsed_req->req_method == HTTP_GET))
    {
        Wifi::scan_for_ap(NULL, wifi_scan_completed_function, (void *)ptr_espconn);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/wifi/connect"))) && (parsed_req->req_method == HTTP_POST))
    {
        http_response(ptr_espconn, HTTP_ACCEPTED, HTTP_CONTENT_TEXT, "", false);
        Wifi::connect();
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/wifi/disconnect"))) && (parsed_req->req_method == HTTP_POST))
    {
        http_response(ptr_espconn, HTTP_ACCEPTED, HTTP_CONTENT_TEXT, "", false);
        Wifi::set_stationap();
        return;
    }
    //
    // now the custom app routes
    //
    if (app_http_routes(ptr_espconn, parsed_req))
        return;
    else
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("I'm sorry, my responses are limited. You must ask the right question."), false);
}