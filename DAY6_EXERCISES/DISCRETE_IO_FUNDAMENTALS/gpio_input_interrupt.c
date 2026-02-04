/*
 * Discrete I/O Fundamentals Lab — EVKB-i.MX RT1050
 *
 * Base project: boards/evkbimxrt1050/driver_examples/gpio/input_interrupt
 *
 * Roles:
 *   - Default: LRU (reads WOW from SW8 and drives shared FLT/GRD open-drain line)
 *   - Define DISCRETE_LAB_ROLE_MONITOR=1 to build MONITOR role (reads shared line only)
 */

#include <stdbool.h>
#include <stdint.h>

#include "fsl_common.h"
#include "fsl_debug_console.h"
#include "fsl_clock.h"
#include "fsl_gpio.h"
#include "fsl_iomuxc.h"
#include "fsl_device_registers.h"

#include "board.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "app.h" /* EXAMPLE_SW_GPIO, EXAMPLE_SW_GPIO_PIN, EXAMPLE_SW_IRQ, ... */

/*******************************************************************************
 * Lab configuration
 ******************************************************************************/

/* WOW (SW8) polarity: most EVK boards use active-low button. */
#define WOW_ACTIVE_LOW 1u

/* Filtering parameters */
#define WOW_SAMPLE_PERIOD_MS        (1u)
#define WOW_HISTORY_LEN             (5u)   /* 5 samples (5 ms window) */
#define WOW_MAJORITY_THRESHOLD      (3u)   /* 3-of-5 */
#define WOW_DEBOUNCE_MS             (5u)   /* candidate must be stable for >= 5 ms */

/* BIT / diagnostics */
#define WOW_NO_ACTIVITY_WARN_MS     (30000u) /* warn if no raw edges for 30 s */
#define LINE_ASSERT_VERIFY_MS       (2u)     /* if we assert, line must go low within 2 ms */

/* Shared FLT/GRD line pin (Arduino/SD/SPI header region): GPIO_SD_B0_00 -> GPIO3_IO12 */
#define FLTGRD_IOMUXC_MUX           IOMUXC_GPIO_SD_B0_00_GPIO3_IO12
#define FLTGRD_GPIO                 GPIO3
#define FLTGRD_PIN                  (12u)

/* FLT/GRD electrical convention: active-low line */
#define FLTGRD_ASSERT_DRIVE_LEVEL   (0u) /* drive low */
#define FLTGRD_RELEASE_LEVEL        (1u) /* open-drain releases when output is 1 */

/* Event log depth */
#define EVENT_LOG_DEPTH             (64u)

/*******************************************************************************
 * Time base (SysTick @ 1ms)
 ******************************************************************************/

static volatile uint32_t g_msTicks = 0u;

void SysTick_Handler(void)
{
    g_msTicks++;
}

static inline uint32_t millis(void)
{
    return g_msTicks;
}

/*******************************************************************************
 * Event log
 ******************************************************************************/

typedef enum
{
    EVT_WOW_RAW = 0,
    EVT_WOW_FILT,
    EVT_FLTGRD_LINE,
    EVT_FLTGRD_CMD,
} event_id_t;

typedef struct
{
    uint32_t t_ms;
    uint8_t id;
    uint8_t value;
} event_t;

static event_t g_evt[EVENT_LOG_DEPTH];
static volatile uint32_t g_evt_wr = 0u;
static uint32_t g_evt_rd = 0u;

static void log_event(event_id_t id, uint8_t value)
{
    uint32_t i = g_evt_wr % EVENT_LOG_DEPTH;
    g_evt[i].t_ms = millis();
    g_evt[i].id = (uint8_t)id;
    g_evt[i].value = value;
    g_evt_wr++;
}

static const char *event_name(event_id_t id)
{
    switch (id)
    {
        case EVT_WOW_RAW: return "WOW_RAW";
        case EVT_WOW_FILT: return "WOW_FILT";
        case EVT_FLTGRD_LINE: return "FLTGRD_LINE";
        case EVT_FLTGRD_CMD: return "FLTGRD_CMD";
        default: return "?";
    }
}

static void flush_events(void)
{
    while (g_evt_rd != g_evt_wr)
    {
        event_t e = g_evt[g_evt_rd % EVENT_LOG_DEPTH];
        g_evt_rd++;
        PRINTF("[%u ms] %s = %u\r\n",
               (unsigned)e.t_ms,
               event_name((event_id_t)e.id),
               (unsigned)e.value);


    }
}

