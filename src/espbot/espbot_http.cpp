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

#include "espbot.hpp"
#include "espbot_diagnostic.hpp"
#include "espbot_event_codes.h"
#include "espbot_global.hpp"
#include "espbot_http.hpp"
#include "espbot_http_routes.hpp"
#include "espbot_json.hpp"
#include "espbot_list.hpp"
#include "espbot_mem_mon.hpp"
#include "espbot_queue.hpp"
#include "espbot_utils.hpp"

static int http_msg_max_size;

void set_http_msg_max_size(int size)
{
    http_msg_max_size = size;
}

int get_http_msg_max_size(void)
{
    return http_msg_max_size;
}

const char *code_msg(int code)
{
    switch (code)
    {
    case HTTP_OK:
        return f_str("OK");
    case HTTP_CREATED:
        return f_str("Created");
    case HTTP_ACCEPTED:
        return f_str("Accepted");
    case HTTP_BAD_REQUEST:
        return f_str("Bad Request");
    case HTTP_UNAUTHORIZED:
        return f_str("Unauthorized");
    case HTTP_FORBIDDEN:
        return f_str("Forbidden");
    case HTTP_NOT_FOUND:
        return f_str("Not Found");
    case HTTP_CONFLICT:
        return f_str("Conflict");
    case HTTP_SERVER_ERROR:
        return f_str("Internal Server Error");
    default:
        return f_str("");
    }
}

char *json_error_msg(int code, const char *msg)
{
    Heap_chunk err_msg(54 + 3 + 22 + os_strlen(msg), dont_free);
    if (err_msg.ref)
    {
        fs_sprintf(err_msg.ref,
                   "{\"error\":{\"code\": %d,\"message\": \"%s\",\"reason\": \"%s\"}}",
                   code, code_msg(code), msg);
        return err_msg.ref;
    }
    else
    {
        esp_diag.error(HTTP_JSON_ERROR_MSG_HEAP_EXHAUSTED, (56 + os_strlen(msg)));
        ERROR("json_error_msg heap exhausted %d", (56 + os_strlen(msg)));
        return NULL;
    }
}

Http_header::Http_header()
{
    m_content_type = NULL;
    m_acrh = NULL;
    m_origin = NULL;
}

Http_header::~Http_header()
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

// DEBUG
// static void print_queue(Queue<struct http_send> *qq)
// {
//     os_printf("queue start\n");
//     struct http_send *ptr = qq->front();
//     while (ptr)
//     {
//         os_printf("e %X, o %d, l %d\n", ptr->p_espconn, ptr->order, ptr->msg_len);
//         ptr = qq->next();
//     }
//     os_printf("queue end\n");
// }
// 
// static void print_split_queue(Queue<struct http_split_send> *qq)
// {
//     os_printf("queue split start\n");
//     struct http_split_send *ptr = qq->front();
//     while (ptr)
//     {
//         os_printf("e %X, o %d, s %d, t %d\n", ptr->p_espconn, ptr->order, ptr->content_size, ptr->content_transferred);
//         ptr = qq->next();
//     }
//     os_printf("queue split end\n");
// }

static Queue<struct http_send> *pending_send;
static char *send_buffer;
static bool esp_busy_sending_data = false;

static os_timer_t clear_busy_sending_data_timer;

static void clear_busy_sending_data(void *arg)
{
    esp_diag.debug(HTTP_CLEAR_BUSY_SENDING_DATA);
    DEBUG("clear_busy_sending_data");
    // something went wrong and this timeout was triggered
    // clear the flag, the buffer and trigger a check of the pending responses queue
    os_timer_disarm(&clear_busy_sending_data_timer);
    if (send_buffer)
    {
        delete[] send_buffer;
        send_buffer = NULL;
    }
    esp_busy_sending_data = false;
    system_os_post(USER_TASK_PRIO_0, SIG_HTTP_CHECK_PENDING_RESPONSE, '0');
}

void clean_pending_send(struct espconn *p_espconn)
{
    system_soft_wdt_feed();
    // clear the pending send queue
    TRACE("cleaning pending send on esponn %X", p_espconn);
    struct http_send *p_pending_send = pending_send->front();
    while (p_pending_send)
    {
        if (p_pending_send->p_espconn == p_espconn)
        {
            pending_send->pop();
            delete[] p_pending_send->msg;
            delete p_pending_send;
        }
        p_pending_send = pending_send->front();
    }
    system_soft_wdt_feed();
    // clear the split send queue
    struct http_split_send *p_pending_response = pending_split_send->front();
    while (p_pending_response)
    {
        if (p_pending_response->p_espconn == p_espconn)
        {
            pending_split_send->pop();
            delete[] p_pending_response->content;
            delete p_pending_response;
        }
        p_pending_response = pending_split_send->front();
    }
}

