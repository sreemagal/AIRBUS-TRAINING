#ifndef BOARD_UART3_PINS_H_
#define BOARD_UART3_PINS_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Configure IOMUXC for LPUART3 on Arduino UART pins (J22[2]/J22[1]). */
void BOARD_InitUart3Pins(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_UART3_PINS_H_ */
