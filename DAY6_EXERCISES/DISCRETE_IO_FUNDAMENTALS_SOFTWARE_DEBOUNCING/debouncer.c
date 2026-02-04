/*
 * debounce.c
 *
 *  Created on: 27 Dec 2025
 *      Author: Lenovo
 */
#include "debouncer.h"

void Debouncer_Init(debouncer_t *d, uint8_t max, uint8_t initial_raw)
{
    d->max = max;
    d->rose = 0u;
    d->fell = 0u;

    if (initial_raw)
    {
        d->count = max;
        d->state = 1u;
    }
    else
    {
        d->count = 0u;
        d->state = 0u;
    }
}

void Debouncer_Update(debouncer_t *d, uint8_t raw)
{
    d->rose = 0u;
    d->fell = 0u;

    /* Saturating integrator */
    if (raw)
    {
        if (d->count < d->max)
        {
            d->count++;
        }
    }
    else
    {
        if (d->count > 0u)
        {
            d->count--;
        }
    }

    /* Commit state only at the extremes */
    if ((d->count == d->max) && (d->state == 0u))
    {
        d->state = 1u;
        d->rose = 1u;
    }
    else if ((d->count == 0u) && (d->state == 1u))
    {
        d->state = 0u;
        d->fell = 1u;
    }
}

bool Debouncer_Rose(debouncer_t *d)
{
    bool v = (d->rose != 0u);
    d->rose = 0u;
    return v;
}

bool Debouncer_Fell(debouncer_t *d)
{
    bool v = (d->fell != 0u);
    d->fell = 0u;
    return v;
}


