/*
 * Discrete I/O Driver Design Using Polling — EVKB-i.MX RT1050
 *
 * Base SDK example: driver_examples/gpio/led_output
 *
 * Discretes:
 *   - SW8 (SNVS_WAKEUP -> GPIO5_IO00) as input
 *   - USER LED (GPIO1_IO09) as output
 *
 * Polling:
 *   - Poll period Tp = 5ms
 *   - Debounce via counter-to-N method
 *
 * Modes:
 *   - Default: GENERIC (button mirrors to LED)
 *   - Avionics: define LAB_MODE_AVIONICS=1 (WOW-gated Pump Cmd + PBIT)
 */

#include <stdbool.h>
#include <stdint.h>

#include "board.h"
#include "app.h"

#include "fsl_common.h"
#include "fsl_clock.h"
#include "fsl_debug_console.h"
#include "fsl_gpio.h"
#include "fsl_iomuxc.h"
#include "fsl_device_registers.h"

#include "io_discrete_poll.h"

/* ------------------------- Build-time knobs ------------------------- */
#ifndef LAB_MODE_AVIONICS
#define LAB_MODE_AVIONICS (0)
#endif

#ifndef ENABLE_DWT_TIMING
#define ENABLE_DWT_TIMING (0)
#endif

#ifndef SW8_ACTIVE_LOW
#define SW8_ACTIVE_LOW (1) /* EVKB SW8 is typically active-low */
#endif

/* Timing */
#define POLL_PERIOD_MS          (5u)
#define POLL_PERIOD_US          (POLL_PERIOD_MS * 1000u)

#define DEBOUNCE_TD_GENERIC_MS  (20u)
#define DEBOUNCE_TD_WOW_MS      (30u)

#define DEBOUNCE_N_GENERIC      ((uint8_t)(DEBOUNCE_TD_GENERIC_MS / POLL_PERIOD_MS)) /* 4 */
#define DEBOUNCE_N_WOW          ((uint8_t)(DEBOUNCE_TD_WOW_MS / POLL_PERIOD_MS))     /* 6 */

#define PBIT_WINDOW_MS          (200u)

/* Periodic status prints */
#define STATUS_PRINT_MS         (500u)

/* ------------------------- Optional DWT timing ------------------------- */
#if (ENABLE_DWT_TIMING != 0)
static void dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}
static uint32_t dwt_cycles(void)
{
    return DWT->CYCCNT;
}
#endif

/* ------------------------- Pin mux & pad setup ------------------------- */
static void discrete_pins_init(void)
{
    /* Ensure IOMUXC clocks are enabled */
    CLOCK_EnableClock(kCLOCK_Iomuxc);
    CLOCK_EnableClock(kCLOCK_IomuxcSnvs);

    /* LED: GPIO_AD_B0_09 -> GPIO1_IO09 */
    IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B0_09_GPIO1_IO09, 0U);

    /* Optional: conservative pad settings for LED (keeper/pull enabled, slow slew, moderate drive) */
    IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B0_09_GPIO1_IO09,
                        IOMUXC_SW_PAD_CTL_PAD_PKE_MASK |
                        IOMUXC_SW_PAD_CTL_PAD_PUE_MASK |
                        IOMUXC_SW_PAD_CTL_PAD_PUS(2U) |
                        IOMUXC_SW_PAD_CTL_PAD_HYS_MASK |
                        IOMUXC_SW_PAD_CTL_PAD_SRE(0U) |
                        IOMUXC_SW_PAD_CTL_PAD_SPEED(0U) |
                        IOMUXC_SW_PAD_CTL_PAD_DSE(2U));

    /* SW8: SNVS_WAKEUP -> GPIO5_IO00 */
    IOMUXC_SetPinMux(IOMUXC_SNVS_WAKEUP_GPIO5_IO00, 0U);

    /* SNVS pad control for SW8: pull/keeper + pull-up + hysteresis */
    IOMUXC_SNVS->SW_PAD_CTL_PAD_WAKEUP =
        (IOMUXC_SNVS_SW_PAD_CTL_PAD_WAKEUP_PKE_MASK |
         IOMUXC_SNVS_SW_PAD_CTL_PAD_WAKEUP_PUE_MASK |
         IOMUXC_SNVS_SW_PAD_CTL_PAD_WAKEUP_PUS(2U) |
         IOMUXC_SNVS_SW_PAD_CTL_PAD_WAKEUP_HYS_MASK |
         IOMUXC_SNVS_SW_PAD_CTL_PAD_WAKEUP_SRE(0U));
}

