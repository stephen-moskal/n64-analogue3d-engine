#ifndef ENGINE_DEBUG_H
#define ENGINE_DEBUG_H

/*
 * Engine build switches. The Makefile sets them per variant:
 *   BUILD=debug   -> ENGINE_DEBUG=1, ENGINE_PROFILE=1 (RDP validator, asserts, profiler)
 *   BUILD=release -> ENGINE_DEBUG=0, ENGINE_PROFILE=0, NDEBUG (libdragon debugf/assertf are no-ops)
 * ENGINE_STATS (cheap per-frame counters) stays on in both unless overridden.
 */

#include <libdragon.h>

#ifndef ENGINE_DEBUG
#define ENGINE_DEBUG 1
#endif

#ifndef ENGINE_PROFILE
#define ENGINE_PROFILE ENGINE_DEBUG
#endif

#ifndef ENGINE_STATS
#define ENGINE_STATS 1
#endif

#if ENGINE_DEBUG
  #define ENGINE_BUILD_NAME          "debug"
  #define ENGINE_ASSERT(cond, ...)   assertf(cond, __VA_ARGS__)
  #define ENGINE_LOG(...)            debugf(__VA_ARGS__)
#else
  #define ENGINE_BUILD_NAME          "release"
  #define ENGINE_ASSERT(cond, ...)   ((void)0)
  #define ENGINE_LOG(...)            ((void)0)
#endif

#endif