void http_check_pending_send(void)
{
    ALL("http_check_pending_send");
    if (espwebsvr.get_status() == down)
    {
        // meanwhile the server went down
        TRACE("http_check_pending_send - clearing pending send and response queues");
        // clear the pending send queue
        struct http_send *p_pending_send = pending_send->front();
        while (p_pending_send)
        {
            pending_send->pop();
            delete[] p_pending_send->msg;
            delete p_pending_send;
            p_pending_send = pending_send->front();
        }
        // clear the split send queue
        struct http_split_send *p_pending_response = pending_split_send->front();
        while (p_pending_response)
        {
            pending_split_send->pop();
            delete[] p_pending_response->content;
            delete p_pending_response;
            p_pending_response = pending_split_send->front();
        }
        return;
    }
    // the server is up!
    // check pending send queue
    struct http_send *p_pending_send = pending_send->front();
    if (p_pending_send)
    {
        TRACE("http_check_pending_send pending send on espconn: %X, len %d",
              p_pending_send->p_espconn, p_pending_send->msg_len);
        // call http_send_buffer only if espconn_send is not busy sending data
        // this will keep the pending send queue properly ordered
        ETS_INTR_LOCK();
        if (!esp_busy_sending_data)
        {
            http_send_buffer(p_pending_send->p_espconn, p_pending_send->order, p_pending_send->msg, p_pending_send->msg_len);
            // the send procedure will clear the buffer so just delete the http_send
            delete p_pending_send;
            pending_send->pop();
        } // DEBUG
        ETS_INTR_UNLOCK();
        // DEBUG
        // print_queue(pending_send);

        // a pending response was found
        // wait for next pending response check so skip any other code
        return;
    }
    // no pending send was found
    // check other pending actions (such as long messages that required to be split)
    struct http_split_send *p_pending_response = pending_split_send->front();
    if (p_pending_response)
    {
        // DEBUG
        // print_split_queue(pending_split_send);
        TRACE("http_check_pending_send pending response on espconn: %X, content_size: %d, content transferred %d, action_function %X",
              p_pending_response->p_espconn,
              p_pending_response->content_size,
              p_pending_response->content_transferred,
              p_pending_response->action_function);
        p_pending_response->action_function(p_pending_response);
        // don't free the content yet
        delete p_pending_response;
        pending_split_send->pop();
        // serving just one pending_split_send, so that just one espconn_send is engaged
        // next one will be triggered by a espconn_send completion
    }
}

void http_sentcb(void *arg)
{
    ALL("http_sentcb");
    struct espconn *ptr_espconn = (struct espconn *)arg;
    // clear the flag and the timeout timer
    os_timer_disarm(&clear_busy_sending_data_timer);
    // clear the message_buffer
    if (send_buffer)
    {
        TRACE("http_sentcb: deleting send_buffer %X", send_buffer);
        delete[] send_buffer;
        send_buffer = NULL;
    }
    esp_busy_sending_data = false;
    system_os_post(USER_TASK_PRIO_0, SIG_HTTP_CHECK_PENDING_RESPONSE, '0');
    system_soft_wdt_feed();
}

static void push_pending_send(struct espconn *p_espconn, int order, char *msg, int len)
{
    struct http_send *response_data = new struct http_send;
    espmem.stack_mon();
    if (response_data)
    {
        response_data->p_espconn = p_espconn;
        response_data->order = order;
        response_data->msg = msg;
        response_data->msg_len = len;
        Queue_err result = pending_send->push(response_data);
        // DEBUG
        // print_queue(pending_send);
        if (result == Queue_full)
        {
            delete[] msg;
            delete response_data;
            esp_diag.error(HTTP_PUSH_PENDING_SEND_QUEUE_FULL);
            ERROR("push_pending_send: full pending send queue");
        }
    }
    else
    {
        esp_diag.error(HTTP_PUSH_PENDING_SEND_HEAP_EXHAUSTED, sizeof(struct http_send));
        ERROR("push_pending_send heap exhausted %d", sizeof(struct http_send));
    }
}

//
// won't check the length of the sent message
//
void http_send_buffer(struct espconn *p_espconn, int order, char *msg, int len)
{
    // Profiler ret_file("http_send_buffer");
    ETS_INTR_LOCK();
    if (esp_busy_sending_data) // previous espconn_send not completed yet
    {
        ETS_INTR_UNLOCK();
        TRACE("http_send_buffer - espconn_send busy");
        push_pending_send(p_espconn, order, msg, len);
    }
    else // previous espconn_send completed
    {
        esp_busy_sending_data = true;
        ETS_INTR_UNLOCK();
        // check if espconn in use
        send_buffer = msg;
        if (!((p_espconn->state == ESPCONN_CONNECT) || (p_espconn->state == ESPCONN_READ)))
        {
            TRACE("espconn unexpected state, won't send any msg");
            if (send_buffer)
            {
                delete[] send_buffer;
                send_buffer = NULL;
            }
            esp_busy_sending_data = false;
            system_os_post(USER_TASK_PRIO_0, SIG_HTTP_CHECK_PENDING_RESPONSE, '0');
            system_soft_wdt_feed();
            return;
        }
        // send the buffer
        DEBUG("espconn_send on espconn: %X, state: %d, msg: %s", p_espconn, p_espconn->state, msg);
        // set a timeout timer for clearing the esp_busy_sending_data in case something goes wrong
        os_timer_disarm(&clear_busy_sending_data_timer);
        os_timer_setfn(&clear_busy_sending_data_timer, (os_timer_func_t *)clear_busy_sending_data, NULL);
        os_timer_arm(&clear_busy_sending_data_timer, 5000, 0);

        sint8 res = espconn_send(p_espconn, (uint8 *)send_buffer, len);
        espmem.stack_mon();
        if (res != 0)
        {
            os_timer_disarm(&clear_busy_sending_data_timer);
            esp_diag.error(HTTP_SEND_BUFFER_ERROR, res);
            ERROR("espconn_send error %d", res);
            if (send_buffer)
            {
                delete[] send_buffer;
                send_buffer = NULL;
            }
            esp_busy_sending_data = false;
            system_os_post(USER_TASK_PRIO_0, SIG_HTTP_CHECK_PENDING_RESPONSE, '0');
        }
        // esp_free(send_buffer); // http_sentcb will free it
    }
    system_soft_wdt_feed();
}

bool http_espconn_in_use(struct espconn *p_espconn)
{
    if ((p_espconn->state == ESPCONN_CONNECT) || (p_espconn->state == ESPCONN_READ) || (p_espconn->state == ESPCONN_WRITE))
        return true;
    else
        return false;
}

