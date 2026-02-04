/*
 * scheduler.h
 *
 *  Created on: 28 Dec 2025
 *      Author: Lenovo
 */

#ifndef SCHEDULER_H_
#define SCHEDULER_H_
#include <stdint.h>
#include <stdbool.h>

typedef void (*task_fn_t)(uint32_t now_ms);

typedef struct
{
    const char *name;
    uint32_t period_ms;
    uint32_t next_release_ms;
    task_fn_t fn;
} sched_task_t;

void Scheduler_Init1msTick(uint32_t coreClockHz);
uint32_t Scheduler_Millis(void);

void Scheduler_Run(sched_task_t *tasks, uint32_t task_count);

#endif /* SCHEDULER_H_ */
