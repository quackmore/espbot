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
#include "espbot_gpio.hpp"
#include "espbot_global.hpp"
#include "espbot_http.hpp"
#include "espbot_http_routes.hpp"
#include "espbot_json.hpp"
#include "espbot_mem_mon.hpp"
#include "espbot_mdns.hpp"
#include "espbot_spiffs.hpp"
#include "espbot_timedate.hpp"
#include "espbot_utils.hpp"
#include "espbot_webserver.hpp"
#include "espbot_wifi.hpp"

static os_timer_t delay_timer;

void init_controllers(void)
{
    os_timer_disarm(&delay_timer);
}

static void getAPlist(void *param)
{
    struct espconn *ptr_espconn = (struct espconn *)param;
    ALL("getAPlist");
    char *scan_list = espwifi_scan_results_json_stringify();
    if (scan_list)
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, scan_list, true);
    else
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
    espwifi_free_ap_list();
}

static const char *get_file_mime_type(char *filename)
{
    ALL("get_file_mime_type");
    char *ptr;
    ptr = (char *)os_strstr(filename, f_str("."));
    if (ptr == NULL)
        return f_str("application/octet-stream");
    else
    {
        if (os_strcmp(ptr, f_str(".css")) == 0)
            return f_str("text/css");
        else if (os_strcmp(ptr, f_str(".html")) == 0)
            return f_str("text/html");
        else if (os_strcmp(ptr, f_str(".js")) == 0)
            return f_str("text/javascript");
        else if (os_strcmp(ptr, f_str(".cfg")) == 0)
            return f_str("text/plain");
        else if (os_strcmp(ptr, f_str(".log")) == 0)
            return f_str("text/plain");
        else if (os_strcmp(ptr, f_str(".prg")) == 0)
            return f_str("text/plain");
        else if (os_strcmp(ptr, f_str(".txt")) == 0)
            return f_str("text/plain");
        else if (os_strcmp(ptr, f_str(".jpeg")) == 0)
            return f_str("image/jpeg");
        else if (os_strcmp(ptr, f_str(".jpg")) == 0)
            return f_str("image/jpeg");
        else if (os_strcmp(ptr, f_str(".png")) == 0)
            return f_str("image/png");
        else
            return f_str("application/octet-stream");
    }
}

