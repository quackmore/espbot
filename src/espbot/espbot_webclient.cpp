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
// ESPCONN <==> WEBCLNT ASSOCIATION
//

typedef struct _A_espconn_webclnt
{
    struct espconn *p_pespconn;
    Webclnt *client;
} A_espconn_webclnt;

static List<A_espconn_webclnt> *webclnt_espconn;

void init_webclients_data_stuctures(void)
{
    esplog.all("init_webclient\n");
    webclnt_espconn = new List<A_espconn_webclnt>(4, delete_content);
}

static Webclnt *get_client(struct espconn *p_pespconn)
{
    esplog.all("get_client\n");
    A_espconn_webclnt *ptr = webclnt_espconn->front();
    while (ptr)
    {
        if (ptr->p_pespconn == p_pespconn)
            return ptr->client;
        ptr = webclnt_espconn->next();
    }
    return NULL;
}

static void add_client_espconn_association(Webclnt *client, struct espconn *p_pespconn)
{
    esplog.all("add_client_espconn_association\n");
    A_espconn_webclnt *new_association = new A_espconn_webclnt;
    if (new_association == NULL)
    {
        esplog.error("add_client_espconn_association - not enough heap memory [%d]\n", sizeof(A_espconn_webclnt));
        return;
    }
    new_association->client = client;
    new_association->p_pespconn = p_pespconn;
    List_err err = webclnt_espconn->push_back(new_association);
    if (err != list_ok)
    {
        esplog.error("add_client_espconn_association - cannot register association between webclient %X and espconn %X\n",
                     client,
                     p_pespconn);
        delete new_association;
        return;
    }
}

static void del_client_association(Webclnt *client)
{
    esplog.all("del_client_association\n");
    A_espconn_webclnt *ptr = webclnt_espconn->front();
    while (ptr)
    {
        if (ptr->client == client)
        {
            webclnt_espconn->remove();
            // one element removed from list, better restart from front
            ptr = webclnt_espconn->front();
        }
        // need to check ptr because of webclnt_espconn->front() maybe was called ...
        if (ptr)
            ptr = webclnt_espconn->next();
    }
}

//
// TIMER FUNCTIONS
//

void webclnt_connect_timeout(void *arg)
{
    esplog.error("webclnt_connect_timeout\n");
    Webclnt *clnt = (Webclnt *)arg;
    clnt->update_status(WEBCLNT_CONNECT_TIMEOUT);
    clnt->call_completed_func();
}

static void webclnt_send_req_timeout_function(void *arg)
{
    esplog.error("webclnt_send_req_timeout_function\n");
    Webclnt *clnt = (Webclnt *)arg;
    clnt->update_status(WEBCLNT_RESPONSE_TIMEOUT);
    clnt->call_completed_func();
}

//
// RECEIVING
//

