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
#include "c_types.h"
#include "esp8266_io.h"
#include "gpio.h"
#include "ip_addr.h"
#include "osapi.h"
#include "library_di_sequence.h"
#include "library_do_sequence.h"
#include "mem.h"
#include "user_interface.h"
}

#include "app.hpp"
#include "app_test.hpp"
#include "espbot_cron.hpp"
#include "espbot_diagnostic.hpp"
#include "espbot_global.hpp"
#include "espbot_hal.h"
#include "espbot_http.hpp"
#include "espbot_json.hpp"
#include "espbot_mdns.hpp"
#include "espbot_mem_mon.hpp"
#include "espbot_mem_macros.h"
#include "espbot_profiler.hpp"
#include "espbot_timedate.hpp"
#include "espbot_utils.hpp"
#include "espbot_webclient.hpp"

#ifdef TEST_FUNCTIONS
// function for testing purpose

static int test_counter;

static os_timer_t test_timer;

static void test_function(void)
{
    // ALL("test_function");
    // run_test();
}

static struct ip_addr host_ip;
static int host_port;
static char *client_request;
static Webclnt *espclient;

void init_test(struct ip_addr ip, uint32 port, char *request)
{
    // ALL("init_test");
    os_memcpy(&host_ip, &ip, sizeof(struct ip_addr));
    host_port = port;
    char ip_str[16];
    os_memset(ip_str, 0, 16);
    fs_sprintf(ip_str, IPSTR, IP2STR(&ip));
    fs_printf("------> ip: %s\n", ip_str);
    client_request = new char[38 + os_strlen(ip_str) + os_strlen(request) + 1];
    if (client_request == NULL)
    {
        fs_printf("fs_zalloc error\n");
        client_request = NULL;
    }
    else
    {
        // GET /user1.bin HTTP/1.1
        // Host: 192.168.1.201
        // Range: bytes=0-1023
        fs_sprintf(client_request, "%s\r\nHost: %s\r\nRange: bytes=0-1023\r\n\r\n", request, ip_str);
        // os_printf("client_request length: %d, effective length: %d , request: %s\n",
        //          (os_strlen(request) + 1),
        //          os_strlen(client_request),
        //          client_request);
    }
    espclient = new Webclnt;
}

void free_client(void *)
{
    delete espclient;
}

void check_version(void *param)
{
    // ALL("check_version");
    switch (espclient->get_status())
    {
    case WEBCLNT_RESPONSE_READY:
        if (espclient->parsed_response->body)
        {
            fs_printf("Server responded: %s\n", espclient->parsed_response->body);
        }
        break;
    default:
        fs_printf("wc_get_version: Ops ... webclient status is %d\n", espclient->get_status());
        break;
    }
    fs_printf("Webclient test completed\n");
    espclient->disconnect(free_client, NULL);
    // delete espclient;
}

void get_version(void *param)
{
    // ALL("get_version");
    switch (espclient->get_status())
    {
    case WEBCLNT_CONNECTED:
        fs_printf("Sending request [%s]\n", client_request);
        espclient->send_req(client_request, os_strlen(client_request), check_version, NULL);
        delete[] client_request;
        break;
    default:
        fs_printf("get_version: Ops ... webclient status is %d\n", espclient->get_status());
        fs_printf("Webclient test completed\n");
        espclient->disconnect(free_client, NULL);
        // delete espclient;
        break;
    }
}

void test_webclient(void)
{
    // webclient test
    // staus WEBCLNT_DISCONNECTED
    // connect to OTA server and get the version
    fs_printf("Starting connection to server ...\n");
    espclient->connect(host_ip, host_port, get_version, NULL);
}

void test_exe_task_fun(void)
{
    fs_printf("now executing a function called by a task\n");
}
/*
static void output_seq_completed(void *param)
{
    struct do_seq *seq = (struct do_seq *)param;
    free_do_seq(seq);
    os_printf("Test completed\n");
}

static void input_seq_completed(void *param)
{
    struct di_seq *seq = (struct di_seq *)param;
    if (seq->ended_by_timeout)
        os_printf("Input sequence reading ended by timeout timer\n");
    else
        os_printf("Input sequence reading completed\n");
    {
        int idx = 0;
        char level;
        uint32 duration;
        os_printf("Sequence acquired:\n");
        for (idx = 0; idx < get_di_seq_length(seq); idx++)
        {
            level = get_di_seq_pulse_level(seq, idx);
            duration = get_di_seq_pulse_duration(seq, idx);
            if (level == ESPBOT_LOW)
                os_printf("pulse %d: level  'LOW' - duration %d\n", idx, duration);
            else
                os_printf("pulse %d: level 'HIGH' - duration %d\n", idx, duration);
        }
        os_printf("Sequence end.\n");
    }
    free_di_seq(seq);
}

uint32 start_time;
uint32 end_time;

static di_seq *dht_input;

static void dht_reading_completed(void *param)
{
    struct di_seq *seq = (struct di_seq *)param;
    os_printf("start DHT -> start reading = %d\n", (end_time - start_time));
    if (seq->ended_by_timeout)
        os_printf("DHT reading ended by timeout timer\n");
    else
        os_printf("DHT reading completed\n");
    {
        int idx = 0;
        char level;
        uint32 duration;
        os_printf("DHT sequence acquired:\n");
        for (idx = 0; idx < get_di_seq_length(seq); idx++)
        {
            level = get_di_seq_pulse_level(seq, idx);
            duration = get_di_seq_pulse_duration(seq, idx);
            if (level == ESPBOT_LOW)
                os_printf("pulse %d: level  'LOW' - duration %d\n", idx, duration);
            else
                os_printf("pulse %d: level 'HIGH' - duration %d\n", idx, duration);
        }
        os_printf("DHT sequence end.\n");
    }
    free_di_seq(seq);
}

static void IRAM dht_start_completed(void *param)
{
    struct do_seq *seq = (struct do_seq *)param;
    // start reading from DHT
    PIN_FUNC_SELECT(ESPBOT_D2_MUX, ESPBOT_D2_FUNC);
    PIN_PULLUP_EN(ESPBOT_D2_MUX);
    GPIO_DIS_OUTPUT(ESPBOT_D2_NUM);

    read_di_sequence(dht_input);

    // free output sequence
    free_do_seq(seq);
}
*/

