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

#include "espbot_cfgfile.hpp"
#include "espbot_cron.hpp"
#include "espbot_diagnostic.hpp"
#include "espbot_event_codes.h"
#include "espbot_global.hpp"
#include "espbot_list.hpp"
#include "espbot_timedate.hpp"
#include "espbot_utils.hpp"

struct job
{
    signed char id;
    char minutes;
    char hours;
    char day_of_month;
    char month;
    char day_of_week;
    void (*command)(void *);
    void *param;
};

static os_timer_t cron_timer;

static List<struct job> *job_list;


static struct
{
    bool enabled;
} cron_cfg;

static struct
{
    bool running;
} cron_state;

static int get_day_of_week(char *str)
{
    // 1 - Mon      Monday
    // 2 - Tue      Tuesday
    // 3 - Wed      Wednesday
    // 4 - Thu      Thursday
    // 5 - Fri      Friday
    // 6 - Sat      Saturday
    // 7 - Sun      Sunday
    if (0 == os_strncmp(str, f_str("Mon"), 3))
        return 1;
    if (0 == os_strncmp(str, f_str("Tue"), 3))
        return 2;
    if (0 == os_strncmp(str, f_str("Wed"), 3))
        return 3;
    if (0 == os_strncmp(str, f_str("Thu"), 3))
        return 4;
    if (0 == os_strncmp(str, f_str("Fri"), 3))
        return 5;
    if (0 == os_strncmp(str, f_str("Sat"), 3))
        return 6;
    if (0 == os_strncmp(str, f_str("Sun"), 3))
        return 7;
    return -1;
}

static int get_month(char *str)
{
    if (0 == os_strncmp(str, f_str("Jan"), 3))
        return 1;
    if (0 == os_strncmp(str, f_str("Feb"), 3))
        return 2;
    if (0 == os_strncmp(str, f_str("Mar"), 3))
        return 3;
    if (0 == os_strncmp(str, f_str("Apr"), 3))
        return 4;
    if (0 == os_strncmp(str, f_str("May"), 3))
        return 5;
    if (0 == os_strncmp(str, f_str("Jun"), 3))
        return 6;
    if (0 == os_strncmp(str, f_str("Jul"), 3))
        return 7;
    if (0 == os_strncmp(str, f_str("Aug"), 3))
        return 8;
    if (0 == os_strncmp(str, f_str("Sep"), 3))
        return 9;
    if (0 == os_strncmp(str, f_str("Oct"), 3))
        return 10;
    if (0 == os_strncmp(str, f_str("Nov"), 3))
        return 11;
    if (0 == os_strncmp(str, f_str("Dec"), 3))
        return 12;
    return -1;
}

static void state_current_time(struct date *time)
{
    uint32 timestamp = timedate_get_timestamp();
    time->timestamp = timestamp;
    char *timestamp_str = timedate_get_timestr(timestamp);
    TRACE("state_current_time date: %s (UTC+%d) [%d]", timestamp_str, timedate_get_timezone(), timestamp);
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
    end_ptr = os_strstr(init_ptr, f_str(" "));
    os_strncpy(tmp_str, init_ptr, (end_ptr - init_ptr));
    time->day_of_month = atoi(tmp_str);
    // get hours
    init_ptr = end_ptr + 1;
    os_memset(tmp_str, 0, 5);
    end_ptr = os_strstr(init_ptr, f_str(":"));
    os_strncpy(tmp_str, init_ptr, (end_ptr - init_ptr));
    time->hours = atoi(tmp_str);
    // get minutes
    init_ptr = end_ptr + 1;
    os_memset(tmp_str, 0, 5);
    end_ptr = os_strstr(init_ptr, f_str(":"));
    os_strncpy(tmp_str, init_ptr, (end_ptr - init_ptr));
    time->minutes = atoi(tmp_str);
    // get seconds
    init_ptr = end_ptr + 1;
    os_memset(tmp_str, 0, 5);
    end_ptr = os_strstr(init_ptr, f_str(" "));
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

struct date *cron_get_current_time(void)
{
    return &current_time;
}

void cron_init_current_time(void)
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
        // os_printf("       param: %X\n", current_job->param);
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
            current_job->command(current_job->param);
        current_job = job_list->next();
    }
}

