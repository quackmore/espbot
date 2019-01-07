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

#include "webclient.hpp"
#include "espbot.hpp"
#include "espbot_global.hpp"
#include "logger.hpp"
#include "json.hpp"
#include "espbot_utils.hpp"
#include "debug.hpp"

//
// HTTP sending data:
// ----------------
// to make sure espconn_send is called after espconn_sent_callback of the previous packet
// a flag is set before calling espconn_send (will be reset by sendcb)
//
// befor sending a response the flag will be checked
// when the flag is found set (espconn_send not done yet)
// a timer will used for postponing response
//

#define DATA_SENDING_TIMER_PERIOD 500
static bool espclient_busy_sending_data;
static char *send_buffer;
static os_timer_t webclnt_wait_for_data_sent;

struct client_send
{
    struct espconn *ptr_espconn;
    char *msg;
};

static void ICACHE_FLASH_ATTR send(struct espconn *p_espconn, char *msg);

static void ICACHE_FLASH_ATTR webclient_pending_request(void *arg)
{
    esplog.all("webclient_pending_request\n");
    struct client_send *request_data = (struct client_send *)arg;
    send(request_data->ptr_espconn, request_data->msg);
    esp_free(request_data);
}

static void ICACHE_FLASH_ATTR webclient_sentcb(void *arg)
{
    esplog.all("webclient_sentcb\n");
    struct espconn *ptr_espconn = (struct espconn *)arg;
    espmem.stack_mon();
    espclient_busy_sending_data = false;
    if (send_buffer)
    {
        esp_free(send_buffer);
        send_buffer = NULL;
    }
}

static void ICACHE_FLASH_ATTR send(struct espconn *p_espconn, char *msg)
{
    esplog.all("webclient::send\n");
    esplog.trace("request: *p_espconn: %X\n"
                 "                msg: %s\n",
                 p_espconn, msg);
    if (espclient_busy_sending_data) // previous espconn_send not completed yet
    {
        esplog.debug("Webclnt::send - previous espconn_send not completed yet\n");
        struct client_send *send_data = (struct client_send *)esp_zalloc(sizeof(struct client_send));
        espmem.stack_mon();
        if (send_data)
        {
            send_data->ptr_espconn = p_espconn;
            send_data->msg = msg;
            os_timer_setfn(&webclnt_wait_for_data_sent, (os_timer_func_t *)webclient_pending_request, (void *)send_data);
            os_timer_arm(&webclnt_wait_for_data_sent, DATA_SENDING_TIMER_PERIOD, 0);
        }
        else
        {
            esplog.error("Webclnt::send - not enough heap memory (%s)\n", sizeof(struct client_send));
        }
    }
    else // previous espconn_send completed
    {
        espclient_busy_sending_data = true;
        sint8 res = espconn_send(p_espconn, (uint8 *)msg, os_strlen(msg));
        espmem.stack_mon();
        send_buffer = msg;
        if (res)
        {
            esplog.error("Webclnt::send - error sending response, error code %d\n", res);
            // on error don't count on sentcb to be called
            espclient_busy_sending_data = false;
            esp_free(msg);
        }
    }
}

//
// TIMERS AND TIMER FUNCTIONS
//

os_timer_t webclnt_connect_timeout_timer;

void ICACHE_FLASH_ATTR webclnt_connect_timeout(void *arg)
{
    esplog.all("webclnt_connect_timeout\n");
    espwebclnt.update_status(WEBCLNT_CONNECT_TIMEOUT);
}

#define WEBCLNT_SEND_REQ_TIMER 500
os_timer_t webclnt_send_req_timer;

void webclnt_send_req_timer_function(void *arg)
{
    esplog.all("webclnt_send_req_timer_function\n");
    Webclnt *client = (Webclnt *)arg;
    client->send_req(NULL);
}

//
// HTTP receive and parsing response
//

static void ICACHE_FLASH_ATTR init_html_parsed_response(Html_parsed_response *res)
{
    esplog.all("init_html_parsed_response\n");
    res->http_code = 0;
    res->content_range_start = 0;
    res->content_range_end = 0;
    res->content_range_size = 0;
    res->content_len = 0;
    res->body = NULL;
}

