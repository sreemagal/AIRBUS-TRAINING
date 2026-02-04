#ifndef ARINC_SIM_H_
#define ARINC_SIM_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void ArincSim_InitUart3(uint32_t baudrate);

/* Roles */
void ArincSim_RunRdc(uint32_t bootCount, uint32_t lastResetFlags);
void ArincSim_RunBridge(void);

#ifdef __cplusplus
}
#endif

#endif /* ARINC_SIM_H_ */
