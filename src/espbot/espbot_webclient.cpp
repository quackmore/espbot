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
#include "mem.h"
#include "osapi.h"
#include "user_interface.h"
}

#include "espbot.hpp"
#include "espbot_diagnostic.hpp"
#include "espbot_event_codes.h"
#include "espbot_global.hpp"
#include "espbot_json.hpp"
#include "espbot_http.hpp"
#include "espbot_logger.hpp"
#include "espbot_mem_mon.hpp"
#include "espbot_utils.hpp"
#include "espbot_webclient.hpp"

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
    webclnt_espconn = new List<A_espconn_webclnt>(4, delete_content);
}

static Webclnt *get_client(struct espconn *p_pespconn)
{
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
    A_espconn_webclnt *new_association = new A_espconn_webclnt;
    if (new_association == NULL)
    {
        esp_diag.error(WEB_CLIENT_ADD_CLIENT_ESPCONN_ASSOCIATION_HEAP_EXHAUSTED, sizeof(A_espconn_webclnt));
        // esplog.error("add_client_espconn_association - not enough heap memory [%d]\n", sizeof(A_espconn_webclnt));
        return;
    }
    new_association->client = client;
    new_association->p_pespconn = p_pespconn;
    List_err err = webclnt_espconn->push_back(new_association);
    if (err != list_ok)
    {
        esp_diag.error(WEB_CLIENT_ADD_CLIENT_ASSOCIATION_REG_ERROR);
        // esplog.error("add_client_espconn_association - cannot register association between webclient %X and espconn %X\n",
        //              client,
        //              p_pespconn);
        delete new_association;
        return;
    }
}

static void del_client_association(Webclnt *client)
{
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
    esp_diag.error(WEB_CLIENT_CONNECT_TIMEOUT);
    // esplog.error("webclnt_connect_timeout\n");
    Webclnt *clnt = (Webclnt *)arg;
    clnt->update_status(WEBCLNT_CONNECT_TIMEOUT);
    clnt->call_completed_func();
}

static void webclnt_send_req_timeout_function(void *arg)
{
    esp_diag.error(WEB_CLIENT_SEND_REQ_TIMEOUT);
    // esplog.error("webclnt_send_req_timeout_function\n");
    Webclnt *clnt = (Webclnt *)arg;
    clnt->update_status(WEBCLNT_RESPONSE_TIMEOUT);
    clnt->call_completed_func();
}

//
// RECEIVING
//

static void webclient_recv(void *arg, char *precdata, unsigned short length)
{
    struct espconn *ptr_espconn = (struct espconn *)arg;
    Webclnt *client = get_client(ptr_espconn);
    if (client == NULL)
    {
        esp_diag.error(WEB_CLIENT_RECV_CANNOT_FIND_ESPCONN, (uint32) ptr_espconn);
        // esplog.error("webclient_recv - cannot get webclient ref from espconn %X\n", ptr_espconn);
        return;
    }
    os_timer_disarm(&client->m_send_req_timeout_timer);
    espmem.stack_mon();
#ifdef DEBUG_TRACE
    esplog.trace("webclient_recv - received msg(%d): %s\n", length, precdata);
#endif
    // in case of binary message
    // int ii;
    // os_printf("msg hex: ");
    // for (ii = 0; ii < length; ii++)
    //     os_printf("%X ", precdata[ii]);
    // os_printf("\n");

    Http_parsed_response *parsed_response = new Http_parsed_response;
    http_parse_response(precdata, length, parsed_response);

#ifdef DEBUG_TRACE
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
#endif

    if (!parsed_response->no_header_message && (parsed_response->h_content_len > parsed_response->content_len))
    {
        os_timer_arm(&client->m_send_req_timeout_timer, WEBCLNT_SEND_REQ_TIMEOUT, 0);
#ifdef DEBUG_TRACE
        esplog.debug("Webclnt::webclient_recv - message has been splitted waiting for completion ...\n");
#endif
        http_save_pending_response(ptr_espconn, precdata, length, parsed_response);
        delete parsed_response;
#ifdef DEBUG_TRACE
        esplog.debug("Webclnt::webclient_recv - pending response saved ...\n");
#endif
        return;
    }
    if (parsed_response->no_header_message)
    {
        os_timer_arm(&client->m_send_req_timeout_timer, WEBCLNT_SEND_REQ_TIMEOUT, 0);
#ifdef DEBUG_TRACE
        esplog.debug("Websvr::webclient_recv - No header message, checking pending responses ...\n");
#endif
        http_check_pending_responses(ptr_espconn, parsed_response->body, parsed_response->content_len, webclient_recv);
        delete parsed_response;
#ifdef DEBUG_TRACE
        esplog.debug("Webclnt::webclient_recv - response checked ...\n");
#endif
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
    struct espconn *pesp_conn = (struct espconn *)arg;
    espmem.stack_mon();
#ifdef DEBUG_TRACE
    esplog.debug("%d.%d.%d.%d:%d err %d reconnect\n", pesp_conn->proto.tcp->remote_ip[0],
                 pesp_conn->proto.tcp->remote_ip[1],
                 pesp_conn->proto.tcp->remote_ip[2],
                 pesp_conn->proto.tcp->remote_ip[3],
                 pesp_conn->proto.tcp->remote_port,
                 err);
#endif
}

static void webclient_discon(void *arg)
{
    struct espconn *pesp_conn = (struct espconn *)arg;
    Webclnt *client = get_client(pesp_conn);
    espmem.stack_mon();
#ifdef DEBUG_TRACE
    esplog.debug("%d.%d.%d.%d:%d disconnect\n", pesp_conn->proto.tcp->remote_ip[0],
                 pesp_conn->proto.tcp->remote_ip[1],
                 pesp_conn->proto.tcp->remote_ip[2],
                 pesp_conn->proto.tcp->remote_ip[3],
                 pesp_conn->proto.tcp->remote_port);
#endif
    if (client == NULL)
    {
        esp_diag.error(WEB_CLIENT_DISCON_CANNOT_FIND_ESPCONN, (uint32) pesp_conn);
        // esplog.error("webclient_discon - cannot get webclient ref from espconn %X\n", pesp_conn);
        return;
    }
    client->update_status(WEBCLNT_DISCONNECTED);
#ifdef DEBUG_TRACE
    esplog.debug("webclient disconnected\n");
#endif
    client->call_completed_func();
}

static void webclient_connected(void *arg)
{
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
        esp_diag.error(WEB_CLIENT_CONNECTED_CANNOT_FIND_ESPCONN, (uint32) pesp_conn);
        // esplog.error("webclient_connected - cannot get webclient ref from espconn %X\n", pesp_conn);
        return;
    }
    os_timer_disarm(&client->m_connect_timeout_timer);
    client->update_status(WEBCLNT_CONNECTED);
#ifdef DEBUG_TRACE
    esplog.debug("webclient connected\n");
#endif
    client->call_completed_func();
}

