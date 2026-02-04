/*
 * Copyright (c) 2015, Freescale Semiconductor, Inc.
 * Copyright 2016-2020 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "app.h"

#include "fsl_common.h"
#include "fsl_clock.h"
#include "fsl_debug_console.h"
#include "fsl_gpio.h"
#include "fsl_iomuxc.h"
#include "fsl_device_registers.h"

#include "lab_config.h"
#include "scheduler.h"
#include "eventq.h"
#include "discrete_in.h"
#include "discrete_out.h"

/* ----------------- Channels ----------------- */
enum
{
    CH_WOW_A = 0,
    CH_WOW_B = 1,
    CH_COUNT
};

/* ----------------- Global driver objects ----------------- */
static dio_in_t g_in_wowA;
static dio_in_t g_in_wowB;
static dio_out_t g_out_led;

/* Two-stage queue to match the lab structure: IO->EventPump->App */
static evt_t g_evtq_io_storage[EVENTQ_DEPTH];
static evt_t g_evtq_app_storage[EVENTQ_DEPTH];
static eventq_t g_evtq_io;
static eventq_t g_evtq_app;

/* ----------------- App state ----------------- */
static bool g_led_toggle_req = false;
static uint32_t g_sw8_press_start_ms = 0u;

/* Avionics */
static bool g_wow_true = false;
static uint32_t g_wow_qual_start_ms = 0u;
static uint32_t g_wow_dequal_start_ms = 0u;
static bool g_fault_wow_disagree = false;
static uint32_t g_disagree_start_ms = 0u;

/* LED blink state for avionics allow indicator */
static bool g_led_blink_phase = false;
static bool Console_TryReadChar(char *out)
{
    int c = DbgConsole_Getchar();   /* returns -1 if no char available */
    if (c < 0)
    {
        return false;
    }
    *out = (char)c;
    return true;
}


/* ----------------- Pin mux helpers ----------------- */
static void Lab_ConfigPins(void)
{
    CLOCK_EnableClock(kCLOCK_Iomuxc);
    CLOCK_EnableClock(kCLOCK_IomuxcSnvs);

    /* USER LED already configured by pin_mux.c in the base example, but safe to mux again */
    IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B0_09_GPIO1_IO09, 0U);

    /* SW8: SNVS_WAKEUP -> GPIO5_IO00 */
    IOMUXC_SetPinMux(IOMUXC_SNVS_WAKEUP_GPIO5_IO00, 0U);

    /* Pull-up + hysteresis on SW8 to avoid floating/noise.
     * Note: SW8 is typically active-low on EVKB.
     */
    IOMUXC_SetPinConfig(IOMUXC_SNVS_WAKEUP_GPIO5_IO00,
                        IOMUXC_SNVS_SW_PAD_CTL_PAD_WAKEUP_PKE_MASK |
                        IOMUXC_SNVS_SW_PAD_CTL_PAD_WAKEUP_PUE_MASK |
                        IOMUXC_SNVS_SW_PAD_CTL_PAD_WAKEUP_PUS(2U) |
                        IOMUXC_SNVS_SW_PAD_CTL_PAD_WAKEUP_HYS_MASK);

#if (LAB_ENABLE_WOW_B != 0)
    /* WOW_B jumper input: GPIO_AD_B0_10 -> GPIO1_IO10 */
    IOMUXC_SetPinMux(LAB_WOWB_IOMUXC, 0U);

    /* Pull-down + hysteresis for a stable default 0 when floating */
    IOMUXC_SetPinConfig(LAB_WOWB_IOMUXC,
                        IOMUXC_SW_PAD_CTL_PAD_PKE_MASK |
                        IOMUXC_SW_PAD_CTL_PAD_PUE_MASK |
                        IOMUXC_SW_PAD_CTL_PAD_PUS(0U) | /* pulldown */
                        IOMUXC_SW_PAD_CTL_PAD_HYS_MASK |
                        IOMUXC_SW_PAD_CTL_PAD_SRE(0U) |
                        IOMUXC_SW_PAD_CTL_PAD_SPEED(0U) |
                        IOMUXC_SW_PAD_CTL_PAD_DSE(2U));
#endif
}

/* ----------------- Task: EventPump 1ms ----------------- */
static void Task_EventPump_1ms(uint32_t now_ms)
{
    (void)now_ms;

    evt_t e;
    while (EventQ_Pop(&g_evtq_io, &e))
    {
        (void)EventQ_Push(&g_evtq_app, &e);
    }
}