static void send_remaining_file(struct http_split_send *p_sr)
{
    ALL("send_remaining_file");
    if (!http_espconn_in_use(p_sr->p_espconn))
    {
        TRACE("send_remaining_file espconn %X state %d, abort", p_sr->p_espconn, p_sr->p_espconn->state);
        delete[] p_sr->content;
        // there will be no send, so trigger a check of pending send
        system_os_post(USER_TASK_PRIO_0, SIG_http_checkPendingResponse, '0');
        return;
    }

    int remaining_size = p_sr->content_size - p_sr->content_transferred;
    if (remaining_size > get_http_msg_max_size())
    {
        // the remaining file size is bigger than response_max_size
        // will split the remaining file over multiple messages
        int buffer_size = get_http_msg_max_size();
        Heap_chunk buffer(buffer_size, dont_free);
        struct http_split_send *p_pending_response = new struct http_split_send;
        if ((buffer.ref == NULL) || (p_pending_response == NULL))
        {
            delete[] p_sr->content;
            int mem_size = sizeof(struct http_split_send);
            if (buffer.ref == NULL)
            {
                mem_size = buffer_size;
                delete[] buffer.ref;
            }
            dia_error_evnt(ROUTES_SEND_REMAINING_MSG_HEAP_EXHAUSTED, mem_size);
            ERROR("send_remaining_file heap exhausted %d", mem_size);
            http_response(p_sr->p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
            return;
        }
        Espfile sel_file(p_sr->content);
        int res = sel_file.n_read(buffer.ref, p_sr->content_transferred, buffer_size);
        if (res < SPIFFS_OK)
        {
            delete[] p_sr->content;
            delete[] buffer.ref;
            delete p_pending_response;
            http_response(p_sr->p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Error reading file"), false);
            return;
        }

        // setup the remaining message
        p_pending_response->p_espconn = p_sr->p_espconn;
        p_pending_response->order = p_sr->order + 1;
        p_pending_response->content = p_sr->content;
        p_pending_response->content_size = p_sr->content_size;
        p_pending_response->content_transferred = p_sr->content_transferred + buffer_size;
        p_pending_response->action_function = send_remaining_file;
        Queue_err result = pending_split_send->push(p_pending_response);
        if (result == Queue_full)
        {
            delete[] p_sr->content;
            delete[] buffer.ref;
            delete p_pending_response;
            dia_error_evnt(ROUTES_SEND_REMAINING_MSG_PENDING_RES_QUEUE_FULL);
            ERROR("send_remaining_file full pending res queue");
            return;
        }
        TRACE("send_remaining_file: *p_espconn: %X, msg (splitted) len: %d",
              p_sr->p_espconn, buffer_size);
        http_send_buffer(p_sr->p_espconn, p_sr->order, buffer.ref, buffer_size);
    }
    else
    {
        // this is the last piece of the message
        Heap_chunk buffer(remaining_size, dont_free);
        if (buffer.ref == NULL)
        {
            dia_error_evnt(ROUTES_SEND_REMAINING_MSG_HEAP_EXHAUSTED, remaining_size);
            ERROR("send_remaining_file heap exhausted %d", remaining_size);
            http_response(p_sr->p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
            delete[] p_sr->content;
            return;
        }
        Espfile sel_file(p_sr->content);
        int res = sel_file.n_read(buffer.ref, p_sr->content_transferred, remaining_size);
        if (res < SPIFFS_OK)
        {
            delete[] p_sr->content;
            http_response(p_sr->p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Error reading file"), false);
            return;
        }
        TRACE("send_remaining_file: *p_espconn: %X, msg (splitted) len: %d",
              p_sr->p_espconn, remaining_size);
        http_send_buffer(p_sr->p_espconn, p_sr->order, buffer.ref, remaining_size);
        delete[] p_sr->content;
    }
}

void return_file(struct espconn *p_espconn, Http_parsed_req *parsed_req, char *filename)
{
    ALL("return_file");
    int file_size = Espfile::size(filename);
    // let's start with the header
    Http_header header;
    header.m_code = HTTP_OK;
    header.m_content_type = (char *)get_file_mime_type(filename);
    header.m_content_length = file_size;
    header.m_content_range_start = 0;
    header.m_content_range_end = 0;
    header.m_content_range_total = 0;
    if (parsed_req->origin)
    {
        header.m_origin = new char[(os_strlen(parsed_req->origin) + 1)];
        if (header.m_origin == NULL)
        {
            dia_error_evnt(ROUTES_RETURN_FILE_HEAP_EXHAUSTED, (os_strlen(parsed_req->origin) + 1));
            ERROR("return_file heap exhausted %d", (os_strlen(parsed_req->origin) + 1));
            http_response(p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
            return;
        }
        os_strcpy(header.m_origin, parsed_req->origin);
    }
    char *header_str = http_format_header(&header);
    if (header_str == NULL)
    {
        dia_error_evnt(ROUTES_RETURN_FILE_HEAP_EXHAUSTED, (os_strlen(header_str)));
        ERROR("return_file heap exhausted %d", (os_strlen(header_str)));
        http_response(p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
        return;
    }
    // ok send the header
    http_send_buffer(p_espconn, 0, header_str, os_strlen(header_str));
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
        Heap_chunk filename_copy(os_strlen(filename), dont_free);
        struct http_split_send *p_pending_response = new struct http_split_send;
        if ((buffer.ref == NULL) || (filename_copy.ref == NULL) || (p_pending_response == NULL))
        {
            int mem_size;
            if (buffer.ref == NULL)
            {
                mem_size = get_http_msg_max_size();
            }
            else if (filename_copy.ref == NULL)
            {
                delete[] buffer.ref;
                mem_size = os_strlen(filename);
            }
            else
            {
                delete[] buffer.ref;
                delete[] filename_copy.ref;
                mem_size = sizeof(struct http_split_send);
            }
            dia_error_evnt(ROUTES_RETURN_FILE_HEAP_EXHAUSTED, mem_size);
            ERROR("return_file heap exhausted %d", mem_size);
            http_response(p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
            return;
        }
        os_strncpy(filename_copy.ref, filename, os_strlen(filename));
        Espfile sel_file(filename);
        int res = sel_file.n_read(buffer.ref, buffer_size);
        if (res < SPIFFS_OK)
        {
            delete[] buffer.ref;
            delete[] filename_copy.ref;
            delete p_pending_response;
            http_response(p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Error reading file"), false);
            return;
        }
        // setup the remaining message
        p_pending_response->p_espconn = p_espconn;
        p_pending_response->order = 2;
        p_pending_response->content = filename_copy.ref;
        p_pending_response->content_size = file_size;
        p_pending_response->content_transferred = buffer_size;
        p_pending_response->action_function = send_remaining_file;
        Queue_err result = pending_split_send->push(p_pending_response);
        if (result == Queue_full)
        {
            delete[] buffer.ref;
            delete[] filename_copy.ref;
            delete p_pending_response;
            dia_error_evnt(ROUTES_RETURN_FILE_PENDING_RES_QUEUE_FULL);
            ERROR("return_file full pending response queue");
            return;
        }
        // send the file piece
        TRACE("return_file *p_espconn: %X, msg (splitted) len: %d", p_espconn, buffer_size);
        http_send_buffer(p_espconn, 1, buffer.ref, buffer_size);
        mem_mon_stack();
    }
    else
    {
        // no need to split the file over multiple messages
        Heap_chunk buffer(file_size, dont_free);
        if (buffer.ref == NULL)
        {
            dia_error_evnt(ROUTES_RETURN_FILE_HEAP_EXHAUSTED, file_size);
            ERROR("return_file heap exhausted %d", file_size);
            http_response(p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
            return;
        }
        Espfile sel_file(filename);
        int res = sel_file.n_read(buffer.ref, file_size);
        if (res < SPIFFS_OK)
        {
            http_response(p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Error reading file"), false);
            return;
        }
        TRACE("return_file *p_espconn: %X, msg (full) len: %d", p_espconn, file_size);
        http_send_buffer(p_espconn, 1, buffer.ref, file_size);
    }
}

void preflight_response(struct espconn *p_espconn, Http_parsed_req *parsed_req)
{
    ALL("preflight_response");
    Http_header header;
    header.m_code = HTTP_OK;
    header.m_content_type = (char *)get_file_mime_type(HTTP_CONTENT_JSON);
    if (parsed_req->origin)
    {
        header.m_origin = new char[os_strlen(parsed_req->origin) + 1];
        if (header.m_origin == NULL)
        {
            dia_error_evnt(ROUTES_PREFLIGHT_RESPONSE_HEAP_EXHAUSTED, (os_strlen(parsed_req->origin) + 1));
            http_response(p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
            return;
        }
        os_strcpy(header.m_origin, parsed_req->origin);
    }
    else
    {
        header.m_origin = NULL;
    }
    if (parsed_req->acrh)
    {
        header.m_acrh = new char[os_strlen(parsed_req->acrh) + 1];
        if (header.m_acrh == NULL)
        {
            dia_error_evnt(ROUTES_PREFLIGHT_RESPONSE_HEAP_EXHAUSTED, (os_strlen(parsed_req->acrh) + 1));
            http_response(p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
            return;
        }
        os_strcpy(header.m_acrh, parsed_req->acrh);
    }
    else
    {
        header.m_acrh = NULL;
    }
    header.m_content_length = 0;
    header.m_content_range_start = 0;
    header.m_content_range_end = 0;
    header.m_content_range_total = 0;
    char *header_str = http_format_header(&header);
    if (header_str == NULL)
    {
        dia_error_evnt(ROUTES_PREFLIGHT_RESPONSE_HEAP_EXHAUSTED);
        http_response(p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
        return;
    }
    // ok send the header
    http_send_buffer(p_espconn, 0, header_str, os_strlen(header_str));
}

static void getCron(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("getCron");
    char *msg = cron_cfg_json_stringify();
    if (msg)
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
    else
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
}

static void setCron(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("setCron");
    JSONP req_croncfg(parsed_req->req_content, parsed_req->content_len);
    int enabled = req_croncfg.getInt(f_str("cron_enabled"));
    if (req_croncfg.getErr() != JSON_noerr)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Json bad syntax"), false);
        return;
    }
    if (enabled)
        cron_enable();
    else
        cron_disable();
    cron_cfg_save();

    char *msg = cron_cfg_json_stringify();
    if (msg)
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
    else
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
    mem_mon_stack();
}

static void getLastReset(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("getLastReset");
    char *msg =  mem_last_reset_json_stringify();
    if (msg)
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
    else
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
    mem_mon_stack();
}

static void getMemDump(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("getMemDump");
    JSONP mem_param(parsed_req->req_content, parsed_req->content_len);
    char address_str[16];
    mem_param.getStr(f_str("address"), address_str, 16);
    int length = mem_param.getInt(f_str("length"));
    if (mem_param.getErr() != JSON_noerr)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Json bad syntax"), false);
        return;
    }
    char *msg = mem_dump_json_stringify(address_str, length);
    if (msg)
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
    else
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
    mem_mon_stack();
}

static void getHexMemDump(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("getHexMemDump");
    JSONP mem_param(parsed_req->req_content, parsed_req->content_len);
    char address_str[16];
    mem_param.getStr(f_str("address"), address_str, 16);
    int length = mem_param.getInt(f_str("length"));
    if (mem_param.getErr() != JSON_noerr)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Json bad syntax"), false);
        return;
    }
    char *msg = mem_dump_hex_json_stringify(address_str, length);
    if (msg)
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
    else
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
    mem_mon_stack();
}

static void getMemInfo(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("getMemInfo");
    char *msg = mem_mon_json_stringify();
    if (msg)
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
    else
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
}

static void ackDiagnosticEvents(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("ackDiagnosticEvents");
    dia_ack_events();
    http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, f_str("{\"msg\":\"Events acknoledged\"}"), false);
}

static void getDiagnosticCfg(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("getDiagnosticCfg");
    char *msg = dia_cfg_json_stringify();
    if (msg)
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
    else
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
}

static void setDiagnosticCfg(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("setDiagnosticCfg");
    JSONP diag_cfg(parsed_req->req_content, parsed_req->content_len);
    int diag_led_mask = diag_cfg.getInt(f_str("diag_led_mask"));
    int serial_log_mask = diag_cfg.getInt(f_str("serial_log_mask"));
    int sdk_print_enabled = diag_cfg.getInt(f_str("sdk_print_enabled"));
    int uart_0_bitrate = diag_cfg.getInt(f_str("uart_0_bitrate"));
    if (diag_cfg.getErr() != JSON_noerr)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Json bad syntax"), false);
        return;
    }
    dia_set_led_mask((char)diag_led_mask);
    dia_set_serial_log_mask((char)serial_log_mask);
    dia_set_sdk_print_enabled((bool)sdk_print_enabled);
    dia_set_uart_0_bitrate((uint32)uart_0_bitrate);
    dia_cfg_save();

    char *msg = dia_cfg_json_stringify();
    if (msg)
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
    else
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
    mem_mon_stack();
}

static void getDiagnosticEvents_next(struct http_split_send *p_sr)
{
    ALL("getdiagnosticevents_next");
    if (!http_espconn_in_use(p_sr->p_espconn))
    {
        TRACE("getdiagnosticevents_next espconn %X state %d, abort", p_sr->p_espconn, p_sr->p_espconn->state);
        // there will be no send, so trigger a check of pending send
        system_os_post(USER_TASK_PRIO_0, SIG_http_checkPendingResponse, '0');
        return;
    }
    int remaining_size = (p_sr->content_size - p_sr->content_transferred) * (42 + 12 + 1 + 2 + 4 + 12) + 2;
    if (remaining_size > get_http_msg_max_size())
    {
        // the remaining content size is bigger than response_max_size
        // will split the remaining content over multiple messages
        int buffer_size = get_http_msg_max_size();
        Heap_chunk buffer(buffer_size, dont_free);
        if (buffer.ref == NULL)
        {
            dia_error_evnt(ROUTES_GETDIAGNOSTICEVENTS_NEXT_HEAP_EXHAUSTED, buffer_size);
            ERROR("getdiagnosticevents_next heap exhausted %d", buffer_size);
            http_response(p_sr->p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
            return;
        }
        struct http_split_send *p_pending_response = new struct http_split_send;
        if (p_pending_response == NULL)
        {
            dia_error_evnt(ROUTES_GETDIAGNOSTICEVENTS_NEXT_HEAP_EXHAUSTED, sizeof(struct http_split_send));
            ERROR("getdiagnosticevents_next not heap exhausted %dn", sizeof(struct http_split_send));
            http_response(p_sr->p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
            delete[] buffer.ref;
            return;
        }
        struct dia_event *event_ptr;
        int ev_count = (buffer_size - 18) / (42 + 12 + 1 + 2 + 4 + 12) + p_sr->content_transferred;
        int idx;
        for (idx = p_sr->content_transferred; idx < ev_count; idx++)
        {
            fs_sprintf(buffer.ref + os_strlen(buffer.ref), ",");
            event_ptr = dia_get_event(idx);
            if (event_ptr)
                fs_sprintf(buffer.ref + os_strlen(buffer.ref), "{\"ts\":%d,\"ack\":%d,\"type\":\"%X\",\"code\":\"%X\",\"val\":%d}",
                           event_ptr->timestamp,
                           event_ptr->ack,
                           event_ptr->type,
                           event_ptr->code,
                           event_ptr->value);
        }
        // setup the remaining message
        p_pending_response->p_espconn = p_sr->p_espconn;
        p_pending_response->order = p_sr->order + 1;
        p_pending_response->content = p_sr->content;
        p_pending_response->content_size = p_sr->content_size;
        p_pending_response->content_transferred = ev_count;
        p_pending_response->action_function = getDiagnosticEvents_next;
        Queue_err result = pending_split_send->push(p_pending_response);
        if (result == Queue_full)
        {
            delete[] buffer.ref;
            delete p_pending_response;
            dia_error_evnt(ROUTES_GETDIAGNOSTICEVENTS_NEXT_PENDING_RES_QUEUE_FULL);
            ERROR("getdiagnosticevents_next full pending res queue");
            return;
        }
        TRACE("getdiagnosticevents_next: *p_espconn: %X, msg (splitted) len: %d",
              p_sr->p_espconn, buffer_size);
        http_send_buffer(p_sr->p_espconn, p_sr->order, buffer.ref, os_strlen(buffer.ref));
    }
    else
    {
        // this is the last piece of the message
        Heap_chunk buffer(remaining_size, dont_free);
        if (buffer.ref == NULL)
        {
            dia_error_evnt(ROUTES_GETDIAGNOSTICEVENTS_NEXT_HEAP_EXHAUSTED, remaining_size);
            ERROR("getdiagnosticevents_next heap exhausted %d", remaining_size);
            http_response(p_sr->p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
            return;
        }
        struct dia_event *event_ptr;
        int idx;
        for (idx = p_sr->content_transferred; idx < p_sr->content_size; idx++)
        {
            fs_sprintf(buffer.ref + os_strlen(buffer.ref), ",");
            event_ptr = dia_get_event(idx);
            if (event_ptr)
                fs_sprintf(buffer.ref + os_strlen(buffer.ref), "{\"ts\":%d,\"ack\":%d,\"type\":\"%X\",\"code\":\"%X\",\"val\":%d}",
                           event_ptr->timestamp,
                           event_ptr->ack,
                           event_ptr->type,
                           event_ptr->code,
                           event_ptr->value);
        }
        fs_sprintf(buffer.ref + os_strlen(buffer.ref), "]}");
        TRACE("getdiagnosticevents_next: *p_espconn: %X, msg (splitted) len: %d",
              p_sr->p_espconn, remaining_size);
        http_send_buffer(p_sr->p_espconn, p_sr->order, buffer.ref, os_strlen(buffer.ref));
    }
}

static void getDiagnosticEvents(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("getDiagnosticEvents");
    // count the diagnostic events and calculate the message content length
    // {"diag_events":[
    // {"ts":,"ack":,"type":"","code":"","val":},
    // ]}
    int evnt_count = 0;
    int content_len = 16 + 2 - 1;
    {
        char tmp_msg[42 + 12 + 1 + 2 + 4 + 12 + 1];
        struct dia_event *event_ptr = dia_get_event(evnt_count);
        while (event_ptr)
        {
            evnt_count++;
            fs_sprintf(tmp_msg, "{\"ts\":%d,\"ack\":%d,\"type\":\"%X\",\"code\":\"%X\",\"val\":%d},",
                       event_ptr->timestamp,
                       event_ptr->ack,
                       event_ptr->type,
                       event_ptr->code,
                       event_ptr->value);
            content_len += os_strlen(tmp_msg);
            if (evnt_count >= dia_get_max_events_count())
                break;
            event_ptr = dia_get_event(evnt_count);
        }
    }
    // let's start with the header
    Http_header header;
    header.m_code = HTTP_OK;
    header.m_content_type = HTTP_CONTENT_JSON;
    header.m_content_length = content_len;
    header.m_content_range_start = 0;
    header.m_content_range_end = 0;
    header.m_content_range_total = 0;
    bool heap_exhausted = false;
    if (parsed_req->origin)
    {
        header.m_origin = new char[(os_strlen(parsed_req->origin) + 1)];
        if (header.m_origin == NULL)
        {
            dia_error_evnt(ROUTES_GETDIAGNOSTICEVENTS_HEAP_EXHAUSTED, (os_strlen(parsed_req->origin) + 1));
            ERROR("getDiagnosticEvents heap exhausted %d", (os_strlen(parsed_req->origin) + 1));
            heap_exhausted = true;
        }
        else
            os_strcpy(header.m_origin, parsed_req->origin);
    }
    char *header_str = http_format_header(&header);
    if (header_str == NULL)
    {
        dia_error_evnt(ROUTES_GETDIAGNOSTICEVENTS_HEAP_EXHAUSTED, (os_strlen(header_str)));
        ERROR("getDiagnosticEvents heap exhausted %d", (os_strlen(header_str)));
        heap_exhausted = true;
    }
    if (heap_exhausted)
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
    else
        // ok send the header
        http_send_buffer(ptr_espconn, 0, header_str, os_strlen(header_str));

    // and now the content
    if (content_len > get_http_msg_max_size())
    {
        // will split the content over multiple messages
        // each the size of http_msg_max_size
        int buffer_size = get_http_msg_max_size();
        Heap_chunk buffer(buffer_size, dont_free);
        if (buffer.ref == NULL)
        {
            dia_error_evnt(ROUTES_GETDIAGNOSTICEVENTS_HEAP_EXHAUSTED, buffer_size);
            ERROR("getDiagnosticEvents heap exhausted %d", buffer_size);
            return;
        }
        struct http_split_send *p_pending_response = new struct http_split_send;
        if (p_pending_response == NULL)
        {
            dia_error_evnt(ROUTES_GETDIAGNOSTICEVENTS_HEAP_EXHAUSTED, sizeof(struct http_split_send));
            ERROR("getDiagnosticEvents heap exhausted %d", sizeof(struct http_split_send));
            delete[] buffer.ref;
            return;
        }
        fs_sprintf(buffer.ref, "{\"diag_events\":[");
        bool first_time = true;
        struct dia_event *event_ptr;
        int ev_count = (buffer_size - 18) / (42 + 12 + 1 + 2 + 4 + 12);
        int idx;
        for (idx = 0; idx < ev_count; idx++)
        {
            if (first_time)
                first_time = false;
            else
                fs_sprintf(buffer.ref + os_strlen(buffer.ref), ",");
            event_ptr = dia_get_event(idx);
            if (event_ptr)
                fs_sprintf(buffer.ref + os_strlen(buffer.ref), "{\"ts\":%d,\"ack\":%d,\"type\":\"%X\",\"code\":\"%X\",\"val\":%d}",
                           event_ptr->timestamp,
                           event_ptr->ack,
                           event_ptr->type,
                           event_ptr->code,
                           event_ptr->value);
        }
        // setup the next message
        p_pending_response->p_espconn = ptr_espconn;
        p_pending_response->order = 2;
        p_pending_response->content = "";
        p_pending_response->content_size = evnt_count;
        p_pending_response->content_transferred = ev_count;
        p_pending_response->action_function = getDiagnosticEvents_next;
        Queue_err result = pending_split_send->push(p_pending_response);
        if (result == Queue_full)
        {
            delete[] buffer.ref;
            delete p_pending_response;
            dia_error_evnt(ROUTES_GETDIAGEVENTS_PENDING_RES_QUEUE_FULL);
            ERROR("getDiagnosticEvents full pending response queue");
            return;
        }
        // send the content fragment
        TRACE("getDiagnosticEvents *p_espconn: %X, msg (splitted) len: %d", ptr_espconn, buffer_size);
        http_send_buffer(ptr_espconn, 1, buffer.ref, os_strlen(buffer.ref));
        mem_mon_stack();
    }
    else
    {
        // no need to split the content over multiple messages
        Heap_chunk buffer(content_len, dont_free);
        if (buffer.ref == NULL)
        {
            dia_error_evnt(ROUTES_GETDIAGNOSTICEVENTS_HEAP_EXHAUSTED, content_len);
            ERROR("getDiagnosticEvents heap exhausted %d", content_len);
            http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
            return;
        }
        fs_sprintf(buffer.ref, "{\"diag_events\":[");
        bool first_time = true;
        struct dia_event *event_ptr;
        int idx;
        for (idx = 0; idx < evnt_count; idx++)
        {
            if (first_time)
                first_time = false;
            else
                fs_sprintf(buffer.ref + os_strlen(buffer.ref), ",");
            event_ptr = dia_get_event(idx);
            if (event_ptr)
                fs_sprintf(buffer.ref + os_strlen(buffer.ref), "{\"ts\":%d,\"ack\":%d,\"type\":\"%X\",\"code\":\"%X\",\"val\":%d}",
                           event_ptr->timestamp,
                           event_ptr->ack,
                           event_ptr->type,
                           event_ptr->code,
                           event_ptr->value);
        }
        fs_sprintf(buffer.ref + os_strlen(buffer.ref), "]}");
        TRACE("getDiagnosticEvents *p_espconn: %X, msg (full) len: %d", ptr_espconn, content_len);
        http_send_buffer(ptr_espconn, 1, buffer.ref, os_strlen(buffer.ref));
    }
}

static void getDeviceName(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("getDeviceName");
    char *msg = espbot_cfg_json_stringify();
    if (msg)
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
    else
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
}

static void setDeviceName(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("setDeviceName");
    JSONP device_cfg(parsed_req->req_content, parsed_req->content_len);
    char device_name[32];
    device_cfg.getStr(f_str("device_name"), device_name, 32);
    if (device_cfg.getErr() != JSON_noerr)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Json bad syntax"), false);
        return;
    }
    espbot_set_name(device_name);
    espbot_cfg_save();
    char *msg = espbot_cfg_json_stringify();
    if (msg)
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
    else
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
}

static void getFs(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("getFs");
    int total_size = esp_spiffs_total_size();
    int used_size = esp_spiffs_used_size();

    Heap_chunk msg(128, dont_free);
    if (msg.ref == NULL)
    {
        dia_error_evnt(ROUTES_GETFS_HEAP_EXHAUSTED, 128);
        ERROR("getFs heap exhausted %d", 128);
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
        return;
    }
    fs_sprintf(msg.ref,
               "{\"file_system_size\": %d,"
               "\"file_system_used_size\": %d}",
               total_size,
               used_size);
    http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
}

static void checkFS(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("checkFS");
    http_response(ptr_espconn, HTTP_ACCEPTED, HTTP_CONTENT_JSON, f_str("{\"msg\":\"FS check started...\"}"), false);

    // give some time to the http reply before starting the FS check
    os_timer_disarm(&delay_timer);
    os_timer_setfn(&delay_timer, (os_timer_func_t *)esp_spiffs_check, NULL);
    os_timer_arm(&delay_timer, 1000, 0);
}

static void getFileList(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("getFileList");
    int file_cnt = 0;
    struct spiffs_dirent *file_ptr = esp_spiffs_list(0);
    // count files first
    while (file_ptr)
    {
        file_cnt++;
        file_ptr = esp_spiffs_list(1);
    }
    // now prepare the list
    // {
    //    "files":[
    //      {"name":"file_1","size":1024},
    //      {"name":"file_2","size":2048}
    //    ]
    // }
    // {"files":[]}
    // {"name":"","size":},
    int file_list_len = 12 + (file_cnt * (20 + 31 + 7)) + 1;
    Heap_chunk file_list(file_list_len, dont_free);
    if (file_list.ref == NULL)
    {
        dia_error_evnt(ROUTES_GETFILELIST_HEAP_EXHAUSTED, file_list_len);
        ERROR("getFileList heap exhausted %d", file_list_len);
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
        return;
    }
    char *tmp_ptr;
    fs_sprintf(file_list.ref, "{\"files\":[");
    file_ptr = esp_spiffs_list(0);
    while (file_ptr)
    {
        tmp_ptr = file_list.ref + os_strlen(file_list.ref);
        if (tmp_ptr != (file_list.ref + os_strlen(f_str("{\"files\":["))))
            *(tmp_ptr++) = ',';
        fs_sprintf(tmp_ptr, "{\"name\":\"%s\",\"size\":%d}", (char *)file_ptr->name, file_ptr->size);
        file_ptr = esp_spiffs_list(1);
    }
    tmp_ptr = file_list.ref + os_strlen(file_list.ref);
    fs_sprintf(tmp_ptr, "]}");
    http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, file_list.ref, true);
    mem_mon_stack();
}

static void getFile(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("getFile");
    char *file_name = parsed_req->url + os_strlen(f_str("/api/file/"));
    if (os_strlen(file_name) == 0)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("No file name provided"), false);
        return;
    }
    if (!Espfile::exists(file_name))
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("File not found"), false);
        return;
    }
    return_file(ptr_espconn, parsed_req, file_name);
}

static void deleteFile(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("deleteFile");
    char *file_name = parsed_req->url + os_strlen(f_str("/api/file/"));
    if (os_strlen(file_name) == 0)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("No file name provided"), false);
        return;
    }
    if (!Espfile::exists(file_name))
    {
        http_response(ptr_espconn, HTTP_NOT_FOUND, HTTP_CONTENT_JSON, f_str("File not found"), false);
        return;
    }
    Espfile sel_file(file_name);
    int res = sel_file.remove();
    mem_mon_stack();
    // check if the file was deleted
    if (res != SPIFFS_OK)
    {
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Error deleting file"), false);
    }
    else
    {
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, f_str("{\"msg\":\"File deleted\"}"), false);
    }
}

static void createFile(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("createFile");
    char *file_name = parsed_req->url + os_strlen(f_str("/api/file/"));
    if (os_strlen(file_name) == 0)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("No file name provided"), false);
        return;
    }
    if (Espfile::exists(file_name))
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("File already exists"), false);
        return;
    }
    Espfile sel_file(file_name);
    int res = sel_file.n_append(parsed_req->req_content, parsed_req->content_len);
    mem_mon_stack();
    if (res < SPIFFS_OK)
    {
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Error creating file"), false);
    }
    else
    {
        http_response(ptr_espconn, HTTP_CREATED, HTTP_CONTENT_JSON, f_str("{\"msg\":\"File created\"}"), false);
    }
}

