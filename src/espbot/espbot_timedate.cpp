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
#include "espbot_config.hpp"
#include "espbot_diagnostic.hpp"
#include "espbot_event_codes.h"
#include "espbot_global.hpp"
#include "espbot_timedate.hpp"
#include "espbot_utils.hpp"

void TimeDate::init(void)
{
    _sntp_enabled = false;
    _timezone = 0; // UTC

    if (restore_cfg())
    {
        esp_diag.warn(TIMEDATE_INIT_DEFAULT_CFG);
        WARN("TimeDate::init no cfg available");
    }
}

void TimeDate::start_sntp(void)
{
    if (_sntp_enabled)
    {
        sntp_setservername(0, (char *)f_str("0.pool.ntp.org"));
        sntp_setservername(1, (char *)f_str("1.pool.ntp.org"));
        sntp_setservername(2, (char *)f_str("2.pool.ntp.org"));
        set_timezone(_timezone);
        sntp_init();
        esp_diag.info(SNTP_START);
        INFO("Sntp started");
    }
}

void TimeDate::stop_sntp(void)
{
    sntp_stop();
    esp_diag.info(SNTP_STOP);
    INFO("Sntp ended");
}

struct espbot_time
{
    uint32 sntp_time;
    uint32 rtc_time;
};

uint32 TimeDate::get_timestamp()
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
        struct espbot_time old_time, time_pair;
        system_rtc_mem_read(64, &old_time, sizeof(struct espbot_time));
        uint32 rtc_diff_us = ((system_get_rtc_time() - old_time.rtc_time) * system_rtc_clock_cali_proc()) >> 12;
        timestamp = old_time.sntp_time + rtc_diff_us / 1000000;
        // refresh saved time pair
        time_pair.rtc_time = system_get_rtc_time();
        time_pair.sntp_time = timestamp;
        system_rtc_mem_write(64, &time_pair, sizeof(struct espbot_time));
    }
    return timestamp;
}

char *TimeDate::get_timestr(uint32 t_time)
{
    char *time_str = sntp_get_real_time(t_time);
    char *tmp_ptr = os_strstr(time_str, "\n");
    if (tmp_ptr)
        *tmp_ptr = '\0';
    return time_str;
}

void TimeDate::set_time_manually(uint32 t_time)
{
    struct espbot_time time_pair;
    time_pair.rtc_time = system_get_rtc_time();
    time_pair.sntp_time = t_time;
    system_rtc_mem_write(64, &time_pair, sizeof(struct espbot_time));
}

void TimeDate::set_timezone(char tz)
{
    _timezone = tz;
    if (sntp_set_timezone(_timezone) == false)
    {
        esp_diag.error(SNTP_CANNOT_SET_TIMEZONE);
        ERROR("TimeDate::set_tz cannot set timezone");
    }
}

char TimeDate::get_timezone(void)
{
    return _timezone;
}

void TimeDate::enable_sntp(void)
{
    _sntp_enabled = true;
}

void TimeDate::disable_sntp(void)
{
    _sntp_enabled = false;
}

bool TimeDate::sntp_enabled(void)
{
    return _sntp_enabled;
}

#define TIMEDATE_FILENAME f_str("timedate.cfg")

int TimeDate::restore_cfg(void)
{
    ALL("TimeDate::restore_cfg");
    File_to_json cfgfile(TIMEDATE_FILENAME);
    espmem.stack_mon();
    if (!cfgfile.exists())
    {
        WARN("TimeDate::restore_cfg file not found");
        return CFG_ERROR;
    }
    if (cfgfile.find_string(f_str("sntp_enabled")))
    {
        esp_diag.error(TIMEDATE_RESTORE_CFG_INCOMPLETE);
        ERROR("TimeDate::restore_cfg incomplete cfg");
        return CFG_ERROR;
    }
    _sntp_enabled = atoi(cfgfile.get_value());
    if (cfgfile.find_string(f_str("timezone")))
    {
        esp_diag.error(TIMEDATE_RESTORE_CFG_INCOMPLETE);
        ERROR("TimeDate::restore_cfg incomplete cfg");
        return CFG_ERROR;
    }
    _timezone = atoi(cfgfile.get_value());
    return CFG_OK;
}

int TimeDate::saved_cfg_not_updated(void)
{
    ALL("TimeDate::saved_cfg_not_updated");
    File_to_json cfgfile(TIMEDATE_FILENAME);
    espmem.stack_mon();
    if (!cfgfile.exists())
    {
        return CFG_REQUIRES_UPDATE;
    }
    if (cfgfile.find_string(f_str("sntp_enabled")))
    {
        esp_diag.error(TIMEDATE_SAVED_CFG_NOT_UPDATED_INCOMPLETE);
        ERROR("TimeDate::saved_cfg_not_updated incomplete cfg");
        return CFG_ERROR;
    }
    if (_sntp_enabled != atoi(cfgfile.get_value()))
    {
        return CFG_REQUIRES_UPDATE;
    }
    if (cfgfile.find_string(f_str("timezone")))
    {
        esp_diag.error(TIMEDATE_SAVED_CFG_NOT_UPDATED_INCOMPLETE);
        ERROR("TimeDate::saved_cfg_not_updated incomplete cfg");
        return CFG_ERROR;
    }
    if (_timezone != atoi(cfgfile.get_value()))
    {
        return CFG_REQUIRES_UPDATE;
    }
    return CFG_OK;
}

int TimeDate::save_cfg(void)
{
    ALL("TimeDate::save_cfg");
    if (saved_cfg_not_updated() != CFG_REQUIRES_UPDATE)
        return CFG_OK;
    if (!espfs.is_available())
    {
        esp_diag.error(TIMEDATE_SAVE_CFG_FS_NOT_AVAILABLE);
        ERROR("TimeDate::save_cfg FS not available");
        return CFG_ERROR;
    }
    Ffile cfgfile(&espfs, (char *)TIMEDATE_FILENAME);
    if (!cfgfile.is_available())
    {
        esp_diag.error(TIMEDATE_SAVE_CFG_CANNOT_OPEN_FILE);
        ERROR("TimeDate::save_cfg cannot open file");
        return CFG_ERROR;
    }
    cfgfile.clear();
    // "{"sntp_enabled": 0, "timezone": -12}" // 37 chars
    char buffer[37];
    espmem.stack_mon();
    fs_sprintf(buffer,
               "{\"sntp_enabled\": %d, \"timezone\": %d}",
               _sntp_enabled,
               _timezone);
    cfgfile.n_append(buffer, os_strlen(buffer));
    return CFG_OK;
}