/* ------------------------- Helpers ------------------------- */
static inline uint32_t cpu_hz(void)
{
    return CLOCK_GetFreq(kCLOCK_CpuClk);
}

int main(void)
{
    /* Board init from SDK example (UART, clocks, etc.) */
    BOARD_InitHardware();

    /* Enable GPIO clocks */
    CLOCK_EnableClock(kCLOCK_Gpio1);
    CLOCK_EnableClock(kCLOCK_Gpio5);

    discrete_pins_init();

#if (ENABLE_DWT_TIMING != 0)
    dwt_init();
#endif

    /* Configure driver channels */
    io_input_t sw8;
    io_output_t led;

    /* Polarity config:
     * - SW8 asserted when pressed. On EVKB SW8 is typically active-low.
     * - USER LED on EVKB is typically active-low (LOGIC_LED_ON=0 in board.h).
     */
    const bool sw8_active_high = (SW8_ACTIVE_LOW != 0) ? false : true;
    const bool led_active_high = false; /* asserted -> drive low -> LED ON */

    /* Initialize channels */
#if (LAB_MODE_AVIONICS != 0)
    io_input_init(&sw8, BOARD_USER_BUTTON_GPIO, BOARD_USER_BUTTON_GPIO_PIN, sw8_active_high, DEBOUNCE_N_WOW);
#else
    io_input_init(&sw8, BOARD_USER_BUTTON_GPIO, BOARD_USER_BUTTON_GPIO_PIN, sw8_active_high, DEBOUNCE_N_GENERIC);
#endif

    /* Fail-safe: deassert LED (OFF) at boot */
    io_output_init(&led, BOARD_USER_LED_GPIO, BOARD_USER_LED_GPIO_PIN, led_active_high, false);

    /* Banner */
    PRINTF("\r\n=== Discrete I/O Polling Driver Lab (EVKB-i.MX RT1050) ===\r\n");
#if (LAB_MODE_AVIONICS != 0)
    PRINTF("Mode: AVIONICS (WOW-gated Pump Command + PBIT)\r\n");
    PRINTF("Poll: Tp=%u ms, WOW debounce Td=%u ms => N=%u\r\n", (unsigned)POLL_PERIOD_MS, (unsigned)DEBOUNCE_TD_WOW_MS, (unsigned)DEBOUNCE_N_WOW);
#else
    PRINTF("Mode: GENERIC (SW8 mirrors to LED)\r\n");
    PRINTF("Poll: Tp=%u ms, debounce Td=%u ms => N=%u\r\n", (unsigned)POLL_PERIOD_MS, (unsigned)DEBOUNCE_TD_GENERIC_MS, (unsigned)DEBOUNCE_N_GENERIC);
#endif
    PRINTF("Input: %s (GPIO5_IO00)  Output: USER LED (GPIO1_IO09)\r\n", BOARD_USER_BUTTON_NAME);
    PRINTF("PBIT window: %u ms (toggle SW8 once after reset in AVIONICS mode)\r\n\r\n", (unsigned)PBIT_WINDOW_MS);

    /* Observability counters */
    uint32_t t_ms = 0u;
    uint32_t last_status_ms = 0u;

    uint32_t raw_edges = 0u;
    uint32_t deb_edges = 0u;

    bool last_raw = io_input_raw(&sw8);

    /* PBIT state (used mainly in avionics mode) */
    bool pbit_active = (LAB_MODE_AVIONICS != 0);
    bool pbit_fault = false;
    bool pbit_sw8_toggled = false;
    uint32_t pbit_elapsed_ms = 0u;

    /* Avionics simulation constants */
#if (LAB_MODE_AVIONICS != 0)
    const bool maint_door_closed = true; /* tie-off as constant for the lab */
#endif

    for (;;)
    {
#if (ENABLE_DWT_TIMING != 0)
        uint32_t t0 = dwt_cycles();
#endif

        /* Poll the input once per period */
        io_edge_t e = io_input_poll(&sw8);
        bool raw = io_input_raw(&sw8);

        /* Raw edge count (for observability / PBIT assist) */
        if (raw != last_raw)
        {
            raw_edges++;
            last_raw = raw;
            if (pbit_active)
            {
                pbit_sw8_toggled = true;
            }
        }

        /* Debounced events */
        if (e != IO_EDGE_NONE)
        {
            deb_edges++;
            if (pbit_active)
            {
                pbit_sw8_toggled = true;
            }

            if (e == IO_EDGE_RISE)
            {
                PRINTF("[%6u ms] %s: PRESS (debounced)\r\n", (unsigned long)t_ms, BOARD_USER_BUTTON_NAME);
            }
            else
            {
                PRINTF("[%6u ms] %s: RELEASE (debounced)\r\n", (unsigned long)t_ms, BOARD_USER_BUTTON_NAME);
            }
        }

#if (LAB_MODE_AVIONICS != 0)
        /* ----------------- AVIONICS: WOW-gated Pump Command -----------------
         * WOW asserted on ground. Use debounced SW8 as WOW.
         */
        bool wow_asserted = io_input_get(&sw8);
        bool airborne = !wow_asserted; /* NOT WOW */
        bool pump_ok = airborne && maint_door_closed;

        /* PBIT window handling */
        if (pbit_active)
        {
            pbit_elapsed_ms += POLL_PERIOD_MS;
            if (pbit_elapsed_ms >= PBIT_WINDOW_MS)
            {
                pbit_active = false;
                if (!pbit_sw8_toggled)
                {
                    pbit_fault = true;
                    PRINTF("PBIT: FAIL (no SW8 toggle detected) -> FAIL-SAFE output\r\n");
                }
                else
                {
                    PRINTF("PBIT: PASS\r\n");
                }
            }
        }

        /* Fail-safe output logic */
        if (pbit_active || pbit_fault)
        {
            io_output_set(&led, false); /* pump OFF */
        }
        else
        {
            io_output_set(&led, pump_ok); /* pump cmd mapped to LED */
        }

        /* Optional: print pump command periodically */
        if ((t_ms - last_status_ms) >= STATUS_PRINT_MS)
        {
            last_status_ms = t_ms;
            PRINTF("[%u ms] STATUS raw=%u deb=%u  WOW=%u  PUMP_A_CMD=%u  raw_edges=%u deb_edges=%u pbit_active=%u pbit_fault=%u\r\n",
                   (unsigned long)t_ms,
                   raw ? 1u : 0u,
                   io_input_get(&sw8) ? 1u : 0u,
                   wow_asserted ? 1u : 0u,
                   (pbit_active || pbit_fault) ? 0u : (pump_ok ? 1u : 0u),
                   (unsigned long)raw_edges,
                   (unsigned long)deb_edges,
                   pbit_active ? 1u : 0u,
                   pbit_fault ? 1u : 0u);
        }
#else
        /* ----------------- GENERIC: mirror SW8 to LED ----------------- */
        io_output_set(&led, io_input_get(&sw8));

        if ((t_ms - last_status_ms) >= STATUS_PRINT_MS)
        {
            last_status_ms = t_ms;
            PRINTF("[%u ms] STATUS raw=%u deb=%u  raw_edges=%u deb_edges=%u\r\n",
                   (unsigned long)t_ms,
                   raw ? 1u : 0u,
                   io_input_get(&sw8) ? 1u : 0u,
                   (unsigned long)raw_edges,
                   (unsigned long)deb_edges);
        }
#endif

#if (ENABLE_DWT_TIMING != 0)
        uint32_t dt = dwt_cycles() - t0;
        PRINTF("poll dt = %u cycles\r\n", (unsigned long)dt);
#endif

        /* Maintain deterministic poll cadence */
        SDK_DelayAtLeastUs(POLL_PERIOD_US, cpu_hz());
        t_ms += POLL_PERIOD_MS;
    }
}