static void appendToFile(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("appendToFile");
    char *file_name = parsed_req->url + os_strlen(f_str("/api/file/"));
    if (os_strlen(file_name) == 0)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("No file name provided"), false);
        return;
    }
    if (!Espfile::exists(file_name))
    {
        http_response(ptr_espconn, HTTP_NOT_FOUND, HTTP_CONTENT_JSON, f_str("File not found"), false);
        return;
    }
    Espfile sel_file(file_name);
    mem_mon_stack();
    int res = sel_file.n_append(parsed_req->req_content, parsed_req->content_len);
    if (res < SPIFFS_OK)
    {
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Error modifying file"), false);
    }
    else
    {
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, f_str("{\"msg\":\"File modified\"}"), false);
    }
}

static void getGpioCfg(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("getGpioCfg");
    int gpio_id;
    char *gpio_name = parsed_req->url + os_strlen(f_str("/api/gpio/cfg/"));
    if (os_strlen(gpio_name) == 0)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("No GPIO ID provided"), false);
        return;
    }
    gpio_id = atoi(gpio_name);
    if (!gpio_valid_id(gpio_id))
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Wrong GPIO ID"), false);
        return;
    }
    char *msg = gpio_cfg_json_stringify(gpio_id);
    if (msg)
    {
        if (os_strlen(msg) == 0)
            http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("gpio_get_config error"), false);
        else
            http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
    }
    else
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
    mem_mon_stack();
}