/*******************************************************************************
 * Discrete pad configuration helpers
 ******************************************************************************/

static void configure_wow_pad_snvs_wakeup(void)
{
    /* SW8 uses SNVS_WAKEUP pin -> GPIO5_IO00.
     * Enable:
     *  - Pull/keeper + Pull-up
     *  - Hysteresis (Schmitt trigger)
     */
    CLOCK_EnableClock(kCLOCK_IomuxcSnvs);

    const uint32_t cfg =
        IOMUXC_SNVS_SW_PAD_CTL_PAD_WAKEUP_PKE_MASK |
        IOMUXC_SNVS_SW_PAD_CTL_PAD_WAKEUP_PUE(1u) |
        /* Pull-up strength: 0b10 = 100K pull-up, 0b01 = 47K, 0b11 = 22K */
        IOMUXC_SNVS_SW_PAD_CTL_PAD_WAKEUP_PUS(2u) |
        IOMUXC_SNVS_SW_PAD_CTL_PAD_WAKEUP_HYS_MASK;

    IOMUXC_SetPinConfig(IOMUXC_SNVS_WAKEUP_GPIO5_IO00, cfg);
}

static void configure_fltgrd_pad_open_drain(void)
{
    /* Configure the shared line pin as GPIO (ALT5) and set pad as:
     *  - Open-drain
     *  - Pull-up enabled (weak internal)
     *  - Hysteresis enabled
     *  - Slow slew, low speed, moderate drive
     */
    CLOCK_EnableClock(kCLOCK_Iomuxc);

    IOMUXC_SetPinMux(FLTGRD_IOMUXC_MUX, 1u);   // SION=1: force input path enabled

    const uint32_t cfg =
        IOMUXC_SW_PAD_CTL_PAD_ODE_MASK |
        IOMUXC_SW_PAD_CTL_PAD_PKE_MASK |
        IOMUXC_SW_PAD_CTL_PAD_PUE(1u) |
        IOMUXC_SW_PAD_CTL_PAD_PUS(2u) |
        IOMUXC_SW_PAD_CTL_PAD_HYS_MASK |
        IOMUXC_SW_PAD_CTL_PAD_SRE(0u) |
        IOMUXC_SW_PAD_CTL_PAD_SPEED(0u) |
        IOMUXC_SW_PAD_CTL_PAD_DSE(2u);

    IOMUXC_SetPinConfig(FLTGRD_IOMUXC_MUX, cfg);
}

/*******************************************************************************
 * WOW filtering (5-sample majority + debounce)
 ******************************************************************************/

typedef struct
{
    uint8_t raw;
    uint8_t filt;
    uint8_t candidate;
    uint8_t history; /* lower 5 bits used */
    uint32_t candidate_since_ms;
    uint32_t last_raw_edge_ms;
} wow_filter_t;

static uint8_t popcount5(uint8_t x)
{
    /* Count bits in lower 5 bits */
    x &= 0x1Fu;
    uint8_t c = 0u;
    for (uint8_t i = 0u; i < 5u; i++)
    {
        c += (x >> i) & 1u;
    }
    return c;
}

static uint8_t wow_read_logical(void)
{
    /* Use PAD status to avoid reading output DR. */
    uint8_t pad = GPIO_PinReadPadStatus(EXAMPLE_SW_GPIO, EXAMPLE_SW_GPIO_PIN);
#if WOW_ACTIVE_LOW
    return (pad == 0u) ? 1u : 0u;
#else
    return (pad != 0u) ? 1u : 0u;
#endif
}

static void wow_filter_init(wow_filter_t *f, uint8_t initial, uint32_t now_ms)
{
    f->raw = initial;
    f->filt = initial;
    f->candidate = initial;
    f->history = initial ? 0x1Fu : 0x00u;
    f->candidate_since_ms = now_ms;
    f->last_raw_edge_ms = now_ms;
}