void http_response(struct espconn *p_espconn, int code, char *content_type, const char *msg, bool free_msg)
{
    TRACE("response on espconn: %X, code %d, msg len %d", p_espconn, code, os_strlen(msg));
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
        esp_diag.error(HTTP_RESPONSE_HEAP_EXHAUSTED, header_len);
        ERROR("http_response heap exhausted %d", header_len);
        return;
    }
    os_sprintf(msg_header.ref, "HTTP/1.1 %d %s\r\nServer: espbot\r\n"
                               "Content-Type: %s\r\n"
                               "Content-Length: %d\r\n"
                               "Access-Control-Allow-Origin: *\r\n\r\n",
               code, code_msg(code), content_type, os_strlen(msg));
    // send separately the header from the content
    // to avoid allocating twice the memory for the message
    // especially very large ones
    http_send_buffer(p_espconn, 0, msg_header.ref, os_strlen(msg_header.ref));
    // when there is no message that's all
    if (os_strlen(msg) == 0)
        return;
    if (free_msg)
    {
        http_send(p_espconn, (char *)msg, os_strlen(msg));
    }
    else
    {
        // response message is not allocated on heap
        // copy it to a buffer
        Heap_chunk msg_short(os_strlen(msg), dont_free);
        if (msg_header.ref)
        {
            os_strcpy(msg_short.ref, msg);
            http_send(p_espconn, msg_short.ref, os_strlen(msg));
        }
        else
        {
            esp_diag.error(HTTP_RESPONSE_HEAP_EXHAUSTED, os_strlen(msg));
            ERROR("http_response heap exhausted %d", os_strlen(msg));
        }
    }
    espmem.stack_mon();
}

Queue<struct http_split_send> *pending_split_send;

static void send_remaining_msg(struct http_split_send *p_sr)
{
    ALL("send_remaining_msg");
    if (!http_espconn_in_use(p_sr->p_espconn))
    {
        TRACE("send_remaining_msg espconn %X state %d, abort", p_sr->p_espconn, p_sr->p_espconn->state);
        delete[] p_sr->content;
        system_os_post(USER_TASK_PRIO_0, SIG_HTTP_CHECK_PENDING_RESPONSE, '0');
        return;
    }
    if ((p_sr->content_size - p_sr->content_transferred) > http_msg_max_size)
    {
        // the message is bigger than response_max_size
        // will split the message
        int buffer_size = http_msg_max_size;
        Heap_chunk buffer(buffer_size + 1, dont_free);
        if (buffer.ref)
        {
            struct http_split_send *p_pending_response = new struct http_split_send;
            if (p_pending_response)
            {
                os_strncpy(buffer.ref, p_sr->content + p_sr->content_transferred, buffer_size);
                // setup the remaining message
                p_pending_response->p_espconn = p_sr->p_espconn;
                p_pending_response->order = p_sr->order + 1;
                p_pending_response->content = p_sr->content;
                p_pending_response->content_size = p_sr->content_size;
                p_pending_response->content_transferred = p_sr->content_transferred + buffer_size;
                p_pending_response->action_function = send_remaining_msg;
                Queue_err result = pending_split_send->push(p_pending_response);
                if (result == Queue_full)
                {
                    delete p_pending_response;
                    esp_diag.error(HTTP_SEND_REMAINING_MSG_RES_QUEUE_FULL);
                    ERROR("send_remaining_msg full pending response queue");
                }
                TRACE("send_remaining_msg on espconn %X, msg (splitted) len %d",
                      p_sr->p_espconn,
                      buffer_size);
                http_send_buffer(p_sr->p_espconn, p_sr->order, buffer.ref, buffer_size);
            }
            else
            {
                esp_diag.error(HTTP_SEND_REMAINING_MSG_HEAP_EXHAUSTED, sizeof(struct http_split_send));
                ERROR("send_remaining_msg heap exhausted %d", sizeof(struct http_split_send));
                delete[] buffer.ref;
                delete[] p_sr->content;
                // there will be no send, so trigger a check of pending send
                system_os_post(USER_TASK_PRIO_0, SIG_HTTP_CHECK_PENDING_RESPONSE, '0');
            }
        }
        else
        {
            esp_diag.error(HTTP_SEND_REMAINING_MSG_HEAP_EXHAUSTED, buffer_size + 1);
            ERROR("send_remaining_msg heap exhausted %d", buffer_size + 1);
            delete[] p_sr->content;
            // there will be no send, so trigger a check of pending send
            system_os_post(USER_TASK_PRIO_0, SIG_HTTP_CHECK_PENDING_RESPONSE, '0');
        }
    }
    else
    {
        // this is the last piece of the message
        int buffer_size = p_sr->content_size - p_sr->content_transferred;
        Heap_chunk buffer((buffer_size + 1), dont_free);
        if (buffer.ref)
        {
            os_strncpy(buffer.ref, p_sr->content + p_sr->content_transferred, buffer_size);
            TRACE("send_remaining_msg on espconn %X, msg (last piece) len: %d",
                  p_sr->p_espconn,
                  buffer_size);
            http_send_buffer(p_sr->p_espconn, p_sr->order, buffer.ref, buffer_size);
        }
        else
        {
            esp_diag.error(HTTP_SEND_REMAINING_MSG_HEAP_EXHAUSTED, (buffer_size + 1));
            ERROR("send_remaining_msg heap exhausted %d", (buffer_size + 1));
            // there will be no send, so trigger a check of pending send
            system_os_post(USER_TASK_PRIO_0, SIG_HTTP_CHECK_PENDING_RESPONSE, '0');
        }
        delete[] p_sr->content;
    }
}

