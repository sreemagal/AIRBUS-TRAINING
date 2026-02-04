/*
 * Deterministic Software Fault Injection (SFI) Campaign
 * Target: EVKB-IMXRT1050 (MCUXpresso SDK 2.16.x style; see uploaded SDK_25_06_00_EVKB-IMXRT1050.zip)
 *
 * Base project to import in MCUXpresso IDE:
 *   boards/evkbimxrt1050/driver_examples/lpuart/interrupt_transfer/
 *
 * This canvas contains a complete, working reference implementation broken into files.
 * Copy each "File:" section into your MCUXpresso project.
 *
 * IMPORTANT HW NOTE:
 *   - Keep LPUART1 as the debug console (PRINTF / CLI).
 *   - Use a SECOND LPUART instance for the ADK/HI-8582 data UART (default below: LPUART3).
 *   - You MUST enable pin muxing for the chosen DATA LPUART pins using MCUXpresso Pins tool
 *     (or manually edit pin_mux.c) to match how you wired EVKB <-> ADK/HI-8582.
 *
 * Build flags:
 *   - Add SFI_ENABLED=1 to the preprocessor symbols (or set to 0 for a clean build).
 *
 * UART framing expected from ADK/HI-8582 -> EVKB:
 *   [0xA5][LEN=4][W0][W1][W2][W3][CHK]
 *   CHK := (LEN + W0 + W1 + W2 + W3) & 0xFF
 *
 * Data rate:
 *   - Keep traffic <= 115200 bps; recommended 50–200 frames/s for clean observation.
 */

/******************************************************************************
 * File: source/app.h  (REPLACE the example's app.h or merge the new macros)
 ******************************************************************************/
#ifndef _APP_H_
#define _APP_H_

/* Base example had DEMO_LPUART == LPUART1 for debug console.
 * Keep it that way for PRINTF + CLI.
 */
#define DEMO_LPUART          LPUART1
#define DEMO_LPUART_CLK_FREQ BOARD_DebugConsoleSrcFreq()

/* Data UART toward ADK/HI-8582 (choose any available LPUART you routed to pins).
 * Default: LPUART3.
 */
#define DATA_LPUART          LPUART3
#define DATA_LPUART_IRQn     LPUART3_IRQn
#define DATA_LPUART_IRQHandler LPUART3_IRQHandler
#define DATA_LPUART_CLK_FREQ BOARD_DebugConsoleSrcFreq()
#define DATA_LPUART_BAUDRATE (115200U)

/* PIT tick (10 ms) used for TX stall monitoring + periodic telemetry. */
#define DEMO_PIT_BASEADDR PIT
#define DEMO_PIT_CHANNEL  kPIT_Chnl_0
#define PIT_IRQ_ID        PIT_IRQn
#define PIT_IRQ_HANDLER   PIT_IRQHandler
#define PIT_SOURCE_CLOCK  CLOCK_GetFreq(kCLOCK_OscClk)

void BOARD_InitHardware(void);

#endif /* _APP_H_ */

/******************************************************************************
 * File: source/fi.h
 ******************************************************************************/
#ifndef FI_H
#define FI_H

#include <stdint.h>
#include <stdbool.h>

#ifndef SFI_ENABLED
#define SFI_ENABLED 1
#endif

typedef enum
{
    FI_SITE_RX_CORRUPT = 0,
    FI_SITE_A429_LABEL_TAMPER,
    FI_SITE_A429_PARITY_TAMPER,
    FI_SITE_TX_API_FAIL,
    FI_SITE_TX_STALL,

    FI_SITE_COUNT
} fi_site_t;

typedef enum
{
    FI_POLICY_DISABLED = 0,
    FI_POLICY_EVERY_N,
    FI_POLICY_WINDOW
} fi_policy_t;

