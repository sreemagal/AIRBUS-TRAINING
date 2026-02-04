#ifndef ARINC429_H
#define ARINC429_H

#include <stdint.h>
#include <stdbool.h>

#include "fsl_lpuart.h"
#include "fi.h"

typedef struct {
    uint8_t  label;
    uint8_t  sdi;
    uint32_t data;
    uint8_t  ssm;
} arinc429_word_t;

uint32_t ARINC429_Pack(const arinc429_word_t *w, bool force_bad_parity);
status_t ARINC429_SendWord(LPUART_Type *base, const arinc429_word_t *w);

#endif
