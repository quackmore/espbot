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
}

#include "espbot_http.hpp"
#include "espbot_webclient.hpp"
#include "espbot.hpp"
#include "espbot_global.hpp"
#include "espbot_logger.hpp"
#include "espbot_json.hpp"
#include "espbot_utils.hpp"
#include "espbot_debug.hpp"

//
// TIMEOUT TIMERS AND TIMER FUNCTIONS
//

static os_timer_t connect_timeout_timer;

void webclnt_connect_timeout(void *arg)
{
    esplog.all("webclnt_connect_timeout\n");
    espwebclnt.update_status(WEBCLNT_CONNECT_TIMEOUT);
    espwebclnt.call_completed_func();
}

static os_timer_t send_req_timeout_timer;

void webclnt_send_req_timeout_function(void *arg)
{
    esplog.all("webclnt_send_req_timeout_function\n");
    os_printf("web client: response timeout\n");
    Webclnt *clnt = (Webclnt *)arg;
    clnt->update_status(WEBCLNT_RESPONSE_TIMEOUT);
    clnt->call_completed_func();
}

static void webclient_recv(void *arg, char *precdata, unsigned short length)
{
    esplog.all("webclient_recv\n");
    os_timer_disarm(&send_req_timeout_timer);
    struct espconn *ptr_espconn = (struct espconn *)arg;
    espmem.stack_mon();
    esplog.trace("received msg(%d): %s\n", length, precdata);

    Http_parsed_response *parsed_response = new Http_parsed_response;
    http_parse_response(precdata, parsed_response);

    esplog.trace("Webclnt::webclient_recv parsed response:\n"
                 "->                              espconn: %X\n"
                 "->                            http code: %d\n"
                 "->                  content_range_start: %d\n"
                 "->                    content_range_end: %d\n"
                 "->                   content_range_size: %d\n"
                 "->                   header content len: %d\n"
                 "->                          content len: %d\n"
                 "->                                 body: %s\n",
                 ptr_espconn,
                 parsed_response->http_code,
                 parsed_response->content_range_start,
                 parsed_response->content_range_end,
                 parsed_response->content_range_size,
                 parsed_response->h_content_len,
                 parsed_response->content_len,
                 parsed_response->body);

    if (!parsed_response->no_header_message && (parsed_response->h_content_len > parsed_response->content_len))
    {
        os_timer_arm(&send_req_timeout_timer, WEBCLNT_SEND_REQ_TIMEOUT, 0);
        esplog.debug("Webclnt::webclient_recv - message has been splitted waiting for completion ...\n");
        http_save_pending_response(ptr_espconn, precdata, length, parsed_response);
        delete parsed_response;
        esplog.debug("Webclnt::webclient_recv - pending response saved ...\n");
        return;
    }
    if (parsed_response->no_header_message)
    {
        os_timer_arm(&send_req_timeout_timer, WEBCLNT_SEND_REQ_TIMEOUT, 0);
        esplog.debug("Websvr::webclient_recv - No header message, checking pending responses ...\n");
        http_check_pending_responses(ptr_espconn, parsed_response->body, webclient_recv);
        delete parsed_response;
        esplog.debug("Webclnt::webclient_recv - response checked ...\n");
        return;
    }
    espwebclnt.update_status(WEBCLNT_RESPONSE_READY);
    espwebclnt.parsed_response = parsed_response;
    espwebclnt.call_completed_func();
    delete parsed_response;
}

//
// ESPCONN callbacks
//

static void webclient_recon(void *arg, sint8 err)
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

static void webclient_discon(void *arg)
{
    esplog.all("webclient_discon\n");
    struct espconn *pesp_conn = (struct espconn *)arg;
    espmem.stack_mon();
    esplog.debug("%d.%d.%d.%d:%d disconnect\n", pesp_conn->proto.tcp->remote_ip[0],
                 pesp_conn->proto.tcp->remote_ip[1],
                 pesp_conn->proto.tcp->remote_ip[2],
                 pesp_conn->proto.tcp->remote_ip[3],
                 pesp_conn->proto.tcp->remote_port);
    espwebclnt.update_status(WEBCLNT_DISCONNECTED);
    espwebclnt.call_completed_func();
}

static void webclient_connected(void *arg)
{
    esplog.all("webclient_connected\n");
    struct espconn *pesp_conn = (struct espconn *)arg;
    espmem.stack_mon();
    espconn_regist_recvcb(pesp_conn, webclient_recv);
    espconn_regist_sentcb(pesp_conn, http_sentcb);
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

    os_timer_disarm(&connect_timeout_timer);
    espwebclnt.update_status(WEBCLNT_CONNECTED);
    espwebclnt.call_completed_func();
}

//
// Webclnt class
//