static void webclient_recv(void *arg, char *precdata, unsigned short length)
{
    esplog.all("webclient_recv\n");
    struct espconn *ptr_espconn = (struct espconn *)arg;
    Webclnt *client = get_client(ptr_espconn);
    if (client == NULL)
    {
        esplog.error("webclient_recv - cannot get webclient ref from espconn %X\n", ptr_espconn);
        return;
    }
    os_timer_disarm(&client->m_send_req_timeout_timer);
    espmem.stack_mon();
    esplog.trace("webclient_recv - received msg(%d): %s\n", length, precdata);
    // in case of binary message
    // int ii;
    // os_printf("msg hex: ");
    // for (ii = 0; ii < length; ii++)
    //     os_printf("%X ", precdata[ii]);
    // os_printf("\n");

    Http_parsed_response *parsed_response = new Http_parsed_response;
    http_parse_response(precdata, length, parsed_response);

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
        os_timer_arm(&client->m_send_req_timeout_timer, WEBCLNT_SEND_REQ_TIMEOUT, 0);
        esplog.debug("Webclnt::webclient_recv - message has been splitted waiting for completion ...\n");
        http_save_pending_response(ptr_espconn, precdata, length, parsed_response);
        delete parsed_response;
        esplog.debug("Webclnt::webclient_recv - pending response saved ...\n");
        return;
    }
    if (parsed_response->no_header_message)
    {
        os_timer_arm(&client->m_send_req_timeout_timer, WEBCLNT_SEND_REQ_TIMEOUT, 0);
        esplog.debug("Websvr::webclient_recv - No header message, checking pending responses ...\n");
        http_check_pending_responses(ptr_espconn, parsed_response->body, parsed_response->content_len, webclient_recv);
        delete parsed_response;
        esplog.debug("Webclnt::webclient_recv - response checked ...\n");
        return;
    }
    client->update_status(WEBCLNT_RESPONSE_READY);
    client->parsed_response = parsed_response;
    client->call_completed_func();
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
    Webclnt *client = get_client(pesp_conn);
    espmem.stack_mon();
    esplog.debug("%d.%d.%d.%d:%d disconnect\n", pesp_conn->proto.tcp->remote_ip[0],
                 pesp_conn->proto.tcp->remote_ip[1],
                 pesp_conn->proto.tcp->remote_ip[2],
                 pesp_conn->proto.tcp->remote_ip[3],
                 pesp_conn->proto.tcp->remote_port);
    if (client == NULL)
    {
        esplog.error("webclient_discon - cannot get webclient ref from espconn %X\n", pesp_conn);
        return;
    }
    client->update_status(WEBCLNT_DISCONNECTED);
    esplog.debug("webclient disconnected\n");
    client->call_completed_func();
}

static void webclient_connected(void *arg)
{
    esplog.all("webclient_connected\n");
    struct espconn *pesp_conn = (struct espconn *)arg;
    Webclnt *client = get_client(pesp_conn);
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

    if (client == NULL)
    {
        esplog.error("webclient_connected - cannot get webclient ref from espconn %X\n", pesp_conn);
        return;
    }
    os_timer_disarm(&client->m_connect_timeout_timer);
    client->update_status(WEBCLNT_CONNECTED);
    esplog.debug("webclient connected\n");
    client->call_completed_func();
}

//
// Webclnt class
//

Webclnt::Webclnt()
{
    esplog.all("Webclnt::Webclnt\n");
    m_status = WEBCLNT_DISCONNECTED;
    m_completed_func = NULL;
    m_param = NULL;
    this->parsed_response = NULL;
    this->request = NULL;
    add_client_espconn_association(this, &m_esp_conn);
}

Webclnt::~Webclnt()
{
    esplog.all("Webclnt::~Webclnt\n");
    del_client_association(this);
    if ((m_status != WEBCLNT_DISCONNECTED) &&
        (m_status != WEBCLNT_CONNECT_FAILURE) &&
        (m_status != WEBCLNT_CONNECT_TIMEOUT) &&
        (m_status != WEBCLNT_CONNECTING))
    {
        espconn_disconnect(&m_esp_conn);
    }
}

void Webclnt::connect(struct ip_addr t_server,
                      uint32 t_port,
                      void (*completed_func)(void *),
                      void *param,
                      int comm_tout)
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
    m_comm_timeout = comm_tout;

    system_soft_wdt_feed();
    m_esp_conn.proto.tcp->local_port = espconn_port();

    m_esp_conn.proto.tcp->remote_port = t_port;
    os_memcpy(m_esp_conn.proto.tcp->remote_ip, &(t_server.addr), sizeof(t_server.addr));

    espconn_regist_connectcb(&m_esp_conn, webclient_connected);
    // set timeout for connection
    os_timer_setfn(&m_connect_timeout_timer, (os_timer_func_t *)webclnt_connect_timeout, (void *)this);
    os_timer_arm(&m_connect_timeout_timer, m_comm_timeout, 0);
    sint8 res = espconn_connect(&m_esp_conn);
    espmem.stack_mon();
    if (res)
    {
        // in this case callback will never be called
        m_status = WEBCLNT_CONNECT_FAILURE;
        esplog.error("Webclnt::connect failed to connect, error code: %d\n", res);
        os_timer_disarm(&m_connect_timeout_timer);
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
    esplog.debug("web client disconnecting ...\n");
}

