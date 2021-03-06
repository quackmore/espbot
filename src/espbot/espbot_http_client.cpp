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
#include "espbot_json.hpp"
#include "espbot_http.hpp"
#include "espbot_list.hpp"
#include "espbot_mem_mon.hpp"
#include "espbot_utils.hpp"
#include "espbot_http_client.hpp"

//
// ESPCONN <==> Http_clt ASSOCIATION
//

typedef struct _A_espconn_http_clt
{
    struct espconn *p_pespconn;
    Http_clt *client;
} A_espconn_http_clt;

static List<A_espconn_http_clt> *http_clt_espconn;

void init_http_clients_data_stuctures(void)
{
    http_clt_espconn = new List<A_espconn_http_clt>(4, delete_content);
}

static Http_clt *get_client(struct espconn *p_pespconn)
{
    A_espconn_http_clt *ptr = http_clt_espconn->front();
    while (ptr)
    {
        if (ptr->p_pespconn == p_pespconn)
            return ptr->client;
        ptr = http_clt_espconn->next();
    }
    return NULL;
}

static void add_client_espconn_association(Http_clt *client, struct espconn *p_pespconn)
{
    ALL("add_client_espconn_association");
    A_espconn_http_clt *new_association = new A_espconn_http_clt;
    if (new_association == NULL)
    {
        dia_error_evnt(HTTP_CLT_ADD_CLIENT_ESPCONN_ASSOCIATION_HEAP_EXHAUSTED, sizeof(A_espconn_http_clt));
        ERROR("add_client_espconn_association heap exhausted %d", sizeof(A_espconn_http_clt));
        return;
    }
    new_association->client = client;
    new_association->p_pespconn = p_pespconn;
    List_err err = http_clt_espconn->push_back(new_association);
    mem_mon_stack();
    if (err != list_ok)
    {
        dia_error_evnt(HTTP_CLT_ADD_CLIENT_ASSOCIATION_REG_ERROR);
        ERROR("add_client_espconn_association http_client %X and espconn full queue", client);
        delete new_association;
        return;
    }
}

static void del_client_association(Http_clt *client)
{
    ALL("del_client_association");
    A_espconn_http_clt *ptr = http_clt_espconn->front();
    mem_mon_stack();
    while (ptr)
    {
        if (ptr->client == client)
        {
            http_clt_espconn->remove();
            // one element removed from list, better restart from front
            ptr = http_clt_espconn->front();
        }
        // need to check ptr because of http_clt_espconn->front() maybe was called ...
        if (ptr)
            ptr = http_clt_espconn->next();
    }
}

//
// TIMER FUNCTIONS
//

void http_clt_connect_timeout(void *arg)
{
    // esp_diag.info(HTTP_CLT_CONNECT_TIMEOUT);
    INFO("http_clt_connect_timeout");
    Http_clt *clnt = (Http_clt *)arg;
    mem_mon_stack();
    clnt->update_status(HTTP_CLT_CONNECT_TIMEOUT);
    clnt->call_completed_func();
}

static void http_clt_send_req_timeout_function(void *arg)
{
    // esp_diag.info(HTTP_CLT_SEND_REQ_TIMEOUT);
    INFO("http_clt_send_req_timeout_function");
    Http_clt *clnt = (Http_clt *)arg;
    mem_mon_stack();
    clnt->update_status(HTTP_CLT_RESPONSE_TIMEOUT);
    clnt->call_completed_func();
}

//
// RECEIVING
//

static void http_client_recv(void *arg, char *precdata, unsigned short length)
{
    struct espconn *ptr_espconn = (struct espconn *)arg;
    ALL("http_client_recv");
    Http_clt *client = get_client(ptr_espconn);
    if (client == NULL)
    {
        dia_error_evnt(HTTP_CLT_RECV_CANNOT_FIND_ESPCONN, (uint32) ptr_espconn);
        ERROR("http_client_recv - cannot get client ref for espconn %X", ptr_espconn);
        return;
    }
    os_timer_disarm(&client->_send_req_timeout_timer);
    mem_mon_stack();
    DEBUG("http_client_recv msg %s", precdata);
    TRACE("http_client_recv msg len %d", length);
    // in case of binary message
    // int ii;
    // os_printf("msg hex: ");
    // for (ii = 0; ii < length; ii++)
    //     os_printf("%X ", precdata[ii]);
    // os_printf("\n");
    Http_parsed_response *parsed_response = new Http_parsed_response;
    http_parse_response(precdata, length, parsed_response);
    TRACE("http_client_recv parsed res\n"
          "            espconn %X\n"
          "          http code %d\n"
          "content_range_start %d\n"
          "  content_range_end %d\n"
          " content_range_size %d\n"
          " header content len %d\n"
          "        content len %d\n"
          "               body %s",
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
        os_timer_arm(&client->_send_req_timeout_timer, client->_comm_timeout, 0);
        TRACE("http_client_recv message has been splitted waiting for completion ...");
        http_save_pending_response(ptr_espconn, precdata, length, parsed_response);
        delete parsed_response;
        TRACE("http_client_recv pending response saved ...");
        return;
    }
    if (parsed_response->no_header_message)
    {
        os_timer_arm(&client->_send_req_timeout_timer, client->_comm_timeout, 0);
        TRACE("http_client_recv no header, checking pending responses ...");
        http_check_pending_responses(ptr_espconn, parsed_response->body, parsed_response->content_len, http_client_recv);
        delete parsed_response;
        TRACE("http_client_recv response checked ...");
        return;
    }
    client->update_status(HTTP_CLT_RESPONSE_READY);
    client->parsed_response = parsed_response;
    client->call_completed_func();
    delete parsed_response;
    mem_mon_stack();
}

