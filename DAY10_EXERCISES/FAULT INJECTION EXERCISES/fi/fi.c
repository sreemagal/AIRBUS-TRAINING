#include "fi.h"

#include <string.h>
#include "fsl_device_registers.h"

static volatile uint32_t fi_mask = FI_FEATURE_MASK;
static volatile uint8_t  fi_prob = FI_DEFAULT_PROB_PCT;
static volatile bool     fi_en   = (FI_ENABLE != 0);

static uint32_t lcg_state = FI_SEED;

static uint32_t ev_count[32];
static uint32_t nth_arm[32];
static bool     nth_fired[32];

static uint32_t win_start_ms[32];
static uint32_t win_end_ms[32];
static bool     win_armed[32];

static inline uint32_t lcg_next(void)
{
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return lcg_state;
}

static inline int bit_index(uint32_t feature)
{
    for (int i = 0; i < 32; i++)
        if (feature == (1u << i))
            return i;
    return -1;
}

static void dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

uint32_t FI_NowMs(void)
{
    uint32_t hz = SystemCoreClock;
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0u || hz == 0u)
        return 0u;
    return (uint32_t)(DWT->CYCCNT / (hz / 1000u));
}

void FI_Init(void)
{
    SystemCoreClockUpdate();
    dwt_init();

    lcg_state = (FI_SEED ^ 0xA5A5A5A5u) + 1u;

    memset((void *)ev_count, 0, sizeof(ev_count));
    memset((void *)nth_arm, 0, sizeof(nth_arm));
    memset((void *)nth_fired, 0, sizeof(nth_fired));
    memset((void *)win_start_ms, 0, sizeof(win_start_ms));
    memset((void *)win_end_ms, 0, sizeof(win_end_ms));
    memset((void *)win_armed, 0, sizeof(win_armed));
}

void FI_SetEnabled(bool en) { fi_en = en; }
bool FI_IsEnabled(void) { return fi_en; }

void FI_SetMask(uint32_t mask) { fi_mask = mask; }
uint32_t FI_GetMask(void) { return fi_mask; }

void FI_SetProbability(uint8_t pct) { fi_prob = pct; }
uint8_t FI_GetProbability(void) { return fi_prob; }

void FI_SetSeed(uint32_t seed) { lcg_state = seed ? seed : 1u; }

uint8_t FI_RandPct(void) { return (uint8_t)(lcg_next() % 100u); }

void FI_BitFlip8(uint8_t *p, uint8_t mask) { *p ^= mask; }

void FI_BitFlipRange(void *buf, uint32_t len, uint32_t everyN)
{
    if (!fi_en || everyN == 0u)
        return;
    uint8_t *b = (uint8_t *)buf;
    for (uint32_t i = 0; i < len; i++)
    {
        if ((i % everyN) == 0u)
            b[i] ^= (uint8_t)(1u << (lcg_next() & 7u));
    }
}

void FI_NotifyEvent(uint32_t feature)
{
    int bi = bit_index(feature);
    if (bi >= 0)
        ev_count[bi]++;
}

void FI_ArmNth(uint32_t feature, uint32_t nth)
{
    int bi = bit_index(feature);
    if (bi >= 0)
    {
        nth_arm[bi] = nth;
        nth_fired[bi] = false;
    }
}

void FI_ArmWindowMs(uint32_t feature, uint32_t start_ms, uint32_t end_ms)
{
    int bi = bit_index(feature);
    if (bi >= 0)
    {
        win_start_ms[bi] = start_ms;
        win_end_ms[bi]   = end_ms;
        win_armed[bi]    = true;
    }
}

static bool window_allows(uint32_t feature)
{
    int bi = bit_index(feature);
    if (bi < 0 || !win_armed[bi])
        return true;
    uint32_t now = FI_NowMs();
    return (now >= win_start_ms[bi]) && (now <= win_end_ms[bi]);
}

static bool nth_allows(uint32_t feature)
{
    int bi = bit_index(feature);
    if (bi < 0)
        return true;

    if (nth_arm[bi] == 0u)
        return true;

    if (nth_fired[bi])
        return false;

    if (ev_count[bi] == nth_arm[bi])
    {
        nth_fired[bi] = true;
        return true;
    }

    return false;
}

bool FI_ShouldFire(uint32_t feature)
{
    if (!fi_en)
        return false;
    if ((fi_mask & feature) == 0u)
        return false;
    if (!window_allows(feature))
        return false;
    if (!nth_allows(feature))
        return false;

    return (FI_RandPct() < fi_prob);
}

/* ==== Exception injections (RT-2 uses these) ==== */
void FI_Inject_DivByZero(void)
{
    SCB->CCR |= SCB_CCR_DIV_0_TRP_Msk;
    volatile int x = 1;
    volatile int y = 0;
    volatile int z = x / y;
    (void)z;
}

void FI_Inject_UnalignedAccess(void)
{
    SCB->CCR |= SCB_CCR_UNALIGN_TRP_Msk;
    uint32_t __attribute__((aligned(4))) word = 0x12345678u;
    uint8_t *pb = (uint8_t *)&word;
    volatile uint32_t bad = *(uint32_t *)(pb + 1);
    (void)bad;
}

extern void FI_MPU_SetupDenyRegion(void);
extern uint8_t g_fi_deny_buf[32];

void FI_Inject_MemFault(void)
{
    FI_MPU_SetupDenyRegion();
    volatile uint32_t *p = (uint32_t *)g_fi_deny_buf;
    volatile uint32_t v  = *p;
    (void)v;
}
