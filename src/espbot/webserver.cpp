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
#include "espbot_release.h"
#include "ip_addr.h"
}

#include "webserver.hpp"
#include "espbot.hpp"
#include "espbot_global.hpp"
#include "logger.hpp"
#include "json.hpp"
#include "espbot_utils.hpp"
#include "debug.hpp"
#include "espbot_test.hpp"

// HTTP status codes
#define HTTP_OK 200
#define HTTP_CREATED 201
#define HTTP_ACCEPTED 202
#define HTTP_BAD_REQUEST 400
#define HTTP_UNAUTHORIZED 401
#define HTTP_FORBIDDEN 403
#define HTTP_NOT_FOUND 404
#define HTTP_SERVER_ERROR 500

static char ICACHE_FLASH_ATTR *error_msg(int code)
{
    esplog.all("error_msg\n");
    switch (code)
    {
    case HTTP_OK:
        return "OK";
    case HTTP_BAD_REQUEST:
        return "Bad Request";
    case HTTP_UNAUTHORIZED:
        return "Unauthorized";
    case HTTP_FORBIDDEN:
        return "Forbidden";
    case HTTP_NOT_FOUND:
        return "Not Found";
    case HTTP_SERVER_ERROR:
        return "Internal Server Error";
    default:
        return "";
    }
}

#define HTTP_CONTENT_TEXT "text/html"
#define HTTP_CONTENT_JSON "application/json"

//
// HTTP responding:
// ----------------
// to make sure espconn_send is called after espconn_sent_callback of the previous packet
// a flag is set before calling espconn_send (will be reset by sendcb)
//
// befor sending a response the flag will be checked
// when the flag is found set (espconn_send not done yet)
// a timer will used for postponing response
//

#define DATA_SENT_TIMER_PERIOD 100
#define MAX_PENDING_RESPONSE_COUNT 8
static char *send_buffer;
static os_timer_t pending_response_timer[MAX_PENDING_RESPONSE_COUNT];
static char pending_response_timer_busy[MAX_PENDING_RESPONSE_COUNT];
static bool esp_busy_sending_data = false;

struct svr_response
{
    struct espconn *p_espconn;
    int code;
    char *content_type;
    char *msg;
    bool free_msg;
    char timer_idx;
};

static void ICACHE_FLASH_ATTR response(struct espconn *p_espconn, int code, char *content_type, char *msg, bool free_msg);

static void ICACHE_FLASH_ATTR webserver_pending_response(void *arg)
{
    esplog.all("webserver_pending_response\n");
    struct svr_response *response_data = (struct svr_response *)arg;
    // free pending response_timer
    pending_response_timer_busy[response_data->timer_idx] = 0;
    response(response_data->p_espconn, response_data->code, response_data->content_type, response_data->msg, response_data->free_msg);
    esp_free(response_data);
}

static void ICACHE_FLASH_ATTR webserver_sentcb(void *arg)
{
    esplog.all("webserver_sentcb\n");
    struct espconn *ptr_espconn = (struct espconn *)arg;
    esp_busy_sending_data = false;
    if (send_buffer)
    {
        esp_free(send_buffer);
        send_buffer = NULL;
    }
}