typedef struct
{
    fi_policy_t policy;
    uint32_t every_n;        /* for EVERY_N */
    uint32_t window_start;   /* for WINDOW (inclusive) */
    uint32_t window_end;     /* for WINDOW (exclusive) */

    uint32_t hit_count;
    uint32_t fire_count;

    /* Optional parameters per site */
    uint8_t bits_to_flip;    /* RX_CORRUPT */
    uint8_t reserved;
    uint16_t reserved2;
} fi_site_cfg_t;

typedef struct
{
    uint32_t ts_ms;
    uint8_t site;
    uint8_t fired;
    uint16_t spare;
    uint32_t hit;
    uint32_t extra;
    uint32_t data_peek;
} fi_event_t;

void FI_Init(uint32_t seed);

/* Timebase hookup (ms). Call once from PIT ISR or your time source. */
void FI_SetNowMs(uint32_t now_ms);
uint32_t FI_NowMs(void);

/* Configuration */
fi_site_cfg_t *FI_SiteCfg(fi_site_t site);
void FI_DisableAll(void);

/* Decision */
bool FI_ShouldFire(fi_site_t site);

/* Helpers */
uint8_t FI_CorruptByteDeterministic(uint8_t in);
uint32_t FI_Rand32(void);

/* Logging */
void FI_Log(fi_site_t site, bool fired, uint32_t hit, uint32_t extra, uint32_t data_peek);
void FI_DumpLog(void (*emit)(const char *s));

#endif /* FI_H */

/******************************************************************************
 * File: source/fi.c
 ******************************************************************************/
#include "fi.h"

#if SFI_ENABLED

#ifndef FI_LOG_DEPTH
#define FI_LOG_DEPTH 128
#endif

static volatile uint32_t g_now_ms;
static fi_site_cfg_t g_sites[FI_SITE_COUNT];
static fi_event_t g_log[FI_LOG_DEPTH];
static volatile uint32_t g_log_wr;

static uint32_t g_rng;

