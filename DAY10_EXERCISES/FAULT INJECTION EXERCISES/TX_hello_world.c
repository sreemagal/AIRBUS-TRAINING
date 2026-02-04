#include "fsl_device_registers.h"
#include "fsl_debug_console.h"
#include "board.h"
#include "app.h"

#include "fsl_common.h"
#include "fsl_lpuart.h"

#include "fi/fi.h"
#include "fi/uart_fi_shim.h"
#include "fi/arinc429.h"

#define LINK_UART LPUART3
#define MAX_BUSY_RETRY 3

static void LinkUart_Init115200(void)
{
    lpuart_config_t cfg;
    LPUART_GetDefaultConfig(&cfg);
    cfg.baudRate_Bps = 115200U;
    cfg.enableTx     = true;
    cfg.enableRx     = true;

    /* Same clock helper used by SDK LPUART examples */
    (void)LPUART_Init(LINK_UART, &cfg, BOARD_DebugConsoleSrcFreq());
}

static uint8_t crc8(const uint8_t *d, uint32_t n)
{
    uint8_t c = 0x00u;
    for (uint32_t i = 0; i < n; i++)
    {
        uint8_t b = d[i];
        for (int k = 0; k < 8; k++)
        {
            uint8_t mix = (uint8_t)((c ^ b) & 0x01u);
            c >>= 1;
            if (mix)
                c ^= 0x8Cu; /* x^8 + x^5 + x^4 + 1 */
            b >>= 1;
        }
    }
    return c;
}

static void safe_mode_blink_forever(void)
{
    PRINTF("SAFE MODE: persistent UART busy -> blinking LED\r\n");
    while (1)
    {
        USER_LED_TOGGLE();
        SDK_DelayAtLeastUs(250000u, SystemCoreClock); /* 250 ms */
    }
}

int main(void)
{
    BOARD_InitHardware();
    LinkUart_Init115200();

    PRINTF("\r\nCT-1 TX (call-site shim) start\r\n");

    FI_Init();

#if FI_ENABLE
    FI_SetEnabled(true);
    FI_SetMask(FI_F_UART_TX_BUSY | FI_F_UART_CORRUPT | FI_F_ARINC_PARITY | FI_F_ARINC_LABEL | FI_F_MEM_BITFLIP);
    FI_SetProbability(5); /* 5% */
#else
    FI_SetEnabled(false);
#endif

    uint32_t ctr = 0;

    /* Avionics-like ARINC word stream */
    arinc429_word_t aw = { .label = 0x12u, .sdi = 1u, .data = 0x12345u, .ssm = 2u };

    while (1)
    {
        /* ===== Generic telemetry frame =====
           [0x55][ctr(4)][payload(8)][crc8(1)]
        */
        uint8_t pkt[1 + 4 + 8 + 1];
        pkt[0] = 0x55u;
        memcpy(&pkt[1], &ctr, 4);
        for (int i = 0; i < 8; i++)
            pkt[5 + i] = (uint8_t)(ctr + (uint32_t)i);
        pkt[13] = crc8(&pkt[1], 4 + 8);

        /* FI: optional SRAM bit flips */
        FI_POINT(FI_F_MEM_BITFLIP, FI_BitFlipRange(pkt, sizeof(pkt), 9));

        /* self-check: refuse transmit if CRC does not match */
        if (crc8(&pkt[1], 4 + 8) != pkt[13])
        {
            PRINTF("TX: CRC mismatch -> refuse telemetry (ctr=%u)\r\n", (unsigned)ctr);
        }
        else
        {
            int retries = 0;
            while (1)
            {
                status_t st = UART_FI_WriteBlocking(LINK_UART, pkt, sizeof(pkt));
                if (st == kStatus_Success)
                    break;
                if (st == kStatus_LPUART_TxBusy)
                {
                    PRINTF("TX: UART busy, retry %d/%d\r\n", retries + 1, MAX_BUSY_RETRY);
                    retries++;
                    if (retries >= MAX_BUSY_RETRY)
                        safe_mode_blink_forever();
                    continue;
                }
                PRINTF("TX: UART error %d\r\n", (int)st);
                break;
            }
        }

        /* ===== Avionics-like ARINC frame ===== */
        (void)ARINC429_SendWord(LINK_UART, &aw);

        ctr++;
        SDK_DelayAtLeastUs(10000u, SystemCoreClock); /* 10 ms */
    }
}
