#include "board.h"
#include "app.h"
#include "fsl_lpuart.h"
#include "fsl_debug_console.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

/*******************************************************************************
 * Configuration
 ******************************************************************************/

#define UART_SNiffer_BAUDRATE       (115200U)
#define UART_RX_RING_BUFFER_SIZE    (512U)
#define UART_RX_TEMP_BUFFER_SIZE    (64U)
#define UART_LINE_BUFFER_SIZE       (80U)
#define ANALYZER_MAX_LABELS         (256U)

/*******************************************************************************
 * Types and globals
 ******************************************************************************/

/* Per-label statistics */
typedef struct
{
    uint32_t totalCount;
    uint32_t ch1Count;
    uint32_t ch2Count;
    uint32_t parityErrorCount;
    uint32_t lastData;
    uint8_t  lastSdi;
    uint8_t  lastSsm;
    uint8_t  lastChannel;
} analyzer_label_stats_t;

/* LPUART3 handle and ring buffer for sniffer input */
static lpuart_handle_t g_lpuartHandle;
static uint8_t         g_rxRingBuffer[UART_RX_RING_BUFFER_SIZE];

/* Line assembly buffer */
static char   s_lineBuffer[UART_LINE_BUFFER_SIZE];
static size_t s_lineLength = 0U;

/* Statistics */
static analyzer_label_stats_t s_labelStats[ANALYZER_MAX_LABELS];
static uint32_t              s_totalWords        = 0U;
static uint32_t              s_totalParityErrors = 0U;

/* Filters */
static bool    s_filterByLabelEnabled   = false;
static uint8_t s_filterLabel            = 0U;
static bool    s_filterByChannelEnabled = false;
static uint8_t s_filterChannel          = 0U; /* 1 or 2 */

/*******************************************************************************
 * Utility: ARINC parity
 ******************************************************************************/

/* Compute ARINC 429 odd parity bit for bits 0..30 of the word */
static uint8_t Analyzer_ComputeParityBit(uint32_t wordNoParity)
{
    uint8_t parity = 0U;
    uint32_t v = wordNoParity & 0x7FFFFFFFU; /* Only 31 bits used */

    while (v != 0U)
    {
        parity ^= (uint8_t)(v & 1U);
        v >>= 1U;
    }

    /* If number of ones in bits 0..30 is odd (parity=1), parity bit must be 0.
       If number of ones is even (parity=0), parity bit must be 1. */
    return (uint8_t)(parity ^ 1U);
}

/*******************************************************************************
 * Analyzer logic
 ******************************************************************************/

static void Analyzer_ClearStats(void)
{
    memset(s_labelStats, 0, sizeof(s_labelStats));
    s_totalWords        = 0U;
    s_totalParityErrors = 0U;
}

static void Analyzer_PrintHelp(void)
{
    PRINTF("\r\n=== ARINC 429 UART Host Commands ===\r\n");
    PRINTF("  h - show this help\r\n");
    PRINTF("  s - show overall summary\r\n");
    PRINTF("  l - list labels with non-zero counts\r\n");
    PRINTF("  c - clear statistics\r\n");
    PRINTF("  f - set label/channel filter\r\n");
    PRINTF("  r - remove filters\r\n");
}

static void Analyzer_PrintSummary(void)
{
    PRINTF("\r\n=== Summary ===\r\n");
    PRINTF("Total words        : %u\r\n", (unsigned int)s_totalWords);
    PRINTF("Total parity errors: %u\r\n", (unsigned int)s_totalParityErrors);
}

static void Analyzer_PrintLabelTable(void)
{
    uint32_t lbl;

    PRINTF("\r\nLabel  Count   CH1   CH2   ParErr  LastData   SDI SSM CH\r\n");
    for (lbl = 0U; lbl < ANALYZER_MAX_LABELS; lbl++)
    {
        const analyzer_label_stats_t *st = &s_labelStats[lbl];
        if (st->totalCount != 0U)
        {
            PRINTF("0x%02X  %6u %5u %5u %7u  0x%05X    %u   %u  %u\r\n",
                   (unsigned int)lbl,
                   (unsigned int)st->totalCount,
                   (unsigned int)st->ch1Count,
                   (unsigned int)st->ch2Count,
                   (unsigned int)st->parityErrorCount,
                   (unsigned int)st->lastData,
                   (unsigned int)st->lastSdi,
                   (unsigned int)st->lastSsm,
                   (unsigned int)st->lastChannel);
        }
    }
}

