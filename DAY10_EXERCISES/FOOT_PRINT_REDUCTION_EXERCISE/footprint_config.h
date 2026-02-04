#ifndef FOOTPRINT_CONFIG_H_
#define FOOTPRINT_CONFIG_H_

/*
 * Build mode:
 *   1 => production footprint build (no debug console, no verbose logs)
 *   0 => dev build (you may enable logs/extra features)
 */
#ifndef FP_BUILD_RELEASE
#define FP_BUILD_RELEASE (1)
#endif

/*
 * SDK debug console selector (from fsl_debug_console.h):
 *   0 => redirect to toolchain
 *   1 => redirect to SDK
 *   2 => disable
 *
 * We set it to 2 for footprint release builds.
 */
#if FP_BUILD_RELEASE
#ifndef SDK_DEBUGCONSOLE
#define SDK_DEBUGCONSOLE (2U)
#endif
#endif

/* Feature gates (compile-time) */
#ifndef FP_FEATURE_TIPSTRING
#define FP_FEATURE_TIPSTRING (1) /* set to 0 to remove tip string from binary */
#endif

#ifndef FP_FEATURE_ECHO
#define FP_FEATURE_ECHO (1)      /* set to 0 to disable echo logic (RX discard) */
#endif

#ifndef FP_FEATURE_LED_HEARTBEAT
#define FP_FEATURE_LED_HEARTBEAT (0) /* set to 1 to toggle the user LED */
#endif

/*
 * Verbose logging:
 *   - In FP_BUILD_RELEASE, this is forced OFF.
 *   - In dev builds, you may set this to 1 and rely on SDK PRINTF.
 */
#ifndef FP_VERBOSE_LOG
#define FP_VERBOSE_LOG (0)
#endif

#if FP_BUILD_RELEASE
#undef FP_VERBOSE_LOG
#define FP_VERBOSE_LOG (0)
#endif

/* Sizes (keep small for footprint) */
#ifndef FP_ECHO_BUFFER_LENGTH
#define FP_ECHO_BUFFER_LENGTH (8U)
#endif

#endif /* FOOTPRINT_CONFIG_H_ */