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

#include "espbot_queue.hpp"
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

ICACHE_FLASH_ATTR Http_header::Http_header()
{
    m_content_type = NULL;
    m_acrh = NULL;
    m_origin = NULL;
}

ICACHE_FLASH_ATTR Http_header::~Http_header()
{
    if (m_acrh)
        delete[] m_acrh;
    if (m_origin)
        delete[] m_origin;
}

//
// HTTP responding:
// ----------------
// to make sure espconn_send is called after espconn_sent_callback of the previous packet
// a flag is set before calling espconn_send (will be reset by sendcb)
//
// befor sending a response the flag will be checked
// when the flag is found set (espconn_send not done yet)
// the response is queued
//

static Queue<struct http_response> *pending_send;
static char *send_buffer;
static bool esp_busy_sending_data = false;

static os_timer_t clear_busy_sending_data_timer;

static void ICACHE_FLASH_ATTR clear_busy_sending_data(void *arg)
{
    esplog.trace("Websvr - espconn_send timeout\n");
    // something went wrong an this timeout was triggered
    // clear the flag, the buffer and trigger a check of the pending responses queue
    os_timer_disarm(&clear_busy_sending_data_timer);
    if (send_buffer)
    {
        // esplog.trace("Websvr - clear_busy_sending_data: deleting send_buffer %X\n", send_buffer);
        delete[] send_buffer;
        send_buffer = NULL;
    }
    esp_busy_sending_data = false;
    system_os_post(USER_TASK_PRIO_0, SIG_HTTP_CHECK_PENDING_RESPONSE, '0');
}

void ICACHE_FLASH_ATTR webserver_check_pending_response(void)
{
    esplog.all("Websvr::webserver_check_pending_response\n");
    if (espwebsvr.get_status() == down)
    {
        // meanwhile the server went down
        esplog.trace("Websvr - clearing pending send and response queues\n");
        // clear the pending send queue
        struct http_response *p_pending_send = pending_send->front();
        while (p_pending_send)
        {
            pending_send->pop();
            delete[] p_pending_send->msg;
            delete p_pending_send;
            p_pending_send = pending_send->front();
        }
        // clear the split response queue
        struct http_split_response *p_pending_response = pending_response->front();
        while (p_pending_response)
        {
            pending_response->pop();
            delete[] p_pending_response->content;
            delete p_pending_response;
            p_pending_response = pending_response->front();
        }
        return;
    }
    // the server is up!
    // check pending send queue
    struct http_response *p_pending_send = pending_send->front();
    if (p_pending_send)
    {
        esplog.trace("pending send found: *p_espconn: %X, msg len: %d\n",
                     p_pending_send->p_espconn, os_strlen(p_pending_send->msg));
        send_response_buffer(p_pending_send->p_espconn, p_pending_send->msg);
        // the send procedure will clear the buffer so just delete the http_response
        // esplog.trace("Websvr - webserver_check_pending_response: deleting p_pending_send\n");
        delete p_pending_send;
        pending_send->pop();
        // a pending response was found
        // wait for next pending response check so skip any other code
        return;
    }
    // no pending send was found
    // check other pending actions (such as long messages that required to be split)
    struct http_split_response *p_pending_response = pending_response->front();
    if (p_pending_response)
    {
        esplog.trace("pending split response found: *p_espconn: %X\n"
                     "                            content_size: %d\n"
                     "                     content_transferred: %d\n"
                     "                         action_function: %X\n",
                     p_pending_response->p_espconn,
                     p_pending_response->content_size,
                     p_pending_response->content_transferred,
                     p_pending_response->action_function);
        p_pending_response->action_function(p_pending_response);
        // don't free the content yet
        // esplog.trace("Websvr - webserver_check_pending_response: deleting p_pending_response\n");
        delete p_pending_response;
        pending_response->pop();
        // serving just one pending_response, so that just one espconn_send is engaged
        // next one will be triggered by a espconn_send completion
    }
}

static void ICACHE_FLASH_ATTR webserver_sentcb(void *arg)
{
    esplog.all("Websvr::webserver_sentcb\n");
    struct espconn *ptr_espconn = (struct espconn *)arg;
    // clear the flag and the timeout timer
    os_timer_disarm(&clear_busy_sending_data_timer);
    // clear the message_buffer
    if (send_buffer)
    {
        // esplog.trace("Websvr - webserver_sentcb: deleting send_buffer %X\n", send_buffer);
        delete[] send_buffer;
        send_buffer = NULL;
    }
    esp_busy_sending_data = false;
    system_os_post(USER_TASK_PRIO_0, SIG_HTTP_CHECK_PENDING_RESPONSE, '0');
}

