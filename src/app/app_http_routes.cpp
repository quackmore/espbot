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
#include "library.h"
}

#include "espbot_webserver.hpp"
#include "app_http_routes.hpp"
#include "espbot.hpp"
#include "espbot_global.hpp"
#include "espbot_logger.hpp"
#include "espbot_json.hpp"
#include "espbot_utils.hpp"
#include "espbot_debug.hpp"
#include "app.hpp"
#include "app_test.hpp"

bool ICACHE_FLASH_ATTR app_http_routes(struct espconn *ptr_espconn, Html_parsed_req *parsed_req)
{
    esplog.all("app_http_routes\n");

    if ((0 == os_strcmp(parsed_req->url, "/api/info")) && (parsed_req->req_method == HTTP_GET))
    {
        int str_len = os_strlen(app_name) + 
                      os_strlen(app_release) + 
                      os_strlen(espbot.get_name()) + 
                      os_strlen(espbot.get_version()) + 
                      os_strlen(library_release) + 
                      10 + 
                      os_strlen(system_get_sdk_version()) + 
                      10;
        Heap_chunk msg(155 + str_len);
        if (msg.ref)
        {
            os_sprintf(msg.ref, "{\"app_name\":\"%s\","
                            "\"app_version\":\"%s\","
                            "\"espbot_name\":\"%s\","
                            "\"espbot_version\":\"%s\","
                            "\"library_version\":\"%s\","
                            "\"chip_id\":\"%d\","
                            "\"sdk_version\":\"%s\","
                            "\"boot_version\":\"%d\"}",
                       app_name,
                       app_release,
                       espbot.get_name(),
                       espbot.get_version(),
                       library_release,
                       system_get_chip_id(),
                       system_get_sdk_version(),
                       system_get_boot_version());
            response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
            // esp_free(msg); // dont't free the msg buffer cause it could not have been used yet
        }
        else
        {
            esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", 350);
        }
        return true;
    }
    if ((0 == os_strcmp(parsed_req->url, "/api/test")) && (parsed_req->req_method == HTTP_POST))
    {
        int test_number;
        Json_str test_cfg(parsed_req->req_content, parsed_req->content_len);
        if (test_cfg.syntax_check() == JSON_SINTAX_OK)
        {
            if (test_cfg.find_pair("test_number") != JSON_NEW_PAIR_FOUND)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Cannot find JSON string 'test_number'", false);
                return true;
            }
            if (test_cfg.get_cur_pair_value_type() != JSON_INTEGER)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "JSON pair with string 'test_number' does not have a INTEGER value type", false);
                return true;
            }
            Heap_chunk tmp_test_number(test_cfg.get_cur_pair_value_len());
            if (tmp_test_number.ref == NULL)
            {
                esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", test_cfg.get_cur_pair_value_len() + 1);
                response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, "not enough heap memory", false);
                return true;
            }
            os_strncpy(tmp_test_number.ref, test_cfg.get_cur_pair_value(), test_cfg.get_cur_pair_value_len());
            test_number = atoi(tmp_test_number.ref);
            espmem.stack_mon();
        }
        else
        {
            response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Json bad syntax", false);
            return true;
        }
        Heap_chunk msg(36, dont_free);
        if (msg.ref)
        {
            os_sprintf(msg.ref, "{\"test_number\": %d}", test_number);
            response(ptr_espconn, HTTP_OK, HTTP_CONTENT_TEXT, msg.ref, true);
            // esp_free(msg); // dont't free the msg buffer cause it could not have been used yet
            run_test(test_number);
        }
        else
        {
            esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", 36);
        }
        return true;
    }
    if ((0 == os_strcmp(parsed_req->url, "/api/testresponse")) && (parsed_req->req_method == HTTP_GET))
    {
        espwebsvr.set_response_max_size(1024);
        Heap_chunk message(512);
        if (message.ref)
        {
            int idx;
            char *ptr = message.ref;
            for (idx = 0; idx < 512; idx++)
            {
                *ptr++ = '0' + (idx % 10);
            }
        }
        else
        {
            esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", 512);
        }
        struct http_header header;
        header.code = HTTP_OK;
        header.content_type = HTTP_CONTENT_TEXT;
        header.content_length = os_strlen(message.ref);
        header.content_range_start = 0;
        header.content_range_end = 0;
        header.content_range_total = 0;
        char *header_str = format_header(&header);

        Heap_chunk msg(512 + os_strlen(header_str) + 1);
        if (msg.ref)
        {
            // copy the header
            os_strncpy(msg.ref, header_str, os_strlen(header_str));
            // now the message
            os_strncpy((msg.ref + os_strlen(header_str)), message.ref, os_strlen(message.ref));

            struct http_response *p_res = (struct http_response *)esp_zalloc(sizeof(struct http_response));
            p_res->p_espconn = ptr_espconn;
            p_res->msg = msg.ref;
            esplog.trace("response: *p_espconn: %X\n"
                         "                 msg: %s\n",
                         p_res->p_espconn, p_res->msg);
            send_response(p_res);
        }
        else
        {
            esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", 36);
        }
        return true;
    }
    return false;
}
