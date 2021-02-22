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
#include "user_interface.h"
}

#include "espbot.hpp"
#include "espbot_cfgfile.hpp"
#include "espbot_diagnostic.hpp"
#include "espbot_event_codes.h"
#include "espbot_global.hpp"
#include "espbot_timedate.h"
#include "espbot_timedate.hpp"
#include "espbot_utils.hpp"
#include "espbot_rtc_mem_map.h"

static struct
{
    bool sntp_enabled;
    signed char timezone;
} timedate_cfg;

static struct
{
    bool sntp_running;
} timedate_state;

void timedate_start_sntp(void)
{
    if (timedate_cfg.sntp_enabled && !timedate_state.sntp_running)
    {
        sntp_setservername(0, (char *)f_str("0.pool.ntp.org"));
        sntp_setservername(1, (char *)f_str("1.pool.ntp.org"));
        sntp_setservername(2, (char *)f_str("2.pool.ntp.org"));
        if (sntp_set_timezone(0) == false)
        {
            dia_error_evnt(SNTP_CANNOT_SET_TIMEZONE);
            ERROR("timedate_start_sntp cannot set timezone");
        }
        sntp_init();
        timedate_state.sntp_running = true;
        dia_info_evnt(SNTP_START);
        INFO("Sntp started");
    }
}

void timedate_stop_sntp(void)
{
    if (timedate_state.sntp_running)
    {
        sntp_stop();
        timedate_state.sntp_running = false;
        dia_info_evnt(SNTP_STOP);
        INFO("Sntp ended");
    }
}

uint32 timedate_get_timestamp()
{
    uint32 timestamp = sntp_get_current_timestamp();
    if ((timestamp > 0))
    {
        // save timestamp and corresponding RTC time into RTC memory
        struct espbot_time time_pair;
        time_pair.rtc_time = system_get_rtc_time();
        time_pair.sntp_time = timestamp;
        system_rtc_mem_write(RTC_TIMEDATE, &time_pair, (sizeof(struct espbot_time) - 4));
    }
    else
    {
        // get last timestamp and corresponding RTC time from RTC memory
        // and calculate current timestamp
        struct espbot_time old_time, cur_time;
        system_rtc_mem_read(RTC_TIMEDATE, &old_time, (sizeof(struct espbot_time) - 4));
        cur_time.rtc_time = system_get_rtc_time();
        uint32 rtc_cal = system_rtc_clock_cali_proc();
        uint32 rtc_diff_us = (uint32)(((uint64)(cur_time.rtc_time - old_time.rtc_time)) *
                                      ((uint64)((rtc_cal * 1000) >> 12)) / 1000);
        // if (rtc_diff_us > 0.5 s) add 1 second to the timestamp
        if ((rtc_diff_us % 1000000) >= 500000)
            timestamp = old_time.sntp_time + (rtc_diff_us / 1000000) + 1;
        else
            timestamp = old_time.sntp_time + (rtc_diff_us / 1000000);

        // refresh saved time pair
        cur_time.sntp_time = timestamp;
        system_rtc_mem_write(RTC_TIMEDATE, &cur_time, (sizeof(struct espbot_time) - 4));
    }
    return timestamp;
}

char *timedate_get_timestr(uint32 t_time)
{
    char *time_str = sntp_get_real_time(t_time + timedate_cfg.timezone * 3600);
    char *tmp_ptr = os_strstr(time_str, f_str("\n"));
    if (tmp_ptr)
        *tmp_ptr = '\0';
    return time_str;
}

void timedate_set_time_manually(uint32 t_time)
{
    struct espbot_time time_pair;
    time_pair.rtc_time = system_get_rtc_time();
    time_pair.sntp_time = t_time;
    system_rtc_mem_write(RTC_TIMEDATE, &time_pair, (sizeof(struct espbot_time) - 4));
    dia_info_evnt(TIMEDATE_CHANGED, t_time);
    INFO("timedate changed to %d", t_time);
}

void timedate_set_timezone(signed char tz)
{
    if (tz != timedate_cfg.timezone)
    {
        timedate_cfg.timezone = tz;
        dia_info_evnt(TIMEZONE_CHANGED, timedate_cfg.timezone);
        INFO("timezone changed to %d", timedate_cfg.timezone);
    }
}

signed char timedate_get_timezone(void)
{
    return timedate_cfg.timezone;
}

void timedate_enable_sntp(void)
{
    if (!timedate_cfg.sntp_enabled)
    {
        timedate_cfg.sntp_enabled = true;
        timedate_start_sntp();
    }
}

void timedate_disable_sntp(void)
{
    timedate_cfg.sntp_enabled = false;
    timedate_stop_sntp();
}

bool timedate_sntp_enabled(void)
{
    return timedate_cfg.sntp_enabled;
}

#define TIMEDATE_FILENAME ((char *)f_str("timedate.cfg"))

