#include "fsl_device_registers.h"
#include "fsl_debug_console.h"
#include "board.h"
#include "app.h"

#include "fsl_common.h"
#include "fsl_lpuart.h"

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
                c ^= 0x8Cu;
            b >>= 1;
        }
    }
    return c;
}

static bool arinc_odd_parity_ok(uint32_t w)
{
    /* overall odd parity check (including bit31 parity bit) */
    uint32_t x = w;
    x ^= x >> 16;
    x ^= x >> 8;
    x ^= x >> 4;
    x &= 0xFu;
    uint8_t parity32 = (uint8_t)((0x6996u >> x) & 1u);
    return (parity32 == 1u);
}

int main(void)
{
    BOARD_InitHardware();
    LinkUart_Init115200();

    PRINTF("\r\nRX analyzer start (listening on LPUART3)\r\n");

    uint32_t ok_tlm = 0, bad_tlm_crc = 0;
    uint32_t ok_arinc = 0, bad_arinc_crc = 0, bad_arinc_parity = 0, bad_arinc_label = 0;

    while (1)
    {
        uint8_t sof;
        (void)LPUART_ReadBlocking(LINK_UART, &sof, 1);

        if (sof == 0x55u)
        {
            uint8_t body[4 + 8 + 1];
            (void)LPUART_ReadBlocking(LINK_UART, body, sizeof(body));
            uint8_t c = crc8(&body[0], 4 + 8);
            if (c != body[12])
                bad_tlm_crc++;
            else
                ok_tlm++;
        }
        else if (sof == 0xA5u)
        {
            uint8_t body[4 + 1];
            (void)LPUART_ReadBlocking(LINK_UART, body, sizeof(body));

            uint8_t c = crc8(&body[0], 4);
            if (c != body[4])
            {
                bad_arinc_crc++;
            }
            else
            {
                uint32_t w = ((uint32_t)body[0]) |
                             ((uint32_t)body[1] << 8) |
                             ((uint32_t)body[2] << 16) |
                             ((uint32_t)body[3] << 24);

                uint8_t label = (uint8_t)(w & 0xFFu);

                if (label != 0x12u)
                    bad_arinc_label++;
                else if (!arinc_odd_parity_ok(w))
                    bad_arinc_parity++;
                else
                    ok_arinc++;
            }
        }

        /* Print a snapshot every ~500 frames */
        if (((ok_tlm + bad_tlm_crc + ok_arinc + bad_arinc_crc + bad_arinc_parity + bad_arinc_label) % 500u) == 0u)
        {
            PRINTF("TLM ok=%u crc=%u | ARINC ok=%u crc=%u parity=%u label=%u\r\n",
                   (unsigned)ok_tlm, (unsigned)bad_tlm_crc,
                   (unsigned)ok_arinc, (unsigned)bad_arinc_crc,
                   (unsigned)bad_arinc_parity, (unsigned)bad_arinc_label);
        }
    }
}
