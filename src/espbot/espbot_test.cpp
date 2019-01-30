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
#include "do_sequence.h"
#include "esp8266_io.h"
#include "gpio.h"
}

#include "espbot_test.hpp"
#include "espbot_global.hpp"
#include "debug.hpp"

// function for testing purpose

static int test_counter;

static os_timer_t test_timer;

static void ICACHE_FLASH_ATTR test_function(void)
{
    esplog.all("test_function\n");
    run_test();
}

static struct ip_addr ota_server_ip;
static int ota_server_port;
static char *ota_request;

void ICACHE_FLASH_ATTR init_test(struct ip_addr ip, uint32 port, char *request)
{
    esplog.all("init_test\n");
    os_memcpy(&ota_server_ip, &ip, sizeof(struct ip_addr));
    ota_server_port = port;
    ota_request = (char *)esp_zalloc(48 + 12 + os_strlen(request));
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
    esplog.all("run_test\n");
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

static void ICACHE_FLASH_ATTR sequence_completed(void *param)
{
    struct do_seq *seq = (struct do_seq *)param;
    free_sequence(seq);
    os_printf("Test completed\n");
}

void ICACHE_FLASH_ATTR run_test(int idx)
{
    struct do_seq *seq;
    switch (idx)
    {
    case 1:
    {
        PIN_FUNC_SELECT(ESPBOT_D4_MUX, ESPBOT_D4_FUNC);
        GPIO_OUTPUT_SET(ESPBOT_D4_NUM, ESPBOT_HIGH);
        seq = new_sequence(ESPBOT_D4_NUM, 1);
        set_sequence_cb(seq, sequence_completed, (void *)seq);

        sequence_clear(seq);
        sequence_add(seq, ESPBOT_LOW, 1000);
        {
            int idx = 0;
            char level;
            uint32 duration;
            os_printf("Sequence defined as:\n");
            for (idx = 0; idx < get_sequence_length(seq); idx++)
            {
                level = get_sequence_pulse_level(seq, idx);
                duration = get_sequence_pulse_duration(seq, idx);
                if (level == ESPBOT_LOW)
                    os_printf("pulse %d: level  'LOW' - duration %d\n", idx, duration);
                else
                    os_printf("pulse %d: level 'HIGH' - duration %d\n", idx, duration);
            }
            os_printf("Sequence end.\n");
        }
        exe_sequence_ms(seq);
    }
    break;
    case 2:
    {
        PIN_FUNC_SELECT(ESPBOT_D4_MUX, ESPBOT_D4_FUNC);
        GPIO_OUTPUT_SET(ESPBOT_D4_NUM, ESPBOT_HIGH);
        seq = new_sequence(ESPBOT_D4_NUM, 9);
        set_sequence_cb(seq, sequence_completed, (void *)seq);

        sequence_clear(seq);
        sequence_add(seq, ESPBOT_LOW, 1000);
        sequence_add(seq, ESPBOT_HIGH, 1500);
        sequence_add(seq, ESPBOT_LOW, 2000);
        sequence_add(seq, ESPBOT_HIGH, 2500);
        sequence_add(seq, ESPBOT_LOW, 3000);
        sequence_add(seq, ESPBOT_HIGH, 3500);
        sequence_add(seq, ESPBOT_LOW, 4000);
        sequence_add(seq, ESPBOT_HIGH, 4500);
        sequence_add(seq, ESPBOT_LOW, 5000);
        {
            int idx = 0;
            char level;
            uint32 duration;
            os_printf("Sequence defined as:\n");
            for (idx = 0; idx < get_sequence_length(seq); idx++)
            {
                level = get_sequence_pulse_level(seq, idx);
                duration = get_sequence_pulse_duration(seq, idx);
                if (level == ESPBOT_LOW)
                    os_printf("pulse %d: level  'LOW' - duration %d\n", idx, duration);
                else
                    os_printf("pulse %d: level 'HIGH' - duration %d\n", idx, duration);
            }
            os_printf("Sequence end.\n");
        }
        exe_sequence_ms(seq);
    }
    break;
    case 3:
    {
        // sequence 1
        PIN_FUNC_SELECT(ESPBOT_D4_MUX, ESPBOT_D4_FUNC);
        GPIO_OUTPUT_SET(ESPBOT_D4_NUM, ESPBOT_HIGH);
        seq = new_sequence(ESPBOT_D4_NUM, 9);
        set_sequence_cb(seq, sequence_completed, (void *)seq);

        sequence_clear(seq);
        sequence_add(seq, ESPBOT_LOW, 1000);
        sequence_add(seq, ESPBOT_HIGH, 1500);
        sequence_add(seq, ESPBOT_LOW, 2000);
        sequence_add(seq, ESPBOT_HIGH, 2500);
        sequence_add(seq, ESPBOT_LOW, 3000);
        sequence_add(seq, ESPBOT_HIGH, 3500);
        sequence_add(seq, ESPBOT_LOW, 4000);
        sequence_add(seq, ESPBOT_HIGH, 4500);
        sequence_add(seq, ESPBOT_LOW, 5000);
        {
            int idx = 0;
            char level;
            uint32 duration;
            os_printf("Sequence defined as:\n");
            for (idx = 0; idx < get_sequence_length(seq); idx++)
            {
                level = get_sequence_pulse_level(seq, idx);
                duration = get_sequence_pulse_duration(seq, idx);
                if (level == ESPBOT_LOW)
                    os_printf("pulse %d: level  'LOW' - duration %d\n", idx, duration);
                else
                    os_printf("pulse %d: level 'HIGH' - duration %d\n", idx, duration);
            }
            os_printf("Sequence end.\n");
        }
        // sequence 2
        struct do_seq *seq_2;
        PIN_FUNC_SELECT(ESPBOT_D5_MUX, ESPBOT_D5_FUNC);
        GPIO_OUTPUT_SET(ESPBOT_D5_NUM, ESPBOT_HIGH);
        seq_2 = new_sequence(ESPBOT_D5_NUM, 8);
        set_sequence_cb(seq_2, sequence_completed, (void *)seq_2);

        sequence_clear(seq_2);
        sequence_add(seq_2, ESPBOT_LOW, 2000);
        sequence_add(seq_2, ESPBOT_HIGH, 3000);
        sequence_add(seq_2, ESPBOT_LOW, 2000);
        sequence_add(seq_2, ESPBOT_HIGH, 3000);
        sequence_add(seq_2, ESPBOT_LOW, 2000);
        sequence_add(seq_2, ESPBOT_HIGH, 3000);
        sequence_add(seq_2, ESPBOT_LOW, 2000);
        sequence_add(seq_2, ESPBOT_HIGH, 3000);
        {
            int idx = 0;
            char level;
            uint32 duration;
            os_printf("Sequence defined as:\n");
            for (idx = 0; idx < get_sequence_length(seq_2); idx++)
            {
                level = get_sequence_pulse_level(seq_2, idx);
                duration = get_sequence_pulse_duration(seq_2, idx);
                if (level == ESPBOT_LOW)
                    os_printf("pulse %d: level  'LOW' - duration %d\n", idx, duration);
                else
                    os_printf("pulse %d: level 'HIGH' - duration %d\n", idx, duration);
            }
            os_printf("Sequence end.\n");
        }
        exe_sequence_ms(seq);
        exe_sequence_ms(seq_2);
    }
    break;
    default:
        break;
    }
}