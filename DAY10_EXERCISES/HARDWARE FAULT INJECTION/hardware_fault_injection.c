/*
 * Copyright (c) 2013 - 2015, Freescale Semiconductor, Inc.
 * Copyright 2016-2017, 2024 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fsl_device_registers.h"
#include "fsl_debug_console.h"
#include "board.h"
#include "app.h"
#include "reset_diag.h"
#include "wdog_window_demo.h"
#include "arinc_sim.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#ifndef ARINC_ROLE_RDC
#define ARINC_ROLE_RDC     1
#endif

#ifndef ARINC_ROLE_BRIDGE
#define ARINC_ROLE_BRIDGE  0
#endif


/*******************************************************************************
 * Prototypes
 ******************************************************************************/

/*******************************************************************************
 * Variables
 ******************************************************************************/

/*******************************************************************************
 * Code
 ******************************************************************************/
/*!
 * @brief Main function
 */
int main(void)
{
    char ch;

    /* Init board hardware. */
    BOARD_InitHardware();

    ResetDiag_Info_t info = ResetDiag_RunEarly();

PRINTF("\r\nPress keys to inject faults:\r\n");
PRINTF("  1 = Software reset (SYSRESETREQ)\r\n");
PRINTF("  2 = RTWDOG missed-refresh timeout reset\r\n");
PRINTF("  3 = RTWDOG early-refresh (window violation) reset\r\n");
PRINTF("  a = Start ARINC simulation role\r\n");
PRINTF("\r\nAlso try hardware: SW4 (Reset) and SW3 (POR reset).\r\n");

while (1)
{
    const char ch = (char)GETCHAR();

    if (ch == '1')
    {
        PRINTF("\r\n[FAULT] Triggering software reset now...\r\n");
        NVIC_SystemReset();
    }
    else if (ch == '2')
    {
        WdogWindowDemo_Run(false, true);
    }
    else if (ch == '3')
    {
        WdogWindowDemo_Run(true, false);
    }
    else if (ch == 'a')
    {
#if ARINC_ROLE_RDC
        ArincSim_RunRdc(info.bootCount, info.resetFlags);
#elif ARINC_ROLE_BRIDGE
        ArincSim_RunBridge();
#else
        PRINTF("ARINC roles disabled.\r\n");
#endif
    }
}

}
