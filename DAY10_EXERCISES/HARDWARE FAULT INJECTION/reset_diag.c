#include "reset_diag.h"

#include "fsl_debug_console.h"
#include "fsl_src.h"

/* GPR indexes: index 0 == GPR1, index 1 == GPR2, etc. */
#define GPR_BOOTCOUNT_INDEX   (0U)
#define GPR_LASTFLAGS_INDEX   (1U)

static void print_if_set(uint32_t flags, uint32_t mask, const char *name)
{
    if ((flags & mask) != 0U)
    {
        PRINTF(" - %s\r\n", name);
    }
}

ResetDiag_Info_t ResetDiag_RunEarly(void)
{
    ResetDiag_Info_t info = {0};

    /* SRC reset status is sticky; read it first thing after console init. */
    const uint32_t flags = SRC_GetResetStatusFlags(SRC);

    uint32_t bootCount = SRC_GetGeneralPurposeRegister(SRC, GPR_BOOTCOUNT_INDEX);
    bootCount++;

    SRC_SetGeneralPurposeRegister(SRC, GPR_BOOTCOUNT_INDEX, bootCount);
    SRC_SetGeneralPurposeRegister(SRC, GPR_LASTFLAGS_INDEX, flags);

    info.resetFlags = flags;
    info.bootCount  = bootCount;

    PRINTF("\r\n=== Reset Diagnostics (SRC) ===\r\n");
    PRINTF("BootCount (SRC GPR1): %lu\r\n", (unsigned long)bootCount);
    PRINTF("ResetFlags (SRC SRSR): 0x%08lx\r\n", (unsigned long)flags);

    ResetDiag_PrintFlags(flags);

    /* Clear only the flags we observed. */
    if (flags != 0U)
    {
        SRC_ClearResetStatusFlags(SRC, flags);
    }

    return info;
}

void ResetDiag_PrintFlags(uint32_t flags)
{
    if (flags == 0U)
    {
        PRINTF(" - (no flags set)\r\n");
        return;
    }

    /* NOTE: Availability depends on silicon feature macros; these are the names used by this SDK. */
#ifdef kSRC_PowerOnResetFlag
    print_if_set(flags, kSRC_PowerOnResetFlag, "Power-On Reset (POR)");
#endif
#ifdef kSRC_IppResetPinFlag
    print_if_set(flags, kSRC_IppResetPinFlag, "IPP_RESET_B qualified reset (board-level reset path)");
#endif
#ifdef kSRC_IppUserResetFlag
    print_if_set(flags, kSRC_IppUserResetFlag, "IPP_USER_RESET_B (external reset pin event)");
#endif
#ifdef kSRC_Wdog3ResetFlag
    print_if_set(flags, kSRC_Wdog3ResetFlag, "RTWDOG / WDOG3 timeout reset");
#endif
#ifdef kSRC_WatchdogResetFlag
    print_if_set(flags, kSRC_WatchdogResetFlag, "WDOG1/WDOG2 timeout reset");
#endif
#ifdef kSRC_SoftwareResetFlag
    print_if_set(flags, kSRC_SoftwareResetFlag, "Software SYSRESETREQ reset");
#endif
#ifdef kSRC_CoreLockupResetFlag
    print_if_set(flags, kSRC_CoreLockupResetFlag, "Core LOCKUP reset");
#endif
#ifdef kSRC_LockupSysResetFlag
    print_if_set(flags, kSRC_LockupSysResetFlag, "LOCKUP or SYSRESETREQ (combined)");
#endif
#ifdef kSRC_JTAGSystemResetFlag
    print_if_set(flags, kSRC_JTAGSystemResetFlag, "JTAG system reset");
#endif
#ifdef kSRC_JTAGSoftwareResetFlag
    print_if_set(flags, kSRC_JTAGSoftwareResetFlag, "JTAG software reset (SJC)");
#endif
#ifdef kSRC_JTAGGeneratedResetFlag
    print_if_set(flags, kSRC_JTAGGeneratedResetFlag, "JTAG generated reset (EXTEST/HIGHZ)");
#endif
#ifdef kSRC_TemperatureSensorResetFlag
    print_if_set(flags, kSRC_TemperatureSensorResetFlag, "Temperature sensor software reset");
#endif
}
