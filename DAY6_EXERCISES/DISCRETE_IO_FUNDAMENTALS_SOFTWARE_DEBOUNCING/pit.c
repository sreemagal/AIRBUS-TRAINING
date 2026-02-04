/*
 * Discrete IO Fundamentals — Software Debouncing (EVKB-i.MX RT1050)
 *
 * Base example: driver_examples/pit
 *
 * Sampling: PIT @ 1kHz (1ms)
 * Debouncer: saturating up/down counter (integrator)
 *
 * Build modes:
 *   - Default (GENERIC): MAX=5ms, toggle LED once per press
 *   - Avionics (WOW): define LAB_MODE_AVIONICS=1, MAX=20ms, validating blink + spoiler simulation
 *
 * If SW8 polarity is opposite, define SW8_ACTIVE_LOW=0.
 */

#include <stdbool.h>
#include <stdint.h>

#include "fsl_debug_console.h"
#include "fsl_common.h"
#include "fsl_clock.h"
#include "fsl_gpio.h"
#include "fsl_iomuxc.h"
#include "fsl_pit.h"

#include "board.h"
#include "app.h"

#include "debouncer.h"

/* ------------------------- Build-time knobs ------------------------- */
#ifndef LAB_MODE_AVIONICS
#define LAB_MODE_AVIONICS (0)
#endif

#ifndef SW8_ACTIVE_LOW
#define SW8_ACTIVE_LOW (1) /* default on EVKB: pressed reads 0 */
#endif

/* PIT sampling */
#define SAMPLE_PERIOD_US (1000u) /* 1ms */

#if (LAB_MODE_AVIONICS != 0)
#define DEBOUNCE_MAX (200u)
#else
#define DEBOUNCE_MAX (5u)
#endif

#if (LAB_MODE_AVIONICS != 0)
#define AVIONICS_BLINK_TOGGLE_MS (50u)   /* demo-only: toggles every 50ms */
#endif

/* BIT / observability */
#define STUCK_WARN_MS (30000u)
#define STATUS_PRINT_MS (500u)

/* SW8: SNVS_WAKEUP -> GPIO5_IO00 */
#define SW8_GPIO GPIO5
#define SW8_PIN  (0u)
#define SW8_MUX  IOMUXC_SNVS_WAKEUP_GPIO5_IO00

/* ------------------------- PIT tick state ------------------------- */
static volatile uint32_t g_pitTicks = 0u;

void PIT_LED_HANDLER(void)
{
    PIT_ClearStatusFlags(DEMO_PIT_BASEADDR, DEMO_PIT_CHANNEL, kPIT_TimerFlag);
    g_pitTicks++; /* 1 tick == 1ms */
    SDK_ISR_EXIT_BARRIER;
}

static inline uint32_t ticks_ms(void)
{
    return g_pitTicks;
}

/* ------------------------- SW8 pad setup (SNVS) ------------------------- */
static void SW8_InitPadAndMux(void)
{
    /* Enable IOMUXC clocks (both domains) */
    CLOCK_EnableClock(kCLOCK_Iomuxc);
    CLOCK_EnableClock(kCLOCK_IomuxcSnvs);

    /* Mux SNVS_WAKEUP to GPIO5_IO00 */
    IOMUXC_SetPinMux(SW8_MUX, 0u);

    /* Configure SNVS pad:
     * - enable pull/keeper
     * - select pull-up
     * - enable hysteresis
     * - slow slew
     */
    uint32_t cfg = 0u;

    cfg |= IOMUXC_SNVS_SW_PAD_CTL_PAD_WAKEUP_PKE_MASK;
    cfg |= IOMUXC_SNVS_SW_PAD_CTL_PAD_WAKEUP_PUE_MASK; /* pull enable */
    cfg |= IOMUXC_SNVS_SW_PAD_CTL_PAD_WAKEUP_PUS(2u);  /* 100K pull-up typical */
    cfg |= IOMUXC_SNVS_SW_PAD_CTL_PAD_WAKEUP_HYS_MASK;
    cfg |= IOMUXC_SNVS_SW_PAD_CTL_PAD_WAKEUP_SRE(0u);  /* slow slew */

    /* Write the pad control register */
    IOMUXC_SNVS->SW_PAD_CTL_PAD_WAKEUP = cfg;
}

