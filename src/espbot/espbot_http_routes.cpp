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
#include "user_interface.h"
#include "mem.h"
#include "ip_addr.h"
}

#include "espbot_webserver.hpp"
#include "espbot_http_routes.hpp"
#include "app_http_routes.hpp"
#include "espbot.hpp"
#include "espbot_global.hpp"
#include "espbot_logger.hpp"
#include "espbot_json.hpp"
#include "espbot_utils.hpp"
#include "espbot_debug.hpp"

static os_timer_t format_delay_timer;
static os_timer_t wifi_scan_timeout_timer;

void ICACHE_FLASH_ATTR init_controllers(void)
{
    os_timer_disarm(&format_delay_timer);
    os_timer_disarm(&wifi_scan_timeout_timer);
}

static void ICACHE_FLASH_ATTR format_function(void)
{
    esplog.all("webserver::format_function\n");
    espfs.format();
}

static void ICACHE_FLASH_ATTR wifi_scan_timeout_function(struct espconn *ptr_espconn)
{
    esplog.all("webserver::wifi_scan_timeout_function\n");
    static int counter = 0;
    if (counter < 10) // wait for a scan completion at maximum for 5 seconds
    {
        if (espwifi.scan_for_ap_completed())
        {
            char *scan_list = (char *)esp_zalloc(40 + ((32 + 6) * espwifi.get_ap_count()));
            if (scan_list)
            {
                char *tmp_ptr;
                espmem.stack_mon();
                os_sprintf(scan_list, "{\"AP_count\": %d,\"AP_SSIDs\":[", espwifi.get_ap_count());
                for (int idx = 0; idx < espwifi.get_ap_count(); idx++)
                {
                    tmp_ptr = scan_list + os_strlen(scan_list);
                    if (idx > 0)
                        *(tmp_ptr++) = ',';
                    os_sprintf(tmp_ptr, "\"%s\"", espwifi.get_ap_name(idx));
                }
                tmp_ptr = scan_list + os_strlen(scan_list);
                os_sprintf(tmp_ptr, "]}");
                response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, scan_list, true);
                counter = 0;
                espwifi.free_ap_list();
            }
            else
            {
                esplog.error("Websvr::wifi_scan_timeout_function - not enough heap memory %d\n", 32 + ((32 + 3) * espwifi.get_ap_count()));
                // may be the list was too big but there is enough heap memory for a response
                response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, "Not enough heap memory", false);
                counter = 0;
                espwifi.free_ap_list();
            }
        }
        else
        {
            // not ready yet, wait for another 500 ms
            os_timer_disarm(&wifi_scan_timeout_timer);
            os_timer_setfn(&wifi_scan_timeout_timer, (os_timer_func_t *)wifi_scan_timeout_function, ptr_espconn);
            os_timer_arm(&wifi_scan_timeout_timer, 500, 0);
            counter++;
        }
    }
    else
    {
        counter = 0;
        response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, "Timeout on Wifi::scan_for_ap", false);
    }
}

static char ICACHE_FLASH_ATTR *get_file_mime_type(char *filename)
{
    char *ptr;
    ptr = (char *)os_strstr(filename, ".");
    if (ptr == NULL)
        return "text/plain";
    else
    {
        if (os_strcmp(ptr, ".css") == 0)
            return "text/css";
        else if (os_strcmp(ptr, ".txt") == 0)
            return "text/plain";
        else if (os_strcmp(ptr, ".html") == 0)
            return "text/html";
        else if (os_strcmp(ptr, ".js") == 0)
            return "text/javascript";
        else if (os_strcmp(ptr, ".css") == 0)
            return "text/css";
        else
            return "text/plain";
    }
}

struct ret_file_response
{
    struct espconn *p_espconn;
    char *filename;
    int file_size;
    int bytes_transferred;
    int timer_idx;
};

static void ICACHE_FLASH_ATTR free_ret_file_response(struct ret_file_response *ptr)
{
    esp_free(ptr->filename);
    esp_free(ptr);
}

#define FILE_SENT_TIMER_PERIOD 100

static void ICACHE_FLASH_ATTR send_remaining_file(void *param)
{
    Profiler ret_file("send_remaining_file");
    esplog.all("webserver::send_remaining_file\n");
    struct ret_file_response *p_res = (struct ret_file_response *)param;
    free_split_msg_timer(p_res->timer_idx);
    esplog.trace("send_remaining_file: *p_espconn: %X\n"
                 "                       filename: %s\n"
                 "                      file_size: %d\n"
                 "               byte_transferred: %d\n"
                 "                      timer_idx: %d\n",
                 p_res->p_espconn, p_res->filename, p_res->file_size, p_res->bytes_transferred, p_res->timer_idx);

    if (espwebsvr.get_status() == down)
    {
        free_ret_file_response(p_res);
        return;
    }

    if (espfs.is_available())
    {
        if (!Ffile::exists(&espfs, p_res->filename))
        {
            response(p_res->p_espconn, HTTP_NOT_FOUND, HTTP_CONTENT_JSON, "File not found", false);
            return;
        }
        Ffile sel_file(&espfs, p_res->filename);
        if (sel_file.is_available())
        {
            // no header this time
            // transfer the file splitted accordingly to the webserver response buffer size
            int buffer_size;
            bool need_to_split_file;
            if ((p_res->file_size - p_res->bytes_transferred) > espwebsvr.get_response_max_size())
            {
                buffer_size = espwebsvr.get_response_max_size();
                need_to_split_file = true;
            }
            else
            {
                buffer_size = p_res->file_size - p_res->bytes_transferred;
                need_to_split_file = false;
            }
            Heap_chunk msg(buffer_size + 1, dont_free);
            if (msg.ref)
            {
                sel_file.n_read(msg.ref, p_res->bytes_transferred, buffer_size);
                // esplog.trace("send_remaining_file: msg: %s\n", msg.ref);
                // send response
                send_response_buffer(p_res->p_espconn, msg.ref);
                // was the content bigger than the buffer ?
                if (need_to_split_file)
                {
                    int timer_idx = get_free_split_msg_timer();
                    // check if there's a timer available
                    if (timer_idx < 0)
                    {
                        esplog.error("Websvr::send_remaining_file: no split_response_timer available\n");
                        response(p_res->p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, "No timer available", false);
                        free_ret_file_response(p_res);
                        return;
                    }
                    p_res->bytes_transferred = p_res->bytes_transferred + buffer_size;
                    p_res->timer_idx = timer_idx;
                    os_timer_t *split_timer = get_split_msg_timer(timer_idx);
                    os_timer_disarm(split_timer);
                    os_timer_setfn(split_timer, (os_timer_func_t *)send_remaining_file, (void *)p_res);
                    os_timer_arm(split_timer, FILE_SENT_TIMER_PERIOD, 0);
                }
                else
                {
                    free_ret_file_response(p_res);
                }
            }
            else
            {
                esplog.error("Websvr::send_remaining_file - not enough heap memory %d\n", buffer_size + 1);
                // may be the file was too big but there is enough heap memory for a response
                response(p_res->p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, "Not enough heap memory", false);
                free_ret_file_response(p_res);
            }
            return;
        }
        else
        {
            response(p_res->p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, "Cannot open file", false);
            free_ret_file_response(p_res);
            return;
        }
    }
    else
    {
        response(p_res->p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, "File system is not available", false);
        free_ret_file_response(p_res);
        return;
    }
}

