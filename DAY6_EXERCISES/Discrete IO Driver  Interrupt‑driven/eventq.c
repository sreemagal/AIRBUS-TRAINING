/*
 * eventq.c
 *
 *  Created on: 29 Dec 2025
 *      Author: Lenovo
 */
#include "eventq.h"

static inline uint32_t idx(const eventq_t *q, uint32_t v)
{
    return (v % q->depth);
}

void EventQ_Init(eventq_t *q, evt_t *storage, uint32_t depth)
{
    q->buf = storage;
    q->depth = depth;
    q->wr = 0u;
    q->rd = 0u;
    q->dropped = 0u;
}

bool EventQ_Push(eventq_t *q, const evt_t *e)
{
    uint32_t count = q->wr - q->rd;
    if (count >= q->depth)
    {
        q->dropped++;
        return false; /* drop newest */
    }

    q->buf[idx(q, q->wr)] = *e;
    q->wr++;
    return true;
}

bool EventQ_Pop(eventq_t *q, evt_t *out)
{
    if (q->rd == q->wr)
    {
        return false;
    }

    *out = q->buf[idx(q, q->rd)];
    q->rd++;
    return true;
}

uint32_t EventQ_Count(const eventq_t *q)
{
    return (q->wr - q->rd);
}


