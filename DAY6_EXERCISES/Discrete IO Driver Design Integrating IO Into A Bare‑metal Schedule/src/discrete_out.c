/*
 * discrete_out.c
 *
 *  Created on: 28 Dec 2025
 *      Author: Lenovo
 */
#include "discrete_out.h"

static inline uint8_t to_phys(bool asserted, bool activeHigh)
{
    /* physical level to drive the pin */
    bool level = activeHigh ? asserted : !asserted;
    return level ? 1u : 0u;
}

void DIO_OutInit(dio_out_t *out, GPIO_Type *gpio, uint32_t pin, bool activeHigh, bool safeDefaultAsserted)
{
    out->gpio = gpio;
    out->pin = pin;
    out->activeHigh = activeHigh;

    out->request = false;
    out->lampTest = false;
    out->safeInhibit = false;
    out->safeDefaultAsserted = safeDefaultAsserted;

    out->outputLogic = to_phys(safeDefaultAsserted, activeHigh);

    gpio_pin_config_t cfg = {0};
    cfg.direction = kGPIO_DigitalOutput;
    cfg.outputLogic = out->outputLogic;
    cfg.interruptMode = kGPIO_NoIntmode;

    GPIO_PinInit(gpio, pin, &cfg);
}

void DIO_OutApply(dio_out_t *out)
{
    bool desiredAsserted;

    if (out->safeInhibit)
    {
        desiredAsserted = out->safeDefaultAsserted;
    }
    else if (out->lampTest)
    {
        desiredAsserted = true;
    }
    else
    {
        desiredAsserted = out->request;
    }

    uint8_t phys = to_phys(desiredAsserted, out->activeHigh);

    if (phys != out->outputLogic)
    {
        out->outputLogic = phys;
        GPIO_PinWrite(out->gpio, out->pin, phys);
    }
}
