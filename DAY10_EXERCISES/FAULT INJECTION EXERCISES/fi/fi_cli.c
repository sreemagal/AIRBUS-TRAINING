#include "fsl_debug_console.h"
#include <string.h>
#include <stdlib.h>

#include "fi.h"

static char line[80];
static unsigned idx = 0;

static bool Console_TryReadChar(char *out)
{
    int c = DbgConsole_Getchar();   /* returns -1 if no char available */
    if (c < 0)
    {
        return false;
    }
    *out = (char)c;
    return true;
}
static uint32_t parse_hex(const char *s)
{
    return (uint32_t)strtoul(s ? s : "0", NULL, 16);
}

static uint32_t feature_from_name(const char *s)
{
    if (!s) return 0;
    if (!strcmp(s, "UART_TX_BUSY")) return FI_F_UART_TX_BUSY;
    if (!strcmp(s, "UART_CORRUPT")) return FI_F_UART_CORRUPT;
    if (!strcmp(s, "ARINC_PARITY")) return FI_F_ARINC_PARITY;
    if (!strcmp(s, "ARINC_LABEL"))  return FI_F_ARINC_LABEL;
    if (!strcmp(s, "MEM_BITFLIP"))  return FI_F_MEM_BITFLIP;
    if (!strcmp(s, "EXCEPTIONS"))   return FI_F_EXCEPTIONS;
    return 0;
}

static void print_help(void)
{
    PRINTF("Commands:\r\n");
    PRINTF("  help\r\n");
    PRINTF("  en 0|1\r\n");
    PRINTF("  msk HEX\r\n");
    PRINTF("  pct 0..100\r\n");
    PRINTF("  seed HEX\r\n");
    PRINTF("  nth FEATURE N\r\n");
    PRINTF("  win FEATURE START_MS END_MS\r\n");
}

void FI_CLI_Poll(void)
{
    char ch;
    if (Console_TryReadChar(&ch) != kStatus_Success)
        return;

    if (ch == '\r' || ch == '\n')
    {
        line[idx] = 0;
        idx = 0;

        char *cmd = strtok(line, " ");
        if (!cmd) return;

        if (!strcmp(cmd, "help")) { print_help(); return; }

        if (!strcmp(cmd, "en"))
        {
            char *a = strtok(NULL, " ");
            FI_SetEnabled(a && a[0] == '1');
            PRINTF("FI enabled=%d\r\n", FI_IsEnabled());
            return;
        }

        if (!strcmp(cmd, "msk"))
        {
            char *a = strtok(NULL, " ");
            FI_SetMask(parse_hex(a));
            PRINTF("mask=0x%08x\r\n", (unsigned)FI_GetMask());
            return;
        }

        if (!strcmp(cmd, "pct"))
        {
            char *a = strtok(NULL, " ");
            FI_SetProbability((uint8_t)atoi(a ? a : "0"));
            PRINTF("pct=%u\r\n", (unsigned)FI_GetProbability());
            return;
        }

        if (!strcmp(cmd, "seed"))
        {
            char *a = strtok(NULL, " ");
            FI_SetSeed(parse_hex(a));
            PRINTF("seed set\r\n");
            return;
        }

        if (!strcmp(cmd, "nth"))
        {
            char *f = strtok(NULL, " ");
            char *n = strtok(NULL, " ");
            uint32_t feat = feature_from_name(f);
            FI_ArmNth(feat, (uint32_t)atoi(n ? n : "0"));
            PRINTF("nth armed\r\n");
            return;
        }

        if (!strcmp(cmd, "win"))
        {
            char *f = strtok(NULL, " ");
            char *s = strtok(NULL, " ");
            char *e = strtok(NULL, " ");
            uint32_t feat = feature_from_name(f);
            FI_ArmWindowMs(feat,
                           (uint32_t)atoi(s ? s : "0"),
                           (uint32_t)atoi(e ? e : "0"));
            PRINTF("window armed\r\n");
            return;
        }

        PRINTF("? (type help)\r\n");
        return;
    }

    if (idx < (sizeof(line) - 1))
        line[idx++] = ch;
}