static void setGpioCfg(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("setGpioCfg");
    int gpio_id;
    char *gpio_name = parsed_req->url + os_strlen(f_str("/api/gpio/cfg/"));
    if (os_strlen(gpio_name) == 0)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("No GPIO ID provided"), false);
        return;
    }
    gpio_id = atoi(gpio_name);
    if (!gpio_valid_id(gpio_id))
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Wrong GPIO ID"), false);
        return;
    }
    JSONP req_gpio(parsed_req->req_content, parsed_req->content_len);
    char gpio_type_str[16];
    req_gpio.getStr(f_str("gpio_type"), gpio_type_str, 16);
    if (req_gpio.getErr() != JSON_noerr)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Json bad syntax"), false);
        return;
    }
    if ((os_strcmp(gpio_type_str, f_str("UNPROVISIONED")) == 0) ||
        (os_strcmp(gpio_type_str, f_str("unprovisioned")) == 0))
        gpio_unconfig(gpio_id);
    else if ((os_strcmp(gpio_type_str, f_str("INPUT")) == 0) ||
             (os_strcmp(gpio_type_str, f_str("input")) == 0))
        gpio_config(gpio_id, ESPBOT_GPIO_INPUT);
    else if ((os_strcmp(gpio_type_str, f_str("OUTPUT")) == 0) ||
             (os_strcmp(gpio_type_str, f_str("output")) == 0))
        gpio_config(gpio_id, ESPBOT_GPIO_OUTPUT);
    else
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Wrong GPIO type"), false);
        return;
    }

    int result = gpio_get_config(gpio_id);
    char *msg = gpio_cfg_json_stringify(gpio_id);
    if (msg)
    {
        if (os_strlen(msg) == 0)
            http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("gpio_set_config error"), false);
        else
            http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
    }
    else
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
    mem_mon_stack();
}