/* ------------------------- Raw read (logical) ------------------------- */
static uint8_t SW8_ReadLogical(void)
{
    /* Read pad status so we see the true input level */
    uint8_t pad = GPIO_PinReadPadStatus(SW8_GPIO, SW8_PIN);

#if (SW8_ACTIVE_LOW != 0)
    /* pressed -> 0 -> logical 1 */
    return (pad == 0u) ? 1u : 0u;
#else
    return (pad != 0u) ? 1u : 0u;
#endif
}

/* ------------------------- Application (avionics simulation) ------------------------- */
#if (LAB_MODE_AVIONICS != 0)
typedef enum
{
    SPOILER_INHIBIT = 0,
    SPOILER_DEPLOY  = 1
} spoiler_cmd_t;
#endif

int main(void)
{
    pit_config_t pitConfig;

    /* Board init from the imported PIT example */
    BOARD_InitHardware();

    /* Enable LED (GPIO1_IO09) */
    LED_INIT();

    /* Enable clocks for GPIO blocks used */
    CLOCK_EnableClock(kCLOCK_Gpio5);

    /* Ensure SW8 is properly muxed and has pull/hysteresis */
    SW8_InitPadAndMux();

    /* Init SW8 as digital input */
    gpio_pin_config_t sw8_in_cfg = {
        .direction = kGPIO_DigitalInput,
        .outputLogic = 0u,
        .interruptMode = kGPIO_NoIntmode,
    };
    GPIO_PinInit(SW8_GPIO, SW8_PIN, &sw8_in_cfg);

    /* Init PIT @ 1ms */
    PIT_GetDefaultConfig(&pitConfig);
    PIT_Init(DEMO_PIT_BASEADDR, &pitConfig);
    PIT_SetTimerPeriod(DEMO_PIT_BASEADDR, DEMO_PIT_CHANNEL,
                       USEC_TO_COUNT(SAMPLE_PERIOD_US, PIT_SOURCE_CLOCK));
    PIT_EnableInterrupts(DEMO_PIT_BASEADDR, DEMO_PIT_CHANNEL, kPIT_TimerInterruptEnable);
    EnableIRQ(PIT_IRQ_ID);
    PIT_StartTimer(DEMO_PIT_BASEADDR, DEMO_PIT_CHANNEL);

    /* Debouncer init */
    debouncer_t db;
    uint8_t raw0 = SW8_ReadLogical();
    Debouncer_Init(&db, (uint8_t)DEBOUNCE_MAX, raw0);

    /* Observability counters */
    uint32_t raw_edge_count = 0u;
    uint32_t debounced_edge_count = 0u;
    uint32_t max_count_reached_events = 0u;
    uint32_t last_raw_change_ms = 0u;

    uint8_t last_raw = raw0;
    uint32_t last_status_ms = 0u;

#if (LAB_MODE_AVIONICS != 0)
    spoiler_cmd_t spoiler = SPOILER_INHIBIT;
#endif

    PRINTF("\r\n=== Software Debouncing Lab (PIT 1kHz) ===\r\n");
#if (LAB_MODE_AVIONICS != 0)
    PRINTF("Mode: AVIONICS (WOW qualify)\r\n");
#else
    PRINTF("Mode: GENERIC (button debounce)\r\n");
#endif
    PRINTF("Sample period: %u us (1 tick = 1 ms)\r\n", (unsigned)SAMPLE_PERIOD_US);
    PRINTF("Debounce MAX: %u ticks => worst-case latency ~%u ms\r\n\r\n",
           (unsigned)DEBOUNCE_MAX, (unsigned)DEBOUNCE_MAX);

    /* Main loop processes every PIT tick (catch up if needed) */
    uint32_t processed = ticks_ms();

    while (true)
    {
        uint32_t nowTicks = ticks_ms();

        while (processed != nowTicks)
        {
            processed++; /* advance logical time by 1ms */
            uint32_t t_ms = processed;

            /* Sample raw */
            uint8_t raw = SW8_ReadLogical();

            /* Raw edge counter */
            if (raw != last_raw)
            {
                raw_edge_count++;
                last_raw_change_ms = t_ms;
                last_raw = raw;
            }

            /* Update debouncer */
            uint8_t prev_state = Debouncer_State(&db);
            Debouncer_Update(&db, raw);

            /* Count qualification events (state becomes 1 at MAX) */
            if ((prev_state == 0u) && (Debouncer_State(&db) == 1u))
            {
                max_count_reached_events++;
            }

            /* Consume edges (debounced) */
            if (Debouncer_Rose(&db))
            {
                debounced_edge_count++;
                PRINTF("[%u ms] DEBOUNCED RISE  raw=%u count=%u state=%u\r\n",
                       (unsigned long)t_ms, raw, db.count, db.state);

#if (LAB_MODE_AVIONICS != 0)
                /* WOW asserted -> deploy spoilers */
                if (spoiler != SPOILER_DEPLOY)
                {
                    spoiler = SPOILER_DEPLOY;
                    PRINTF("[%u ms] SPOILER_CMD=DEPLOY\r\n", (unsigned long)t_ms);
                }
#else
                /* Generic: toggle LED exactly once per press */
                LED_TOGGLE();
#endif
            }

            if (Debouncer_Fell(&db))
            {
                debounced_edge_count++;
                PRINTF("[%u ms] DEBOUNCED FALL  raw=%u count=%u state=%u\r\n",
                       (unsigned long)t_ms, raw, db.count, db.state);

#if (LAB_MODE_AVIONICS != 0)
                /* WOW deasserted -> inhibit spoilers */
                if (spoiler != SPOILER_INHIBIT)
                {
                    spoiler = SPOILER_INHIBIT;
                    PRINTF("[%u ms] SPOILER_CMD=INHIBIT\r\n", (unsigned long)t_ms);
                }
#endif
            }

#if (LAB_MODE_AVIONICS != 0)
            /* LED behavior for avionics mode:
             * - validating assertion: state=0 and 0<count<MAX  => blink 2 Hz
             * - qualified asserted: state=1 => ON
             * - deasserted: state=0 and count==0 => OFF
             */
            if ((db.state == 0u) && (db.count > 0u) && (db.count < db.max))
            {
                /* 2 Hz blink: toggle every 250ms */
        //        bool on = ((t_ms / 250u) & 1u) ? true : false;
            	bool on = ((t_ms / AVIONICS_BLINK_TOGGLE_MS) & 1u) ? true : false;

                if (on)
                {
                    USER_LED_ON();
                }
                else
                {
                    USER_LED_OFF();
                }
            }
            else if (db.state != 0u)
            {
                USER_LED_ON();
            }
            else
            {
                USER_LED_OFF();
            }
#endif

            /* Periodic status line (every 500ms) */
            if ((t_ms - last_status_ms) >= STATUS_PRINT_MS)
            {
                last_status_ms = t_ms;

                bool stuck_warn = ((t_ms - last_raw_change_ms) >= STUCK_WARN_MS);

                PRINTF("[%u ms] STATUS raw=%u count=%u state=%u  raw_edges=%u deb_edges=%u max_events=%u stuck_warn=%u\r\n",
                       (unsigned long)t_ms,
                       raw,
                       db.count,
                       db.state,
                       (unsigned long)raw_edge_count,
                       (unsigned long)debounced_edge_count,
                       (unsigned long)max_count_reached_events,
                       stuck_warn ? 1u : 0u);
            }
        }

        /* Let CPU sleep until next interrupt (keeps jitter low) */
       SDK_DelayAtLeastUs(1000u, CLOCK_GetFreq(kCLOCK_CpuClk));

    }
}
