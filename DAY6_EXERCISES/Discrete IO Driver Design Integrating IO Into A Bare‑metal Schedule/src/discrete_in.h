/*
 * discrete_in.h
 *
 *  Created on: 28 Dec 2025
 *      Author: Lenovo
 */

#ifndef DISCRETE_IN_H_
#define DISCRETE_IN_H_
#include <stdint.h>
#include <stdbool.h>

#include "fsl_gpio.h"

typedef struct
{
    GPIO_Type *gpio;
    uint32_t pin;
    bool activeHigh;

    /* integrator debounce */
    uint32_t count;
    uint32_t countMax;

    /* debounced state (logical asserted) */
    bool state;
    bool prev_state;
} dio_in_t;

void DIO_InInit(dio_in_t *in, GPIO_Type *gpio, uint32_t pin, bool activeHigh, uint32_t countMax);

/* sample raw pin, run debounce, and return true if debounced state changed */
bool DIO_InUpdate(dio_in_t *in);

/* read current debounced logical asserted state */
static inline bool DIO_InGet(const dio_in_t *in)
{
    return in->state;
}

/* returns the edge event type if changed; EVT_NONE if no change */
uint32_t DIO_InEdgeEvent(const dio_in_t *in);




#endif /* DISCRETE_IN_H_ */
