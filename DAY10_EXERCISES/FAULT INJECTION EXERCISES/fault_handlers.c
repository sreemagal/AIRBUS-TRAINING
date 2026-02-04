#include "fsl_debug_console.h"
#include "fsl_device_registers.h"

void HardFault_Handler(void)
{
    PRINTF("HardFault -> reset\r\n");
    NVIC_SystemReset();
}

void MemManage_Handler(void)
{
    PRINTF("MemManage -> reset\r\n");
    NVIC_SystemReset();
}

void BusFault_Handler(void)
{
    PRINTF("BusFault -> reset\r\n");
    NVIC_SystemReset();
}

void UsageFault_Handler(void)
{
    PRINTF("UsageFault -> reset\r\n");
    NVIC_SystemReset();
}