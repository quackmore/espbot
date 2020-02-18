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
}

#include "espbot_cron.hpp"
#include "espbot_diagnostic.hpp"
#include "espbot_event_codes.h"
#include "espbot_global.hpp"
#include "espbot_utils.hpp"

static os_timer_t heater_period_timer;

struct job
{
    char minutes;
    char hours;
    char day_of_month;
    char month;
    char day_of_week;
    void (*command)(void);
};

static os_timer_t cron_timer;

static List<struct job> *job_list;

static int get_day_of_week(char *str)
{
    // 0 - Sun      Sunday
    // 1 - Mon      Monday
    // 2 - Tue      Tuesday
    // 3 - Wed      Wednesday
    // 4 - Thu      Thursday
    // 5 - Fri      Friday
    // 6 - Sat      Saturday
    // 7 - Sun      Sunday
    if (0 == os_strncmp(str, "Sun", 3))
        return 0;
    if (0 == os_strncmp(str, "Mon", 3))
        return 1;
    if (0 == os_strncmp(str, "Tue", 3))
        return 2;
    if (0 == os_strncmp(str, "Wed", 3))
        return 3;
    if (0 == os_strncmp(str, "Thu", 3))
        return 4;
    if (0 == os_strncmp(str, "Fri", 3))
        return 5;
    if (0 == os_strncmp(str, "Sat", 3))
        return 6;
    return -1;
}

static int get_month(char *str)
{
    if (0 == os_strncmp(str, "Jan", 3))
        return 1;
    if (0 == os_strncmp(str, "Feb", 3))
        return 2;
    if (0 == os_strncmp(str, "Mar", 3))
        return 3;
    if (0 == os_strncmp(str, "Apr", 3))
        return 4;
    if (0 == os_strncmp(str, "May", 3))
        return 5;
    if (0 == os_strncmp(str, "Jun", 3))
        return 6;
    if (0 == os_strncmp(str, "Jul", 3))
        return 7;
    if (0 == os_strncmp(str, "Aug", 3))
        return 8;
    if (0 == os_strncmp(str, "Sep", 3))
        return 9;
    if (0 == os_strncmp(str, "Oct", 3))
        return 10;
    if (0 == os_strncmp(str, "Nov", 3))
        return 11;
    if (0 == os_strncmp(str, "Dec", 3))
        return 12;
    return -1;
}

static void state_current_time(struct date *time)
{
    uint32 timestamp = esp_time.get_timestamp();
    time->timestamp = timestamp;
    char *timestamp_str = esp_time.get_timestr(timestamp);
    TRACE("state_current_time date: %s (UTC+%d) [%d]", timestamp_str, esp_time.get_timezone(), timestamp);
    char tmp_str[5];
    // get day of week
    char *init_ptr = timestamp_str;
    char *end_ptr;
    os_memset(tmp_str, 0, 5);
    os_strncpy(tmp_str, init_ptr, 3);
    time->day_of_week = get_day_of_week(tmp_str);
    // get month
    init_ptr += 4;
    os_memset(tmp_str, 0, 5);
    os_strncpy(tmp_str, init_ptr, 3);
    time->month = get_month(tmp_str);
    // get day of month
    init_ptr += 4;
    os_memset(tmp_str, 0, 5);
    end_ptr = os_strstr(init_ptr, " ");
    os_strncpy(tmp_str, init_ptr, (end_ptr - init_ptr));
    time->day_of_month = atoi(tmp_str);
    // get hours
    init_ptr = end_ptr + 1;
    os_memset(tmp_str, 0, 5);
    end_ptr = os_strstr(init_ptr, ":");
    os_strncpy(tmp_str, init_ptr, (end_ptr - init_ptr));
    time->hours = atoi(tmp_str);
    // get minutes
    init_ptr = end_ptr + 1;
    os_memset(tmp_str, 0, 5);
    end_ptr = os_strstr(init_ptr, ":");
    os_strncpy(tmp_str, init_ptr, (end_ptr - init_ptr));
    time->minutes = atoi(tmp_str);
    // get seconds
    init_ptr = end_ptr + 1;
    os_memset(tmp_str, 0, 5);
    end_ptr = os_strstr(init_ptr, " ");
    os_strncpy(tmp_str, init_ptr, (end_ptr - init_ptr));
    time->seconds = atoi(tmp_str);
    // get year
    init_ptr = end_ptr + 1;
    os_memset(tmp_str, 0, 5);
    os_strncpy(tmp_str, init_ptr, 4);
    time->year = atoi(tmp_str);
    // esplog.trace("Current Time --->  year: %d\n", time->year);
    // esplog.trace("                  month: %d\n", time->month);
    // esplog.trace("           day of month: %d\n", time->day_of_month);
    // esplog.trace("                  hours: %d\n", time->hours);
    // esplog.trace("                minutes: %d\n", time->minutes);
    // esplog.trace("                seconds: %d\n", time->seconds);
    // esplog.trace("            day of week: %d\n", time->day_of_week);
}

