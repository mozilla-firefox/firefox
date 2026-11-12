/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This header contains most functions that give information about the Profiler:
// Whether it is active or not, paused, and the selected features.

#ifndef ProfilerState_h
#define ProfilerState_h

#include <mozilla/DefineEnum.h>
#include <mozilla/EnumSet.h>
#include "mozilla/ProfilerPlatformMacros.h"
#include "mozilla/ProfilerUtils.h"
#include "mozilla/Perfetto.h"

#include <functional>

//---------------------------------------------------------------------------
// Profiler features
//---------------------------------------------------------------------------

#if defined(GP_PLAT_arm64_darwin)
#  define POWER_HELP "Sample per process power use"
#elif defined(GP_PLAT_amd64_darwin)
#  define POWER_HELP \
    "Record the power used by the entire system with each sample."
#elif defined(GP_PLAT_amd64_linux)
#  define POWER_HELP                                                \
    "Record the power used by the entire system with each sample. " \
    "Only available with Intel CPUs and requires setting "          \
    "the sysctl kernel.perf_event_paranoid to 0."

#elif defined(GP_OS_windows)
#  define POWER_HELP                                                       \
    "Record the value of every energy meter available on the system with " \
    "each sample. Only available on Windows 11 with Intel CPUs."
#else
#  define POWER_HELP "Not supported on this platform."
#endif

// Higher-order macro containing all the feature info in one place. Define
// |MACRO| appropriately to extract the relevant parts. Note that the number
// values are used internally only and so can be changed without consequence.
// Any changes to this list should also be applied to the feature list in
// toolkit/components/extensions/schemas/geckoProfiler.json.
// *** Synchronize with lists in BaseProfilerState.h and geckoProfiler.json ***
#define PROFILER_FOR_EACH_FEATURE(MACRO)                                   \
  MACRO(0, "java", Java, "Profile Java code, Android only")                \
                                                                           \
  MACRO(1, "js", JS,                                                       \
        "Get the JS engine to expose the JS stack to the profiler")        \
                                                                           \
  MACRO(2, "mainthreadio", MainThreadIO, "Add main thread file I/O")       \
                                                                           \
  MACRO(3, "fileio", FileIO,                                               \
        "Add file I/O from all profiled threads, implies mainthreadio")    \
                                                                           \
  MACRO(4, "fileioall", FileIOAll,                                         \
        "Add file I/O from all threads, implies fileio")                   \
                                                                           \
  MACRO(5, "nomarkerstacks", NoMarkerStacks,                               \
        "Markers do not capture stacks, to reduce overhead")               \
                                                                           \
  MACRO(6, "screenshots", Screenshots,                                     \
        "Take a snapshot of the window on every composition")              \
                                                                           \
  MACRO(7, "seqstyle", SequentialStyle,                                    \
        "Disable parallel traversal in styling")                           \
                                                                           \
  MACRO(8, "stackwalk", StackWalk,                                         \
        "Walk the C++ stack, not available on all platforms")              \
                                                                           \
  MACRO(9, "jsallocations", JSAllocations,                                 \
        "Have the JavaScript engine track allocations")                    \
                                                                           \
  MACRO(10, "nostacksampling", NoStackSampling,                            \
        "Disable all stack sampling: Cancels \"js\", \"stackwalk\" and "   \
        "labels")                                                          \
                                                                           \
  MACRO(11, "nativeallocations", NativeAllocations,                        \
        "Collect the stacks from a smaller subset of all native "          \
        "allocations, biasing towards collecting larger allocations")      \
                                                                           \
  MACRO(12, "ipcmessages", IPCMessages,                                    \
        "Have the IPC layer track cross-process messages")                 \
                                                                           \
  MACRO(13, "audiocallbacktracing", AudioCallbackTracing,                  \
        "Audio callback tracing")                                          \
                                                                           \
  MACRO(14, "winsystemticksampling", WinSystemTickSampling,                \
        "Keep sampling on the system timer tick, without high-resolution " \
        "timers or timer resolution changes, to minimize wakeups and "     \
        "timer interference. The sampling interval will be at least "      \
        "~16ms")                                                           \
                                                                           \
  MACRO(15, "cpuallthreads", CPUAllThreads,                                \
        "Sample the CPU utilization of all registered threads")            \
                                                                           \
  MACRO(16, "samplingallthreads", SamplingAllThreads,                      \
        "Sample the stacks of all registered threads")                     \
                                                                           \
  MACRO(17, "markersallthreads", MarkersAllThreads,                        \
        "Record markers from all registered threads")                      \
                                                                           \
  MACRO(18, "unregisteredthreads", UnregisteredThreads,                    \
        "Discover and profile unregistered threads -- beware: expensive!") \
                                                                           \
  MACRO(19, "processcpu", ProcessCPU,                                      \
        "Sample the CPU utilization of each process")                      \
                                                                           \
  MACRO(20, "power", Power, POWER_HELP)                                    \
                                                                           \
  MACRO(21, "cpufreq", CPUFrequency,                                       \
        "Record the clock frequency of "                                   \
        "every CPU core for every profiler sample.")                       \
                                                                           \
  MACRO(22, "bandwidth", Bandwidth,                                        \
        "Record the network bandwidth used for every profiler sample.")    \
                                                                           \
  MACRO(23, "memory", Memory,                                              \
        "Track the memory allocations and deallocations per process over " \
        "time.")                                                           \
                                                                           \
  MACRO(24, "tracing", Tracing,                                            \
        "Instead of sampling periodically, captures information about "    \
        "every function executed for the duration (JS only)")              \
                                                                           \
  MACRO(25, "sandbox", Sandbox,                                            \
        "Report sandbox syscalls and logs in the "                         \
        "profiler.")                                                       \
                                                                           \
  MACRO(26, "flows", Flows,                                                \
        "Include all flow-related markers. These markers show the program" \
        "better but can cause more overhead in some places than normal.")  \
                                                                           \
  MACRO(27, "jssources", JSSources,                                        \
        "Collect JavaScript source code information for profiled scripts.")

