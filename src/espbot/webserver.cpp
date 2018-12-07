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
}

#include "webserver.hpp"
#include "espbot.hpp"
#include "espbot_global.hpp"
#include "debug.hpp"
#include "json.hpp"
#include "espbot_utils.hpp"

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

#define DATA_SENT_TIMER_PERIOD 500
static char *send_buffer;
static os_timer_t websvr_wait_for_data_sent;
static bool esp_busy_sending_data = false;

struct svr_response
{
    struct espconn *p_espconn;
    int code;
    char *content_type;
    char *msg;
    bool free_msg;
};

static void ICACHE_FLASH_ATTR response(struct espconn *p_espconn, int code, char *content_type, char *msg, bool free_msg);

static void ICACHE_FLASH_ATTR webserver_pending_response(void *arg)
{
    struct svr_response *response_data = (struct svr_response *)arg;
    response(response_data->p_espconn, response_data->code, response_data->content_type, response_data->msg, response_data->free_msg);
    os_free(response_data);
}

static void ICACHE_FLASH_ATTR webserver_sentcb(void *arg)
{
    struct espconn *ptr_espconn = (struct espconn *)arg;
    esp_busy_sending_data = false;
    if (send_buffer)
        os_free(send_buffer);
}

static void ICACHE_FLASH_ATTR response(struct espconn *p_espconn, int code, char *content_type, char *msg, bool free_msg)
{
    esplog.trace("response: *p_espconn: %X\n"
                 "                code: %d\n"
                 "        content-type: %s\n"
                 "                 msg: %s\n"
                 "            free_msg: %d\n",
                 p_espconn, code, content_type, msg, free_msg);
    if (esp_busy_sending_data) // previous espconn_send not completed yet
    {
        esplog.info("Websvr::response - previous espconn_send not completed yet\n");
        struct svr_response *response_data = (struct svr_response *)os_zalloc(sizeof(struct svr_response));
        if (response_data)
        {
            response_data->p_espconn = p_espconn;
            response_data->code = code;
            response_data->content_type = content_type;
            response_data->msg = msg;
            response_data->free_msg = free_msg;
            os_timer_setfn(&websvr_wait_for_data_sent, (os_timer_func_t *)webserver_pending_response, (void *)response_data);
            os_timer_arm(&websvr_wait_for_data_sent, DATA_SENT_TIMER_PERIOD, 0);
        }
        else
        {
            esplog.error("Websvr::response: not enough heap memory (%s)\n", sizeof(struct svr_response));
        }
    }
    else // previous espconn_send completed
    {
        esp_busy_sending_data = true;
        if (code >= HTTP_BAD_REQUEST) // format error msg as json
        {
            char *err_msg = (char *)os_zalloc(79 + os_strlen(msg));
            if (err_msg)
            {
                os_sprintf(err_msg,
                           "{\"error\":{\"code\": %d,\"message\": \"%s\",\"reason\": \"%s\"}}",
                           code, error_msg(code), msg);
                // replace original msg with the formatted one
                if (free_msg)
                    os_free(msg);
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
        send_buffer = (char *)os_zalloc(184 + os_strlen(msg)); // header + code + max error msg + max content type + content len
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
                os_free(msg); // free the msg buffer now that it has been used
            sint8 res = espconn_send(p_espconn, (uint8 *)send_buffer, os_strlen(send_buffer));
            if (res)
            {
                esplog.error("websvr::response: error sending response, error code %d\n", res);
                // on error don't count on sentcb to be called
                esp_busy_sending_data = false;
                os_free(send_buffer);
            }
            // os_free(send_buffer); // webserver_sentcb will free it
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
    espfs.format();
}

static os_timer_t wifi_scan_timeout_timer;

static void ICACHE_FLASH_ATTR wifi_scan_timeout_function(struct espconn *ptr_espconn)
{
    static int counter = 0;
    if (counter < 10) // wait for a scan completion at maximum for 5 seconds
    {
        if (espwifi.scan_for_ap_completed())
        {
            char *scan_list = (char *)os_zalloc(40 + ((32 + 6) * espwifi.get_ap_count()));
            if (scan_list)
            {
                char *tmp_ptr;
                os_sprintf(scan_list, "{\"AP_count\": %d,\"AP_SSIDss\":[", espwifi.get_ap_count());
                for (int idx = 0; idx < espwifi.get_ap_count(); idx++)
                {
                    tmp_ptr = scan_list + os_strlen(scan_list);
                    if (idx > 0)
                        *(tmp_ptr++) = ',';
                    os_sprintf(tmp_ptr, "\"%s\"", espwifi.get_ap_name(idx));
                }
                tmp_ptr = scan_list + os_strlen(scan_list);
                os_sprintf(tmp_ptr, "]}");
                response(ptr_espconn, HTTP_CREATED, HTTP_CONTENT_JSON, scan_list, true);
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
//  List of enabled routes:
//  [GET]   /
//  [GET]   /:filename
//  [GET]   /api/espbot/info
//  [GET]   /api/espbot/cfg
//  [POST]  /api/espbot/cfg
//  [POST]  /api/espbot/reset
//  [GET]   /api/fs/info
//  [POST]  /api/fs/format
//  [GET]   /api/files/ls
//  [GET]   /api/files/cat/:filename
//  [POST]  /api/files/delete/:filename
//  [POST]  /api/files/create/:filename
//  [GET]   /api/debug/info
//  [GET]   /api/debug/cfg
//  [POST]  /api/debug/cfg
//  [GET]   /api/wifi/cfg
//  [POST]  /api/wifi/cfg
//  [GET]   /api/wifi/scan
//  [POST]  /api/wifi/connect
//  [POST]  /api/wifi/disconnect
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
    no_header_message = false;
    req_method = HTTP_UNDEFINED;
    url = NULL;
    content_len = 0;
    req_content = NULL;
}

ICACHE_FLASH_ATTR Html_parsed_req::~Html_parsed_req()
{
    if (url)
        os_free(url);
    if (req_content)
        os_free(req_content);
}

static void ICACHE_FLASH_ATTR parse_http_request(char *req, Html_parsed_req *parsed_req)
{
    char *tmp_ptr = req;
    char *end_ptr = NULL;
    char *tmp_str = NULL;
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
        parsed_req->req_content = (char *)os_zalloc(parsed_req->content_len + 1);
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
    parsed_req->url = (char *)os_zalloc(len + 1);
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
    tmp_str = (char *)os_zalloc(len + 1);
    if (tmp_str == NULL)
    {
        esplog.error("websvr::parse_http_request - not enough heap memory\n");
        return;
    }
    parsed_req->content_len = atoi(tmp_str);
    os_free(tmp_str);

    // checkout for request content
    tmp_ptr = (char *)os_strstr(tmp_ptr, "\r\n\r\n");
    if (tmp_ptr == NULL)
    {
        esplog.error("websvr::parse_http_request - cannot find Content start\n");
        return;
    }
    tmp_ptr += 4;
    parsed_req->content_len = os_strlen(tmp_ptr);
    parsed_req->req_content = (char *)os_zalloc(parsed_req->content_len + 1);
    if (parsed_req->req_content == NULL)
    {
        esplog.error("websvr::parse_http_request - not enough heap memory\n");
        return;
    }
    os_memcpy(parsed_req->req_content, tmp_ptr, parsed_req->content_len);
}

static void ICACHE_FLASH_ATTR return_file(struct espconn *p_espconn, char *filename)
{
    if (espfs.is_available())
    {
        if (!Ffile::exists(&espfs, filename))
        {
            response(p_espconn, HTTP_NOT_FOUND, HTTP_CONTENT_JSON, "File not found", false);
            return;
        }
        int file_size = Ffile::size(&espfs, filename);
        Ffile sel_file(&espfs, filename);
        if (sel_file.is_available())
        {
            char *file_content = (char *)os_zalloc(file_size + 1);
            if (file_content)
            {
                sel_file.n_read(file_content, file_size);
                response(p_espconn, HTTP_OK, HTTP_CONTENT_TEXT, file_content, true);
                // os_free(file_content); // dont't free the msg buffer cause it could not have been used yet
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
    struct espconn *ptr_espconn = (struct espconn *)arg;
    Html_parsed_req parsed_req;

    esplog.trace("Websvr::webserver_recv received request len:%u\n", length);
    esplog.trace("Websvr::webserver_recv received request:\n%s\n", precdata);

    parse_http_request(precdata, &parsed_req);

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
        esplog.info("Websvr::webserver_recv - No header message or empty url\n");
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
    if ((0 == os_strcmp(parsed_req.url, "/api/espbot/info")) && (parsed_req.req_method == HTTP_GET))
    {
        char *msg = (char *)os_zalloc(256);
        if (msg)
        {
            os_sprintf(msg, "{\"espbot_name\":\"%s\","
                            "\"espbot_version\":\"%s\","
                            "\"chip_id\":\"%d\","
                            "\"sdk_version\":\"%s\","
                            "\"boot_version\":\"%d\"}",
                       espbot.get_name(),
                       ESPBOT_RELEASE,
                       system_get_chip_id(),
                       system_get_sdk_version(),
                       system_get_boot_version());
            response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
            // os_free(msg); // dont't free the msg buffer cause it could not have been used yet
        }
        else
        {
            esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", 256);
        }
        return;
    }
    if ((0 == os_strcmp(parsed_req.url, "/api/espbot/cfg")) && (parsed_req.req_method == HTTP_GET))
    {
        char *msg = (char *)os_zalloc(64);
        if (msg)
        {
            os_sprintf(msg, "{\"espbot_name\":\"%s\"}", espbot.get_name());
            response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
            // os_free(msg); // dont't free the msg buffer cause it could not have been used yet
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
            char *tmp_name = (char *)os_zalloc(espbot_cfg.get_cur_pair_value_len() + 1);
            if (tmp_name)
            {
                os_strncpy(tmp_name, espbot_cfg.get_cur_pair_value(), espbot_cfg.get_cur_pair_value_len());
                espbot.set_name(tmp_name);
                os_free(tmp_name);
            }
            else
            {
                esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", espbot_cfg.get_cur_pair_value_len() + 1);
            }
        }
        else
        {
            response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Json bad syntax", false);
            return;
        }
        char *msg = (char *)os_zalloc(64);
        if (msg)
        {
            os_sprintf(msg, "{\"espbot_name\":\"%s\"}", espbot.get_name());
            response(ptr_espconn, HTTP_CREATED, HTTP_CONTENT_JSON, msg, true);
            // os_free(msg); // dont't free the msg buffer cause it could not have been used yet
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
        espbot.reset();
        return;
    }
    if ((0 == os_strcmp(parsed_req.url, "/api/fs/info")) && (parsed_req.req_method == HTTP_GET))
    {
        if (espfs.is_available())
        {
            char *msg = (char *)os_zalloc(128);
            if (msg)
            {
                os_sprintf(msg, "{\"file_system_size\": %d,"
                                "\"file_system_used_size\": %d}",
                           espfs.get_total_size(), espfs.get_used_size());
                response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
                // os_free(msg); // dont't free the msg buffer cause it could not have been used yet
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
            char *file_list = (char *)os_zalloc(32 + (file_cnt * (32 + 3)));
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
                // os_free(file_list); // dont't free the msg buffer cause it could not have been used yet
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
    if ((0 == os_strcmp(parsed_req.url, "/api/debug/info")) && (parsed_req.req_method == HTTP_GET))
    {
        // check how much memory needed for last_errors
        int last_errors_len = 0;
        char *err_ptr = esp_last_errors.get_head();
        while (err_ptr)
        {
            last_errors_len += os_strlen(err_ptr);
            err_ptr = esp_last_errors.next();
        }
        char *msg = (char *)os_zalloc(64 +                           // formatting string
                                      10 +                           // heap mem figures
                                      (3 * esp_last_errors.size()) + // errors formatting
                                      last_errors_len);              // errors
        if (msg)
        {
            os_sprintf(msg,
                       "{\"current_heap_size\": %d,\"minimum_heap_size\": %d,\"errors\":[",
                       espdebug.check_heap_size(),
                       espdebug.get_mim_heap_size());
            // now add saved errors
            char *str_ptr;
            int cnt = 0;
            err_ptr = esp_last_errors.get_head();
            while (err_ptr)
            {
                str_ptr = msg + os_strlen(msg);
                if (cnt == 0)
                    os_sprintf(str_ptr, "%s", err_ptr);
                else
                    os_sprintf(str_ptr, ",%s", err_ptr);
                err_ptr = esp_last_errors.next();
                cnt++;
            }
            str_ptr = msg + os_strlen(msg);
            os_sprintf(str_ptr, "]}");
            response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
            // os_free(msg); // dont't free the msg buffer cause it could not have been used yet
        }
        else
        {
            esplog.error("Websvr::webserver_recv - not enough heap memory %d\n",
                         64 + 10 + (3 * esp_last_errors.size()) + last_errors_len);
        }
        return;
    }
    if ((0 == os_strcmp(parsed_req.url, "/api/debug/cfg")) && (parsed_req.req_method == HTTP_GET))
    {
        char *msg = (char *)os_zalloc(64);
        if (msg)
        {
            os_sprintf(msg,
                       "{\"logger_serial_level\": %d,\"logger_memory_level\": %d}",
                       esplog.get_serial_level(),
                       esplog.get_memory_level());
            response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
            // os_free(msg); // dont't free the msg buffer cause it could not have been used yet
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
            char *tmp_serial_level = (char *)os_zalloc(debug_cfg.get_cur_pair_value_len() + 1);
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
                os_free(tmp_serial_level);
                return;
            }
            if (debug_cfg.get_cur_pair_value_type() != JSON_INTEGER)
            {
                response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "JSON pair with string 'logger_memory_level' does not have an integer value type", false);
                os_free(tmp_serial_level);
                return;
            }
            char *tmp_memory_level = (char *)os_zalloc(debug_cfg.get_cur_pair_value_len() + 1);
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
            os_free(tmp_serial_level);
            os_free(tmp_memory_level);
        }
        else
        {
            response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Json bad syntax", false);
            return;
        }
        char *msg = (char *)os_zalloc(64);
        if (msg)
        {
            os_sprintf(msg,
                       "{\"logger_serial_level\": %d,\"logger_memory_level\": %d}",
                       esplog.get_serial_level(),
                       esplog.get_memory_level());
            response(ptr_espconn, HTTP_CREATED, HTTP_CONTENT_JSON, msg, true);
            // os_free(msg); // dont't free the msg buffer cause it could not have been used yet
        }
        else
        {
            esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", 64);
        }
        return;
    }
    if ((0 == os_strcmp(parsed_req.url, "/api/wifi/cfg")) && (parsed_req.req_method == HTTP_GET))
    {
        char *msg = (char *)os_zalloc(64);
        if (msg)
        {
            os_sprintf(msg,
                       "{\"station_ssid\":\"%s\",\"station_pwd\":\"%s\"}",
                       espwifi.station_get_ssid(),
                       espwifi.station_get_password());
            response(ptr_espconn, HTTP_OK, HTTP_CONTENT_JSON, msg, true);
            // os_free(msg); // dont't free the msg buffer cause it could not have been used yet
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
        }
        else
        {
            response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "Json bad syntax", false);
            return;
        }
        char *msg = (char *)os_zalloc(140);
        if (msg)
        {
            os_sprintf(msg,
                       "{\"station_ssid\":\"%s\",\"station_pwd\":\"%s\"}",
                       espwifi.station_get_ssid(),
                       espwifi.station_get_password());
            response(ptr_espconn, HTTP_CREATED, HTTP_CONTENT_JSON, msg, true);
            // os_free(msg); // dont't free the msg buffer cause it could not have been used yet
        }
        else
        {
            esplog.error("Websvr::webserver_recv - not enough heap memory %d\n", 140);
        }
        return;
    }
    if ((0 == os_strcmp(parsed_req.url, "/api/wifi/scan")) && (parsed_req.req_method == HTTP_POST))
    {
        // response(ptr_espconn, HTTP_OK, HTTP_CONTENT_TEXT, NULL);
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
    //  [GET]   /api/wifi/scan
    response(ptr_espconn, HTTP_BAD_REQUEST, HTTP_CONTENT_JSON, "I'm sorry, my responses are limited. You must ask the right question.", false);
}

static ICACHE_FLASH_ATTR void webserver_recon(void *arg, sint8 err)
{
    struct espconn *pesp_conn = (struct espconn *)arg;
    esplog.info("%d.%d.%d.%d:%d err %d reconnect\n", pesp_conn->proto.tcp->remote_ip[0],
                pesp_conn->proto.tcp->remote_ip[1],
                pesp_conn->proto.tcp->remote_ip[2],
                pesp_conn->proto.tcp->remote_ip[3],
                pesp_conn->proto.tcp->remote_port,
                err);
}

static ICACHE_FLASH_ATTR void webserver_discon(void *arg)
{
    struct espconn *pesp_conn = (struct espconn *)arg;
    esplog.info("%d.%d.%d.%d:%d disconnect\n", pesp_conn->proto.tcp->remote_ip[0],
                pesp_conn->proto.tcp->remote_ip[1],
                pesp_conn->proto.tcp->remote_ip[2],
                pesp_conn->proto.tcp->remote_ip[3],
                pesp_conn->proto.tcp->remote_port);
}

static void ICACHE_FLASH_ATTR webserver_listen(void *arg)
{
    struct espconn *pesp_conn = (struct espconn *)arg;
    espconn_regist_recvcb(pesp_conn, webserver_recv);
    espconn_regist_sentcb(pesp_conn, webserver_sentcb);
    espconn_regist_reconcb(pesp_conn, webserver_recon);
    espconn_regist_disconcb(pesp_conn, webserver_discon);
}

void ICACHE_FLASH_ATTR Websvr::start(uint32 port)
{
    os_timer_disarm(&websvr_wait_for_data_sent);
    os_timer_disarm(&format_delay_timer);
    os_timer_disarm(&wifi_scan_timeout_timer);

    esp_conn.type = ESPCONN_TCP;
    esp_conn.state = ESPCONN_NONE;
    esp_conn.proto.tcp = &esptcp;
    esp_conn.proto.tcp->local_port = port;
    espconn_regist_connectcb(&esp_conn, webserver_listen);
    espconn_accept(&esp_conn);
    esplog.info("web server started\n");
}

void ICACHE_FLASH_ATTR Websvr::stop()
{
    espconn_disconnect(&esp_conn);
    espconn_delete(&esp_conn);
    esplog.info("web server stopped\n");
}