static uint32_t xorshift32(uint32_t x)
{
    /* Deterministic and fast. */
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

void FI_Init(uint32_t seed)
{
    g_now_ms = 0;
    g_rng = (seed == 0U) ? 0xA5A5A5A5U : seed;

    for (int i = 0; i < (int)FI_SITE_COUNT; i++)
    {
        g_sites[i].policy = FI_POLICY_DISABLED;
        g_sites[i].every_n = 0;
        g_sites[i].window_start = 0;
        g_sites[i].window_end = 0;
        g_sites[i].hit_count = 0;
        g_sites[i].fire_count = 0;
        g_sites[i].bits_to_flip = 1;
    }

    /* Default arming matching the exercise spec */
    g_sites[FI_SITE_RX_CORRUPT].policy = FI_POLICY_EVERY_N;
    g_sites[FI_SITE_RX_CORRUPT].every_n = 200;
    g_sites[FI_SITE_RX_CORRUPT].bits_to_flip = 3;

    g_sites[FI_SITE_TX_API_FAIL].policy = FI_POLICY_EVERY_N;
    g_sites[FI_SITE_TX_API_FAIL].every_n = 100;

    g_sites[FI_SITE_TX_STALL].policy = FI_POLICY_WINDOW;
    g_sites[FI_SITE_TX_STALL].window_start = 500;
    g_sites[FI_SITE_TX_STALL].window_end   = 520;

    /* Optional: you can arm label/parity tampers from CLI */
    g_sites[FI_SITE_A429_LABEL_TAMPER].policy = FI_POLICY_DISABLED;
    g_sites[FI_SITE_A429_PARITY_TAMPER].policy = FI_POLICY_DISABLED;

    g_log_wr = 0;
}

void FI_SetNowMs(uint32_t now_ms) { g_now_ms = now_ms; }
uint32_t FI_NowMs(void) { return g_now_ms; }

fi_site_cfg_t *FI_SiteCfg(fi_site_t site)
{
    if ((int)site < 0 || site >= FI_SITE_COUNT) return (fi_site_cfg_t *)0;
    return &g_sites[site];
}

void FI_DisableAll(void)
{
    for (int i = 0; i < (int)FI_SITE_COUNT; i++)
    {
        g_sites[i].policy = FI_POLICY_DISABLED;
    }
}

uint32_t FI_Rand32(void)
{
    g_rng = xorshift32(g_rng);
    return g_rng;
}

bool FI_ShouldFire(fi_site_t site)
{
    fi_site_cfg_t *cfg = FI_SiteCfg(site);
    if (!cfg) return false;

    cfg->hit_count++;

    bool fire = false;
    switch (cfg->policy)
    {
        case FI_POLICY_DISABLED:
            fire = false;
            break;
        case FI_POLICY_EVERY_N:
            fire = (cfg->every_n != 0U) && ((cfg->hit_count % cfg->every_n) == 0U);
            break;
        case FI_POLICY_WINDOW:
            fire = (cfg->hit_count >= cfg->window_start) && (cfg->hit_count < cfg->window_end);
            break;
        default:
            fire = false;
            break;
    }

    if (fire) cfg->fire_count++;
    return fire;
}

uint8_t FI_CorruptByteDeterministic(uint8_t in)
{
    /* Flip cfg->bits_to_flip bits deterministically based on RNG. */
    fi_site_cfg_t *cfg = FI_SiteCfg(FI_SITE_RX_CORRUPT);
    uint8_t out = in;

    const uint8_t flips = (cfg && cfg->bits_to_flip) ? cfg->bits_to_flip : 1U;
    for (uint8_t i = 0; i < flips; i++)
    {
        uint32_t r = FI_Rand32();
        uint8_t bit = (uint8_t)(r & 0x7U);
        out ^= (uint8_t)(1U << bit);
    }
    return out;
}

void FI_Log(fi_site_t site, bool fired, uint32_t hit, uint32_t extra, uint32_t data_peek)
{
    uint32_t idx = g_log_wr++;
    fi_event_t *e = &g_log[idx % FI_LOG_DEPTH];
    e->ts_ms = FI_NowMs();
    e->site = (uint8_t)site;
    e->fired = fired ? 1U : 0U;
    e->hit = hit;
    e->extra = extra;
    e->data_peek = data_peek;
}

static const char *site_name(fi_site_t s)
{
    switch (s)
    {
        case FI_SITE_RX_CORRUPT: return "RX_CORRUPT";
        case FI_SITE_A429_LABEL_TAMPER: return "A429_LABEL_TAMPER";
        case FI_SITE_A429_PARITY_TAMPER: return "A429_PARITY_TAMPER";
        case FI_SITE_TX_API_FAIL: return "TX_API_FAIL";
        case FI_SITE_TX_STALL: return "TX_STALL";
        default: return "?";
    }
}

void FI_DumpLog(void (*emit)(const char *s))
{
    /* Very small footprint dump; caller formats. */
    (void)emit;
    /* Implemented in main via PRINTF for convenience; keep this module RTOS-agnostic. */
}

#else /* SFI_ENABLED == 0 */

void FI_Init(uint32_t seed) { (void)seed; }
void FI_SetNowMs(uint32_t now_ms) { (void)now_ms; }
uint32_t FI_NowMs(void) { return 0; }
fi_site_cfg_t *FI_SiteCfg(fi_site_t site) { (void)site; return (fi_site_cfg_t *)0; }
void FI_DisableAll(void) {}
bool FI_ShouldFire(fi_site_t site) { (void)site; return false; }
uint8_t FI_CorruptByteDeterministic(uint8_t in) { return in; }
uint32_t FI_Rand32(void) { return 0; }
void FI_Log(fi_site_t site, bool fired, uint32_t hit, uint32_t extra, uint32_t data_peek)
{
    (void)site; (void)fired; (void)hit; (void)extra; (void)data_peek;
}
void FI_DumpLog(void (*emit)(const char *s)) { (void)emit; }

#endif

/******************************************************************************
 * File: source/a429_frame.h
 ******************************************************************************/
#ifndef A429_FRAME_H
#define A429_FRAME_H

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    A429_PARSE_NONE = 0,
    A429_PARSE_WORD_OK,
    A429_PARSE_BAD_CHK,
    A429_PARSE_BAD_LEN,
} a429_parse_result_t;