//
// will split the message when the length is greater than http response_max_size
//
void http_send(struct espconn *p_espconn, char *msg, int msg_len)
{
    ALL("http_send");
    // Profiler ret_file("http_send");
    if (msg_len > http_msg_max_size)
    {
        // the message is bigger than response_max_size
        // will split the message
        int buffer_size = http_msg_max_size;
        Heap_chunk buffer((buffer_size + 1), dont_free);
        if (buffer.ref)
        {
            struct http_split_send *p_pending_response = new struct http_split_send;
            if (p_pending_response)
            {
                os_strncpy(buffer.ref, msg, buffer_size);
                // setup the remaining message
                p_pending_response->p_espconn = p_espconn;
                p_pending_response->order = 2;
                p_pending_response->content = msg;
                p_pending_response->content_size = msg_len;
                p_pending_response->content_transferred = buffer_size;
                p_pending_response->action_function = send_remaining_msg;
                Queue_err result = pending_split_send->push(p_pending_response);
                if (result == Queue_full)
                {
                    delete p_pending_response;
                    esp_diag.error(HTTP_SEND_RES_QUEUE_FULL);
                    ERROR("http_send full pending response queue");
                }
                TRACE("http_send on espconn: %X, msg (splitted) len: %d", p_espconn, buffer_size);
                http_send_buffer(p_espconn, 1, buffer.ref, buffer_size);
            }
            else
            {
                esp_diag.error(HTTP_SEND_HEAP_EXHAUSTED, sizeof(struct http_split_send));
                ERROR("http_send heap exhausted %d", sizeof(struct http_split_send));
                delete[] buffer.ref;
                delete[] msg;
            }
        }
        else
        {
            esp_diag.error(HTTP_SEND_HEAP_EXHAUSTED, (buffer_size + 1));
            ERROR("http_send heap exhausted %d", (buffer_size + 1));
            delete[] msg;
        }
    }
    else
    {
        // no need to split the message, just send it
        TRACE("http_send on espconn: %X, msg (full) len: %d", p_espconn, msg_len);
        http_send_buffer(p_espconn, 1, msg, msg_len);
    }
}

char *http_format_header(class Http_header *p_header)
{
    ALL("http_format_header");
    // allocate a buffer
    // HTTP...        ->  37 + 3 + 22 =  62
    // Content-Type   ->  17 + 24     =  41
    // Content-Length ->  22 + 5      =  27
    // Content-Range  ->  32 + 15     =  47
    // Pragma         ->  24          =  24
    //                                = 201
    int header_length = 201;
    if (p_header->m_acrh)
    {
        header_length += 37; // Access-Control-Request-Headers string format
        header_length += os_strlen(p_header->m_acrh);
    }

    if (p_header->m_origin)
    {
        header_length += os_strlen(f_str("Access-Control-Allow-Methods: GET,POST,PUT,DELETE,OPTIONS\r\n"));
        header_length += 34; // Origin string format
        header_length += os_strlen(p_header->m_origin);
    }

    Heap_chunk header_msg(header_length, dont_free);
    if (header_msg.ref)
    {
        // setup the header
        char *ptr = header_msg.ref;
        fs_sprintf(ptr, "HTTP/1.1 %d %s\r\nServer: espbot\r\n",
                   p_header->m_code, code_msg(p_header->m_code));
        ptr = ptr + os_strlen(ptr);
        fs_sprintf(ptr, "Content-Type: %s\r\n", p_header->m_content_type);
        ptr = ptr + os_strlen(ptr);
        if (p_header->m_content_range_total > 0)
        {
            fs_sprintf(ptr, "Content-Range: bytes %d-%d/%d\r\n", p_header->m_content_range_total, p_header->m_content_range_total, p_header->m_content_range_total);
            ptr = ptr + os_strlen(ptr);
        }
        fs_sprintf(ptr, "Content-Length: %d\r\n", p_header->m_content_length);
        ptr = ptr + os_strlen(ptr);
        // os_sprintf(ptr, "Date: Wed, 28 Nov 2018 12:00:00 GMT\r\n");
        // os_printf("---->msg: %s\n", msg.ref);
        if (p_header->m_origin)
        {
            fs_sprintf(ptr, "Access-Control-Allow-Origin: %s\r\n", p_header->m_origin);
            ptr = ptr + os_strlen(ptr);
            fs_sprintf(ptr, "Access-Control-Allow-Methods: GET,POST,PUT,DELETE,OPTIONS\r\n");
            ptr = ptr + os_strlen(ptr);
        }
        if (p_header->m_acrh)
        {
            fs_sprintf(ptr, "Access-Control-Allow-Headers: Content-Type,%s\r\n", p_header->m_acrh);
            ptr = ptr + os_strlen(ptr);
        }
        fs_sprintf(ptr, "Pragma: no-cache\r\n\r\n");
        return header_msg.ref;
    }
    else
    {
        esp_diag.error(HTTP_FORMAT_HEADER_HEAP_EXHAUSTED, header_length);
        ERROR("http_format_header heap exhausted %d", header_length);
        return NULL;
    }
}

// end of HTTP responding

//
// HTTP Receiving:
//

Http_parsed_req::Http_parsed_req()
{
    no_header_message = false;
    req_method = HTTP_UNDEFINED;
    acrh = NULL;
    origin = NULL;
    url = NULL;
    content_len = 0;
    req_content = NULL;
}

Http_parsed_req::~Http_parsed_req()
{
    if (acrh)
        delete[] acrh;
    if (origin)
        delete[] origin;
    if (url)
        delete[] url;
    if (req_content)
        delete[] req_content;
}

