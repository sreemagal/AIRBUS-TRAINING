/*
 * discrete_out.h
 *
 *  Created on: 28 Dec 2025
 *      Author: Lenovo
 */
#include <stdint.h>
#include <stdbool.h>

#include "fsl_gpio.h"

typedef struct
{
    GPIO_Type *gpio;
    uint32_t pin;
    bool activeHigh;

    /* Requested logical asserted state (application writes) */
    bool request;

    /* Lamp test override (forces asserted output while true) */
    bool lampTest;

    /* Safe inhibit: if true, output will be forced to safe default */
    bool safeInhibit;

    /* Safe default logical asserted state used when inhibited */
    bool safeDefaultAsserted;

    /* Last applied physical output level */
    uint8_t outputLogic;
} dio_out_t;

void DIO_OutInit(dio_out_t *out, GPIO_Type *gpio, uint32_t pin, bool activeHigh, bool safeDefaultAsserted);

/* Apply request + lamp-test + inhibit rules to hardware */
void DIO_OutApply(dio_out_t *out);
