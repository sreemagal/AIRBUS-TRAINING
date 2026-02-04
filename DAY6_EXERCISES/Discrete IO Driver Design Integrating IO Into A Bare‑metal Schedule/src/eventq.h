/*
 * eventq.h
 *
 *  Created on: 28 Dec 2025
 *      Author: Lenovo
 */

#ifndef EVENTQ_H_
#define EVENTQ_H_
#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    EVT_NONE = 0,
    EVT_EDGE_RISE,
    EVT_EDGE_FALL,
    EVT_WOW_TRUE_RISE,
    EVT_WOW_TRUE_FALL,
    EVT_FAULT_LATCHED,
    EVT_FAULT_CLEARED,
} evt_type_t;

typedef struct
{
    evt_type_t type;
    uint32_t channel;
    uint32_t t_ms;
    uint32_t value;
} evt_t;

typedef struct
{
    evt_t *buf;
    uint32_t depth;
    volatile uint32_t wr;
    volatile uint32_t rd;
    volatile uint32_t dropped;
} eventq_t;

void EventQ_Init(eventq_t *q, evt_t *storage, uint32_t depth);
bool EventQ_Push(eventq_t *q, const evt_t *e); /* drop-newest on full */
bool EventQ_Pop(eventq_t *q, evt_t *out);
uint32_t EventQ_Count(const eventq_t *q);



#endif /* EVENTQ_H_ */
