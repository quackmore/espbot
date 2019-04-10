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
#include "espbot.hpp"
#include "espbot_global.hpp"
#include "espbot_logger.hpp"
#include "espbot_json.hpp"
#include "espbot_utils.hpp"
#include "espbot_debug.hpp"

char ICACHE_FLASH_ATTR *code_msg(int code)
{
    esplog.all("Websvr::code_msg\n");
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

char ICACHE_FLASH_ATTR *json_error_msg(int code, char *msg)
{
    esplog.all("Websvr::json_error_msg\n");
    Heap_chunk err_msg(54 + 3 + 22 + os_strlen(msg), dont_free);
    if (err_msg.ref)
    {
        os_sprintf(err_msg.ref,
                   "{\"error\":{\"code\": %d,\"message\": \"%s\",\"reason\": \"%s\"}}",
                   code, code_msg(code), msg);
        return err_msg.ref;
    }
    else
    {
        esplog.error("json_error_msg - not enough heap memory %d\n", (56 + os_strlen(msg)));
        return NULL;
    }
}

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

#define DATA_SENT_TIMER_PERIOD 40
#define MAX_PENDING_RESPONSE_COUNT 10

static char *send_buffer;
static bool esp_busy_sending_data = false;
static os_timer_t pending_response_timer[MAX_PENDING_RESPONSE_COUNT];
static bool pending_response_timer_busy[MAX_PENDING_RESPONSE_COUNT];

static os_timer_t clear_busy_sending_data_timer;

static void ICACHE_FLASH_ATTR clear_busy_sending_data(void *arg)
{
    esplog.all("Websvr::clear_busy_sending_data\n");
    esp_busy_sending_data = false;
}

struct svr_response
{
    struct espconn *p_espconn;
    char *msg;
    bool free_msg;
    char timer_idx;
};

static void ICACHE_FLASH_ATTR webserver_pending_response(void *arg)
{
    struct svr_response *response_data = (struct svr_response *)arg;
    // free pending response_timer
    pending_response_timer_busy[response_data->timer_idx] = false;
    esplog.all("Websvr::webserver_pending_response\n");
    // esplog.trace("response: *p_espconn: %X\n"
    //              "                 msg: %s\n"
    //              "           timer_idx: %d\n",
    //              response_data->p_espconn, response_data->msg, response_data->timer_idx);
    if (espwebsvr.get_status() == up)
        send_response_buffer(response_data->p_espconn, response_data->msg);
    esp_free(response_data);
}

static void ICACHE_FLASH_ATTR webserver_sentcb(void *arg)
{
    esplog.all("Websvr::webserver_sentcb\n");
    struct espconn *ptr_espconn = (struct espconn *)arg;
    esp_busy_sending_data = false;
    if (send_buffer)
    {
        esp_free(send_buffer);
        send_buffer = NULL;
    }
}

void free_http_response(struct http_response *ptr)
{
    esplog.all("Websvr::free_http_response\n");
    esp_free(ptr->msg);
    esp_free(ptr);
}

/*
send_response
{
    format buffer
    check if pending responses are needed and queue them
    send_response_buffer
}

send_response_buffer()
{
    if (busy)
        queue the buffer and espconn
    else
        set send busy
        send the buffer
}

sent_cb
{
    set send free
    if buffer queue is not empty
        signal to task
    if on espconn there is a pending response
        signal to task
}

task
{
    buffer queue is not empty: 
        pop from queue
        send_buffer
        check for pending response on espconn
        pending response
}
*/

//
// won't check the length of the sent message
//
void ICACHE_FLASH_ATTR send_response_buffer(struct espconn *p_espconn, char *msg)
{
    // Profiler ret_file("send_response_buffer");
    system_soft_wdt_feed();
    if (esp_busy_sending_data) // previous espconn_send not completed yet
    {
        esplog.debug("Websvr::send_response_buffer - previous espconn_send not completed yet\n");
        struct svr_response *response_data = (struct svr_response *)esp_zalloc(sizeof(struct svr_response));
        espmem.stack_mon();
        if (response_data)
        {
            // look for a free pending_response_timer
            int timer_idx;
            for (timer_idx = 0; timer_idx < MAX_PENDING_RESPONSE_COUNT; timer_idx++)
                if (!pending_response_timer_busy[timer_idx])
                {
                    // reserve the timer
                    pending_response_timer_busy[timer_idx] = true;
                    break;
                }
            // check if an available timer was found
            if (timer_idx >= MAX_PENDING_RESPONSE_COUNT)
                esplog.error("Websvr::send_response: no pending_response timers available\n");
            else
            {
                response_data->p_espconn = p_espconn;
                response_data->msg = msg;
                response_data->timer_idx = timer_idx;
                // esplog.trace("response: *p_espconn: %X\n"
                //              "                 msg: %s\n"
                //              "           timer_idx: %d\n",
                //              p_espconn, msg, timer_idx);
                os_timer_disarm(&pending_response_timer[timer_idx]);
                os_timer_setfn(&pending_response_timer[timer_idx], (os_timer_func_t *)webserver_pending_response, (void *)response_data);
                // calculate random timer between DATA_SENT_TIMER_PERIOD and 2*DATA_SENT_TIMER_PERIOD
                int timer_value = DATA_SENT_TIMER_PERIOD + get_rand_int(DATA_SENT_TIMER_PERIOD);
                os_timer_arm(&pending_response_timer[timer_idx], timer_value, 0);
                esplog.all("Websvr::send_response - waiting for espconn_send to complete\n");
            }
        }
        else
        {
            esplog.error("Websvr::send_response: not enough heap memory (%d)\n", sizeof(struct svr_response));
        }
    }
    else // previous espconn_send completed
    {
        esplog.all("webserver::send_response_buffer\n");
        esplog.trace("response: *p_espconn: %X\n"
                     "                 msg: %s\n",
                     p_espconn, msg);
        esp_busy_sending_data = true;
        // set a timeout timer for clearing the esp_busy_sending_data in case something goes wrong
        os_timer_disarm(&clear_busy_sending_data_timer);
        os_timer_setfn(&clear_busy_sending_data_timer, (os_timer_func_t *)clear_busy_sending_data, NULL);
        os_timer_arm(&clear_busy_sending_data_timer, 10000, 0);

        send_buffer = msg;
        sint8 res = espconn_send(p_espconn, (uint8 *)send_buffer, os_strlen(send_buffer));
        espmem.stack_mon();
        if (res)
        {
            esplog.error("websvr::send_response: error sending response, error code %d\n", res);
            // on error don't count on sentcb to be called
            esp_busy_sending_data = false;
            esp_free(send_buffer);
            send_buffer = NULL;
        }
        // esp_free(send_buffer); // webserver_sentcb will free it
    }
}

void ICACHE_FLASH_ATTR response(struct espconn *p_espconn, int code, char *content_type, char *msg, bool free_msg)
{
    // Profiler ret_file("response");
    esplog.all("webserver::response\n");
    esplog.trace("response: *p_espconn: %X\n"
                 "                code: %d\n"
                 "        content-type: %s\n"
                 "                 msg: %s\n"
                 "            free_msg: %d\n",
                 p_espconn, code, content_type, msg, free_msg);
    if (code >= HTTP_BAD_REQUEST) // format error msg as json
    {
        char *err_msg = json_error_msg(code, msg);

        // free original message
        if (free_msg)
            esp_free(msg);
        if (err_msg)
        {
            // replace original msg with the formatted one
            msg = err_msg;
            free_msg = true;
        }
        else
        {
            return;
        }
    }
    // allocate a buffer
    // HTTP...        ->  40 + 3 + 22 =  65
    // Content-Type   ->  20 + 17     =  37
    // Content-Length ->  22 + 15     =  47
    // Pragma         ->  24          =  24
    //                                = 173
    Heap_chunk complete_msg((173 + os_strlen(msg)), dont_free);
    if (complete_msg.ref)
    {
        os_sprintf(complete_msg.ref, "HTTP/1.0 %d %s\r\nServer: espbot/2.0\r\n"
                                     "Content-Type: %s\r\n"
                                     "Content-Length: %d\r\n"
                                     "Pragma: no-cache\r\n\r\n%s",
                   code, code_msg(code), content_type, os_strlen(msg), msg);
        if (free_msg)
            esp_free(msg); // free the msg buffer now that it has been used
        struct http_response *p_res = (struct http_response *)esp_zalloc(sizeof(struct http_response));
        if (p_res)
        {
            p_res->p_espconn = p_espconn;
            p_res->msg = complete_msg.ref;
            send_response(p_res);
        }
        else
        {
            esplog.error("websvr::response: not enough heap memory (%d)\n", sizeof(struct http_response));
        }
        espmem.stack_mon();
    }
    else
    {
        esplog.error("websvr::response: not enough heap memory (%d)\n", (173 + os_strlen(msg)));
    }
}

#define MAX_SPLIT_RESPONSE_TIMER 12
static os_timer_t split_msg_timer[MAX_SPLIT_RESPONSE_TIMER];
static bool split_msg_timer_busy[MAX_SPLIT_RESPONSE_TIMER];

int get_free_split_msg_timer(void)
{
    esplog.all("webserver::get_free_split_msg_timer\n");
    int timer_idx;
    for (timer_idx = 0; timer_idx < MAX_SPLIT_RESPONSE_TIMER; timer_idx++)
        if (!split_msg_timer_busy[timer_idx])
        {
            split_msg_timer_busy[timer_idx] = true;
            break;
        }
    if (timer_idx >= MAX_SPLIT_RESPONSE_TIMER)
        return -1;
    else
        return timer_idx;
}

os_timer_t ICACHE_FLASH_ATTR *get_split_msg_timer(int idx)
{
    esplog.all("webserver::get_split_msg_timer\n");
    split_msg_timer_busy[idx] = true;
    return &split_msg_timer[idx];
}

void ICACHE_FLASH_ATTR free_split_msg_timer(int idx)
{
    esplog.all("webserver::free_split_msg_timer\n");
    split_msg_timer_busy[idx] = false;
}

static void ICACHE_FLASH_ATTR send_remaining_msg(void *param)
{
    // Profiler ret_file("send_remaining_msg");
    esplog.all("webserver::send_remaining_msg\n");
    struct http_response *p_response = (struct http_response *)param;
    // free the timer
    free_split_msg_timer(p_response->timer_idx);
    // check if meanwhile the http server went down
    if (espwebsvr.get_status() == down)
    {
        free_http_response(p_response);
        return;
    }
    // allocate a buffer
    int buffer_size;
    if (os_strlen(p_response->remaining_msg) > espwebsvr.get_response_max_size())
        buffer_size = espwebsvr.get_response_max_size();
    else
        buffer_size = os_strlen(p_response->remaining_msg);
    Heap_chunk msg(buffer_size, dont_free);
    if (msg.ref)
    {
        // esplog.trace("response: *p_espconn: %X\n"
        //              "                 msg: %s\n",
        //              p_response->p_espconn, p_response->remaining_msg);
        os_strncpy(msg.ref, p_response->remaining_msg, buffer_size);
        send_response_buffer(p_response->p_espconn, msg.ref);
        // check if the message was not completely sent
        if (os_strlen(p_response->remaining_msg) > buffer_size)
        {
            p_response->remaining_msg = p_response->remaining_msg + buffer_size;
            // look for a free split_response_timer
            int timer_idx = get_free_split_msg_timer();
            // check if there's a timer available
            if (timer_idx < 0)
            {
                free_http_response(p_response);
                esplog.error("Websvr::response: no split_response_timer available\n");
            }
            else
            { // start a timer and pass remaining message description
                p_response->timer_idx = timer_idx;
                os_timer_t *split_timer = get_split_msg_timer(timer_idx);
                os_timer_disarm(split_timer);
                os_timer_setfn(split_timer, (os_timer_func_t *)send_remaining_msg, (void *)p_response);
                // calculate random timer between DATA_SENT_TIMER_PERIOD and 2*DATA_SENT_TIMER_PERIOD
                int timer_value = DATA_SENT_TIMER_PERIOD + get_rand_int(DATA_SENT_TIMER_PERIOD);
                os_timer_arm(split_timer, timer_value, 0);
            }
        }
        else
        {
            free_http_response(p_response);
        }
    }
    else
    {
        free_http_response(p_response);
        esplog.error("websvr::response: not enough heap memory (%d)\n", buffer_size);
    }
}

//
// will split the message when the length is greater than webserver response_max_size
//
void ICACHE_FLASH_ATTR send_response(struct http_response *p_response)
{
    // Profiler ret_file("send_response");
    esplog.all("webserver::send_response\n");
    int buffer_size;
    if (os_strlen(p_response->msg) > espwebsvr.get_response_max_size())
        buffer_size = espwebsvr.get_response_max_size();
    else
        buffer_size = os_strlen(p_response->msg);
    Heap_chunk buffer(buffer_size + 1, dont_free);
    if (buffer.ref)
    {
        os_strncpy(buffer.ref, p_response->msg, buffer_size);
        esplog.trace("response: *p_espconn: %X\n"
                     "                 msg: %s\n",
                     p_response->p_espconn, buffer.ref);
        send_response_buffer(p_response->p_espconn, buffer.ref);
        // check if the message was not completely sent
        if (os_strlen(p_response->msg) > buffer_size)
        {
            p_response->remaining_msg = p_response->msg + buffer_size;
            // look for a free split_response_timer
            int timer_idx = get_free_split_msg_timer();
            // check if there's a timer available
            if (timer_idx < 0)
            {
                free_http_response(p_response);
                esplog.error("Websvr::response: no split_response_timer available\n");
            }
            else
            { // start a timer and pass remaining message description
                p_response->timer_idx = timer_idx;
                os_timer_t *split_timer = get_split_msg_timer(timer_idx);
                os_timer_disarm(split_timer);
                os_timer_setfn(split_timer, (os_timer_func_t *)send_remaining_msg, (void *)p_response);
                // calculate random timer between DATA_SENT_TIMER_PERIOD and 2*DATA_SENT_TIMER_PERIOD
                int timer_value = DATA_SENT_TIMER_PERIOD + get_rand_int(DATA_SENT_TIMER_PERIOD);
                os_timer_arm(split_timer, timer_value, 0);
            }
        }
        else
            free_http_response(p_response);
    }
    else
    {
        free_http_response(p_response);
        esplog.error("websvr::response: not enough heap memory (%d)\n", buffer_size);
    }
}

char ICACHE_FLASH_ATTR *format_header(struct http_header *p_header)
{
    esplog.all("webserver::format_header\n");
    // allocate a buffer
    // HTTP...        ->  37 + 3 + 22 =  62
    // Content-Type   ->  19 + 17     =  36
    // Content-Length ->  22 + 5      =  27
    // Content-Range  ->  32 + 15     =  47
    // Pragma         ->  24          =  24
    //                                = 196
    Heap_chunk header_msg(196, dont_free);
    if (header_msg.ref)
    {
        // setup the header
        char *ptr = header_msg.ref;
        os_sprintf(ptr, "HTTP/1.0 %d %s\r\nServer: espbot/2.0\r\n",
                   p_header->code, code_msg(p_header->code));
        ptr = ptr + os_strlen(ptr);
        os_sprintf(ptr, "Content-Type: %s\r\n", p_header->content_type);
        ptr = ptr + os_strlen(ptr);
        if (p_header->content_range_total > 0)
        {
            os_sprintf(ptr, "Content-Range: bytes %d-%d/%d\r\n", p_header->content_range_total, p_header->content_range_total, p_header->content_range_total);
            ptr = ptr + os_strlen(ptr);
        }
        os_sprintf(ptr, "Content-Length: %d\r\n", p_header->content_length);
        ptr = ptr + os_strlen(ptr);
        // os_sprintf(ptr, "Date: Wed, 28 Nov 2018 12:00:00 GMT\r\n");
        // os_printf("---->msg: %s\n", msg.ref);
        ptr = ptr + os_strlen(ptr);
        os_sprintf(ptr, "Pragma: no-cache\r\n\r\n");
        return header_msg.ref;
    }
    else
    {
        esplog.error("websvr::format_header: not enough heap memory (%d)\n", 196);
        return NULL;
    }
}

// end of HTTP responding

//
// HTTP Receiving:
//

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
    espbot_http_routes(ptr_espconn, &parsed_req);
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
    // setup a timer pool for managing delayed espconn_send
    int idx;
    for (idx = 0; idx < MAX_PENDING_RESPONSE_COUNT; idx++)
    {
        os_timer_disarm(&pending_response_timer[idx]);
        pending_response_timer_busy[idx] = false;
    }

    for (idx = 0; idx < MAX_SPLIT_RESPONSE_TIMER; idx++)
    {
        os_timer_disarm(&split_msg_timer[idx]);
        split_msg_timer_busy[idx] = false;
    }

    // setup specific controllers timer
    init_controllers();

    // setup sdk TCP variables
    m_esp_conn.type = ESPCONN_TCP;
    m_esp_conn.state = ESPCONN_NONE;
    m_esp_conn.proto.tcp = &m_esptcp;
    m_esp_conn.proto.tcp->local_port = port;
    espconn_regist_connectcb(&m_esp_conn, webserver_listen);
    espconn_accept(&m_esp_conn);

    // setup the default response buffer size
    m_send_response_max_size = 1024;

    // now the server is up
    m_status = up;
    esplog.debug("web server started\n");
}

void ICACHE_FLASH_ATTR Websvr::stop()
{
    esplog.all("Websvr::stop\n");
    espconn_disconnect(&m_esp_conn);
    espconn_delete(&m_esp_conn);
    m_status = down;
    esplog.debug("web server stopped\n");
}

Websvr_status ICACHE_FLASH_ATTR Websvr::get_status(void)
{
    return m_status;
}

void ICACHE_FLASH_ATTR Websvr::set_response_max_size(int size)
{
    m_send_response_max_size = size;
}

int ICACHE_FLASH_ATTR Websvr::get_response_max_size(void)
{
    return m_send_response_max_size;
}