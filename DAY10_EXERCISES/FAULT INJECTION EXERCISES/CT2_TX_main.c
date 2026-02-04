#include "fsl_device_registers.h"
#include "fsl_debug_console.h"
#include "board.h"
#include "app.h"

#include "fsl_common.h"
#include "fsl_lpuart.h"

#include "fi/fi.h"
#include "fi/arinc429.h"

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

    PRINTF("\r\nCT-2 TX (--wrap LPUART_WriteBlocking) start\r\n");

    FI_Init();

#if FI_ENABLE
    FI_SetEnabled(true);
    FI_SetMask(FI_F_UART_TX_BUSY | FI_F_UART_CORRUPT | FI_F_ARINC_PARITY | FI_F_ARINC_LABEL);
    FI_SetProbability(5);
#else
    FI_SetEnabled(false);
#endif

    arinc429_word_t aw = { .label = 0x12u, .sdi = 1u, .data = 0x12345u, .ssm = 2u };

    while (1)
    {
        status_t st = ARINC429_SendWord(LINK_UART, &aw);
        if (st == kStatus_LPUART_TxBusy)
            PRINTF("TX: busy (from wrapper), retrying\r\n");

        SDK_DelayAtLeastUs(10000u, SystemCoreClock);
    }
}