/* Parse one line from sniffer and update statistics. */
static void Analyzer_ProcessLine(const char *line)
{
    unsigned int ch = 0U;
    unsigned int lbl = 0U;
    unsigned int sdi = 0U;
    unsigned int data = 0U;
    unsigned int ssm = 0U;
    unsigned int p = 0U;

    /* Example line:
       CH1 LBL=1F SDI=0 DATA=12345 SSM=1 P=1
     */
    int parsed = sscanf(line,
                        "CH%u LBL=%x SDI=%u DATA=%x SSM=%u P=%u",
                        &ch, &lbl, &sdi, &data, &ssm, &p);
    if (parsed != 6)
    {
        PRINTF("WARN: Could not parse line: \"%s\"\r\n", line);
        return;
    }

    uint8_t  channel   = (uint8_t)ch;
    uint8_t  labelByte = (uint8_t)(lbl & 0xFFU);
    uint8_t  sdiBits   = (uint8_t)(sdi & 0x03U);
    uint32_t dataBits  = (uint32_t)(data & 0x1FFFFFU); /* 19-bit field */
    uint8_t  ssmBits   = (uint8_t)(ssm & 0x03U);
    uint8_t  parityBit = (uint8_t)(p & 0x01U);

    /* Reconstruct bits 0..30 (no parity) in Holt/our bit layout:
       bits 0..7   : LABEL
       bits 8..9   : SDI
       bits 10..28 : DATA (19 bits)
       bits 29..30 : SSM
       bit  31     : parity
     */
    uint32_t wordNoParity = 0U;
    wordNoParity |= (uint32_t)labelByte;
    wordNoParity |= ((uint32_t)sdiBits << 8);
    wordNoParity |= ((uint32_t)dataBits << 10);
    wordNoParity |= ((uint32_t)ssmBits << 29);

    uint8_t expectedParity = Analyzer_ComputeParityBit(wordNoParity);
    bool parityOk = (parityBit == expectedParity);

    s_totalWords++;

    analyzer_label_stats_t *st = &s_labelStats[labelByte];
    st->totalCount++;
    if (channel == 1U)
    {
        st->ch1Count++;
    }
    else if (channel == 2U)
    {
        st->ch2Count++;
    }
    if (!parityOk)
    {
        st->parityErrorCount++;
        s_totalParityErrors++;
    }

    st->lastData    = dataBits;
    st->lastSdi     = sdiBits;
    st->lastSsm     = ssmBits;
    st->lastChannel = channel;

    /* Apply filters for live output on debug console */
    bool match = true;

    if (s_filterByLabelEnabled && (labelByte != s_filterLabel))
    {
        match = false;
    }

    if (s_filterByChannelEnabled && (channel != s_filterChannel))
    {
        match = false;
    }

    if (match)
    {
        PRINTF("CH%u LBL=%02X SDI=%u DATA=%05X SSM=%u P=%u%s\r\n",
               (unsigned int)channel,
               (unsigned int)labelByte,
               (unsigned int)sdiBits,
               (unsigned int)dataBits,
               (unsigned int)ssmBits,
               (unsigned int)parityBit,
               parityOk ? "" : " [PARITY ERR]");
    }
}

/*******************************************************************************
 * UART (sniffer) handling – LPUART3
 ******************************************************************************/

/* We don't need anything in the callback for this use-case (ring buffer only). */
static void LPUART_SnifferCallback(LPUART_Type *base,
                                   lpuart_handle_t *handle,
                                   status_t status,
                                   void *userData)
{
    (void)base;
    (void)handle;
    (void)status;
    (void)userData;
}

/* Pull available bytes from the LPUART ring buffer and feed the line parser. */
static void Sniffer_PollUart(void)
{
    size_t available = LPUART_TransferGetRxRingBufferLength(DEMO_LPUART, &g_lpuartHandle);

    if (available == 0U)
    {
        return;
    }

    uint8_t rxTemp[UART_RX_TEMP_BUFFER_SIZE];
    lpuart_transfer_t xfer;
    size_t toRead = available;

    if (toRead > UART_RX_TEMP_BUFFER_SIZE)
    {
        toRead = UART_RX_TEMP_BUFFER_SIZE;
    }

    xfer.data     = rxTemp;
    xfer.dataSize = toRead;

    size_t receivedBytes = 0U;
    status_t status = LPUART_TransferReceiveNonBlocking(DEMO_LPUART,
                                                        &g_lpuartHandle,
                                                        &xfer,
                                                        &receivedBytes);
    if ((status != kStatus_Success) && (status != kStatus_LPUART_RxIdle))
    {
        /* RX busy or error: just skip this time */
        return;
    }

    for (size_t i = 0U; i < receivedBytes; i++)
    {
        char ch = (char)rxTemp[i];

        if ((ch == '\r') || (ch == '\n'))
        {
            if (s_lineLength > 0U)
            {
                s_lineBuffer[s_lineLength] = '\0';
                Analyzer_ProcessLine(s_lineBuffer);
                s_lineLength = 0U;
            }
        }
        else
        {
            if (s_lineLength < (UART_LINE_BUFFER_SIZE - 1U))
            {
                s_lineBuffer[s_lineLength++] = ch;
            }
            else
            {
                /* Overflow, drop line */
                s_lineLength = 0U;
            }
        }
    }
}

