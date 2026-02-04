/*
 * dio_irq.c
 *
 *  Created on: 29 Dec 2025
 *      Author: Lenovo
 */
#include "dio_irq.h"
#include "fsl_common.h"

static inline uint8_t phys_to_logical(uint8_t phys, bool activeHigh)
{
    if (activeHigh)
    {
        return (phys ? 1u : 0u);
    }
    else
    {
        return (phys ? 0u : 1u);
    }
}

void DIO_IrqInInit(dio_irq_in_t *ch,
                   GPIO_Type *gpio,
                   uint32_t pin,
                   bool activeHigh,
                   dio_edge_mode_t edgeMode,
                   uint32_t debounce_us,
                   uint8_t safe_default_level)
{
    ch->gpio = gpio;
    ch->pin = pin;
    ch->activeHigh = activeHigh;
    ch->edgeMode = edgeMode;
    ch->debounce_us = debounce_us;

    ch->armed = false;
    ch->t_irq_us = 0u;
    ch->last_irq_us = 0u;
    ch->raw_edges = 0u;

    ch->debounced_level = safe_default_level ? 1u : 0u;

    ch->fault_chatter = false;
    ch->last_accepted_us = 0u;
}

void DIO_IrqInArmFromISR(dio_irq_in_t *ch, uint64_t now_us)
{
    ch->raw_edges++;
    ch->t_irq_us = now_us;
    ch->last_irq_us = now_us;
    ch->armed = true;
}

void DIO_IrqInService(dio_irq_in_t *ch, uint64_t now_us, uint8_t channel_id, eventq_t *q)
{
    /* If not armed, nothing to do */
    uint32_t primask = DisableGlobalIRQ();
    bool armed = ch->armed;
    uint64_t t_irq = ch->t_irq_us;
    EnableGlobalIRQ(primask);

    if (!armed)
    {
        return;
    }

    /* Wait until quiet-time debounce passes */
    if ((now_us - t_irq) < (uint64_t)ch->debounce_us)
    {
        return;
    }

    /* Disarm (a later IRQ will re-arm) */
    primask = DisableGlobalIRQ();
    ch->armed = false;
    EnableGlobalIRQ(primask);

    /* Sample physical pad and map to logical */
    uint8_t phys = GPIO_PinReadPadStatus(ch->gpio, ch->pin);
    uint8_t logical = phys_to_logical(phys, ch->activeHigh);

    if (logical == ch->debounced_level)
    {
        return; /* no change */
    }

    /* Commit and push event */
    ch->debounced_level = logical;

    evt_t e;
    e.t_us = now_us;
    e.channel = channel_id;
    e.level = logical;
    e.type = (uint8_t)(logical ? EVT_DEBOUNCED_RISE : EVT_DEBOUNCED_FALL);

    (void)EventQ_Push(q, &e);
}


