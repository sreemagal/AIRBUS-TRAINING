#ifndef _APP_H_
#define _APP_H_

/* Base example had DEMO_LPUART == LPUART1 for debug console.
 * Keep it that way for PRINTF + CLI.
 */
#define DEMO_LPUART          LPUART1
#define DEMO_LPUART_CLK_FREQ BOARD_DebugConsoleSrcFreq()

/* Data UART toward ADK/HI-8582 (choose any available LPUART you routed to pins).
 * Default: LPUART3.
 */
#define DATA_LPUART          LPUART3
#define DATA_LPUART_IRQn     LPUART3_IRQn
#define DATA_LPUART_IRQHandler LPUART3_IRQHandler
#define DATA_LPUART_CLK_FREQ BOARD_DebugConsoleSrcFreq()
#define DATA_LPUART_BAUDRATE (115200U)

/* PIT tick (10 ms) used for TX stall monitoring + periodic telemetry. */
#define DEMO_PIT_BASEADDR PIT
#define DEMO_PIT_CHANNEL  kPIT_Chnl_0
#define PIT_IRQ_ID        PIT_IRQn
#define PIT_IRQ_HANDLER   PIT_IRQHandler
#define PIT_SOURCE_CLOCK  CLOCK_GetFreq(kCLOCK_OscClk)

void BOARD_InitHardware(void);

#endif /* _APP_H_ */