static void ICACHE_FLASH_ATTR return_file(struct espconn *p_espconn, char *filename)
{
    Profiler ret_file("return_file");
    esplog.all("webserver::return_file\n");
    if (espfs.is_available())
    {
        if (!Ffile::exists(&espfs, filename))
        {
            response(p_espconn, HTTP_NOT_FOUND, HTTP_CONTENT_JSON, "File not found", false);
            return;
        }
        int file_size = Ffile::size(&espfs, filename);
        Ffile sel_file(&espfs, filename);
        espmem.stack_mon();
        if (sel_file.is_available())
        {
            // let's start with the header
            struct http_header header;
            header.code = HTTP_OK;
            header.content_type = get_file_mime_type(filename);
            header.content_length = file_size;
            header.content_range_start = 0;
            header.content_range_end = 0;
            header.content_range_total = 0;
            char *header_str = format_header(&header);
            if (header_str == NULL)
            {
                response(p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, "Not enough heap memory", false);
                return;
            }
            // transfer the file splitted accordingly to the webserver response buffer size
            int buffer_size;
            bool need_to_split_file;
            if ((file_size + os_strlen(header_str)) > espwebsvr.get_response_max_size())
            {
                buffer_size = espwebsvr.get_response_max_size();
                need_to_split_file = true;
            }
            else
            {
                buffer_size = (file_size + os_strlen(header_str));
                need_to_split_file = false;
            }
            Heap_chunk msg(buffer_size + 1, dont_free);
            if (msg.ref)
            {
                char *ptr = msg.ref;
                // copy the header
                os_strncpy(ptr, header_str, os_strlen(header_str));
                esp_free(header_str);
                // and the file content after that
                ptr = ptr + os_strlen(msg.ref);
                int byte_transferred = buffer_size - os_strlen(msg.ref);
                sel_file.n_read(ptr, byte_transferred);
                // send response
                // esplog.trace("return_file: *p_espconn: %X\n"
                //              "                    msg: %s\n",
                //              p_espconn, msg.ref);
                send_response_buffer(p_espconn, msg.ref);
                // was the content bigger than the buffer ?
                if (need_to_split_file)
                {
                    struct ret_file_response *p_remaining = (struct ret_file_response *)esp_zalloc(sizeof(struct ret_file_response));
                    if (p_remaining == NULL)
                    {
                        esplog.error("Websvr::return_file - not enough heap memory %d\n", sizeof(struct ret_file_response));
                        response(p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, "Not enough heap memory", false);
                        return;
                    }
                    Heap_chunk filename_copy(os_strlen(filename), dont_free);
                    if (filename_copy.ref == NULL)
                    {
                        esplog.error("Websvr::return_file - not enough heap memory %d\n", os_strlen(filename));
                        response(p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, "Not enough heap memory", false);
                        return;
                    }
                    os_strncpy(filename_copy.ref, filename, os_strlen(filename));
                    int timer_idx = get_free_split_msg_timer();
                    // check if there's a timer available
                    if (timer_idx < 0)
                    {
                        esplog.error("Websvr::return_file: no split_response_timer available\n");
                        response(p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, "No timer available", false);
                        return;
                    }
                    p_remaining->p_espconn = p_espconn;
                    p_remaining->filename = filename_copy.ref;
                    p_remaining->file_size = file_size;
                    p_remaining->bytes_transferred = byte_transferred;
                    p_remaining->timer_idx = timer_idx;
                    os_timer_t *split_timer = get_split_msg_timer(timer_idx);
                    os_timer_disarm(split_timer);
                    os_timer_setfn(split_timer, (os_timer_func_t *)send_remaining_file, (void *)p_remaining);
                    os_timer_arm(split_timer, FILE_SENT_TIMER_PERIOD, 0);
                }
                espmem.stack_mon();
            }
            else
            {
                esp_free(header_str);
                esplog.error("Websvr::return_file - not enough heap memory %d\n", buffer_size + 1);
                // may be the file was too big but there is enough heap memory for a response
                response(p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, "Not enough heap memory", false);
            }
            return;
        }
        else
        {
            response(p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, "Cannot open file", false);
            return;
        }
    }
    else
    {
        response(p_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, "File system is not available", false);
        return;
    }
}

