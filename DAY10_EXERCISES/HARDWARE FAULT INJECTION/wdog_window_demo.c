#include "wdog_window_demo.h"

#include "fsl_debug_console.h"
#include "fsl_rtwdog.h"
#include "fsl_common.h"

static void delay_ms(uint32_t ms)
{
    /* SDK helper: delays at least the requested time. */
    SDK_DelayAtLeastUs((uint64_t)ms * 1000ULL, SystemCoreClock);
}

void WdogWindowDemo_Run(bool triggerEarlyViolation, bool triggerMissedRefresh)
{
    rtwdog_config_t cfg;
    RTWDOG_GetDefaultConfig(&cfg);

    cfg.enableRtwdog      = true;
    cfg.enableUpdate      = true;
    cfg.enableInterrupt   = false;
    cfg.enableWindowMode  = true;

    /* Make debugging friendlier: allow halting without instant watchdog reset. */
    cfg.workMode.enableDebug = true;

    /*
     * Windowed watchdog behavior (counter counts down):
     *  - If you refresh too early (counter still above windowValue) => reset
     *  - If you refresh too late (counter reaches 0) => reset
     *
     * Choose conservative values for lab demonstration.
     */
    cfg.timeoutValue = 0xB000U; /* overall timeout */
    cfg.windowValue  = 0x6000U; /* earliest allowed refresh */

    PRINTF("\r\n[WDOG] Initializing RTWDOG windowed mode (window=0x%04x, timeout=0x%04x)\r\n",
           cfg.windowValue, cfg.timeoutValue);

    RTWDOG_Init(RTWDOG, &cfg);

    /* Normal operation: refresh inside the allowed window for a few cycles. */
    for (int i = 0; i < 5; i++)
    {
        /* Wait until we are inside the refresh window. */
        while (RTWDOG_GetCounterValue(RTWDOG) > (uint32_t)cfg.windowValue)
        {
            /* spin */
        }
        RTWDOG_Refresh(RTWDOG);
        PRINTF("[WDOG] Refreshed inside window (cycle %d)\r\n", i);
        delay_ms(50);
    }

    if (triggerEarlyViolation)
    {
        PRINTF("[WDOG] Triggering EARLY refresh violation now (expect reset) ...\r\n");
        /* Refresh immediately while counter is high -> violation. */
        RTWDOG_Refresh(RTWDOG);
        while (1) { /* should reset */ }
    }

    if (triggerMissedRefresh)
    {
        PRINTF("[WDOG] Triggering MISSED refresh timeout (expect reset) ...\r\n");
        /* Do not refresh anymore; just wait. */
        while (1)
        {
            delay_ms(200);
            PRINTF("[WDOG] waiting... CNT=0x%04lx\r\n", (unsigned long)RTWDOG_GetCounterValue(RTWDOG));
        }
    }

    PRINTF("[WDOG] Demo finished without forced reset.\r\n");
}