static void ICACHE_FLASH_ATTR parse_http_response(char *request, Html_parsed_response *parsed_response)
{
    esplog.all("webclnt::parse_http_response\n");
    char *tmp_ptr = request;
    char *end_ptr = NULL;
    char *tmp_str = NULL;
    int len = 0;
    espmem.stack_mon();

    if (tmp_ptr == NULL)
    {
        esplog.error("webclnt::parse_http_response - cannot parse empty message\n");
        return;
    }

    // looking for HTTP CODE
    tmp_ptr = request;
    tmp_ptr = (char *)os_strstr(tmp_ptr, "HTTP");
    if (tmp_ptr == NULL)
    {
        tmp_ptr = request;
    }
    else
    {
        tmp_ptr = (char *)os_strstr(tmp_ptr, " ");
        do
        {
            tmp_ptr++;
        } while (*tmp_ptr == ' ');
    }
    end_ptr = (char *)os_strstr(tmp_ptr, " ");
    if (end_ptr == NULL)
    {
        esplog.error("webclnt::parse_http_response - cannot find HTTP code\n");
        return;
    }
    len = end_ptr - tmp_ptr;
    tmp_str = (char *)esp_zalloc(len + 1);
    if (tmp_str == NULL)
    {
        esplog.error("webclnt::parse_http_response - not enough heap memory\n");
        return;
    }
    os_strncpy(tmp_str, tmp_ptr, len);
    parsed_response->http_code = atoi(tmp_str);
    esp_free(tmp_str);

    // now the content-length
    tmp_ptr = request;
    tmp_ptr = (char *)os_strstr(tmp_ptr, "Content-Length: ");
    if (tmp_ptr == NULL)
    {
        esplog.trace("webclnt::parse_http_response - didn't find any Content-Length\n");
    }
    else
    {
        tmp_ptr += 16;
        end_ptr = (char *)os_strstr(tmp_ptr, "\r\n");
        if (end_ptr == NULL)
        {
            esplog.error("webclnt::parse_http_response - cannot find Content-Length value\n");
            return;
        }
        len = end_ptr - tmp_ptr;
        tmp_str = (char *)esp_zalloc(len + 1);
        if (tmp_str == NULL)
        {
            esplog.error("webclnt::parse_http_response - not enough heap memory\n");
            return;
        }
        os_strncpy(tmp_str, tmp_ptr, len);
        parsed_response->content_len = atoi(tmp_str);
        esp_free(tmp_str);
    }
    // now Content-Range (if any)
    tmp_ptr = request;
    tmp_ptr = (char *)os_strstr(tmp_ptr, "Content-Range: ");
    if (tmp_ptr == NULL)
    {
        esplog.trace("webclnt::parse_http_response - didn't find any Content-Range\n");
    }
    else
    {
        tmp_ptr = (char *)os_strstr(tmp_ptr, "bytes");
        if (tmp_ptr == NULL)
        {
            esplog.error("webclnt::parse_http_response - cannot find Content-Range value\n");
            return;
        }
        // range start
        tmp_ptr += os_strlen("bytes ");
        end_ptr = (char *)os_strstr(tmp_ptr, "-");
        if (end_ptr == NULL)
        {
            esplog.error("webclnt::parse_http_response - cannot find range start\n");
            return;
        }
        len = end_ptr - tmp_ptr;
        tmp_str = (char *)esp_zalloc(len + 1);
        if (tmp_str == NULL)
        {
            esplog.error("webclnt::parse_http_response - not enough heap memory\n");
            return;
        }
        os_strncpy(tmp_str, tmp_ptr, len);
        parsed_response->content_range_start = atoi(tmp_str);
        esp_free(tmp_str);
        // range end
        tmp_ptr++;
        end_ptr = (char *)os_strstr(tmp_ptr, "/");
        if (end_ptr == NULL)
        {
            esplog.error("webclnt::parse_http_response - cannot find range end\n");
            return;
        }
        len = end_ptr - tmp_ptr;
        tmp_str = (char *)esp_zalloc(len + 1);
        if (tmp_str == NULL)
        {
            esplog.error("webclnt::parse_http_response - not enough heap memory\n");
            return;
        }
        os_strncpy(tmp_str, tmp_ptr, len);
        parsed_response->content_range_start = atoi(tmp_str);
        esp_free(tmp_str);
        // range size
        tmp_ptr++;
        end_ptr = (char *)os_strstr(tmp_ptr, "\r\n");
        if (end_ptr == NULL)
        {
            esplog.error("webclnt::parse_http_response - cannot find Content-Range size\n");
            return;
        }
        len = end_ptr - tmp_ptr;
        tmp_str = (char *)esp_zalloc(len + 1);
        if (tmp_str == NULL)
        {
            esplog.error("webclnt::parse_http_response - not enough heap memory\n");
            return;
        }
        os_strncpy(tmp_str, tmp_ptr, len);
        parsed_response->content_range_size = atoi(tmp_str);
        esp_free(tmp_str);
    }
    // finally the body
    tmp_ptr = request;
    tmp_ptr = (char *)os_strstr(tmp_ptr, "\r\n\r\n");
    if (tmp_ptr == NULL)
    {
        esplog.error("webclnt::parse_http_response - cannot find Content start\n");
        return;
    }
    tmp_ptr += 4;
    len = os_strlen(tmp_ptr);
    parsed_response->body = (char *)esp_zalloc(len + 1);
    if (parsed_response->body == NULL)
    {
        esplog.error("webclnt::parse_http_response - not enough heap memory\n");
        return;
    }
    os_memcpy(parsed_response->body, tmp_ptr, len);
}