/* ----------------- Task: DiscreteIn 5ms ----------------- */
static void Task_DiscreteIn_5ms(uint32_t now_ms)
{
    (void)now_ms;

    bool changedA = DIO_InUpdate(&g_in_wowA);
    if (changedA)
    {
        evt_t e = {0};
        e.channel = CH_WOW_A;
        e.t_ms = Scheduler_Millis();
        e.value = DIO_InGet(&g_in_wowA) ? 1u : 0u;
        e.type = DIO_InGet(&g_in_wowA) ? EVT_EDGE_RISE : EVT_EDGE_FALL;
        (void)EventQ_Push(&g_evtq_io, &e);
    }

#if (LAB_ENABLE_WOW_B != 0)
    bool changedB = DIO_InUpdate(&g_in_wowB);
    if (changedB)
    {
        evt_t e = {0};
        e.channel = CH_WOW_B;
        e.t_ms = Scheduler_Millis();
        e.value = DIO_InGet(&g_in_wowB) ? 1u : 0u;
        e.type = DIO_InGet(&g_in_wowB) ? EVT_EDGE_RISE : EVT_EDGE_FALL;
        (void)EventQ_Push(&g_evtq_io, &e);
    }
#endif
}

/* ----------------- Helpers: long-press lamp test ----------------- */
static void UpdateLampTest(uint32_t now_ms, bool sw8_asserted)
{
    if (sw8_asserted)
    {
        if (g_sw8_press_start_ms == 0u)
        {
            g_sw8_press_start_ms = now_ms;
        }

        if ((now_ms - g_sw8_press_start_ms) >= LAMP_TEST_HOLD_MS)
        {
            g_out_led.lampTest = true;
        }
    }
    else
    {
        g_sw8_press_start_ms = 0u;
        g_out_led.lampTest = false;
    }
}

