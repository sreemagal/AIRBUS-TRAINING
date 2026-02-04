/*
 * dio_timebase_pit.c
 *
 *  Created on: 29 Dec 2025
 *      Author: Lenovo
 */

#include "dio_timebase_pit.h"

#include "fsl_common.h"
#include "fsl_clock.h"
#include "fsl_pit.h"

/* Use PIT channel 0 as a free-running timer.
 * We program LDVAL=0xFFFFFFFF and let it roll over.
 */
#define TB_PIT_BASE    PIT
#define TB_PIT_CH      kPIT_Chnl_0
#define TB_LDVAL_MAX   (0xFFFFFFFFu)

static uint32_t g_tb_clk_hz = 0u;
static uint32_t g_ticks_per_us = 0u; /* integer ticks/us (works with 24MHz = 24) */

static uint32_t g_last_cval = 0u;
static uint64_t g_tick_accum = 0u; /* accumulated PIT ticks (monotonic) */

uint32_t DIO_TimebaseClockHz_PIT(void)
{
    return g_tb_clk_hz;
}

void DIO_TimebaseInit_PIT(void)
{
    pit_config_t cfg;

    PIT_GetDefaultConfig(&cfg);
    PIT_Init(TB_PIT_BASE, &cfg);

    /* Stop channel, program LDVAL, clear flag, then start. */
    PIT_StopTimer(TB_PIT_BASE, TB_PIT_CH);

    TB_PIT_BASE->CHANNEL[TB_PIT_CH].LDVAL = TB_LDVAL_MAX;
    PIT_ClearStatusFlags(TB_PIT_BASE, TB_PIT_CH, kPIT_TimerFlag);

    PIT_StartTimer(TB_PIT_BASE, TB_PIT_CH);

    /* The official PIT example for this board uses kCLOCK_OscClk as the PIT source clock. */
    g_tb_clk_hz = CLOCK_GetFreq(kCLOCK_OscClk);
    if (g_tb_clk_hz == 0u)
    {
        g_tb_clk_hz = 24000000u; /* last-resort safe guess */
    }

    g_ticks_per_us = g_tb_clk_hz / 1000000u;
    if (g_ticks_per_us == 0u)
    {
        g_ticks_per_us = 1u;
    }

    /* Initialize accumulators */
    g_last_cval = PIT_GetCurrentTimerCount(TB_PIT_BASE, TB_PIT_CH);
    g_tick_accum = 0u;
}

uint64_t DIO_TimeNowUs_PIT(void)
{
    /* Protect against concurrent calls from main + ISR.
     * (GPIO ISR calls this to timestamp the interrupt.)
     */
    uint32_t primask = DisableGlobalIRQ();

    uint32_t cur = PIT_GetCurrentTimerCount(TB_PIT_BASE, TB_PIT_CH);

    /* PIT counts down. Normal case: cur <= last.
     * If cur > last -> rollover occurred.
     */
    uint32_t delta_ticks;
    if (cur <= g_last_cval)
    {
        delta_ticks = g_last_cval - cur;
    }
    else
    {
        /* Rollover: last -> 0, reload to LDVAL, then down to cur */
        delta_ticks = (g_last_cval + 1u) + (TB_LDVAL_MAX - cur);
    }

    g_last_cval = cur;
    g_tick_accum += (uint64_t)delta_ticks;

    EnableGlobalIRQ(primask);

    /* Convert ticks to microseconds using integer ticks/us.
     * With 24MHz clock: 24 ticks = 1 us.
     */
    return (g_tick_accum / (uint64_t)g_ticks_per_us);
}

