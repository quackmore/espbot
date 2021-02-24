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
#include "espbot_http.hpp"
#include "espbot_json.hpp"
#include "espbot_mem_mon.hpp"
#include "espbot_utils.hpp"
#include "espbot_http_server.hpp"

static void get_api_info(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("get_api_info");
    char *msg = app_info_json_stringify();
    if (msg)
        http_response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
    else
        http_response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, f_str("Heap exhausted"), false);
}

#ifdef TEST_FUNCTIONS

static void runTest(struct espconn *ptr_espconn, Http_parsed_req *parsed_req)
{
    ALL("runTest");
    int test_number;
    int test_param;
    JSONP test_cfg(parsed_req->req_content, parsed_req->content_len);
    test_cfg.getVal((char *)f_str("test_number"), test_number);
    test_cfg.getVal((char *)f_str("test_param"), test_param);
    if(test_cfg.getErr()  != JSON_noerr)
    {
        http_response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, f_str("Json bad syntax"), false);
        return;
    }

    espmem.stack_mon();
    // {"test_number":4294967295,"test_param":4294967295}
    Heap_chunk msg(64, dont_free);
    if (msg.ref == NULL)
    {
        dia_error_evnt(APP_RUNTEST_HEAP_EXHAUSTED, 36);
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