/* ----------------- Task: App 10ms ----------------- */
static void Task_App_10ms(uint32_t now_ms)
{
    /* Drain app queue */
    evt_t e;
    while (EventQ_Pop(&g_evtq_app, &e))
    {
        if (LAB_APP_MODE == LAB_MODE_GENERIC)
        {
            if ((e.channel == CH_WOW_A) && (e.type == EVT_EDGE_RISE))
            {
                g_led_toggle_req = !g_led_toggle_req;
 PRINTF("[%8lu ms] SW8 PRESS -> LED_REQ toggled=%u", (unsigned long)e.t_ms,g_led_toggle_req ? 1u : 0u);
            }
            else if ((e.channel == CH_WOW_A) && (e.type == EVT_EDGE_FALL))
            {
                PRINTF("[%8lu ms] SW8 RELEASE", (unsigned long)e.t_ms);
            }
        }
        else
        {
            /* Avionics logging */
            if (e.channel == CH_WOW_A)
            {
   PRINTF("[%8lu ms] WOW_A=%u", (unsigned long)e.t_ms, e.value);
            }
            else if (e.channel == CH_WOW_B)
            {
   PRINTF("[%8lu ms] WOW_B=%u", (unsigned long)e.t_ms, e.value);
            }
        }
    }

    /* Non-blocking fault clear in avionics mode */
    if (LAB_APP_MODE == LAB_MODE_AVIONICS)
    {
        char ch;
        if (Console_TryReadChar(&ch) && ((ch == 'c') || (ch == 'C')))
        {
            if (g_fault_wow_disagree)
            {
                g_fault_wow_disagree = false;
                evt_t eclr = {0};
                eclr.type = EVT_FAULT_CLEARED;
                eclr.t_ms = now_ms;
                (void)EventQ_Push(&g_evtq_io, &eclr);
            }
        }
    }

    /* Long-press lamp test always supported via WOW_A (SW8) */
    bool sw8_asserted = DIO_InGet(&g_in_wowA);
    UpdateLampTest(now_ms, sw8_asserted);

    if (LAB_APP_MODE == LAB_MODE_AVIONICS)
    {
        /* WOW consolidation rules */
        bool wowA = DIO_InGet(&g_in_wowA);

#if (LAB_ENABLE_WOW_B != 0)
        bool wowB = DIO_InGet(&g_in_wowB);
#else
        bool wowB = wowA;
#endif

        /* Qualify/dequalify WOW_TRUE with 40ms filtering */
        if (!g_wow_true)
        {
            if (wowA && wowB)
            {
                if (g_wow_qual_start_ms == 0u)
                {
                    g_wow_qual_start_ms = now_ms;
                }

                if ((now_ms - g_wow_qual_start_ms) >= WOW_QUALIFY_MS)
                {
                    g_wow_true = true;
                    g_wow_qual_start_ms = 0u;
                    evt_t et = {0};
                    et.type = EVT_WOW_TRUE_RISE;
                    et.t_ms = now_ms;
                    (void)EventQ_Push(&g_evtq_io, &et);
     PRINTF("[%8lu ms] WOW_TRUE=1 (qualified)", (unsigned long)now_ms);
                }
            }
            else
            {
                g_wow_qual_start_ms = 0u;
            }
        }
        else
        {
            if ((!wowA) || (!wowB))
            {
                if (g_wow_dequal_start_ms == 0u)
                {
                    g_wow_dequal_start_ms = now_ms;
                }

                if ((now_ms - g_wow_dequal_start_ms) >= WOW_QUALIFY_MS)
                {
                    g_wow_true = false;
                    g_wow_dequal_start_ms = 0u;
                    evt_t ef = {0};
                    ef.type = EVT_WOW_TRUE_FALL;
                    ef.t_ms = now_ms;
                    (void)EventQ_Push(&g_evtq_io, &ef);
  PRINTF("[%8lu ms] WOW_TRUE=0 (de-qualified)", (unsigned long)now_ms);
                }
            }
            else
            {
                g_wow_dequal_start_ms = 0u;
            }
        }

        /* Disagree fault latch (500 ms continuous disagreement) */
        if (wowA != wowB)
        {
            if (g_disagree_start_ms == 0u)
            {
                g_disagree_start_ms = now_ms;
            }
            else if (!g_fault_wow_disagree && ((now_ms - g_disagree_start_ms) >= WOW_DISAGREE_LATCH_MS))
            {
                g_fault_wow_disagree = true;
                evt_t efault = {0};
                efault.type = EVT_FAULT_LATCHED;
                efault.t_ms = now_ms;
                (void)EventQ_Push(&g_evtq_io, &efault);
     PRINTF("[%8lu ms] FAULT_WOW_DISAGREE LATCHED", (unsigned long)now_ms);
            }
        }
        else
        {
            g_disagree_start_ms = 0u;
        }

        /* Safe inhibit if fault latched */
        g_out_led.safeInhibit = g_fault_wow_disagree;

        /* “Thrust reverser test allowed” indicator:
         * Allowed when WOW_TRUE=1 and no fault.
         * We represent allowed by blinking LED at 2 Hz.
         */
        bool allowed = (g_wow_true && !g_fault_wow_disagree);
        if (allowed)
        {
            if ((now_ms / LED_BLINK_HALF_PERIOD_MS) & 1u)
            {
                g_out_led.request = true;
            }
            else
            {
                g_out_led.request = false;
            }
        }
        else
        {
            g_out_led.request = false;
        }

        /* periodic status line (every 100ms) */
        static uint32_t next_status_ms = 0u;
        if ((int32_t)(now_ms - next_status_ms) >= 0)
        {
            next_status_ms = now_ms + 100u;
      PRINTF("[%8lu ms] STATUS WOW_A=%u WOW_B=%u WOW_TRUE=%u FAULT=%u LED_REQ=%u LAMP=%u DROPPED(io=%lu app=%lu)",
                   (unsigned long)now_ms,
                   wowA ? 1u : 0u,
                   wowB ? 1u : 0u,
                   g_wow_true ? 1u : 0u,
                   g_fault_wow_disagree ? 1u : 0u,
                   g_out_led.request ? 1u : 0u,
                   g_out_led.lampTest ? 1u : 0u,
                   (unsigned long)g_evtq_io.dropped,
                   (unsigned long)g_evtq_app.dropped);
        }
    }
    else
    {
        /* Generic mode output request */
        g_out_led.safeInhibit = false;
        g_out_led.request = g_led_toggle_req;
    }
}

/* ----------------- Task: DiscreteOut 10ms ----------------- */
static void Task_DiscreteOut_10ms(uint32_t now_ms)
{
    (void)now_ms;
    DIO_OutApply(&g_out_led);
}

