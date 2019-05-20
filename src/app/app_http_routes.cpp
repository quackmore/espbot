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
#include "app_http_routes.hpp"
#include "espbot.hpp"
#include "espbot_global.hpp"
#include "espbot_logger.hpp"
#include "espbot_json.hpp"
#include "espbot_utils.hpp"
#include "espbot_debug.hpp"
#include "app.hpp"
#include "app_test.hpp"
#include "library.hpp"

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
        Heap_chunk msg(155 + str_len, dont_free);
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
            esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", 155 + str_len);
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
        Http_header header;
        header.m_code = HTTP_OK;
        header.m_content_type = HTTP_CONTENT_TEXT;
        header.m_content_length = os_strlen(message.ref);
        header.m_content_range_start = 0;
        header.m_content_range_end = 0;
        header.m_content_range_total = 0;
        char *header_str = format_header(&header);

        Heap_chunk msg(512 + os_strlen(header_str) + 1, dont_free);
        if (msg.ref)
        {
            // copy the header
            os_strncpy(msg.ref, header_str, os_strlen(header_str));
            // now the message
            os_strncpy((msg.ref + os_strlen(header_str)), message.ref, os_strlen(message.ref));

            esplog.trace("calling send_response: *p_espconn: %X\n"
                         "                              msg: %s\n",
                         ptr_espconn, msg.ref);
            send_response(ptr_espconn, msg.ref);
        }
        else
        {
            esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", 36);
        }
        return true;
    }
    if ((0 == os_strcmp(parsed_req->url, "/api/testwifiscan")) && (parsed_req->req_method == HTTP_GET))
    {
        uint8 ssid[32];
        uint8 bssid[6];
        int ch;
        int show_hidden;   // 1 will show hidden channels
        int scan_type;     // 0 active
        int scan_min_time; // min 30 ms (juniper specs)
        int scan_max_time; // max 1500 ms

        Json_str test_cfg(parsed_req->req_content, parsed_req->content_len);
        if (test_cfg.syntax_check() == JSON_SINTAX_OK)
        {
            // ssid
            if (test_cfg.find_pair("ssid") != JSON_NEW_PAIR_FOUND)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Cannot find JSON string 'ssid'", false);
                return true;
            }
            if (test_cfg.get_cur_pair_value_type() != JSON_STRING)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "JSON pair with string 'ssid' does not have a STRING value type", false);
                return true;
            }
            Heap_chunk tmp_ssid(test_cfg.get_cur_pair_value_len());
            if (tmp_ssid.ref == NULL)
            {
                esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", test_cfg.get_cur_pair_value_len() + 1);
                response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, "not enough heap memory", false);
                return true;
            }
            os_strncpy(tmp_ssid.ref, test_cfg.get_cur_pair_value(), test_cfg.get_cur_pair_value_len());
            // bssid
            if (test_cfg.find_pair("bssid") != JSON_NEW_PAIR_FOUND)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Cannot find JSON string 'bssid'", false);
                return true;
            }
            if (test_cfg.get_cur_pair_value_type() != JSON_STRING)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "JSON pair with string 'bssid' does not have a STRING value type", false);
                return true;
            }
            Heap_chunk tmp_bssid(test_cfg.get_cur_pair_value_len());
            if (tmp_bssid.ref == NULL)
            {
                esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", test_cfg.get_cur_pair_value_len() + 1);
                response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, "not enough heap memory", false);
                return true;
            }
            os_strncpy(tmp_bssid.ref, test_cfg.get_cur_pair_value(), test_cfg.get_cur_pair_value_len());
            // ch
            if (test_cfg.find_pair("ch") != JSON_NEW_PAIR_FOUND)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Cannot find JSON string 'ch'", false);
                return true;
            }
            if (test_cfg.get_cur_pair_value_type() != JSON_INTEGER)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "JSON pair with string 'ch' does not have a INTEGER value type", false);
                return true;
            }
            Heap_chunk tmp_ch(test_cfg.get_cur_pair_value_len());
            if (tmp_ch.ref == NULL)
            {
                esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", test_cfg.get_cur_pair_value_len() + 1);
                response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, "not enough heap memory", false);
                return true;
            }
            os_strncpy(tmp_ch.ref, test_cfg.get_cur_pair_value(), test_cfg.get_cur_pair_value_len());
            // show_hidden
            if (test_cfg.find_pair("show_hidden") != JSON_NEW_PAIR_FOUND)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Cannot find JSON string 'show_hidden'", false);
                return true;
            }
            if (test_cfg.get_cur_pair_value_type() != JSON_INTEGER)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "JSON pair with string 'show_hidden' does not have a INTEGER value type", false);
                return true;
            }
            Heap_chunk tmp_show_hidden(test_cfg.get_cur_pair_value_len());
            if (tmp_show_hidden.ref == NULL)
            {
                esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", test_cfg.get_cur_pair_value_len() + 1);
                response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, "not enough heap memory", false);
                return true;
            }
            os_strncpy(tmp_show_hidden.ref, test_cfg.get_cur_pair_value(), test_cfg.get_cur_pair_value_len());
            // show_hidden
            if (test_cfg.find_pair("scan_type") != JSON_NEW_PAIR_FOUND)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Cannot find JSON string 'scan_type'", false);
                return true;
            }
            if (test_cfg.get_cur_pair_value_type() != JSON_INTEGER)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "JSON pair with string 'scan_type' does not have a INTEGER value type", false);
                return true;
            }
            Heap_chunk tmp_scan_type(test_cfg.get_cur_pair_value_len());
            if (tmp_scan_type.ref == NULL)
            {
                esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", test_cfg.get_cur_pair_value_len() + 1);
                response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, "not enough heap memory", false);
                return true;
            }
            os_strncpy(tmp_scan_type.ref, test_cfg.get_cur_pair_value(), test_cfg.get_cur_pair_value_len());
            // scan_min_time
            if (test_cfg.find_pair("scan_min_time") != JSON_NEW_PAIR_FOUND)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Cannot find JSON string 'scan_min_time'", false);
                return true;
            }
            if (test_cfg.get_cur_pair_value_type() != JSON_INTEGER)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "JSON pair with string 'scan_min_time' does not have a INTEGER value type", false);
                return true;
            }
            Heap_chunk tmp_scan_min_time(test_cfg.get_cur_pair_value_len());
            if (tmp_scan_min_time.ref == NULL)
            {
                esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", test_cfg.get_cur_pair_value_len() + 1);
                response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, "not enough heap memory", false);
                return true;
            }
            os_strncpy(tmp_scan_min_time.ref, test_cfg.get_cur_pair_value(), test_cfg.get_cur_pair_value_len());
            // scan_max_time
            if (test_cfg.find_pair("scan_max_time") != JSON_NEW_PAIR_FOUND)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Cannot find JSON string 'scan_max_time'", false);
                return true;
            }
            if (test_cfg.get_cur_pair_value_type() != JSON_INTEGER)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "JSON pair with string 'scan_max_time' does not have a INTEGER value type", false);
                return true;
            }
            Heap_chunk tmp_scan_max_time(test_cfg.get_cur_pair_value_len());
            if (tmp_scan_max_time.ref == NULL)
            {
                esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", test_cfg.get_cur_pair_value_len() + 1);
                response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, "not enough heap memory", false);
                return true;
            }
            os_strncpy(tmp_scan_max_time.ref, test_cfg.get_cur_pair_value(), test_cfg.get_cur_pair_value_len());
            // ----------------
            os_strncpy((char *)ssid, tmp_ssid.ref, 32);
            os_strncpy((char *)bssid, tmp_bssid.ref, 6);
            ch = atoi(tmp_ch.ref);
            show_hidden = atoi(tmp_show_hidden.ref);
            scan_type = atoi(tmp_scan_type.ref);
            scan_min_time = atoi(tmp_scan_min_time.ref);
            scan_max_time = atoi(tmp_scan_max_time.ref);
            espmem.stack_mon();
        }
        else
        {
            response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Json bad syntax", false);
            return true;
        }
        Heap_chunk msg(200, dont_free);
        if (msg.ref)
        {
            os_sprintf(msg.ref,
                       "{\"ssid\": \"%s\",\"bssid\": \"%s\",\"ch\": %d,\"show_hidden\": %d,\"scan_type\": %d,\"scan_min_time\": %d,\"scan_max_time\": %d}",
                       ssid,
                       bssid,
                       ch,
                       show_hidden,
                       scan_type,
                       scan_min_time,
                       scan_max_time);
            response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
            // esp_free(msg); // dont't free the msg buffer cause it could not have been used yet
            struct scan_config cfg;
            cfg.ssid = NULL;
            cfg.bssid = NULL;
            cfg.channel = ch;
            cfg.show_hidden = show_hidden;
            cfg.scan_type = (wifi_scan_type_t)scan_type;
            cfg.scan_time.active.min = scan_min_time;
            cfg.scan_time.active.max = scan_max_time;
            Wifi::scan_for_ap(&cfg, NULL, NULL);
        }
        else
        {
            esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", 200);
        }
        return true;
    }
    if ((0 == os_strcmp(parsed_req->url, "/api/testfastscan")) && (parsed_req->req_method == HTTP_GET))
    {
        response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, "", false);
        static char ch_list[] = {1, 6, 11};
        Wifi::fast_scan_for_best_ap("FASTWEB-13", ch_list, 3, NULL, NULL);
        return true;
    }
    return false;
}

// if ((0 == os_strcmp(parsed_req->url, "/api/wifi/scan")) && (parsed_req->req_method == HTTP_GET))
// {
//     Wifi::scan_for_ap(NULL, wifi_scan_completed_function, (void *)ptr_espconn);
//     return;
// }