static struct date current_time;

struct date *get_current_time(void)
{
    return &current_time;
}

void init_current_time(void)
{
    state_current_time(&current_time);
}

static void cron_execute(void)
{
    ALL("cron_execute");
    cron_sync();
    state_current_time(&current_time);
    struct job *current_job = job_list->front();
    while (current_job)
    {
        // os_printf("-----------> current job\n");
        // os_printf("     minutes: %d\n", current_job->minutes);
        // os_printf("       hours: %d\n", current_job->hours);
        // os_printf("day of month: %d\n", current_job->day_of_month);
        // os_printf("       month: %d\n", current_job->month);
        // os_printf(" day of week: %d\n", current_job->day_of_week);
        // os_printf("     command: %X\n", current_job->command);
        if ((current_job->minutes != CRON_STAR) && (current_job->minutes != current_time.minutes))
        {
            current_job = job_list->next();
            continue;
        }
        if ((current_job->hours != CRON_STAR) && (current_job->hours != current_time.hours))
        {
            current_job = job_list->next();
            continue;
        }
        if ((current_job->day_of_month != CRON_STAR) && (current_job->day_of_month != current_time.day_of_month))
        {
            current_job = job_list->next();
            continue;
        }
        if ((current_job->month != CRON_STAR) && (current_job->month != current_time.month))
        {
            current_job = job_list->next();
            continue;
        }
        if ((current_job->day_of_week != CRON_STAR) && (current_job->day_of_week != current_time.day_of_week))
        {
            current_job = job_list->next();
            continue;
        }
        if (current_job->command)
            current_job->command();
        current_job = job_list->next();
    }
}

void cron_init(void)
{
    os_timer_disarm(&cron_timer);
    os_timer_setfn(&cron_timer, (os_timer_func_t *)cron_execute, NULL);
    job_list = new List<struct job>(CRON_MAX_JOBS);
    os_memset(&current_time, 0, sizeof(struct date));
}

/*
 * will sync the cron_timer to a one minute period according to timestamp
 * provided by SNTP
 */
void cron_sync(void)
{
    ALL("cron_sync");
    uint32 cron_period;
    uint32 timestamp = esp_time.get_timestamp();
    timestamp = timestamp % 60;

    if (timestamp < 30)
        // you are late -> next minute period in (60 - timestamp) seconds
        cron_period = 60000 - timestamp * 1000;
    else
        // you are early -> next minute period in (60 + (60 - timestamp)) seconds
        cron_period = 120000 - timestamp * 1000;
    os_timer_arm(&cron_timer, cron_period, 0);
}

int cron_add_job(char minutes,
                 char hours,
                 char day_of_month,
                 char month,
                 char day_of_week,
                 void (*command)(void))
{
    ALL("cron_add_job");
    // * # job definition:
    // * # .---------------- minute (0 - 59)
    // * # |  .------------- hour (0 - 23)
    // * # |  |  .---------- day of month (1 - 31)
    // * # |  |  |  .------- month (1 - 12) OR jan,feb,mar,apr ...
    // * # |  |  |  |  .---- day of week (0 - 6) (Sunday=0 or 7) OR sun,mon,tue,wed,thu,fri,sat
    // * # |  |  |  |  |
    // * # *  *  *  *  * funcntion
    struct job *new_job = new struct job;
    new_job->minutes = minutes;
    new_job->hours = hours;
    new_job->day_of_month = day_of_month;
    new_job->month = month;
    new_job->day_of_week = day_of_week;
    new_job->command = command;
    int result = job_list->push_back(new_job);
    return result;
}