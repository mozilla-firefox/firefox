/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Telemetry and use counter functionality. */

#ifndef js_friend_UsageStatistics_h
#define js_friend_UsageStatistics_h

#include "mozilla/TimeStamp.h"  // mozilla::TimeDuration
#include "mozilla/Variant.h"    // mozilla::Variant

#include <stdint.h>  // uint32_t

#include "jstypes.h"  // JS_PUBLIC_API

struct JS_PUBLIC_API JSContext;
class JS_PUBLIC_API JSObject;

/*
 * Each glean metric must be manually added
 * to the switch statement in AccumulateTelemetryCallback().
 */
#define FOR_EACH_JS_METRIC(_)                       \
  _(GC_REASON_2, Enumeration)                       \
  _(GC_IS_COMPARTMENTAL, Boolean)                   \
  _(GC_GRAY_BITS_VALID, Boolean)                    \
  _(GC_ZONE_COUNT, QuantityDistribution)            \
  _(GC_ZONES_COLLECTED, QuantityDistribution)       \
  _(GC_MARK_STACK_MAX_CAPACITY, MemoryDistribution) \
  _(GC_MS, TimeDuration)                            \
  _(GC_BUDGET_MS_2, TimeDuration)                   \
  _(GC_BUDGET_WAS_INCREASED, Boolean)               \
  _(GC_SLICE_WAS_LONG, Boolean)                     \
  _(GC_BUDGET_OVERRUN, TimeDuration)                \
  _(GC_ANIMATION_MS, TimeDuration)                  \
  _(GC_MAX_PAUSE_MS_2, TimeDuration)                \
  _(GC_PREPARE_MS, TimeDuration)                    \
  _(GC_MARK_MS, TimeDuration)                       \
  _(GC_SWEEP_MS, TimeDuration)                      \
  _(GC_COMPACT_MS, TimeDuration)                    \
  _(GC_MARK_ROOTS_US, TimeDuration)                 \
  _(GC_MARK_GRAY_MS_2, TimeDuration)                \
  _(GC_MARK_WEAK_MS, TimeDuration)                  \
  _(GC_SLICE_MS, TimeDuration)                      \
  _(GC_SLOW_PHASE, Enumeration)                     \
  _(GC_SLOW_TASK, Enumeration)                      \
  _(GC_MMU_50, Percentage)                          \
  _(GC_RESET, Boolean)                              \
  _(GC_RESET_REASON, Enumeration)                   \
  _(GC_NON_INCREMENTAL, Boolean)                    \
  _(GC_NON_INCREMENTAL_REASON, Enumeration)         \
  _(GC_MINOR_REASON, Enumeration)                   \
  _(GC_MINOR_REASON_LONG, Enumeration)              \
  _(GC_MINOR_US, TimeDuration)                      \
  _(GC_NURSERY_BYTES_2, MemoryDistribution)         \
  _(GC_PRETENURE_COUNT_2, QuantityDistribution)     \
  _(GC_NURSERY_PROMOTION_RATE, Percentage)          \
  _(GC_TENURED_SURVIVAL_RATE, Percentage)           \
  _(GC_MARK_RATE_2, QuantityDistribution)           \
  _(GC_TIME_BETWEEN_S, TimeDuration)                \
  _(GC_TIME_BETWEEN_SLICES_MS, TimeDuration)        \
  _(GC_SLICE_COUNT, QuantityDistribution)           \
  _(GC_EFFECTIVENESS, MemoryDistribution)           \
  _(GC_BUFFER_ALLOC_HEAP_BYTES, MemoryDistribution) \
  _(GC_BUFFER_ALLOC_HEAP_DENSITY, Percentage)       \
  _(GC_PARALLEL_MARK, Boolean)                      \
  _(GC_PARALLEL_MARK_SPEEDUP, Integer)              \
  _(GC_PARALLEL_MARK_UTILIZATION, Percentage)       \
  _(GC_PARALLEL_MARK_INTERRUPTIONS, Integer)        \
  _(GC_TASK_START_DELAY_US, TimeDuration)           \
  _(GC_TIME_BETWEEN_MINOR_MS, TimeDuration)

// clang-format off
#define ENUM_DEF(NAME, _) NAME,
enum class JSMetric {
  FOR_EACH_JS_METRIC(ENUM_DEF)
  Count
};
#undef ENUM_DEF
// clang-format on

using JSTelemetryData = mozilla::Variant<bool, size_t, mozilla::TimeDuration>;

using JSAccumulateTelemetryDataCallback = void (*)(JSMetric,
                                                   const JSTelemetryData&);

extern JS_PUBLIC_API void JS_SetAccumulateTelemetryCallback(
    JSContext* cx, JSAccumulateTelemetryDataCallback callback);

#define FOR_EACH_JS_USE_COUNTER(_)                         \
  _(WASM, Wasm)                                            \
  _(WASM_LEGACY_EXCEPTIONS, WasmLegacyExceptions)          \
  _(ISHTMLDDA_FUSE, IsHTMLDDAFuse)                         \
  _(OPTIMIZE_GET_ITERATOR_FUSE, OptimizeGetIteratorFuse)   \
  _(LEGACY_LANG_SUBTAG, LegacyLangSubtag)                  \
  _(IC_STUB_TOO_LARGE, ICStubTooLarge)                     \
  _(IC_STUB_OOM, ICStubOOM)                                \
  _(DATEPARSE, DateParse)                                  \
  _(DATEPARSE_IMPL_DEF, DateParseImplDef)                  \
  _(OPTIMIZE_ARRAY_SPECIES_FUSE, OptimizeArraySpeciesFuse) \
  _(OPTIMIZE_PROMISE_LOOKUP_FUSE, OptimizePromiseLookupFuse)

/*
 * Use counter names passed to the accumulate use counter callback.
 *
 * It's OK to for these enum values to change as they will be mapped to a
 * fixed member of the mozilla::UseCounter enum by the callback.
 */

#define ENUM_DEF(NAME, _) NAME,
enum class JSUseCounter { FOR_EACH_JS_USE_COUNTER(ENUM_DEF) COUNT };
#undef ENUM_DEF

using JSSetUseCounterCallback = void (*)(JSObject*, JSUseCounter);

extern JS_PUBLIC_API void JS_SetSetUseCounterCallback(
    JSContext* cx, JSSetUseCounterCallback callback);

#endif  // js_friend_UsageStatistics_h
