/*
 * lab_config.h
 *
 *  Created on: 28 Dec 2025
 *      Author: Lenovo
 */

#ifndef LAB_CONFIG_H_
#define LAB_CONFIG_H_

#include <stdint.h>
#include <stdbool.h>

/* ----------------- App mode selection ----------------- */
#define LAB_MODE_GENERIC  (0)
#define LAB_MODE_AVIONICS (1)

#ifndef LAB_APP_MODE
#define LAB_APP_MODE LAB_MODE_AVIONICS
#endif

/* ----------------- Scheduler periods ----------------- */
#define TASK_EVENTPUMP_PERIOD_MS   (1u)
#define TASK_DISCRETEIN_PERIOD_MS  (5u)
#define TASK_APP_PERIOD_MS         (10u)
#define TASK_DISCRETEOUT_PERIOD_MS (10u)
#define TASK_HEARTBEAT_PERIOD_MS   (100u)

/* ----------------- Debounce for input channels -----------------
 * Integrator: count saturates [0..COUNT_MAX]
 * state commits to 1 when count==COUNT_MAX; commits to 0 when count==0.
 * With 5 ms sampling:
 *   COUNT_MAX=4 -> ~20 ms to assert/deassert (typical button)
 */
#define IN_COUNT_MAX      (4u)

/* ----------------- Long press lamp-test ----------------- */
#define LAMP_TEST_HOLD_MS (1000u)

/* ----------------- Avionics rules ----------------- */
#define WOW_QUALIFY_MS        (40u)
#define WOW_DISAGREE_LATCH_MS (500u)

/* LED blink (2 Hz) -> toggle every 250ms */
#define LED_BLINK_HALF_PERIOD_MS (250u)

/* ----------------- Event queue ----------------- */
#define EVENTQ_DEPTH (32u)

/* ----------------- Optional WOW_B channel pin mapping -----------------
 * Default: GPIO_AD_B0_10 -> GPIO1_IO10
 */
#ifndef LAB_ENABLE_WOW_B
#define LAB_ENABLE_WOW_B (1)
#endif

#define LAB_WOWB_GPIO      GPIO1
#define LAB_WOWB_PIN       (10u)
#define LAB_WOWB_IOMUXC    IOMUXC_GPIO_AD_B0_10_GPIO1_IO10

#endif /* LAB_CONFIG_H_ */
