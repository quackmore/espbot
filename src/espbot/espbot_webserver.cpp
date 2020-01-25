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
#include "espbot_global.hpp"
#include "espbot_http.hpp"
#include "espbot_http_routes.hpp"
#include "espbot_json.hpp"
#include "espbot_logger.hpp"
#include "espbot_mem_mon.hpp"
#include "espbot_queue.hpp"
#include "espbot_webserver.hpp"
#include "espbot_utils.hpp"

static void webserver_recv(void *arg, char *precdata, unsigned short length)
{
    esplog.all("webserver_recv\n");
    struct espconn *ptr_espconn = (struct espconn *)arg;
    espmem.stack_mon();
    Http_parsed_req parsed_req;

    esplog.trace("Websvr::webserver_recv received request len:%u\n", length);
    esplog.trace("Websvr::webserver_recv received request:\n%s\n", precdata);

    http_parse_request(precdata, &parsed_req);

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
        http_save_pending_request(arg, precdata, length, &parsed_req);
        return;
    }
    if (parsed_req.no_header_message)
    {
        esplog.debug("Websvr::webserver_recv - No header message\n");
        http_check_pending_requests(ptr_espconn, parsed_req.req_content, webserver_recv);
        return;
    }
    if (parsed_req.url == NULL)
    {
        esplog.debug("Websvr::webserver_recv - Empty url\n");
        return;
    }
    espbot_http_routes(ptr_espconn, &parsed_req);
}

static void webserver_recon(void *arg, sint8 err)
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

static void webserver_discon(void *arg)
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

static void webserver_listen(void *arg)
{
    esplog.all("webserver_listen\n");
    struct espconn *pesp_conn = (struct espconn *)arg;
    espmem.stack_mon();
    espconn_regist_recvcb(pesp_conn, webserver_recv);
    espconn_regist_sentcb(pesp_conn, http_sentcb);
    espconn_regist_reconcb(pesp_conn, webserver_recon);
    espconn_regist_disconcb(pesp_conn, webserver_discon);
}

void Websvr::init(void)
{
    esplog.all("Websvr::init\n");

    // setup specific controllers timer
    init_controllers();
}

void Websvr::start(uint32 port)
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

void Websvr::stop()
{
    esplog.all("Websvr::stop\n");
    espconn_disconnect(&m_esp_conn);
    espconn_delete(&m_esp_conn);
    m_status = down;
    
    http_queues_clear();

    esplog.debug("web server stopped\n");
}

Websvr_status Websvr::get_status(void)
{
    return m_status;
}