void Webclnt::init(void)
{
    esplog.all("Webclnt::init\n");
    m_status = WEBCLNT_DISCONNECTED;
    m_completed_func = NULL;
    m_param = NULL;
    this->parsed_response = NULL;
    this->request = NULL;
}

void Webclnt::connect(struct ip_addr t_server, uint32 t_port, void (*completed_func)(void *), void *param)
{
    esplog.all("Webclnt::connect\n");

    os_memcpy(&m_host, &t_server, sizeof(struct ip_addr));
    m_port = t_port;

    m_completed_func = completed_func;
    m_param = param;
    m_status = WEBCLNT_CONNECTING;
    m_esp_conn.type = ESPCONN_TCP;
    m_esp_conn.state = ESPCONN_NONE;
    m_esp_conn.proto.tcp = &m_esptcp;

    system_soft_wdt_feed();
    m_esp_conn.proto.tcp->local_port = espconn_port();

    m_esp_conn.proto.tcp->remote_port = t_port;
    os_memcpy(m_esp_conn.proto.tcp->remote_ip, &(t_server.addr), sizeof(t_server.addr));

    espconn_regist_connectcb(&m_esp_conn, webclient_connected);
    // set timeout for connection
    os_timer_setfn(&connect_timeout_timer, (os_timer_func_t *)webclnt_connect_timeout, NULL);
    os_timer_arm(&connect_timeout_timer, WEBCLNT_CONNECTION_TIMEOUT, 0);
    sint8 res = espconn_connect(&m_esp_conn);
    espmem.stack_mon();
    if (res)
    {
        // in this case callback will never be called
        m_status = WEBCLNT_CONNECT_FAILURE;
        esplog.error("Webclnt::connect failed to connect, error code: %d\n", res);
        os_timer_disarm(&connect_timeout_timer);
        call_completed_func();
    }
}

void Webclnt::disconnect(void (*completed_func)(void *), void *param)
{
    esplog.all("Webclnt::disconnect\n");
    m_status = WEBCLNT_DISCONNECTED;
    m_completed_func = completed_func;
    m_param = param;

    // there is no need to delete this->request, http_send will do it
    // if (this->request)
    //     delete[] this->request;

    espconn_disconnect(&m_esp_conn);
    esplog.debug("web client disconnected\n");
}

void Webclnt::format_request(char *t_request)
{
    esplog.all("Webclnt::format_request\n");
    // there is no need to delete this->request, last http_send did it
    // if (this->request)
    //     delete[] this->request;

    int request_len = 16 + // string format
                      12 + // ip address
                      5 +  // port
                      os_strlen(t_request) +
                      1;
    this->request = new char[request_len];
    if (this->request == NULL)
    {
        esplog.error("Webclnt::format_request - not enough heap memory %d\n", request_len);
        return;
    }
    uint32 *tmp_ptr = &m_host.addr;
    os_sprintf(this->request,
               "%s\r\nHost: %d.%d.%d.%d:%d\r\n\r\n",
               t_request,
               ((char *)tmp_ptr)[0],
               ((char *)tmp_ptr)[1],
               ((char *)tmp_ptr)[2],
               ((char *)tmp_ptr)[3],
               m_port);
    os_printf("request_len: %d, effective length: %d request: %s\n", request_len, os_strlen(this->request), this->request);
}

void Webclnt::send_req(char *t_msg, void (*completed_func)(void *), void *param)
{
    esplog.all("Webclnt::send_req\n");
    espmem.stack_mon();
    m_completed_func = completed_func;
    format_request(t_msg);

    switch (m_status)
    {
    case WEBCLNT_CONNECTED:
    case WEBCLNT_RESPONSE_READY:
        esplog.trace("Webclnt::send_req - connected, sending ...\n");
        http_send(&m_esp_conn, this->request);
        m_status = WEBCLNT_WAITING_RESPONSE;
        os_timer_disarm(&send_req_timeout_timer);
        os_timer_setfn(&send_req_timeout_timer, (os_timer_func_t *)webclnt_send_req_timeout_function, (void *)this);
        os_timer_arm(&send_req_timeout_timer, WEBCLNT_SEND_REQ_TIMEOUT, 0);
        break;
    default:
        m_status = WEBCLNT_CANNOT_SEND_REQUEST;
        esplog.error("Webclnt::send_req - cannot send request as status is %s\n", get_status());
        call_completed_func();
        break;
    }
}

Webclnt_status_type Webclnt::get_status(void)
{
    esplog.all("Webclnt::get_status\n");
    return m_status;
}

void Webclnt::update_status(Webclnt_status_type t_status)
{
    esplog.all("Webclnt::update_status\n");
    m_status = t_status;
}

void Webclnt::call_completed_func(void)
{
    esplog.all("Webclnt::call_completed_func\n");
    if (m_completed_func)
        m_completed_func(m_param);
}