#include "fi.h"

#if SFI_ENABLED

#ifndef FI_LOG_DEPTH
#define FI_LOG_DEPTH 128
#endif

static volatile uint32_t g_now_ms;
static fi_site_cfg_t g_sites[FI_SITE_COUNT];
static fi_event_t g_log[FI_LOG_DEPTH];
static volatile uint32_t g_log_wr;

static uint32_t g_rng;

static uint32_t xorshift32(uint32_t x)
{
    /* Deterministic and fast. */
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

void FI_Init(uint32_t seed)
{
    g_now_ms = 0;
    g_rng = (seed == 0U) ? 0xA5A5A5A5U : seed;

    for (int i = 0; i < (int)FI_SITE_COUNT; i++)
    {
        g_sites[i].policy = FI_POLICY_DISABLED;
        g_sites[i].every_n = 0;
        g_sites[i].window_start = 0;
        g_sites[i].window_end = 0;
        g_sites[i].hit_count = 0;
        g_sites[i].fire_count = 0;
        g_sites[i].bits_to_flip = 1;
    }

    /* Default arming matching the exercise spec */
    g_sites[FI_SITE_RX_CORRUPT].policy = FI_POLICY_EVERY_N;
    g_sites[FI_SITE_RX_CORRUPT].every_n = 200;
    g_sites[FI_SITE_RX_CORRUPT].bits_to_flip = 3;

    g_sites[FI_SITE_TX_API_FAIL].policy = FI_POLICY_EVERY_N;
    g_sites[FI_SITE_TX_API_FAIL].every_n = 100;

    g_sites[FI_SITE_TX_STALL].policy = FI_POLICY_WINDOW;
    g_sites[FI_SITE_TX_STALL].window_start = 500;
    g_sites[FI_SITE_TX_STALL].window_end   = 520;

    /* Optional: you can arm label/parity tampers from CLI */
    g_sites[FI_SITE_A429_LABEL_TAMPER].policy = FI_POLICY_DISABLED;
    g_sites[FI_SITE_A429_PARITY_TAMPER].policy = FI_POLICY_DISABLED;

    g_log_wr = 0;
}

void FI_SetNowMs(uint32_t now_ms) { g_now_ms = now_ms; }
uint32_t FI_NowMs(void) { return g_now_ms; }

fi_site_cfg_t *FI_SiteCfg(fi_site_t site)
{
    if ((int)site < 0 || site >= FI_SITE_COUNT) return (fi_site_cfg_t *)0;
    return &g_sites[site];
}

void FI_DisableAll(void)
{
    for (int i = 0; i < (int)FI_SITE_COUNT; i++)
    {
        g_sites[i].policy = FI_POLICY_DISABLED;
    }
}

uint32_t FI_Rand32(void)
{
    g_rng = xorshift32(g_rng);
    return g_rng;
}

bool FI_ShouldFire(fi_site_t site)
{
    fi_site_cfg_t *cfg = FI_SiteCfg(site);
    if (!cfg) return false;

    cfg->hit_count++;

    bool fire = false;
    switch (cfg->policy)
    {
        case FI_POLICY_DISABLED:
            fire = false;
            break;
        case FI_POLICY_EVERY_N:
            fire = (cfg->every_n != 0U) && ((cfg->hit_count % cfg->every_n) == 0U);
            break;
        case FI_POLICY_WINDOW:
            fire = (cfg->hit_count >= cfg->window_start) && (cfg->hit_count < cfg->window_end);
            break;
        default:
            fire = false;
            break;
    }

    if (fire) cfg->fire_count++;
    return fire;
}

uint8_t FI_CorruptByteDeterministic(uint8_t in)
{
    /* Flip cfg->bits_to_flip bits deterministically based on RNG. */
    fi_site_cfg_t *cfg = FI_SiteCfg(FI_SITE_RX_CORRUPT);
    uint8_t out = in;

    const uint8_t flips = (cfg && cfg->bits_to_flip) ? cfg->bits_to_flip : 1U;
    for (uint8_t i = 0; i < flips; i++)
    {
        uint32_t r = FI_Rand32();
        uint8_t bit = (uint8_t)(r & 0x7U);
        out ^= (uint8_t)(1U << bit);
    }
    return out;
}

void FI_Log(fi_site_t site, bool fired, uint32_t hit, uint32_t extra, uint32_t data_peek)
{
    uint32_t idx = g_log_wr++;
    fi_event_t *e = &g_log[idx % FI_LOG_DEPTH];
    e->ts_ms = FI_NowMs();
    e->site = (uint8_t)site;
    e->fired = fired ? 1U : 0U;
    e->hit = hit;
    e->extra = extra;
    e->data_peek = data_peek;
}

static const char *site_name(fi_site_t s)
{
    switch (s)
    {
        case FI_SITE_RX_CORRUPT: return "RX_CORRUPT";
        case FI_SITE_A429_LABEL_TAMPER: return "A429_LABEL_TAMPER";
        case FI_SITE_A429_PARITY_TAMPER: return "A429_PARITY_TAMPER";
        case FI_SITE_TX_API_FAIL: return "TX_API_FAIL";
        case FI_SITE_TX_STALL: return "TX_STALL";
        default: return "?";
    }
}

void FI_DumpLog(void (*emit)(const char *s))
{
    /* Very small footprint dump; caller formats. */
    (void)emit;
    /* Implemented in main via PRINTF for convenience; keep this module RTOS-agnostic. */
}

#else /* SFI_ENABLED == 0 */

void FI_Init(uint32_t seed) { (void)seed; }
void FI_SetNowMs(uint32_t now_ms) { (void)now_ms; }
uint32_t FI_NowMs(void) { return 0; }
fi_site_cfg_t *FI_SiteCfg(fi_site_t site) { (void)site; return (fi_site_cfg_t *)0; }
void FI_DisableAll(void) {}
bool FI_ShouldFire(fi_site_t site) { (void)site; return false; }
uint8_t FI_CorruptByteDeterministic(uint8_t in) { return in; }
uint32_t FI_Rand32(void) { return 0; }
void FI_Log(fi_site_t site, bool fired, uint32_t hit, uint32_t extra, uint32_t data_peek)
{
    (void)site; (void)fired; (void)hit; (void)extra; (void)data_peek;
}
void FI_DumpLog(void (*emit)(const char *s)) { (void)emit; }

#endif