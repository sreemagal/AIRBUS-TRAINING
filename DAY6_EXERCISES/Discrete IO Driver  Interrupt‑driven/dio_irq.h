/*
 * dio_irq.h
 *
 *  Created on: 29 Dec 2025
 *      Author: Lenovo
 */

#ifndef DIO_IRQ_H_
#define DIO_IRQ_H_
#include <stdint.h>
#include <stdbool.h>

#include "fsl_gpio.h"
#include "eventq.h"

typedef enum
{
    DIO_EDGE_RISE = 0,
    DIO_EDGE_FALL,
    DIO_EDGE_BOTH,
} dio_edge_mode_t;

typedef struct
{
    GPIO_Type *gpio;
    uint32_t pin;
    bool activeHigh;           /* physical->logical mapping */
    dio_edge_mode_t edgeMode;  /* informational (HW sets actual edge mode) */

    uint32_t debounce_us;      /* quiet-time debounce window */

    /* ISR-updated */
    volatile bool armed;
    volatile uint64_t t_irq_us;     /* last edge timestamp */
    volatile uint64_t last_irq_us;  /* last IRQ time (for stable-holdoff) */
    volatile uint32_t raw_edges;

    /* Debounced state */
    uint8_t debounced_level;   /* logical 0/1 */

    /* Chatter fault support (avionics) */
    bool fault_chatter;
    uint64_t last_accepted_us;
} dio_irq_in_t;

void DIO_IrqInInit(dio_irq_in_t *ch,
                   GPIO_Type *gpio,
                   uint32_t pin,
                   bool activeHigh,
                   dio_edge_mode_t edgeMode,
                   uint32_t debounce_us,
                   uint8_t safe_default_level);

/* Call from GPIO ISR when this pin generated an interrupt. */
void DIO_IrqInArmFromISR(dio_irq_in_t *ch, uint64_t now_us);

/* Service debounce in main context. Pushes debounced edge events into q.
 * If avionics chatter logic is enabled in the application, it can use:
 *   ch->fault_chatter, ch->last_irq_us, ch->last_accepted_us
 */
void DIO_IrqInService(dio_irq_in_t *ch, uint64_t now_us, uint8_t channel_id, eventq_t *q);

static inline uint32_t DIO_IrqInRawEdgeCount(const dio_irq_in_t *ch) { return ch->raw_edges; }
static inline uint8_t  DIO_IrqInDebouncedLevel(const dio_irq_in_t *ch) { return ch->debounced_level; }




#endif /* DIO_IRQ_H_ */
