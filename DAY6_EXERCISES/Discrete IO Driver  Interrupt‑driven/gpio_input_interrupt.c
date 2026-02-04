#include <stdbool.h>
#include <stdint.h>

#include "fsl_common.h"
#include "fsl_gpio.h"
#include "fsl_pit.h"
#include "fsl_debug_console.h"

#include "app.h"
#include "board.h"
#include "pin_mux.h"

#include "dio_timebase_pit.h"
#include "eventq.h"
#include "dio_irq.h"

/* ---------------- Lab knobs ---------------- */
#define LAB_AVIONICS_MODE   (1)   /* 1: WOW + chatter fault logic; 0: basic debounce demo */

#if (LAB_AVIONICS_MODE)
#define LAB_DEBOUNCE_US     (50000u)  /* 50 ms */
#else
#define LAB_DEBOUNCE_US     (20000u)  /* 20 ms */
#endif

#define CHATTER_WINDOW_US   (10000u)   /* <10 ms between accepted transitions => fault */
#define FAULT_HOLDOFF_US    (100000u)  /* 100 ms stable (no IRQ) to clear fault */

#define EVENTQ_DEPTH        (32u)

/* Channel IDs */
enum { CH_SW8 = 0 };

/* Event queue */
static evt_t g_evt_storage[EVENTQ_DEPTH];
static eventq_t g_evtq;

/* Discrete input channel */
static dio_irq_in_t g_sw8;

/* Avionics WOW state */
static bool g_wow_ground = false; /* 1=GROUND, 0=AIR */
static bool g_fault_chatter = false;

/* Counters */
static uint32_t g_debounced_edges = 0u;

/* ---------------- GPIO ISR ----------------
 * ISR hygiene: clear flag, timestamp, arm. No prints.
 */
void EXAMPLE_GPIO_IRQHandler(void)
{
    uint32_t flags = GPIO_PortGetInterruptFlags(EXAMPLE_SW_GPIO);
    uint32_t mask = (1u << EXAMPLE_SW_GPIO_PIN);

    if ((flags & mask) != 0u)
    {
        GPIO_PortClearInterruptFlags(EXAMPLE_SW_GPIO, mask);

        uint64_t now_us = DIO_TimeNowUs_PIT();
        DIO_IrqInArmFromISR(&g_sw8, now_us);
    }

    SDK_ISR_EXIT_BARRIER;
}

static inline void LED_Init(void)
{
    USER_LED_INIT(LOGIC_LED_OFF);
}

static inline void LED_Set(bool on)
{
    if (on)
    {
        GPIO_PinWrite(BOARD_USER_LED_GPIO, BOARD_USER_LED_GPIO_PIN, LOGIC_LED_ON);
    }
    else
    {
        GPIO_PinWrite(BOARD_USER_LED_GPIO, BOARD_USER_LED_GPIO_PIN, LOGIC_LED_OFF);
    }
}

static inline void LED_Toggle(void)
{
    USER_LED_TOGGLE();
}

