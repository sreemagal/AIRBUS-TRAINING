/*
 * eventq.h
 *
 *  Created on: 29 Dec 2025
 *      Author: Lenovo
 */

#ifndef EVENTQ_H_
#define EVENTQ_H_
#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    EVT_NONE = 0,
    EVT_DEBOUNCED_RISE,
    EVT_DEBOUNCED_FALL,
    EVT_FAULT_CHATTER_LATCHED,
    EVT_FAULT_CHATTER_CLEARED,
} evt_type_t;

typedef struct
{
    uint64_t t_us;
    uint8_t type;     /* evt_type_t */
    uint8_t channel;  /* channel id */
    uint8_t level;    /* logical level 0/1 */
} evt_t;

typedef struct
{
    evt_t *buf;
    uint32_t depth;
    volatile uint32_t wr;
    volatile uint32_t rd;
    volatile uint32_t dropped; /* drop-newest policy */
} eventq_t;

void EventQ_Init(eventq_t *q, evt_t *storage, uint32_t depth);
bool EventQ_Push(eventq_t *q, const evt_t *e); /* returns false if dropped */
bool EventQ_Pop(eventq_t *q, evt_t *out);
uint32_t EventQ_Count(const eventq_t *q);




#endif /* EVENTQ_H_ */
