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
#include "library_di_sequence.h"
#include "library_do_sequence.h"
#include "esp8266_io.h"
#include "gpio.h"
}

#include "iram.h"
#include "app.hpp"
#include "espbot_http.hpp"
#include "espbot_webclient.hpp"
#include "app_test.hpp"
#include "espbot_global.hpp"
#include "espbot_debug.hpp"

// function for testing purpose

static int test_counter;

static os_timer_t test_timer;

static void test_function(void)
{
    esplog.all("test_function\n");
    // run_test();
}

static struct ip_addr host_ip;
static int host_port;
static char *client_request;
static Webclnt *espclient;

void init_test(struct ip_addr ip, uint32 port, char *request)
{
    esplog.all("init_test\n");
    os_memcpy(&host_ip, &ip, sizeof(struct ip_addr));
    host_port = port;
    request = "GET /user1.bin HTTP/1.1\r\nRange: bytes=0-1023";
    // GET /user1.bin HTTP/1.1
    // Host: 192.168.1.201
    // Range: bytes=0-1023
    // User-Agent: PostmanRuntime/7.15.2
    // Accept: */*
    // Cache-Control: no-cache
    // Postman-Token: c677f309-b16d-4f1b-a5cd-0e2b470a9a66,b3728ec9-2966-4b08-b49a-69e88790b76b
    // Host: 192.168.1.201
    // Accept-Encoding: gzip, deflate
    // Connection: keep-alive
    // cache-control: no-cache
    client_request = new char[os_strlen(request) + 1];
    if (client_request == NULL)
    {
        os_printf("os_zalloc error\n");
        client_request = NULL;
    }
    else
    {
        os_strcpy(client_request, request);
        os_printf("client_request length: %d, effective length: %d , request: %s\n", (os_strlen(request) + 1), os_strlen(client_request), client_request);
    }
    espclient = new Webclnt;
}

void free_client(void *)
{
    delete espclient;
}

void check_version(void *param)
{
    esplog.all("check_version\n");
    switch (espclient->get_status())
    {
    case WEBCLNT_RESPONSE_READY:
        if (espclient->parsed_response->body)
        {
            os_printf("Server responded: %s\n", espclient->parsed_response->body);
        }
        break;
    default:
        os_printf("wc_get_version: Ops ... webclient status is %d\n", espclient->get_status());
        break;
    }
    os_printf("Webclient test completed\n");
    espclient->disconnect(free_client, NULL);
    // delete espclient;
}

void get_version(void *param)
{
    esplog.all("get_version\n");
    switch (espclient->get_status())
    {
    case WEBCLNT_CONNECTED:
        os_printf("Sending request [%s]\n", client_request);
        espclient->send_req(client_request, check_version, NULL);
        delete[] client_request;
        break;
    default:
        os_printf("get_version: Ops ... webclient status is %d\n", espclient->get_status());
        os_printf("Webclient test completed\n");
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
    os_printf("Starting connection to server ...\n");
    espclient->connect(host_ip, host_port, get_version, NULL);
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
void run_test(int idx)
{
    struct do_seq *seq;
    switch (idx)
    {
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
    */
    case 13:
    {
        // Error print
        static int counter = 0;
        counter++;
        esplog.error("A new error (%d) was injected.\n", counter);
    }
    break;
    /*
    case 256:
    {
        // set web server response buffer size
        set_http_msg_max_size(256);
        esplog.info("response buffer size set to 256 bytes\n");
    }
    break;
    case 512:
    {
        // set web server response buffer size
        set_http_msg_max_size(512);
        esplog.info("response buffer size set to 512 bytes\n");
    }
    break;
    case 768:
    {
        // set web server response buffer size
        set_http_msg_max_size(768);
        esplog.info("response buffer size set to 768 bytes\n");
    }
    break;
    case 1024:
    {
        // set web server response buffer size
        set_http_msg_max_size(1024);
        esplog.info("response buffer size set to 1024 bytes\n");
    }
    break;
    case 1280:
    {
        // set web server response buffer size
        set_http_msg_max_size(1280);
        esplog.info("response buffer size set to 1280 bytes\n");
    }
    break;
    case 1536:
    {
        // set web server response buffer size
        set_http_msg_max_size(1536);
        esplog.info("response buffer size set to 1536 bytes\n");
    }
    break;
    case 1792:
    {
        // set web server response buffer size
        set_http_msg_max_size(1792);
        esplog.info("response buffer size set to 1792 bytes\n");
    }
    break;
    */
    default:
        break;
    }
}