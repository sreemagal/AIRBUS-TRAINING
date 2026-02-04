#ifndef FI_H
#define FI_H

#include <stdint.h>
#include <stdbool.h>

#ifndef SFI_ENABLED
#define SFI_ENABLED 1
#endif

typedef enum
{
    FI_SITE_RX_CORRUPT = 0,
    FI_SITE_A429_LABEL_TAMPER,
    FI_SITE_A429_PARITY_TAMPER,
    FI_SITE_TX_API_FAIL,
    FI_SITE_TX_STALL,

    FI_SITE_COUNT
} fi_site_t;

typedef enum
{
    FI_POLICY_DISABLED = 0,
    FI_POLICY_EVERY_N,
    FI_POLICY_WINDOW
} fi_policy_t;

typedef struct
{
    fi_policy_t policy;
    uint32_t every_n;        /* for EVERY_N */
    uint32_t window_start;   /* for WINDOW (inclusive) */
    uint32_t window_end;     /* for WINDOW (exclusive) */

    uint32_t hit_count;
    uint32_t fire_count;

    /* Optional parameters per site */
    uint8_t bits_to_flip;    /* RX_CORRUPT */
    uint8_t reserved;
    uint16_t reserved2;
} fi_site_cfg_t;

typedef struct
{
    uint32_t ts_ms;
    uint8_t site;
    uint8_t fired;
    uint16_t spare;
    uint32_t hit;
    uint32_t extra;
    uint32_t data_peek;
} fi_event_t;

void FI_Init(uint32_t seed);

/* Timebase hookup (ms). Call once from PIT ISR or your time source. */
void FI_SetNowMs(uint32_t now_ms);
uint32_t FI_NowMs(void);

/* Configuration */
fi_site_cfg_t *FI_SiteCfg(fi_site_t site);
void FI_DisableAll(void);

/* Decision */
bool FI_ShouldFire(fi_site_t site);

/* Helpers */
uint8_t FI_CorruptByteDeterministic(uint8_t in);
uint32_t FI_Rand32(void);

/* Logging */
void FI_Log(fi_site_t site, bool fired, uint32_t hit, uint32_t extra, uint32_t data_peek);
void FI_DumpLog(void (*emit)(const char *s));

#endif /* FI_H */