static void getGpioLevel(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("getGpioLevel");
    int gpio_id;
    char *gpio_name = parsed_req->url + os_strlen(f_str("/api/gpio/"));
    if (os_strlen(gpio_name) == 0)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("No GPIO ID provided"), false);
        return;
    }
    gpio_id = atoi(gpio_name);
    if (!gpio_valid_id(gpio_id))
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Wrong GPIO ID"), false);
        return;
    }
    char *msg = gpio_state_json_stringify(gpio_id);
    if (msg)
    {
        if (os_strlen(msg) == 0)
            http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("gpio_read error"), false);
        else
            http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
    }
    else
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
    mem_mon_stack();
}

static void setGpioLevel(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("setGpioLevel");
    int gpio_id;
    char *gpio_name = parsed_req->url + os_strlen(f_str("/api/gpio/"));
    if (os_strlen(gpio_name) == 0)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("No GPIO ID provided"), false);
        return;
    }
    gpio_id = atoi(gpio_name);
    if (!gpio_valid_id(gpio_id))
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Wrong GPIO ID"), false);
        return;
    }
    int output_level;
    JSONP req_gpio(parsed_req->req_content, parsed_req->content_len);
    char output_level_str[16];
    req_gpio.getStr(f_str("gpio_level"), output_level_str, 16);
    if (req_gpio.getErr() != JSON_noerr)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Json bad syntax"), false);
        return;
    }
    if ((os_strcmp(output_level_str, f_str("LOW")) == 0) ||
        (os_strcmp(output_level_str, f_str("low")) == 0))
        output_level = ESPBOT_LOW;
    else if ((os_strcmp(output_level_str, f_str("HIGH")) == 0) ||
             (os_strcmp(output_level_str, f_str("high")) == 0))
        output_level = ESPBOT_HIGH;
    else
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Wrong GPIO level"), false);
        return;
    }
    int result = gpio_set(gpio_id, output_level);

    switch (result)
    {
    case ESPBOT_GPIO_WRONG_IDX:
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Wrong GPIO ID"), false);
        break;
    case ESPBOT_GPIO_UNPROVISIONED:
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("GPIO is unprovisioned"), false);
        break;
    case ESPBOT_GPIO_WRONG_LVL:
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Wrong output level"), false);
        break;
    case ESPBOT_GPIO_CANNOT_CHANGE_INPUT:
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Cannot change input"), false);
        break;
    case ESPBOT_GPIO_OK:
    {
        char *msg = gpio_state_json_stringify(gpio_id);
        if (msg)
        {
            if (os_strlen(msg) == 0)
                http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("gpio_read error"), false);
            else
                http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
        }
        else
            http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
    }
    default:
        break;
    }
    mem_mon_stack();
}