void http_parse_request(char *req, unsigned short length, Http_parsed_req *parsed_req)
{
    ALL("http_parse_request");
    char *tmp_ptr = req;
    char *end_ptr = NULL;
    espmem.stack_mon();
    int len = 0;

    if (tmp_ptr == NULL)
    {
        esp_diag.error(HTTP_PARSE_REQUEST_CANNOT_PARSE_EMPTY_MSG);
        ERROR("http_parse_request - empty message");
        return;
    }
    if (os_strncmp(tmp_ptr, f_str("GET "), 4) == 0)
    {
        parsed_req->req_method = HTTP_GET;
        tmp_ptr += 4;
    }
    else if (os_strncmp(tmp_ptr, f_str("POST "), 5) == 0)
    {
        parsed_req->req_method = HTTP_POST;
        tmp_ptr += 5;
    }
    else if (os_strncmp(tmp_ptr, f_str("PUT "), 4) == 0)
    {
        parsed_req->req_method = HTTP_PUT;
        tmp_ptr += 4;
    }
    else if (os_strncmp(tmp_ptr, f_str("PATCH "), 6) == 0)
    {
        parsed_req->req_method = HTTP_PATCH;
        tmp_ptr += 6;
    }
    else if (os_strncmp(tmp_ptr, f_str("DELETE "), 7) == 0)
    {
        parsed_req->req_method = HTTP_DELETE;
        tmp_ptr += 7;
    }
    else if (os_strncmp(tmp_ptr, f_str("OPTIONS "), 8) == 0)
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
        // parsed_req->content_len = os_strlen(tmp_ptr);
        parsed_req->content_len = length;
        parsed_req->req_content = new char[parsed_req->content_len + 1];
        if (parsed_req->req_content == NULL)
        {
            esp_diag.error(HTTP_PARSE_REQUEST_HEAP_EXHAUSTED, (parsed_req->content_len + 1));
            ERROR("http_parse_request heap exhausted %d", (parsed_req->content_len + 1));
            return;
        }
        os_memcpy(parsed_req->req_content, tmp_ptr, parsed_req->content_len);
        return;
    }

    // this is a standard request with header

    // checkout url
    end_ptr = (char *)os_strstr(tmp_ptr, f_str(" HTTP"));
    if (end_ptr == NULL)
    {
        esp_diag.error(HTTP_PARSE_REQUEST_CANNOT_FIND_HTTP_TOKEN);
        ERROR("http_parse_request cannot find HTTP token");
        return;
    }
    len = end_ptr - tmp_ptr;
    parsed_req->url = new char[len + 1];
    if (parsed_req->url == NULL)
    {
        esp_diag.error(HTTP_PARSE_REQUEST_HEAP_EXHAUSTED, (len + 1));
        ERROR("http_parse_request heap exhausted %d", (len + 1));
        return;
    }
    os_memcpy(parsed_req->url, tmp_ptr, len);

    // checkout Access-Control-Request-Headers
    tmp_ptr = req;
    tmp_ptr = (char *)os_strstr(tmp_ptr, f_str("Access-Control-Request-Headers: "));
    if (tmp_ptr == NULL)
    {
        tmp_ptr = req;
        tmp_ptr = (char *)os_strstr(tmp_ptr, f_str("access-control-request-headers: "));
    }
    if (tmp_ptr != NULL)
    {
        tmp_ptr += 32;
        end_ptr = (char *)os_strstr(tmp_ptr, f_str("\r\n"));
        if (end_ptr == NULL)
        {
            esp_diag.error(HTTP_PARSE_REQUEST_CANNOT_FIND_ACC_CTRL_REQ_HEADERS);
            ERROR("http_parse_request cannot find Access-Control-Request-Headers");
            return;
        }
        len = end_ptr - tmp_ptr;
        parsed_req->acrh = new char[len + 1];
        if (parsed_req->acrh == NULL)
        {
            esp_diag.error(HTTP_PARSE_REQUEST_HEAP_EXHAUSTED, (len + 1));
            ERROR("http_parse_request heap exhausted %d", (len + 1));
            return;
        }
        os_strncpy(parsed_req->acrh, tmp_ptr, len);
    }

    // checkout Origin
    tmp_ptr = req;
    tmp_ptr = (char *)os_strstr(tmp_ptr, f_str("Origin: "));
    if (tmp_ptr == NULL)
    {
        tmp_ptr = req;
        tmp_ptr = (char *)os_strstr(tmp_ptr, f_str("origin: "));
    }
    if (tmp_ptr != NULL)
    {
        tmp_ptr += 8;
        end_ptr = (char *)os_strstr(tmp_ptr, f_str("\r\n"));
        if (end_ptr == NULL)
        {
            esp_diag.error(HTTP_PARSE_REQUEST_CANNOT_FIND_ORIGIN);
            ERROR("http_parse_request cannot find origin");
            return;
        }
        len = end_ptr - tmp_ptr;
        parsed_req->origin = new char[len + 1];
        if (parsed_req->origin == NULL)
        {
            esp_diag.error(HTTP_PARSE_REQUEST_HEAP_EXHAUSTED, (len + 1));
            ERROR("http_parse_request heap exhausted %d", (len + 1));
            return;
        }
        os_strncpy(parsed_req->origin, tmp_ptr, len);
    }

    // checkout for request content
    // and calculate the effective content length
    tmp_ptr = req;
    tmp_ptr = (char *)os_strstr(tmp_ptr, f_str("\r\n\r\n"));
    if (tmp_ptr == NULL)
    {
        esp_diag.error(HTTP_PARSE_REQUEST_CANNOT_FIND_CONTENT_START);
        ERROR("http_parse_request cannot find Content start");
        return;
    }
    tmp_ptr += 4;
    // parsed_req->content_len = os_strlen(tmp_ptr);
    parsed_req->content_len = length - (tmp_ptr - req);
    parsed_req->req_content = new char[parsed_req->content_len + 1];
    if (parsed_req->req_content == NULL)
    {
        esp_diag.error(HTTP_PARSE_REQUEST_HEAP_EXHAUSTED, (parsed_req->content_len + 1));
        ERROR("http_parse_request heap exhausted %d", (parsed_req->content_len + 1));
        return;
    }
    os_memcpy(parsed_req->req_content, tmp_ptr, parsed_req->content_len);

    // checkout Content-Length
    parsed_req->h_content_len = parsed_req->content_len;
    tmp_ptr = req;
    tmp_ptr = (char *)os_strstr(tmp_ptr, f_str("Content-Length: "));
    if (tmp_ptr == NULL)
    {
        tmp_ptr = req;
        tmp_ptr = (char *)os_strstr(tmp_ptr, f_str("content-length: "));
        if (tmp_ptr == NULL)
        {
            TRACE("http_parse_request didn't find any Content-Length");
        }
    }
    if (tmp_ptr != NULL)
    {
        tmp_ptr += 16;
        end_ptr = (char *)os_strstr(tmp_ptr, f_str("\r\n"));
        if (end_ptr == NULL)
        {
            esp_diag.error(HTTP_PARSE_REQUEST_CANNOT_FIND_CONTENT_LEN);
            ERROR("http_parse_request cannot find Content-Length value");
            return;
        }
        len = end_ptr - tmp_ptr;
        Heap_chunk tmp_str(len + 1);
        if (tmp_str.ref == NULL)
        {
            esp_diag.error(HTTP_PARSE_REQUEST_HEAP_EXHAUSTED, (len + 1));
            ERROR("http_parse_request heap exhausted %d", (len + 1));
            return;
        }
        os_strncpy(tmp_str.ref, tmp_ptr, len);
        parsed_req->h_content_len = atoi(tmp_str.ref);
    }
}