//
// Webclnt class
//

Webclnt::Webclnt()
{
    m_status = WEBCLNT_DISCONNECTED;
    m_completed_func = NULL;
    m_param = NULL;
    this->parsed_response = NULL;
    this->request = NULL;
    add_client_espconn_association(this, &m_esp_conn);
}

Webclnt::~Webclnt()
{
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
        esp_diag.error(WEB_CLIENT_CONNECT_CONN_FAILURE, res);
        // esplog.error("Webclnt::connect failed to connect, error code: %d\n", res);
        os_timer_disarm(&m_connect_timeout_timer);
        call_completed_func();
    }
}

void Webclnt::disconnect(void (*completed_func)(void *), void *param)
{
    m_status = WEBCLNT_DISCONNECTED;
    m_completed_func = completed_func;
    m_param = param;

    // there is no need to delete this->request, http_send will do it
    // if (this->request)
    //     delete[] this->request;

    espconn_disconnect(&m_esp_conn);
#ifdef DEBUG_TRACE
    esplog.debug("web client disconnecting ...\n");
#endif
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
    espmem.stack_mon();
    m_completed_func = completed_func;
    m_param = param;
    this->request = new char[os_strlen(t_msg) + 1];
    if (this->request == NULL)
    {
        esp_diag.error(WEB_CLIENT_SEND_REQ_HEAP_EXHAUSTED, (os_strlen(t_msg) + 1));
        // esplog.error("Webclnt::format_request - not enough heap memory %d\n", (os_strlen(t_msg) + 1));
        return;
    }
    os_memcpy(this->request, t_msg, os_strlen(t_msg) + 1);

    switch (m_status)
    {
    case WEBCLNT_CONNECTED:
    case WEBCLNT_RESPONSE_READY:
#ifdef DEBUG_TRACE
        esplog.trace("Webclnt::send_req - connected, sending: %s\n", this->request);
#endif
        http_send(&m_esp_conn, this->request);
        m_status = WEBCLNT_WAITING_RESPONSE;
        os_timer_disarm(&m_send_req_timeout_timer);
        os_timer_setfn(&m_send_req_timeout_timer, (os_timer_func_t *)webclnt_send_req_timeout_function, (void *)this);
        os_timer_arm(&m_send_req_timeout_timer, WEBCLNT_SEND_REQ_TIMEOUT, 0);
        break;
    default:
        esp_diag.error(WEB_CLIENT_SEND_REQ_CANNOT_SEND_REQ, m_status);
        // esplog.error("Webclnt::send_req - cannot send request as status is %s\n", m_status);
        m_status = WEBCLNT_CANNOT_SEND_REQUEST;
        call_completed_func();
        break;
    }
}

Webclnt_status_type Webclnt::get_status(void)
{
    return m_status;
}

void Webclnt::update_status(Webclnt_status_type t_status)
{
    m_status = t_status;
    print_status();
}

void Webclnt::call_completed_func(void)
{
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
#ifdef DEBUG_TRACE
    esplog.trace("Webclnt status --> %s\n", status);
#endif
}