/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */
#ifndef __DIO_TASK_H__
#define __DIO_TASK_H__

#include "c_types.h"

#define ESPBOT_MEM 1 // will use espbot_2.0 zalloc and free \
                     // undefine this if you want to use sdk os_zalloc and os_free

#define DIO_TASK_QUEUE_LEN 4
#define SIG_DO_SEQ_COMPLETED 1
#define SIG_DI_SEQ_COMPLETED 2

void init_dio_task(void);

#endif