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
#include "espbot_mem_mon.hpp"
#include "espbot_queue.hpp"
#include "espbot_webserver.hpp"
#include "espbot_utils.hpp"

static void webserver_recv(void *arg, char *precdata, unsigned short length)
{
    struct espconn *ptr_espconn = (struct espconn *)arg;
    Http_parsed_req parsed_req;
    DEBUG("webserver_recv len %u, msg %s", length, precdata);
    http_parse_request(precdata, length, &parsed_req);
    system_soft_wdt_feed();
    TRACE("webserver_recv parsed req\n"
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
    if (!parsed_req.no_header_message && (parsed_req.h_content_len > parsed_req.content_len))
    {
        TRACE("webserver_recv message has been splitted waiting for completion ...");
        http_save_pending_request(arg, precdata, length, &parsed_req);
        return;
    }
    if (parsed_req.no_header_message)
    {
        TRACE("webserver_recv no header message");
        http_check_pending_requests(ptr_espconn, parsed_req.req_content, webserver_recv);
        return;
    }
    if (parsed_req.url == NULL)
    {
        esp_diag.debug(WEB_SERVER_EMPTY_URL);
        DEBUG("webserver_recv empty url");
        return;
    }
    espbot_http_routes(ptr_espconn, &parsed_req);
    espmem.stack_mon();
}

static void webserver_recon(void *arg, sint8 err)
{
    struct espconn *pesp_conn = (struct espconn *)arg;
    espmem.stack_mon();
    DEBUG("webserver %d.%d.%d.%d:%d err %d reconnect",
          pesp_conn->proto.tcp->remote_ip[0],
          pesp_conn->proto.tcp->remote_ip[1],
          pesp_conn->proto.tcp->remote_ip[2],
          pesp_conn->proto.tcp->remote_ip[3],
          pesp_conn->proto.tcp->remote_port,
          err);
}

static void webserver_discon(void *arg)
{
    struct espconn *pesp_conn = (struct espconn *)arg;
    espmem.stack_mon();
    DEBUG("webserver %d.%d.%d.%d:%d disconnect",
          pesp_conn->proto.tcp->remote_ip[0],
          pesp_conn->proto.tcp->remote_ip[1],
          pesp_conn->proto.tcp->remote_ip[2],
          pesp_conn->proto.tcp->remote_ip[3],
          pesp_conn->proto.tcp->remote_port);
}

static void webserver_listen(void *arg)
{
    ALL("webserver_listen");
    struct espconn *pesp_conn = (struct espconn *)arg;
    espmem.stack_mon();
    espconn_regist_recvcb(pesp_conn, webserver_recv);
    espconn_regist_sentcb(pesp_conn, http_sentcb);
    espconn_regist_reconcb(pesp_conn, webserver_recon);
    espconn_regist_disconcb(pesp_conn, webserver_discon);
}

void Websvr::init(void)
{
    // setup specific controllers timer
    init_controllers();
}

void Websvr::start(uint32 port)
{
    // setup sdk TCP variables
    m_esp_conn.type = ESPCONN_TCP;
    m_esp_conn.state = ESPCONN_NONE;
    m_esp_conn.proto.tcp = &m_esptcp;
    m_esp_conn.proto.tcp->local_port = port;
    espconn_regist_connectcb(&m_esp_conn, webserver_listen);
    espconn_accept(&m_esp_conn);

    // now the server is up
    m_status = up;
    esp_diag.debug(WEB_SERVER_START);
    DEBUG("webserver started");
}

void Websvr::stop()
{
    espconn_disconnect(&m_esp_conn);
    espconn_delete(&m_esp_conn);
    m_status = down;

    http_queues_clear();

    esp_diag.debug(WEB_SERVER_STOP);
    DEBUG("webserver stopped");
}

Websvr_status Websvr::get_status(void)
{
    return m_status;
}