static void getMdns(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("getMdns");
    char *msg = mdns_cfg_json_stringify();
    if (msg)
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
    else
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
}

static void setMdns(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("setMdns");
    JSONP mdns_cfg(parsed_req->req_content, parsed_req->content_len);
    int mdns_enabled = mdns_cfg.getInt(f_str("mdns_enabled"));
    if (mdns_cfg.getErr() != JSON_noerr)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Json bad syntax"), false);
        return;
    }
    if (mdns_enabled)
        mdns_enable();
    else
        mdns_disable();
    mdns_cfg_save();

    char *msg = mdns_cfg_json_stringify();
    if (msg)
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
    else
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
    mem_mon_stack();
}

static void getTimedateCfg(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("getTimedateCfg");
    char *msg = timedate_cfg_json_stringify();
    if (msg)
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
    else
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
}

static void setTimedateCfg(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("setTimedateCfg");
    JSONP req_timedate(parsed_req->req_content, parsed_req->content_len);
    int sntp_enabled = req_timedate.getInt(f_str("sntp_enabled"));
    int timezone = req_timedate.getInt(f_str("timezone"));
    if (req_timedate.getErr() != JSON_noerr)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Json bad syntax"), false);
        return;
    }

    if (sntp_enabled)
        timedate_enable_sntp();
    else
        timedate_disable_sntp();
    timedate_set_timezone(timezone);
    timedate_cfg_save();

    char *msg = timedate_cfg_json_stringify();
    if (msg)
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
    else
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
    mem_mon_stack();
}

static void getTimedate(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("getTimedate");
    char *msg = timedate_state_json_stringify();
    if (msg)
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
    else
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
}

static void setTimedate(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("setTimedate");
    JSONP req_timedate(parsed_req->req_content, parsed_req->content_len);
    int32 timestamp = (int32)req_timedate.getInt(f_str("timestamp"));
    if (req_timedate.getErr() != JSON_noerr)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Json bad syntax"), false);
        return;
    }

    timedate_set_time_manually((uint32)timestamp);

    char *msg = timedate_state_json_stringify();
    if (msg)
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
    else
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
    mem_mon_stack();
}

