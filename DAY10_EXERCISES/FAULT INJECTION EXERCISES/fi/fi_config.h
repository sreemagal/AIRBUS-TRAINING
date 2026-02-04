#ifndef FI_CONFIG_H
#define FI_CONFIG_H

#ifndef FI_ENABLE
#define FI_ENABLE 0
#endif

#ifndef FI_DEFAULT_PROB_PCT
#define FI_DEFAULT_PROB_PCT 5
#endif

#ifndef FI_SEED
#define FI_SEED 0x4D534543u
#endif

#ifndef FI_FEATURE_MASK
#define FI_FEATURE_MASK 0xFFFFFFFFu
#endif

#define FI_F_UART_TX_BUSY   (1u << 0)
#define FI_F_UART_CORRUPT   (1u << 1)
#define FI_F_ARINC_PARITY   (1u << 2)
#define FI_F_ARINC_LABEL    (1u << 3)
#define FI_F_MEM_BITFLIP    (1u << 4)
#define FI_F_EXCEPTIONS     (1u << 5)

#endif
