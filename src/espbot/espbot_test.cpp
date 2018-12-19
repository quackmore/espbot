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
#include "mem.h"
#include "user_interface.h"
#include "ip_addr.h"
}

#include "espbot_test.hpp"
#include "espbot_global.hpp"

// function for testing purpose

static int test_counter;

static os_timer_t test_timer;

static void ICACHE_FLASH_ATTR test_function(void)
{
    run_test();
}

static struct ip_addr ota_server_ip;
static int ota_server_port;
static char *ota_request;

void ICACHE_FLASH_ATTR init_test(struct ip_addr ip, uint32 port, char *request)
{
    os_memcpy(&ota_server_ip, &ip, sizeof(struct ip_addr));
    ota_server_port = port;
    ota_request = (char *)os_zalloc(48 + 12 + os_strlen(request));
    if (ota_request == NULL)
    {
        os_printf("os_zalloc error\n");
        ota_request = NULL;
    }
    else
    {
        uint32 *tmp_ptr = &ip.addr;
        os_sprintf(ota_request,
                   "%s\r\nHost: %d.%d.%d.%d:%d\r\n\r\n",
                   request,
                   ((char *)tmp_ptr)[0],
                   ((char *)tmp_ptr)[1],
                   ((char *)tmp_ptr)[2],
                   ((char *)tmp_ptr)[3],
                   port);
        os_printf("request: %s\n", ota_request);
    }
}

void ICACHE_FLASH_ATTR run_test(void)
{
    system_soft_wdt_feed();
    switch (espwebclnt.get_status())
    {
    case WEBCLNT_DISCONNECTED:
        os_printf("Starting connection to server ...\n");
        os_timer_setfn(&test_timer, (os_timer_func_t *)test_function, NULL);
        os_timer_arm(&test_timer, 500, 0);
        espwebclnt.connect(ota_server_ip, ota_server_port);
        break;
    case WEBCLNT_CONNECTING:
        os_printf("Connecting to server ...\n");
        os_timer_setfn(&test_timer, (os_timer_func_t *)test_function, NULL);
        os_timer_arm(&test_timer, 500, 0);
        break;
    case WEBCLNT_CONNECTED:
        os_printf("Sending request to server ...\n");
        os_timer_setfn(&test_timer, (os_timer_func_t *)test_function, NULL);
        os_timer_arm(&test_timer, 500, 0);
        espwebclnt.send_req(ota_request);
        break;
    case WEBCLNT_WAITING_RESPONSE:
        os_printf("Awaiting for response from server ...\n");
        os_timer_setfn(&test_timer, (os_timer_func_t *)test_function, NULL);
        os_timer_arm(&test_timer, 500, 0);
        break;
    case WEBCLNT_RESPONSE_READY:
        os_printf("Received Response: %s\n", espwebclnt.m_response->body);
        espwebclnt.disconnect();
        espwebclnt.free_response();
        break;
    default:
        os_printf("Test aborted because of an error encountered\n");
        espwebclnt.disconnect();
        break;
    }
}