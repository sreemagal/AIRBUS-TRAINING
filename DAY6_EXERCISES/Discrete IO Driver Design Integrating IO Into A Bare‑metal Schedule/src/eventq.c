/*
 * eventq.c
 *
 *  Created on: 28 Dec 2025
 *      Author: Lenovo
 */

#include "eventq.h"

void EventQ_Init(eventq_t *q, evt_t *storage, uint32_t depth)
{
    q->buf = storage;
    q->depth = depth;
    q->wr = 0u;
    q->rd = 0u;
    q->dropped = 0u;
}

static inline uint32_t next_idx(const eventq_t *q, uint32_t idx)
{
    idx++;
    if (idx >= q->depth)
    {
        idx = 0u;
    }
    return idx;
}

bool EventQ_Push(eventq_t *q, const evt_t *e)
{
    uint32_t wr = q->wr;
    uint32_t nxt = next_idx(q, wr);

    if (nxt == q->rd)
    {
        /* full: drop newest */
        q->dropped++;
        return false;
    }

    q->buf[wr] = *e;
    q->wr = nxt;
    return true;
}

bool EventQ_Pop(eventq_t *q, evt_t *out)
{
    if (q->rd == q->wr)
    {
        return false;
    }

    *out = q->buf[q->rd];
    q->rd = next_idx(q, q->rd);
    return true;
}

uint32_t EventQ_Count(const eventq_t *q)
{
    uint32_t wr = q->wr;
    uint32_t rd = q->rd;

    if (wr >= rd)
    {
        return (wr - rd);
    }
    return (q->depth - (rd - wr));
}

