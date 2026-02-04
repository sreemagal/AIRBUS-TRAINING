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