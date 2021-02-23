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
#include "c_types.h"
#include "espconn.h"
}

#include "espbot.hpp"
#include "espbot_diagnostic.hpp"
#include "espbot_event_codes.h"
#include "espbot_http.hpp"
#include "espbot_http_routes.hpp"
#include "espbot_json.hpp"
#include "espbot_mem_mon.hpp"
#include "espbot_queue.hpp"
#include "espbot_http_server.hpp"
#include "espbot_utils.hpp"

static struct
{
    Http_svr_status status;
    struct espconn esp_conn;
    esp_tcp esptcp;
} http_svr_state;

static void http_svr_recv(void *arg, char *precdata, unsigned short length)
{
    struct espconn *ptr_espconn = (struct espconn *)arg;
    Http_parsed_req parsed_req;
    DEBUG("http_svr_recv on %X, len %u, msg %s", ptr_espconn, length, precdata);
    http_parse_request(precdata, length, &parsed_req);
    TRACE("http_svr_recv parsed req\n"
          "no_header_message: %d\n"
          "           method: %d\n"
          "              url: %s\n"
          "      content len: %d\n"
          "          content: %s",
          parsed_req.no_header_message,
          parsed_req.req_method,
          parsed_req.url,
          parsed_req.content_len,
          parsed_req.req_content);
    mem_mon_stack();
    if (!parsed_req.no_header_message && (parsed_req.h_content_len > parsed_req.content_len))
    {
        TRACE("http_svr_recv message has been splitted waiting for completion ...");
        http_save_pending_request(arg, precdata, length, &parsed_req);
        return;
    }
    if (parsed_req.no_header_message)
    {
        TRACE("http_svr_recv no header message");
        http_check_pending_requests(ptr_espconn, parsed_req.req_content, parsed_req.content_len, http_svr_recv);
        return;
    }
    if (parsed_req.url == NULL)
    {
        dia_debug_evnt(HTTP_SVR_EMPTY_URL);
        DEBUG("http_svr_recv empty url");
        return;
    }
    system_soft_wdt_feed();
    espbot_http_routes(ptr_espconn, &parsed_req);
}

static void http_svr_recon(void *arg, sint8 err)
{
    struct espconn *pesp_conn = (struct espconn *)arg;
    mem_mon_stack();
    DEBUG("http_svr %d.%d.%d.%d:%d err %d reconnect",
          pesp_conn->proto.tcp->remote_ip[0],
          pesp_conn->proto.tcp->remote_ip[1],
          pesp_conn->proto.tcp->remote_ip[2],
          pesp_conn->proto.tcp->remote_ip[3],
          pesp_conn->proto.tcp->remote_port,
          err);
}

static void http_svr_discon(void *arg)
{
    struct espconn *pesp_conn = (struct espconn *)arg;
    mem_mon_stack();
    DEBUG("http_svr %d.%d.%d.%d:%d disconnect",
          pesp_conn->proto.tcp->remote_ip[0],
          pesp_conn->proto.tcp->remote_ip[1],
          pesp_conn->proto.tcp->remote_ip[2],
          pesp_conn->proto.tcp->remote_ip[3],
          pesp_conn->proto.tcp->remote_port);
}

static void http_svr_listen(void *arg)
{
    ALL("http_svr_listen");
    struct espconn *pesp_conn = (struct espconn *)arg;
    mem_mon_stack();
    espconn_regist_recvcb(pesp_conn, http_svr_recv);
    espconn_regist_sentcb(pesp_conn, http_sentcb);
    espconn_regist_reconcb(pesp_conn, http_svr_recon);
    espconn_regist_disconcb(pesp_conn, http_svr_discon);
}

void http_svr_init(void)
{
    // setup specific controllers timer 'espbot_http_routes.cpp'
    init_controllers();
    http_svr_state.status = http_svr_down;
}

void http_svr_start(uint32 port)
{
    if (http_svr_state.status == http_svr_down)
    { // setup sdk TCP variables
        http_svr_state.esp_conn.type = ESPCONN_TCP;
        http_svr_state.esp_conn.state = ESPCONN_NONE;
        http_svr_state.esp_conn.proto.tcp = &http_svr_state.esptcp;
        http_svr_state.esp_conn.proto.tcp->local_port = port;
        espconn_regist_connectcb(&http_svr_state.esp_conn, http_svr_listen);
        espconn_accept(&http_svr_state.esp_conn);

        // now the server is up
        http_svr_state.status = http_svr_up;
        dia_info_evnt(HTTP_SVR_START);
        INFO("http_svr started");
    }
}

void http_svr_stop()
{
    if (http_svr_state.status == http_svr_up)
    {
        http_svr_state.status = http_svr_down;
        espconn_disconnect(&http_svr_state.esp_conn);
        espconn_delete(&http_svr_state.esp_conn);

        http_queues_clear();

        dia_info_evnt(HTTP_SVR_STOP);
        INFO("http_svr stopped");
    }
}

Http_svr_status http_svr_get_status(void)
{
    return http_svr_state.status;
}