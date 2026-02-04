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