//
// won't check the length of the sent message
//
void ICACHE_FLASH_ATTR send_response_buffer(struct espconn *p_espconn, char *msg)
{
    // Profiler ret_file("send_response_buffer");
    ETS_INTR_LOCK();
    if (esp_busy_sending_data) // previous espconn_send not completed yet
    {
        ETS_INTR_UNLOCK();
        esplog.trace("Websvr::send_response_buffer - previous espconn_send not completed yet\n");
        struct http_response *response_data = new struct http_response;
        espmem.stack_mon();
        if (response_data)
        {
            response_data->p_espconn = p_espconn;
            response_data->msg = msg;
            Queue_err result = pending_send->push(response_data);
            if (result == Queue_full)
                esplog.error("Websvr::send_response_buffer: pending send queue is full\n");
        }
        else
        {
            esplog.error("Websvr::send_response_buffer: not enough heap memory (%d)\n", sizeof(struct http_response));
        }
    }
    else // previous espconn_send completed
    {
        esp_busy_sending_data = true;
        ETS_INTR_UNLOCK();
        esplog.trace("espconn_send: *p_espconn: %X\n"
                     "                     msg: %s\n",
                     p_espconn,
                     msg);
        // set a timeout timer for clearing the esp_busy_sending_data in case something goes wrong
        os_timer_disarm(&clear_busy_sending_data_timer);
        os_timer_setfn(&clear_busy_sending_data_timer, (os_timer_func_t *)clear_busy_sending_data, NULL);
        os_timer_arm(&clear_busy_sending_data_timer, 2000, 0);

        send_buffer = msg;
        sint8 res = espconn_send(p_espconn, (uint8 *)send_buffer, os_strlen(send_buffer));
        espmem.stack_mon();
        if (res)
        {
            esplog.error("websvr::send_response_buffer: error sending response, error code %d\n", res);
            // nevermind about sentcb, there is a timeout now
            // esp_busy_sending_data = false;
            // delete[] send_buffer;
            // send_buffer = NULL;
        }
        // esp_free(send_buffer); // webserver_sentcb will free it
    }
    system_soft_wdt_feed();
}

