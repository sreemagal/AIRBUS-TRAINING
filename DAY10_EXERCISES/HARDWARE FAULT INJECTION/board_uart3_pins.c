#include "board_uart3_pins.h"

#include "fsl_common.h"
#include "fsl_iomuxc.h"
#include "fsl_clock.h"

void BOARD_InitUart3Pins(void)
{
    CLOCK_EnableClock(kCLOCK_Iomuxc);

    /* Arduino header UART pins: J22[2]=TX, J22[1]=RX */
    IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B1_06_LPUART3_TXD, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B1_07_LPUART3_RXD, 0U);

    /* Match the style used by the SDK examples for LPUART pin config. */
    IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B1_06_LPUART3_TXD, 0x10B0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B1_07_LPUART3_RXD, 0x10B0U);
}
