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
#include "ip_addr.h"
#include "sntp.h"
}

#include "espbot.hpp"
#include "espbot_diagnostic.hpp"
#include "espbot_event_codes.h"
#include "espbot_global.hpp"
#include "espbot_logger.hpp"
#include "espbot_sntp.hpp"

void Sntp::start(void)
{
    sntp_setservername(0, "0.pool.ntp.org");
    sntp_setservername(1, "1.pool.ntp.org");
    sntp_setservername(2, "2.pool.ntp.org");
    if (sntp_set_timezone(1) == false)
    {
        esp_diag.error(SNTP_CANNOT_SET_TIMEZONE);
        // esplog.error("Sntp::start - cannot set timezone\n");
    }
    sntp_init();
    esp_diag.info(SNTP_START);
    // esplog.debug("Sntp started\n");
}

void Sntp::stop(void)
{
    sntp_stop();
    esp_diag.info(SNTP_STOP);
    // esplog.debug("Sntp ended\n");
}

struct espbot_time
{
    uint32 sntp_time;
    uint32 rtc_time;
};

uint32 Sntp::get_timestamp()
{
    uint32 timestamp = sntp_get_current_timestamp();
    if (timestamp > 0)
    {
        // save timestamp and corresponding RTC time into RTC memory
        struct espbot_time time_pair;
        time_pair.rtc_time = system_get_rtc_time();
        time_pair.sntp_time = timestamp;
        system_rtc_mem_write(64, &time_pair, sizeof(struct espbot_time));
    }
    else
    {
        // get last timestamp and corresponding RTC time from RTC memory
        // and calculate current timestamp
        struct espbot_time old_time;
        system_rtc_mem_read(64, &old_time, sizeof(struct espbot_time));
        uint32 rtc_diff_us = ((system_get_rtc_time() - old_time.rtc_time) * system_rtc_clock_cali_proc()) >> 12;
        timestamp = old_time.sntp_time + rtc_diff_us / 1000000;
    }
    return timestamp;
}

char *Sntp::get_timestr(uint32 t_time)
{
    char *time_str = sntp_get_real_time(t_time);
    char *tmp_ptr = os_strstr(time_str, "\n");
    if (tmp_ptr)
        *tmp_ptr = '\0';
    return time_str;
}
