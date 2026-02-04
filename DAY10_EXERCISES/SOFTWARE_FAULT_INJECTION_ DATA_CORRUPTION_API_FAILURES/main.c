#include <string.h>
#include <stdlib.h>

#include "board.h"
#include "app.h"
#include "fsl_debug_console.h"
#include "fsl_gpio.h"
#include "fsl_pit.h"

#include "fi.h"
#include "a429_frame.h"
#include "data_uart.h"

/* -------------------------------
 * User LED macros come from board.h in the imported example.
 * ------------------------------- */

/* -------------------------------
 * Simple ACK on data UART to exercise TX path
 * ------------------------------- */
#define ACK_SYNC 0xACU

typedef enum
{
    ACK_OK = 0,
    ACK_BAD_CHK = 1,
    ACK_BAD_PARITY = 2,
    ACK_BAD_PLAUS = 3,
    ACK_BAD_LEN = 4,
    ACK_TX_DROP = 5,
} ack_reason_t;

typedef struct
{
    uint8_t b[2];
    uint8_t len;
} ack_msg_t;

#define ACK_Q_DEPTH 32
static ack_msg_t g_ackQ[ACK_Q_DEPTH];
static volatile uint32_t g_ackWr = 0, g_ackRd = 0;

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

static bool AckQ_Push(ack_reason_t r)
{
    uint32_t wr = g_ackWr;
    uint32_t rd = g_ackRd;
    if ((wr - rd) >= ACK_Q_DEPTH)
    {
        return false;
    }

    ack_msg_t *m = &g_ackQ[wr % ACK_Q_DEPTH];
    m->b[0] = ACK_SYNC;
    m->b[1] = (uint8_t)r;
    m->len = 2;

    g_ackWr = wr + 1;
    return true;
}

static bool AckQ_Peek(ack_msg_t *out)
{
    uint32_t rd = g_ackRd;
    if (rd == g_ackWr) return false;
    *out = g_ackQ[rd % ACK_Q_DEPTH];
    return true;
}

static void AckQ_Pop(void)
{
    if (g_ackRd != g_ackWr) g_ackRd++;
}

/* -------------------------------
 * Timebase via PIT
 * ------------------------------- */
static volatile uint32_t g_ms = 0;

void PIT_IRQ_HANDLER(void)
{
    if (PIT_GetStatusFlags(DEMO_PIT_BASEADDR, DEMO_PIT_CHANNEL) & kPIT_TimerFlag)
    {
        PIT_ClearStatusFlags(DEMO_PIT_BASEADDR, DEMO_PIT_CHANNEL, kPIT_TimerFlag);
        g_ms += 10U;
        FI_SetNowMs(g_ms);
    }
    SDK_ISR_EXIT_BARRIER;
}

static void Pit10ms_Init(void)
{
    pit_config_t pitConfig;
    PIT_GetDefaultConfig(&pitConfig);
    PIT_Init(DEMO_PIT_BASEADDR, &pitConfig);

    /* 10 ms tick */
    PIT_SetTimerPeriod(DEMO_PIT_BASEADDR, DEMO_PIT_CHANNEL, USEC_TO_COUNT(10000U, PIT_SOURCE_CLOCK));
    PIT_EnableInterrupts(DEMO_PIT_BASEADDR, DEMO_PIT_CHANNEL, kPIT_TimerInterruptEnable);
    EnableIRQ(PIT_IRQ_ID);
    PIT_StartTimer(DEMO_PIT_BASEADDR, DEMO_PIT_CHANNEL);
}

/* -------------------------------
 * Plausibility barrier
 * ------------------------------- */
static uint8_t g_allowLabel[256];
static uint32_t g_lastLabelMs[256];
static const uint32_t g_minLabelIntervalMs = 20U; /* 50 Hz */

static void Plausibility_Init(void)
{
    memset(g_allowLabel, 0, sizeof(g_allowLabel));
    memset(g_lastLabelMs, 0, sizeof(g_lastLabelMs));

    /* Allow-list example: change to your real labels. */
    g_allowLabel[0x01] = 1;
    g_allowLabel[0x02] = 1;
    g_allowLabel[0x03] = 1;
    g_allowLabel[0x04] = 1;
}

static bool Plausibility_Accept(uint8_t label)
{
    if (!g_allowLabel[label]) return false;

    uint32_t now = g_ms;
    uint32_t last = g_lastLabelMs[label];
    if ((now - last) < g_minLabelIntervalMs)
    {
        return false;
    }
    g_lastLabelMs[label] = now;
    return true;
}

