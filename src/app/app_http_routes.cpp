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
#include "user_interface.h"
}

#include "app.hpp"
#include "app_event_codes.h"
#include "app_http_routes.hpp"
#include "app_test.hpp"
#include "espbot.hpp"
#include "espbot_diagnostic.hpp"
#include "espbot_global.hpp"
#include "espbot_http.hpp"
#include "espbot_json.hpp"
#include "espbot_mem_mon.hpp"
#include "espbot_utils.hpp"
#include "espbot_webserver.hpp"
#include "library.hpp"

static void get_api_info(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    // {"device_name":"","chip_id":"","app_name":"","app_version":"","espbot_version":"","library_version":"","sdk_version":"","boot_version":""}
    ALL("get_api_info");
    int str_len = 138 +
                  os_strlen(espbot.get_name()) +
                  10 +
                  os_strlen(app_name) +
                  os_strlen(app_release) +
                  os_strlen(espbot.get_version()) +
                  os_strlen(library_release) +
                  os_strlen(system_get_sdk_version()) +
                  10 +
                  1;
    Heap_chunk msg(138 + str_len, dont_free);
    if (msg.ref == NULL)
    {
        esp_diag.error(APP_GET_API_INFO_HEAP_EXHAUSTED, 155 + str_len);
        ERROR("get_api_info heap exhausted %d", 155 + str_len);
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
        return;
    }
    fs_sprintf(msg.ref,
               "{\"device_name\":\"%s\",\"chip_id\":\"%d\",\"app_name\":\"%s\",",
               espbot.get_name(),
               system_get_chip_id(),
               app_name);
    fs_sprintf((msg.ref + os_strlen(msg.ref)),
               "\"app_version\":\"%s\",\"espbot_version\":\"%s\",",
               app_release,
               espbot.get_version());
    fs_sprintf(msg.ref + os_strlen(msg.ref),
               "\"api_version\":\"%s\",\"library_version\":\"%s\",",
               f_str(API_RELEASE),
               library_release);
    fs_sprintf(msg.ref + os_strlen(msg.ref),
               "\"sdk_version\":\"%s\",\"boot_version\":\"%d\"}",
               system_get_sdk_version(),
               system_get_boot_version());
    http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
}

#ifdef TEST_FUNCTIONS

static void runTest(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("runTest");
    int32 test_number;
    int32 test_param;
    Json_str test_cfg(parsed_req->req_content, parsed_req->content_len);
    if (test_cfg.syntax_check() != JSON_SINTAX_OK)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Json bad syntax"), false);
        return;
    }
    if (test_cfg.find_pair(f_str("test_number")) != JSON_NEW_PAIR_FOUND)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Cannot find JSON string 'test_number'"), false);
        return;
    }
    if (test_cfg.get_cur_pair_value_type() != JSON_INTEGER)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("JSON pair with string 'test_number' does not have a INTEGER value type"), false);
        return;
    }
    test_number = atoi(test_cfg.get_cur_pair_value());
    if (test_cfg.find_pair(f_str("test_param")) != JSON_NEW_PAIR_FOUND)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Cannot find JSON string 'test_param'"), false);
        return;
    }
    if (test_cfg.get_cur_pair_value_type() != JSON_INTEGER)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("JSON pair with string 'test_param' does not have a INTEGER value type"), false);
        return;
    }
    test_param = atoi(test_cfg.get_cur_pair_value());
    espmem.stack_mon();
    // {"test_number":4294967295,"test_param":4294967295}
    Heap_chunk msg(64, dont_free);
    if (msg.ref == NULL)
    {
        esp_diag.error(APP_RUNTEST_HEAP_EXHAUSTED, 36);
        ERROR("runTest heap exhausted %d", 36);
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
        return;
    }
    fs_sprintf(msg.ref,
               "{\"test_number\":%d,\"test_param\":%d}",
               test_number,
               test_param);
    http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg.ref, true);
    run_test(test_number, test_param);
}

#endif

bool app_http_routes(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("app_http_routes");

    if ((0 == os_strcmp(parsed_req->url, f_str("/api/info"))) && (parsed_req->req_method == HTTP_GET))
    {
        get_api_info(ptr_espconn, parsed_req);
        return true;
    }
#ifdef TEST_FUNCTIONS
    if ((0 == os_strcmp(parsed_req->url, f_str("/api/test"))) && (parsed_req->req_method == HTTP_POST))
    {
        runTest(ptr_espconn, parsed_req);
        return true;
    }
#endif
    return false;
}