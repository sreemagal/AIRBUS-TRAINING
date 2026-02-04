#ifndef DEBOUNCER_H_
#define DEBOUNCER_H_

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
    uint8_t max;      /* qualification threshold */
    uint8_t count;    /* saturating integrator counter [0..max] */
    uint8_t state;    /* debounced state 0/1 */
    uint8_t rose;     /* latched edge flag */
    uint8_t fell;     /* latched edge flag */
} debouncer_t;

/* Initialize with MAX and an initial raw sample (0/1). */
void Debouncer_Init(debouncer_t *d, uint8_t max, uint8_t initial_raw);

/* Update once per sampling tick with raw sample (0/1). */
void Debouncer_Update(debouncer_t *d, uint8_t raw);

/* Edge flag consumers (return-and-clear). */
bool Debouncer_Rose(debouncer_t *d);
bool Debouncer_Fell(debouncer_t *d);

static inline uint8_t Debouncer_State(const debouncer_t *d) { return d->state; }

#endif /* DEBOUNCER_H_ */