void ICACHE_FLASH_ATTR espbot_http_routes(struct espconn *ptr_espconn, Html_parsed_req *parsed_req)
{
    esplog.all("espbot_http_routes\n");

    if ((0 == os_strcmp(parsed_req->url, "/")) && (parsed_req->req_method == HTTP_GET)) // home
    {
        // home: look for index.html
        char *file_name = "index.html";
        return_file(ptr_espconn, file_name);
        return;
    }
    if ((os_strncmp(parsed_req->url, "/api/", 5)) && (parsed_req->req_method == HTTP_GET)) // file
    {
        // not an api: look for specified file
        char *file_name = parsed_req->url + os_strlen("/");
        return_file(ptr_espconn, file_name);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, "/api/debug/log")) && (parsed_req->req_method == HTTP_GET))
    {
        // check how much memory needed for last logs
        int esp_event_log_len = 0;
        char *err_ptr = esp_event_log.get_head();
        while (err_ptr)
        {
            esp_event_log_len += os_strlen(err_ptr);
            err_ptr = esp_event_log.next();
        }
        char *msg = (char *)esp_zalloc(16 +                         // formatting string
                                       10 +                         // heap mem figures
                                       (3 * esp_event_log.size()) + // errors formatting
                                       esp_event_log_len);          // errors
        if (msg)
        {
            os_sprintf(msg, "{\"events\":[");
            // now add saved errors
            char *str_ptr;
            int cnt = 0;
            espmem.stack_mon();
            err_ptr = esp_event_log.get_head();
            while (err_ptr)
            {
                str_ptr = msg + os_strlen(msg);
                if (cnt == 0)
                    os_sprintf(str_ptr, "%s", err_ptr);
                else
                    os_sprintf(str_ptr, ",%s", err_ptr);
                err_ptr = esp_event_log.next();
                cnt++;
            }
            str_ptr = msg + os_strlen(msg);
            os_sprintf(str_ptr, "]}");
            response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
            // esp_free(msg); // dont't free the msg buffer cause it could not have been used yet
        }
        else
        {
            esplog.error("Websvr::webserver_recv - not enough heap memory %d\n",
                         16 + 10 + (3 * esp_event_log.size()) + esp_event_log_len);
        }
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, "/api/debug/hexmemdump")) && (parsed_req->req_method == HTTP_GET))
    {
        char *address;
        int length;
        Json_str debug_cfg(parsed_req->req_content, parsed_req->content_len);
        espmem.stack_mon();
        if (debug_cfg.syntax_check() == JSON_SINTAX_OK)
        {
            if (debug_cfg.find_pair("address") != JSON_NEW_PAIR_FOUND)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Cannot find JSON string 'address'", false);
                return;
            }
            if (debug_cfg.get_cur_pair_value_type() != JSON_STRING)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "JSON pair with string 'address' does not have a string value type", false);
                return;
            }
            Heap_chunk address_hex_str(debug_cfg.get_cur_pair_value_len() + 1);
            if (address_hex_str.ref)
            {
                os_strncpy(address_hex_str.ref, debug_cfg.get_cur_pair_value(), debug_cfg.get_cur_pair_value_len());
            }
            else
            {
                esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", debug_cfg.get_cur_pair_value_len() + 1);
                return;
            }
            address = (char *)atoh(address_hex_str.ref);
            if (debug_cfg.find_pair("length") != JSON_NEW_PAIR_FOUND)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Cannot find JSON string 'length'", false);
                return;
            }
            if (debug_cfg.get_cur_pair_value_type() != JSON_INTEGER)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "JSON pair with string 'length' does not have an integer value type", false);
                return;
            }
            Heap_chunk length_str(debug_cfg.get_cur_pair_value_len() + 1);
            if (length_str.ref)
            {
                os_strncpy(length_str.ref, debug_cfg.get_cur_pair_value(), debug_cfg.get_cur_pair_value_len());
            }
            else
            {
                esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", debug_cfg.get_cur_pair_value_len() + 1);
                return;
            }
            length = atoi(length_str.ref);
            espmem.stack_mon();
        }
        else
        {
            response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Json bad syntax", false);
            return;
        }
        Heap_chunk msg(48 + length * 3, dont_free);
        if (msg.ref)
        {
            os_sprintf(msg.ref,
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
            os_sprintf(msg.ref + os_strlen(msg.ref), "\"}");
            response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
            // esp_free(msg); // dont't free the msg buffer cause it could not have been used yet
        }
        else
        {
            esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", 64);
        }
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, "/api/debug/memdump")) && (parsed_req->req_method == HTTP_GET))
    {
        char *address;
        int length;
        Json_str debug_cfg(parsed_req->req_content, parsed_req->content_len);
        espmem.stack_mon();
        if (debug_cfg.syntax_check() == JSON_SINTAX_OK)
        {
            if (debug_cfg.find_pair("address") != JSON_NEW_PAIR_FOUND)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Cannot find JSON string 'address'", false);
                return;
            }
            if (debug_cfg.get_cur_pair_value_type() != JSON_STRING)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "JSON pair with string 'address' does not have a string value type", false);
                return;
            }
            Heap_chunk address_hex_str(debug_cfg.get_cur_pair_value_len() + 1);
            if (address_hex_str.ref)
            {
                os_strncpy(address_hex_str.ref, debug_cfg.get_cur_pair_value(), debug_cfg.get_cur_pair_value_len());
            }
            else
            {
                esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", debug_cfg.get_cur_pair_value_len() + 1);
                return;
            }
            address = (char *)atoh(address_hex_str.ref);
            if (debug_cfg.find_pair("length") != JSON_NEW_PAIR_FOUND)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Cannot find JSON string 'length'", false);
                return;
            }
            if (debug_cfg.get_cur_pair_value_type() != JSON_INTEGER)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "JSON pair with string 'length' does not have an integer value type", false);
                return;
            }
            Heap_chunk length_str(debug_cfg.get_cur_pair_value_len() + 1);
            if (length_str.ref)
            {
                os_strncpy(length_str.ref, debug_cfg.get_cur_pair_value(), debug_cfg.get_cur_pair_value_len());
            }
            else
            {
                esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", debug_cfg.get_cur_pair_value_len() + 1);
                return;
            }
            length = atoi(length_str.ref);
            espmem.stack_mon();
        }
        else
        {
            response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Json bad syntax", false);
            return;
        }
        Heap_chunk msg(48 + length, dont_free);
        if (msg.ref)
        {
            os_sprintf(msg.ref,
                       "{\"address\":\"%X\",\"length\": %d,\"content\":\"",
                       address,
                       length);
            int cnt;
            char *ptr = msg.ref + os_strlen(msg.ref);
            for (cnt = 0; cnt < length; cnt++)
                os_sprintf(ptr++, "%c", *(address + cnt));
            os_sprintf(msg.ref + os_strlen(msg.ref), "\"}");
            response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
            // esp_free(msg); // dont't free the msg buffer cause it could not have been used yet
        }
        else
        {
            esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", 64);
        }
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, "/api/debug/meminfo")) && (parsed_req->req_method == HTTP_GET))
    {
        // count the heap items
        int heap_item_count = 0;
        struct heap_item *heap_obj_ptr = espmem.get_heap_item(first);
        while (heap_obj_ptr)
        {
            heap_item_count++;
            heap_obj_ptr = espmem.get_heap_item(next);
        }

        char *msg = (char *)esp_zalloc(172 +                          // formatting string
                                       (42 * (heap_item_count + 1))); // heap objects
        if (msg)
        {
            os_sprintf(msg,
                       "{\"stack_max_addr\":\"%X\",\"stack_min_addr\":\"%X\",\"heap_start_addr\":\"%X\",\"heap_used_size\": %d,\"heap_max_size\": %d,\"heap_min_size\": %d,\"heap_max_obj\": %d,\"heap_obj_list\": [",
                       espmem.get_max_stack_addr(),
                       espmem.get_min_stack_addr(),
                       espmem.get_start_heap_addr(),
                       espmem.get_used_heap_size(),
                       espmem.get_max_heap_size(),
                       espmem.get_mim_heap_size(),
                       espmem.get_max_heap_objs());
            // now add saved errors
            char *str_ptr;
            int cnt = 0;
            espmem.stack_mon();
            heap_obj_ptr = espmem.get_heap_item(first);
            while (heap_obj_ptr)
            {
                str_ptr = msg + os_strlen(msg);
                if (cnt == 0)
                {
                    os_sprintf(str_ptr, "{\"addr\":\"%X\",\"size\":%d}", heap_obj_ptr->addr, heap_obj_ptr->size);
                    cnt++;
                }
                else
                    os_sprintf(str_ptr, ",{\"addr\":\"%X\",\"size\":%d}", heap_obj_ptr->addr, heap_obj_ptr->size);

                heap_obj_ptr = espmem.get_heap_item(next);
            }
            str_ptr = msg + os_strlen(msg);
            os_sprintf(str_ptr, "]}");
            response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
            // esp_free(msg); // dont't free the msg buffer cause it could not have been used yet
        }
        else
        {
            esplog.error("Websvr::webserver_recv - not enough heap memory %d\n",
                         172 + (42 * heap_item_count));
        }
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, "/api/debug/cfg")) && (parsed_req->req_method == HTTP_GET))
    {
        char *msg = (char *)esp_zalloc(64);
        if (msg)
        {
            os_sprintf(msg,
                       "{\"logger_serial_level\": %d,\"logger_memory_level\": %d}",
                       esplog.get_serial_level(),
                       esplog.get_memory_level());
            response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
            // esp_free(msg); // dont't free the msg buffer cause it could not have been used yet
        }
        else
        {
            esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", 64);
        }
        return;
    }
    if ((os_strcmp(parsed_req->url, "/api/debug/cfg") == 0) && (parsed_req->req_method == HTTP_POST))
    {
        Json_str debug_cfg(parsed_req->req_content, parsed_req->content_len);
        espmem.stack_mon();
        if (debug_cfg.syntax_check() == JSON_SINTAX_OK)
        {
            if (debug_cfg.find_pair("logger_serial_level") != JSON_NEW_PAIR_FOUND)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Cannot find JSON string 'logger_serial_level'", false);
                return;
            }
            if (debug_cfg.get_cur_pair_value_type() != JSON_INTEGER)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "JSON pair with string 'logger_serial_level' does not have an integer value type", false);
                return;
            }
            char *tmp_serial_level = (char *)esp_zalloc(debug_cfg.get_cur_pair_value_len() + 1);
            if (tmp_serial_level)
            {
                os_strncpy(tmp_serial_level, debug_cfg.get_cur_pair_value(), debug_cfg.get_cur_pair_value_len());
            }
            else
            {
                esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", debug_cfg.get_cur_pair_value_len() + 1);
                return;
            }
            if (debug_cfg.find_pair("logger_memory_level") != JSON_NEW_PAIR_FOUND)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Cannot find JSON string 'logger_memory_level'", false);
                esp_free(tmp_serial_level);
                return;
            }
            if (debug_cfg.get_cur_pair_value_type() != JSON_INTEGER)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "JSON pair with string 'logger_memory_level' does not have an integer value type", false);
                esp_free(tmp_serial_level);
                return;
            }
            char *tmp_memory_level = (char *)esp_zalloc(debug_cfg.get_cur_pair_value_len() + 1);
            if (tmp_memory_level)
            {
                os_strncpy(tmp_memory_level, debug_cfg.get_cur_pair_value(), debug_cfg.get_cur_pair_value_len());
            }
            else
            {
                esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", debug_cfg.get_cur_pair_value_len() + 1);
                return;
            }
            esplog.set_levels(atoi(tmp_serial_level), atoi(tmp_memory_level));
            esp_free(tmp_serial_level);
            esp_free(tmp_memory_level);
            espmem.stack_mon();
        }
        else
        {
            response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Json bad syntax", false);
            return;
        }
        char *msg = (char *)esp_zalloc(64);
        if (msg)
        {
            os_sprintf(msg,
                       "{\"logger_serial_level\": %d,\"logger_memory_level\": %d}",
                       esplog.get_serial_level(),
                       esplog.get_memory_level());
            response(ptr_espconn, HTTP_CREATED, HTTP_CONTENT_JSON, msg, true);
            // esp_free(msg); // dont't free the msg buffer cause it could not have been used yet
        }
        else
        {
            esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", 64);
        }
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, "/api/espbot/cfg")) && (parsed_req->req_method == HTTP_GET))
    {
        char *msg = (char *)esp_zalloc(64);
        if (msg)
        {
            os_sprintf(msg, "{\"espbot_name\":\"%s\"}", espbot.get_name());
            response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
            // esp_free(msg); // dont't free the msg buffer cause it could not have been used yet
        }
        else
        {
            esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", 64);
        }
        return;
    }
    if ((os_strcmp(parsed_req->url, "/api/espbot/cfg") == 0) && (parsed_req->req_method == HTTP_POST))
    {
        Json_str espbot_cfg(parsed_req->req_content, parsed_req->content_len);
        espmem.stack_mon();
        if (espbot_cfg.syntax_check() == JSON_SINTAX_OK)
        {
            if (espbot_cfg.find_pair("espbot_name") != JSON_NEW_PAIR_FOUND)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Cannot find JSON string 'espbot_name'", false);
                return;
            }
            if (espbot_cfg.get_cur_pair_value_type() != JSON_STRING)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "JSON pair with string 'espbot_name' does not have a STRING value type", false);
                return;
            }
            char *tmp_name = (char *)esp_zalloc(espbot_cfg.get_cur_pair_value_len() + 1);
            if (tmp_name)
            {
                os_strncpy(tmp_name, espbot_cfg.get_cur_pair_value(), espbot_cfg.get_cur_pair_value_len());
                espbot.set_name(tmp_name);
                esp_free(tmp_name);
            }
            else
            {
                esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", espbot_cfg.get_cur_pair_value_len() + 1);
            }
            espmem.stack_mon();
        }
        else
        {
            response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Json bad syntax", false);
            return;
        }
        char *msg = (char *)esp_zalloc(64);
        if (msg)
        {
            os_sprintf(msg, "{\"espbot_name\":\"%s\"}", espbot.get_name());
            response(ptr_espconn, HTTP_CREATED, HTTP_CONTENT_JSON, msg, true);
            // esp_free(msg); // dont't free the msg buffer cause it could not have been used yet
        }
        else
        {
            esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", 64);
        }
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, "/api/espbot/info")) && (parsed_req->req_method == HTTP_GET))
    {
        char *msg = (char *)esp_zalloc(350);
        if (msg)
        {
            os_sprintf(msg, "{\"espbot_name\":\"%s\","
                            "\"espbot_version\":\"%s\","
                            "\"chip_id\":\"%d\","
                            "\"sdk_version\":\"%s\","
                            "\"boot_version\":\"%d\"}",
                       espbot.get_name(),
                       espbot_release,
                       system_get_chip_id(),
                       system_get_sdk_version(),
                       system_get_boot_version());
            response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
            // esp_free(msg); // dont't free the msg buffer cause it could not have been used yet
        }
        else
        {
            esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", 350);
        }
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, "/api/espbot/reset")) && (parsed_req->req_method == HTTP_POST))
    {
        response(ptr_espconn, HTTP_ACCEPTED, HTTP_CONTENT_TEXT, "", false);
        espbot.reset(ESP_REBOOT);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, "/api/fs/info")) && (parsed_req->req_method == HTTP_GET))
    {
        if (espfs.is_available())
        {
            char *msg = (char *)esp_zalloc(128);
            if (msg)
            {
                os_sprintf(msg, "{\"file_system_size\": %d,"
                                "\"file_system_used_size\": %d}",
                           espfs.get_total_size(), espfs.get_used_size());
                response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
                // esp_free(msg); // dont't free the msg buffer cause it could not have been used yet
                return;
            }
            else
            {
                esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", 128);
                return;
            }
        }
        else
        {
            response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, "File system is not available", false);
            return;
        }
    }
    if ((0 == os_strcmp(parsed_req->url, "/api/fs/format")) && (parsed_req->req_method == HTTP_POST))
    {
        if (espfs.is_available())
        {
            response(ptr_espconn, HTTP_ACCEPTED, HTTP_CONTENT_TEXT, "", false);
            os_timer_disarm(&format_delay_timer);
            os_timer_setfn(&format_delay_timer, (os_timer_func_t *)format_function, NULL);
            os_timer_arm(&format_delay_timer, 500, 0);
            return;
        }
        else
        {
            response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, "File system is not available", false);
            return;
        }
    }
    if ((0 == os_strcmp(parsed_req->url, "/api/files/ls")) && (parsed_req->req_method == HTTP_GET))
    {
        if (espfs.is_available())
        {
            int file_cnt = 0;
            struct spiffs_dirent *file_ptr = espfs.list(0);
            // count files first
            while (file_ptr)
            {
                file_cnt++;
                file_ptr = espfs.list(1);
            }
            // now prepare the list
            char *file_list = (char *)esp_zalloc(32 + (file_cnt * (32 + 3)));
            if (file_list)
            {
                char *tmp_ptr = file_list;
                os_sprintf(file_list, "{\"files\":[");
                file_ptr = espfs.list(0);
                while (file_ptr)
                {
                    tmp_ptr = file_list + os_strlen(file_list);
                    if (tmp_ptr != (file_list + os_strlen("{\"files\":[")))
                        *(tmp_ptr++) = ',';
                    os_sprintf(tmp_ptr, "\"%s\"", (char *)file_ptr->name);
                    file_ptr = espfs.list(1);
                }
                tmp_ptr = file_list + os_strlen(file_list);
                os_sprintf(tmp_ptr, "]}");
                response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, file_list, true);
                // esp_free(file_list); // dont't free the msg buffer cause it could not have been used yet
                espmem.stack_mon();
                return;
            }
            else
            {
                esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", 128);
                return;
            }
        }
        else
        {
            response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, "File system is not available", false);
            return;
        }
    }
    if ((0 == os_strncmp(parsed_req->url, "/api/files/cat/", os_strlen("/api/files/cat/"))) && (parsed_req->req_method == HTTP_GET))
    {
        char *file_name = parsed_req->url + os_strlen("/api/files/cat/");
        if (os_strlen(file_name) == 0)
        {
            response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "No file name provided", false);
            return;
        }
        return_file(ptr_espconn, file_name);
        return;
    }
    if ((0 == os_strncmp(parsed_req->url, "/api/files/delete/", os_strlen("/api/files/delete/"))) && (parsed_req->req_method == HTTP_POST))
    {
        char *file_name = parsed_req->url + os_strlen("/api/files/delete/");
        if (os_strlen(file_name) == 0)
        {
            response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "No file name provided", false);
            return;
        }
        if (espfs.is_available())
        {
            if (!Ffile::exists(&espfs, file_name))
            {
                response(ptr_espconn, HTTP_NOT_FOUND, HTTP_CONTENT_JSON, "File not found", false);
                return;
            }
            Ffile sel_file(&espfs, file_name);
            espmem.stack_mon();
            if (sel_file.is_available())
            {
                response(ptr_espconn, HTTP_ACCEPTED, HTTP_CONTENT_TEXT, "", false);
                sel_file.remove();
                return;
            }
            else
            {
                response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, "Cannot open file", false);
                return;
            }
        }
        else
        {
            response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, "File system is not available", false);
            return;
        }
    }
    if ((0 == os_strncmp(parsed_req->url, "/api/files/create/", os_strlen("/api/files/create/"))) && (parsed_req->req_method == HTTP_POST))
    {
        char *file_name = parsed_req->url + os_strlen("/api/files/create/");
        if (os_strlen(file_name) == 0)
        {
            response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "No file name provided", false);
            return;
        }
        if (espfs.is_available())
        {
            if (Ffile::exists(&espfs, file_name))
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "File already exists", false);
                return;
            }
            Ffile sel_file(&espfs, file_name);
            espmem.stack_mon();
            if (sel_file.is_available())
            {
                sel_file.n_append(parsed_req->req_content, parsed_req->content_len);
                response(ptr_espconn, HTTP_CREATED, HTTP_CONTENT_TEXT, "", false);
                return;
            }
            else
            {
                response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, "Cannot open file", false);
                return;
            }
        }
        else
        {
            response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, "File system is not available", false);
            return;
        }
    }
    if ((0 == os_strncmp(parsed_req->url, "/api/files/append/", os_strlen("/api/files/append/"))) && (parsed_req->req_method == HTTP_POST))
    {
        char *file_name = parsed_req->url + os_strlen("/api/files/append/");
        if (os_strlen(file_name) == 0)
        {
            response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "No file name provided", false);
            return;
        }
        if (espfs.is_available())
        {
            if (!Ffile::exists(&espfs, file_name))
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "File does not exists", false);
                return;
            }
            Ffile sel_file(&espfs, file_name);
            espmem.stack_mon();
            if (sel_file.is_available())
            {
                sel_file.n_append(parsed_req->req_content, parsed_req->content_len);
                response(ptr_espconn, HTTP_CREATED, HTTP_CONTENT_TEXT, "", false);
                return;
            }
            else
            {
                response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, "Cannot open file", false);
                return;
            }
        }
        else
        {
            response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, "File system is not available", false);
            return;
        }
    }
    if ((os_strcmp(parsed_req->url, "/api/gpio/cfg") == 0) && (parsed_req->req_method == HTTP_POST))
    {
        int gpio_pin;
        int gpio_type;
        Json_str gpio_cfg(parsed_req->req_content, parsed_req->content_len);
        if (gpio_cfg.syntax_check() == JSON_SINTAX_OK)
        {
            if (gpio_cfg.find_pair("gpio_pin") != JSON_NEW_PAIR_FOUND)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Cannot find JSON string 'gpio_pin'", false);
                return;
            }
            if (gpio_cfg.get_cur_pair_value_type() != JSON_INTEGER)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "JSON pair with string 'gpio_pin' does not have an integer value type", false);
                return;
            }
            Heap_chunk tmp_pin(gpio_cfg.get_cur_pair_value_len());
            if (tmp_pin.ref)
            {
                os_strncpy(tmp_pin.ref, gpio_cfg.get_cur_pair_value(), gpio_cfg.get_cur_pair_value_len());
            }
            else
            {
                esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", gpio_cfg.get_cur_pair_value_len() + 1);
                return;
            }
            if (gpio_cfg.find_pair("gpio_type") != JSON_NEW_PAIR_FOUND)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Cannot find JSON string 'gpio_type'", false);
                return;
            }
            if (gpio_cfg.get_cur_pair_value_type() != JSON_STRING)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "JSON pair with string 'gpio_type' does not have a string value type", false);
                return;
            }
            Heap_chunk tmp_type(gpio_cfg.get_cur_pair_value_len());
            if (tmp_type.ref)
            {
                os_strncpy(tmp_type.ref, gpio_cfg.get_cur_pair_value(), gpio_cfg.get_cur_pair_value_len());
            }
            else
            {
                esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", gpio_cfg.get_cur_pair_value_len() + 1);
                return;
            }
            gpio_pin = atoi(tmp_pin.ref);
            if ((os_strcmp(tmp_type.ref, "INPUT") == 0) || (os_strcmp(tmp_type.ref, "input") == 0))
                gpio_type = ESPBOT_GPIO_INPUT;
            else if ((os_strcmp(tmp_type.ref, "OUTPUT") == 0) || (os_strcmp(tmp_type.ref, "output") == 0))
                gpio_type = ESPBOT_GPIO_OUTPUT;
            else
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Wrong gpio type", false);
                return;
            }
            if (esp_gpio.config(gpio_pin, gpio_type) == ESPBOT_GPIO_WRONG_IDX)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Wrong gpio pin", false);
                return;
            }
            espmem.stack_mon();
        }
        else
        {
            response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Json bad syntax", false);
            return;
        }

        Heap_chunk msg(parsed_req->content_len, dont_free);
        if (msg.ref)
        {
            os_strncpy(msg.ref, parsed_req->req_content, parsed_req->content_len);
            response(ptr_espconn, HTTP_CREATED, HTTP_CONTENT_JSON, msg.ref, true);
            // esp_free(msg); // dont't free the msg buffer cause it could not have been used yet
        }
        else
        {
            esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", 64);
        }
        espmem.stack_mon();
        return;
    }
    if ((os_strcmp(parsed_req->url, "/api/gpio/uncfg") == 0) && (parsed_req->req_method == HTTP_POST))
    {
        int gpio_pin;
        int gpio_type;
        Json_str gpio_cfg(parsed_req->req_content, parsed_req->content_len);
        if (gpio_cfg.syntax_check() == JSON_SINTAX_OK)
        {
            if (gpio_cfg.find_pair("gpio_pin") != JSON_NEW_PAIR_FOUND)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Cannot find JSON string 'gpio_pin'", false);
                return;
            }
            if (gpio_cfg.get_cur_pair_value_type() != JSON_INTEGER)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "JSON pair with string 'gpio' does not have an integer value type", false);
                return;
            }
            Heap_chunk tmp_pin(gpio_cfg.get_cur_pair_value_len());
            if (tmp_pin.ref)
            {
                os_strncpy(tmp_pin.ref, gpio_cfg.get_cur_pair_value(), gpio_cfg.get_cur_pair_value_len());
            }
            else
            {
                esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", gpio_cfg.get_cur_pair_value_len() + 1);
                return;
            }
            gpio_pin = atoi(tmp_pin.ref);
            if (esp_gpio.unconfig(gpio_pin) == ESPBOT_GPIO_WRONG_IDX)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Wrong gpio pin", false);
                return;
            }
            espmem.stack_mon();
        }
        else
        {
            response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Json bad syntax", false);
            return;
        }

        Heap_chunk msg(48, dont_free);
        if (msg.ref)
        {
            os_sprintf(msg.ref, "{\"gpio_pin\": %d,\"gpio_type\":\"unprovisioned\"}", gpio_pin);
            response(ptr_espconn, HTTP_CREATED, HTTP_CONTENT_JSON, msg.ref, true);
            // esp_free(msg); // dont't free the msg buffer cause it could not have been used yet
        }
        else
        {
            esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", 64);
        }
        espmem.stack_mon();
        return;
    }
    if ((os_strcmp(parsed_req->url, "/api/gpio/cfg") == 0) && (parsed_req->req_method == HTTP_GET))
    {
        int gpio_pin;
        int result;
        Json_str gpio_cfg(parsed_req->req_content, parsed_req->content_len);
        if (gpio_cfg.syntax_check() == JSON_SINTAX_OK)
        {
            if (gpio_cfg.find_pair("gpio_pin") != JSON_NEW_PAIR_FOUND)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Cannot find JSON string 'gpio_pin'", false);
                return;
            }
            if (gpio_cfg.get_cur_pair_value_type() != JSON_INTEGER)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "JSON pair with string 'gpio_pin' does not have an integer value type", false);
                return;
            }
            Heap_chunk tmp_pin(gpio_cfg.get_cur_pair_value_len());
            if (tmp_pin.ref)
            {
                os_strncpy(tmp_pin.ref, gpio_cfg.get_cur_pair_value(), gpio_cfg.get_cur_pair_value_len());
            }
            else
            {
                esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", gpio_cfg.get_cur_pair_value_len() + 1);
                return;
            }
            gpio_pin = atoi(tmp_pin.ref);
            espmem.stack_mon();
        }
        else
        {
            response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Json bad syntax", false);
            return;
        }

        Heap_chunk msg(48, dont_free);
        if (msg.ref)
        {
            result = esp_gpio.get_config(gpio_pin);
            switch (result)
            {
            case ESPBOT_GPIO_WRONG_IDX:
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Wrong gpio pin", false);
                return;
            case ESPBOT_GPIO_UNPROVISIONED:
                os_sprintf(msg.ref, "{\"gpio_pin\": %d,\"gpio_type\":\"unprovisioned\"}", gpio_pin);
                response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
                return;
            case ESPBOT_GPIO_INPUT:
                os_sprintf(msg.ref, "{\"gpio_pin\": %d,\"gpio_type\":\"input\"}", gpio_pin);
                response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
                return;
            case ESPBOT_GPIO_OUTPUT:
                os_sprintf(msg.ref, "{\"gpio_pin\": %d,\"gpio_type\":\"output\"}", gpio_pin);
                response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
                return;
            default:
                response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, "Gpio.get_config error", false);
                return;
            }
        }
        else
        {
            esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", 64);
        }
        espmem.stack_mon();
        return;
    }
    if ((os_strcmp(parsed_req->url, "/api/gpio/read") == 0) && (parsed_req->req_method == HTTP_GET))
    {
        int gpio_pin;
        int result;
        Json_str gpio_cfg(parsed_req->req_content, parsed_req->content_len);
        if (gpio_cfg.syntax_check() == JSON_SINTAX_OK)
        {
            if (gpio_cfg.find_pair("gpio_pin") != JSON_NEW_PAIR_FOUND)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Cannot find JSON string 'gpio_pin'", false);
                return;
            }
            if (gpio_cfg.get_cur_pair_value_type() != JSON_INTEGER)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "JSON pair with string 'gpio_pin' does not have an integer value type", false);
                return;
            }
            Heap_chunk tmp_pin(gpio_cfg.get_cur_pair_value_len());
            if (tmp_pin.ref)
            {
                os_strncpy(tmp_pin.ref, gpio_cfg.get_cur_pair_value(), gpio_cfg.get_cur_pair_value_len());
            }
            else
            {
                esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", gpio_cfg.get_cur_pair_value_len() + 1);
                return;
            }
            gpio_pin = atoi(tmp_pin.ref);
            espmem.stack_mon();
        }
        else
        {
            response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Json bad syntax", false);
            return;
        }

        Heap_chunk msg(48, dont_free);
        if (msg.ref)
        {
            result = esp_gpio.read(gpio_pin);
            switch (result)
            {
            case ESPBOT_GPIO_WRONG_IDX:
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Wrong gpio pin", false);
                return;
            case ESPBOT_GPIO_UNPROVISIONED:
                os_sprintf(msg.ref, "{\"gpio_pin\": %d,\"gpio_type\":\"unprovisioned\"}", gpio_pin);
                break;
            case ESPBOT_LOW:
                os_sprintf(msg.ref, "{\"gpio_pin\": %d,\"gpio_status\":\"LOW\"}", gpio_pin);
                break;
            case ESPBOT_HIGH:

                os_sprintf(msg.ref, "{\"gpio_pin\": %d,\"gpio_status\":\"HIGH\"}", gpio_pin);
                break;
            default:
                break;
            }
        }
        else
        {
            esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", 64);
        }
        response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
        espmem.stack_mon();
        return;
    }
    if ((os_strcmp(parsed_req->url, "/api/gpio/set") == 0) && (parsed_req->req_method == HTTP_POST))
    {
        int gpio_pin;
        int output_level;
        int result;
        Json_str gpio_cfg(parsed_req->req_content, parsed_req->content_len);
        if (gpio_cfg.syntax_check() == JSON_SINTAX_OK)
        {
            if (gpio_cfg.find_pair("gpio_pin") != JSON_NEW_PAIR_FOUND)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Cannot find JSON string 'gpio_pin'", false);
                return;
            }
            if (gpio_cfg.get_cur_pair_value_type() != JSON_INTEGER)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "JSON pair with string 'gpio_pin' does not have an integer value type", false);
                return;
            }
            Heap_chunk tmp_pin(gpio_cfg.get_cur_pair_value_len());
            if (tmp_pin.ref)
            {
                os_strncpy(tmp_pin.ref, gpio_cfg.get_cur_pair_value(), gpio_cfg.get_cur_pair_value_len());
            }
            else
            {
                esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", gpio_cfg.get_cur_pair_value_len() + 1);
                return;
            }
            if (gpio_cfg.find_pair("gpio_status") != JSON_NEW_PAIR_FOUND)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Cannot find JSON string 'gpio_status'", false);
                return;
            }
            if (gpio_cfg.get_cur_pair_value_type() != JSON_STRING)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "JSON pair with string 'gpio_status' does not have a string value type", false);
                return;
            }
            Heap_chunk tmp_level(gpio_cfg.get_cur_pair_value_len());
            if (tmp_level.ref)
            {
                os_strncpy(tmp_level.ref, gpio_cfg.get_cur_pair_value(), gpio_cfg.get_cur_pair_value_len());
            }
            else
            {
                esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", gpio_cfg.get_cur_pair_value_len() + 1);
                return;
            }
            gpio_pin = atoi(tmp_pin.ref);
            if ((os_strcmp(tmp_level.ref, "LOW") == 0) || (os_strcmp(tmp_level.ref, "low") == 0))
                output_level = ESPBOT_LOW;
            else if ((os_strcmp(tmp_level.ref, "HIGH") == 0) || (os_strcmp(tmp_level.ref, "high") == 0))
                output_level = ESPBOT_HIGH;
            else
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Wrong gpio level", false);
                return;
            }
            espmem.stack_mon();
        }
        else
        {
            response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Json bad syntax", false);
            return;
        }

        Heap_chunk msg(48, dont_free);
        if (msg.ref)
        {
            result = esp_gpio.set(gpio_pin, output_level);
            switch (result)
            {
            case ESPBOT_GPIO_WRONG_IDX:
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Wrong gpio pin", false);
                return;
            case ESPBOT_GPIO_UNPROVISIONED:
                os_sprintf(msg.ref, "{\"gpio_pin\": %d,\"gpio_type\":\"unprovisioned\"}", gpio_pin);
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, msg.ref, true);
                return;
            case ESPBOT_GPIO_WRONG_LVL:
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Wrong output level", false);
                return;
            case ESPBOT_GPIO_CANNOT_CHANGE_INPUT:
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Cannot change input", false);
                return;
            case ESPBOT_GPIO_OK:
                os_strncpy(msg.ref, parsed_req->req_content, parsed_req->content_len);
                response(ptr_espconn, HTTP_CREATED, HTTP_CONTENT_JSON, msg.ref, true);
                return;
            default:
                break;
            }
            // esp_free(msg); // dont't free the msg buffer cause it could not have been used yet
        }
        else
        {
            esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", 64);
        }
        espmem.stack_mon();
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, "/api/ota/info")) && (parsed_req->req_method == HTTP_GET))
    {
        char *msg = (char *)esp_zalloc(36);

        if (msg)
        {
            os_sprintf(msg, "{\"ota_status\": %d}", esp_ota.get_status());
            response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
            // esp_free(msg); // dont't free the msg buffer cause it could not have been used yet
        }
        else
        {
            esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", 36);
        }
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, "/api/ota/cfg")) && (parsed_req->req_method == HTTP_GET))
    {
        char *msg = (char *)esp_zalloc(90 +
                                       16 +
                                       6 +
                                       os_strlen(esp_ota.get_path()) +
                                       10);
        if (msg)
        {
            os_sprintf(msg,
                       "{\"host\":\"%s\",\"port\":%d,\"path\":\"%s\",\"check_version\":\"%s\",\"reboot_on_completion\":\"%s\"}",
                       esp_ota.get_host(),
                       esp_ota.get_port(),
                       esp_ota.get_path(),
                       esp_ota.get_check_version(),
                       esp_ota.get_reboot_on_completion());
            response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
            // esp_free(msg); // dont't free the msg buffer cause it could not have been used yet
        }
        else
        {
            esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", 64);
        }
        return;
    }
    if ((os_strcmp(parsed_req->url, "/api/ota/cfg") == 0) && (parsed_req->req_method == HTTP_POST))
    {
        Json_str ota_cfg(parsed_req->req_content, parsed_req->content_len);
        if (ota_cfg.syntax_check() == JSON_SINTAX_OK)
        {
            if (ota_cfg.find_pair("host") != JSON_NEW_PAIR_FOUND)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Cannot find JSON string 'host'", false);
                return;
            }
            if (ota_cfg.get_cur_pair_value_type() != JSON_STRING)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "JSON pair with string 'host' does not have a string value type", false);
                return;
            }
            Heap_chunk tmp_host(ota_cfg.get_cur_pair_value_len());
            if (tmp_host.ref)
            {
                os_strncpy(tmp_host.ref, ota_cfg.get_cur_pair_value(), ota_cfg.get_cur_pair_value_len());
            }
            else
            {
                esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", ota_cfg.get_cur_pair_value_len() + 1);
                return;
            }
            if (ota_cfg.find_pair("port") != JSON_NEW_PAIR_FOUND)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Cannot find JSON string 'port'", false);
                return;
            }
            if (ota_cfg.get_cur_pair_value_type() != JSON_INTEGER)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "JSON pair with string 'port' does not have an integer value type", false);
                return;
            }
            Heap_chunk tmp_port(ota_cfg.get_cur_pair_value_len());
            if (tmp_port.ref)
            {
                os_strncpy(tmp_port.ref, ota_cfg.get_cur_pair_value(), ota_cfg.get_cur_pair_value_len());
            }
            else
            {
                esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", ota_cfg.get_cur_pair_value_len() + 1);
                return;
            }
            if (ota_cfg.find_pair("path") != JSON_NEW_PAIR_FOUND)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Cannot find JSON string 'path'", false);
                return;
            }
            if (ota_cfg.get_cur_pair_value_type() != JSON_STRING)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "JSON pair with string 'path' does not have a string value type", false);
                return;
            }
            Heap_chunk tmp_path(ota_cfg.get_cur_pair_value_len());
            if (tmp_path.ref)
            {
                os_strncpy(tmp_path.ref, ota_cfg.get_cur_pair_value(), ota_cfg.get_cur_pair_value_len());
            }
            else
            {
                esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", ota_cfg.get_cur_pair_value_len() + 1);
                return;
            }
            if (ota_cfg.find_pair("check_version") != JSON_NEW_PAIR_FOUND)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Cannot find JSON string 'check_version'", false);
                return;
            }
            if (ota_cfg.get_cur_pair_value_type() != JSON_STRING)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "JSON pair with string 'check_version' does not have a string value type", false);
                return;
            }
            Heap_chunk tmp_check_version(ota_cfg.get_cur_pair_value_len());
            if (tmp_check_version.ref)
            {
                os_strncpy(tmp_check_version.ref, ota_cfg.get_cur_pair_value(), ota_cfg.get_cur_pair_value_len());
            }
            else
            {
                esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", ota_cfg.get_cur_pair_value_len() + 1);
                return;
            }
            if (ota_cfg.find_pair("reboot_on_completion") != JSON_NEW_PAIR_FOUND)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Cannot find JSON string 'reboot_on_completion'", false);
                return;
            }
            if (ota_cfg.get_cur_pair_value_type() != JSON_STRING)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "JSON pair with string 'reboot_on_completion' does not have a string value type", false);
                return;
            }
            Heap_chunk tmp_reboot_on_completion(ota_cfg.get_cur_pair_value_len());
            if (tmp_reboot_on_completion.ref)
            {
                os_strncpy(tmp_reboot_on_completion.ref, ota_cfg.get_cur_pair_value(), ota_cfg.get_cur_pair_value_len());
            }
            else
            {
                esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", ota_cfg.get_cur_pair_value_len() + 1);
                return;
            }
            esp_ota.set_host(tmp_host.ref);
            esp_ota.set_port(tmp_port.ref);
            esp_ota.set_path(tmp_path.ref);
            esp_ota.set_check_version(tmp_check_version.ref);
            esp_ota.set_reboot_on_completion(tmp_reboot_on_completion.ref);
            esp_ota.save_cfg();
            espmem.stack_mon();
        }
        else
        {
            response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Json bad syntax", false);
            return;
        }

        char *msg = (char *)esp_zalloc(85 +
                                       16 +
                                       6 +
                                       os_strlen(esp_ota.get_path()) +
                                       os_strlen(esp_ota.get_check_version()) +
                                       os_strlen(esp_ota.get_reboot_on_completion()));
        if (msg)
        {
            os_sprintf(msg,
                       "{\"host\":\"%s\",\"port\":%d,\"path\":\"%s\",\"check_version\":\"%s\",\"reboot_on_completion\":\"%s\"}",
                       esp_ota.get_host(),
                       esp_ota.get_port(),
                       esp_ota.get_path(),
                       esp_ota.get_check_version(),
                       esp_ota.get_reboot_on_completion());
            response(ptr_espconn, HTTP_CREATED, HTTP_CONTENT_JSON, msg, true);
            // esp_free(msg); // dont't free the msg buffer cause it could not have been used yet
        }
        else
        {
            esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", 64);
        }
        espmem.stack_mon();
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, "/api/ota/reboot")) && (parsed_req->req_method == HTTP_POST))
    {
        response(ptr_espconn, HTTP_ACCEPTED, HTTP_CONTENT_TEXT, "", false);
        espbot.reset(ESP_OTA_REBOOT);
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, "/api/ota/upgrade")) && (parsed_req->req_method == HTTP_POST))
    {
        response(ptr_espconn, HTTP_ACCEPTED, HTTP_CONTENT_TEXT, "", false);
        esp_ota.start_upgrade();
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, "/api/wifi/cfg")) && (parsed_req->req_method == HTTP_GET))
    {
        char *msg = (char *)esp_zalloc(64);
        if (msg)
        {
            os_sprintf(msg,
                       "{\"station_ssid\":\"%s\",\"station_pwd\":\"%s\"}",
                       espwifi.station_get_ssid(),
                       espwifi.station_get_password());
            response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
            // esp_free(msg); // dont't free the msg buffer cause it could not have been used yet
        }
        else
        {
            esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", 64);
        }
        return;
    }
    if ((os_strcmp(parsed_req->url, "/api/wifi/cfg") == 0) && (parsed_req->req_method == HTTP_POST))
    {
        Json_str wifi_cfg(parsed_req->req_content, parsed_req->content_len);
        if (wifi_cfg.syntax_check() == JSON_SINTAX_OK)
        {
            if (wifi_cfg.find_pair("station_ssid") != JSON_NEW_PAIR_FOUND)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Cannot find JSON string 'station_ssid'", false);
                return;
            }
            if (wifi_cfg.get_cur_pair_value_type() != JSON_STRING)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "JSON pair with string 'station_ssid' does not have a STRING value type", false);
                return;
            }
            char *tmp_ssid = wifi_cfg.get_cur_pair_value();
            int tmp_ssid_len = wifi_cfg.get_cur_pair_value_len();
            if (wifi_cfg.find_pair("station_pwd") != JSON_NEW_PAIR_FOUND)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Cannot find JSON string 'station_pwd'", false);
                return;
            }
            if (wifi_cfg.get_cur_pair_value_type() != JSON_STRING)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "JSON pair with string 'station_pwd' does not have an integer value type", false);
                return;
            }
            espwifi.station_set_ssid(tmp_ssid, tmp_ssid_len);
            espwifi.station_set_pwd(wifi_cfg.get_cur_pair_value(), wifi_cfg.get_cur_pair_value_len());
            espwifi.save_cfg();
            espwifi.connect();
            espmem.stack_mon();
        }
        else
        {
            response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Json bad syntax", false);
            return;
        }
        char *msg = (char *)esp_zalloc(140);
        if (msg)
        {
            os_sprintf(msg,
                       "{\"station_ssid\":\"%s\",\"station_pwd\":\"%s\"}",
                       espwifi.station_get_ssid(),
                       espwifi.station_get_password());
            response(ptr_espconn, HTTP_CREATED, HTTP_CONTENT_JSON, msg, true);
            // esp_free(msg); // dont't free the msg buffer cause it could not have been used yet
        }
        else
        {
            esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", 140);
        }
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, "/api/wifi/info")) && (parsed_req->req_method == HTTP_GET))
    {
        Heap_chunk msg(44 + 32 + 42, dont_free);
        if (msg.ref)
        {
            switch (espwifi.get_op_mode())
            {
            case STATION_MODE:
                os_sprintf(msg.ref, "{\"op_mode\":\"STATION\",\"SSID\":\"%s\",", espwifi.station_get_ssid());
                break;
            case SOFTAP_MODE:
                os_sprintf(msg.ref, "{\"op_mode\":\"AP\",");
                break;
            case STATIONAP_MODE:
                os_sprintf(msg.ref, "{\"op_mode\":\"AP\",");
                break;
            default:
                break;
            }
            char *ptr = msg.ref + os_strlen(msg.ref);
            struct ip_info tmp_ip;
            espwifi.get_ip_address(&tmp_ip);
            char *ip_ptr = (char *)&tmp_ip.ip.addr;
            os_sprintf(ptr, "\"ip_address\":\"%d.%d.%d.%d\"}", ip_ptr[0], ip_ptr[1], ip_ptr[2], ip_ptr[3]);
            response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
        }
        else
        {
            esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", 64);
        }
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, "/api/wifi/scan")) && (parsed_req->req_method == HTTP_GET))
    {
        // response(ptr_espconn, HTTP_OK, HTTP_CONTENT_TEXT, NULL);
        os_timer_disarm(&wifi_scan_timeout_timer);
        os_timer_setfn(&wifi_scan_timeout_timer, (os_timer_func_t *)wifi_scan_timeout_function, ptr_espconn);
        os_timer_arm(&wifi_scan_timeout_timer, 500, 0);
        espwifi.scan_for_ap();
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, "/api/wifi/connect")) && (parsed_req->req_method == HTTP_POST))
    {
        response(ptr_espconn, HTTP_ACCEPTED, HTTP_CONTENT_TEXT, "", false);
        Wifi::connect();
        return;
    }
    if ((0 == os_strcmp(parsed_req->url, "/api/wifi/disconnect")) && (parsed_req->req_method == HTTP_POST))
    {
        response(ptr_espconn, HTTP_ACCEPTED, HTTP_CONTENT_TEXT, "", false);
        Wifi::set_stationap();
        return;
    }
    //
    // now the custom app routes
    //
    if (app_http_routes(ptr_espconn, parsed_req))
        return;
    else
        response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "I'm sorry, my responses are limited. You must ask the right question.", false);
}