void ICACHE_FLASH_ATTR response(struct espconn *p_espconn, int code, char *content_type, char *msg, bool free_msg)
{
    esplog.all("webserver::response\n");
    esplog.trace("response: *p_espconn: %X\n"
                 "                code: %d\n"
                 "        content-type: %s\n"
                 "          msg length: %d\n"
                 "            free_msg: %d\n",
                 p_espconn, code, content_type, os_strlen(msg), free_msg);
    // when code is not 200 format the error msg as json
    if (code >= HTTP_BAD_REQUEST)
    {
        char *err_msg = json_error_msg(code, msg);

        // free original message
        if (free_msg)
            delete[] msg;
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
    // Now format the message header
    int header_len = 110 +
                     3 +
                     os_strlen(code_msg(code)) +
                     os_strlen(content_type) +
                     os_strlen(msg);
    Heap_chunk msg_header(header_len, dont_free);
    if (msg_header.ref == NULL)
    {
        esplog.error("websvr::response: not enough heap memory (%d)\n", header_len);
        return;
    }
    os_sprintf(msg_header.ref, "HTTP/1.0 %d %s\r\nServer: espbot/2.0\r\n"
                               "Content-Type: %s\r\n"
                               "Content-Length: %d\r\n"
                               "Access-Control-Allow-Origin: *\r\n\r\n",
               code, code_msg(code), content_type, os_strlen(msg));
    // send separately the header from the content
    // to avoid allocating twice the memory for the message
    // especially very large ones
    send_response_buffer(p_espconn, msg_header.ref);
    // when there is no message that's all
    if (os_strlen(msg) == 0)
        return;
    if (free_msg)
    {
        send_response(p_espconn, msg);
    }
    else
    {
        // response message is not allocated on heap
        // copy it to a buffer
        Heap_chunk msg_short(os_strlen(msg), dont_free);
        if (msg_header.ref)
        {
            os_strcpy(msg_short.ref, msg);
            send_response(p_espconn, msg_short.ref);
        }
        else
        {
            esplog.error("websvr::response: not enough heap memory (%d)\n", os_strlen(msg));
        }
    }
    espmem.stack_mon();
}

Queue<struct http_split_response> *pending_response;

static void ICACHE_FLASH_ATTR send_remaining_msg(struct http_split_response *p_sr)
{
    esplog.all("webserver::send_remaining_msg\n");

    if ((p_sr->content_size - p_sr->content_transferred) > espwebsvr.get_response_max_size())
    {
        // the message is bigger than response_max_size
        // will split the message
        int buffer_size = espwebsvr.get_response_max_size();
        Heap_chunk buffer(buffer_size + 1, dont_free);
        if (buffer.ref)
        {
            struct http_split_response *p_pending_response = new struct http_split_response;
            if (p_pending_response)
            {
                os_strncpy(buffer.ref, p_sr->content + p_sr->content_transferred, buffer_size);
                // setup the remaining message
                p_pending_response->p_espconn = p_sr->p_espconn;
                p_pending_response->content = p_sr->content;
                p_pending_response->content_size = p_sr->content_size;
                p_pending_response->content_transferred = p_sr->content_transferred + buffer_size;
                p_pending_response->action_function = send_remaining_msg;
                Queue_err result = pending_response->push(p_pending_response);
                if (result == Queue_full)
                    esplog.error("Websvr::send_response_buffer: pending response queue is full\n");

                esplog.trace("send_remaining_msg: *p_espconn: %X\n"
                             "msg (splitted) len: %d\n",
                             p_sr->p_espconn, os_strlen(buffer.ref));
                send_response_buffer(p_sr->p_espconn, buffer.ref);
            }
            else
            {
                esplog.error("websvr::send_remaining_msg: not enough heap memory (%d)\n", sizeof(struct http_split_response));
                delete[] buffer.ref;
                delete[] p_sr->content;
            }
        }
        else
        {
            esplog.error("websvr::send_remaining_msg: not enough heap memory (%d)\n", buffer_size);
            delete[] p_sr->content;
        }
    }
    else
    {
        // this is the last piece of the message
        int buffer_size = espwebsvr.get_response_max_size();
        Heap_chunk buffer(buffer_size + 1, dont_free);
        if (buffer.ref)
        {
            os_strncpy(buffer.ref, p_sr->content + p_sr->content_transferred, buffer_size);
            esplog.trace("send_remaining_msg: *p_espconn: %X\n"
                         "          msg (last piece) len: %d\n",
                         p_sr->p_espconn, os_strlen(buffer.ref));
            send_response_buffer(p_sr->p_espconn, buffer.ref);
        }
        else
        {
            esplog.error("websvr::send_remaining_msg: not enough heap memory (%d)\n", buffer_size);
        }
        // esplog.trace("Websvr - send_remaining_msg: deleting p_sr->content\n");
        delete[] p_sr->content;
    }
}

//
// will split the message when the length is greater than webserver response_max_size
//
void ICACHE_FLASH_ATTR send_response(struct espconn *p_espconn, char *msg)
{
    // Profiler ret_file("send_response");
    esplog.all("webserver::send_response\n");
    if (os_strlen(msg) > espwebsvr.get_response_max_size())
    {
        // the message is bigger than response_max_size
        // will split the message
        int buffer_size = espwebsvr.get_response_max_size();
        Heap_chunk buffer(buffer_size + 1, dont_free);
        if (buffer.ref)
        {
            struct http_split_response *p_pending_response = new struct http_split_response;
            if (p_pending_response)
            {
                os_strncpy(buffer.ref, msg, buffer_size);
                // setup the remaining message
                p_pending_response->p_espconn = p_espconn;
                p_pending_response->content = msg;
                p_pending_response->content_size = os_strlen(msg);
                p_pending_response->content_transferred = buffer_size;
                p_pending_response->action_function = send_remaining_msg;
                Queue_err result = pending_response->push(p_pending_response);
                if (result == Queue_full)
                    esplog.error("Websvr::send_response_buffer: pending response queue is full\n");

                esplog.trace("send_response: *p_espconn: %X\n"
                             "       msg (splitted) len: %d\n",
                             p_espconn, os_strlen(buffer.ref));
                send_response_buffer(p_espconn, buffer.ref);
            }
            else
            {
                esplog.error("websvr::send_response: not enough heap memory (%d)\n", sizeof(struct http_split_response));
                delete[] buffer.ref;
                delete[] msg;
            }
        }
        else
        {
            esplog.error("websvr::send_response: not enough heap memory (%d)\n", buffer_size);
            delete[] msg;
        }
    }
    else
    {
        // no need to split the message, just send it
        esplog.trace("send_response: *p_espconn: %X\n"
                     "           msg (full) len: %d\n",
                     p_espconn, os_strlen(msg));
        send_response_buffer(p_espconn, msg);
    }
}

char ICACHE_FLASH_ATTR *format_header(class Http_header *p_header)
{
    esplog.all("webserver::format_header\n");
    // allocate a buffer
    // HTTP...        ->  37 + 3 + 22 =  62
    // Content-Type   ->  19 + 17     =  36
    // Content-Length ->  22 + 5      =  27
    // Content-Range  ->  32 + 15     =  47
    // Pragma         ->  24          =  24
    //                                = 196
    int header_length = 196;
    if (p_header->m_acrh)
    {
        header_length += 37; // Access-Control-Request-Headers string format
        header_length += os_strlen(p_header->m_acrh);
        header_length += os_strlen("Access-Control-Allow-Methods: GET,POST,PUT,DELETE,OPTIONS\r\n");
    }

    if (p_header->m_origin)
    {
        header_length += 34; // Origin string format
        header_length += os_strlen(p_header->m_origin);
    }

    Heap_chunk header_msg(header_length, dont_free);
    if (header_msg.ref)
    {
        // setup the header
        char *ptr = header_msg.ref;
        os_sprintf(ptr, "HTTP/1.0 %d %s\r\nServer: espbot/2.0\r\n",
                   p_header->m_code, code_msg(p_header->m_code));
        ptr = ptr + os_strlen(ptr);
        os_sprintf(ptr, "Content-Type: %s\r\n", p_header->m_content_type);
        ptr = ptr + os_strlen(ptr);
        if (p_header->m_content_range_total > 0)
        {
            os_sprintf(ptr, "Content-Range: bytes %d-%d/%d\r\n", p_header->m_content_range_total, p_header->m_content_range_total, p_header->m_content_range_total);
            ptr = ptr + os_strlen(ptr);
        }
        os_sprintf(ptr, "Content-Length: %d\r\n", p_header->m_content_length);
        ptr = ptr + os_strlen(ptr);
        // os_sprintf(ptr, "Date: Wed, 28 Nov 2018 12:00:00 GMT\r\n");
        // os_printf("---->msg: %s\n", msg.ref);
        if (p_header->m_origin)
        {
            os_sprintf(ptr, "Access-Control-Allow-Origin: %s\r\n", p_header->m_origin);
            ptr = ptr + os_strlen(ptr);
        }
        if (p_header->m_acrh)
        {
            os_sprintf(ptr, "Access-Control-Allow-Methods: GET,POST,PUT,DELETE,OPTIONS\r\n");
            ptr = ptr + os_strlen(ptr);
            os_sprintf(ptr, "Access-Control-Allow-Headers: Content-Type,%s\r\n", p_header->m_acrh);
            ptr = ptr + os_strlen(ptr);
        }
        os_sprintf(ptr, "Pragma: no-cache\r\n\r\n");
        return header_msg.ref;
    }
    else
    {
        esplog.error("websvr::format_header: not enough heap memory (%d)\n", header_length);
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
    acrh = NULL;
    origin = NULL;
    url = NULL;
    content_len = 0;
    req_content = NULL;
}

ICACHE_FLASH_ATTR Html_parsed_req::~Html_parsed_req()
{
    esplog.all("Html_parsed_req::~Html_parsed_req\n");
    if (acrh)
        delete[] acrh;
    if (origin)
        delete[] origin;
    if (url)
        delete[] url;
    if (req_content)
        delete[] req_content;
}

static void ICACHE_FLASH_ATTR parse_http_request(char *req, Html_parsed_req *parsed_req)
{
    esplog.all("webserver::parse_http_request\n");
    char *tmp_ptr = req;
    char *end_ptr = NULL;
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
    else if (os_strncmp(tmp_ptr, "OPTIONS ", 8) == 0)
    {
        parsed_req->req_method = HTTP_OPTIONS;
        tmp_ptr += 8;
    }
    else
    {
        parsed_req->no_header_message = true;
    }

    if (parsed_req->no_header_message)
    {
        parsed_req->content_len = os_strlen(tmp_ptr);
        parsed_req->req_content = new char[parsed_req->content_len + 1];
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
    parsed_req->url = new char[len + 1];
    if (parsed_req->url == NULL)
    {
        esplog.error("websvr::parse_http_request - not enough heap memory\n");
        return;
    }
    os_memcpy(parsed_req->url, tmp_ptr, len);

    // checkout Access-Control-Request-Headers
    tmp_ptr = req;
    tmp_ptr = (char *)os_strstr(tmp_ptr, "Access-Control-Request-Headers: ");
    if (tmp_ptr == NULL)
    {
        tmp_ptr = req;
        tmp_ptr = (char *)os_strstr(tmp_ptr, "access-control-request-headers: ");
    }
    if (tmp_ptr != NULL)
    {
        tmp_ptr += 32;
        end_ptr = (char *)os_strstr(tmp_ptr, "\r\n");
        if (end_ptr == NULL)
        {
            esplog.error("websvr::parse_http_request - cannot find Access-Control-Request-Headers\n");
            return;
        }
        len = end_ptr - tmp_ptr;
        parsed_req->acrh = new char[len + 1];
        if (parsed_req->acrh == NULL)
        {
            esplog.error("websvr::parse_http_request - not enough heap memory\n");
            return;
        }
        os_strncpy(parsed_req->acrh, tmp_ptr, len);
    }

    // checkout Origin
    tmp_ptr = req;
    tmp_ptr = (char *)os_strstr(tmp_ptr, "Origin: ");
    if (tmp_ptr == NULL)
    {
        tmp_ptr = req;
        tmp_ptr = (char *)os_strstr(tmp_ptr, "origin: ");
    }
    if (tmp_ptr != NULL)
    {
        tmp_ptr += 8;
        end_ptr = (char *)os_strstr(tmp_ptr, "\r\n");
        if (end_ptr == NULL)
        {
            esplog.error("websvr::parse_http_request - cannot find origin\n");
            return;
        }
        len = end_ptr - tmp_ptr;
        parsed_req->origin = new char[len + 1];
        if (parsed_req->origin == NULL)
        {
            esplog.error("websvr::parse_http_request - not enough heap memory\n");
            return;
        }
        os_strncpy(parsed_req->origin, tmp_ptr, len);
    }

    // checkout for request content
    // and calculate the effective content length
    tmp_ptr = req;
    tmp_ptr = (char *)os_strstr(tmp_ptr, "\r\n\r\n");
    if (tmp_ptr == NULL)
    {
        esplog.error("websvr::parse_http_request - cannot find Content start\n");
        return;
    }
    tmp_ptr += 4;
    parsed_req->content_len = os_strlen(tmp_ptr);
    parsed_req->req_content = new char[parsed_req->content_len + 1];
    if (parsed_req->req_content == NULL)
    {
        esplog.error("websvr::parse_http_request - not enough heap memory\n");
        return;
    }
    os_memcpy(parsed_req->req_content, tmp_ptr, parsed_req->content_len);

    // checkout Content-Length
    parsed_req->h_content_len = parsed_req->content_len;
    tmp_ptr = req;
    tmp_ptr = (char *)os_strstr(tmp_ptr, "Content-Length: ");
    if (tmp_ptr == NULL)
    {
        tmp_ptr = req;
        tmp_ptr = (char *)os_strstr(tmp_ptr, "content-length: ");
        if (tmp_ptr == NULL)
        {
            esplog.trace("websvr::parse_http_request - didn't find any Content-Length\n");
        }
    }
    if (tmp_ptr != NULL)
    {
        tmp_ptr += 16;
        end_ptr = (char *)os_strstr(tmp_ptr, "\r\n");
        if (end_ptr == NULL)
        {
            esplog.error("websvr::parse_http_request - cannot find Content-Length value\n");
            return;
        }
        len = end_ptr - tmp_ptr;
        Heap_chunk tmp_str(len + 1);
        if (tmp_str.ref == NULL)
        {
            esplog.error("websvr::parse_http_request - not enough heap memory\n");
            return;
        }
        os_strncpy(tmp_str.ref, tmp_ptr, len);
        parsed_req->h_content_len = atoi(tmp_str.ref);
    }
}

class Html_pending_req
{
public:
    Html_pending_req();
    ~Html_pending_req();
    struct espconn *p_espconn;
    char *request;
    int content_len;
    int content_received;
};

ICACHE_FLASH_ATTR Html_pending_req::Html_pending_req()
{
    p_espconn = NULL;
    request = NULL;
    content_len = 0;
    content_received = 0;
}

ICACHE_FLASH_ATTR Html_pending_req::~Html_pending_req()
{
    if (request)
        delete[] request;
}

List<Html_pending_req> *pending_requests;

static void ICACHE_FLASH_ATTR save_pending_request(void *arg, char *precdata, unsigned short length, Html_parsed_req *parsed_req)
{
    Html_pending_req *pending_req = new Html_pending_req;
    if (pending_req == NULL)
    {
        esplog.error("websvr::save_pending_request - not enough heap memory [%d]\n", sizeof(Html_pending_req));
        return;
    }
    // total expected message length
    int msg_len = length + (parsed_req->h_content_len - parsed_req->content_len);
    pending_req->request = new char[msg_len + 1];
    if (pending_req->request == NULL)
    {
        esplog.error("websvr::save_pending_request - not enough heap memory [%d]\n", msg_len);
        delete pending_req;
        return;
    }
    pending_req->p_espconn = (struct espconn *)arg;
    os_strncpy(pending_req->request, precdata, length);
    pending_req->content_len = parsed_req->h_content_len;
    pending_req->content_received = parsed_req->content_len;
    List_err err = pending_requests->push_back(pending_req);
    if (err != list_ok)
    {
        esplog.error("websvr::save_pending_request - cannot save pending request\n");
        delete pending_req;
        return;
    }
}

static void webserver_recv(void *arg, char *precdata, unsigned short length);

static void ICACHE_FLASH_ATTR check_for_pending_requests(struct espconn *p_espconn, char *msg)
{
    // look for a pending request on p_espconn
    Html_pending_req *p_p_req = pending_requests->front();
    while (p_p_req)
    {
        if (p_p_req->p_espconn == p_espconn)
            break;
        p_p_req = pending_requests->next();
    }
    if (p_p_req == NULL)
    {
        esplog.error("websvr::check_for_pending_requests - cannot find pending request on espconn %X\n", p_espconn);
        return;
    }
    // add the received message part
    char *str_ptr = p_p_req->request + os_strlen(p_p_req->request);
    os_strncpy(str_ptr, msg, os_strlen(msg));
    p_p_req->content_received += os_strlen(msg);
    // check if the message is completed
    if (p_p_req->content_len == p_p_req->content_received)
    {
        webserver_recv((void *)p_espconn, p_p_req->request, os_strlen(p_p_req->request));
        pending_requests->remove();
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

    if (!parsed_req.no_header_message && (parsed_req.h_content_len > parsed_req.content_len))
    {
        esplog.debug("Websvr::webserver_recv - message has been splitted waiting for completion ...\n");
        save_pending_request(arg, precdata, length, &parsed_req);
        return;
    }
    if (parsed_req.no_header_message)
    {
        esplog.debug("Websvr::webserver_recv - No header message\n");
        check_for_pending_requests(ptr_espconn, parsed_req.req_content);
        return;
    }
    if (parsed_req.url == NULL)
    {
        esplog.debug("Websvr::webserver_recv - Empty url\n");
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

void ICACHE_FLASH_ATTR Websvr::init(void)
{
    esplog.all("Websvr::init\n");

    // setup specific controllers timer
    init_controllers();

    pending_send = new Queue<struct http_response>(8);
    pending_response = new Queue<struct http_split_response>(4);
    pending_requests = new List<Html_pending_req>(4, delete_content);

    // setup the default response buffer size
    m_send_response_max_size = 256;
}

void ICACHE_FLASH_ATTR Websvr::start(uint32 port)
{
    esplog.all("Websvr::start\n");

    // setup sdk TCP variables
    m_esp_conn.type = ESPCONN_TCP;
    m_esp_conn.state = ESPCONN_NONE;
    m_esp_conn.proto.tcp = &m_esptcp;
    m_esp_conn.proto.tcp->local_port = port;
    espconn_regist_connectcb(&m_esp_conn, webserver_listen);
    espconn_accept(&m_esp_conn);

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
    struct http_response *p_send = pending_send->front();
    while (p_send)
    {
        delete[] p_send->msg;
        delete p_send;
        pending_send->pop();
        p_send = pending_send->front();
    }
    struct http_split_response *p_split = pending_response->front();
    while (p_split)
    {
        delete[] p_split->content;
        delete p_split;
        pending_response->pop();
        p_split = pending_response->front();
    }
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