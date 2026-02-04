#include "discrete_in.h"

static inline bool raw_to_logical(uint32_t raw, bool activeHigh)
{
    bool level = (raw != 0u);
    return activeHigh ? level : !level;
}

void DIO_InInit(dio_in_t *in, GPIO_Type *gpio, uint32_t pin, bool activeHigh, uint32_t countMax)
{
    in->gpio = gpio;
    in->pin = pin;
    in->activeHigh = activeHigh;

    in->count = 0u;
    in->countMax = countMax;

    /* Initialize state from current pin */
    uint32_t raw = GPIO_PinReadPadStatus(gpio, pin);
    bool logical = raw_to_logical(raw, activeHigh);

    in->state = logical;
    in->prev_state = logical;

    /* Initialize integrator count consistent with state */
    in->count = logical ? countMax : 0u;
}

bool DIO_InUpdate(dio_in_t *in)
{
    uint32_t raw = GPIO_PinReadPadStatus(in->gpio, in->pin);
    bool logical = raw_to_logical(raw, in->activeHigh);

    /* Integrator update */
    if (logical)
    {
        if (in->count < in->countMax)
        {
            in->count++;
        }
    }
    else
    {
        if (in->count > 0u)
        {
            in->count--;
        }
    }

    in->prev_state = in->state;

    /* Commit state at endpoints */
    if (in->count == 0u)
    {
        in->state = false;
    }
    else if (in->count >= in->countMax)
    {
        in->state = true;
    }

    return (in->state != in->prev_state);
}

uint32_t DIO_InEdgeEvent(const dio_in_t *in)
{
    if (in->state == in->prev_state)
    {
        return 0u; /* EVT_NONE */
    }

    return in->state ? 1u : 2u; /* EVT_EDGE_RISE / EVT_EDGE_FALL (mapped by caller) */
}
/*
 * discrete_in.c
 *
 *  Created on: 28 Dec 2025
 *      Author: Lenovo
 */