/* ----------------- Task: Heartbeat 100ms (optional) ----------------- */
static void Task_Heartbeat_100ms(uint32_t now_ms)
{
    (void)now_ms;
    g_led_blink_phase = !g_led_blink_phase;
}

int main(void)
{
    BOARD_InitHardware();
    Lab_ConfigPins();

    /* Init queues */
    EventQ_Init(&g_evtq_io, g_evtq_io_storage, EVENTQ_DEPTH);
    EventQ_Init(&g_evtq_app, g_evtq_app_storage, EVENTQ_DEPTH);

    /* Enable GPIO clocks used */
    CLOCK_EnableClock(kCLOCK_Gpio1);
    CLOCK_EnableClock(kCLOCK_Gpio5);

    /* Inputs */
    DIO_InInit(&g_in_wowA, BOARD_USER_BUTTON_GPIO, BOARD_USER_BUTTON_GPIO_PIN, false, IN_COUNT_MAX);

#if (LAB_ENABLE_WOW_B != 0)
    DIO_InInit(&g_in_wowB, LAB_WOWB_GPIO, LAB_WOWB_PIN, true, IN_COUNT_MAX);
#else
    DIO_InInit(&g_in_wowB, BOARD_USER_BUTTON_GPIO, BOARD_USER_BUTTON_GPIO_PIN, false, IN_COUNT_MAX);
#endif

    /* Output: USER LED is typically active-low */
    DIO_OutInit(&g_out_led, BOARD_USER_LED_GPIO, BOARD_USER_LED_GPIO_PIN, false, false);

    /* Start SysTick scheduler */
    Scheduler_Init1msTick(CLOCK_GetFreq(kCLOCK_CoreSysClk));

    PRINTF("=== Bare-metal Scheduler + Discrete IO Lab ===");
    PRINTF("Mode: %s", (LAB_APP_MODE == LAB_MODE_AVIONICS) ? "AVIONICS" : "GENERIC");
    PRINTF("Task periods: EventPump=%ums In=%ums App=%ums Out=%ums Heartbeat=%ums",
           (unsigned)TASK_EVENTPUMP_PERIOD_MS,
           (unsigned)TASK_DISCRETEIN_PERIOD_MS,
           (unsigned)TASK_APP_PERIOD_MS,
           (unsigned)TASK_DISCRETEOUT_PERIOD_MS,
           (unsigned)TASK_HEARTBEAT_PERIOD_MS);

    PRINTF("Input WOW_A: %s (GPIO5_IO00)", BOARD_USER_BUTTON_NAME);
#if (LAB_ENABLE_WOW_B != 0)
    PRINTF("Input WOW_B: GPIO1_IO10 (GPIO_AD_B0_10) with pull-down");
#else
    PRINTF("Input WOW_B: disabled (tied to WOW_A)");
#endif

    PRINTF("Debounce integrator: sample=%ums countMax=%u => ~%ums",
           (unsigned)TASK_DISCRETEIN_PERIOD_MS,
           (unsigned)IN_COUNT_MAX,
           (unsigned)(TASK_DISCRETEIN_PERIOD_MS * IN_COUNT_MAX));

    if (LAB_APP_MODE == LAB_MODE_AVIONICS)
    {
        PRINTF("Avionics: WOW qualify=%ums, disagree latch=%ums (press 'c' to clear fault)",
               (unsigned)WOW_QUALIFY_MS,
               (unsigned)WOW_DISAGREE_LATCH_MS);
    }
    else
    {
        PRINTF("Generic: press SW8 toggles LED request; hold >%ums for lamp test",
               (unsigned)LAMP_TEST_HOLD_MS);
    }

    sched_task_t tasks[] = {
        {"EventPump",   TASK_EVENTPUMP_PERIOD_MS,   0u, Task_EventPump_1ms},
        {"DiscreteIn",  TASK_DISCRETEIN_PERIOD_MS,  0u, Task_DiscreteIn_5ms},
        {"App",         TASK_APP_PERIOD_MS,         0u, Task_App_10ms},
        {"DiscreteOut", TASK_DISCRETEOUT_PERIOD_MS, 0u, Task_DiscreteOut_10ms},
        {"Heartbeat",   TASK_HEARTBEAT_PERIOD_MS,   0u, Task_Heartbeat_100ms},
    };

    Scheduler_Run(tasks, (uint32_t)(sizeof(tasks) / sizeof(tasks[0])));

    /* Should never reach here */
    for (;;)
    {
    }
}
