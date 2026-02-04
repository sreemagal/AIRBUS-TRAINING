#include "footprint_config.h"

#include "board.h"
#include "app.h"

#include "fsl_common.h"
#include "fsl_lpuart.h"

#if FP_VERBOSE_LOG
#include "fsl_debug_console.h"
#endif

/*******************************************************************************
 * Small, footprint-friendly logging (compiled out when FP_VERBOSE_LOG==0)
 ******************************************************************************/
#if FP_VERBOSE_LOG
#define FP_LOG(...) PRINTF(__VA_ARGS__)
#else
#define FP_LOG(...) do { } while (0)
#endif

/*******************************************************************************
 * Static storage only (NO HEAP)
 ******************************************************************************/
static lpuart_handle_t s_lpuartHandle;

/*
 * Non-cacheable + aligned buffers.
 * For GCC in this SDK, AT_NONCACHEABLE_SECTION_ALIGN may only enforce alignment,
 * depending on memory/linker configuration.
 */
AT_NONCACHEABLE_SECTION_ALIGN(static uint8_t s_rxBuffer[FP_ECHO_BUFFER_LENGTH], 32);
AT_NONCACHEABLE_SECTION_ALIGN(static uint8_t s_txBuffer[FP_ECHO_BUFFER_LENGTH], 32);

static volatile bool s_txOnGoing     = false;
static volatile bool s_rxOnGoing     = false;
static volatile bool s_txBufferFull  = false;
static volatile bool s_rxBufferEmpty = true;

#if FP_FEATURE_TIPSTRING
/* Keep tip string short to reduce .rodata footprint */
static const uint8_t s_tipString[] =
    "LPUART footprint echo\r\n"
    "Type 8 bytes -> echoed\r\n";
#endif

static void FP_CopyU8(uint8_t *dst, const uint8_t *src, size_t len)
{
    /* Avoid pulling in larger libc routines in some toolchains. */
    while (len--)
    {
        *dst++ = *src++;
    }
}

static void LPUART_UserCallback(LPUART_Type *base, lpuart_handle_t *handle, status_t status, void *userData)
{
    (void)base;
    (void)handle;
    (void)userData;

    if (kStatus_LPUART_TxIdle == status)
    {
        s_txOnGoing = false;
    }

    if (kStatus_LPUART_RxIdle == status)
    {
        s_rxOnGoing = false;
    }

    /* If you want, you can add a minimal error counter here (still static, no printf). */
}

int main(void)
{
    lpuart_config_t config;
    lpuart_transfer_t xfer;
    lpuart_transfer_t rxXfer;
    lpuart_transfer_t txXfer;

    BOARD_InitHardware();

#if FP_FEATURE_LED_HEARTBEAT
    USER_LED_INIT(1U);
#endif

    /* Minimal LPUART init */
    LPUART_GetDefaultConfig(&config);
    config.baudRate_Bps = BOARD_DEBUG_UART_BAUDRATE; /* 115200 by default on this board */
    config.enableTx     = true;
    config.enableRx     = true;

    LPUART_Init(DEMO_LPUART, &config, DEMO_LPUART_CLK_FREQ);
    LPUART_TransferCreateHandle(DEMO_LPUART, &s_lpuartHandle, LPUART_UserCallback, NULL);

#if FP_FEATURE_TIPSTRING
    /* Send a short tip string using LPUART itself (no PRINTF / debug console). */
    xfer.data     = (uint8_t *)(uintptr_t)s_tipString;
    xfer.dataSize = sizeof(s_tipString) - 1U;

    s_txOnGoing = true;
    if (kStatus_Success != LPUART_TransferSendNonBlocking(DEMO_LPUART, &s_lpuartHandle, &xfer))
    {
        /* Hard stop on init failure (deterministic). */
        while (1)
        {
        }
    }
#endif

    /* Prime the first RX */
    rxXfer.data     = s_rxBuffer;
    rxXfer.dataSize = FP_ECHO_BUFFER_LENGTH;
    s_rxOnGoing     = true;
    if (kStatus_Success != LPUART_TransferReceiveNonBlocking(DEMO_LPUART, &s_lpuartHandle, &rxXfer, NULL))
    {
        while (1)
        {
        }
    }

    for (;;)
    {
#if FP_FEATURE_LED_HEARTBEAT
        /* Very low cost heartbeat; set to 0 in footprint builds */
        USER_LED_TOGGLE();
#endif

        /* When RX completes, decide what to do with the data */
        if ((!s_rxOnGoing) && (s_rxBufferEmpty))
        {
            s_rxBufferEmpty = false;

#if FP_FEATURE_ECHO
            /* Copy RX -> TX buffer */
            FP_CopyU8(s_txBuffer, s_rxBuffer, FP_ECHO_BUFFER_LENGTH);
            s_txBufferFull = true;
#else
            /* Discard received bytes (RX-only mode) */
            (void)s_txBufferFull;
#endif

            /* Immediately start another RX */
            s_rxOnGoing = true;
            if (kStatus_Success != LPUART_TransferReceiveNonBlocking(DEMO_LPUART, &s_lpuartHandle, &rxXfer, NULL))
            {
                while (1)
                {
                }
            }
        }

#if FP_FEATURE_ECHO
        /* When TX not busy and TX buffer is full, send it */
        if ((!s_txOnGoing) && (s_txBufferFull))
        {
            txXfer.data     = s_txBuffer;
            txXfer.dataSize = FP_ECHO_BUFFER_LENGTH;

            s_txBufferFull = false;
            s_txOnGoing    = true;

            if (kStatus_Success != LPUART_TransferSendNonBlocking(DEMO_LPUART, &s_lpuartHandle, &txXfer))
            {
                while (1)
                {
                }
            }
        }
#endif

        /* Optional: tiny, compiled-out debug */
        FP_LOG(".");
    }
}