static int cron_restore_cfg(void);

void cron_init(void)
{
    cron_cfg.enabled = false;
    cron_state.running = false;
    if (cron_restore_cfg() != CFG_ok)
    {
        dia_warn_evnt(CRON_INIT_DEFAULT_CFG);
        WARN("cron_init no cfg available");
    }
    if (cron_cfg.enabled)
    {
        dia_info_evnt(CRON_START);
        INFO("cron started");
    }
    else
    {
        dia_info_evnt(CRON_STOP);
        INFO("cron stopped");
    }
    os_timer_disarm(&cron_timer);
    os_timer_setfn(&cron_timer, (os_timer_func_t *)cron_execute, NULL);
    job_list = new List<struct job>(CRON_MAX_JOBS, delete_content);
    os_memset(&current_time, 0, sizeof(struct date));
    state_current_time(&current_time);
}

/*
 * will sync the cron_timer to a one minute period according to timestamp
 * provided by SNTP
 */
void cron_sync(void)
{
    ALL("cron_sync");
    if (!cron_cfg.enabled)
        return;
    uint32 cron_period;
    uint32 timestamp = timedate_get_timestamp();
    // TRACE("cron timestamp: %d", timestamp);
    timestamp = timestamp % 60;

    if (timestamp < 30)
        // you are late -> next minute period in (60 - timestamp) seconds
        cron_period = 60000 - timestamp * 1000;
    else
        // you are early -> next minute period in (60 + (60 - timestamp)) seconds
        cron_period = 120000 - timestamp * 1000;
    // TRACE("cron period: %d", cron_period);
    os_timer_arm(&cron_timer, cron_period, 0);
    cron_state.running = true;
}

int cron_add_job(char minutes,
                 char hours,
                 char day_of_month,
                 char month,
                 char day_of_week,
                 void (*command)(void *),
                 void *param)
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
    if (new_job == NULL)
    {
        dia_error_evnt(CRON_ADD_JOB_HEAP_EXHAUSTED, sizeof(struct job));
        ERROR("cron_add_job heap exhausted %d", sizeof(struct job));
        return -1;
    }
    new_job->minutes = minutes;
    new_job->hours = hours;
    new_job->day_of_month = day_of_month;
    new_job->month = month;
    new_job->day_of_week = day_of_week;
    new_job->command = command;
    new_job->param = param;
    // assign a fake id
    new_job->id = -1;
    // find a free id
    struct job *job_itr = job_list->front();
    int idx;
    bool id_used;
    // looking for a not used id from 0 to job_list->size()
    for (idx = 1; idx <= job_list->size(); idx++)
    {
        id_used = false;
        while (job_itr)
        {
            // os_printf("-----------> current job\n");
            // os_printf("          id: %d\n", current_job->id);
            if (job_itr->id == idx)
            {
                id_used = true;
                break;
            }
            job_itr = job_list->next();
        }
        if (!id_used)
        {
            new_job->id = idx;
            break;
        }
    }
    // checkout if a free id was found or need to add a new one
    if (new_job->id < 0)
    {
        new_job->id = job_list->size() + 1;
    }
    // add the new job
    int result = job_list->push_back(new_job);
    if (result != list_ok)
    {
        dia_error_evnt(CRON_ADD_JOB_CANNOT_COMPLETE);
        ERROR("cron_add_job cannot complete");
        return -1;
    }
    return new_job->id;
}

void cron_del_job(int job_id)
{
    struct job *job_itr = job_list->front();
    while (job_itr)
    {
        if (job_itr->id == job_id)
        {
            job_list->remove();
            break;
        }
        job_itr = job_list->next();
    }
}

void cron_print_jobs(void)
{
    struct job *job_itr = job_list->front();
    while (job_itr)
    {
        fs_printf("-----------> current job\n");
        fs_printf("          id: %d\n", (signed char)job_itr->id);
        fs_printf("     minutes: %d\n", job_itr->minutes);
        fs_printf("       hours: %d\n", job_itr->hours);
        fs_printf("day of month: %d\n", job_itr->day_of_month);
        fs_printf("       month: %d\n", job_itr->month);
        fs_printf(" day of week: %d\n", job_itr->day_of_week);
        fs_printf("     command: %X\n", job_itr->command);
        fs_printf("       param: %X\n", job_itr->param);
        job_itr = job_list->next();
    }
}