/* -------------------------------
 * Simple CLI (non-blocking) on debug console
 * Commands:
 *   fi dump
 *   fi arm rx every <N> bits <M>
 *   fi arm txbusy every <N>
 *   fi arm txstall window <S> <E>
 *   fi arm label every <N>
 *   fi arm parity every <N>
 *   fi off
 * ------------------------------- */

static char g_cliLine[96];
static uint32_t g_cliLen = 0;

static void cli_print_cfg(void)
{
#if SFI_ENABLED
    PRINTF("\r\nSFI sites:\r\n");
    for (int i = 0; i < (int)FI_SITE_COUNT; i++)
    {
        fi_site_cfg_t *c = FI_SiteCfg((fi_site_t)i);
        if (!c) continue;
        PRINTF("  site %d: policy=%d hit=%lu fire=%lu every=%lu win=[%lu,%lu) bits=%u\r\n",
               i, (int)c->policy, c->hit_count, c->fire_count, c->every_n, c->window_start, c->window_end, c->bits_to_flip);
    }
#else
    PRINTF("\r\nSFI_DISABLED build.\r\n");
#endif
}

static void cli_handle_line(const char *line)
{
    /* Tokenize in-place copy */
    char buf[96];
    strncpy(buf, line, sizeof(buf) - 1U);
    buf[sizeof(buf) - 1U] = '\0';

    char *argv[10] = {0};
    int argc = 0;
    char *tok = strtok(buf, " \t\r\n");
    while (tok && argc < 10)
    {
        argv[argc++] = tok;
        tok = strtok(NULL, " \t\r\n");
    }
    if (argc == 0) return;

    if (strcmp(argv[0], "fi") != 0)
    {
        PRINTF("\r\nUnknown. Try: fi dump | fi arm ... | fi off\r\n");
        return;
    }

    if (argc >= 2 && strcmp(argv[1], "dump") == 0)
    {
        /* Dump a short snapshot of site counters; event log dump kept minimal */
        cli_print_cfg();
        return;
    }

    if (argc >= 2 && strcmp(argv[1], "off") == 0)
    {
#if SFI_ENABLED
        FI_DisableAll();
        PRINTF("\r\nSFI disabled (all sites).\r\n");
#endif
        return;
    }

    if (argc >= 3 && strcmp(argv[1], "arm") == 0)
    {
#if SFI_ENABLED
        fi_site_t site = FI_SITE_COUNT;
        if (strcmp(argv[2], "rx") == 0) site = FI_SITE_RX_CORRUPT;
        else if (strcmp(argv[2], "txbusy") == 0) site = FI_SITE_TX_API_FAIL;
        else if (strcmp(argv[2], "txstall") == 0) site = FI_SITE_TX_STALL;
        else if (strcmp(argv[2], "label") == 0) site = FI_SITE_A429_LABEL_TAMPER;
        else if (strcmp(argv[2], "parity") == 0) site = FI_SITE_A429_PARITY_TAMPER;

        if (site == FI_SITE_COUNT)
        {
            PRINTF("\r\nUnknown site. Use: rx|txbusy|txstall|label|parity\r\n");
            return;
        }

        fi_site_cfg_t *c = FI_SiteCfg(site);
        if (!c) return;

        if (argc >= 5 && strcmp(argv[3], "every") == 0)
        {
            c->policy = FI_POLICY_EVERY_N;
            c->every_n = (uint32_t)strtoul(argv[4], NULL, 0);
            if (site == FI_SITE_RX_CORRUPT && argc >= 7 && strcmp(argv[5], "bits") == 0)
            {
                c->bits_to_flip = (uint8_t)strtoul(argv[6], NULL, 0);
            }
            PRINTF("\r\nArmed site %d EVERY %lu\r\n", (int)site, c->every_n);
            return;
        }
        if (argc >= 6 && strcmp(argv[3], "window") == 0)
        {
            c->policy = FI_POLICY_WINDOW;
            c->window_start = (uint32_t)strtoul(argv[4], NULL, 0);
            c->window_end   = (uint32_t)strtoul(argv[5], NULL, 0);
            PRINTF("\r\nArmed site %d WINDOW [%lu,%lu)\r\n", (int)site, c->window_start, c->window_end);
            return;
        }

        PRINTF("\r\nUsage: fi arm <site> every <N> [bits <M>] | fi arm txstall window <S> <E>\r\n");
#else
        PRINTF("\r\nSFI_DISABLED build.\r\n");
#endif
        return;
    }

    PRINTF("\r\nUsage: fi dump | fi arm ... | fi off\r\n");
}