static void ota_answer_on_completion(void *param)
{
    struct espconn *ptr_espconn = (struct espconn *)param;
    switch (esp_ota.get_last_result())
    {
    case OTA_SUCCESS:
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, f_str("{\"msg\":\"OTA completed. Rebooting...\"}"), false);
        break;
    case OTA_FAILED:
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("OTA failed"), false);
        break;
    case OTA_ALREADY_TO_THE_LATEST:
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, f_str("{\"msg\":\"Binary version already to the latest\"}"), false);
        break;
    default:
        break;
    }
}

static void startOTA(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("startOTA");
    esp_ota.set_cb_on_completion(ota_answer_on_completion);
    esp_ota.set_cb_param((void *)ptr_espconn);
    esp_ota.start_upgrade();
}

static void getOtaCfg(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("getOtaCfg");
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
                   "\"check_version\":%d,\"reboot_on_completion\":%d}",
                   esp_ota.get_check_version(),
                   esp_ota.get_reboot_on_completion());
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
    }
    else
    {
        dia_error_evnt(ROUTES_GETOTACFG_HEAP_EXHAUSTED, msg_len);
        ERROR("getOtaCfg heap exhausted %d", msg_len);
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
    }
}

static void setOtaCfg(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("setOtaCfg");
    JSONP req_otacfg(parsed_req->req_content, parsed_req->content_len);
    char host[16];
    req_otacfg.getStr(f_str("host"), host, 16);
    int port = req_otacfg.getInt(f_str("port"));
    char path[128];
    req_otacfg.getStr(f_str("path"), path, 128);
    bool check_version = (bool)req_otacfg.getInt(f_str("check_version"));
    bool reboot_on_completion = req_otacfg.getInt(f_str("reboot_on_completion"));
    if (req_otacfg.getErr() != JSON_noerr)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Json bad syntax"), false);
        return;
    }

    esp_ota.set_host(host);
    esp_ota.set_port(port);
    esp_ota.set_path(path);
    esp_ota.set_check_version(check_version);
    esp_ota.set_reboot_on_completion(reboot_on_completion);
    esp_ota.save_cfg();

    // {"host":"","port":,"path":"","check_version":,"reboot_on_completion":}

    int msg_len = 70 + 15 + 5 + os_strlen(esp_ota.get_path()) + 1 + 1 + 1;
    Heap_chunk msg(msg_len, dont_free);
    if (msg.ref == NULL)
    {
        dia_error_evnt(ROUTES_SETOTACFG_HEAP_EXHAUSTED, msg_len);
        ERROR("setOtaCfg heap exhausted %d", msg_len);
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
        return;
    }
    fs_sprintf(msg.ref,
               "{\"host\":\"%s\",\"port\":%d,\"path\":\"%s\",",
               esp_ota.get_host(),
               esp_ota.get_port(),
               esp_ota.get_path());
    fs_sprintf((msg.ref + os_strlen(msg.ref)),
               "\"check_version\":%d,\"reboot_on_completion\":%d}",
               esp_ota.get_check_version(),
               esp_ota.get_reboot_on_completion());
    http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
    mem_mon_stack();
}

static void getWifi(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("getWifi");
    char *msg = espwifi_status_json_stringify();
    if (msg)
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
    else
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
}

static void getWifiApCfg(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("getWifiApCfg");
    char *msg = espwifi_cfg_json_stringify();
    if (msg)
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
    else
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
}

static void setWifiApCfg(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("setWifiApCfg");
    JSONP req_apcfg(parsed_req->req_content, parsed_req->content_len);
    int ap_channel = req_apcfg.getInt(f_str("ap_channel"));
    char ap_pwd[64];
    req_apcfg.getStr(f_str("ap_pwd"), ap_pwd, 64);
    if (req_apcfg.getErr() != JSON_noerr)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Json bad syntax"), false);
        return;
    }

    espwifi_ap_set_ch(ap_channel);
    espwifi_ap_set_pwd(ap_pwd, os_strlen(ap_pwd));
    espwifi_cfg_save();
    mem_mon_stack();

    char *msg = espwifi_cfg_json_stringify();
    if (msg)
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
    else
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
}

static void getWifiStationCfg(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("getWifiStationCfg");
    char *msg = espwifi_cfg_json_stringify();
    if (msg)
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
    else
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
}

static void setWifiStationCfg(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("setWifiStationCfg");
    JSONP req_stationcfg(parsed_req->req_content, parsed_req->content_len);
    char ssid[32];
    req_stationcfg.getStr(f_str("station_ssid"), ssid, 32);
    char pwd[64];
    req_stationcfg.getStr(f_str("station_pwd"), pwd, 64);
    if (req_stationcfg.getErr() != JSON_noerr)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Json bad syntax"), false);
        return;
    }

    espwifi_station_set_ssid(ssid, os_strlen(ssid));
    espwifi_station_set_pwd(pwd, os_strlen(pwd));

    // wait before connecting to the AP so that the client could get the response
    os_timer_disarm(&delay_timer);
    os_timer_setfn(&delay_timer, (os_timer_func_t *)espwifi_connect_to_ap, NULL);
    os_timer_arm(&delay_timer, 1000, 0);
    mem_mon_stack();

    char *msg = espwifi_cfg_json_stringify();
    if (msg)
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
    else
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
}

static void connectWifi(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("connectWifi");
    // {"msg":"Connecting to SSID ..."}
    int msg_len = 32 + 32 + 1;
    char *msg = new char[msg_len];
    if (msg)
    {
        fs_sprintf(msg, "{\"msg\":\"Connecting to SSID %s...\",", espwifi_station_get_ssid());
        http_response(ptr_espconn, HTTP_ACCEPTED, HTTP_CONTENT_JSON, msg, true);
        os_timer_disarm(&delay_timer);
        os_timer_setfn(&delay_timer, (os_timer_func_t *)espwifi_connect_to_ap, NULL);
        os_timer_arm(&delay_timer, 500, 0);
    }
    else
    {
        dia_error_evnt(ROUTES_CONNECTWIFI_HEAP_EXHAUSTED, msg_len);
        ERROR("connectWifi heap exhausted %d", msg_len);
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
    }
}