//
// ESPCONN callbacks
//

static void http_client_recon(void *arg, sint8 err)
{
    struct espconn *pesp_conn = (struct espconn *)arg;
    DEBUG("http_client reconnect to %d.%d.%d.%d:%d err %d",
          pesp_conn->proto.tcp->remote_ip[0],
          pesp_conn->proto.tcp->remote_ip[1],
          pesp_conn->proto.tcp->remote_ip[2],
          pesp_conn->proto.tcp->remote_ip[3],
          pesp_conn->proto.tcp->remote_port,
          err);
    mem_mon_stack();
}

static void http_client_discon(void *arg)
{
    struct espconn *pesp_conn = (struct espconn *)arg;
    DEBUG("http_client disconnect from %d.%d.%d.%d:%d",
          pesp_conn->proto.tcp->remote_ip[0],
          pesp_conn->proto.tcp->remote_ip[1],
          pesp_conn->proto.tcp->remote_ip[2],
          pesp_conn->proto.tcp->remote_ip[3],
          pesp_conn->proto.tcp->remote_port);
    // Http_clt *client = get_client(pesp_conn);
    // mem_mon_stack();
    // if (client == NULL)
    // {
    //     dia_error_evnt(HTTP_CLT_DISCON_CANNOT_FIND_ESPCONN, (uint32)pesp_conn);
    //     ERROR("http_client_discon cannot get http_client ref for espconn %X", pesp_conn);
    //     return;
    // }
    // client->update_status(HTTP_CLT_DISCONNECTED);
    // client->call_completed_func();
    mem_mon_stack();
}

static void http_client_connected(void *arg)
{
    struct espconn *pesp_conn = (struct espconn *)arg;
    DEBUG("http_client connected to %d.%d.%d.%d:%d",
          pesp_conn->proto.tcp->remote_ip[0],
          pesp_conn->proto.tcp->remote_ip[1],
          pesp_conn->proto.tcp->remote_ip[2],
          pesp_conn->proto.tcp->remote_ip[3],
          pesp_conn->proto.tcp->remote_port);
    Http_clt *client = get_client(pesp_conn);
    mem_mon_stack();
    espconn_regist_recvcb(pesp_conn, http_client_recv);
    espconn_regist_sentcb(pesp_conn, http_sentcb);
    espconn_regist_reconcb(pesp_conn, http_client_recon);
    espconn_regist_disconcb(pesp_conn, http_client_discon);
    if (client == NULL)
    {
        dia_error_evnt(HTTP_CLT_CONNECTED_CANNOT_FIND_ESPCONN, (uint32)pesp_conn);
        ERROR("http_client_connected cannot get http_client ref for espconn %X", pesp_conn);
        return;
    }
    os_timer_disarm(&client->_connect_timeout_timer);
    client->update_status(HTTP_CLT_CONNECTED);
    client->call_completed_func();
}

//
// Http_clt class
//

Http_clt::Http_clt()
{
    ALL("Http_clt");
    _status = HTTP_CLT_DISCONNECTED;
    _completed_func = NULL;
    _param = NULL;
    this->parsed_response = NULL;
    this->request = NULL;
    add_client_espconn_association(this, &_esp_conn);
}

Http_clt::~Http_clt()
{
    ALL("~Http_clt");
    del_client_association(this);
    if ((_status != HTTP_CLT_DISCONNECTED) &&
        (_status != HTTP_CLT_CONNECT_FAILURE) &&
        (_status != HTTP_CLT_CONNECT_TIMEOUT) &&
        (_status != HTTP_CLT_CONNECTING))
    {
        espconn_disconnect(&_esp_conn);
    }
}

