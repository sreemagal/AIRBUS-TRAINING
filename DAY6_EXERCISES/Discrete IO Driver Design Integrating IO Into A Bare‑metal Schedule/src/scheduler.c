/*
 * scheduler.c
 *
 *  Created on: 28 Dec 2025
 *      Author: Lenovo
 */

#include "scheduler.h"
#include "fsl_common.h"

static volatile uint32_t g_ms = 0u;

void SysTick_Handler(void)
{
    g_ms++;
}

void Scheduler_Init1msTick(uint32_t coreClockHz)
{
    (void)SysTick_Config(coreClockHz / 1000u);
}

uint32_t Scheduler_Millis(void)
{
    return g_ms;
}

void Scheduler_Run(sched_task_t *tasks, uint32_t task_count)
{
    /* Initialize next-release times */
    uint32_t now = Scheduler_Millis();
    for (uint32_t i = 0; i < task_count; i++)
    {
        tasks[i].next_release_ms = now + tasks[i].period_ms;
    }

    while (true)
    {
        now = Scheduler_Millis();
        bool ran_any = false;

        for (uint32_t i = 0; i < task_count; i++)
        {
            /* Signed diff handles wrap safely */
            while ((int32_t)(now - tasks[i].next_release_ms) >= 0)
            {
                tasks[i].next_release_ms += tasks[i].period_ms;
                tasks[i].fn(now);
                ran_any = true;

                /* Refresh now in case the task took time */
                now = Scheduler_Millis();
            }
        }

        if (!ran_any)
        {
            /* Idle: busy spin (no sleep instruction used) */
            __NOP();
        }
    }
}

