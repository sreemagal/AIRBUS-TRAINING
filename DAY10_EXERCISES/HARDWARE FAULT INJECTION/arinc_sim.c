#include "arinc_sim.h"

#include "fsl_debug_console.h"
#include "fsl_lpuart.h"
#include "fsl_common.h"
#include "board.h"
#include "board_uart3_pins.h"

#define ARINC_UART            LPUART3
#define ARINC_UART_CLK_FREQ   BOARD_DebugConsoleSrcFreq()

#define LINE_MAX              96

static volatile uint32_t g_msTicks;

void SysTick_Handler(void)
{
    g_msTicks++;
}

static void timebase_init_1ms(void)
{
    g_msTicks = 0;
    (void)SysTick_Config(SystemCoreClock / 1000U);
}

static uint32_t now_ms(void)
{
    return g_msTicks;
}

static void uart3_write_str(const char *s)
{
    while (*s)
    {
        LPUART_WriteByte(ARINC_UART, (uint8_t)*s++);
        while ((LPUART_GetStatusFlags(ARINC_UART) & kLPUART_TxDataRegEmptyFlag) == 0U) { }
    }
}

static bool uart3_read_line(char *out, uint32_t outMax, uint32_t timeoutMs)
{
    uint32_t idx = 0;
    const uint32_t t0 = now_ms();

    while ((now_ms() - t0) < timeoutMs)
    {
        if ((LPUART_GetStatusFlags(ARINC_UART) & kLPUART_RxDataRegFullFlag) != 0U)
        {
            const char ch = (char)LPUART_ReadByte(ARINC_UART);

            if ((ch == '\n') || (ch == '\r'))
            {
                if (idx == 0U)
                {
                    continue;
                }
                out[idx] = '\0';
                return true;
            }

            if (idx + 1U < outMax)
            {
                out[idx++] = ch;
            }
        }
    }

    return false;
}

void ArincSim_InitUart3(uint32_t baudrate)
{
    BOARD_InitUart3Pins();

    lpuart_config_t cfg;
    LPUART_GetDefaultConfig(&cfg);
    cfg.baudRate_Bps = baudrate;
    cfg.enableTx     = true;
    cfg.enableRx     = true;

    LPUART_Init(ARINC_UART, &cfg, ARINC_UART_CLK_FREQ);

    timebase_init_1ms();
}

static bool bridge_wait_ready(uint32_t bootCount, uint32_t flags)
{
    char line[LINE_MAX];
    char hello[LINE_MAX];

    (void)snprintf(hello, sizeof(hello), "HELLO %lu %08lx\r\n",
                   (unsigned long)bootCount, (unsigned long)flags);

    for (int attempt = 0; attempt < 10; attempt++)
    {
        uart3_write_str(hello);
        if (uart3_read_line(line, sizeof(line), 200U))
        {
            if (strcmp(line, "READY") == 0)
            {
                return true;
            }
        }
        SDK_DelayAtLeastUs(100000ULL, SystemCoreClock); /* 100 ms */
    }

    return false;
}

void ArincSim_RunRdc(uint32_t bootCount, uint32_t lastResetFlags)
{
    PRINTF("\r\n[ARINC-SIM:RDC] Init UART3 on Arduino pins.\r\n");
    ArincSim_InitUart3(115200U);

    PRINTF("[ARINC-SIM:RDC] Handshaking...\r\n");
    if (!bridge_wait_ready(bootCount, lastResetFlags))
    {
        PRINTF("[ARINC-SIM:RDC] ERROR: Bridge not READY. Check wiring + that the other board runs BRIDGE build.\r\n");
        return;
    }

    PRINTF("[ARINC-SIM:RDC] READY received. Starting scheduled label stream.\r\n");
    PRINTF("[ARINC-SIM:RDC] Tip: press SW4 (Reset) or SW3 (POR reset) to simulate a fault. After reboot, RDC re-handshakes.\r\n");

    uint32_t seq = 0;
    uint32_t lastTxMs = now_ms();

    while (1)
    {
        const uint32_t label = 0x123U; /* placeholder label */
        const uint32_t data  = (seq & 0xFFFFU);

        char tx[LINE_MAX];
        (void)snprintf(tx, sizeof(tx), "TX %lu %03lx %04lx\r\n",
                       (unsigned long)seq, (unsigned long)label, (unsigned long)data);

        uart3_write_str(tx);

        /* Optional: wait for ACK briefly (not strictly required for ARINC, but helps the lab). */
        char line[LINE_MAX];
        if (uart3_read_line(line, sizeof(line), 50U))
        {
            /* expected: ACK <seq> */
        }

        /* Fixed schedule: send every 50 ms (20 Hz) */
        const uint32_t now = now_ms();
        const uint32_t elapsed = now - lastTxMs;
        if (elapsed < 50U)
        {
            SDK_DelayAtLeastUs((uint64_t)(50U - elapsed) * 1000ULL, SystemCoreClock);
        }
        lastTxMs = now_ms();

        seq++;
    }
}

void ArincSim_RunBridge(void)
{
    PRINTF("\r\n[ARINC-SIM:BRIDGE] Init UART3 on Arduino pins.\r\n");
    ArincSim_InitUart3(115200U);

    PRINTF("[ARINC-SIM:BRIDGE] Waiting for HELLO...\r\n");

    uint32_t lastWordMs = now_ms();
    uint32_t missedCount = 0;

    while (1)
    {
        char line[LINE_MAX];
        if (!uart3_read_line(line, sizeof(line), 200U))
        {
            /* No line within 200ms -> treat as a gap */
            missedCount++;
            if (missedCount == 3U)
            {
                PRINTF("[ARINC-SIM:BRIDGE] WARNING: intermittent source (3 consecutive gaps).\r\n");
            }
            continue;
        }

        /* Got a line; reset gap counter */
        missedCount = 0;

        /* Measure timing gaps */
        const uint32_t now = now_ms();
        const uint32_t gap = now - lastWordMs;
        lastWordMs = now;

        if (gap > 120U)
        {
            PRINTF("[ARINC-SIM:BRIDGE] Gap detected: %lu ms\r\n", (unsigned long)gap);
        }

        if (strncmp(line, "HELLO ", 6) == 0)
        {
            PRINTF("[ARINC-SIM:BRIDGE] %s\r\n", line);
            uart3_write_str("READY\r\n");
            continue;
        }

        if (strncmp(line, "TX ", 3) == 0)
        {
            /* Parse TX <seq> <label> <data> (best-effort) */
            unsigned long seq = 0, label = 0, data = 0;
            (void)sscanf(line, "TX %lu %lx %lx", &seq, &label, &data);

            /* Acknowledge */
            char ack[LINE_MAX];
            (void)snprintf(ack, sizeof(ack), "ACK %lu\r\n", seq);
            uart3_write_str(ack);
            continue;
        }

        /* Unknown line; ignore but print for visibility */
        PRINTF("[ARINC-SIM:BRIDGE] RX: %s\r\n", line);
    }
}
