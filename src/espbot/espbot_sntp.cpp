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

#include "espbot_sntp.hpp"
#include "espbot.hpp"
#include "espbot_global.hpp"
#include "espbot_logger.hpp"

void ICACHE_FLASH_ATTR Sntp::start(void)
{
    esplog.all("Sntp::start\n");
    sntp_setservername(0, "0.pool.ntp.org");
    sntp_setservername(1, "1.pool.ntp.org");
    sntp_setservername(2, "2.pool.ntp.org");
    if (sntp_set_timezone(1) == false)
        esplog.error("Sntp::start - cannot set timezone\n");
    sntp_init();
    esplog.debug("Sntp started\n");
}

void ICACHE_FLASH_ATTR Sntp::stop(void)
{
    esplog.all("Sntp::stop\n");
    sntp_stop();
    esplog.debug("Sntp ended\n");
}

uint32 ICACHE_FLASH_ATTR Sntp::get_timestamp()
{
    return sntp_get_current_timestamp();
}

char ICACHE_FLASH_ATTR *Sntp::get_timestr(uint32 t_time)
{
    return sntp_get_real_time(t_time);
}