class Http_pending_req
{
public:
    Http_pending_req();
    ~Http_pending_req();
    struct espconn *p_espconn;
    char *request;
    int content_len;
    int content_received;
};

Http_pending_req::Http_pending_req()
{
    p_espconn = NULL;
    request = NULL;
    content_len = 0;
    content_received = 0;
}

Http_pending_req::~Http_pending_req()
{
    if (request)
        delete[] request;
}

static List<Http_pending_req> *pending_requests;

void http_save_pending_request(void *arg, char *precdata, unsigned short length, Http_parsed_req *parsed_req)
{
    ALL("http_save_pending_request");
    Http_pending_req *pending_req = new Http_pending_req;
    if (pending_req == NULL)
    {
        esp_diag.error(HTTP_SAVE_PENDING_REQUEST_HEAP_EXHAUSTED, sizeof(Http_pending_req));
        ERROR("http_save_pending_request heap exhausted %d", sizeof(Http_pending_req));
        return;
    }
    // total expected message length
    int msg_len = length + (parsed_req->h_content_len - parsed_req->content_len);
    pending_req->request = new char[msg_len + 1];
    if (pending_req->request == NULL)
    {
        esp_diag.error(HTTP_SAVE_PENDING_REQUEST_HEAP_EXHAUSTED, msg_len);
        ERROR("http_save_pending_request heap exhausted %d", msg_len);
        delete pending_req;
        return;
    }
    pending_req->p_espconn = (struct espconn *)arg;
    os_memcpy(pending_req->request, precdata, length);
    // os_strncpy(pending_req->request, precdata, length);
    // pending_req->content_len = parsed_req->h_content_len;
    // pending_req->content_received = parsed_req->content_len;
    pending_req->content_len = msg_len;
    pending_req->content_received = length;
    List_err err = pending_requests->push_back(pending_req);
    if (err != list_ok)
    {
        esp_diag.error(HTTP_SAVE_PENDING_REQUEST_CANNOT_SAVE_REQ);
        ERROR("http_save_pending_request cannot save pending request");
        delete pending_req;
        return;
    }
}

void http_check_pending_requests(struct espconn *p_espconn, char *new_msg, unsigned short length, void (*msg_complete)(void *, char *, unsigned short))
{
    ALL("http_check_pending_requests");
    // look for a pending request on p_espconn
    Http_pending_req *p_p_req = pending_requests->front();
    while (p_p_req)
    {
        if (p_p_req->p_espconn == p_espconn)
            break;
        p_p_req = pending_requests->next();
    }
    if (p_p_req == NULL)
    {
        esp_diag.error(HTTP_CHECK_PENDING_REQUESTS_CANNOT_FIND_REQ, (uint32)p_espconn);
        ERROR("http_check_pending_requests cannot find pending req for espconn %X", p_espconn);
        return;
    }
    // add the received message part
    char *str_ptr = p_p_req->request + p_p_req->content_received;
    os_memcpy(str_ptr, new_msg, length);
    // os_strncpy(str_ptr, new_msg, os_strlen(new_msg));
    p_p_req->content_received += length;
    // check if the message is completed
    if (p_p_req->content_len == p_p_req->content_received)
    {
        msg_complete((void *)p_espconn, p_p_req->request, p_p_req->content_len);
        pending_requests->remove();
    }
}

void clean_pending_responses(struct espconn *p_espconn)
{
    ALL("http_clean_pending_responses");
    // look for a pending request on p_espconn
    Http_pending_req *p_p_req = pending_requests->front();
    while (p_p_req)
    {
        if (p_p_req->p_espconn == p_espconn)
        {
            esp_diag.debug(HTTP_DELETED_PENDING_RESPONSE);
            WARN("cleaning pending responses for espconn %X", p_espconn);
            pending_requests->remove();
            // one element removed from list, better restart from front
            p_p_req = pending_requests->front();
        }
        // need to check p_p_req because of pending_requests->front() maybe was called ...
        if (p_p_req)
            p_p_req = pending_requests->next();
    }
}

class Http_pending_res
{
public:
    Http_pending_res();
    ~Http_pending_res();
    struct espconn *p_espconn;
    char *response;
    int content_len;
    int content_received;
};

Http_pending_res::Http_pending_res()
{
    p_espconn = NULL;
    response = NULL;
    content_len = 0;
    content_received = 0;
}

Http_pending_res::~Http_pending_res()
{
    if (response)
        delete[] response;
}

static List<Http_pending_res> *pending_responses;

// void print_pending_response(void)
// {
//     os_printf("---------> pending responses\n");
//     Http_pending_res *p_p_res = pending_responses->front();
//     int ii=0;
//     while (p_p_res)
//     {
//         os_printf("---------> response\n");
//         os_printf("                     espconn: %X\n", p_p_res->p_espconn);
//         os_printf("                    response: %s\n", p_p_res->response);
//         os_printf("                 content_len: %d\n", p_p_res->content_len);
//         os_printf("            content_received: %d\n", p_p_res->content_received);
//         p_p_res = pending_responses->next();
//         ii++;
//         if (ii>4)
//             break;
//     }
//     os_printf("---------> end\n");
// }

