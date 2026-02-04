#include "a429_frame.h"

#define ST_WAIT_SYNC  0
#define ST_LEN        1
#define ST_PAYLOAD    2
#define ST_CHK        3

static uint8_t chk8(uint8_t len, const uint8_t payload[4])
{
    uint16_t s = (uint16_t)len;
    s = (uint16_t)(s + payload[0] + payload[1] + payload[2] + payload[3]);
    return (uint8_t)(s & 0xFFU);
}

void A429_ParserInit(a429_uart_parser_t *p)
{
    p->st = ST_WAIT_SYNC;
    p->len = 0;
    p->pay_idx = 0;
    p->chk = 0;
    p->frames_ok = 0;
    p->frames_bad_chk = 0;
    p->frames_bad_len = 0;
}

a429_parse_result_t A429_ParserFeed(a429_uart_parser_t *p, uint8_t byte, uint32_t *out_word)
{
    switch (p->st)
    {
        case ST_WAIT_SYNC:
            if (byte == 0xA5U)
            {
                p->st = ST_LEN;
            }
            break;

        case ST_LEN:
            p->len = byte;
            p->pay_idx = 0;
            if (p->len != 4U)
            {
                p->frames_bad_len++;
                p->st = ST_WAIT_SYNC;
                return A429_PARSE_BAD_LEN;
            }
            p->st = ST_PAYLOAD;
            break;

        case ST_PAYLOAD:
            p->payload[p->pay_idx++] = byte;
            if (p->pay_idx >= 4U)
            {
                p->st = ST_CHK;
            }
            break;

        case ST_CHK:
            p->chk = byte;
            {
                uint8_t exp = chk8(p->len, p->payload);
                if (exp != p->chk)
                {
                    p->frames_bad_chk++;
                    p->st = ST_WAIT_SYNC;
                    return A429_PARSE_BAD_CHK;
                }

                /* Assemble little-endian 32-bit word: W0=LSB. */
                uint32_t w = ((uint32_t)p->payload[0]) |
                             ((uint32_t)p->payload[1] << 8) |
                             ((uint32_t)p->payload[2] << 16) |
                             ((uint32_t)p->payload[3] << 24);
                *out_word = w;

                p->frames_ok++;
                p->st = ST_WAIT_SYNC;
                return A429_PARSE_WORD_OK;
            }

        default:
            p->st = ST_WAIT_SYNC;
            break;
    }

    return A429_PARSE_NONE;
}

static uint32_t popcount32(uint32_t x)
{
    /* Portable popcount */
    x = x - ((x >> 1U) & 0x55555555U);
    x = (x & 0x33333333U) + ((x >> 2U) & 0x33333333U);
    x = (x + (x >> 4U)) & 0x0F0F0F0FU;
    x = x + (x >> 8U);
    x = x + (x >> 16U);
    return x & 0x3FU;
}

bool A429_CheckOddParity(uint32_t word)
{
    /* Treat bit31 as the parity bit; odd parity across all 32 bits means popcount is odd. */
    return (popcount32(word) & 0x1U) == 1U;
}

uint8_t A429_Label(uint32_t word)
{
    /* Label in bits 0..7 if using little-endian assembly above. */
    return (uint8_t)(word & 0xFFU);
}