int main(void)
{
    /* Input config: both-edge interrupts */
    gpio_pin_config_t sw_config = {
        .direction = kGPIO_DigitalInput,
        .outputLogic = 0,
        .interruptMode = kGPIO_IntRisingOrFallingEdge,
    };

    BOARD_InitHardware();

    PRINTF("\r\n=== Interrupt-driven Discrete IO Driver Lab ===\r\n");
    PRINTF("Button: %s  GPIO base=%p pin=%u\r\n", EXAMPLE_SW_NAME, EXAMPLE_SW_GPIO, (unsigned)EXAMPLE_SW_GPIO_PIN);
    PRINTF("Mode: %s\r\n", LAB_AVIONICS_MODE ? "AVIONICS (WOW + chatter fault)" : "BASIC (debounce + events)");
    PRINTF("Debounce: %u us\r\n", (unsigned)LAB_DEBOUNCE_US);

    /* Init LED output (safe default OFF) */
    LED_Init();
    LED_Set(false);

    /* Init PIT timebase */
    DIO_TimebaseInit_PIT();
    PRINTF("Timebase: PIT clk=%u Hz\r\n", (unsigned long)DIO_TimebaseClockHz_PIT());

    /* Init event queue */
    EventQ_Init(&g_evtq, g_evt_storage, EVENTQ_DEPTH);

    /* Init discrete input driver:
     * SW8 on EVKB is typically active-low (pressed reads 0).
     * We map pressed->logical 1 by setting activeHigh=false.
     * Power-up safe default for avionics WOW: AIR (0) until stable ground seen.
     */
    DIO_IrqInInit(&g_sw8,
                 EXAMPLE_SW_GPIO,
                 EXAMPLE_SW_GPIO_PIN,
                 false, /* activeHigh */
                 DIO_EDGE_BOTH,
                 LAB_DEBOUNCE_US,
                 0u /* safe default logical level */);

    /* Configure the GPIO pin and IRQ */
    EnableIRQ(EXAMPLE_SW_IRQ);
    GPIO_PinInit(EXAMPLE_SW_GPIO, EXAMPLE_SW_GPIO_PIN, &sw_config);
    GPIO_PortClearInterruptFlags(EXAMPLE_SW_GPIO, 1u << EXAMPLE_SW_GPIO_PIN);
    GPIO_PortEnableInterrupts(EXAMPLE_SW_GPIO, 1u << EXAMPLE_SW_GPIO_PIN);

    /* IMPORTANT: to detect a stable level that exists at boot (no edge),
     * arm one “virtual” debounce evaluation at startup.
     */
    DIO_IrqInArmFromISR(&g_sw8, DIO_TimeNowUs_PIT());

    uint64_t last_status_us = 0u;

    while (1)
    {
        uint64_t now_us = DIO_TimeNowUs_PIT();

        /* Service debounce (deferred from ISR). */
        if (!g_fault_chatter)
        {
            DIO_IrqInService(&g_sw8, now_us, CH_SW8, &g_evtq);
        }
        else
        {
            /* Fault holdoff: clear only after 100ms with no IRQ activity */
            uint64_t last_irq = g_sw8.last_irq_us;
            if ((now_us - last_irq) >= FAULT_HOLDOFF_US)
            {
                g_fault_chatter = false;
                evt_t fe = { .t_us = now_us, .type = (uint8_t)EVT_FAULT_CHATTER_CLEARED, .channel = CH_SW8, .level = g_sw8.debounced_level };
                (void)EventQ_Push(&g_evtq, &fe);

                /* Resync: force a fresh evaluation after clearing */
                DIO_IrqInArmFromISR(&g_sw8, now_us);
            }
        }

        /* Consume events */
        evt_t e;
        while (EventQ_Pop(&g_evtq, &e))
        {
            switch ((evt_type_t)e.type)
            {
                case EVT_DEBOUNCED_RISE:
                case EVT_DEBOUNCED_FALL:
                {
                    g_debounced_edges++;

#if (LAB_AVIONICS_MODE)
                    /* WOW: logical 1 = GROUND, logical 0 = AIR */
                    bool new_ground = (e.level != 0u);

                    /* Chatter detection: accepted transitions too close together */
                    if (g_sw8.last_accepted_us != 0u)
                    {
                        uint64_t dt = e.t_us - g_sw8.last_accepted_us;
                        if (dt < CHATTER_WINDOW_US)
                        {
                            g_fault_chatter = true;
                            evt_t fe = { .t_us = e.t_us, .type = (uint8_t)EVT_FAULT_CHATTER_LATCHED, .channel = CH_SW8, .level = e.level };
                            (void)EventQ_Push(&g_evtq, &fe);
                            break;
                        }
                    }
                    g_sw8.last_accepted_us = e.t_us;

                    g_wow_ground = new_ground;
                    PRINTF("[%10llu us] WOW=%s  (debounced)\r\n",
                           (unsigned long long)e.t_us,
                           g_wow_ground ? "GROUND" : "AIR");

                    /* For avionics demo: toggle LED once per GROUND transition */
                    if (e.type == EVT_DEBOUNCED_RISE)
                    {
                        LED_Toggle();
                    }
#else
                    PRINTF("[%10llu us] DEBOUNCED %s  level=%u\r\n",
                           (unsigned long long)e.t_us,
                           (e.type == EVT_DEBOUNCED_RISE) ? "RISE" : "FALL",
                           e.level);

                    /* Basic demo: toggle LED on debounced press (rise) */
                    if (e.type == EVT_DEBOUNCED_RISE)
                    {
                        LED_Toggle();
                    }
#endif
                    break;
                }

                case EVT_FAULT_CHATTER_LATCHED:
                    PRINTF("[%10llu us] FAULT_CHATTER LATCHED (accepted transitions <10ms)\r\n",
                           (unsigned long long)e.t_us);
                    LED_Set(true); /* steady ON in fault */
                    break;

                case EVT_FAULT_CHATTER_CLEARED:
                    PRINTF("[%10llu us] FAULT_CHATTER CLEARED (>=100ms stable)\r\n",
                           (unsigned long long)e.t_us);
                    LED_Set(false);
                    break;

                default:
                    break;
            }
        }

        /* Periodic status (every 500ms) */
        if ((now_us - last_status_us) >= 500000ull)
        {
            last_status_us = now_us;
            PRINTF("[%10llu us] STATUS raw_edges=%u deb_edges=%u dropped=%u fault=%u deb_level=%u\r\n",
                   (unsigned long long)now_us,
                   (unsigned long)DIO_IrqInRawEdgeCount(&g_sw8),
                   (unsigned long)g_debounced_edges,
                   (unsigned long)g_evtq.dropped,
                   g_fault_chatter ? 1u : 0u,
                   (unsigned)DIO_IrqInDebouncedLevel(&g_sw8));
        }

        /* Small cooperative delay to allow debounce windows to elapse.
         * We cannot use pure __WFI() here because debounce expiry may occur
         * after the last bounce IRQ (no wake source). This 1ms delay keeps
         * CPU usage low but guarantees debounce servicing.
         */
        SDK_DelayAtLeastUs(1000u, CLOCK_GetFreq(kCLOCK_CpuClk));
    }
}
