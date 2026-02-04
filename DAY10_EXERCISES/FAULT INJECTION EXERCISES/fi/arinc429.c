#include "arinc429.h"

static uint8_t crc8(const uint8_t *d, uint32_t n)
{
    uint8_t c = 0x00u;
    for (uint32_t i = 0; i < n; i++)
    {
        uint8_t b = d[i];
        for (int k = 0; k < 8; k++)
        {
            uint8_t mix = (uint8_t)((c ^ b) & 0x01u);
            c >>= 1;
            if (mix)
                c ^= 0x8Cu;
            b >>= 1;
        }
    }
    return c;
}

static uint8_t odd_parity_31(uint32_t v31)
{
    uint32_t x = v31;
    x ^= x >> 16;
    x ^= x >> 8;
    x ^= x >> 4;
    x &= 0xFu;
    uint8_t parity = (uint8_t)((0x6996u >> x) & 1u);
    return (parity == 0u) ? 1u : 0u;
}

uint32_t ARINC429_Pack(const arinc429_word_t *w, bool force_bad_parity)
{
    uint32_t v = 0u;
    v |= ((uint32_t)(w->label) & 0xFFu) << 0;
    v |= ((uint32_t)(w->sdi) & 0x03u) << 8;
    v |= ((uint32_t)(w->data) & 0x7FFFFu) << 10;
    v |= ((uint32_t)(w->ssm) & 0x03u) << 29;

    uint8_t p = odd_parity_31(v);
    if (force_bad_parity)
        p ^= 1u;
    v |= ((uint32_t)p) << 31;

    return v;
}

status_t ARINC429_SendWord(LPUART_Type *base, const arinc429_word_t *w)
{
    bool bad_parity = false;
    arinc429_word_t temp = *w;

    /* FI #1: label substitution */
    FI_POINT(FI_F_ARINC_LABEL, temp.label ^= 0x1Fu;);

    /* FI #2: parity toggle */
    FI_POINT(FI_F_ARINC_PARITY, bad_parity = true;);

    uint32_t word = ARINC429_Pack(&temp, bad_parity);

    /* Frame: [0xA5][word LSB..MSB][CRC8(word bytes)] */
    uint8_t frame[1 + 4 + 1];
    frame[0] = 0xA5u;
    frame[1] = (uint8_t)(word & 0xFFu);
    frame[2] = (uint8_t)((word >> 8) & 0xFFu);
    frame[3] = (uint8_t)((word >> 16) & 0xFFu);
    frame[4] = (uint8_t)((word >> 24) & 0xFFu);
    frame[5] = crc8(&frame[1], 4);

    /* Optional: burst noise */
    FI_POINT(FI_F_UART_CORRUPT, FI_BitFlipRange(frame, sizeof(frame), 2););

    return UART_FI_WriteBlocking(base, frame, sizeof(frame));
}