/*
 * CONFIGURATION & PERSISTENCY
 */
void cron_enable(void)
{
    if (!cron_cfg.enabled)
    {
        cron_cfg.enabled = true;
        dia_info_evnt(CRON_ENABLED);
        INFO("cron enabled");
    }
    if (!cron_state.running)
    {
        cron_sync();
        dia_info_evnt(CRON_START);
        INFO("cron started");
    }
}

void cron_start(void)
{
    if (!cron_state.running)
    {
        cron_sync();
        dia_info_evnt(CRON_START);
        INFO("cron started");
    }
}

void cron_disable(void)
{
    os_timer_disarm(&cron_timer);
    if (cron_cfg.enabled)
    {
        cron_cfg.enabled = false;
        dia_info_evnt(CRON_DISABLED);
        INFO("cron disabled");
    }
    if (cron_state.running)
    {
        cron_state.running = false;
        dia_info_evnt(CRON_STOP);
        INFO("cron stopped");
    }
}

void cron_stop(void)
{
    os_timer_disarm(&cron_timer);
    if (cron_state.running)
    {
        cron_state.running = false;
        dia_info_evnt(CRON_STOP);
        INFO("cron stopped");
    }
}

bool cron_enabled(void)
{
    return cron_cfg.enabled;
}

#define CRON_FILENAME ((char *)f_str("cron.cfg"))

static int cron_restore_cfg(void)
{
    ALL("cron_restore_cfg");

    if (!Espfile::exists(CRON_FILENAME))
        return CFG_cantRestore;
    Cfgfile cfgfile(CRON_FILENAME);
    espmem.stack_mon();
    int enabled = cfgfile.getInt(f_str("cron_enabled"));
    if (cfgfile.getErr() != JSON_noerr)
    {
        dia_error_evnt(CRON_RESTORE_CFG_ERROR);
        ERROR("cron_restore_cfg error");
        return CFG_error;
    }
    cron_cfg.enabled = (bool)enabled;
    return CFG_ok;
}

static int cron_saved_cfg_updated(void)
{
    ALL("cron_saved_cfg_updated");
    if (!Espfile::exists(CRON_FILENAME))
    {
        return CFG_notUpdated;
    }
    Cfgfile cfgfile(CRON_FILENAME);
    espmem.stack_mon();
    int enabled = cfgfile.getInt(f_str("cron_enabled"));
    if (cfgfile.getErr() != JSON_noerr)
    {
        // no need to arise an error, the cfg file will be overwritten
        // dia_error_evnt(CRON_SAVED_CFG_UPDATED_ERROR);
        // ERROR("cron_saved_cfg_updated error");
        return CFG_error;
    }
    if (cron_cfg.enabled != (bool)enabled)
    {
        return CFG_notUpdated;
    }
    return CFG_ok;
}

char *cron_cfg_json_stringify(char *dest, int len)
{
    // {"cron_enabled":0}
    int msg_len = 18 + 1;
    char *msg;
    if (dest == NULL)
    {
        msg = new char[msg_len];
        if (msg == NULL)
        {
            dia_error_evnt(CRON_CFG_STRINGIFY_HEAP_EXHAUSTED, msg_len);
            ERROR("cron_cfg_json_stringify heap exhausted [%d]", msg_len);
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
               "{\"cron_enabled\":%d}",
               cron_cfg.enabled);
    return msg;
}

int cron_cfg_save(void)
{
    ALL("cron_cfg_save");
    if (cron_saved_cfg_updated() == CFG_ok)
        return CFG_ok;
    Cfgfile cfgfile(CRON_FILENAME);
    espmem.stack_mon();
    if (cfgfile.clear() != SPIFFS_OK)
        return CFG_error;
    char str[19];
    cron_cfg_json_stringify(str, 19);
    int res = cfgfile.n_append(str, os_strlen(str));
    if (res < SPIFFS_OK)
        return CFG_error;
    return CFG_ok;
}