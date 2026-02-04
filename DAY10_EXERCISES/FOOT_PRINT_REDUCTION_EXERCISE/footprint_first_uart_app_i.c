/*
 * Footprint-First UART App (i.MX RT1050 EVKB)
 *
 * Apply this solution on top of the MCUXpresso SDK example:
 *   boards/evkbimxrt1050/driver_examples/lpuart/interrupt_transfer
 *
 * What you do with the files below:
 *   1) Add:    footprint_config.h
 *   2) Replace: lpuart_interrupt_transfer.c  (use the file content provided below)
 *   3) Patch:  board.h  (small include + macro tweak)
 *   4) Patch:  board.c  (compile-out BOARD_InitDebugConsole when debug console is disabled)
 *
 * Notes:
 *   - This code is designed to work with the SDK LPUART interrupt transfer APIs.
 *   - This code intentionally uses NO malloc/free.
 *   - Verbose logging is fully compiled out in FP_BUILD_RELEASE=1.
 *   - Non-cacheable placement is requested via AT_NONCACHEABLE_SECTION_ALIGN();
 *     depending on toolchain/linker scripts, you may still need to map the section.
 */

// ============================================================================
// File: footprint_config.h   (ADD THIS NEW FILE to the example project)
// ============================================================================
#ifndef FOOTPRINT_CONFIG_H_
#define FOOTPRINT_CONFIG_H_

/*
 * Build mode:
 *   1 => production footprint build (no debug console, no verbose logs)
 *   0 => dev build (you may enable logs/extra features)
 */
#ifndef FP_BUILD_RELEASE
#define FP_BUILD_RELEASE (1)
#endif

/*
 * SDK debug console selector (from fsl_debug_console.h):
 *   0 => redirect to toolchain
 *   1 => redirect to SDK
 *   2 => disable
 *
 * We set it to 2 for footprint release builds.
 */
#if FP_BUILD_RELEASE
#ifndef SDK_DEBUGCONSOLE
#define SDK_DEBUGCONSOLE (2U)
#endif
#endif

/* Feature gates (compile-time) */
#ifndef FP_FEATURE_TIPSTRING
#define FP_FEATURE_TIPSTRING (1) /* set to 0 to remove tip string from binary */
#endif

#ifndef FP_FEATURE_ECHO
#define FP_FEATURE_ECHO (1)      /* set to 0 to disable echo logic (RX discard) */
#endif

#ifndef FP_FEATURE_LED_HEARTBEAT
#define FP_FEATURE_LED_HEARTBEAT (0) /* set to 1 to toggle the user LED */
#endif

/*
 * Verbose logging:
 *   - In FP_BUILD_RELEASE, this is forced OFF.
 *   - In dev builds, you may set this to 1 and rely on SDK PRINTF.
 */
#ifndef FP_VERBOSE_LOG
#define FP_VERBOSE_LOG (0)
#endif

#if FP_BUILD_RELEASE
#undef FP_VERBOSE_LOG
#define FP_VERBOSE_LOG (0)
#endif

/* Sizes (keep small for footprint) */
#ifndef FP_ECHO_BUFFER_LENGTH
#define FP_ECHO_BUFFER_LENGTH (8U)
#endif

#endif /* FOOTPRINT_CONFIG_H_ */


// ============================================================================
// File: lpuart_interrupt_transfer.c  (REPLACE the example file with this)
// Path: boards/evkbimxrt1050/driver_examples/lpuart/interrupt_transfer/
// ============================================================================

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


// ============================================================================
// File: board.h  (PATCH)
// Path: boards/evkbimxrt1050/driver_examples/lpuart/interrupt_transfer/board.h
//
// Make these two changes:
//   A) include footprint_config.h
//   B) include serial manager header so kSerialPort_Uart is known even if
//      debug console is disabled in the build.
//
// Add these includes near the top (after existing includes):
// ============================================================================
/*
--- PATCH START (board.h) ---

#include "footprint_config.h"
#include "fsl_component_serial_manager.h"

--- PATCH END (board.h) ---
*/


// ============================================================================
// File: board.c  (PATCH)
// Path: boards/evkbimxrt1050/driver_examples/lpuart/interrupt_transfer/board.c
//
// Goal: Compile out BOARD_InitDebugConsole() when SDK_DEBUGCONSOLE==2 (disabled)
// so no DbgConsole_Init symbol is required.
// ============================================================================
/*
--- PATCH START (board.c) ---

// Add this include BEFORE including fsl_debug_console.h
#include "footprint_config.h"

// Keep this include (it provides the SDK_DEBUGCONSOLE logic and constants)
#include "fsl_debug_console.h"

...

// Replace the existing BOARD_InitDebugConsole() function with this:

void BOARD_InitDebugConsole(void)
{
#if defined(SDK_DEBUGCONSOLE) && (SDK_DEBUGCONSOLE == 2U)
    // Debug console disabled for footprint release: intentionally do nothing.
    // This prevents pulling in serial manager / debug console init paths.
    return;
#else
    uint32_t uartClkSrcFreq = BOARD_DebugConsoleSrcFreq();
    DbgConsole_Init(BOARD_DEBUG_UART_INSTANCE, BOARD_DEBUG_UART_BAUDRATE, BOARD_DEBUG_UART_TYPE, uartClkSrcFreq);
#endif
}

--- PATCH END (board.c) ---
*/