static bool wow_filter_update(wow_filter_t *f, uint8_t new_raw, uint32_t now_ms)
{
    bool filt_changed = false;

    if (new_raw != f->raw)
    {
        f->raw = new_raw;
        f->last_raw_edge_ms = now_ms;
        log_event(EVT_WOW_RAW, new_raw);
    }

    /* Shift in raw sample */
    f->history = (uint8_t)(((uint8_t)(f->history << 1) | (new_raw & 1u)) & 0x1Fu);

    /* Majority */
    uint8_t ones = popcount5(f->history);
    uint8_t maj = (ones >= WOW_MAJORITY_THRESHOLD) ? 1u : 0u;

    if (maj != f->candidate)
    {
        f->candidate = maj;
        f->candidate_since_ms = now_ms;
    }

    if ((f->candidate != f->filt) && ((now_ms - f->candidate_since_ms) >= WOW_DEBOUNCE_MS))
    {
        f->filt = f->candidate;
        filt_changed = true;
        log_event(EVT_WOW_FILT, f->filt);
    }

    return filt_changed;
}

/*******************************************************************************
 * FLT/GRD line helpers
 ******************************************************************************/

static uint8_t fltgrd_read_line_asserted(void)
{
    /* Active-low asserted. Use PAD status to observe true wired-OR line. */
    uint8_t pad = GPIO_PinReadPadStatus(FLTGRD_GPIO, FLTGRD_PIN);
    return (pad == 0u) ? 1u : 0u;
}

static void fltgrd_drive_assert(bool assert)
{
    if (assert)
    {
        GPIO_PinWrite(FLTGRD_GPIO, FLTGRD_PIN, FLTGRD_ASSERT_DRIVE_LEVEL);
        log_event(EVT_FLTGRD_CMD, 1u);
    }
    else
    {
        GPIO_PinWrite(FLTGRD_GPIO, FLTGRD_PIN, FLTGRD_RELEASE_LEVEL);
        log_event(EVT_FLTGRD_CMD, 0u);
    }
}

/*******************************************************************************
 * Interrupt (SW8) — used as an edge indicator only
 ******************************************************************************/

static volatile bool g_wow_irq_seen = false;

void EXAMPLE_GPIO_IRQHandler(void)
{
    GPIO_PortClearInterruptFlags(EXAMPLE_SW_GPIO, 1u << EXAMPLE_SW_GPIO_PIN);
    g_wow_irq_seen = true;
    __DSB();
}

/*******************************************************************************
 * Main
 ******************************************************************************/

