#ifndef DEBUG_LOG_H_
#define DEBUG_LOG_H_

#include "cmsis_os.h"
#include "stdio.h"

/* ── Log levels ─────────────────────────────────────────────────────────────
 * Set DEBUG_LOG_LEVEL to control verbosity:
 *   0 = silent, 1 = ERROR only, 2 = +WARN, 3 = +INFO, 4 = +DEBUG (verbose)
 */
#ifndef DEBUG_LOG_LEVEL
#define DEBUG_LOG_LEVEL  3
#endif

/* ── Public API ──────────────────────────────────────────────────────────── */
void  DebugLog_Init(osMutexId *uart_mutex);
void  DebugLog_Print(const char *level_str, const char *fmt, ...);
void  DebugLog_PrintStats(void);   /* call from heartbeat — prints all diag counters */

/* ── Macros ──────────────────────────────────────────────────────────────── */
#if DEBUG_LOG_LEVEL >= 1
  #define LOG_E(fmt, ...)  DebugLog_Print("ERR ", fmt, ##__VA_ARGS__)
#else
  #define LOG_E(fmt, ...)  do {} while(0)
#endif

#if DEBUG_LOG_LEVEL >= 2
  #define LOG_W(fmt, ...)  DebugLog_Print("WARN", fmt, ##__VA_ARGS__)
#else
  #define LOG_W(fmt, ...)  do {} while(0)
#endif

#if DEBUG_LOG_LEVEL >= 3
  #define LOG_I(fmt, ...)  DebugLog_Print("INFO", fmt, ##__VA_ARGS__)
#else
  #define LOG_I(fmt, ...)  do {} while(0)
#endif

#if DEBUG_LOG_LEVEL >= 4
  #define LOG_D(fmt, ...)  DebugLog_Print("DBG ", fmt, ##__VA_ARGS__)
#else
  #define LOG_D(fmt, ...)  do {} while(0)
#endif

#endif /* DEBUG_LOG_H_ */