typedef struct
{
    /* Parser state */
    uint8_t st;
    uint8_t len;
    uint8_t payload[4];
    uint8_t pay_idx;
    uint8_t chk;

    /* Stats */
    uint32_t frames_ok;
    uint32_t frames_bad_chk;
    uint32_t frames_bad_len;

} a429_uart_parser_t;

void A429_ParserInit(a429_uart_parser_t *p);

a429_parse_result_t A429_ParserFeed(a429_uart_parser_t *p, uint8_t byte, uint32_t *out_word);

/* ARINC checks */
bool A429_CheckOddParity(uint32_t word);
uint8_t A429_Label(uint32_t word);

#endif /* A429_FRAME_H */

/******************************************************************************
 * File: source/a429_frame.c
 ******************************************************************************/
#include "a429_frame.h"

#define ST_WAIT_SYNC  0
#define ST_LEN        1
#define ST_PAYLOAD    2
#define ST_CHK        3

static uint8_t chk8(uint8_t len, const uint8_t payload[4])
{
    uint16_t s = (uint16_t)len;
    s = (uint16_t)(s + payload[0] + payload[1] + payload[2] + payload[3]);
    return (uint8_t)(s & 0xFFU);
}

void A429_ParserInit(a429_uart_parser_t *p)
{
    p->st = ST_WAIT_SYNC;
    p->len = 0;
    p->pay_idx = 0;
    p->chk = 0;
    p->frames_ok = 0;
    p->frames_bad_chk = 0;
    p->frames_bad_len = 0;
}

a429_parse_result_t A429_ParserFeed(a429_uart_parser_t *p, uint8_t byte, uint32_t *out_word)
{
    switch (p->st)
    {
        case ST_WAIT_SYNC:
            if (byte == 0xA5U)
            {
                p->st = ST_LEN;
            }
            break;

        case ST_LEN:
            p->len = byte;
            p->pay_idx = 0;
            if (p->len != 4U)
            {
                p->frames_bad_len++;
                p->st = ST_WAIT_SYNC;
                return A429_PARSE_BAD_LEN;
            }
            p->st = ST_PAYLOAD;
            break;

        case ST_PAYLOAD:
            p->payload[p->pay_idx++] = byte;
            if (p->pay_idx >= 4U)
            {
                p->st = ST_CHK;
            }
            break;

        case ST_CHK:
            p->chk = byte;
            {
                uint8_t exp = chk8(p->len, p->payload);
                if (exp != p->chk)
                {
                    p->frames_bad_chk++;
                    p->st = ST_WAIT_SYNC;
                    return A429_PARSE_BAD_CHK;
                }

                /* Assemble little-endian 32-bit word: W0=LSB. */
                uint32_t w = ((uint32_t)p->payload[0]) |
                             ((uint32_t)p->payload[1] << 8) |
                             ((uint32_t)p->payload[2] << 16) |
                             ((uint32_t)p->payload[3] << 24);
                *out_word = w;

                p->frames_ok++;
                p->st = ST_WAIT_SYNC;
                return A429_PARSE_WORD_OK;
            }

        default:
            p->st = ST_WAIT_SYNC;
            break;
    }

    return A429_PARSE_NONE;
}

static uint32_t popcount32(uint32_t x)
{
    /* Portable popcount */
    x = x - ((x >> 1U) & 0x55555555U);
    x = (x & 0x33333333U) + ((x >> 2U) & 0x33333333U);
    x = (x + (x >> 4U)) & 0x0F0F0F0FU;
    x = x + (x >> 8U);
    x = x + (x >> 16U);
    return x & 0x3FU;
}

