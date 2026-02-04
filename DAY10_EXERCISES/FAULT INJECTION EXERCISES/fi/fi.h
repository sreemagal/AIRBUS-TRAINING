#ifndef FI_H
#define FI_H

#include <stdint.h>
#include <stdbool.h>

#include "fi_config.h"

#ifdef __cplusplus
extern "C" {
#endif

void     FI_Init(void);
void     FI_SetEnabled(bool en);
bool     FI_IsEnabled(void);

void     FI_SetMask(uint32_t mask);
uint32_t FI_GetMask(void);

void     FI_SetProbability(uint8_t pct);
uint8_t  FI_GetProbability(void);

void     FI_SetSeed(uint32_t seed);

uint8_t  FI_RandPct(void);
bool     FI_ShouldFire(uint32_t feature);

void     FI_BitFlip8(uint8_t *p, uint8_t mask);
void     FI_BitFlipRange(void *buf, uint32_t len, uint32_t everyN);

/* Runtime trigger helpers (used in RT-1) */
void     FI_NotifyEvent(uint32_t feature);
void     FI_ArmNth(uint32_t feature, uint32_t nth);
void     FI_ArmWindowMs(uint32_t feature, uint32_t start_ms, uint32_t end_ms);
uint32_t FI_NowMs(void);

/* Exception injections (used in RT-2) */
void FI_Inject_DivByZero(void);
void FI_Inject_UnalignedAccess(void);
void FI_Inject_MemFault(void);

#if FI_ENABLE
#define FI_POINT(feature, code_block) \
    do { if (FI_ShouldFire((feature))) { code_block; } } while (0)
#else
#define FI_POINT(feature, code_block) \
    do { (void)(feature); } while (0)
#endif

#ifdef __cplusplus
}
#endif

#endif