static void disconnectWifi(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("disconnectWifi");
    // {"msg":"Disconnecting from SSID ..."}
    int msg_len = 37 + 32 + 1;
    char *msg = new char[msg_len];
    if (msg)
    {
        fs_sprintf(msg, "{\"msg\":\"Disconnecting from SSID %s...\",", espwifi_station_get_ssid());
        http_response(ptr_espconn, HTTP_ACCEPTED, HTTP_CONTENT_JSON, msg, true);
        os_timer_disarm(&delay_timer);
        os_timer_setfn(&delay_timer, (os_timer_func_t *)espwifi_work_as_ap, NULL);
        os_timer_arm(&delay_timer, 500, 0);
    }
    else
    {
        dia_error_evnt(ROUTES_DISCONNECTWIFI_HEAP_EXHAUSTED, msg_len);
        ERROR("disconnectWifi heap exhausted %d", msg_len);
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
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
        return_file(ptr_espconn, parsed_req, (char *)f_str("index.html"));
        return;
    }
    if ((os_strncmp(parsed_req->url, f_str("/api/"), 5)) && (parsed_req->req_method == HTTP_GET))
    {
        // not an api: look for specified file
        char *file_name = parsed_req->url + os_strlen("/");
        return_file(ptr_espconn, parsed_req, file_name);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/cron"))) && (parsed_req->req_method == HTTP_GET))
    {
        getCron(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/cron"))) && (parsed_req->req_method == HTTP_POST))
    {
        setCron(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/debug/lastReset"))) && (parsed_req->req_method == HTTP_GET))
    {
        getLastReset(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/debug/hexMemDump"))) && (parsed_req->req_method == HTTP_POST))
    {
        getHexMemDump(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/debug/memDump"))) && (parsed_req->req_method == HTTP_POST))
    {
        getMemDump(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/debug/memInfo"))) && (parsed_req->req_method == HTTP_GET))
    {
        getMemInfo(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/deviceName"))) && (parsed_req->req_method == HTTP_GET))
    {
        getDeviceName(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/deviceName"))) && (parsed_req->req_method == HTTP_POST))
    {
        setDeviceName(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/diagnostic"))) && (parsed_req->req_method == HTTP_GET))
    {
        getDiagnosticEvents(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/diagnostic"))) && (parsed_req->req_method == HTTP_POST))
    {
        ackDiagnosticEvents(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/diagnostic/cfg"))) && (parsed_req->req_method == HTTP_GET))
    {
        getDiagnosticCfg(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/diagnostic/cfg"))) && (parsed_req->req_method == HTTP_POST))
    {
        setDiagnosticCfg(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/file"))) && (parsed_req->req_method == HTTP_GET))
    {
        getFileList(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strncmp(parsed_req->url, f_str("/api/file/"), os_strlen(f_str("/api/file/")))) && (parsed_req->req_method == HTTP_GET))
    {
        getFile(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strncmp(parsed_req->url, f_str("/api/file/"), os_strlen(f_str("/api/file/")))) && (parsed_req->req_method == HTTP_POST))
    {
        createFile(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strncmp(parsed_req->url, f_str("/api/file/"), os_strlen(f_str("/api/file/")))) && (parsed_req->req_method == HTTP_PUT))
    {
        appendToFile(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strncmp(parsed_req->url, f_str("/api/file/"), os_strlen(f_str("/api/file/")))) && (parsed_req->req_method == HTTP_DELETE))
    {
        deleteFile(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/fs"))) && (parsed_req->req_method == HTTP_GET))
    {
        getFs(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/fs/check"))) && (parsed_req->req_method == HTTP_POST))
    {
        checkFS(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strncmp(parsed_req->url, f_str("/api/gpio/cfg/"), os_strlen(f_str("/api/gpio/cfg/")))) && (parsed_req->req_method == HTTP_GET))
    {
        getGpioCfg(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strncmp(parsed_req->url, f_str("/api/gpio/cfg/"), os_strlen(f_str("/api/gpio/cfg/")))) && (parsed_req->req_method == HTTP_POST))
    {
        setGpioCfg(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strncmp(parsed_req->url, f_str("/api/gpio/"), os_strlen(f_str("/api/gpio/")))) && (parsed_req->req_method == HTTP_GET))
    {
        getGpioLevel(ptr_espconn, parsed_req);
        return;
    }
    if (0 == (os_strncmp(parsed_req->url, f_str("/api/gpio/"), os_strlen(f_str("/api/gpio/")))) && (parsed_req->req_method == HTTP_POST))
    {
        setGpioLevel(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/mdns"))) && (parsed_req->req_method == HTTP_GET))
    {
        getMdns(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/mdns"))) && (parsed_req->req_method == HTTP_POST))
    {
        setMdns(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/reboot"))) && (parsed_req->req_method == HTTP_POST))
    {
        http_response(ptr_espconn, HTTP_ACCEPTED, HTTP_CONTENT_JSON, f_str("{\"msg\":\"Device rebooting...\"}"), false);
        espbot_reset(ESPBOT_restart);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/timedate"))) && (parsed_req->req_method == HTTP_GET))
    {
        getTimedate(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/timedate"))) && (parsed_req->req_method == HTTP_POST))
    {
        setTimedate(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/timedate/cfg"))) && (parsed_req->req_method == HTTP_GET))
    {
        getTimedateCfg(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/timedate/cfg"))) && (parsed_req->req_method == HTTP_POST))
    {
        setTimedateCfg(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/ota"))) && (parsed_req->req_method == HTTP_POST))
    {
        startOTA(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/ota/cfg"))) && (parsed_req->req_method == HTTP_GET))
    {
        getOtaCfg(ptr_espconn, parsed_req);
        return;
    }
    if ((os_strcmp(parsed_req->url, f_str("/api/ota/cfg")) == 0) && (parsed_req->req_method == HTTP_POST))
    {
        setOtaCfg(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/ota/reboot"))) && (parsed_req->req_method == HTTP_POST))
    {
        http_response(ptr_espconn, HTTP_ACCEPTED, HTTP_CONTENT_JSON, f_str("{\"msg\":\"Rebooting after OTA...\"}"), false);
        espbot_reset(ESPBOT_rebootAfterOta);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/wifi"))) && (parsed_req->req_method == HTTP_GET))
    {
        getWifi(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/wifi/scan"))) && (parsed_req->req_method == HTTP_GET))
    {
        espwifi_scan_for_ap(NULL, getAPlist, (void *)ptr_espconn);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/wifi/ap/cfg"))) && (parsed_req->req_method == HTTP_GET))
    {
        getWifiApCfg(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/wifi/ap/cfg"))) && (parsed_req->req_method == HTTP_POST))
    {
        setWifiApCfg(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/wifi/station/cfg"))) && (parsed_req->req_method == HTTP_GET))
    {
        getWifiStationCfg(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/wifi/station/cfg"))) && (parsed_req->req_method == HTTP_POST))
    {
        setWifiStationCfg(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/wifi/connect"))) && (parsed_req->req_method == HTTP_POST))
    {
        connectWifi(ptr_espconn, parsed_req);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/wifi/disconnect"))) && (parsed_req->req_method == HTTP_POST))
    {
        disconnectWifi(ptr_espconn, parsed_req);
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