// *** Synchronize with lists in BaseProfilerState.h and geckoProfiler.json ***

struct ProfilerFeature {
#define DECLARE(n_, str_, Name_, desc_)                                \
  static constexpr uint32_t Name_ = (1u << n_);                        \
  [[nodiscard]] static constexpr bool Has##Name_(uint32_t aFeatures) { \
    return aFeatures & Name_;                                          \
  }                                                                    \
  static constexpr void Set##Name_(uint32_t& aFeatures) {              \
    aFeatures |= Name_;                                                \
  }                                                                    \
  static constexpr void Clear##Name_(uint32_t& aFeatures) {            \
    aFeatures &= ~Name_;                                               \
  }

  // Define a bitfield constant, a getter, and two setters for each feature.
  PROFILER_FOR_EACH_FEATURE(DECLARE)

#undef DECLARE

  [[nodiscard]] static constexpr bool ShouldInstallMemoryHooks(
      uint32_t aFeatures) {
    return ProfilerFeature::HasMemory(aFeatures) ||
           ProfilerFeature::HasNativeAllocations(aFeatures);
  }
};

// clang-format off
MOZ_DEFINE_ENUM_CLASS(ProfilingState,(
  // A callback will be invoked ...
  AlreadyActive,     // if the profiler is active when the callback is added.
  RemovingCallback,  // when the callback is removed.
  Started,           // after the profiler has started.
  Pausing,           // before the profiler is paused.
  Resumed,           // after the profiler has resumed.
  GeneratingProfile, // before a profile is created.
  Stopping,          // before the profiler stops (unless restarting afterward).
  ShuttingDown       // before the profiler is shut down.
));
// clang-format on

[[nodiscard]] inline static const char* ProfilingStateToString(
    ProfilingState aProfilingState) {
  switch (aProfilingState) {
    case ProfilingState::AlreadyActive:
      return "Profiler already active";
    case ProfilingState::RemovingCallback:
      return "Callback being removed";
    case ProfilingState::Started:
      return "Profiler started";
    case ProfilingState::Pausing:
      return "Profiler pausing";
    case ProfilingState::Resumed:
      return "Profiler resumed";
    case ProfilingState::GeneratingProfile:
      return "Generating profile";
    case ProfilingState::Stopping:
      return "Profiler stopping";
    case ProfilingState::ShuttingDown:
      return "Profiler shutting down";
    default:
      MOZ_ASSERT_UNREACHABLE("Unexpected ProfilingState enum value");
      return "?";
  }
}

using ProfilingStateSet = mozilla::EnumSet<ProfilingState>;

[[nodiscard]] constexpr ProfilingStateSet AllProfilingStates() {
  ProfilingStateSet set;
  using Value = std::underlying_type_t<ProfilingState>;
  for (Value stateValue = 0;
       stateValue <= static_cast<Value>(kHighestProfilingState); ++stateValue) {
    set += static_cast<ProfilingState>(stateValue);
  }
  return set;
}

// Type of callbacks to be invoked at certain state changes.
// It must NOT call profiler_add/remove_state_change_callback().
using ProfilingStateChangeCallback = std::function<void(ProfilingState)>;

#include "mozilla/Atomics.h"
#include "mozilla/Maybe.h"

#include <stdint.h>