static void ICACHE_FLASH_ATTR webclient_recv(void *arg, char *precdata, unsigned short length)
{
    esplog.all("webclient_recv\n");
    os_timer_disarm(&webclnt_send_req_timer);
    struct espconn *ptr_espconn = (struct espconn *)arg;
    espmem.stack_mon();
    esplog.trace("received msg: %s\n", precdata);
    Html_parsed_response *parsed_response = (Html_parsed_response *)esp_zalloc(sizeof(Html_parsed_response));
    if (parsed_response)
    {
        init_html_parsed_response(parsed_response);
        parse_http_response(precdata, parsed_response);

        esplog.trace("Webclnt::webclient_recv parsed response:\n"
                     "->                            http code: %d\n"
                     "->                  content_range_start: %d\n"
                     "->                    content_range_end: %d\n"
                     "->                   content_range_size: %d\n"
                     "->                          content len: %d\n"
                     "->                                 body: %s\n",
                     parsed_response->http_code,
                     parsed_response->content_range_start,
                     parsed_response->content_range_end,
                     parsed_response->content_range_size,
                     parsed_response->content_len,
                     parsed_response->body);

        espwebclnt.m_response = parsed_response;
        espwebclnt.update_status(WEBCLNT_RESPONSE_READY);
    }
    else
    {
        esplog.error("Webclnt::webclient_recv - not enough heap memory (%s)\n", sizeof(Html_parsed_response));
        espwebclnt.update_status(WEBCLNT_RESPONSE_ERROR);
    }
}

//
// ESPCONN callbacks
//

static ICACHE_FLASH_ATTR void webclient_recon(void *arg, sint8 err)
{
    esplog.all("webclient_recon\n");
    struct espconn *pesp_conn = (struct espconn *)arg;
    espmem.stack_mon();
    esplog.debug("%d.%d.%d.%d:%d err %d reconnect\n", pesp_conn->proto.tcp->remote_ip[0],
                pesp_conn->proto.tcp->remote_ip[1],
                pesp_conn->proto.tcp->remote_ip[2],
                pesp_conn->proto.tcp->remote_ip[3],
                pesp_conn->proto.tcp->remote_port,
                err);
}

static ICACHE_FLASH_ATTR void webclient_discon(void *arg)
{
    esplog.all("webclient_discon\n");
    struct espconn *pesp_conn = (struct espconn *)arg;
    espmem.stack_mon();
    esplog.debug("%d.%d.%d.%d:%d disconnect\n", pesp_conn->proto.tcp->remote_ip[0],
                pesp_conn->proto.tcp->remote_ip[1],
                pesp_conn->proto.tcp->remote_ip[2],
                pesp_conn->proto.tcp->remote_ip[3],
                pesp_conn->proto.tcp->remote_port);
}

static void ICACHE_FLASH_ATTR webclient_connected(void *arg)
{
    esplog.all("webclient_connected\n");
    struct espconn *pesp_conn = (struct espconn *)arg;
    espmem.stack_mon();
    espconn_regist_recvcb(pesp_conn, webclient_recv);
    espconn_regist_sentcb(pesp_conn, webclient_sentcb);
    espconn_regist_reconcb(pesp_conn, webclient_recon);
    espconn_regist_disconcb(pesp_conn, webclient_discon);

    // os_printf("webclient_connected: pesp_conn->state: %d\n", pesp_conn->state);
    //    enum espconn_state
    //    {
    //        ESPCONN_NONE,
    //        ESPCONN_WAIT,
    //        ESPCONN_LISTEN,
    //        ESPCONN_CONNECT,
    //        ESPCONN_WRITE,
    //        ESPCONN_READ,
    //        ESPCONN_CLOSE
    //    };

    os_timer_disarm(&webclnt_connect_timeout_timer);
    espwebclnt.update_status(WEBCLNT_CONNECTED);
}

//
// Webclnt class
//

void ICACHE_FLASH_ATTR Webclnt::init(void)
{
    esplog.all("Webclnt::init\n");
    m_status = WEBCLNT_DISCONNECTED;
    m_response = NULL;
}

