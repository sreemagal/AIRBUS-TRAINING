#include "fsl_lpuart.h"
#include "fi.h"

extern status_t __real_LPUART_WriteBlocking(LPUART_Type *base, const uint8_t *data, size_t length);

status_t __wrap_LPUART_WriteBlocking(LPUART_Type *base, const uint8_t *data, size_t length)
{
    FI_POINT(FI_F_UART_TX_BUSY, return kStatus_LPUART_TxBusy;);

    if (FI_ENABLE && FI_ShouldFire(FI_F_UART_CORRUPT) && length <= 128u)
    {
        uint8_t scratch[128];
        for (size_t i = 0; i < length; i++)
            scratch[i] = data[i];

        FI_BitFlipRange(scratch, (uint32_t)length, 11);
        return __real_LPUART_WriteBlocking(base, scratch, length);
    }

    return __real_LPUART_WriteBlocking(base, data, length);
}