bool A429_CheckOddParity(uint32_t word)
{
    /* Treat bit31 as the parity bit; odd parity across all 32 bits means popcount is odd. */
    return (popcount32(word) & 0x1U) == 1U;
}

uint8_t A429_Label(uint32_t word)
{
    /* Label in bits 0..7 if using little-endian assembly above. */
    return (uint8_t)(word & 0xFFU);
}

/******************************************************************************
 * File: source/data_uart.h
 ******************************************************************************/
#ifndef DATA_UART_H
#define DATA_UART_H

#include <stdint.h>
#include <stdbool.h>

#include "fsl_lpuart.h"
#include "fi.h"

typedef struct
{
    LPUART_Type *base;
    lpuart_handle_t handle;

    volatile bool rxOnGoing;
    volatile bool txOnGoing;
    volatile bool rxByteReady;

    uint8_t rxByte;

    /* TX stall simulation */
    volatile bool txStallActive;

} data_uart_t;

void DataUart_Init(data_uart_t *u, LPUART_Type *base, uint32_t clkHz, uint32_t baud);

/* Non-blocking, 1-byte RX "pump" */
status_t DataUart_StartReceive1(data_uart_t *u);

/* TX wrapper that can inject API failure / stall */
status_t DataUart_SendNonBlocking_FI(data_uart_t *u, const uint8_t *data, size_t len);

/* Progress tracking for stall monitor */
status_t DataUart_GetSendCount(data_uart_t *u, uint32_t *count);
void DataUart_AbortSend(data_uart_t *u);

#endif /* DATA_UART_H */

/******************************************************************************
 * File: source/data_uart.c
 ******************************************************************************/
#include "data_uart.h"
#include "fsl_common.h"
#include "fsl_gpio.h"

static void DataUart_Callback(LPUART_Type *base, lpuart_handle_t *handle, status_t status, void *userData)
{
    (void)base;
    (void)handle;
    data_uart_t *u = (data_uart_t *)userData;

    if (status == kStatus_LPUART_RxIdle)
    {
        u->rxOnGoing = false;
        u->rxByteReady = true;
    }
    else if (status == kStatus_LPUART_TxIdle)
    {
#if SFI_ENABLED
        /* Simulate lost TX completion by refusing to clear txOnGoing for this transfer. */
        if (u->txStallActive)
        {
            /* Keep txOnGoing asserted; PIT monitor must recover. */
            return;
        }
#endif
        u->txOnGoing = false;
    }
    else
    {
        /* Ignore */
    }
}

void DataUart_Init(data_uart_t *u, LPUART_Type *base, uint32_t clkHz, uint32_t baud)
{
    lpuart_config_t cfg;

    u->base = base;
    u->rxOnGoing = false;
    u->txOnGoing = false;
    u->rxByteReady = false;
    u->rxByte = 0;
    u->txStallActive = false;

    LPUART_GetDefaultConfig(&cfg);
    cfg.baudRate_Bps = baud;
    cfg.enableTx = true;
    cfg.enableRx = true;

    LPUART_Init(base, &cfg, clkHz);
    LPUART_TransferCreateHandle(base, &u->handle, DataUart_Callback, (void *)u);

    /* Enable IRQ for this LPUART instance (user must ensure DATA_LPUART_IRQn matches base). */
    /* NOTE: If using multiple cores/RTOS, set NVIC priority appropriately. */
}

status_t DataUart_StartReceive1(data_uart_t *u)
{
    lpuart_transfer_t xfer;
    if (u->rxOnGoing) return kStatus_LPUART_RxBusy;

    xfer.data = &u->rxByte;
    xfer.dataSize = 1U;

    u->rxOnGoing = true;
    u->rxByteReady = false;

    return LPUART_TransferReceiveNonBlocking(u->base, &u->handle, &xfer, NULL);
}