void ICACHE_FLASH_ATTR Webclnt::connect(struct ip_addr t_server, uint32 t_port)
{
    esplog.all("Webclnt::connect\n");

    if (m_response)
    {
        esp_free(m_response);
        m_response = NULL;
    }

    m_status = WEBCLNT_CONNECTING;
    m_esp_conn.type = ESPCONN_TCP;
    m_esp_conn.state = ESPCONN_NONE;
    m_esp_conn.proto.tcp = &m_esptcp;

    system_soft_wdt_feed();
    m_esp_conn.proto.tcp->local_port = espconn_port();

    m_esp_conn.proto.tcp->remote_port = t_port;
    os_memcpy(m_esp_conn.proto.tcp->remote_ip, &(t_server.addr), 4);

    espconn_regist_connectcb(&m_esp_conn, webclient_connected);
    // set timeout for connection
    os_timer_setfn(&webclnt_connect_timeout_timer, (os_timer_func_t *)webclnt_connect_timeout, NULL);
    os_timer_arm(&webclnt_connect_timeout_timer, 5000, 0);
    sint8 res = espconn_connect(&m_esp_conn);
    espmem.stack_mon();
    if (res)
    {
        // in this case callback will never be called
        m_status = WEBCLNT_CONNECT_FAILURE;
        esplog.error("Webclnt::connect failed to connect, error code: %d\n", res);
        os_timer_disarm(&webclnt_connect_timeout_timer);
    }
}

void ICACHE_FLASH_ATTR Webclnt::disconnect(void)
{
    esplog.all("Webclnt::disconnect\n");
    m_status = WEBCLNT_DISCONNECTED;
    espconn_disconnect(&m_esp_conn);
    // espconn_delete(&m_esp_conn);
    esplog.debug("web client disconnected\n");
}

void ICACHE_FLASH_ATTR Webclnt::send_req(char *t_msg)
{
    esplog.all("Webclnt::send_req\n");
    static int timer_cnt;
    espmem.stack_mon();
    // first call, save the message
    if (t_msg)
    {
        m_msg = t_msg;
        timer_cnt = 0;
    }

    switch (m_status)
    {
    case WEBCLNT_DISCONNECTED:
        esplog.trace("Webclnt::send_req - disconnected, awaiting ...\n");
        if (timer_cnt > 10)
        {
            esplog.error("Webclnt::send_req - timeout waiting for connection\n");
            disconnect();
        }
        else
        {
            timer_cnt++;
            os_timer_setfn(&webclnt_send_req_timer, (os_timer_func_t *)webclnt_send_req_timer_function, (void *)this);
            os_timer_arm(&webclnt_send_req_timer, WEBCLNT_SEND_REQ_TIMER, 0);
        }
        break;
    case WEBCLNT_CONNECTED:
        esplog.trace("Webclnt::send_req - connected, sending ...\n");
        send(&m_esp_conn, m_msg);
        m_status = WEBCLNT_WAITING_RESPONSE;
        timer_cnt = 0;
        os_timer_setfn(&webclnt_send_req_timer, (os_timer_func_t *)webclnt_send_req_timer_function, (void *)this);
        os_timer_arm(&webclnt_send_req_timer, WEBCLNT_SEND_REQ_TIMER, 0);
        m_status = WEBCLNT_WAITING_RESPONSE;
        break;
    case WEBCLNT_WAITING_RESPONSE:
        esplog.trace("Webclnt::send_req - awaiting for response...\n");
        if (timer_cnt > 10)
        {
            esplog.error("Webclnt::send_req - timeout waiting for response\n");
            disconnect();
        }
        else
        {
            timer_cnt++;
            os_timer_setfn(&webclnt_send_req_timer, (os_timer_func_t *)webclnt_send_req_timer_function, (void *)this);
            os_timer_arm(&webclnt_send_req_timer, WEBCLNT_SEND_REQ_TIMER, 0);
        }
        break;
    }
}

Webclnt_status_type ICACHE_FLASH_ATTR Webclnt::get_status(void)
{
    esplog.all("Webclnt::get_status\n");
    return m_status;
}

void ICACHE_FLASH_ATTR Webclnt::update_status(Webclnt_status_type t_status)
{
    esplog.all("Webclnt::update_status\n");
    m_status = t_status;
}

void ICACHE_FLASH_ATTR Webclnt::free_response(void)
{
    esplog.all("Webclnt::free_response\n");
    if (m_response)
    {
        if (m_response->body)
        {
            esp_free(m_response->body);
            m_response->body = NULL;
        }
        esp_free(m_response);
        m_response = NULL;
    }
}