namespace mozilla::profiler::detail {

// RacyFeatures is only defined in this header file so that its methods can
// be inlined into profiler_is_active(). Please do not use anything from the
// detail namespace outside the profiler.

// Within the profiler's code, the preferred way to check profiler activeness
// and features is via ActivePS(). However, that requires locking gPSMutex.
// There are some hot operations where absolute precision isn't required, so we
// duplicate the activeness/feature state in a lock-free manner in this class.
class RacyFeatures {
 public:
  static void SetActive(uint32_t aFeatures) {
    sActiveAndFeatures = Active | aFeatures;
  }

  static void SetETWCollectionActive() {
    sActiveAndFeatures |= ETWCollectionEnabled;
  }

  static void SetETWCollectionInactive() {
    sActiveAndFeatures &= ~ETWCollectionEnabled;
  }

  static void SetPerfettoTracingActive() {
    sActiveAndFeatures |= PerfettoTracingEnabled;
  }

  static void SetPerfettoTracingInactive() {
    sActiveAndFeatures &= ~PerfettoTracingEnabled;
  }

  static void SetInactive() { sActiveAndFeatures = 0; }

  static void SetPaused() { sActiveAndFeatures |= Paused; }

  static void SetUnpaused() { sActiveAndFeatures &= ~Paused; }

  static void SetSamplingPaused() { sActiveAndFeatures |= SamplingPaused; }

  static void SetSamplingUnpaused() { sActiveAndFeatures &= ~SamplingPaused; }

  [[nodiscard]] static Maybe<uint32_t> FeaturesIfActive() {
    if (uint32_t af = sActiveAndFeatures; af & Active) {
      // Active, remove the Active&Paused bits to get all features.
      return Some(af & ~(Active | Paused | SamplingPaused));
    }
    return Nothing();
  }

  [[nodiscard]] static Maybe<uint32_t> FeaturesIfActiveAndUnpaused() {
    if (uint32_t af = sActiveAndFeatures; (af & (Active | Paused)) == Active) {
      // Active but not fully paused, remove the Active and sampling-paused bits
      // to get all features.
      return Some(af & ~(Active | SamplingPaused));
    }
    return Nothing();
  }

  // This implementation must be kept in sync with `gecko_profiler::is_active`
  // in the Profiler Rust API.
  [[nodiscard]] static bool IsActive() {
    return uint32_t(sActiveAndFeatures) & Active;
  }

  [[nodiscard]] static bool IsActiveWithFeature(uint32_t aFeature) {
    uint32_t af = sActiveAndFeatures;  // copy it first
    return (af & Active) && (af & aFeature);
  }

  [[nodiscard]] static bool IsActiveWithoutFeature(uint32_t aFeature) {
    uint32_t af = sActiveAndFeatures;  // copy it first
    return (af & Active) && !(af & aFeature);
  }

  // True if profiler is active, and not fully paused.
  // Note that periodic sampling *could* be paused!
  // This implementation must be kept in sync with
  // `gecko_profiler::is_active_and_unpaused` in the Profiler Rust API.
  [[nodiscard]] static bool IsActiveAndUnpaused() {
    uint32_t af = sActiveAndFeatures;  // copy it first
    return (af & Active) && !(af & Paused);
  }

  // True if profiler is active, and sampling is not paused (though generic
  // `SetPaused()` or specific `SetSamplingPaused()`).
  [[nodiscard]] static bool IsActiveAndSamplingUnpaused() {
    uint32_t af = sActiveAndFeatures;  // copy it first
    return (af & Active) && !(af & (Paused | SamplingPaused));
  }

  [[nodiscard]] static bool IsCollectingMarkers() {
    uint32_t af = sActiveAndFeatures;  // copy it first
    return ((af & Active) && !(af & Paused)) || (af & ETWCollectionEnabled) ||
           (af & PerfettoTracingEnabled);
  }

  // This implementation must be kept in sync with
  // `gecko_profiler::is_etw_collecting_markers` in the Profiler Rust API.
  [[nodiscard]] static bool IsETWCollecting() {
    uint32_t af = sActiveAndFeatures;  // copy it first
    return (af & ETWCollectionEnabled);
  }

  // This implementation must be kept in sync with
  // `gecko_profiler::is_perfetto_tracing` in the Profiler Rust API.
  [[nodiscard]] static bool IsPerfettoTracing() {
    uint32_t af = sActiveAndFeatures;  // copy it first
    return (af & PerfettoTracingEnabled);
  }

 private:
  static constexpr uint32_t Active = 1u << 31;
  static constexpr uint32_t Paused = 1u << 30;
  static constexpr uint32_t SamplingPaused = 1u << 29;
  static constexpr uint32_t ETWCollectionEnabled = 1u << 28;
  static constexpr uint32_t PerfettoTracingEnabled = 1u << 27;

// Ensure Active/Paused don't overlap with any of the feature bits.
#define NO_OVERLAP(n_, str_, Name_, desc_)                \
  static_assert(ProfilerFeature::Name_ != SamplingPaused, \
                "bad feature "                            \
                "value");

