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

    PRINTF("\r\nRT-1 TX (runtime policy + CLI) start\r\n");
    PRINTF("Type: help\r\n");

    FI_Init();

    /* Build includes FI. Start disabled; enable via CLI. */
    FI_SetEnabled(false);
    FI_SetMask(0xFFFFFFFFu);
    FI_SetProbability(5);
    FI_SetSeed(0x0000BEEFu);

    /* Required triggers (can also arm via CLI): */
    FI_ArmNth(FI_F_ARINC_PARITY, 10);
    FI_ArmWindowMs(FI_F_ARINC_PARITY, 2000u, 2100u);

    arinc429_word_t aw = { .label = 0x12u, .sdi = 1u, .data = 0x12345u, .ssm = 2u };

    uint32_t tx_ctr = 0;

    while (1)
    {
        FI_CLI_Poll();

        /* Event counting for Nth trigger */
        FI_NotifyEvent(FI_F_ARINC_PARITY);

        status_t st = ARINC429_SendWord(LINK_UART, &aw);
        if (st == kStatus_LPUART_TxBusy)
            PRINTF("TX: busy\r\n");

        if ((tx_ctr % 100u) == 0u)
        {
            PRINTF("t=%ums tx=%u FI=%d mask=0x%08x pct=%u\r\n",
                   (unsigned)FI_NowMs(), (unsigned)tx_ctr, FI_IsEnabled(),
                   (unsigned)FI_GetMask(), (unsigned)FI_GetProbability());
        }

        tx_ctr++;
        SDK_DelayAtLeastUs(10000u, SystemCoreClock);
    }
}