static void cli_poll(void)
{
    char ch;
    if (Console_TryReadChar(&ch) != kStatus_Success)
    {
        return;
    }

    if (ch == '\r' || ch == '\n')
    {
        if (g_cliLen > 0)
        {
            g_cliLine[g_cliLen] = '\0';
            cli_handle_line(g_cliLine);
            g_cliLen = 0;
        }
        return;
    }

    if (g_cliLen < (sizeof(g_cliLine) - 1U))
    {
        g_cliLine[g_cliLen++] = ch;
    }
}

/* -------------------------------
 * Main
 * ------------------------------- */

static data_uart_t g_dataUart;
static a429_uart_parser_t g_parser;

/* Counters */
static uint32_t rx_ok = 0;
static uint32_t rx_bad_chk = 0;
static uint32_t rx_bad_parity = 0;
static uint32_t rx_bad_plaus = 0;
static uint32_t rx_bad_len = 0;

static uint32_t tx_retries = 0;
static uint32_t tx_failures = 0;
static uint32_t tx_recoveries = 0;
static uint32_t tx_drops = 0;

/* TX stall monitor */
static uint32_t g_tx_last_progress_ms = 0;
static uint32_t g_tx_last_send_count = 0;
static uint32_t g_tx_last_checked_ms = 0;

static void telemetry_print_1s(void)
{
    static uint32_t last = 0;
    if ((g_ms - last) >= 1000U)
    {
        last = g_ms;
        PRINTF("\r\n[%lu ms] rx_ok=%lu bad_chk=%lu bad_parity=%lu bad_plaus=%lu bad_len=%lu | tx_retry=%lu tx_fail=%lu tx_recov=%lu tx_drop=%lu\r\n",
               g_ms, rx_ok, rx_bad_chk, rx_bad_parity, rx_bad_plaus, rx_bad_len,
               tx_retries, tx_failures, tx_recoveries, tx_drops);
    }
}

static void tx_stall_monitor_10ms(void)
{
    /* Run at ~10ms cadence; do not call LPUART APIs inside PIT ISR. */
    if ((g_ms - g_tx_last_checked_ms) < 10U) return;
    g_tx_last_checked_ms = g_ms;

    if (!g_dataUart.txOnGoing) return;

    uint32_t cnt = 0;
    if (DataUart_GetSendCount(&g_dataUart, &cnt) == kStatus_Success)
    {
        if (cnt != g_tx_last_send_count)
        {
            g_tx_last_send_count = cnt;
            g_tx_last_progress_ms = g_ms;
        }
        else
        {
            /* No progress */
            if ((g_ms - g_tx_last_progress_ms) > 100U)
            {
                /* Stall detected */
                tx_recoveries++;
                PRINTF("\r\nTX stall detected at %lu ms (send_count=%lu). Aborting and retrying.\r\n", g_ms, cnt);

                DataUart_AbortSend(&g_dataUart);

                /* Keep pending ACK in queue; next main-loop iteration will resend. */
                g_tx_last_progress_ms = g_ms;
                g_tx_last_send_count = 0;
            }
        }
    }
}