static int timedate_restore_cfg(void)
{
    ALL("timedate_restore_cfg");
    if (!Espfile::exists(TIMEDATE_FILENAME))
        return CFG_cantRestore;
    Cfgfile cfgfile(TIMEDATE_FILENAME);
    espmem.stack_mon();
    int sntp_enabled = cfgfile.getInt(f_str("sntp_enabled"));
    int timezone = cfgfile.getInt(f_str("timezone"));
    if (cfgfile.getErr() != JSON_noerr)
    {
        dia_error_evnt(TIMEDATE_RESTORE_CFG_ERROR);
        ERROR("timedate_restore_cfg error");
        return CFG_error;
    }
    timedate_cfg.sntp_enabled = (bool)sntp_enabled;
    timedate_cfg.timezone = (signed char)timezone;
    return CFG_ok;
}

static int timedate_saved_cfg_updated(void)
{
    ALL("timedate_saved_cfg_updated");
    if (!Espfile::exists(TIMEDATE_FILENAME))
    {
        return CFG_notUpdated;
    }
    Cfgfile cfgfile(TIMEDATE_FILENAME);
    espmem.stack_mon();
    int sntp_enabled = cfgfile.getInt(f_str("sntp_enabled"));
    int timezone = cfgfile.getInt(f_str("timezone"));
    if (cfgfile.getErr() != JSON_noerr)
    {
        // no need to raise an error, the cfg file will be overwritten
        // dia_error_evnt(TIMEDATE_SAVED_CFG_UPDATED_ERROR);
        // ERROR("timedate_saved_cfg_updated error");
        return CFG_error;
    }
    if ((timedate_cfg.sntp_enabled != (bool)sntp_enabled) ||
        (timedate_cfg.timezone != (signed char)timezone))
    {
        return CFG_notUpdated;
    }
    return CFG_ok;
}

char *timedate_cfg_json_stringify(char *dest, int len)
{
    // {"sntp_enabled":1, "timezone":-12}
    int msg_len = 34 + 1;
    char *msg;
    if (dest == NULL)
    {
        msg = new char[msg_len];
        if (msg == NULL)
        {
            dia_error_evnt(TIMEDATE_CFG_STRINGIFY_HEAP_EXHAUSTED, msg_len);
            ERROR("timedate_cfg_json_stringify heap exhausted [%d]", msg_len);
            return NULL;
        }
    }
    else
    {
        msg = dest;
        if (len < msg_len)
        {
            *msg = 0;
            return msg;
        }
    }
    fs_sprintf(msg,
               "{\"sntp_enabled\":%d,\"timezone\":%d}",
               timedate_cfg.sntp_enabled,
               timedate_cfg.timezone);
    return msg;
}

char *timedate_state_json_stringify(char *dest, int len)
{
    // {"timestamp":1589272200,"timedate":"Tue May 12 09:30:00 2020","sntp_enabled":0,"timezone":-12}
    int msg_len = 94 + 1;
    char *msg;
    if (dest == NULL)
    {
        msg = new char[msg_len];
        if (msg == NULL)
        {
            dia_error_evnt(TIMEDATE_STATE_STRINGIFY_HEAP_EXHAUSTED, msg_len);
            ERROR("timedate_state_json_stringify heap exhausted [%d]", msg_len);
            return NULL;
        }
    }
    else
    {
        msg = dest;
        if (len < msg_len)
        {
            *msg = 0;
            return msg;
        }
    }
    uint32 current_timestamp = timedate_get_timestamp();
    fs_sprintf(msg,
               "{\"timestamp\":%d,\"date\":\"%s\",",
               current_timestamp,
               timedate_get_timestr(current_timestamp));
    fs_sprintf(msg + os_strlen(msg),
               "\"sntp_enabled\":%d,\"timezone\":%d}",
               timedate_cfg.sntp_enabled,
               timedate_cfg.timezone);
    return msg;
}

int timedate_cfg_save(void)
{
    ALL("timedate_cfg_save");
    if (timedate_saved_cfg_updated() == CFG_ok)
        return CFG_ok;
    Cfgfile cfgfile(TIMEDATE_FILENAME);
    espmem.stack_mon();
    if (cfgfile.clear() != SPIFFS_OK)
        return CFG_error;
    char str[35];
    timedate_cfg_json_stringify(str, 35);
    int res = cfgfile.n_append(str, os_strlen(str));
    if (res < SPIFFS_OK)
        return CFG_error;
    return CFG_ok;
}

void timedate_init_essential(void)
{
    timedate_cfg.sntp_enabled = false;
    timedate_cfg.timezone = 0; // UTC
    timedate_state.sntp_running = false;

    struct espbot_time rtc_time;
    system_rtc_mem_read(RTC_TIMEDATE, &rtc_time, sizeof(struct espbot_time));
    if (rtc_time.magic != ESP_TIMEDATE_MAGIC)
    {
        rtc_time.sntp_time = 0;
        rtc_time.rtc_time = 0;
        rtc_time.magic = ESP_TIMEDATE_MAGIC;
        system_rtc_mem_write(RTC_TIMEDATE, &rtc_time, sizeof(struct espbot_time));
    }
}

void timedate_init(void)
{
    // the default configuration was setup by init_essential

    if (timedate_restore_cfg() != CFG_ok)
    {
        dia_warn_evnt(TIMEDATE_INIT_DEFAULT_CFG);
        WARN("timedate_init no cfg available");
    }
}