/*******************************************************************************
 * Debug-console command handling
 ******************************************************************************/

static void Analyzer_SetFilterInteractive(void)
{
    uint32_t label = 0U;
    uint32_t channel = 0U;

    PRINTF("\r\nEnter hex label to filter (e.g. 1F), or 0xFF to disable label filter: ");
    SCANF("%x", &label);
    label &= 0xFFU;

    if (label == 0xFFU)
    {
        s_filterByLabelEnabled = false;
        PRINTF("\r\nLabel filter disabled.\r\n");
    }
    else
    {
        s_filterByLabelEnabled = true;
        s_filterLabel          = (uint8_t)label;
        PRINTF("\r\nLabel filter set to 0x%02X.\r\n", (unsigned int)s_filterLabel);
    }

    PRINTF("Enter channel filter (0=any, 1 or 2): ");
    SCANF("%u", &channel);

    if (channel == 1U || channel == 2U)
    {
        s_filterByChannelEnabled = true;
        s_filterChannel          = (uint8_t)channel;
        PRINTF("\r\nChannel filter set to %u.\r\n", (unsigned int)s_filterChannel);
    }
    else
    {
        s_filterByChannelEnabled = false;
        PRINTF("\r\nChannel filter disabled (any channel).\r\n");
    }
}

/* Non-blocking poll for a console command using DbgConsole_TryGetchar. */
static void Analyzer_HandleUserInput(void)
{
    char ch;
    status_t st = DbgConsole_TryGetchar(&ch);

    if (st != kStatus_Success)
    {
        return;
    }

    switch (ch)
    {
        case 'h':
        case 'H':
            Analyzer_PrintHelp();
            break;

        case 's':
        case 'S':
            Analyzer_PrintSummary();
            break;

        case 'l':
        case 'L':
            Analyzer_PrintLabelTable();
            break;

        case 'c':
        case 'C':
            Analyzer_ClearStats();
            PRINTF("\r\nStatistics cleared.\r\n");
            break;

        case 'f':
        case 'F':
            Analyzer_SetFilterInteractive();
            break;

        case 'r':
        case 'R':
            s_filterByLabelEnabled   = false;
            s_filterByChannelEnabled = false;
            PRINTF("\r\nAll filters disabled.\r\n");
            break;

        default:
            PRINTF("\r\nUnknown command '%c'. Press 'h' for help.\r\n", ch);
            break;
    }
}

/*******************************************************************************
 * Main
 ******************************************************************************/

int main(void)
{
    lpuart_config_t config;

    BOARD_InitHardware();

    PRINTF("\r\n=== IMXRT1050 ARINC 429 UART Host ===\r\n");
    PRINTF("LPUART3 is used to receive sniffer output at %u baud.\r\n", (unsigned int)UART_SNiffer_BAUDRATE);
    PRINTF("Use the debug console (USB CDC) for commands. Press 'h' for help.\r\n");

    /* Configure LPUART3 (DEMO_LPUART) for 115200, 8-N-1 */
    LPUART_GetDefaultConfig(&config);
    config.baudRate_Bps = UART_SNiffer_BAUDRATE;
    config.enableTx     = false; /* We only receive from sniffer; TX is not required */
    config.enableRx     = true;

    LPUART_Init(DEMO_LPUART, &config, DEMO_LPUART_CLK_FREQ);

    /* Create handle and start RX ring buffer */
    LPUART_TransferCreateHandle(DEMO_LPUART, &g_lpuartHandle, LPUART_SnifferCallback, NULL);
    LPUART_TransferStartRingBuffer(DEMO_LPUART, &g_lpuartHandle, g_rxRingBuffer, sizeof(g_rxRingBuffer));

    /* Initialize analyzer state */
    Analyzer_ClearStats();
    Analyzer_PrintHelp();

    while (1)
    {
        /* Continuously grab data from the sniffer UART and parse it */
        Sniffer_PollUart();

        /* Handle any PC console commands (non-blocking) */
        Analyzer_HandleUserInput();
    }
}
