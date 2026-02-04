#ifndef A429_FRAME_H
#define A429_FRAME_H

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    A429_PARSE_NONE = 0,
    A429_PARSE_WORD_OK,
    A429_PARSE_BAD_CHK,
    A429_PARSE_BAD_LEN,
} a429_parse_result_t;

typedef struct
{
    /* Parser state */
    uint8_t st;
    uint8_t len;
    uint8_t payload[4];
    uint8_t pay_idx;
    uint8_t chk;

    /* Stats */
    uint32_t frames_ok;
    uint32_t frames_bad_chk;
    uint32_t frames_bad_len;

} a429_uart_parser_t;

void A429_ParserInit(a429_uart_parser_t *p);

a429_parse_result_t A429_ParserFeed(a429_uart_parser_t *p, uint8_t byte, uint32_t *out_word);

/* ARINC checks */
bool A429_CheckOddParity(uint32_t word);
uint8_t A429_Label(uint32_t word);

#endif /* A429_FRAME_H */