void http_save_pending_response(struct espconn *p_espconn, char *precdata, unsigned short length, Http_parsed_response *parsed_res)
{
    ALL("http_save_pending_response");
    Http_pending_res *pending_res = new Http_pending_res;
    if (pending_res == NULL)
    {
        esp_diag.error(HTTP_SAVE_PENDING_RESPONSE_HEAP_EXHAUSTED, sizeof(Http_pending_res));
        ERROR("http_save_pending_response heap exhausted %d", sizeof(Http_pending_res));
        return;
    }
    // total expected message length
    int msg_len = length + (parsed_res->h_content_len - parsed_res->content_len);
    pending_res->response = new char[msg_len + 1];
    if (pending_res->response == NULL)
    {
        esp_diag.error(HTTP_SAVE_PENDING_RESPONSE_HEAP_EXHAUSTED, msg_len);
        ERROR("http_save_pending_response heap exhausted %d", msg_len);
        delete pending_res;
        return;
    }
    pending_res->p_espconn = p_espconn;
    os_memcpy(pending_res->response, precdata, length);
    pending_res->content_len = msg_len;
    pending_res->content_received = length;
    List_err err = pending_responses->push_back(pending_res);
    if (err != list_ok)
    {
        esp_diag.error(HTTP_SAVE_PENDING_RESPONSE_CANNOT_SAVE_RES);
        ERROR("http_save_pending_response cannot save pending res");
        delete pending_res;
        return;
    }
}

void http_check_pending_responses(struct espconn *p_espconn, char *new_msg, unsigned short length, void (*msg_complete)(void *, char *, unsigned short))
{
    ALL("http_check_pending_responses");
    // look for a pending request on p_espconn
    Http_pending_res *p_p_res = pending_responses->front();
    while (p_p_res)
    {
        if (p_p_res->p_espconn == p_espconn)
            break;
        p_p_res = pending_responses->next();
    }
    if (p_p_res == NULL)
    {
        esp_diag.error(HTTP_CHECK_PENDING_RESPONSE_CANNOT_FIND_RES, (uint32)p_espconn);
        ERROR("http_check_pending_responses cannot find pending response for espconn %X", p_espconn);
        return;
    }
    // add the received message part
    char *str_ptr = p_p_res->response + p_p_res->content_received;
    os_memcpy(str_ptr, new_msg, length);
    p_p_res->content_received += length;
    // check if the message is completed
    if (p_p_res->content_len == p_p_res->content_received)
    {
        msg_complete((void *)p_espconn, p_p_res->response, p_p_res->content_len);
        pending_responses->remove();
    }
}

void http_init(void)
{
    // http_msg_max_size = 1024;
    http_msg_max_size = 1460;

    pending_send = new Queue<struct http_send>(16);
    pending_split_send = new Queue<struct http_split_send>(16);
    pending_requests = new List<Http_pending_req>(4, delete_content);
    pending_responses = new List<Http_pending_res>(4, delete_content);
}

void http_queues_clear(void)
{
    ALL("http_queues_clear");
    struct http_send *p_send = pending_send->front();
    while (p_send)
    {
        delete[] p_send->msg;
        delete p_send;
        pending_send->pop();
        p_send = pending_send->front();
    }
    struct http_split_send *p_split = pending_split_send->front();
    while (p_split)
    {
        delete[] p_split->content;
        delete p_split;
        pending_split_send->pop();
        p_split = pending_split_send->front();
    }
}

//
// HTTP parsing response
//

Http_parsed_response::Http_parsed_response()
{
    no_header_message = true;
    content_range_start = 0;
    content_range_end = 0;
    content_range_size = 0;
    h_content_len = 0;
    content_len = 0;
    body = NULL;
}

Http_parsed_response::~Http_parsed_response()
{
    if (body)
        delete[] body;
}

