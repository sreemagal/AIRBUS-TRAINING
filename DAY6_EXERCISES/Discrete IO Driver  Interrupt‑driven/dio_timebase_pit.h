/*
 * dio_timebase_pit.h
 *
 *  Created on: 29 Dec 2025
 *      Author: Lenovo
 */

#ifndef DIO_TIMEBASE_PIT_H_
#define DIO_TIMEBASE_PIT_H_

#include <stdint.h>

/* Initialize PIT channel 0 as a free-running downcounter for timestamps. */
void DIO_TimebaseInit_PIT(void);

/* Monotonic timestamp (microseconds). */
uint64_t DIO_TimeNowUs_PIT(void);

/* Returns PIT clock used for conversion (Hz). */
uint32_t DIO_TimebaseClockHz_PIT(void);


#endif /* DIO_TIMEBASE_PIT_H_ */
