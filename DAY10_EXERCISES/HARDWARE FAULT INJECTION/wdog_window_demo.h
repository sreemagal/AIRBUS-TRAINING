#ifndef WDOG_WINDOW_DEMO_H_
#define WDOG_WINDOW_DEMO_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Demonstrates windowed RTWDOG:
 *  - normal refresh for N cycles
 *  - optional early refresh violation
 *  - optional missed refresh timeout
 */
void WdogWindowDemo_Run(bool triggerEarlyViolation, bool triggerMissedRefresh);

#ifdef __cplusplus
}
#endif

#endif /* WDOG_WINDOW_DEMO_H_ */
