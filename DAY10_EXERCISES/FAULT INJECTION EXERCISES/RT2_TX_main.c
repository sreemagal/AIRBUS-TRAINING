#include "fsl_device_registers.h"
#include "fsl_debug_console.h"
#include "board.h"
#include "app.h"

#include "fsl_common.h"
#include "fsl_lpuart.h"

#include "fi/fi.h"
#include "fi/uart_fi_shim.h"
#include "fi/arinc429.h"

void FI_CLI_Poll(void); /* from fi_cli.c */

#define LINK_UART LPUART3

static void LinkUart_Init115200(void)
{
    lpuart_config_t cfg;
    LPUART_GetDefaultConfig(&cfg);
    cfg.baudRate_Bps = 115200U;
    cfg.enableTx     = true;
    cfg.enableRx     = true;
    (void)LPUART_Init(LINK_UART, &cfg, BOARD_DebugConsoleSrcFreq());
}

int main(void)
{
    BOARD_InitHardware();
    LinkUart_Init115200();

    PRINTF("\r\nRT-2 TX (exceptions + MPU) start\r\n");
    PRINTF("Use CLI from RT-1 for FI control.\r\n");

    FI_Init();

    FI_SetEnabled(false);
    FI_SetMask(0xFFFFFFFFu);
    FI_SetProbability(100); /* make exception injections obvious when enabled */

    arinc429_word_t aw = { .label = 0x12u, .sdi = 1u, .data = 0x12345u, .ssm = 2u };

    while (1)
    {
        FI_CLI_Poll();

        /* Keep sending ARINC-like traffic */
        (void)ARINC429_SendWord(LINK_UART, &aw);

        /* Exception injection points (enable FI and set mask EXCEPTIONS when ready) */
        FI_POINT(FI_F_EXCEPTIONS, {
            PRINTF("Inject divide-by-zero (UsageFault expected)\r\n");
            FI_Inject_DivByZero();
        });

        FI_POINT(FI_F_EXCEPTIONS, {
            PRINTF("Inject unaligned access (UsageFault expected)\r\n");
            FI_Inject_UnalignedAccess();
        });

        FI_POINT(FI_F_EXCEPTIONS, {
            PRINTF("Inject MPU no-access read (MemManage expected)\r\n");
            FI_Inject_MemFault();
        });

        SDK_DelayAtLeastUs(200000u, SystemCoreClock); /* slow down so you see logs */
    }
}
