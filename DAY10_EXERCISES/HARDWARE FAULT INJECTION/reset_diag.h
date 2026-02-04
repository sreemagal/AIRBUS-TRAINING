#ifndef RESET_DIAG_H_
#define RESET_DIAG_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ResetDiag_Info
{
    uint32_t resetFlags;
    uint32_t bootCount;
} ResetDiag_Info_t;

/* Call once very early after clocks + debug console init. */
ResetDiag_Info_t ResetDiag_RunEarly(void);

/* Convert reset flags to a short human-readable string list. */
void ResetDiag_PrintFlags(uint32_t flags);

#ifdef __cplusplus
}
#endif

#endif /* RESET_DIAG_H_ */
