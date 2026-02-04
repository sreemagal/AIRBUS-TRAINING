#include "uart_fi_shim.h"
#include "fi.h"

#define UART_FI_MAX_COPY 128u

status_t UART_FI_WriteBlocking(LPUART_Type *base, const uint8_t *data, size_t length)
{
    /* Inject: busy return */
    FI_POINT(FI_F_UART_TX_BUSY, return kStatus_LPUART_TxBusy;);

    /* Inject: corruption (copy to scratch to avoid writing into const buffers) */
    if (FI_ENABLE && FI_ShouldFire(FI_F_UART_CORRUPT) && (length <= UART_FI_MAX_COPY))
    {
        uint8_t scratch[UART_FI_MAX_COPY];
        for (size_t i = 0; i < length; i++)
            scratch[i] = data[i];

        FI_BitFlipRange(scratch, (uint32_t)length, 7);
        return LPUART_WriteBlocking(base, scratch, length);
    }

    return LPUART_WriteBlocking(base, data, length);
}