void Http_clt::connect(struct ip_addr t_server,
                      uint32 t_port,
                      void (*completed_func)(void *),
                      void *param,
                      int comm_tout)
{
    ALL("Http_clt::connect");
    os_memcpy(&_host, &t_server, sizeof(struct ip_addr));
    _port = t_port;

    _completed_func = completed_func;
    _param = param;
    _status = HTTP_CLT_CONNECTING;
    _esp_conn.type = ESPCONN_TCP;
    _esp_conn.state = ESPCONN_NONE;
    _esp_conn.proto.tcp = &_esptcp;
    _comm_timeout = comm_tout;

    system_soft_wdt_feed();
    _esp_conn.proto.tcp->local_port = espconn_port();

    _esp_conn.proto.tcp->remote_port = t_port;
    os_memcpy(_esp_conn.proto.tcp->remote_ip, &(t_server.addr), sizeof(t_server.addr));

    espconn_regist_connectcb(&_esp_conn, http_client_connected);
    // set timeout for connection
    os_timer_setfn(&_connect_timeout_timer, (os_timer_func_t *)http_clt_connect_timeout, (void *)this);
    os_timer_arm(&_connect_timeout_timer, _comm_timeout, 0);
    sint8 res = espconn_connect(&_esp_conn);
    mem_mon_stack();
    if (res)
    {
        // no connection established, have to call the callback 
        _status = HTTP_CLT_CONNECT_FAILURE;
        // esp_diag.info(HTTP_CLT_CONNECT_CONN_FAILURE, res);
        INFO("Http_clt failed to connect err %d", res);
        os_timer_disarm(&_connect_timeout_timer);
        call_completed_func();
    }
}

void Http_clt::disconnect(void (*completed_func)(void *), void *param)
{
    ALL("Http_clt::disconnect");
    _status = HTTP_CLT_DISCONNECTED;
    _completed_func = completed_func;
    _param = param;

    // there is no need to delete this->request, http_send will do it
    // if (this->request)
    //     delete[] this->request;

    espconn_disconnect(&_esp_conn);
    call_completed_func();
}

void Http_clt::send_req(char *t_msg, int msg_len, void (*completed_func)(void *), void *param)
{
    ALL("Http_clt::send_req");
    _completed_func = completed_func;
    _param = param;
    this->request = new char[msg_len + 1];
    if (this->request == NULL)
    {
        dia_error_evnt(HTTP_CLT_SEND_REQ_HEAP_EXHAUSTED, (msg_len + 1));
        ERROR("Http_clt::send_req heap exhausted %d", (msg_len + 1));
        return;
    }
    req_len = msg_len;
    os_memcpy(this->request, t_msg, req_len);

    switch (_status)
    {
    case HTTP_CLT_CONNECTED:
    case HTTP_CLT_RESPONSE_READY:
        DEBUG("Http_clt::send_req msg %s\n", this->request);
        http_send(&_esp_conn, this->request, this->req_len);
        _status = HTTP_CLT_WAITING_RESPONSE;
        os_timer_disarm(&_send_req_timeout_timer);
        os_timer_setfn(&_send_req_timeout_timer, (os_timer_func_t *)http_clt_send_req_timeout_function, (void *)this);
        os_timer_arm(&_send_req_timeout_timer, _comm_timeout, 0);
        break;
    default:
        // esp_diag.info(HTTP_CLT_SEND_REQ_CANNOT_SEND_REQ, _status);
        INFO("Http_clt::send_req - cannot send request status is %s", _status);
        _status = HTTP_CLT_CANNOT_SEND_REQUEST;
        call_completed_func();
        break;
    }
    mem_mon_stack();
}

Http_clt_status_type Http_clt::get_status(void)
{
    return _status;
}

void Http_clt::update_status(Http_clt_status_type t_status)
{
    _status = t_status;
    print_status();
}

void Http_clt::call_completed_func(void)
{
    if (_completed_func)
        _completed_func(_param);
}

void Http_clt::print_status(void)
{
    char *status;
    switch (_status)
    {
    case HTTP_CLT_RESPONSE_READY:
        status = (char *)f_str("RESPONSE_READY");
        break;
    case HTTP_CLT_DISCONNECTED:
        status = (char *)f_str("DISCONNECTED");
        break;
    case HTTP_CLT_CONNECTING:
        status = (char *)f_str("CONNECTING");
        break;
    case HTTP_CLT_CONNECTED:
        status = (char *)f_str("CONNECTED");
        break;
    case HTTP_CLT_WAITING_RESPONSE:
        status = (char *)f_str("WAITING_RESPONSE");
        break;
    case HTTP_CLT_CONNECT_FAILURE:
        status = (char *)f_str("CONNECT_FAILURE");
        break;
    case HTTP_CLT_CONNECT_TIMEOUT:
        status = (char *)f_str("CONNECT_TIMEOUT");
        break;
    case HTTP_CLT_RESPONSE_ERROR:
        status = (char *)f_str("RESPONSE_ERROR");
        break;
    case HTTP_CLT_CANNOT_SEND_REQUEST:
        status = (char *)f_str("CANNOT_SEND_REQUEST");
        break;
    case HTTP_CLT_RESPONSE_TIMEOUT:
        status = (char *)f_str("RESPONSE_TIMEOUT");
        break;
    default:
        status = (char *)f_str("UNKNOWN");
        break;
    }
    TRACE("Http_clt status --> %s", status);
    mem_mon_stack();
}