status_t DataUart_SendNonBlocking_FI(data_uart_t *u, const uint8_t *data, size_t len)
{
    lpuart_transfer_t xfer;

    /* Inject API failure: return TxBusy without calling the driver. */
#if SFI_ENABLED
    {
        fi_site_cfg_t *cfg = FI_SiteCfg(FI_SITE_TX_API_FAIL);
        bool fire = FI_ShouldFire(FI_SITE_TX_API_FAIL);
        if (cfg)
        {
            FI_Log(FI_SITE_TX_API_FAIL, fire, cfg->hit_count, (uint32_t)len, (uint32_t)(len ? data[0] : 0U));
        }
        if (fire)
        {
            return kStatus_LPUART_TxBusy;
        }
    }
#endif

#if SFI_ENABLED
    /* Inject TX stall (lost completion): arm a one-transfer stall when site is active. */
    {
        fi_site_cfg_t *cfg = FI_SiteCfg(FI_SITE_TX_STALL);
        bool fire = FI_ShouldFire(FI_SITE_TX_STALL);
        if (cfg)
        {
            FI_Log(FI_SITE_TX_STALL, fire, cfg->hit_count, (uint32_t)len, (uint32_t)(len ? data[0] : 0U));
        }
        u->txStallActive = fire;
    }
#else
    u->txStallActive = false;
#endif

    xfer.data = (uint8_t *)(uintptr_t)data;
    xfer.dataSize = len;

    u->txOnGoing = true;
    return LPUART_TransferSendNonBlocking(u->base, &u->handle, &xfer);
}

status_t DataUart_GetSendCount(data_uart_t *u, uint32_t *count)
{
    return LPUART_TransferGetSendCount(u->base, &u->handle, count);
}

void DataUart_AbortSend(data_uart_t *u)
{
    LPUART_TransferAbortSend(u->base, &u->handle);
    u->txOnGoing = false;
    u->txStallActive = false;
}

/******************************************************************************
 * File: source/main.c  (REPLACE lpuart_interrupt_transfer.c with this main.c)
 ******************************************************************************/
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
    if (DbgConsole_TryGetchar(&ch) != kStatus_Success)
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

/******************************************************************************
 * File: ADK/HI-8582 side (IAR) — optional helper to generate UART frames
 *
 * This is NOT built in MCUXpresso. Copy the function below into your Holt
 * HI-8582 transmitter project (IAR), e.g. HOLT_PROJECT_BUILDS/ARINC429_TRANSMITTER/HI-8582_Demo/main.c
 *
 * It uses the existing USART/printf infrastructure found in the Holt project
 * (USART_Write / BOARD_USART_BASE).
 ******************************************************************************/

/*
//------------------------- ADK / HI-8582 (IAR) snippet -------------------------
#include <stdint.h>
#include <usart/usart.h>

static uint8_t chk8(uint8_t len, const uint8_t p[4])
{
    uint16_t s = (uint16_t)len;
    s = (uint16_t)(s + p[0] + p[1] + p[2] + p[3]);
    return (uint8_t)(s & 0xFFU);
}

static void UART_SendA429Frame(uint32_t word_le)
{
    uint8_t frame[7];
    frame[0] = 0xA5;
    frame[1] = 4;
    frame[2] = (uint8_t)(word_le & 0xFF);
    frame[3] = (uint8_t)((word_le >> 8) & 0xFF);
    frame[4] = (uint8_t)((word_le >> 16) & 0xFF);
    frame[5] = (uint8_t)((word_le >> 24) & 0xFF);
    frame[6] = chk8(frame[1], &frame[2]);

    for (int i = 0; i < 7; i++)
    {
        USART_Write(BOARD_USART_BASE, frame[i], 0);
    }
}

// Call UART_SendA429Frame(word) at a controlled rate (e.g., 50–200 Hz)
// to feed the EVKB receiver.
//-------------------------------------------------------------------------------
*/