void run_test(int32 idx, int32 param)
{
    struct do_seq *seq;
    static int event_counter = 0;
    INFO("Running test %d with param %d", idx, param);
    switch (idx)
    {
    case 1:
    {
        while (1)
        {
            ;
        }
        break;
    }
    case 2:
    {
        uint32 *exc_ptr = NULL;
        uint32 number = *exc_ptr;
        break;
    }
    case 3:
    {
        system_soft_wdt_stop();
        break;
    }
    case 4:
    {
        int idx;
        uint32 val;
        fs_printf("RTC stack dump\n");
        for (idx = 0; idx < 20; idx++)
        {
            system_rtc_mem_read((66 + idx), (void *)&val, 4);
            fs_printf("%d %X\n", idx, val);
        }
        break;
    }
    case 5:
    {
        int idx;
        uint32 val = 0;
        fs_printf("filling RTC stack dump with 0\n");
        for (idx = 0; idx < 20; idx++)
        {
            system_rtc_mem_write((66 + idx), (void *)&val, 4);
        }
        break;
    }
    case 6:
    {
        fs_printf("SP address: %X\n", get_last_crash_SP());
        break;
    }
    case 7:
    {
        uint32_t value;
        if (get_last_crash_stack_dump(0, &value) == 0)
            fs_printf("value: %X\n", value);
        else
            fs_printf("error retrieving value\n");
        break;
    }
    case 8:
    {
        uint32_t value;
        if (get_last_crash_stack_dump(1, &value) == 0)
            fs_printf("value: %X\n", value);
        else
            fs_printf("error retrieving value\n");
        break;
    }
/*
    case 10:
    {
        os_printf("installing EXCCAUSE_ILLEGAL\n");
        install_exc(0);
        break;
    }
    case 11:
    {
        os_printf("installing EXCCAUSE_SYSCALL\n");
        install_exc(1);
        break;
    }
    case 12:
    {
        os_printf("installing EXCCAUSE_INSTR_ERROR\n");
        install_exc(2);
        break;
    }
    case 13:
    {
        os_printf("installing EXCCAUSE_LOAD_STORE_ERROR\n");
        install_exc(3);
        break;
    }
    case 14:
    {
        os_printf("installing EXCCAUSE_DIVIDE_BY_ZERO\n");
        install_exc(4);
        break;
    }
    case 15:
    {
        os_printf("installing EXCCAUSE_UNALIGNED\n");
        install_exc(5);
        break;
    }
    case 16:
    {
        os_printf("installing EXCCAUSE_INSTR_DATA_ERROR\n");
        install_exc(6);
        break;
    }
    case 17:
    {
        os_printf("installing EXCCAUSE_LOAD_STORE_DATA_ERROR\n");
        install_exc(7);
        break;
    }
    case 18:
    {
        os_printf("installing EXCCAUSE_INSTR_ADDR_ERROR\n");
        install_exc(8);
        break;
    }
    case 19:
    {
        os_printf("installing EXCCAUSE_LOAD_STORE_ADDR_ERROR\n");
        install_exc(9);
        break;
    }
    case 20:
    {
        os_printf("installing EXCCAUSE_INSTR_PROHIBITED\n");
        install_exc(10);
        break;
    }
    case 21:
    {
        os_printf("installing EXCCAUSE_LOAD_PROHIBITED\n");
        install_exc(11);
        break;
    }
    case 22:
    {
        os_printf("installing EXCCAUSE_STORE_PROHIBITED\n");
        install_exc(12);
        break;
    }
*/
        /*
    case 1:
    {
        PIN_FUNC_SELECT(ESPBOT_D4_MUX, ESPBOT_D4_FUNC);
        GPIO_OUTPUT_SET(ESPBOT_D4_NUM, ESPBOT_HIGH);
        seq = new_do_seq(ESPBOT_D4_NUM, 1);
        set_do_seq_cb(seq, output_seq_completed, (void *)seq, task);

        out_seq_clear(seq);
        out_seq_add(seq, ESPBOT_LOW, 1000);
        {
            int idx = 0;
            char level;
            uint32 duration;
            os_printf("Sequence defined as:\n");
            for (idx = 0; idx < get_do_seq_length(seq); idx++)
            {
                level = get_do_seq_pulse_level(seq, idx);
                duration = get_do_seq_pulse_duration(seq, idx);
                if (level == ESPBOT_LOW)
                    os_printf("pulse %d: level  'LOW' - duration %d\n", idx, duration);
                else
                    os_printf("pulse %d: level 'HIGH' - duration %d\n", idx, duration);
            }
            os_printf("Sequence end.\n");
        }
        exe_do_seq_ms(seq);
    }
    break;
    case 2:
    {
        PIN_FUNC_SELECT(ESPBOT_D4_MUX, ESPBOT_D4_FUNC);
        GPIO_OUTPUT_SET(ESPBOT_D4_NUM, ESPBOT_HIGH);
        seq = new_do_seq(ESPBOT_D4_NUM, 9);
        set_do_seq_cb(seq, output_seq_completed, (void *)seq, task);

        out_seq_clear(seq);
        out_seq_add(seq, ESPBOT_LOW, 1000);
        out_seq_add(seq, ESPBOT_HIGH, 1500);
        out_seq_add(seq, ESPBOT_LOW, 2000);
        out_seq_add(seq, ESPBOT_HIGH, 2500);
        out_seq_add(seq, ESPBOT_LOW, 3000);
        out_seq_add(seq, ESPBOT_HIGH, 3500);
        out_seq_add(seq, ESPBOT_LOW, 4000);
        out_seq_add(seq, ESPBOT_HIGH, 4500);
        out_seq_add(seq, ESPBOT_LOW, 5000);
        {
            int idx = 0;
            char level;
            uint32 duration;
            os_printf("Sequence defined as:\n");
            for (idx = 0; idx < get_do_seq_length(seq); idx++)
            {
                level = get_do_seq_pulse_level(seq, idx);
                duration = get_do_seq_pulse_duration(seq, idx);
                if (level == ESPBOT_LOW)
                    os_printf("pulse %d: level  'LOW' - duration %d\n", idx, duration);
                else
                    os_printf("pulse %d: level 'HIGH' - duration %d\n", idx, duration);
            }
            os_printf("Sequence end.\n");
        }
        exe_do_seq_ms(seq);
    }
    break;
    case 3:
    {
        // sequence 1
        PIN_FUNC_SELECT(ESPBOT_D4_MUX, ESPBOT_D4_FUNC);
        GPIO_OUTPUT_SET(ESPBOT_D4_NUM, ESPBOT_HIGH);
        seq = new_do_seq(ESPBOT_D4_NUM, 9);
        set_do_seq_cb(seq, output_seq_completed, (void *)seq, task);

        out_seq_clear(seq);
        out_seq_add(seq, ESPBOT_LOW, 1000);
        out_seq_add(seq, ESPBOT_HIGH, 1500);
        out_seq_add(seq, ESPBOT_LOW, 2000);
        out_seq_add(seq, ESPBOT_HIGH, 2500);
        out_seq_add(seq, ESPBOT_LOW, 3000);
        out_seq_add(seq, ESPBOT_HIGH, 3500);
        out_seq_add(seq, ESPBOT_LOW, 4000);
        out_seq_add(seq, ESPBOT_HIGH, 4500);
        out_seq_add(seq, ESPBOT_LOW, 5000);
        {
            int idx = 0;
            char level;
            uint32 duration;
            os_printf("Sequence defined as:\n");
            for (idx = 0; idx < get_do_seq_length(seq); idx++)
            {
                level = get_do_seq_pulse_level(seq, idx);
                duration = get_do_seq_pulse_duration(seq, idx);
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
        seq_2 = new_do_seq(ESPBOT_D5_NUM, 8);
        set_do_seq_cb(seq_2, output_seq_completed, (void *)seq_2, task);

        out_seq_clear(seq_2);
        out_seq_add(seq_2, ESPBOT_LOW, 2000);
        out_seq_add(seq_2, ESPBOT_HIGH, 3000);
        out_seq_add(seq_2, ESPBOT_LOW, 2000);
        out_seq_add(seq_2, ESPBOT_HIGH, 3000);
        out_seq_add(seq_2, ESPBOT_LOW, 2000);
        out_seq_add(seq_2, ESPBOT_HIGH, 3000);
        out_seq_add(seq_2, ESPBOT_LOW, 2000);
        out_seq_add(seq_2, ESPBOT_HIGH, 3000);
        {
            int idx = 0;
            char level;
            uint32 duration;
            os_printf("Sequence defined as:\n");
            for (idx = 0; idx < get_do_seq_length(seq_2); idx++)
            {
                level = get_do_seq_pulse_level(seq_2, idx);
                duration = get_do_seq_pulse_duration(seq_2, idx);
                if (level == ESPBOT_LOW)
                    os_printf("pulse %d: level  'LOW' - duration %d\n", idx, duration);
                else
                    os_printf("pulse %d: level 'HIGH' - duration %d\n", idx, duration);
            }
            os_printf("Sequence end.\n");
        }
        exe_do_seq_ms(seq);
        exe_do_seq_ms(seq_2);
    }
    break;
    case 4:
    {
        PIN_FUNC_SELECT(ESPBOT_D4_MUX, ESPBOT_D4_FUNC);
        GPIO_OUTPUT_SET(ESPBOT_D4_NUM, ESPBOT_HIGH);
        seq = new_do_seq(ESPBOT_D4_NUM, 9);
        set_do_seq_cb(seq, output_seq_completed, (void *)seq, task);

        out_seq_clear(seq);
        out_seq_add(seq, ESPBOT_LOW, 150);
        out_seq_add(seq, ESPBOT_HIGH, 150);
        out_seq_add(seq, ESPBOT_LOW, 150);
        out_seq_add(seq, ESPBOT_HIGH, 150);
        out_seq_add(seq, ESPBOT_LOW, 150);
        out_seq_add(seq, ESPBOT_HIGH, 150);
        out_seq_add(seq, ESPBOT_LOW, 150);
        out_seq_add(seq, ESPBOT_HIGH, 150);
        out_seq_add(seq, ESPBOT_LOW, 150);
        {
            int idx = 0;
            char level;
            uint32 duration;
            os_printf("Sequence defined as:\n");
            for (idx = 0; idx < get_do_seq_length(seq); idx++)
            {
                level = get_do_seq_pulse_level(seq, idx);
                duration = get_do_seq_pulse_duration(seq, idx);
                if (level == ESPBOT_LOW)
                    os_printf("pulse %d: level  'LOW' - duration %d\n", idx, duration);
                else
                    os_printf("pulse %d: level 'HIGH' - duration %d\n", idx, duration);
            }
            os_printf("Sequence end.\n");
        }
        exe_do_seq_ms(seq);
    }
    break;
    case 5:
    {
        PIN_FUNC_SELECT(ESPBOT_D4_MUX, ESPBOT_D4_FUNC);
        GPIO_OUTPUT_SET(ESPBOT_D4_NUM, ESPBOT_HIGH);
        seq = new_do_seq(ESPBOT_D4_NUM, 9);
        set_do_seq_cb(seq, output_seq_completed, (void *)seq, task);

        out_seq_clear(seq);
        out_seq_add(seq, ESPBOT_LOW, 150000);
        out_seq_add(seq, ESPBOT_HIGH, 150000);
        out_seq_add(seq, ESPBOT_LOW, 150000);
        out_seq_add(seq, ESPBOT_HIGH, 150000);
        out_seq_add(seq, ESPBOT_LOW, 150000);
        out_seq_add(seq, ESPBOT_HIGH, 150000);
        out_seq_add(seq, ESPBOT_LOW, 150000);
        out_seq_add(seq, ESPBOT_HIGH, 150000);
        out_seq_add(seq, ESPBOT_LOW, 150000);
        {
            int idx = 0;
            char level;
            uint32 duration;
            os_printf("Sequence defined as:\n");
            for (idx = 0; idx < get_do_seq_length(seq); idx++)
            {
                level = get_do_seq_pulse_level(seq, idx);
                duration = get_do_seq_pulse_duration(seq, idx);
                if (level == ESPBOT_LOW)
                    os_printf("pulse %d: level  'LOW' - duration %d\n", idx, duration);
                else
                    os_printf("pulse %d: level 'HIGH' - duration %d\n", idx, duration);
            }
            os_printf("Sequence end.\n");
        }
        exe_do_seq_us(seq);
    }
    break;
    case 6:
    {
        // checking input sequence reading (ms)
        // defining input sequence
        PIN_FUNC_SELECT(ESPBOT_D5_MUX, ESPBOT_D5_FUNC);
        PIN_PULLUP_EN(ESPBOT_D5_MUX);
        GPIO_DIS_OUTPUT(ESPBOT_D5_NUM);

        struct di_seq *input_seq = new_di_seq(ESPBOT_D5_NUM, 9, 20, TIMEOUT_MS);
        set_di_seq_cb(input_seq, input_seq_completed, (void *)input_seq, task);
        read_di_sequence(input_seq);

        // now the output
        PIN_FUNC_SELECT(ESPBOT_D4_MUX, ESPBOT_D4_FUNC);
        GPIO_OUTPUT_SET(ESPBOT_D4_NUM, ESPBOT_HIGH);
        seq = new_do_seq(ESPBOT_D4_NUM, 9);
        set_do_seq_cb(seq, output_seq_completed, (void *)seq, task);

        out_seq_clear(seq);
        out_seq_add(seq, ESPBOT_LOW, 1000);
        out_seq_add(seq, ESPBOT_HIGH, 1000);
        out_seq_add(seq, ESPBOT_LOW, 1000);
        out_seq_add(seq, ESPBOT_HIGH, 1000);
        out_seq_add(seq, ESPBOT_LOW, 1000);
        out_seq_add(seq, ESPBOT_HIGH, 1000);
        out_seq_add(seq, ESPBOT_LOW, 1000);
        out_seq_add(seq, ESPBOT_HIGH, 1000);
        out_seq_add(seq, ESPBOT_LOW, 1000);
        {
            int idx = 0;
            char level;
            uint32 duration;
            os_printf("Sequence defined as:\n");
            for (idx = 0; idx < get_do_seq_length(seq); idx++)
            {
                level = get_do_seq_pulse_level(seq, idx);
                duration = get_do_seq_pulse_duration(seq, idx);
                if (level == ESPBOT_LOW)
                    os_printf("pulse %d: level  'LOW' - duration %d\n", idx, duration);
                else
                    os_printf("pulse %d: level 'HIGH' - duration %d\n", idx, duration);
            }
            os_printf("Sequence end.\n");
        }
        exe_do_seq_us(seq);
    }
    break;
    case 7:
    {
        // checking input sequence reading (us)
        // defining input sequence
        PIN_FUNC_SELECT(ESPBOT_D5_MUX, ESPBOT_D5_FUNC);
        PIN_PULLUP_EN(ESPBOT_D5_MUX);
        GPIO_DIS_OUTPUT(ESPBOT_D5_NUM);

        struct di_seq *input_seq = new_di_seq(ESPBOT_D5_NUM, 9, 750000, TIMEOUT_US);
        set_di_seq_cb(input_seq, input_seq_completed, (void *)input_seq, task);

        // now the output
        PIN_FUNC_SELECT(ESPBOT_D4_MUX, ESPBOT_D4_FUNC);
        GPIO_OUTPUT_SET(ESPBOT_D4_NUM, ESPBOT_HIGH);
        seq = new_do_seq(ESPBOT_D4_NUM, 9);
        set_do_seq_cb(seq, output_seq_completed, (void *)seq, task);

        out_seq_clear(seq);
        out_seq_add(seq, ESPBOT_LOW, 5);
        out_seq_add(seq, ESPBOT_HIGH, 5);
        out_seq_add(seq, ESPBOT_LOW, 5);
        out_seq_add(seq, ESPBOT_HIGH, 5);
        out_seq_add(seq, ESPBOT_LOW, 5);
        out_seq_add(seq, ESPBOT_HIGH, 5);
        out_seq_add(seq, ESPBOT_LOW, 5);
        out_seq_add(seq, ESPBOT_HIGH, 5);
        out_seq_add(seq, ESPBOT_LOW, 5);
        {
            int idx = 0;
            char level;
            uint32 duration;
            os_printf("Sequence defined as:\n");
            for (idx = 0; idx < get_do_seq_length(seq); idx++)
            {
                level = get_do_seq_pulse_level(seq, idx);
                duration = get_do_seq_pulse_duration(seq, idx);
                if (level == ESPBOT_LOW)
                    os_printf("pulse %d: level  'LOW' - duration %d\n", idx, duration);
                else
                    os_printf("pulse %d: level 'HIGH' - duration %d\n", idx, duration);
            }
            os_printf("Sequence end.\n");
        }
        read_di_sequence(input_seq);
        exe_do_seq_ms(seq);
    }
    break;
    case 8:
    {
        // checking input sequence reading end by timeout (ms)
        // defining input sequence
        PIN_FUNC_SELECT(ESPBOT_D5_MUX, ESPBOT_D5_FUNC);
        PIN_PULLUP_EN(ESPBOT_D5_MUX);
        GPIO_DIS_OUTPUT(ESPBOT_D5_NUM);

        struct di_seq *input_seq = new_di_seq(ESPBOT_D5_NUM, 9, 20, TIMEOUT_MS);
        set_di_seq_cb(input_seq, input_seq_completed, (void *)input_seq, task);
        read_di_sequence(input_seq);

        // now the output
        PIN_FUNC_SELECT(ESPBOT_D4_MUX, ESPBOT_D4_FUNC);
        GPIO_OUTPUT_SET(ESPBOT_D4_NUM, ESPBOT_HIGH);
        seq = new_do_seq(ESPBOT_D4_NUM, 7);
        set_do_seq_cb(seq, output_seq_completed, (void *)seq, task);

        out_seq_clear(seq);
        out_seq_add(seq, ESPBOT_LOW, 1000);
        out_seq_add(seq, ESPBOT_HIGH, 1000);
        out_seq_add(seq, ESPBOT_LOW, 1000);
        out_seq_add(seq, ESPBOT_HIGH, 1000);
        out_seq_add(seq, ESPBOT_LOW, 1000);
        out_seq_add(seq, ESPBOT_HIGH, 1000);
        out_seq_add(seq, ESPBOT_LOW, 1000);
        out_seq_add(seq, ESPBOT_HIGH, 1000);
        out_seq_add(seq, ESPBOT_LOW, 1000);
        {
            int idx = 0;
            char level;
            uint32 duration;
            os_printf("Sequence defined as:\n");
            for (idx = 0; idx < get_do_seq_length(seq); idx++)
            {
                level = get_do_seq_pulse_level(seq, idx);
                duration = get_do_seq_pulse_duration(seq, idx);
                if (level == ESPBOT_LOW)
                    os_printf("pulse %d: level  'LOW' - duration %d\n", idx, duration);
                else
                    os_printf("pulse %d: level 'HIGH' - duration %d\n", idx, duration);
            }
            os_printf("Sequence end.\n");
        }
        exe_do_seq_us(seq);
    }
    break;
    case 9:
    {
        // checking input sequence reading end by timeout (us)
        // defining input sequence
        PIN_FUNC_SELECT(ESPBOT_D5_MUX, ESPBOT_D5_FUNC);
        PIN_PULLUP_EN(ESPBOT_D5_MUX);
        GPIO_DIS_OUTPUT(ESPBOT_D5_NUM);

        struct di_seq *input_seq = new_di_seq(ESPBOT_D5_NUM, 9, 50000, TIMEOUT_US);
        set_di_seq_cb(input_seq, input_seq_completed, (void *)input_seq, task);

        // now the output
        PIN_FUNC_SELECT(ESPBOT_D4_MUX, ESPBOT_D4_FUNC);
        GPIO_OUTPUT_SET(ESPBOT_D4_NUM, ESPBOT_HIGH);
        seq = new_do_seq(ESPBOT_D4_NUM, 7);
        set_do_seq_cb(seq, output_seq_completed, (void *)seq, task);

        out_seq_clear(seq);
        out_seq_add(seq, ESPBOT_LOW, 5);
        out_seq_add(seq, ESPBOT_HIGH, 5);
        out_seq_add(seq, ESPBOT_LOW, 5);
        out_seq_add(seq, ESPBOT_HIGH, 5);
        out_seq_add(seq, ESPBOT_LOW, 5);
        out_seq_add(seq, ESPBOT_HIGH, 5);
        out_seq_add(seq, ESPBOT_LOW, 5);
        out_seq_add(seq, ESPBOT_HIGH, 5);
        out_seq_add(seq, ESPBOT_LOW, 5);
        {
            int idx = 0;
            char level;
            uint32 duration;
            os_printf("Sequence defined as:\n");
            for (idx = 0; idx < get_do_seq_length(seq); idx++)
            {
                level = get_do_seq_pulse_level(seq, idx);
                duration = get_do_seq_pulse_duration(seq, idx);
                if (level == ESPBOT_LOW)
                    os_printf("pulse %d: level  'LOW' - duration %d\n", idx, duration);
                else
                    os_printf("pulse %d: level 'HIGH' - duration %d\n", idx, duration);
            }
            os_printf("Sequence end.\n");
        }
        read_di_sequence(input_seq);
        exe_do_seq_ms(seq);
    }
    break;
    case 10:
    {
        // DHT reading
        // Send start sequence
        // High ____                    ____
        // Low      |_____ 1,5 ms _____|
        PIN_FUNC_SELECT(ESPBOT_D2_MUX, ESPBOT_D2_FUNC);
        GPIO_OUTPUT_SET(ESPBOT_D2_NUM, ESPBOT_HIGH);
        seq = new_do_seq(ESPBOT_D2_NUM, 1);
        set_do_seq_cb(seq, dht_start_completed, (void *)seq, task);
        out_seq_add(seq, ESPBOT_LOW, 1500);
        // out_seq_add(seq, ESPBOT_HIGH, 20);
        // prepare input sequence
        dht_input = new_di_seq(ESPBOT_D2_NUM, 82, 1000, TIMEOUT_MS);
        set_di_seq_cb(dht_input, dht_reading_completed, (void *)dht_input, task);

        exe_do_seq_us(seq);
    }
    break;
    case 14:
    {
        // Error diagnostic log
        int idx = 0;
        fs_printf("###### start of diagnostic events\n");
        while (esp_diag.get_event(idx))
        {
            struct dia_event *event_ptr = esp_diag.get_event(idx);
            fs_printf("event %d - %s %d %X %X %d\n",
                      idx,
                      esp_sntp.get_timestr(event_ptr->timestamp),
                      event_ptr->ack,
                      event_ptr->type,
                      event_ptr->code,
                      event_ptr->value);
            idx++;
            if (idx >= esp_diag.get_max_events_count())
                break;
        }
        fs_printf("###### end of diagnostic events\n");
    }
    break;
    case 15:
    {
        // Acknowledge events
        esp_diag.ack_events();
    }
    break;
    case 16:
    {
        // get unacknowledged events
        int value = esp_diag.get_unack_events();
        fs_printf("unacknoledged events: %d\n", value);
    }
    break;
    case 17:
    {
        // Error print
        event_counter++;
        fs_printf("A new fatal event (%d) was injected.\n", event_counter);
        esp_diag.fatal(event_counter, 100 + event_counter);
    }
    break;
    case 18:
    {
        // Error print
        event_counter++;
        fs_printf("A new error event (%d) was injected.\n", event_counter);
        esp_diag.error(event_counter, 100 + event_counter);
    }
    break;
    case 19:
    {
        // Error print
        event_counter++;
        fs_printf("A new warning event (%d) was injected.\n", event_counter);
        esp_diag.warn(event_counter, 100 + event_counter);
    }
    break;
    case 20:
    {
        // Error print
        event_counter++;
        fs_printf("A new info event (%d) was injected.\n", event_counter);
        esp_diag.info(event_counter, 100 + event_counter);
    }
    break;
    case 21:
    {
        // Error print
        event_counter++;
        fs_printf("A new debug event (%d) was injected.\n", event_counter);
        esp_diag.debug(event_counter, 100 + event_counter);
    }
    break;
    case 22:
    {
        // Error print
        event_counter++;
        fs_printf("A new trace event (%d) was injected.\n", event_counter);
        esp_diag.trace(event_counter, 100 + event_counter);
    }
    break;
    case 29:
    {
        TRACE("testing operations on strings mapped in flash");
        const char *str = f_str("this string is mapped into flash and is composed by a pretty long number of characters (92)!");
        TRACE("now printing a string mapped in flash: \"%s\"", str);
        TRACE("now calculating the len of a string mapped in flash");
        int len = os_strlen(str);
        TRACE("the len was %d", len);
        char tmp_str[100];
        TRACE("now copying a string mapped in flash");
        os_strcpy(tmp_str, str);
        TRACE("string mapped in flash: %s", str);
        TRACE("string mapped in ram: %s", tmp_str);
        TRACE("now comparing a string mapped in flash");
        int res = os_strcmp(str, f_str("example 1"));
        TRACE("result: %d", res);
        res = os_strcmp(str, f_str("example 1"));
        TRACE("result: %d", res);
        TRACE("measuring how long a TRACE takes...");
        {
            Profiler("TRACE took");
            TRACE("this is a trace test %d", 8);
        }
        TRACE("done");
    }
    break;
    case 30:
    {
        char str[80];
        {
            Profiler ram_string("ram string sprintf");
            os_sprintf(str, "this string is mapped into RAM\n");
        }
        os_printf(str);
        {
            Profiler flash_string("flash string sprintf");
            fs_sprintf(str, "this string is mapped into FLASH\n");
        }
        os_printf(str);
    }
    break;
    case 31:
    {
        // char str[256];
        // os_printf("41 char string\n");
        // fs_sprintf(str, "1_______10________20________30________40");
        // os_printf("%s\n", str);
        // os_printf("51 char string\n");
        // fs_sprintf(str, "1_______10________20________30________40________50");
        // os_printf("%s\n", str);
        // os_printf("61 char string\n");
        // fs_sprintf(str, "1_______10________20________30________40________50________60");
        // os_printf("%s\n", str);
        // os_printf("71 char string\n");
        // fs_sprintf(str, "1_______10________20________30________40________50________60________70");
        // os_printf("%s\n", str);
        // os_printf("72 char string\n");
        // fs_sprintf(str, "1_______10________20________30________40________50________60________70_");
        // os_printf("%s\n", str);
        // os_printf("73 char string\n");
        // fs_sprintf(str, "1_______10________20________30________40________50________60________70_2");
        // os_printf("%s\n", str);
        // os_printf("74 char string\n");
        // fs_sprintf(str, "1_______10________20________30________40________50________60________70__3");
        // os_printf("%s\n", str);
        // os_printf("75 char string\n");
        // fs_sprintf(str, "1_______10________20________30________40________50________60________70___4");
        // os_printf("%s\n", str);
    }
    break;
    case 32:
    {
        // char str[256];
        // os_printf("41 char string\n");
        // fs_sprintf(str, "1_______10________20________30________40");
        // os_printf("%s\n", str);
        // os_printf("51 char string\n");
        // fs_sprintf(str, "1_______10________20________30________40________50");
        // os_printf("%s\n", str);
        // os_printf("61 char string\n");
        // fs_sprintf(str, "1_______10________20________30________40________50________60");
        // os_printf("%s\n", str);
        // os_printf("71 char string\n");
        // fs_sprintf(str, "1_______10________20________30________40________50________60________70");
        // os_printf("%s\n", str);
        // os_printf("72 char string\n");
        // fs_sprintf(str, "1_______10________20________30________40________50________60________70_");
        // os_printf("%s\n", str);
        // os_printf("73 char string\n");
        // fs_sprintf(str, "1_______10________20________30________40________50________60________70_2");
        // os_printf("%s\n", str);
        // os_printf("74 char string\n");
        // fs_sprintf(str, "1_______10________20________30________40________50________60________70__3");
        // os_printf("%s\n", str);
    }
    break;
    case 33:
    {
        // char str[256];
        // os_printf("41 char string\n");
        // fs_sprintf(str, "1_______10________20________30________40");
        // os_printf("%s\n", str);
        // os_printf("51 char string\n");
        // fs_sprintf(str, "1_______10________20________30________40________50");
        // os_printf("%s\n", str);
        // os_printf("61 char string\n");
        // fs_sprintf(str, "1_______10________20________30________40________50________60");
        // os_printf("%s\n", str);
        // os_printf("71 char string\n");
        // fs_sprintf(str, "1_______10________20________30________40________50________60________70");
        // os_printf("%s\n", str);
        // os_printf("72 char string\n");
        // fs_sprintf(str, "1_______10________20________30________40________50________60________70_");
        // os_printf("%s\n", str);
        // os_printf("73 char string\n");
        // fs_sprintf(str, "1_______10________20________30________40________50________60________70_2");
        // os_printf("%s\n", str);
    }
    break;
    case 34:
    {
        // char str[256];
        // os_printf("41 char string\n");
        // fs_sprintf(str, "1_______10________20________30________40");
        // os_printf("%s\n", str);
        // os_printf("51 char string\n");
        // fs_sprintf(str, "1_______10________20________30________40________50");
        // os_printf("%s\n", str);
        // os_printf("61 char string\n");
        // fs_sprintf(str, "1_______10________20________30________40________50________60");
        // os_printf("%s\n", str);
        // os_printf("71 char string\n");
        // fs_sprintf(str, "1_______10________20________30________40________50________60________70");
        // os_printf("%s\n", str);
    }
    break;
*/
        /*
    case 40:
    {
        fs_printf("testing function execution from task\n");
        subsequent_function(test_exe_task_fun);
        fs_printf("completing testing function\n");
    }
    break;
    // CRON
    case 50:
    {
        fs_printf("Disabling cron...\n");
        disable_cron();
        save_cron_cfg();
        fs_printf("cron status is %d\n", cron_enabled());
    }
    break;
    case 51:
    {
        fs_printf("Enabling cron...\n");
        enable_cron();
        save_cron_cfg();
        fs_printf("cron status is %d\n", cron_enabled());
    }
    break;
    // MDNS
    case 52:
    {
        fs_printf("Disabling mdns...\n");
        esp_mDns.disable();
        esp_mDns.save_cfg();
        fs_printf("mdns status is %d\n", esp_mDns.is_enabled());
    }
    break;
    case 53:
    {
        fs_printf("Enabling mdns...\n");
        esp_mDns.enable();
        esp_mDns.save_cfg();
        fs_printf("mdns status is %d\n", esp_mDns.is_enabled());
    }
    break;
    // SNTP
    case 54:
    {
        fs_printf("Disabling sntp...\n");
        esp_time.disable_sntp();
        esp_time.save_cfg();
        fs_printf("sntp status is %d\n", esp_time.sntp_enabled());
    }
    break;
    case 55:
    {
        fs_printf("Enabling sntp...\n");
        esp_time.enable_sntp();
        esp_time.save_cfg();
        fs_printf("sntp status is %d\n", esp_time.sntp_enabled());
    }
    break;
    case 56:
    {
        fs_printf("Setting time manually...\n");
        esp_time.set_time_manually(1582124400);
        fs_printf("time set \n");
    }
    break;
    // 1582124400
    case 58:
    case 59:
    case 60:
    case 61:
    case 62:
    case 63:
    case 64:
    case 65:
    case 66:
    case 67:
    case 68:
    case 69:
    case 70:
    case 71:
    case 72:
    case 73:
    case 74:
    case 75:
    case 76:
    case 77:
    case 78:
    case 79:
    case 80:
    case 81:
    case 82:
    {
        fs_printf("setting timezone...\n");
        signed char tz = idx - 70;
        esp_time.set_timezone(tz);
        // esp_time.save_cfg();
        fs_printf("timezone is %d\n", esp_time.get_timezone());
    }
    break;
    case 90:
    {
        fs_printf("OTA testing...\n");
        // esp_ota.new_start_upgrade();
    }
    break;
    case 91:
    {
        fs_printf("OTA success...\n");
        esp_ota.set_status(OTA_SUCCESS);
        // extern void ota_engine(void);
        // subsequent_function(ota_engine);
    }
    break;
    case 92:
    {
        fs_printf("OTA failed...\n");
        esp_ota.set_status(OTA_FAILED);
        // extern void ota_engine(void);
        // subsequent_function(ota_engine);
    }
    break;
    case 93:
    {
        fs_printf("printing cron jobs...\n");
        cron_print_jobs();
    }
    break;
    case 94:
    {
        static int job_id = 0;
        fs_printf("adding new job...\n");
        int result = cron_add_job(job_id, 0, 0, 0, 0, NULL, NULL);
        if (result > 0)
            fs_printf("added job %d", result);
        else
            fs_printf("error adding job");
        cron_print_jobs();
    }
    break;
    case 101:
    case 102:
    case 103:
    case 104:
    case 105:
    case 106:
    case 107:
    case 108:
    case 109:
    case 110:
    {
        int job_id = idx - 100;
        fs_printf("removing job %d ...\n", job_id);
        cron_del_job(job_id);
        cron_print_jobs();
    }
    break;
    case 200:
        // espmem.print_heap_objects();
        break;
*/
        /*
    case 101:
    {
        // check json numbers
        char *sample_str = "{\"first\":1,\"second\":-1,\"third\":0.5,\"fourth\":-0.5}";
        char tmp_str[10];
        char tmp_value[10];
        Json_str json_obj(sample_str, os_strlen(sample_str));
        os_printf("JSON syntax: found and error at position %d\n", json_obj.syntax_check());
        json_obj.find_pair("first");
        os_memset(tmp_str, 0, 10);
        os_strncpy(tmp_str, json_obj.get_cur_pair_string(), json_obj.get_cur_pair_string_len());
        os_memset(tmp_value, 0, 10);
        os_strncpy(tmp_value, json_obj.get_cur_pair_value(), json_obj.get_cur_pair_value_len());
        fs_printf("pair name: %s, pair type: %d, pair value: %s, pair value len: %d\n",
                  tmp_str,
                  json_obj.get_cur_pair_value_type(),
                  tmp_value,
                  json_obj.get_cur_pair_value_len());
        fs_printf("atoi(%s) = %d\n", tmp_value, atoi(tmp_value));
        json_obj.find_pair("second");
        os_memset(tmp_str, 0, 10);
        os_strncpy(tmp_str, json_obj.get_cur_pair_string(), json_obj.get_cur_pair_string_len());
        os_memset(tmp_value, 0, 10);
        os_strncpy(tmp_value, json_obj.get_cur_pair_value(), json_obj.get_cur_pair_value_len());
        fs_printf("pair name: %s, pair type: %d, pair value: %s, pair value len: %d\n",
                  tmp_str,
                  json_obj.get_cur_pair_value_type(),
                  tmp_value,
                  json_obj.get_cur_pair_value_len());
        fs_printf("atoi(%s) = %d\n", tmp_value, atoi(tmp_value));
        json_obj.find_pair("third");
        os_memset(tmp_str, 0, 10);
        os_strncpy(tmp_str, json_obj.get_cur_pair_string(), json_obj.get_cur_pair_string_len());
        os_memset(tmp_value, 0, 10);
        os_strncpy(tmp_value, json_obj.get_cur_pair_value(), json_obj.get_cur_pair_value_len());
        fs_printf("pair name: %s, pair type: %d, pair value: %s, pair value len: %d\n",
                  tmp_str,
                  json_obj.get_cur_pair_value_type(),
                  tmp_value,
                  json_obj.get_cur_pair_value_len());
        fs_printf("atoi(%s) = %d\n", tmp_value, atoi(tmp_value));
        json_obj.find_pair("fourth");
        os_memset(tmp_str, 0, 10);
        os_strncpy(tmp_str, json_obj.get_cur_pair_string(), json_obj.get_cur_pair_string_len());
        os_memset(tmp_value, 0, 10);
        os_strncpy(tmp_value, json_obj.get_cur_pair_value(), json_obj.get_cur_pair_value_len());
        fs_printf("pair name: %s, pair type: %d, pair value: %s, pair value len: %d\n",
                  tmp_str,
                  json_obj.get_cur_pair_value_type(),
                  tmp_value,
                  json_obj.get_cur_pair_value_len());
        fs_printf("atoi(%s) = %d\n", tmp_value, atoi(tmp_value));
    }
    break;
    /*
    case 256:
    {
        // set web server response buffer size
        set_http_msg_max_size(256);
        INFO("response buffer size set to 256 bytes");
    }
    break;
    case 512:
    {
        // set web server response buffer size
        set_http_msg_max_size(512);
        INFO("response buffer size set to 512 bytes");
    }
    break;
    case 768:
    {
        // set web server response buffer size
        set_http_msg_max_size(768);
        INFO("response buffer size set to 768 bytes");
    }
    break;
    case 1024:
    {
        // set web server response buffer size
        set_http_msg_max_size(1024);
        INFO("response buffer size set to 1024 bytes");
    }
    break;
    case 1280:
    {
        // set web server response buffer size
        set_http_msg_max_size(1280);
        INFO("response buffer size set to 1280 bytes");
    }
    break;
    case 1536:
    {
        // set web server response buffer size
        set_http_msg_max_size(1536);
        INFO("response buffer size set to 1536 bytes");
    }
    break;
    case 1792:
    {
        // set web server response buffer size
        set_http_msg_max_size(1792);
        INFO("response buffer size set to 1792 bytes");
    }
    break;
    */
    default:
        break;
    }
}
#endif