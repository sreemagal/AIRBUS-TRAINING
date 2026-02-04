#include "fsl_device_registers.h"
#include "m-profile/armv7m_mpu.h"
#include <stdint.h>

/* Protected buffer: we will create an MPU region that forbids access to this */
uint8_t __attribute__((aligned(32))) g_fi_deny_buf[32];

void FI_MPU_SetupDenyRegion(void)
{
    ARM_MPU_Disable();

    /* Use region 7 (arbitrary choice). Base address must be aligned to region size (32B). */
    ARM_MPU_SetRegionEx(
        7u,
        ARM_MPU_RBAR(7u, (uint32_t)g_fi_deny_buf),
        ARM_MPU_RASR(
            1u,               /* XN: execute never */
            ARM_MPU_AP_NONE,  /* no access */
            0u, 1u, 0u, 0u,   /* TEX=0, S=1, C=0, B=0 */
            0u,               /* subregion disable mask */
            ARM_MPU_REGION_SIZE_32B
        )
    );

    /* Enable MPU with default privileged memory map */
    ARM_MPU_Enable(MPU_CTRL_PRIVDEFENA_Msk);
}