int main(void)
{
    BOARD_InitHardware();

    USER_LED_INIT(LOGIC_LED_OFF);

#if SFI_ENABLED
    PRINTF("\r\n=== SFI BUILD: Data Corruption + API Failures (Deterministic) ===\r\n");
#else
    PRINTF("\r\n=== CLEAN BUILD (SFI disabled) ===\r\n");
#endif

    FI_Init(0xC0FFEE01U);
    A429_ParserInit(&g_parser);
    Plausibility_Init();

    Pit10ms_Init();

    /* Init data UART (ADK/HI-8582 link) */
    DataUart_Init(&g_dataUart, DATA_LPUART, DATA_LPUART_CLK_FREQ, DATA_LPUART_BAUDRATE);
    EnableIRQ(DATA_LPUART_IRQn);

    /* Prime RX */
    (void)DataUart_StartReceive1(&g_dataUart);

    PRINTF("\r\nCLI: fi dump | fi arm rx every <N> bits <M> | fi arm txbusy every <N> | fi arm txstall window <S> <E> | fi arm label every <N> | fi arm parity every <N> | fi off\r\n");

    uint32_t last_good_word = 0;
    bool last_good_valid = false;

    while (1)
    {
        /* --- CLI --- */
        cli_poll();

        /* --- RX pump --- */
        if (!g_dataUart.rxOnGoing && !g_dataUart.rxByteReady)
        {
            (void)DataUart_StartReceive1(&g_dataUart);
        }

        if (g_dataUart.rxByteReady)
        {
            uint8_t b = g_dataUart.rxByte;
            g_dataUart.rxByteReady = false;

#if SFI_ENABLED
            /* RX corruption injection point ("before" app ring buffer / parser) */
            {
                fi_site_cfg_t *cfg = FI_SiteCfg(FI_SITE_RX_CORRUPT);
                bool fire = FI_ShouldFire(FI_SITE_RX_CORRUPT);
                if (cfg)
                {
                    FI_Log(FI_SITE_RX_CORRUPT, fire, cfg->hit_count, (uint32_t)b, (uint32_t)b);
                }
                if (fire)
                {
                    USER_LED_TOGGLE();
                    b = FI_CorruptByteDeterministic(b);
                }
            }
#endif

            uint32_t word = 0;
            a429_parse_result_t pr = A429_ParserFeed(&g_parser, b, &word);

            if (pr == A429_PARSE_BAD_LEN)
            {
                rx_bad_len++;
                (void)AckQ_Push(ACK_BAD_LEN);
            }
            else if (pr == A429_PARSE_BAD_CHK)
            {
                rx_bad_chk++;
                USER_LED_ON(); /* solid indicates error observed */
                (void)AckQ_Push(ACK_BAD_CHK);
            }
            else if (pr == A429_PARSE_WORD_OK)
            {
                /* Optional post-parse tampers */
#if SFI_ENABLED
                {
                    fi_site_cfg_t *cfg = FI_SiteCfg(FI_SITE_A429_LABEL_TAMPER);
                    bool fire = FI_ShouldFire(FI_SITE_A429_LABEL_TAMPER);
                    if (cfg) FI_Log(FI_SITE_A429_LABEL_TAMPER, fire, cfg->hit_count, 0, word);
                    if (fire)
                    {
                        /* Flip MSB of label to push it out of allow-list deterministically. */
                        word ^= 0x00000080U;
                        USER_LED_TOGGLE();
                    }
                }
                {
                    fi_site_cfg_t *cfg = FI_SiteCfg(FI_SITE_A429_PARITY_TAMPER);
                    bool fire = FI_ShouldFire(FI_SITE_A429_PARITY_TAMPER);
                    if (cfg) FI_Log(FI_SITE_A429_PARITY_TAMPER, fire, cfg->hit_count, 0, word);
                    if (fire)
                    {
                        /* Flip parity bit (bit31). */
                        word ^= 0x80000000U;
                        USER_LED_TOGGLE();
                    }
                }
#endif

                /* Barrier 2: odd parity */
                if (!A429_CheckOddParity(word))
                {
                    rx_bad_parity++;
                    USER_LED_ON();
                    (void)AckQ_Push(ACK_BAD_PARITY);
                }
                else
                {
                    /* Barrier 3: plausibility */
                    uint8_t label = A429_Label(word);
                    if (!Plausibility_Accept(label))
                    {
                        rx_bad_plaus++;
                        USER_LED_ON();
                        (void)AckQ_Push(ACK_BAD_PLAUS);
                    }
                    else
                    {
                        rx_ok++;
                        last_good_word = word;
                        last_good_valid = true;
                        USER_LED_OFF();
                        (void)AckQ_Push(ACK_OK);
                    }
                }
            }
        }

        /* --- TX path (exercise API failures + stall recovery) --- */
        if (!g_dataUart.txOnGoing)
        {
            ack_msg_t m;
            if (AckQ_Peek(&m))
            {
                status_t st = DataUart_SendNonBlocking_FI(&g_dataUart, m.b, m.len);
                if (st == kStatus_Success)
                {
                    AckQ_Pop();
                    g_tx_last_progress_ms = g_ms;
                    g_tx_last_send_count = 0;
                }
                else if (st == kStatus_LPUART_TxBusy)
                {
                    /* Retry later */
                    tx_retries++;
                    g_dataUart.txOnGoing = false; /* because we did not actually start the driver */
                }
                else
                {
                    /* Unexpected TX failure; drop this ACK */
                    tx_failures++;
                    AckQ_Pop();
                }
            }
        }
        else
        {
            /* Maintain a moving 'progress' baseline even for normal transfers */
            if (g_tx_last_progress_ms == 0U) g_tx_last_progress_ms = g_ms;
        }

        /* If ACK queue overflowed, record and (optionally) tell peer */
        if ((g_ackWr - g_ackRd) >= ACK_Q_DEPTH)
        {
            if (AckQ_Push(ACK_TX_DROP) == false)
            {
                tx_drops++;
            }
        }

        /* --- Stall monitor + telemetry --- */
        tx_stall_monitor_10ms();
        telemetry_print_1s();

        /* Optional: print last-known-good word occasionally */
        if (last_good_valid)
        {
            /* keep silent by default; uncomment for debugging
             * PRINTF("Last good ARINC word: 0x%08lX\r\n", last_good_word);
             */
        }
    }
}