void http_parse_response(char *response, int length, Http_parsed_response *parsed_response)
{
    ALL("http_parse_response");
    char *tmp_ptr = response;
    char *end_ptr = NULL;
    char *tmp_str = NULL;
    int len = 0;
    espmem.stack_mon();

    if (tmp_ptr == NULL)
    {
        esp_diag.error(HTTP_PARSE_RESPONSE_CANNOT_PARSE_EMPTY_MSG);
        ERROR("http_parse_response empty message");
        return;
    }

    // looking for HTTP CODE
    tmp_ptr = response;
    tmp_ptr = (char *)os_strstr(tmp_ptr, f_str("HTTP"));
    if (tmp_ptr == NULL)
    {
        tmp_ptr = response;
        parsed_response->no_header_message = true;
    }
    else
    {
        parsed_response->no_header_message = false;
        tmp_ptr = (char *)os_strstr(tmp_ptr, f_str(" "));
        do
        {
            tmp_ptr++;
        } while (*tmp_ptr == ' ');
    }
    if (parsed_response->no_header_message)
    {
        parsed_response->content_len = length;
        parsed_response->body = new char[parsed_response->content_len + 1];
        if (parsed_response->body == NULL)
        {
            esp_diag.error(HTTP_PARSE_RESPONSE_HEAP_EXHAUSTED, (parsed_response->content_len + 1));
            ERROR("http_parse_response heap exhausted %d", (parsed_response->content_len + 1));
            return;
        }
        os_memcpy(parsed_response->body, tmp_ptr, parsed_response->content_len);
        return;
    }

    end_ptr = (char *)os_strstr(tmp_ptr, f_str(" "));
    if (end_ptr == NULL)
    {
        esp_diag.error(HTTP_PARSE_RESPONSE_CANNOT_FIND_HTTP_TOKEN);
        ERROR("http_parse_response - cannot find HTTP token");
        return;
    }
    len = end_ptr - tmp_ptr;
    {
        Heap_chunk tmp_str(len + 1);
        if (tmp_str.ref == NULL)
        {
            esp_diag.error(HTTP_PARSE_RESPONSE_HEAP_EXHAUSTED, (len + 1));
            ERROR("http_parse_response heap exhausted %d", (len + 1));
            return;
        }
        os_strncpy(tmp_str.ref, tmp_ptr, len);
        parsed_response->http_code = atoi(tmp_str.ref);
    }

    // now the content-length
    tmp_ptr = response;
    tmp_ptr = (char *)os_strstr(tmp_ptr, f_str("Content-Length: "));
    if (tmp_ptr == NULL)
    {
        TRACE("http_parse_response didn't find any Content-Length");
    }
    else
    {
        tmp_ptr += 16;
        end_ptr = (char *)os_strstr(tmp_ptr, f_str("\r\n"));
        if (end_ptr == NULL)
        {
            esp_diag.error(HTTP_PARSE_RESPONSE_CANNOT_FIND_CONTENT_LEN);
            ERROR("http_parse_response cannot find Content-Length value");
            return;
        }
        len = end_ptr - tmp_ptr;
        Heap_chunk tmp_str(len + 1);
        if (tmp_str.ref == NULL)
        {
            esp_diag.error(HTTP_PARSE_RESPONSE_HEAP_EXHAUSTED, (len + 1));
            ERROR("http_parse_response heap exhausted %d", (len + 1));
            return;
        }
        os_strncpy(tmp_str.ref, tmp_ptr, len);
        parsed_response->h_content_len = atoi(tmp_str.ref);
    }
    // now Content-Range (if any)
    tmp_ptr = response;
    tmp_ptr = (char *)os_strstr(tmp_ptr, f_str("Content-Range: "));
    if (tmp_ptr == NULL)
    {
        TRACE("http_parse_response didn't find any Content-Range");
    }
    else
    {
        tmp_ptr = (char *)os_strstr(tmp_ptr, f_str("bytes"));
        if (tmp_ptr == NULL)
        {
            esp_diag.error(HTTP_PARSE_RESPONSE_CANNOT_FIND_CONTENT_RANGE);
            ERROR("http_parse_response cannot find Content-Range value");
            return;
        }
        // range start
        tmp_ptr += os_strlen(f_str("bytes "));
        end_ptr = (char *)os_strstr(tmp_ptr, f_str("-"));
        if (end_ptr == NULL)
        {
            esp_diag.error(HTTP_PARSE_RESPONSE_CANNOT_FIND_RANGE_START);
            ERROR("http_parse_response cannot find range start");
            return;
        }
        len = end_ptr - tmp_ptr;
        {
            Heap_chunk tmp_str(len + 1);
            if (tmp_str.ref == NULL)
            {
                esp_diag.error(HTTP_PARSE_RESPONSE_HEAP_EXHAUSTED, (len + 1));
                ERROR("http_parse_response heap exhausted %d", (len + 1));
                return;
            }
            os_strncpy(tmp_str.ref, tmp_ptr, len);
            parsed_response->content_range_start = atoi(tmp_str.ref);
        }
        // range end
        tmp_ptr = end_ptr + 1;
        end_ptr = (char *)os_strstr(tmp_ptr, f_str("/"));
        if (end_ptr == NULL)
        {
            esp_diag.error(HTTP_PARSE_RESPONSE_CANNOT_FIND_RANGE_END);
            ERROR("http_parse_response cannot find range end");
            return;
        }
        len = end_ptr - tmp_ptr;
        {
            Heap_chunk tmp_str(len + 1);
            if (tmp_str.ref == NULL)
            {
                esp_diag.error(HTTP_PARSE_RESPONSE_HEAP_EXHAUSTED, (len + 1));
                ERROR("http_parse_response heap exhausted %d", (len + 1));
                return;
            }
            os_strncpy(tmp_str.ref, tmp_ptr, len);
            parsed_response->content_range_end = atoi(tmp_str.ref);
        }
        // range size
        tmp_ptr = end_ptr + 1;
        end_ptr = (char *)os_strstr(tmp_ptr, f_str("\r\n"));
        if (end_ptr == NULL)
        {
            esp_diag.error(HTTP_PARSE_RESPONSE_CANNOT_FIND_RANGE_SIZE);
            ERROR("http_parse_response cannot find Content-Range size");
            return;
        }
        len = end_ptr - tmp_ptr;
        {
            Heap_chunk tmp_str(len + 1);
            if (tmp_str.ref == NULL)
            {
                esp_diag.error(HTTP_PARSE_RESPONSE_HEAP_EXHAUSTED, (len + 1));
                ERROR("http_parse_response heap exhausted %d", (len + 1));
                return;
            }
            os_strncpy(tmp_str.ref, tmp_ptr, len);
            parsed_response->content_range_size = atoi(tmp_str.ref);
        }
    }
    // finally the body
    tmp_ptr = response;
    tmp_ptr = (char *)os_strstr(tmp_ptr, f_str("\r\n\r\n"));
    if (tmp_ptr == NULL)
    {
        esp_diag.error(HTTP_PARSE_RESPONSE_CANNOT_FIND_CONTENT_START);
        ERROR("http_parse_response cannot find Content start");
        return;
    }
    tmp_ptr += 4;
    len = length - (tmp_ptr - response);
    parsed_response->content_len = len;
    parsed_response->body = new char[len + 1];
    if (parsed_response->body == NULL)
    {
        esp_diag.error(HTTP_PARSE_RESPONSE_HEAP_EXHAUSTED, (len + 1));
        ERROR("http_parse_response heap exhausted %d", (len + 1));
        return;
    }
    os_memcpy(parsed_response->body, tmp_ptr, len);
}