  PROFILER_FOR_EACH_FEATURE(NO_OVERLAP);

#undef NO_OVERLAP

  // We combine the active bit with the feature bits so they can be read or
  // written in a single atomic operation.
  static Atomic<uint32_t, MemoryOrdering::Relaxed> sActiveAndFeatures;
};

}  // namespace mozilla::profiler::detail

//---------------------------------------------------------------------------
// Get information from the profiler
//---------------------------------------------------------------------------

// Is the profiler active? Note: the return value of this function can become
// immediately out-of-date. E.g. the profile might be active but then
// profiler_stop() is called immediately afterward. One common and reasonable
// pattern of usage is the following:
//
//   if (profiler_is_active()) {
//     ExpensiveData expensiveData = CreateExpensiveData();
//     PROFILER_OPERATION(expensiveData);
//   }
//
// where PROFILER_OPERATION is a no-op if the profiler is inactive. In this
// case the profiler_is_active() check is just an optimization -- it prevents
// us calling CreateExpensiveData() unnecessarily in most cases, but the
// expensive data will end up being created but not used if another thread
// stops the profiler between the CreateExpensiveData() and PROFILER_OPERATION
// calls.
[[nodiscard]] inline bool profiler_is_active() {
  return mozilla::profiler::detail::RacyFeatures::IsActive();
}

// Same as profiler_is_active(), but also checks if the profiler is not paused.
[[nodiscard]] inline bool profiler_is_active_and_unpaused() {
  return mozilla::profiler::detail::RacyFeatures::IsActiveAndUnpaused();
}

// Same as profiler_is_active_and_unpaused(), but also checks if the  ETW is
// collecting markers.
[[nodiscard]] inline bool profiler_is_collecting_markers() {
  return mozilla::profiler::detail::RacyFeatures::IsCollectingMarkers();
}

// Reports if our ETW tracelogger is running.
[[nodiscard]] inline bool profiler_is_etw_collecting_markers() {
  return mozilla::profiler::detail::RacyFeatures::IsETWCollecting();
}

[[nodiscard]] inline bool profiler_is_perfetto_tracing() {
  return mozilla::profiler::detail::RacyFeatures::IsPerfettoTracing();
}

// Is the profiler active and paused? Returns false if the profiler is inactive.
[[nodiscard]] bool profiler_is_paused();

// Is the profiler active and sampling is paused? Returns false if the profiler
// is inactive.
[[nodiscard]] bool profiler_is_sampling_paused();

// Get all the features supported by the profiler that are accepted by
// profiler_start(). The result is the same whether the profiler is active or
// not.
[[nodiscard]] uint32_t profiler_get_available_features();

// Returns the full feature set if the profiler is active.
// Note: the return value can become immediately out-of-date, much like the
// return value of profiler_is_active().
[[nodiscard]] inline mozilla::Maybe<uint32_t> profiler_features_if_active() {
  return mozilla::profiler::detail::RacyFeatures::FeaturesIfActive();
}

// Returns the full feature set if the profiler is active and unpaused.
// Note: the return value can become immediately out-of-date, much like the
// return value of profiler_is_active().
[[nodiscard]] inline mozilla::Maybe<uint32_t>
profiler_features_if_active_and_unpaused() {
  return mozilla::profiler::detail::RacyFeatures::FeaturesIfActiveAndUnpaused();
}

// Check if a profiler feature (specified via the ProfilerFeature type) is
// active. Returns false if the profiler is inactive. Note: the return value
// can become immediately out-of-date, much like the return value of
// profiler_is_active().
[[nodiscard]] bool profiler_feature_active(uint32_t aFeature);

// Check if the profiler is active without a feature (specified via the
// ProfilerFeature type). Note: the return value can become immediately
// out-of-date, much like the return value of profiler_is_active().
[[nodiscard]] bool profiler_active_without_feature(uint32_t aFeature);

// Returns true if any of the profiler mutexes are currently locked *on the
// current thread*. This may be used by re-entrant code that may call profiler
// functions while the same of a different profiler mutex is locked, which could
// deadlock.
[[nodiscard]] bool profiler_is_locked_on_current_thread();

// Install a callback to be invoked at any of the given profiling state changes.
// An optional non-zero identifier may be given, to allow later removal of the
// callback, the caller is responsible for making sure it's really unique (e.g.,
// by using a pointer to an object it owns.)
void profiler_add_state_change_callback(
    ProfilingStateSet aProfilingStateSet,
    ProfilingStateChangeCallback&& aCallback, uintptr_t aUniqueIdentifier = 0);

// Remove the callback with the given non-zero identifier.
void profiler_remove_state_change_callback(uintptr_t aUniqueIdentifier);

#endif  // ProfilerState_h