static void ICACHE_FLASH_ATTR response(struct espconn *p_espconn, int code, char *content_type, char *msg, bool free_msg)
{
    esplog.all("webserver::response\n");
    esplog.trace("response: *p_espconn: %X\n"
                 "                code: %d\n"
                 "        content-type: %s\n"
                 "                 msg: %s\n"
                 "            free_msg: %d\n",
                 p_espconn, code, content_type, msg, free_msg);
    if (esp_busy_sending_data) // previous espconn_send not completed yet
    {
        esplog.debug("Websvr::response - previous espconn_send not completed yet\n");
        struct svr_response *response_data = (struct svr_response *)esp_zalloc(sizeof(struct svr_response));
        espmem.stack_mon();
        if (response_data)
        {
            // look for a free pending_response_timer
            int timer_idx;
            for (timer_idx = 0; timer_idx < MAX_PENDING_RESPONSE_COUNT; timer_idx++)
                if (pending_response_timer_busy[timer_idx] == 0)
                {
                    pending_response_timer_busy[timer_idx] = 1;
                    break;
                }
            // check if there's a timer available
            if (timer_idx >= MAX_PENDING_RESPONSE_COUNT)
                esplog.error("Websvr::response: no pending_response timers available\n");
            else
            {
                response_data->p_espconn = p_espconn;
                response_data->code = code;
                response_data->content_type = content_type;
                response_data->msg = msg;
                response_data->free_msg = free_msg;
                response_data->timer_idx = timer_idx;
                os_timer_disarm(&pending_response_timer[timer_idx]);
                os_timer_setfn(&pending_response_timer[timer_idx], (os_timer_func_t *)webserver_pending_response, (void *)response_data);
                os_timer_arm(&pending_response_timer[timer_idx], DATA_SENT_TIMER_PERIOD, 0);
                esplog.all("Websvr::response - waiting for espconn_send to complete\n");
            }
        }
        else
        {
            esplog.error("Websvr::response: not enough heap memory (%d)\n", sizeof(struct svr_response));
        }
    }
    else // previous espconn_send completed
    {
        esp_busy_sending_data = true;
        if (code >= HTTP_BAD_REQUEST) // format error msg as json
        {
            char *err_msg = (char *)esp_zalloc(79 + os_strlen(msg));
            if (err_msg)
            {
                os_sprintf(err_msg,
                           "{\"error\":{\"code\": %d,\"message\": \"%s\",\"reason\": \"%s\"}}",
                           code, error_msg(code), msg);
                // replace original msg with the formatted one
                if (free_msg)
                    esp_free(msg);
                msg = err_msg;
                free_msg = true;
            }
            else
            {
                esplog.error("websvr::response: not enough heap memory (%d)\n", (171 + 25 + os_strlen(msg)));
                // there will be no response so clear the flag
                esp_busy_sending_data = false;
                return;
            }
        }
        send_buffer = (char *)esp_zalloc(184 + os_strlen(msg)); // header + code + max error msg + max content type + content len
        if (send_buffer)
        {
            os_sprintf(send_buffer, "HTTP/1.0 %d %s\r\n"
                                    "Server: espbot/1.0.0\r\n"
                                    "Content-Type: %s\r\n"
                                    "Content-Length: %d\r\n"
                                    "Date: Wed, 28 Nov 2018 12:00:00 GMT\r\n"
                                    "Pragma: no-cache\r\n\r\n%s",
                       code, error_msg(code), content_type, os_strlen(msg), msg);
            if (free_msg)
                esp_free(msg); // free the msg buffer now that it has been used
            sint8 res = espconn_send(p_espconn, (uint8 *)send_buffer, os_strlen(send_buffer));
            espmem.stack_mon();
            if (res)
            {
                esplog.error("websvr::response: error sending response, error code %d\n", res);
                // on error don't count on sentcb to be called
                esp_busy_sending_data = false;
                esp_free(send_buffer);
                send_buffer = NULL;
            }
            // esp_free(send_buffer); // webserver_sentcb will free it
        }
        else
        {
            esplog.error("websvr::response: not enough heap memory (%d)\n", (171 + 25 + os_strlen(msg)));
            // there will be no response so clear the flag
            esp_busy_sending_data = false;
        }
    }
}

// end of HTTP responding

//
// WEB API features that require timing
// + File system format
// + Wifi scan for APs
//

static os_timer_t format_delay_timer;

static void ICACHE_FLASH_ATTR format_function(void)
{
    esplog.all("webserver::format_function\n");
    espfs.format();
}

static os_timer_t wifi_scan_timeout_timer;

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

// End of WEB API features that require timing

//
// HTTP Receiving:
// Managing:
// + File requests
// + WEB API
//

typedef enum
{
    HTTP_GET = 0,
    HTTP_POST,
    HTTP_PUT,
    HTTP_PATCH,
    HTTP_DELETE,
    HTTP_UNDEFINED
} Html_methods;

class Html_parsed_req
{
  public:
    Html_parsed_req();
    ~Html_parsed_req();
    bool no_header_message; // tells if HTTP request contains only POST content
                            // (a POST msg was splitted into two different messages
                            // the first with the header the second with the content
                            // e.g. like Safari browser does)
    Html_methods req_method;
    char *url;
    int content_len;
    char *req_content;
};

ICACHE_FLASH_ATTR Html_parsed_req::Html_parsed_req()
{
    esplog.all("Html_parsed_req::Html_parsed_req\n");
    no_header_message = false;
    req_method = HTTP_UNDEFINED;
    url = NULL;
    content_len = 0;
    req_content = NULL;
}