int main(void)
{
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();

    /* 1ms time base */
    uint32_t coreHz = CLOCK_GetFreq(kCLOCK_CoreSysClk);
    (void)SysTick_Config(coreHz / 1000u);

    /* Configure pads for Discrete IO robustness */
    configure_wow_pad_snvs_wakeup();
    configure_fltgrd_pad_open_drain();



    /* Init GPIO clocks */
    CLOCK_EnableClock(kCLOCK_Gpio1);
    CLOCK_EnableClock(kCLOCK_Gpio3);
    CLOCK_EnableClock(kCLOCK_Gpio5);
    /* Init LED */
       USER_LED_INIT(1u);
       USER_LED_OFF();

    /* WOW input pin init */
    gpio_pin_config_t wow_in_cfg = {
        .direction = kGPIO_DigitalInput,
        .outputLogic = 0u,
        .interruptMode = kGPIO_IntRisingOrFallingEdge,
    };
    GPIO_PinInit(EXAMPLE_SW_GPIO, EXAMPLE_SW_GPIO_PIN, &wow_in_cfg);

    /* Enable and attach IRQ */
    GPIO_PortEnableInterrupts(EXAMPLE_SW_GPIO, 1u << EXAMPLE_SW_GPIO_PIN);
    EnableIRQ(EXAMPLE_SW_IRQ);

#if defined(DISCRETE_LAB_ROLE_MONITOR) && (DISCRETE_LAB_ROLE_MONITOR != 0)
    const bool isMonitor = true;
#else
    const bool isMonitor = false;
#endif

    /* FLT/GRD pin init depends on role */
    if (isMonitor)
    {
        gpio_pin_config_t line_in_cfg = {
            .direction = kGPIO_DigitalInput,
            .outputLogic = 0u,
            .interruptMode = kGPIO_NoIntmode,
        };
        GPIO_PinInit(FLTGRD_GPIO, FLTGRD_PIN, &line_in_cfg);
    }
    else
    {
        gpio_pin_config_t line_out_cfg = {
            .direction = kGPIO_DigitalOutput,
            .outputLogic = FLTGRD_RELEASE_LEVEL, /* release by default */
            .interruptMode = kGPIO_NoIntmode,
        };
        GPIO_PinInit(FLTGRD_GPIO, FLTGRD_PIN, &line_out_cfg);
        fltgrd_drive_assert(false);
    }

    PRINTF("\r\n=== Discrete IO Lab: WOW + FLT/GRD (open-drain) ===\r\n");
    PRINTF("Role: %s\r\n", isMonitor ? "MONITOR" : "LRU");
    PRINTF("WOW source: %s (GPIO5 pin0 / SNVS_WAKEUP)\r\n", EXAMPLE_SW_NAME);
    PRINTF("FLT/GRD line: GPIO3_IO12 (GPIO_SD_B0_00) open-drain active-low\r\n");
    PRINTF("Filter: %u-of-%u majority + %u ms debounce\r\n\r\n",
           (unsigned)WOW_MAJORITY_THRESHOLD, (unsigned)WOW_HISTORY_LEN, (unsigned)WOW_DEBOUNCE_MS);

    /* Initialize filter */
    uint32_t now = millis();
    wow_filter_t wow;
    wow_filter_init(&wow, wow_read_logical(), now);
    log_event(EVT_WOW_RAW, wow.raw);
    log_event(EVT_WOW_FILT, wow.filt);

    /* Track FLT/GRD line edges */
    uint8_t last_line = fltgrd_read_line_asserted();
    log_event(EVT_FLTGRD_LINE, last_line);

    /* BIT state */
    bool bit_wow_no_activity = false;
    bool bit_line_not_low_on_assert = false;
    uint32_t line_assert_cmd_time = 0u;
    bool line_cmd_asserted = false;

    uint32_t last_print = now;

    for (;;)
    {
        /* Run at 1ms cadence (SysTick) */
        uint32_t t = millis();

        /* WOW filter update */
        (void)wow_filter_update(&wow, wow_read_logical(), t);

        /* Control logic (LRU only): WOW asserted -> assert FLT/GRD */
        if (!isMonitor)
        {
            bool cmd = (wow.filt != 0u);
            if (cmd != line_cmd_asserted)
            {
                line_cmd_asserted = cmd;
                if (line_cmd_asserted)
                {
                    line_assert_cmd_time = t;
                }
                fltgrd_drive_assert(line_cmd_asserted);
            }

            /* BIT: if we assert, the *pad* must read low shortly after (wired-OR must go low). */
            if (line_cmd_asserted)
            {
                if ((t - line_assert_cmd_time) >= LINE_ASSERT_VERIFY_MS)
                {
                    uint8_t line = fltgrd_read_line_asserted();
                    if (line == 0u)
                    {
                        bit_line_not_low_on_assert = true;
                    }
                }
            }
        }

        /* Track line edges (all roles) */
        uint8_t line_now = fltgrd_read_line_asserted();
        if (line_now != last_line)
        {
            last_line = line_now;
            log_event(EVT_FLTGRD_LINE, line_now);
        }

        /* BIT: WOW activity monitor (warning-level) */
        if ((t - wow.last_raw_edge_ms) >= WOW_NO_ACTIVITY_WARN_MS)
        {
            bit_wow_no_activity = true;
        }
        else
        {
            bit_wow_no_activity = false;
        }

        /* LED indicates WOW filtered state */
        if (wow.filt != 0u)
        {
            USER_LED_ON();
        }
        else
        {
            USER_LED_OFF();
        }

        /* Periodic status print */
        if ((t - last_print) >= 1000u)
        {
            last_print = t;

            PRINTF("\r\n--- STATUS @ %u ms ---\r\n", (unsigned long)t);
            PRINTF("WOW_RAW=%u WOW_FILT=%u  LINE=%u  IRQ_SEEN=%u\r\n",
                   wow.raw, wow.filt, line_now, g_wow_irq_seen ? 1u : 0u);
            PRINTF("BIT: wow_no_activity=%u  line_not_low_on_assert=%u\r\n",
                   bit_wow_no_activity ? 1u : 0u,
                   bit_line_not_low_on_assert ? 1u : 0u);

            flush_events();

            /* Clear edge marker */
            g_wow_irq_seen = false;
        }

        /* Optional: */
       SDK_DelayAtLeastUs(1000u, CLOCK_GetFreq(kCLOCK_CpuClk));
    }
}