// void Webclnt::format_request(char *t_request)
// {
//     esplog.all("Webclnt::format_request\n");
//     // there is no need to delete this->request, last http_send did it
//     // if (this->request)
//     //     delete[] this->request;
//
//     int request_len = 16 + // string format
//                       12 + // ip address
//                       5 +  // port
//                       os_strlen(t_request) +
//                       1;
//     this->request = new char[request_len];
//     if (this->request == NULL)
//     {
//         esplog.error("Webclnt::format_request - not enough heap memory %d\n", request_len);
//         return;
//     }
//     uint32 *tmp_ptr = &m_host.addr;
//     os_sprintf(this->request,
//                "%s\r\nHost: %d.%d.%d.%d:%d\r\n\r\n",
//                t_request,
//                ((char *)tmp_ptr)[0],
//                ((char *)tmp_ptr)[1],
//                ((char *)tmp_ptr)[2],
//                ((char *)tmp_ptr)[3],
//                m_port);
//     // os_printf("request_len: %d, effective length: %d request: %s\n", request_len, os_strlen(this->request), this->request);
// }

void Webclnt::send_req(char *t_msg, void (*completed_func)(void *), void *param)
{
    esplog.all("Webclnt::send_req\n");
    espmem.stack_mon();
    m_completed_func = completed_func;
    this->request = new char[os_strlen(t_msg) + 1];
    if (this->request == NULL)
    {
        esplog.error("Webclnt::format_request - not enough heap memory %d\n", os_strlen(t_msg));
        return;
    }
    os_memcpy(this->request, t_msg, os_strlen(t_msg) + 1);

    switch (m_status)
    {
    case WEBCLNT_CONNECTED:
    case WEBCLNT_RESPONSE_READY:
        esplog.trace("Webclnt::send_req - connected, sending: %s\n", this->request);
        http_send(&m_esp_conn, this->request);
        m_status = WEBCLNT_WAITING_RESPONSE;
        os_timer_disarm(&m_send_req_timeout_timer);
        os_timer_setfn(&m_send_req_timeout_timer, (os_timer_func_t *)webclnt_send_req_timeout_function, (void *)this);
        os_timer_arm(&m_send_req_timeout_timer, WEBCLNT_SEND_REQ_TIMEOUT, 0);
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
    print_status();
}

void Webclnt::call_completed_func(void)
{
    esplog.all("Webclnt::call_completed_func\n");
    if (m_completed_func)
        m_completed_func(m_param);
}

void Webclnt::print_status(void)
{
    char *status;
    switch (m_status)
    {
    case WEBCLNT_RESPONSE_READY:
        status = "RESPONSE_READY";
        break;
    case WEBCLNT_DISCONNECTED:
        status = "DISCONNECTED";
        break;
    case WEBCLNT_CONNECTING:
        status = "CONNECTING";
        break;
    case WEBCLNT_CONNECTED:
        status = "CONNECTED";
        break;
    case WEBCLNT_WAITING_RESPONSE:
        status = "WAITING_RESPONSE";
        break;
    case WEBCLNT_CONNECT_FAILURE:
        status = "CONNECT_FAILURE";
        break;
    case WEBCLNT_CONNECT_TIMEOUT:
        status = "CONNECT_TIMEOUT";
        break;
    case WEBCLNT_RESPONSE_ERROR:
        status = "RESPONSE_ERROR";
        break;
    case WEBCLNT_CANNOT_SEND_REQUEST:
        status = "CANNOT_SEND_REQUEST";
        break;
    case WEBCLNT_RESPONSE_TIMEOUT:
        status = "RESPONSE_TIMEOUT";
        break;
    default:
        status = "UNKNOWN";
        break;
    }
    esplog.trace("Webclnt status --> %s\n", status);
}