ICACHE_FLASH_ATTR Html_parsed_req::~Html_parsed_req()
{
    esplog.all("Html_parsed_req::~Html_parsed_req\n");
    if (url)
        esp_free(url);
    if (req_content)
        esp_free(req_content);
}

static void ICACHE_FLASH_ATTR parse_http_request(char *req, Html_parsed_req *parsed_req)
{
    esplog.all("webserver::parse_http_request\n");
    char *tmp_ptr = req;
    char *end_ptr = NULL;
    char *tmp_str = NULL;
    espmem.stack_mon();
    int len = 0;

    if (tmp_ptr == NULL)
    {
        esplog.error("websvr::parse_http_request - cannot parse empty message\n");
        return;
    }

    if (os_strncmp(tmp_ptr, "GET ", 4) == 0)
    {
        parsed_req->req_method = HTTP_GET;
        tmp_ptr += 4;
    }
    else if (os_strncmp(tmp_ptr, "POST ", 5) == 0)
    {
        parsed_req->req_method = HTTP_POST;
        tmp_ptr += 5;
    }
    else if (os_strncmp(tmp_ptr, "PUT ", 4) == 0)
    {
        parsed_req->req_method = HTTP_PUT;
        tmp_ptr += 4;
    }
    else if (os_strncmp(tmp_ptr, "PATCH ", 6) == 0)
    {
        parsed_req->req_method = HTTP_PATCH;
        tmp_ptr += 6;
    }
    else if (os_strncmp(tmp_ptr, "DELETE ", 7) == 0)
    {
        parsed_req->req_method = HTTP_DELETE;
        tmp_ptr += 7;
    }
    else
    {
        parsed_req->no_header_message = true;
    }

    if (parsed_req->no_header_message)
    {
        parsed_req->content_len = os_strlen(tmp_ptr);
        parsed_req->req_content = (char *)esp_zalloc(parsed_req->content_len + 1);
        if (parsed_req->req_content == NULL)
        {
            esplog.error("websvr::parse_http_request - not enough heap memory\n");
            return;
        }
        os_memcpy(parsed_req->req_content, tmp_ptr, parsed_req->content_len);
        return;
    }

    // this is a standard request with header

    // checkout url
    end_ptr = (char *)os_strstr(tmp_ptr, " HTTP");
    if (end_ptr == NULL)
    {
        esplog.error("websvr::parse_http_request - cannot find HTTP token\n");
        return;
    }
    len = end_ptr - tmp_ptr;
    parsed_req->url = (char *)esp_zalloc(len + 1);
    if (parsed_req->url == NULL)
    {
        esplog.error("websvr::parse_http_request - not enough heap memory\n");
        return;
    }
    os_memcpy(parsed_req->url, tmp_ptr, len);

    // checkout Content-Length
    tmp_ptr = (char *)os_strstr(tmp_ptr, "Content-Length: ");
    if (tmp_ptr == NULL)
    {
        tmp_ptr = req;
        tmp_ptr = (char *)os_strstr(tmp_ptr, "content-length: ");
        if (tmp_ptr == NULL)
        {
            esplog.trace("websvr::parse_http_request - didn't find any Content-Length\n");
            return;
        }
    }
    tmp_ptr += 16;
    end_ptr = (char *)os_strstr(tmp_ptr, "\r\n");
    if (end_ptr == NULL)
    {
        esplog.error("websvr::parse_http_request - cannot find Content-Length value\n");
        return;
    }
    len = end_ptr - tmp_ptr;
    tmp_str = (char *)esp_zalloc(len + 1);
    if (tmp_str == NULL)
    {
        esplog.error("websvr::parse_http_request - not enough heap memory\n");
        return;
    }
    parsed_req->content_len = atoi(tmp_str);
    esp_free(tmp_str);

    // checkout for request content
    tmp_ptr = (char *)os_strstr(tmp_ptr, "\r\n\r\n");
    if (tmp_ptr == NULL)
    {
        esplog.error("websvr::parse_http_request - cannot find Content start\n");
        return;
    }
    tmp_ptr += 4;
    parsed_req->content_len = os_strlen(tmp_ptr);
    parsed_req->req_content = (char *)esp_zalloc(parsed_req->content_len + 1);
    if (parsed_req->req_content == NULL)
    {
        esplog.error("websvr::parse_http_request - not enough heap memory\n");
        return;
    }
    os_memcpy(parsed_req->req_content, tmp_ptr, parsed_req->content_len);
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

static void ICACHE_FLASH_ATTR return_file(struct espconn *p_espconn, char *filename)
{
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
            char *file_content = (char *)esp_zalloc(file_size + 1);
            if (file_content)
            {
                sel_file.n_read(file_content, file_size);

                response(p_espconn, HTTP_OK, get_file_mime_type(filename), file_content, true);
                // esp_free(file_content); // dont't free the msg buffer cause it could not have been used yet
            }
            else
            {
                esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", file_size + 1);
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

static void ICACHE_FLASH_ATTR webserver_recv(void *arg, char *precdata, unsigned short length)
{
    esplog.all("webserver_recv\n");
    struct espconn *ptr_espconn = (struct espconn *)arg;
    espmem.stack_mon();
    Html_parsed_req parsed_req;

    esplog.trace("Websvr::webserver_recv received request len:%u\n", length);
    esplog.trace("Websvr::webserver_recv received request:\n%s\n", precdata);

    parse_http_request(precdata, &parsed_req);

    system_soft_wdt_feed();

    esplog.trace("Websvr::webserver_recv parsed request:\n"
                 "->                  no_header_message: %d\n"
                 "->                             method: %d\n"
                 "->                                url: %s\n"
                 "->                        content len: %d\n"
                 "->                            content: %s\n",
                 parsed_req.no_header_message,
                 parsed_req.req_method,
                 parsed_req.url,
                 parsed_req.content_len,
                 parsed_req.req_content);

    if (parsed_req.no_header_message || (parsed_req.url == NULL))
    {
        esplog.debug("Websvr::webserver_recv - No header message or empty url\n");
        return;
    }
    if ((0 == os_strcmp(parsed_req.url, "/")) && (parsed_req.req_method == HTTP_GET)) // home
    {
        // home: look for index.html
        char *file_name = "index.html";
        return_file(ptr_espconn, file_name);
        return;
    }
    if ((os_strncmp(parsed_req.url, "/api/", 5)) && (parsed_req.req_method == HTTP_GET)) // file
    {
        // not an api: look for specified file
        char *file_name = parsed_req.url + os_strlen("/");
        return_file(ptr_espconn, file_name);
        return;
    }
    if ((0 == os_strcmp(parsed_req.url, "/api/debug/log")) && (parsed_req.req_method == HTTP_GET))
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
    if ((0 == os_strcmp(parsed_req.url, "/api/debug/hexmemdump")) && (parsed_req.req_method == HTTP_GET))
    {
        char *address;
        int length;
        Json_str debug_cfg(parsed_req.req_content, parsed_req.content_len);
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
            String address_hex_str(debug_cfg.get_cur_pair_value_len() + 1);
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
            String length_str(debug_cfg.get_cur_pair_value_len() + 1);
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
        String msg(48 + length * 3, 0);
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
    if ((0 == os_strcmp(parsed_req.url, "/api/debug/memdump")) && (parsed_req.req_method == HTTP_GET))
    {
        char *address;
        int length;
        Json_str debug_cfg(parsed_req.req_content, parsed_req.content_len);
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
            String address_hex_str(debug_cfg.get_cur_pair_value_len() + 1);
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
            String length_str(debug_cfg.get_cur_pair_value_len() + 1);
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
        String msg(48 + length, 0);
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
    if ((0 == os_strcmp(parsed_req.url, "/api/debug/meminfo")) && (parsed_req.req_method == HTTP_GET))
    {
        // count the heap items
        int heap_item_count = 0;
        struct heap_item *heap_obj_ptr = espmem.next_heap_item(0);
        while (heap_obj_ptr)
        {
            heap_item_count++;
            heap_obj_ptr = espmem.next_heap_item(1);
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
            heap_obj_ptr = espmem.next_heap_item(0);
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

                heap_obj_ptr = espmem.next_heap_item(1);
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
    if ((0 == os_strcmp(parsed_req.url, "/api/debug/cfg")) && (parsed_req.req_method == HTTP_GET))
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
    if ((os_strcmp(parsed_req.url, "/api/debug/cfg") == 0) && (parsed_req.req_method == HTTP_POST))
    {
        Json_str debug_cfg(parsed_req.req_content, parsed_req.content_len);
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
    if ((0 == os_strcmp(parsed_req.url, "/api/espbot/info")) && (parsed_req.req_method == HTTP_GET))
    {
        char *msg = (char *)esp_zalloc(350);
        if (msg)
        {
            os_sprintf(msg, "{\"espbot_name\":\"%s\","
                            "\"espbot_alias\":\"%s\","
                            "\"espbot_version\":\"%s\","
                            "\"chip_id\":\"%d\","
                            "\"sdk_version\":\"%s\","
                            "\"boot_version\":\"%d\"}",
                       espbot.get_name(),
                       espbot.get_alias(),
                       ESPBOT_RELEASE,
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
    if ((0 == os_strcmp(parsed_req.url, "/api/espbot/cfg")) && (parsed_req.req_method == HTTP_GET))
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
    if ((os_strcmp(parsed_req.url, "/api/espbot/cfg") == 0) && (parsed_req.req_method == HTTP_POST))
    {
        Json_str espbot_cfg(parsed_req.req_content, parsed_req.content_len);
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
    if ((0 == os_strcmp(parsed_req.url, "/api/espbot/reset")) && (parsed_req.req_method == HTTP_POST))
    {
        response(ptr_espconn, HTTP_ACCEPTED, HTTP_CONTENT_TEXT, "", false);
        espbot.reset(ESP_REBOOT);
        return;
    }
    if ((0 == os_strcmp(parsed_req.url, "/api/fs/info")) && (parsed_req.req_method == HTTP_GET))
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
    if ((0 == os_strcmp(parsed_req.url, "/api/fs/format")) && (parsed_req.req_method == HTTP_POST))
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
    if ((0 == os_strcmp(parsed_req.url, "/api/files/ls")) && (parsed_req.req_method == HTTP_GET))
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
    if ((0 == os_strncmp(parsed_req.url, "/api/files/cat/", os_strlen("/api/files/cat/"))) && (parsed_req.req_method == HTTP_GET))
    {
        char *file_name = parsed_req.url + os_strlen("/api/files/cat/");
        if (os_strlen(file_name) == 0)
        {
            response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "No file name provided", false);
            return;
        }
        return_file(ptr_espconn, file_name);
        return;
    }
    if ((0 == os_strncmp(parsed_req.url, "/api/files/delete/", os_strlen("/api/files/delete/"))) && (parsed_req.req_method == HTTP_POST))
    {
        char *file_name = parsed_req.url + os_strlen("/api/files/delete/");
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
    if ((0 == os_strncmp(parsed_req.url, "/api/files/create/", os_strlen("/api/files/create/"))) && (parsed_req.req_method == HTTP_POST))
    {
        char *file_name = parsed_req.url + os_strlen("/api/files/create/");
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
                sel_file.n_append(parsed_req.req_content, parsed_req.content_len);
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
    if ((0 == os_strncmp(parsed_req.url, "/api/files/append/", os_strlen("/api/files/append/"))) && (parsed_req.req_method == HTTP_POST))
    {
        char *file_name = parsed_req.url + os_strlen("/api/files/append/");
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
                sel_file.n_append(parsed_req.req_content, parsed_req.content_len);
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
    if ((os_strcmp(parsed_req.url, "/api/gpio/cfg") == 0) && (parsed_req.req_method == HTTP_POST))
    {
        int gpio_pin;
        int gpio_type;
        Json_str gpio_cfg(parsed_req.req_content, parsed_req.content_len);
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
            String tmp_pin(gpio_cfg.get_cur_pair_value_len());
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
            String tmp_type(gpio_cfg.get_cur_pair_value_len());
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

        String msg(parsed_req.content_len, 0);
        if (msg.ref)
        {
            os_strncpy(msg.ref, parsed_req.req_content, parsed_req.content_len);
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
    if ((os_strcmp(parsed_req.url, "/api/gpio/uncfg") == 0) && (parsed_req.req_method == HTTP_POST))
    {
        int gpio_pin;
        int gpio_type;
        Json_str gpio_cfg(parsed_req.req_content, parsed_req.content_len);
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
            String tmp_pin(gpio_cfg.get_cur_pair_value_len());
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

        String msg(48, 0);
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
    if ((os_strcmp(parsed_req.url, "/api/gpio/cfg") == 0) && (parsed_req.req_method == HTTP_GET))
    {
        int gpio_pin;
        int result;
        Json_str gpio_cfg(parsed_req.req_content, parsed_req.content_len);
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
            String tmp_pin(gpio_cfg.get_cur_pair_value_len());
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

        String msg(48, 0);
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
    if ((os_strcmp(parsed_req.url, "/api/gpio/read") == 0) && (parsed_req.req_method == HTTP_GET))
    {
        int gpio_pin;
        int result;
        Json_str gpio_cfg(parsed_req.req_content, parsed_req.content_len);
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
            String tmp_pin(gpio_cfg.get_cur_pair_value_len());
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

        String msg(48, 0);
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
    if ((os_strcmp(parsed_req.url, "/api/gpio/set") == 0) && (parsed_req.req_method == HTTP_POST))
    {
        int gpio_pin;
        int output_level;
        int result;
        Json_str gpio_cfg(parsed_req.req_content, parsed_req.content_len);
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
            String tmp_pin(gpio_cfg.get_cur_pair_value_len());
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
            String tmp_level(gpio_cfg.get_cur_pair_value_len());
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

        String msg(48, 0);
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
                os_strncpy(msg.ref, parsed_req.req_content, parsed_req.content_len);
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
    if ((0 == os_strcmp(parsed_req.url, "/api/ota/info")) && (parsed_req.req_method == HTTP_GET))
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
    if ((0 == os_strcmp(parsed_req.url, "/api/ota/cfg")) && (parsed_req.req_method == HTTP_GET))
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
    if ((os_strcmp(parsed_req.url, "/api/ota/cfg") == 0) && (parsed_req.req_method == HTTP_POST))
    {
        Json_str ota_cfg(parsed_req.req_content, parsed_req.content_len);
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
            String tmp_host(ota_cfg.get_cur_pair_value_len());
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
            String tmp_port(ota_cfg.get_cur_pair_value_len());
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
            String tmp_path(ota_cfg.get_cur_pair_value_len());
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
            String tmp_check_version(ota_cfg.get_cur_pair_value_len());
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
            String tmp_reboot_on_completion(ota_cfg.get_cur_pair_value_len());
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
    if ((0 == os_strcmp(parsed_req.url, "/api/ota/reboot")) && (parsed_req.req_method == HTTP_POST))
    {
        response(ptr_espconn, HTTP_ACCEPTED, HTTP_CONTENT_TEXT, "", false);
        espbot.reset(ESP_OTA_REBOOT);
        return;
    }
    if ((0 == os_strcmp(parsed_req.url, "/api/ota/upgrade")) && (parsed_req.req_method == HTTP_POST))
    {
        response(ptr_espconn, HTTP_ACCEPTED, HTTP_CONTENT_TEXT, "", false);
        esp_ota.start_upgrade();
        return;
    }
    if ((0 == os_strcmp(parsed_req.url, "/api/wifi/cfg")) && (parsed_req.req_method == HTTP_GET))
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
    if ((os_strcmp(parsed_req.url, "/api/wifi/cfg") == 0) && (parsed_req.req_method == HTTP_POST))
    {
        Json_str wifi_cfg(parsed_req.req_content, parsed_req.content_len);
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
    if ((0 == os_strcmp(parsed_req.url, "/api/wifi/info")) && (parsed_req.req_method == HTTP_GET))
    {
        String msg(44 + 32 + 42, 0);
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
    if ((0 == os_strcmp(parsed_req.url, "/api/wifi/scan")) && (parsed_req.req_method == HTTP_GET))
    {
        // response(ptr_espconn, HTTP_OK, HTTP_CONTENT_TEXT, NULL);
        os_timer_disarm(&wifi_scan_timeout_timer);
        os_timer_setfn(&wifi_scan_timeout_timer, (os_timer_func_t *)wifi_scan_timeout_function, ptr_espconn);
        os_timer_arm(&wifi_scan_timeout_timer, 500, 0);
        espwifi.scan_for_ap();
        return;
    }
    if ((0 == os_strcmp(parsed_req.url, "/api/wifi/connect")) && (parsed_req.req_method == HTTP_POST))
    {
        response(ptr_espconn, HTTP_ACCEPTED, HTTP_CONTENT_TEXT, "", false);
        Wifi::connect();
        return;
    }
    if ((0 == os_strcmp(parsed_req.url, "/api/wifi/disconnect")) && (parsed_req.req_method == HTTP_POST))
    {
        response(ptr_espconn, HTTP_ACCEPTED, HTTP_CONTENT_TEXT, "", false);
        Wifi::switch_to_stationap();
        return;
    }
    if ((0 == os_strcmp(parsed_req.url, "/api/test")) && (parsed_req.req_method == HTTP_POST))
    {
        int test_number;
        Json_str test_cfg(parsed_req.req_content, parsed_req.content_len);
        if (test_cfg.syntax_check() == JSON_SINTAX_OK)
        {
            if (test_cfg.find_pair("test_number") != JSON_NEW_PAIR_FOUND)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Cannot find JSON string 'test_number'", false);
                return;
            }
            if (test_cfg.get_cur_pair_value_type() != JSON_INTEGER)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "JSON pair with string 'test_number' does not have a INTEGER value type", false);
                return;
            }
            String tmp_test_number(test_cfg.get_cur_pair_value_len());
            if (tmp_test_number.ref == NULL)
            {
                esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", test_cfg.get_cur_pair_value_len() + 1);
                response(ptr_espconn, HTTP_SERVER_ERROR, HTTP_CONTENT_JSON, "not enough heap memory", false);
                return;
            }
            os_strncpy(tmp_test_number.ref, test_cfg.get_cur_pair_value(), test_cfg.get_cur_pair_value_len());
            test_number = atoi(tmp_test_number.ref);
            espmem.stack_mon();
        }
        else
        {
            response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Json bad syntax", false);
            return;
        }
        String msg(36, 0);
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
        return;
    }

    response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "I'm sorry, my responses are limited. You must ask the right question.", false);
}

static ICACHE_FLASH_ATTR void webserver_recon(void *arg, sint8 err)
{
    esplog.all("webserver_recon\n");
    struct espconn *pesp_conn = (struct espconn *)arg;
    espmem.stack_mon();
    esplog.debug("%d.%d.%d.%d:%d err %d reconnect\n", pesp_conn->proto.tcp->remote_ip[0],
                 pesp_conn->proto.tcp->remote_ip[1],
                 pesp_conn->proto.tcp->remote_ip[2],
                 pesp_conn->proto.tcp->remote_ip[3],
                 pesp_conn->proto.tcp->remote_port,
                 err);
}

static ICACHE_FLASH_ATTR void webserver_discon(void *arg)
{
    esplog.all("webserver_discon\n");
    struct espconn *pesp_conn = (struct espconn *)arg;
    espmem.stack_mon();
    esplog.debug("%d.%d.%d.%d:%d disconnect\n", pesp_conn->proto.tcp->remote_ip[0],
                 pesp_conn->proto.tcp->remote_ip[1],
                 pesp_conn->proto.tcp->remote_ip[2],
                 pesp_conn->proto.tcp->remote_ip[3],
                 pesp_conn->proto.tcp->remote_port);
}

static void ICACHE_FLASH_ATTR webserver_listen(void *arg)
{
    esplog.all("webserver_listen\n");
    struct espconn *pesp_conn = (struct espconn *)arg;
    espmem.stack_mon();
    espconn_regist_recvcb(pesp_conn, webserver_recv);
    espconn_regist_sentcb(pesp_conn, webserver_sentcb);
    espconn_regist_reconcb(pesp_conn, webserver_recon);
    espconn_regist_disconcb(pesp_conn, webserver_discon);
}

void ICACHE_FLASH_ATTR Websvr::start(uint32 port)
{
    esplog.all("Websvr::start\n");
    int idx;
    for (idx = 0; idx < MAX_PENDING_RESPONSE_COUNT; idx++)
    {
        os_timer_disarm(&pending_response_timer[idx]);
        pending_response_timer_busy[idx] = 0;
    }
    os_timer_disarm(&format_delay_timer);
    os_timer_disarm(&wifi_scan_timeout_timer);

    esp_conn.type = ESPCONN_TCP;
    esp_conn.state = ESPCONN_NONE;
    esp_conn.proto.tcp = &esptcp;
    esp_conn.proto.tcp->local_port = port;
    espconn_regist_connectcb(&esp_conn, webserver_listen);
    espconn_accept(&esp_conn);
    esplog.debug("web server started\n");
}

void ICACHE_FLASH_ATTR Websvr::stop()
{
    esplog.all("Websvr::stop\n");
    espconn_disconnect(&esp_conn);
    espconn_delete(&esp_conn);
    esplog.debug("web server stopped\n");
}