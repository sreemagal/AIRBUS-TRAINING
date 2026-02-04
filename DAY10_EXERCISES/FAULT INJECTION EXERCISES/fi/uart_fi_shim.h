#ifndef UART_FI_SHIM_H
#define UART_FI_SHIM_H

#include <stddef.h>
#include <stdint.h>
#include "fsl_lpuart.h"

status_t UART_FI_WriteBlocking(LPUART_Type *base, const uint8_t *data, size_t length);

#endif
