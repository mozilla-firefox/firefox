/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// There are three kinds of samples done by the profiler.
//
// - A "periodic" sample is the most complex kind. It is done in response to a
//   timer while the profiler is active. It involves writing a stack trace plus
//   a variety of other values (memory measurements, responsiveness
//   measurements, markers, etc.) into the main ProfileBuffer. The sampling is
//   done from off-thread, and so SuspendAndSampleAndResumeThread() is used to
//   get the register values.
//
// - A "synchronous" sample is a simpler kind. It is done in response to an API
//   call (profiler_get_backtrace()). It involves writing a stack trace and
//   little else into a temporary ProfileBuffer, and wrapping that up in a
//   ProfilerBacktrace that can be subsequently used in a marker. The sampling
//   is done on-thread, and so REGISTERS_SYNC_POPULATE() is used to get the
//   register values.
//
// - A "backtrace" sample is the simplest kind. It is done in response to an
//   API call (profiler_suspend_and_sample_thread()). It involves getting a
//   stack trace via a ProfilerStackCollector; it does not write to a
//   ProfileBuffer. The sampling is done from off-thread, and so uses
//   SuspendAndSampleAndResumeThread() to get the register values.

#include "platform.h"

#include "GeckoProfiler.h"
#include "GeckoProfilerReporter.h"
#include "PageInformation.h"
#include "PowerCounters.h"
#include "ProfileBuffer.h"
#include "ProfiledThreadData.h"
#include "ProfilerBacktrace.h"
#include "ProfilerChild.h"
#include "ProfilerCodeAddressService.h"
#include "ProfilerControl.h"
#include "ProfilerCPUFreq.h"
#include "ProfilerIOInterposeObserver.h"
#include "ProfilerParent.h"
#include "ProfilerNativeStack.h"
#include "ProfilerStackWalk.h"
#include "ProfilerRustBindings.h"
#include "mozilla/Assertions.h"
#include "mozilla/HelperMacros.h"
#include "mozilla/PodOperations.h"
#include "mozilla/Likely.h"
#include "mozilla/Maybe.h"
#include "mozilla/MozPromise.h"
#include "mozilla/Perfetto.h"
#include "nsID.h"
#include "nsIDUtils.h"
#include "nsString.h"
#include "nsCExternalHandlerService.h"
#include "nsCOMPtr.h"
#include "nsDebug.h"
#include "nsISupports.h"
#include "nsXPCOM.h"
#include "VTuneProfiler.h"
#include "ETWTools.h"

#include "js/ProfilingFrameIterator.h"
#include "memory_counter.h"
#include "memory_hooks.h"
#include "memory_markers.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/ArrayAlgorithm.h"
#include "mozilla/BaseAndGeckoProfilerDetail.h"
#include "mozilla/BaseProfiler.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/ExtensionPolicyService.h"
#include "mozilla/extensions/WebExtensionPolicy.h"
#include "mozilla/FOGIPC.h"
#include "mozilla/glean/ProcesstoolsMetrics.h"
#include "mozilla/Monitor.h"
#include "mozilla/Preferences.h"
#include "mozilla/Printf.h"
#include "mozilla/ProcInfo.h"
#include "mozilla/ProfilerBufferSize.h"
#include "mozilla/ProfileBufferChunkManagerSingle.h"
#include "mozilla/ProfileBufferChunkManagerWithLocalLimit.h"
#include "mozilla/ProfileChunkedBuffer.h"
#include "mozilla/ProfilerBandwidthCounter.h"
#include "mozilla/ProfilerDumpOrCrash.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/SharedLibraries.h"
#include "mozilla/Services.h"
#include "mozilla/StackWalk.h"
#include "mozilla/SyncRunnable.h"
#include "mozilla/Try.h"
#ifdef XP_WIN
#  include "mozilla/NativeNt.h"
#  include "mozilla/StackWalkThread.h"
#  include "mozilla/WindowsStackWalkInitialization.h"
#endif
#include "mozilla/StaticPtr.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/Vector.h"
#include "nsDirectoryServiceDefs.h"
#include "nsDirectoryServiceUtils.h"
#include "nsIDocShell.h"
#include "nsIHttpProtocolHandler.h"
#include "nsIObserverService.h"
#include "nsIPropertyBag2.h"
#include "nsIXULAppInfo.h"
#include "nsIXULRuntime.h"
#include "nsJSPrincipals.h"
#include "nsMemoryReporterManager.h"
#include "nsPIDOMWindow.h"
#include "nsProfilerStartParams.h"
#include "nsScriptSecurityManager.h"
#include "nsSystemInfo.h"
#include "nsThreadUtils.h"
#include "nsXULAppAPI.h"
#include "xpcpublic.h"
#include "nsDirectoryServiceUtils.h"
#include "Tracing.h"
#include "prdtoa.h"
#include "prtime.h"

#include <algorithm>
#include <errno.h>
#include <fstream>
#include <set>
#include <sstream>
#include <string_view>
#include <type_traits>

// To simplify other code in this file, define a helper definition to avoid
// repeating the same preprocessor checks.

// The signals that we use to control the profiler conflict with the signals
// used to control the code coverage tool. Therefore, if coverage is enabled,
// we need to disable our own signal handling mechanisms.
#ifndef MOZ_CODE_COVERAGE
#  ifdef XP_WIN
// TODO: Add support for windows "signal"-like behaviour. See Bug 1867328.
#  elif defined(GP_OS_darwin) || defined(GP_OS_linux) || \
      defined(GP_OS_android) || defined(GP_OS_freebsd)
// Specify the specific platforms that we want to support
#    define GECKO_PROFILER_ASYNC_POSIX_SIGNAL_CONTROL 1
#  else
// No support on this unknown platform!
#  endif
#endif

// We need some extra includes if we're supporting async posix signals
#if defined(GECKO_PROFILER_ASYNC_POSIX_SIGNAL_CONTROL)
#  include <signal.h>
#  include <fcntl.h>
#  include <unistd.h>
#  include <errno.h>
#  include <pthread.h>
#endif

#if defined(GP_OS_android)
#  include "JavaExceptions.h"
#  include "mozilla/java/GeckoJavaSamplerNatives.h"
#  include "mozilla/jni/Refs.h"
#endif

#if defined(XP_MACOSX)
#  include "nsCocoaFeatures.h"
#endif

#if defined(GP_PLAT_amd64_darwin)
#  include <cpuid.h>
#endif

#if defined(GP_OS_windows)
#  include <processthreadsapi.h>

// GetThreadInformation is not available on Windows 7.
WINBASEAPI
BOOL WINAPI GetThreadInformation(
    _In_ HANDLE hThread, _In_ THREAD_INFORMATION_CLASS ThreadInformationClass,
    _Out_writes_bytes_(ThreadInformationSize) LPVOID ThreadInformation,
    _In_ DWORD ThreadInformationSize);

#endif

// Win32 builds always have frame pointers, so FramePointerStackWalk() always
// works.
#if defined(GP_PLAT_x86_windows)
#  define HAVE_NATIVE_UNWIND
#  define USE_FRAME_POINTER_STACK_WALK
#endif

// Win64 builds always omit frame pointers, so we use the slower
// MozStackWalk(), which works in that case.
#if defined(GP_PLAT_amd64_windows)
#  define HAVE_NATIVE_UNWIND
#  define USE_MOZ_STACK_WALK
#endif

// AArch64 Win64 doesn't seem to use frame pointers, so we use the slower
// MozStackWalk().
#if defined(GP_PLAT_arm64_windows)
#  define HAVE_NATIVE_UNWIND
#  define USE_MOZ_STACK_WALK
#endif

// Mac builds use FramePointerStackWalk(). Even if we build without
// frame pointers, we'll still get useful stacks in system libraries
// because those always have frame pointers.
// We don't use MozStackWalk() on Mac.
#if defined(GP_OS_darwin)
#  define HAVE_NATIVE_UNWIND
#  define USE_FRAME_POINTER_STACK_WALK
#endif

// Android builds use the ARM Exception Handling ABI to unwind.
#if defined(GP_PLAT_arm_linux) || defined(GP_PLAT_arm_android)
#  define HAVE_NATIVE_UNWIND
#  define USE_EHABI_STACKWALK
#  include "EHABIStackWalk.h"
#endif

// Linux/BSD builds use LUL, which uses DWARF info to unwind stacks.
#if defined(GP_PLAT_amd64_linux) || defined(GP_PLAT_x86_linux) ||       \
    defined(GP_PLAT_amd64_android) || defined(GP_PLAT_x86_android) ||   \
    defined(GP_PLAT_mips64_linux) || defined(GP_PLAT_arm64_linux) ||    \
    defined(GP_PLAT_arm64_android) || defined(GP_PLAT_amd64_freebsd) || \
    defined(GP_PLAT_arm64_freebsd)
#  define HAVE_NATIVE_UNWIND
#  define USE_LUL_STACKWALK
#  include "lul/LulMain.h"
#  include "lul/platform-linux-lul.h"

// On linux we use LUL for periodic samples and synchronous samples, but we use
// FramePointerStackWalk for backtrace samples when MOZ_PROFILING is enabled.
// (See the comment at the top of the file for a definition of
// periodic/synchronous/backtrace.).
//
// FramePointerStackWalk can produce incomplete stacks when the current entry is
// in a shared library without framepointers, however LUL can take a long time
// to initialize, which is undesirable for consumers of
// profiler_suspend_and_sample_thread like the Background Hang Reporter.
#  if defined(MOZ_PROFILING)
#    define USE_FRAME_POINTER_STACK_WALK
#  endif
#endif

// We can only stackwalk without expensive initialization on platforms which
// support FramePointerStackWalk or MozStackWalk. LUL Stackwalking requires
// initializing LUL, and EHABIStackWalk requires initializing EHABI, both of
// which can be expensive.
#if defined(USE_FRAME_POINTER_STACK_WALK) || defined(USE_MOZ_STACK_WALK)
#  define HAVE_FASTINIT_NATIVE_UNWIND
#endif

#ifdef MOZ_VALGRIND
#  include <valgrind/memcheck.h>
#else
#  define VALGRIND_MAKE_MEM_DEFINED(_addr, _len) ((void)0)
#endif

#if defined(GP_OS_linux) || defined(GP_OS_android) || defined(GP_OS_freebsd)
#  include <ucontext.h>
#endif

using namespace mozilla;
using namespace mozilla::literals::ProportionValue_literals;

using mozilla::profiler::detail::RacyFeatures;
using ThreadRegistration = mozilla::profiler::ThreadRegistration;
using ThreadRegistrationInfo = mozilla::profiler::ThreadRegistrationInfo;
using ThreadRegistry = mozilla::profiler::ThreadRegistry;

LazyLogModule gProfilerLog("prof");

ProfileChunkedBuffer& profiler_get_core_buffer() {
  // Defer to the Base Profiler in mozglue to create the core buffer if needed,
  // and keep a reference here, for quick access in xul.
  static ProfileChunkedBuffer& sProfileChunkedBuffer =
      baseprofiler::profiler_get_core_buffer();
  return sProfileChunkedBuffer;
}

#if defined(GECKO_PROFILER_ASYNC_POSIX_SIGNAL_CONTROL)
// Control character to start the profiler ('g' for "go"!)
static const char sAsyncSignalControlCharStart = 'g';
// Control character to stop the profiler ('s' for "stop"!)
static const char sAsyncSignalControlCharStop = 's';

// This is a file descriptor that is the "write" end of the POSIX pipe that we
// use to start the profiler. It is written to in profiler_start_signal_handler
// and read from in AsyncSignalControlThread
static mozilla::Atomic<int, mozilla::MemoryOrdering::Relaxed>
    sAsyncSignalControlWriteFd(-1);

// Atomic flag to stop the profiler from within the sampling loop
mozilla::Atomic<bool, mozilla::MemoryOrdering::Relaxed> gStopAndDumpFromSignal(
    false);
#endif

// Forward declare the function to call when we need to dump + stop from within
// the async control thread
void profiler_dump_and_stop();
// Forward declare the function to call when we need to start the profiler.
void profiler_start_from_signal();

#if defined(GP_OS_darwin)
// Forward declare thermal state observer function (macOS only)
void profiler_register_thermal_state_observer();
#endif

mozilla::Atomic<int, mozilla::MemoryOrdering::Relaxed> gSkipSampling;

#if defined(GP_OS_android)
class GeckoJavaSampler
    : public java::GeckoJavaSampler::Natives<GeckoJavaSampler> {
 private:
  GeckoJavaSampler();

 public:
  static double GetProfilerTime() {
    if (!profiler_is_active()) {
      return 0.0;
    }
    return profiler_time();
  };

  static void JavaStringArrayToCharArray(
      jni::ObjectArray::Param& aJavaArray,
      Vector<UniqueFreePtr<char>>& aCharArray, JNIEnv* aJni) {
    int arraySize = aJavaArray->Length();
    for (int i = 0; i < arraySize; i++) {
      jstring javaString =
          (jstring)(aJni->GetObjectArrayElement(aJavaArray.Get(), i));
      const char* filterString = aJni->GetStringUTFChars(javaString, 0);
      if (filterString != nullptr) {
        int filterStringLen = aJni->GetStringUTFLength(javaString);
        MOZ_RELEASE_ASSERT(
            aCharArray.append(strndup(filterString, filterStringLen)));
        aJni->ReleaseStringUTFChars(javaString, filterString);
      }
    }
  }

  static void StartProfiler(jni::ObjectArray::Param aFiltersArray,
                            jni::ObjectArray::Param aFeaturesArray) {
    JNIEnv* jni = jni::GetEnvForThread();
    Vector<UniqueFreePtr<char>> filtersTemp;
    Vector<UniqueFreePtr<char>> featureStringArray;

    JavaStringArrayToCharArray(aFiltersArray, filtersTemp, jni);
    JavaStringArrayToCharArray(aFeaturesArray, featureStringArray, jni);

    uint32_t features = 0;
    auto convertToPtr = [](const UniqueFreePtr<char>& ptr) -> const char* {
      return ptr.get();
    };
    auto featureStringArrayPtr =
        mozilla::TransformIntoNewArray(featureStringArray, convertToPtr);
    features = ParseFeaturesFromStringArray(featureStringArrayPtr.Elements(),
                                            featureStringArrayPtr.Length());

    // 128 * 1024 * 1024 is the entries preset that is given in
    // devtools/client/performance-new/shared/background.sys.mjs
    auto filtersTempPtr =
        mozilla::TransformIntoNewArray(filtersTemp, convertToPtr);
    profiler_start(PowerOfTwo32(128u * 1024 * 1024), 5.0, features,
                   filtersTempPtr.Elements(), filtersTempPtr.Length(), 0,
                   Nothing());
  }

  static void StopProfiler(jni::Object::Param aGeckoResult) {
    auto result = java::GeckoResult::LocalRef(aGeckoResult);
    profiler_pause();
    nsCOMPtr<nsIProfiler> nsProfiler(
        do_GetService("@mozilla.org/tools/profiler;1"));
    nsProfiler->GetProfileDataAsGzippedArrayBufferAndroid(0)->Then(
        GetMainThreadSerialEventTarget(), __func__,
        [result](FallibleTArray<uint8_t> compressedProfile) {
          result->Complete(jni::ByteArray::New(
              reinterpret_cast<const int8_t*>(compressedProfile.Elements()),
              compressedProfile.Length()));

          // Done with capturing a profile. Stop the profiler.
          profiler_stop();
        },
        [result](nsresult aRv) {
          char errorString[9];
          sprintf(errorString, "%08x", uint32_t(aRv));
          result->CompleteExceptionally(
              mozilla::java::sdk::IllegalStateException::New(errorString)
                  .Cast<jni::Throwable>());

          // Failed to capture a profile. Stop the profiler.
          profiler_stop();
        });
  }
};
#endif

constexpr static bool ValidateFeatures() {
  int expectedFeatureNumber = 0;

  // Feature numbers should start at 0 and increase by 1 each.
#define CHECK_FEATURE(n_, str_, Name_, desc_) \
  if ((n_) != expectedFeatureNumber) {        \
    return false;                             \
  }                                           \
  ++expectedFeatureNumber;

  PROFILER_FOR_EACH_FEATURE(CHECK_FEATURE)

#undef CHECK_FEATURE

  return true;
}

static_assert(ValidateFeatures(), "Feature list is invalid");

// Return all features that are available on this platform.
static uint32_t AvailableFeatures() {
  uint32_t features = 0;

#define ADD_FEATURE(n_, str_, Name_, desc_) \
  ProfilerFeature::Set##Name_(features);

  // Add all the possible features.
  PROFILER_FOR_EACH_FEATURE(ADD_FEATURE)

#undef ADD_FEATURE

  // Now remove features not supported on this platform/configuration.
#if !defined(GP_OS_android)
  ProfilerFeature::ClearJava(features);
#endif
#if !defined(HAVE_NATIVE_UNWIND)
  ProfilerFeature::ClearStackWalk(features);
#endif
#if defined(MOZ_REPLACE_MALLOC) && defined(MOZ_PROFILER_MEMORY)
  if (getenv("XPCOM_MEM_BLOAT_LOG")) {
    DEBUG_LOG("XPCOM_MEM_BLOAT_LOG is set, disabling native allocations.");
    // The memory hooks are available, but the bloat log is enabled, which is
    // not compatible with the native allocations tracking. See the comment in
    // enable_native_allocations() (tools/profiler/core/memory_hooks.cpp) for
    // more information.
    ProfilerFeature::ClearNativeAllocations(features);
  }
#else
  // The memory hooks are not available.
  ProfilerFeature::ClearNativeAllocations(features);
#endif
#if !defined(MOZ_MEMORY) or !defined(MOZ_PROFILER_MEMORY)
  ProfilerFeature::ClearMemory(features);
#endif

#if !defined(GP_OS_windows)
  ProfilerFeature::ClearNoTimerResolutionChange(features);
#endif
#if !defined(HAVE_CPU_FREQ_SUPPORT)
  ProfilerFeature::ClearCPUFrequency(features);
#endif

  return features;
}

// Default features common to all contexts (even if not available).
static constexpr uint32_t DefaultFeatures() {
  return ProfilerFeature::Java | ProfilerFeature::JS |
         ProfilerFeature::StackWalk | ProfilerFeature::Screenshots |
         ProfilerFeature::ProcessCPU;
}

// Extra default features when MOZ_PROFILER_STARTUP is set (even if not
// available).
static constexpr uint32_t StartupExtraDefaultFeatures() {
  // Enable file I/Os by default for startup profiles as startup is heavy on
  // I/O operations.
  return ProfilerFeature::FileIOAll | ProfilerFeature::IPCMessages;
}

Json::String ToCompactString(const Json::Value& aJsonValue) {
  Json::StreamWriterBuilder builder;
  // No indentations, and no newlines.
  builder["indentation"] = "";
  // This removes spaces after colons.
  builder["enableYAMLCompatibility"] = false;
  // Only 6 digits after the decimal point; timestamps in ms have ns precision.
  builder["precision"] = 6;
  builder["precisionType"] = "decimal";

  return Json::writeString(builder, aJsonValue);
}

MOZ_RUNINIT /* static */ mozilla::baseprofiler::detail::BaseProfilerMutex
    ProfilingLog::gMutex;
constinit /* static */ mozilla::UniquePtr<Json::Value> ProfilingLog::gLog;

/* static */ void ProfilingLog::Init() {
  mozilla::baseprofiler::detail::BaseProfilerAutoLock lock{gMutex};
  MOZ_ASSERT(!gLog);
  gLog = mozilla::MakeUniqueFallible<Json::Value>(Json::objectValue);
  if (gLog) {
    (*gLog)[Json::StaticString{"profilingLogBegin" TIMESTAMP_JSON_SUFFIX}] =
        ProfilingLog::Timestamp();
  }
}

/* static */ void ProfilingLog::Destroy() {
  mozilla::baseprofiler::detail::BaseProfilerAutoLock lock{gMutex};
  MOZ_ASSERT(gLog);
  gLog = nullptr;
}

/* static */ bool ProfilingLog::IsLockedOnCurrentThread() {
  return gMutex.IsLockedOnCurrentThread();
}

// RAII class to lock the profiler mutex.
// It provides a mechanism to determine if it is locked or not in order for
// memory hooks to avoid re-entering the profiler locked state.
// Locking order: Profiler, ThreadRegistry, ThreadRegistration.
class MOZ_RAII PSAutoLock {
 public:
  PSAutoLock()
      : mLock([]() -> mozilla::baseprofiler::detail::BaseProfilerMutex& {
          // In DEBUG builds, *before* we attempt to lock gPSMutex, we want to
          // check that the ThreadRegistry, ThreadRegistration, and ProfilingLog
          // mutexes are *not* locked on this thread, to avoid inversion
          // deadlocks.
          MOZ_ASSERT(!ThreadRegistry::IsRegistryMutexLockedOnCurrentThread());
          MOZ_ASSERT(!ThreadRegistration::IsDataMutexLockedOnCurrentThread());
          MOZ_ASSERT(!ProfilingLog::IsLockedOnCurrentThread());
          return gPSMutex;
        }()) {}

  PSAutoLock(const PSAutoLock&) = delete;
  void operator=(const PSAutoLock&) = delete;

  static bool IsLockedOnCurrentThread() {
    return gPSMutex.IsLockedOnCurrentThread();
  }

 private:
  static mozilla::baseprofiler::detail::BaseProfilerMutex gPSMutex;
  mozilla::baseprofiler::detail::BaseProfilerAutoLock mLock;
};

MOZ_RUNINIT /* static */ mozilla::baseprofiler::detail::BaseProfilerMutex
    PSAutoLock::gPSMutex{"Gecko Profiler mutex"};

// Only functions that take a PSLockRef arg can access CorePS's and ActivePS's
// fields.
typedef const PSAutoLock& PSLockRef;

#define PS_GET(type_, name_)      \
  static type_ name_(PSLockRef) { \
    MOZ_ASSERT(sInstance);        \
    return sInstance->m##name_;   \
  }

#define PS_GET_LOCKLESS(type_, name_) \
  static type_ name_() {              \
    MOZ_ASSERT(sInstance);            \
    return sInstance->m##name_;       \
  }

#define PS_GET_AND_SET(type_, name_)                  \
  PS_GET(type_, name_)                                \
  static void Set##name_(PSLockRef, type_ a##name_) { \
    MOZ_ASSERT(sInstance);                            \
    sInstance->m##name_ = a##name_;                   \
  }

static constexpr size_t MAX_JS_FRAMES =
    mozilla::profiler::ThreadRegistrationData::MAX_JS_FRAMES;
using JsFrame = mozilla::profiler::ThreadRegistrationData::JsFrame;
using JsFrameBuffer = mozilla::profiler::ThreadRegistrationData::JsFrameBuffer;

#if defined(GECKO_PROFILER_ASYNC_POSIX_SIGNAL_CONTROL)

// ASYNC POSIX SIGNAL HANDLING SUPPORT
//
// Integrating POSIX signals
// (https://man7.org/linux/man-pages/man7/signal.7.html) into a complex
// multi-threaded application such as Firefox can be a tricky proposition.
// Signals are delivered by the operating system to a program, which then
// invokes a signal handler
// (https://man7.org/linux/man-pages/man2/sigaction.2.html) outside the normal
// flow of control. This handler is responsible for performing operations in
// response to the signal. If there is no "custom" handler defined, then default
// behaviour is triggered, which usually results in a terminated program.
//
// As signal handlers interrupt the normal flow of control, Firefox may not be
// in a safe state while the handler is running (e.g. it may be halfway through
// a garbage collection cycle, or a critical lock may be held by the current
// thread). This is something we must be aware of while writing one, and we are
// additionally limited in terms of which POSIX functions we can call to those
// which are async signal safe
// (https://man7.org/linux/man-pages/man7/signal-safety.7.html).
//
// In the context of Firefox, this presents a number of details that we must be
// aware of:
//
// * We are very limited by what we can call when we handle a signal: Many
//   functions in Firefox, and in the profiler specifically, allocate memory
//   when called. Allocating memory is specifically **not** async-signal-safe,
//   and so any functions that allocate should not be called from a signal
//   handler.
//
// * We need to be careful with how we communicate to other threads in the
//   process. The signal handler runs asynchronously, interrupting the current
//   thread of execution. Communication should therefore use atomics or other
//   concurrency constructs to ensure that data is read and written correctly.
//   We should avoid taking locks, as we may easily deadlock while within the
//   signal handler.
//
// * We cannot use the usual Firefox mechanisms for triggering behaviour in
//   other threads. For instance, tools such as ``NS_DispatchToMainThread``
//   allocate memory when called, which is not allowed within a signal handler.
//
// We solve these constraints by introducing a new thread within the Firefox
// profiler, the AsyncSignalControlThread which is responsible for carrying out
// the actions triggered by a signal handler. We communicate between handlers
// and this thread with the use of a libc pipe
// (https://pubs.opengroup.org/onlinepubs/9699919799/functions/write.html#tag_16_685_08).
// Writing to a pipe is async-signal-safe, so we can do so from a signal
// handler, and we can set the pipe to be "blocking", meaning that when our
// control thread tries to read it will block at the OS level (consuming no CPU)
// until the handler writes to it. This is in contrast to (e.g.) an atomic
// variable, where our thread would have to "busy wait" for it to be set.
//
// We have one "control" thread per process, and use a single byte for messages
// we send. Writes to pipes are atomic if the size is less than or equal to
// ``PIPE_BUF``, which (although implementation defined) in our case is always
// one, thus trivially atomic.
//
// The control flow for a typical Firefox session in which a user starts and
// stops profiling using POSIX signals therefore looks something like the
// following:
//
// * Profiler initialization.
//
//   * The main thread of each process starts the signal control thread, and
//     initialises signal handlers for ``SIGUSR1`` and ``SIGSUR2``.
//   * The signal control thread sets up pipes for communication, and begins
//     reading, blocking itself.
//
// * *After some time...*
// * The user sends ``SIGUSR1`` to Firefox, e.g. using ``kill -s USR1 <firefox
//   pid>``
//
//   * The profiler_start_signal_handler signal handler for ``SIGUSR1`` is
//     triggered by the operating system. This writes the "start" control
//     character to the communication pipe and returns.
//   * The signal control thread wakes up, as there is now data on the pipe.
//   * The control thread recognises the "start" character, and starts the
//     profiler with a set of default presets.
//   * The control thread loops, and goes back to waiting on the pipe.
//
// * *The user uses Firefox, or waits for it to do something...*
// * The user sends ``SIGUSR2`` to Firefox, e.g. using ``kill -s USR1 <firefox
//   pid>``
//
//   * The profiler_stop_signal_handler signal handler for ``SIGUSR2`` is
//     triggered by the operating system. This writes the "stop" control
//     character to the communication pipe and returns.
//   * The signal control thread wakes up, as there is now data on the pipe.
//   * The control thread recognises the "stop" character, and calls
//     profiler_stop_signal_handler to dump the profile to disk.
//   * The control thread loops, and goes back to waiting on the pipe.
//
// * *The user can now start another profiling session...*
//

// Forward declare this, so we can call it from the constructor.
static void* AsyncSignalControlThreadEntry(void* aArg);

// Define our platform specific async (posix) signal control thread here.
class AsyncSignalControlThread {
 public:
  AsyncSignalControlThread() : mThread() {
    // Try to open a pipe for this to communicate with. If we can't do this,
    // then we give up and return, as there's no point continuing without
    // being able to communicate
    int pipeFds[2];
    if (pipe(pipeFds)) {
      LOG("Profiler AsyncSignalControlThread failed to create a pipe.");
      return;
    }

    // Close this pipe on calls to exec().
    fcntl(pipeFds[0], F_SETFD, FD_CLOEXEC);
    fcntl(pipeFds[1], F_SETFD, FD_CLOEXEC);

    // Write the reading side to mFd, and the writing side to the global atomic
    mFd = pipeFds[0];
    sAsyncSignalControlWriteFd = pipeFds[1];

    // We don't really care about stack size, as it should be minimal, so
    // leave the pthread attributes as a nullptr, i.e. choose the default.
    pthread_attr_t* attr_ptr = nullptr;
    if (pthread_create(&mThread, attr_ptr, AsyncSignalControlThreadEntry,
                       this) != 0) {
      MOZ_CRASH("pthread_create failed");
    }
  };

  ~AsyncSignalControlThread() {
    // Derived from code in nsDumpUtils.cpp. Comment reproduced here for
    // poisterity: Close sAsyncSignalControlWriteFd /after/ setting the fd to
    // -1. Otherwise we have the (admittedly far-fetched) race where we
    //
    //  1) close sAsyncSignalControlWriteFd
    //  2) open a new fd with the same number as sAsyncSignalControlWriteFd
    //     had.
    //  3) receive a signal, then write to the fd.
    int asyncSignalControlWriteFd = sAsyncSignalControlWriteFd.exchange(-1);
    // This will unblock the "read" in StartWatching.
    close(asyncSignalControlWriteFd);
    // Finally, exit the thread.
    pthread_join(mThread, nullptr);
  };

  void Watch() {
    char msg[1];
    ssize_t nread;
    while (true) {
      // Try reading from the pipe. This will block until something is written:
      nread = read(mFd, msg, sizeof(msg));

      if (nread == -1 && errno == EINTR) {
        // nread == -1 and errno == EINTR means that `read` was interrupted
        // by a signal before reading any data. This is likely because the
        // profiling thread interrupted us (with SIGPROF). We can safely ignore
        // this and "go around" the loop again to try and read.
        continue;
      }

      if (nread == -1 && errno != EINTR) {
        // nread == -1 and errno != EINTR means that `read` has failed in some
        // way that we can't recover from. In this case, all we can do is give
        // up, and quit the watcher, as the pipe is likely broken.
        LOG("Error (%d) when reading in AsyncSignalControlThread", errno);
        return;
      }

      if (nread == 0) {
        // nread == 0 signals that the other end of the pipe has been cleanly
        // closed. Close our end, and exit the reading loop.
        close(mFd);
        return;
      }

      // If we reach here, nread != 0 and nread != -1. This means that we
      // should have read at least one byte, which should be a control byte
      // for the profiler.
      // It *might* happen that `read` is interrupted by the sampler thread
      // after successfully reading. If this occurs, read returns the number
      // of bytes read. As anything other than 1 is wrong for us, we can
      // always assume that we can read whatever `read` read.
      MOZ_RELEASE_ASSERT(nread == 1);

      if (msg[0] == sAsyncSignalControlCharStart) {
        // Check to see if the profiler is already running. This is done within
        // `profiler_start` anyway, but if we check sooner we avoid running all
        // the other code between now and that check.
        if (!profiler_is_active()) {
          profiler_start_from_signal();
        }
      } else if (msg[0] == sAsyncSignalControlCharStop) {
        // Check to see whether the profiler is even running before trying to
        // stop the profiler. Most other methods of stopping the profiler (i.e.
        // those through nsProfiler etc) already know whether or not the
        // profiler is running, so don't try and stop it if it's already
        // running. Signal-stopping doesn't have this constraint, so we should
        // check just in case there is a codepath followed by
        // `profiler_dump_and_stop` that breaks if we stop while stopped.
        if (profiler_is_active()) {
          profiler_dump_and_stop();
        }
      } else {
        LOG("AsyncSignalControlThread recieved unknown control signal: %c",
            msg[0]);
      }
    }
  };

 private:
  // The read side of the pipe that we use to communicate from a signal handler
  // to the AsyncSignalControlThread
  int mFd;

  // The thread handle for the async signal control thread
  // Note, that unlike the sampler thread, this is currently a posix-only
  // feature. Therefore, we don't bother to have a windows equivalent - we
  // just use a pthread_t
  pthread_t mThread;
};

static void* AsyncSignalControlThreadEntry(void* aArg) {
  NS_SetCurrentThreadName("AsyncSignalControlThread");
  auto* thread = static_cast<AsyncSignalControlThread*>(aArg);
  thread->Watch();
  return nullptr;
}
#endif

// All functions in this file can run on multiple threads unless they have an
// NS_IsMainThread() assertion.

// This class contains the profiler's core global state, i.e. that which is
// valid even when the profiler is not active. Most profile operations can't do
// anything useful when this class is not instantiated, so we release-assert
// its non-nullness in all such operations.
//
// Accesses to CorePS are guarded by gPSMutex. Getters and setters take a
// PSAutoLock reference as an argument as proof that the gPSMutex is currently
// locked. This makes it clear when gPSMutex is locked and helps avoid
// accidental unlocked accesses to global state. There are ways to circumvent
// this mechanism, but please don't do so without *very* good reason and a
// detailed explanation.
//
// The exceptions to this rule:
//
// - mProcessStartTime, because it's immutable;
class CorePS {
 private:
#ifdef MOZ_PERFETTO
  class PerfettoObserver : public perfetto::TrackEventSessionObserver {
   public:
    PerfettoObserver() { perfetto::TrackEvent::AddSessionObserver(this); }
    ~PerfettoObserver() { perfetto::TrackEvent::RemoveSessionObserver(this); }

    void OnStart(const perfetto::DataSourceBase::StartArgs&) override {
      mozilla::profiler::detail::RacyFeatures::SetPerfettoTracingActive();
    }
    void OnStop(const perfetto::DataSourceBase::StopArgs&) override {
      mozilla::profiler::detail::RacyFeatures::SetPerfettoTracingInactive();
    }
  } perfettoObserver;
#endif

  CorePS()
      : mProcessStartTime(TimeStamp::ProcessCreation()),
        mMaybeBandwidthCounter(nullptr)
#if defined(GECKO_PROFILER_ASYNC_POSIX_SIGNAL_CONTROL)
        ,
        mAsyncSignalControlThread(nullptr)
#endif
#ifdef USE_LUL_STACKWALK
        ,
        mLul(nullptr)
#endif
  {
    MOZ_ASSERT(NS_IsMainThread(),
               "CorePS must be created from the main thread");
  }

  ~CorePS() {
#if defined(GECKO_PROFILER_ASYNC_POSIX_SIGNAL_CONTROL)
    delete mAsyncSignalControlThread;
#endif
#ifdef USE_LUL_STACKWALK
    delete sInstance->mLul;
#endif
    delete mMaybeBandwidthCounter;
  }

 public:
  static void Create(PSLockRef aLock) {
    MOZ_ASSERT(!sInstance);
    sInstance = new CorePS();
  }

  static void Destroy(PSLockRef aLock) {
    MOZ_ASSERT(sInstance);
    delete sInstance;
    sInstance = nullptr;
  }

  // Unlike ActivePS::Exists(), CorePS::Exists() can be called without gPSMutex
  // being locked. This is because CorePS is instantiated so early on the main
  // thread that we don't have to worry about it being racy.
  static bool Exists() { return !!sInstance; }

  static void AddSizeOf(PSLockRef, MallocSizeOf aMallocSizeOf,
                        size_t& aProfSize, size_t& aLulSize) {
    MOZ_ASSERT(sInstance);

    aProfSize += aMallocSizeOf(sInstance);

    aProfSize += ThreadRegistry::SizeOfIncludingThis(aMallocSizeOf);

    for (auto& registeredPage : sInstance->mRegisteredPages) {
      aProfSize += registeredPage->SizeOfIncludingThis(aMallocSizeOf);
    }

    // Measurement of the following things may be added later if DMD finds it
    // is worthwhile:
    // - CorePS::mRegisteredPages itself (its elements' children are
    // measured above)

#if defined(USE_LUL_STACKWALK)
    if (lul::LUL* lulPtr = sInstance->mLul; lulPtr) {
      aLulSize += lulPtr->SizeOfIncludingThis(aMallocSizeOf);
    }
#endif
  }

  // No PSLockRef is needed for this field because it's immutable.
  PS_GET_LOCKLESS(TimeStamp, ProcessStartTime)

  PS_GET(JsFrameBuffer&, JsFrames)

  PS_GET(Vector<RefPtr<PageInformation>>&, RegisteredPages)

  // Takes the page fields rather than a PageInformation, so that the common
  // case of re-registering an already-known page doesn't have to allocate one
  // only to throw it away.
  static void AppendRegisteredPage(PSLockRef, uint64_t aTabID,
                                   uint64_t aInnerWindowID,
                                   const nsCString& aUrl,
                                   uint64_t aEmbedderInnerWindowID,
                                   bool aIsPrivateBrowsing) {
    MOZ_ASSERT(sInstance);

    // Inner window IDs are unique for each page, so they are enough to
    // identify an already-registered page.
    auto foundPageIter = std::find_if(
        sInstance->mRegisteredPages.begin(), sInstance->mRegisteredPages.end(),
        [aInnerWindowID](const RefPtr<PageInformation>& aPage) {
          return aPage->InnerWindowID() == aInnerWindowID;
        });

    if (foundPageIter != sInstance->mRegisteredPages.end()) {
      if ((*foundPageIter)->Url().EqualsLiteral("about:blank")) {
        // When a BrowsingContext is loaded, the first url loaded in it will be
        // about:blank, and if the principal matches, the first document loaded
        // in it will share an inner window. That's why we should delete the
        // intermittent about:blank if they share the inner window.
        sInstance->mRegisteredPages.erase(foundPageIter);
      } else {
        // Do not register the same page again.
        return;
      }
    }

    MOZ_RELEASE_ASSERT(
        sInstance->mRegisteredPages.append(MakeRefPtr<PageInformation>(
            aTabID, aInnerWindowID, aUrl, aEmbedderInnerWindowID,
            aIsPrivateBrowsing)));
  }

  static void RemoveRegisteredPage(PSLockRef,
                                   uint64_t aRegisteredInnerWindowID) {
    MOZ_ASSERT(sInstance);
    // Remove RegisteredPage from mRegisteredPages by given inner window ID.
    sInstance->mRegisteredPages.eraseIf([&](const RefPtr<PageInformation>& rd) {
      return rd->InnerWindowID() == aRegisteredInnerWindowID;
    });
  }

  static void ClearRegisteredPages(PSLockRef) {
    MOZ_ASSERT(sInstance);
    sInstance->mRegisteredPages.clear();
  }

  PS_GET(const Vector<BaseProfilerCount*>&, Counters)

  static void AppendCounter(PSLockRef, BaseProfilerCount* aCounter) {
    MOZ_ASSERT(sInstance);
    // we don't own the counter; they may be stored in static objects
    MOZ_RELEASE_ASSERT(sInstance->mCounters.append(aCounter));
  }

  static void RemoveCounter(PSLockRef, BaseProfilerCount* aCounter) {
    // we may be called to remove a counter after the profiler is stopped or
    // late in shutdown.
    if (sInstance) {
      auto* counter = std::find(sInstance->mCounters.begin(),
                                sInstance->mCounters.end(), aCounter);
      MOZ_RELEASE_ASSERT(counter != sInstance->mCounters.end());
      sInstance->mCounters.erase(counter);
    }
  }

#ifdef USE_LUL_STACKWALK
  static lul::LUL* Lul() {
    MOZ_RELEASE_ASSERT(sInstance);
    return sInstance->mLul;
  }
  static void SetLul(UniquePtr<lul::LUL> aLul) {
    MOZ_RELEASE_ASSERT(sInstance);
    MOZ_RELEASE_ASSERT(
        sInstance->mLul.compareExchange(nullptr, aLul.release()));
  }
#endif

  PS_GET_AND_SET(const nsACString&, ProcessName)
  PS_GET_AND_SET(const nsACString&, ETLDplus1)
#if !defined(XP_WIN)
  PS_GET_AND_SET(const Maybe<nsCOMPtr<nsIFile>>&, AsyncSignalDumpDirectory)
#endif

  // The scheduled off-main-thread profile dump deadline
  // (profiler_schedule_dump_to_file). A null deadline means none is scheduled.
  // Read by the sampler thread.
  static const TimeStamp& ScheduledDumpDeadline(PSLockRef) {
    MOZ_ASSERT(sInstance);
    return sInstance->mScheduledDumpDeadline;
  }
  static const nsACString& ScheduledDumpPath(PSLockRef) {
    MOZ_ASSERT(sInstance);
    return sInstance->mScheduledDumpPath;
  }
  static bool ScheduledDumpExitAfter(PSLockRef) {
    MOZ_ASSERT(sInstance);
    return sInstance->mScheduledDumpExitAfter;
  }
  static void ScheduleDumpToFile(PSLockRef, const TimeStamp& aDeadline,
                                 const nsACString& aPath, bool aExitAfterDump) {
    MOZ_ASSERT(sInstance);
    sInstance->mScheduledDumpDeadline = aDeadline;
    sInstance->mScheduledDumpPath = aPath;
    sInstance->mScheduledDumpExitAfter = aExitAfterDump;
  }
  static void CancelScheduledDump(PSLockRef) {
    MOZ_ASSERT(sInstance);
    sInstance->mScheduledDumpDeadline = TimeStamp{};
  }

  static void SetBandwidthCounter(ProfilerBandwidthCounter* aBandwidthCounter) {
    MOZ_ASSERT(sInstance);

    sInstance->mMaybeBandwidthCounter = aBandwidthCounter;
  }
  static ProfilerBandwidthCounter* GetBandwidthCounter() {
    MOZ_ASSERT(sInstance);

    return sInstance->mMaybeBandwidthCounter;
  }

#if defined(GECKO_PROFILER_ASYNC_POSIX_SIGNAL_CONTROL)
  static void SetAsyncSignalControlThread(
      AsyncSignalControlThread* aAsyncSignalControlThread) {
    MOZ_ASSERT(sInstance);
    sInstance->mAsyncSignalControlThread = aAsyncSignalControlThread;
  }
#endif

 private:
  // The singleton instance
  static CorePS* sInstance;

  // The time that the process started.
  const TimeStamp mProcessStartTime;

  // Network bandwidth counter for the Bandwidth feature.
  ProfilerBandwidthCounter* mMaybeBandwidthCounter;

  // Info on all the registered pages.
  // InnerWindowIDs in mRegisteredPages are unique.
  Vector<RefPtr<PageInformation>> mRegisteredPages;

  // Non-owning pointers to all active counters
  Vector<BaseProfilerCount*> mCounters;

#if defined(GECKO_PROFILER_ASYNC_POSIX_SIGNAL_CONTROL)
  // Background thread for communicating with async signal handlers
  AsyncSignalControlThread* mAsyncSignalControlThread;
#endif

#ifdef USE_LUL_STACKWALK
  // LUL's state. Null prior to the first activation, non-null thereafter.
  // Owned by this CorePS.
  mozilla::Atomic<lul::LUL*> mLul;
#endif

  // Process name, provided by child process initialization code.
  nsAutoCString mProcessName;
  // Private name, provided by child process initialization code (eTLD+1 in
  // fission)
  nsAutoCString mETLDplus1;

  // A scheduled off-main-thread profile dump (profiler_schedule_dump_to_file):
  // the deadline at which the sampler thread should write the profile, and the
  // file to write it to. Null deadline when none is scheduled.
  TimeStamp mScheduledDumpDeadline;
  nsAutoCString mScheduledDumpPath;
  // Whether the sampler thread should exit the process once it has written the
  // scheduled dump. Only meaningful while a dump is scheduled.
  bool mScheduledDumpExitAfter = false;

  // This memory buffer is used by the MergeStacks mechanism. Previously it was
  // stack allocated, but this led to a stack overflow, as it was too much
  // memory. Here the buffer can be pre-allocated, and shared with the
  // MergeStacks feature as needed. MergeStacks is only run while holding the
  // lock, so it is safe to have only one instance allocated for all of the
  // threads.
  JsFrameBuffer mJsFrames;

  // Cached download directory for when we need to dump profiles to disk.
#if !defined(XP_WIN)
  Maybe<nsCOMPtr<nsIFile>> mAsyncSignalDumpDirectory;
#endif
};

CorePS* CorePS::sInstance = nullptr;

void locked_profiler_add_sampled_counter(PSLockRef aLock,
                                         BaseProfilerCount* aCounter) {
  CorePS::AppendCounter(aLock, aCounter);
}

void locked_profiler_remove_sampled_counter(PSLockRef aLock,
                                            BaseProfilerCount* aCounter) {
  // Note: we don't enforce a final sample, though we could do so if the
  // profiler was active
  CorePS::RemoveCounter(aLock, aCounter);
}

class SamplerThread;

static SamplerThread* NewSamplerThread(PSLockRef aLock, uint32_t aGeneration,
                                       double aInterval, uint32_t aFeatures);

struct LiveProfiledThreadData {
  UniquePtr<ProfiledThreadData> mProfiledThreadData;
};

// This class contains the profiler's global state that is valid only when the
// profiler is active. When not instantiated, the profiler is inactive.
//
// Accesses to ActivePS are guarded by gPSMutex, in much the same fashion as
// CorePS.
//
class ActivePS {
 private:
  constexpr static uint32_t ChunkSizeForEntries(uint32_t aEntries) {
    return uint32_t(std::min(size_t(ClampToAllowedEntries(aEntries)) *
                                 scBytesPerEntry / scMinimumNumberOfChunks,
                             size_t(scMaximumChunkSize)));
  }

  static uint32_t AdjustFeatures(uint32_t aFeatures, uint32_t aFilterCount) {
    // Filter out any features unavailable in this platform/configuration.
    aFeatures &= AvailableFeatures();

    // Some features imply others.
    if (aFeatures & ProfilerFeature::FileIOAll) {
      aFeatures |= ProfilerFeature::MainThreadIO | ProfilerFeature::FileIO;
    } else if (aFeatures & ProfilerFeature::FileIO) {
      aFeatures |= ProfilerFeature::MainThreadIO;
    }

    if (aFeatures & ProfilerFeature::Tracing) {
      aFeatures &= ~ProfilerFeature::Memory;
      aFeatures |= ProfilerFeature::NoStackSampling;
      aFeatures |= ProfilerFeature::JS;
    }

    return aFeatures;
  }

  bool ShouldInterposeIOs() {
    return ProfilerFeature::HasMainThreadIO(mFeatures) ||
           ProfilerFeature::HasFileIO(mFeatures) ||
           ProfilerFeature::HasFileIOAll(mFeatures);
  }

  ActivePS(
      PSLockRef aLock, const TimeStamp& aProfilingStartTime,
      PowerOfTwo32 aCapacity, double aInterval, uint32_t aFeatures,
      const char** aFilters, uint32_t aFilterCount, uint64_t aActiveTabID,
      const Maybe<double>& aDuration,
      UniquePtr<ProfileBufferChunkManagerWithLocalLimit> aChunkManagerOrNull)
      : mProfilingStartTime(aProfilingStartTime),
        mGeneration(sNextGeneration++),
        mCapacity(aCapacity),
        mDuration(aDuration),
        mInterval(aInterval),
        mFeatures(AdjustFeatures(aFeatures, aFilterCount)),
        mActiveTabID(aActiveTabID),
        mProfileBufferChunkManager(
            aChunkManagerOrNull
                ? std::move(aChunkManagerOrNull)
                : MakeUnique<ProfileBufferChunkManagerWithLocalLimit>(
                      size_t(ClampToAllowedEntries(aCapacity.Value())) *
                          scBytesPerEntry,
                      ChunkSizeForEntries(aCapacity.Value()))),
        mProfileBuffer([this]() -> ProfileChunkedBuffer& {
          ProfileChunkedBuffer& coreBuffer = profiler_get_core_buffer();
          coreBuffer.SetChunkManagerIfDifferent(*mProfileBufferChunkManager);
          return coreBuffer;
        }()),
        mMaybeProcessCPUCounter(ProfilerFeature::HasProcessCPU(aFeatures)
                                    ? new ProcessCPUCounter(aLock)
                                    : nullptr),
        mMaybePowerCounters(nullptr),
        mMaybeCPUFreq(nullptr),
        // The new sampler thread doesn't start sampling immediately because the
        // main loop within Run() is blocked until this function's caller
        // unlocks gPSMutex.
        mSamplerThread(
            NewSamplerThread(aLock, mGeneration, aInterval, aFeatures)),
        mIsPaused(false),
        mIsSamplingPaused(false) {
    ProfilingLog::Init();

    // Deep copy and lower-case aFilters.
    MOZ_ALWAYS_TRUE(mFilters.resize(aFilterCount));
    MOZ_ALWAYS_TRUE(mFiltersLowered.resize(aFilterCount));
    for (uint32_t i = 0; i < aFilterCount; ++i) {
      mFilters[i] = aFilters[i];
      mFiltersLowered[i].reserve(mFilters[i].size());
      std::transform(mFilters[i].cbegin(), mFilters[i].cend(),
                     std::back_inserter(mFiltersLowered[i]), ::tolower);
    }

#if !defined(RELEASE_OR_BETA)
    if (ShouldInterposeIOs()) {
      // We need to register the observer on the main thread, because we want
      // to observe IO that happens on the main thread.
      // IOInterposer needs to be initialized before calling
      // IOInterposer::Register or our observer will be silently dropped.
      if (NS_IsMainThread()) {
        IOInterposer::Init();
        IOInterposer::Register(IOInterposeObserver::OpAll,
                               &ProfilerIOInterposeObserver::GetInstance());
      } else {
        NS_DispatchToMainThread(
            NS_NewRunnableFunction("ActivePS::ActivePS", []() {
              // Note: This could theoretically happen after ActivePS gets
              // destroyed, but it's ok:
              // - The Observer always checks that the profiler is (still)
              //   active before doing its work.
              // - The destruction should happen on the same thread as this
              //   construction, so the un-registration will also be dispatched
              //   and queued on the main thread, and run after this.
              IOInterposer::Init();
              IOInterposer::Register(
                  IOInterposeObserver::OpAll,
                  &ProfilerIOInterposeObserver::GetInstance());
            }));
      }
    }
#endif

    if (ProfilerFeature::HasPower(aFeatures)) {
      mMaybePowerCounters = new PowerCounters();
      for (const auto& powerCounter : mMaybePowerCounters->GetCounters()) {
        locked_profiler_add_sampled_counter(aLock, powerCounter.get());
      }
    }

    if (ProfilerFeature::HasCPUFrequency(aFeatures)) {
      mMaybeCPUFreq = new ProfilerCPUFreq();
    }
  }

  ~ActivePS() {
    MOZ_ASSERT(
        !mMaybeProcessCPUCounter,
        "mMaybeProcessCPUCounter should have been deleted before ~ActivePS()");
    MOZ_ASSERT(
        !mMaybePowerCounters,
        "mMaybePowerCounters should have been deleted before ~ActivePS()");
    MOZ_ASSERT(!mMaybeCPUFreq,
               "mMaybeCPUFreq should have been deleted before ~ActivePS()");
#if defined(MOZ_MEMORY) && defined(MOZ_PROFILER_MEMORY)
    MOZ_ASSERT(!mMemoryCounter,
               "mMemoryCounter should have been deleted before ~ActivePS()");
#endif

#if !defined(RELEASE_OR_BETA)
    if (ShouldInterposeIOs()) {
      // We need to unregister the observer on the main thread, because that's
      // where we've registered it.
      if (NS_IsMainThread()) {
        IOInterposer::Unregister(IOInterposeObserver::OpAll,
                                 &ProfilerIOInterposeObserver::GetInstance());
      } else {
        NS_DispatchToMainThread(
            NS_NewRunnableFunction("ActivePS::~ActivePS", []() {
              IOInterposer::Unregister(
                  IOInterposeObserver::OpAll,
                  &ProfilerIOInterposeObserver::GetInstance());
            }));
      }
    }
#endif

#if defined(MOZ_PROFILER_MEMORY) && defined(MOZJEMALLOC_PROFILING_CALLBACKS)
    unregister_profiler_memory_callbacks();
#endif

    if (mProfileBufferChunkManager) {
      // We still control the chunk manager, remove it from the core buffer.
      profiler_get_core_buffer().ResetChunkManager();
    }

    ProfilingLog::Destroy();
  }

  bool ThreadSelected(const char* aThreadName) {
    if (mFiltersLowered.empty()) {
      return true;
    }

    std::string name = aThreadName;
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);

    for (const auto& filter : mFiltersLowered) {
      if (filter == "*") {
        return true;
      }

      // Crude, non UTF-8 compatible, case insensitive substring search
      if (name.find(filter) != std::string::npos) {
        return true;
      }

      // If the filter is "pid:<my pid>", profile all threads.
      if (mozilla::profiler::detail::FilterHasPid(filter.c_str())) {
        return true;
      }
    }

    return false;
  }

 public:
  static void Create(
      PSLockRef aLock, const TimeStamp& aProfilingStartTime,
      PowerOfTwo32 aCapacity, double aInterval, uint32_t aFeatures,
      const char** aFilters, uint32_t aFilterCount, uint64_t aActiveTabID,
      const Maybe<double>& aDuration,
      UniquePtr<ProfileBufferChunkManagerWithLocalLimit> aChunkManagerOrNull) {
    MOZ_ASSERT(!sInstance);
    sInstance = new ActivePS(aLock, aProfilingStartTime, aCapacity, aInterval,
                             aFeatures, aFilters, aFilterCount, aActiveTabID,
                             aDuration, std::move(aChunkManagerOrNull));
  }

  [[nodiscard]] static SamplerThread* Destroy(PSLockRef aLock) {
    MOZ_ASSERT(sInstance);
    if (sInstance->mMaybeProcessCPUCounter) {
      locked_profiler_remove_sampled_counter(
          aLock, sInstance->mMaybeProcessCPUCounter);
      delete sInstance->mMaybeProcessCPUCounter;
      sInstance->mMaybeProcessCPUCounter = nullptr;
    }

    if (sInstance->mMaybePowerCounters) {
      for (const auto& powerCounter :
           sInstance->mMaybePowerCounters->GetCounters()) {
        locked_profiler_remove_sampled_counter(aLock, powerCounter.get());
      }
      delete sInstance->mMaybePowerCounters;
      sInstance->mMaybePowerCounters = nullptr;
    }

    if (sInstance->mMaybeCPUFreq) {
      delete sInstance->mMaybeCPUFreq;
      sInstance->mMaybeCPUFreq = nullptr;
    }

#if defined(MOZ_MEMORY) && defined(MOZ_PROFILER_MEMORY)
    if (sInstance->mMemoryCounter) {
      locked_profiler_remove_sampled_counter(aLock,
                                             sInstance->mMemoryCounter.get());
      sInstance->mMemoryCounter = nullptr;
    }
#endif

    ProfilerBandwidthCounter* counter = CorePS::GetBandwidthCounter();
    if (counter && counter->IsRegistered()) {
      // Because profiler_count_bandwidth_bytes does a racy
      // profiler_feature_active check to avoid taking the lock,
      // free'ing the memory of the counter would be crashy if the
      // socket thread attempts to increment the counter while we are
      // stopping the profiler.
      // Instead, we keep the counter in CorePS and only mark it as
      // unregistered so that the next attempt to count bytes
      // will re-register it.
      locked_profiler_remove_sampled_counter(aLock, counter);
      counter->MarkUnregistered();
    }

    auto samplerThread = sInstance->mSamplerThread;
    delete sInstance;
    sInstance = nullptr;

    return samplerThread;
  }

  static bool Exists(PSLockRef) { return !!sInstance; }

  static bool Equals(PSLockRef, PowerOfTwo32 aCapacity,
                     const Maybe<double>& aDuration, double aInterval,
                     uint32_t aFeatures, const char** aFilters,
                     uint32_t aFilterCount, uint64_t aActiveTabID) {
    MOZ_ASSERT(sInstance);
    if (sInstance->mCapacity != aCapacity ||
        sInstance->mDuration != aDuration ||
        sInstance->mInterval != aInterval ||
        sInstance->mFeatures != aFeatures ||
        sInstance->mFilters.length() != aFilterCount ||
        sInstance->mActiveTabID != aActiveTabID) {
      return false;
    }

    for (uint32_t i = 0; i < sInstance->mFilters.length(); ++i) {
      if (strcmp(sInstance->mFilters[i].c_str(), aFilters[i]) != 0) {
        return false;
      }
    }
    return true;
  }

  static size_t SizeOf(PSLockRef, MallocSizeOf aMallocSizeOf) {
    MOZ_ASSERT(sInstance);

    size_t n = aMallocSizeOf(sInstance);

    n += sInstance->mProfileBuffer.SizeOfExcludingThis(aMallocSizeOf);

    // Measurement of the following members may be added later if DMD finds it
    // is worthwhile:
    // - mLiveProfiledThreads (both the array itself, and the contents)
    // - mDeadProfiledThreads (both the array itself, and the contents)
    //

    return n;
  }

  static ThreadProfilingFeatures ProfilingFeaturesForThread(
      PSLockRef aLock, const ThreadRegistrationInfo& aInfo) {
    MOZ_ASSERT(sInstance);
    if (sInstance->ThreadSelected(aInfo.Name())) {
      // This thread was selected by the user, record everything.
      return ThreadProfilingFeatures::Any;
    }
    ThreadProfilingFeatures features = ThreadProfilingFeatures::NotProfiled;
    if (ActivePS::FeatureCPUAllThreads(aLock)) {
      features = Combine(features, ThreadProfilingFeatures::CPUUtilization);
    }
    if (ActivePS::FeatureSamplingAllThreads(aLock)) {
      features = Combine(features, ThreadProfilingFeatures::Sampling);
    }
    if (ActivePS::FeatureMarkersAllThreads(aLock)) {
      features = Combine(features, ThreadProfilingFeatures::Markers);
    }
    return features;
  }

  [[nodiscard]] static bool AppendPostSamplingCallback(
      PSLockRef, PostSamplingCallback&& aCallback);

  // Writes out the current active configuration of the profile.
  static void WriteActiveConfiguration(
      PSLockRef aLock, JSONWriter& aWriter,
      const Span<const char>& aPropertyName = MakeStringSpan("")) {
    if (!sInstance) {
      if (!aPropertyName.empty()) {
        aWriter.NullProperty(aPropertyName);
      } else {
        aWriter.NullElement();
      }
      return;
    };

    if (!aPropertyName.empty()) {
      aWriter.StartObjectProperty(aPropertyName);
    } else {
      aWriter.StartObjectElement();
    }

    {
      aWriter.StartArrayProperty("features");
#define WRITE_ACTIVE_FEATURES(n_, str_, Name_, desc_)    \
  if (profiler_feature_active(ProfilerFeature::Name_)) { \
    aWriter.StringElement(str_);                         \
  }

      PROFILER_FOR_EACH_FEATURE(WRITE_ACTIVE_FEATURES)
#undef WRITE_ACTIVE_FEATURES
      aWriter.EndArray();
    }
    {
      aWriter.StartArrayProperty("threads");
      for (const auto& filter : sInstance->mFilters) {
        aWriter.StringElement(filter);
      }
      aWriter.EndArray();
    }
    {
      // Now write all the simple values.

      // The interval is also available on profile.meta.interval
      aWriter.DoubleProperty("interval", sInstance->mInterval);
      aWriter.IntProperty("capacity", sInstance->mCapacity.Value());
      if (sInstance->mDuration) {
        aWriter.DoubleProperty("duration", sInstance->mDuration.value());
      }
      // Here, we are converting uint64_t to double. Tab IDs are
      // being created using `nsContentUtils::GenerateProcessSpecificId`, which
      // is specifically designed to only use 53 of the 64 bits to be lossless
      // when passed into and out of JS as a double.
      aWriter.DoubleProperty("activeTabID", sInstance->mActiveTabID);
    }
    aWriter.EndObject();
  }

  PS_GET_LOCKLESS(TimeStamp, ProfilingStartTime)

  PS_GET(uint32_t, Generation)

  PS_GET(PowerOfTwo32, Capacity)

  PS_GET(Maybe<double>, Duration)

  PS_GET(double, Interval)

  PS_GET(uint32_t, Features)

  PS_GET(uint64_t, ActiveTabID)

#define PS_GET_FEATURE(n_, str_, Name_, desc_)                \
  static bool Feature##Name_(PSLockRef) {                     \
    MOZ_ASSERT(sInstance);                                    \
    return ProfilerFeature::Has##Name_(sInstance->mFeatures); \
  }

  PROFILER_FOR_EACH_FEATURE(PS_GET_FEATURE)

#undef PS_GET_FEATURE

  static bool ShouldInstallMemoryHooks(PSLockRef) {
    MOZ_ASSERT(sInstance);
    return ProfilerFeature::ShouldInstallMemoryHooks(sInstance->mFeatures);
  }

  static uint32_t JSFlags(PSLockRef aLock) {
    uint32_t Flags = 0;
    Flags |=
        FeatureJS(aLock) ? uint32_t(JSInstrumentationFlags::StackSampling) : 0;

    Flags |= FeatureJSAllocations(aLock)
                 ? uint32_t(JSInstrumentationFlags::Allocations)
                 : 0;
    return Flags;
  }

  PS_GET(const Vector<std::string>&, Filters)
  PS_GET(const Vector<std::string>&, FiltersLowered)

  // Not using PS_GET, because only the "Controlled" interface of
  // `mProfileBufferChunkManager` should be exposed here.
  static ProfileBufferChunkManagerWithLocalLimit& ControlledChunkManager(
      PSLockRef) {
    MOZ_ASSERT(sInstance);
    MOZ_ASSERT(sInstance->mProfileBufferChunkManager);
    return *sInstance->mProfileBufferChunkManager;
  }

  static void FulfillChunkRequests(PSLockRef) {
    MOZ_ASSERT(sInstance);
    if (sInstance->mProfileBufferChunkManager) {
      sInstance->mProfileBufferChunkManager->FulfillChunkRequests();
    }
  }

  static ProfileBuffer& Buffer(PSLockRef) {
    MOZ_ASSERT(sInstance);
    return sInstance->mProfileBuffer;
  }

  static const Vector<LiveProfiledThreadData>& LiveProfiledThreads(PSLockRef) {
    MOZ_ASSERT(sInstance);
    return sInstance->mLiveProfiledThreads;
  }

  struct ProfiledThreadListElement {
    TimeStamp mRegisterTime;
    JSContext* mJSContext;  // Null for unregistered threads.
    ProfiledThreadData* mProfiledThreadData;
  };
  using ProfiledThreadList = Vector<ProfiledThreadListElement>;

  // Returns a ProfiledThreadList with all threads that should be included in a
  // profile, both for threads that are still registered, and for threads that
  // have been unregistered but still have data in the buffer.
  // The returned array is sorted by thread register time.
  // Do not hold on to the return value past LockedRegistry.
  static ProfiledThreadList ProfiledThreads(
      ThreadRegistry::LockedRegistry& aLockedRegistry, PSLockRef aLock) {
    MOZ_ASSERT(sInstance);
    ProfiledThreadList array;
    MOZ_RELEASE_ASSERT(
        array.initCapacity(sInstance->mLiveProfiledThreads.length() +
                           sInstance->mDeadProfiledThreads.length()));

    for (ThreadRegistry::OffThreadRef offThreadRef : aLockedRegistry) {
      ProfiledThreadData* profiledThreadData =
          offThreadRef.UnlockedRWForLockedProfilerRef().GetProfiledThreadData(
              aLock);
      if (!profiledThreadData) {
        // This thread was not profiled, continue with the next one.
        continue;
      }
      ThreadRegistry::OffThreadRef::RWFromAnyThreadWithLock lockedThreadData =
          offThreadRef.GetLockedRWFromAnyThread();
      MOZ_RELEASE_ASSERT(array.append(ProfiledThreadListElement{
          profiledThreadData->Info().RegisterTime(),
          lockedThreadData->GetJSContext(), profiledThreadData}));
    }

    for (auto& t : sInstance->mDeadProfiledThreads) {
      MOZ_RELEASE_ASSERT(array.append(ProfiledThreadListElement{
          t->Info().RegisterTime(), (JSContext*)nullptr, t.get()}));
    }

    std::sort(array.begin(), array.end(),
              [](const ProfiledThreadListElement& a,
                 const ProfiledThreadListElement& b) {
                return a.mRegisterTime < b.mRegisterTime;
              });
    return array;
  }

  static Vector<RefPtr<PageInformation>> ProfiledPages(PSLockRef aLock) {
    MOZ_ASSERT(sInstance);
    Vector<RefPtr<PageInformation>> array;
    for (auto& d : CorePS::RegisteredPages(aLock)) {
      MOZ_RELEASE_ASSERT(array.append(d));
    }
    for (auto& d : sInstance->mDeadProfiledPages) {
      MOZ_RELEASE_ASSERT(array.append(d));
    }
    // We don't need to sort the pages like threads since we won't show them
    // as a list.
    return array;
  }

  // Collect JS sources from the main thread, since script source storage is
  // shared between all threads.
  static nsTArray<mozilla::JSSourceEntry> GatherJSSources(PSLockRef aLock) {
    nsTArray<mozilla::JSSourceEntry> jsSourceEntries;
    bool gatherSourceText =
        ProfilerFeature::HasJSSources(ActivePS::Features(aLock));

    ThreadRegistry::LockedRegistry lockedRegistry;
    ActivePS::ProfiledThreadList threads =
        ActivePS::ProfiledThreads(lockedRegistry, aLock);

    // Get the JS context of the main thread. We don't need to get the
    // JSContext of others because the script source storage is shared between
    // threads.
    auto* mainThread =
        std::find_if(threads.begin(), threads.end(), [](const auto& thread) {
          return thread.mProfiledThreadData->Info().IsMainThread();
        });

    if (mainThread == threads.end() || !mainThread->mJSContext) {
      return jsSourceEntries;
    }
    JSContext* jsContext = mainThread->mJSContext;

    // Always gather source metadata (filename, sourceMapURL), but only gather
    // actual source text if the JS sources feature is enabled.
    js::ProfilerJSSources threadSources = js::GetProfilerScriptSources(
        JS_GetRuntime(jsContext), gatherSourceText);

    // Compute hash for each source based on filepath and source text.
    // This replaces random UUIDs with deterministic hashes, which enables us to
    // deduplicate sources with identical content.
    for (ProfilerJSSourceData& sourceData : threadSources) {
      nsCString hash = nsPrintfCString("%08x", sourceData.hash());
      jsSourceEntries.AppendElement(
          JSSourceEntry(std::move(hash), std::move(sourceData)));
    }

    return jsSourceEntries;
  }

  static ProfiledThreadData* AddLiveProfiledThread(
      PSLockRef, UniquePtr<ProfiledThreadData>&& aProfiledThreadData) {
    MOZ_ASSERT(sInstance);
    MOZ_RELEASE_ASSERT(sInstance->mLiveProfiledThreads.append(
        LiveProfiledThreadData{std::move(aProfiledThreadData)}));

    // Return a weak pointer to the ProfiledThreadData object.
    return sInstance->mLiveProfiledThreads.back().mProfiledThreadData.get();
  }

  static void UnregisterThread(PSLockRef aLockRef,
                               ProfiledThreadData* aProfiledThreadData) {
    MOZ_ASSERT(sInstance);

    DiscardExpiredDeadProfiledThreads(aLockRef);

    // Find the right entry in the mLiveProfiledThreads array and remove the
    // element, moving the ProfiledThreadData object for the thread into the
    // mDeadProfiledThreads array.
    for (size_t i = 0; i < sInstance->mLiveProfiledThreads.length(); i++) {
      LiveProfiledThreadData& thread = sInstance->mLiveProfiledThreads[i];
      if (thread.mProfiledThreadData == aProfiledThreadData) {
        thread.mProfiledThreadData->NotifyUnregistered(
            sInstance->mProfileBuffer.BufferRangeEnd());
        MOZ_RELEASE_ASSERT(sInstance->mDeadProfiledThreads.append(
            std::move(thread.mProfiledThreadData)));
        sInstance->mLiveProfiledThreads.erase(
            &sInstance->mLiveProfiledThreads[i]);
        return;
      }
    }
  }

  // This is a counter to collect process CPU utilization during profiling.
  // It cannot be a raw `ProfilerCounter` because we need to manually add/remove
  // it while the profiler lock is already held.
  class ProcessCPUCounter final : public AtomicProfilerCount {
   public:
    explicit ProcessCPUCounter(PSLockRef aLock)
        : AtomicProfilerCount("processCPU", &mCounter, nullptr, "CPU",
                              "Process CPU utilization") {
      // Adding on construction, so it's ready before the sampler starts.
      locked_profiler_add_sampled_counter(aLock, this);
      // Note: Removed from ActivePS::Destroy, because a lock is needed.
    }

    void Add(int64_t aNumber) { mCounter += aNumber; }

   private:
    ProfilerAtomicSigned mCounter;
  };
  PS_GET(ProcessCPUCounter*, MaybeProcessCPUCounter);

  PS_GET(PowerCounters*, MaybePowerCounters);

  PS_GET(ProfilerCPUFreq*, MaybeCPUFreq);

  PS_GET_AND_SET(bool, IsPaused)

  // True if sampling is paused (though generic `SetIsPaused()` or specific
  // `SetIsSamplingPaused()`).
  static bool IsSamplingPaused(PSLockRef lock) {
    MOZ_ASSERT(sInstance);
    return IsPaused(lock) || sInstance->mIsSamplingPaused;
  }

  static void SetIsSamplingPaused(PSLockRef, bool aIsSamplingPaused) {
    MOZ_ASSERT(sInstance);
    sInstance->mIsSamplingPaused = aIsSamplingPaused;
  }

  static void DiscardExpiredDeadProfiledThreads(PSLockRef) {
    MOZ_ASSERT(sInstance);
    uint64_t bufferRangeStart = sInstance->mProfileBuffer.BufferRangeStart();
    // Discard any dead threads that were unregistered before bufferRangeStart.
    sInstance->mDeadProfiledThreads.eraseIf(
        [bufferRangeStart](
            const UniquePtr<ProfiledThreadData>& aProfiledThreadData) {
          Maybe<uint64_t> bufferPosition =
              aProfiledThreadData->BufferPositionWhenUnregistered();
          MOZ_RELEASE_ASSERT(bufferPosition,
                             "should have unregistered this thread");
          return *bufferPosition < bufferRangeStart;
        });
  }

  static void UnregisterPage(PSLockRef aLock,
                             uint64_t aRegisteredInnerWindowID) {
    MOZ_ASSERT(sInstance);
    auto& registeredPages = CorePS::RegisteredPages(aLock);
    for (size_t i = 0; i < registeredPages.length(); i++) {
      RefPtr<PageInformation>& page = registeredPages[i];
      if (page->InnerWindowID() == aRegisteredInnerWindowID) {
        page->NotifyUnregistered(sInstance->mProfileBuffer.BufferRangeEnd());
        MOZ_RELEASE_ASSERT(
            sInstance->mDeadProfiledPages.append(std::move(page)));
        registeredPages.erase(&registeredPages[i--]);
      }
    }
  }

  static void DiscardExpiredPages(PSLockRef) {
    MOZ_ASSERT(sInstance);
    uint64_t bufferRangeStart = sInstance->mProfileBuffer.BufferRangeStart();
    // Discard any dead pages that were unregistered before
    // bufferRangeStart.
    sInstance->mDeadProfiledPages.eraseIf(
        [bufferRangeStart](const RefPtr<PageInformation>& aProfiledPage) {
          Maybe<uint64_t> bufferPosition =
              aProfiledPage->BufferPositionWhenUnregistered();
          MOZ_RELEASE_ASSERT(bufferPosition,
                             "should have unregistered this page");
          return *bufferPosition < bufferRangeStart;
        });
  }

  static void ClearUnregisteredPages(PSLockRef) {
    MOZ_ASSERT(sInstance);
    sInstance->mDeadProfiledPages.clear();
  }

  static void ClearExpiredExitProfiles(PSLockRef) {
    MOZ_ASSERT(sInstance);
    uint64_t bufferRangeStart = sInstance->mProfileBuffer.BufferRangeStart();
    // Discard exit profiles that were gathered before our buffer RangeStart.
    // If we have started to overwrite our data from when the Base profile was
    // added, we should get rid of that Base profile because it's now older than
    // our oldest Gecko profile data.
    //
    // When adding: (In practice the starting buffer should be empty)
    // v Start == End
    // |                 <-- Buffer range, initially empty.
    // ^ mGeckoIndexWhenBaseProfileAdded < Start FALSE -> keep it
    //
    // Later, still in range:
    // v Start   v End
    // |=========|       <-- Buffer range growing.
    // ^ mGeckoIndexWhenBaseProfileAdded < Start FALSE -> keep it
    //
    // Even later, now out of range:
    //       v Start      v End
    //       |============|       <-- Buffer range full and sliding.
    // ^ mGeckoIndexWhenBaseProfileAdded < Start TRUE! -> Discard it
    if (sInstance->mBaseProfileThreads &&
        sInstance->mGeckoIndexWhenBaseProfileAdded
                .ConvertToProfileBufferIndex() <
            profiler_get_core_buffer().GetState().mRangeStart) {
      DEBUG_LOG("ClearExpiredExitProfiles() - Discarding base profile %p",
                sInstance->mBaseProfileThreads.get());
      sInstance->mBaseProfileThreads.reset();
    }
    sInstance->mExitProfiles.eraseIf(
        [bufferRangeStart](const ExitProfile& aExitProfile) {
          return aExitProfile.mBufferPositionAtGatherTime < bufferRangeStart;
        });
  }

  static void AddBaseProfileThreads(PSLockRef aLock,
                                    UniquePtr<char[]> aBaseProfileThreads) {
    MOZ_ASSERT(sInstance);
    DEBUG_LOG("AddBaseProfileThreads(%p)", aBaseProfileThreads.get());
    sInstance->mBaseProfileThreads = std::move(aBaseProfileThreads);
    sInstance->mGeckoIndexWhenBaseProfileAdded =
        ProfileBufferBlockIndex::CreateFromProfileBufferIndex(
            profiler_get_core_buffer().GetState().mRangeEnd);
  }

  static UniquePtr<char[]> MoveBaseProfileThreads(PSLockRef aLock) {
    MOZ_ASSERT(sInstance);

    ClearExpiredExitProfiles(aLock);

    DEBUG_LOG("MoveBaseProfileThreads() - Consuming base profile %p",
              sInstance->mBaseProfileThreads.get());
    return std::move(sInstance->mBaseProfileThreads);
  }

  static void AddExitProfile(
      PSLockRef aLock,
      ProfileAndAdditionalInformation&& aExitProfileAndAdditionalInfo) {
    MOZ_ASSERT(sInstance);

    ClearExpiredExitProfiles(aLock);

    MOZ_RELEASE_ASSERT(sInstance->mExitProfiles.append(
        ExitProfile{std::move(aExitProfileAndAdditionalInfo),
                    sInstance->mProfileBuffer.BufferRangeEnd()}));
  }

  static Vector<ProfileAndAdditionalInformation> MoveExitProfiles(
      PSLockRef aLock) {
    MOZ_ASSERT(sInstance);

    ClearExpiredExitProfiles(aLock);

    Vector<ProfileAndAdditionalInformation> profiles;
    MOZ_RELEASE_ASSERT(
        profiles.initCapacity(sInstance->mExitProfiles.length()));
    for (auto& profile : sInstance->mExitProfiles) {
      MOZ_RELEASE_ASSERT(
          profiles.append(std::move(profile.mProfileAndAdditionalInformation)));
    }
    sInstance->mExitProfiles.clear();
    return profiles;
  }

#if defined(MOZ_MEMORY) && defined(MOZ_PROFILER_MEMORY)
  static void SetMemoryCounter(UniquePtr<BaseProfilerCount> aMemoryCounter,
                               PSLockRef aLock) {
    MOZ_ASSERT(sInstance);

    sInstance->mMemoryCounter = std::move(aMemoryCounter);
  }

  static bool IsMemoryCounter(const BaseProfilerCount* aMemoryCounter,
                              PSLockRef aLock) {
    MOZ_ASSERT(sInstance);

    return sInstance->mMemoryCounter.get() == aMemoryCounter;
  }
#endif

 private:
  // The singleton instance.
  static ActivePS* sInstance;

  const TimeStamp mProfilingStartTime;

  // We need to track activity generations. If we didn't we could have the
  // following scenario.
  //
  // - profiler_stop() locks gPSMutex, de-instantiates ActivePS, unlocks
  //   gPSMutex, deletes the SamplerThread (which does a join).
  //
  // - profiler_start() runs on a different thread, locks gPSMutex,
  //   re-instantiates ActivePS, unlocks gPSMutex -- all before the join
  //   completes.
  //
  // - SamplerThread::Run() locks gPSMutex, sees that ActivePS is instantiated,
  //   and continues as if the start/stop pair didn't occur. Also
  //   profiler_stop() is stuck, unable to finish.
  //
  // By checking ActivePS *and* the generation, we can avoid this scenario.
  // sNextGeneration is used to track the next generation number; it is static
  // because it must persist across different ActivePS instantiations.
  const uint32_t mGeneration;
  static uint32_t sNextGeneration;

  // The maximum number of entries in mProfileBuffer.
  const PowerOfTwo32 mCapacity;

  // The maximum duration of entries in mProfileBuffer, in seconds.
  const Maybe<double> mDuration;

  // The interval between samples, measured in milliseconds.
  const double mInterval;

  // The profile features that are enabled.
  const uint32_t mFeatures;

  // Substrings of names of threads we want to profile.
  Vector<std::string> mFilters;
  Vector<std::string> mFiltersLowered;

  // ID of the active browser screen's active tab.
  // It's being used to determine the profiled tab. It's "0" if we failed to
  // get the ID.
  const uint64_t mActiveTabID;

  // The chunk manager used by `mProfileBuffer` below.
  // May become null if it gets transferred ouf of the Gecko Profiler.
  UniquePtr<ProfileBufferChunkManagerWithLocalLimit> mProfileBufferChunkManager;

  // The buffer into which all samples are recorded.
  ProfileBuffer mProfileBuffer;

  // ProfiledThreadData objects for any threads that were profiled at any point
  // during this run of the profiler:
  //  - mLiveProfiledThreads contains all threads that are still registered, and
  //  - mDeadProfiledThreads contains all threads that have already been
  //    unregistered but for which there is still data in the profile buffer.
  Vector<LiveProfiledThreadData> mLiveProfiledThreads;
  Vector<UniquePtr<ProfiledThreadData>> mDeadProfiledThreads;

  // Info on all the dead pages.
  // Registered pages are being moved to this array after unregistration.
  // We are keeping them in case we need them in the profile data.
  // We are removing them when we ensure that we won't need them anymore.
  Vector<RefPtr<PageInformation>> mDeadProfiledPages;

  // Used to collect process CPU utilization values, if the feature is on.
  ProcessCPUCounter* mMaybeProcessCPUCounter;

  // Used to collect power use data, if the power feature is on.
  PowerCounters* mMaybePowerCounters;

  // Used to collect cpu frequency, if the CPU frequency feature is on.
  ProfilerCPUFreq* mMaybeCPUFreq;

  // The current sampler thread. This class is not responsible for destroying
  // the SamplerThread object; the Destroy() method returns it so the caller
  // can destroy it.
  SamplerThread* const mSamplerThread;

  // Is the profiler fully paused?
  bool mIsPaused;

  // Is the profiler periodic sampling paused?
  bool mIsSamplingPaused;

  // Optional startup profile thread array from BaseProfiler.
  UniquePtr<char[]> mBaseProfileThreads;
  ProfileBufferBlockIndex mGeckoIndexWhenBaseProfileAdded;

  struct ExitProfile {
    ProfileAndAdditionalInformation mProfileAndAdditionalInformation;
    uint64_t mBufferPositionAtGatherTime;
  };
  Vector<ExitProfile> mExitProfiles;

#if defined(MOZ_MEMORY) && defined(MOZ_PROFILER_MEMORY)
  UniquePtr<BaseProfilerCount> mMemoryCounter;
#endif
};

ActivePS* ActivePS::sInstance = nullptr;
uint32_t ActivePS::sNextGeneration = 0;

#undef PS_GET
#undef PS_GET_LOCKLESS
#undef PS_GET_AND_SET

using ProfilerStateChangeMutex =
    mozilla::baseprofiler::detail::BaseProfilerMutex;
using ProfilerStateChangeLock =
    mozilla::baseprofiler::detail::BaseProfilerAutoLock;
MOZ_RUNINIT static ProfilerStateChangeMutex gProfilerStateChangeMutex;

struct IdentifiedProfilingStateChangeCallback {
  ProfilingStateSet mProfilingStateSet;
  ProfilingStateChangeCallback mProfilingStateChangeCallback;
  uintptr_t mUniqueIdentifier;

  explicit IdentifiedProfilingStateChangeCallback(
      ProfilingStateSet aProfilingStateSet,
      ProfilingStateChangeCallback&& aProfilingStateChangeCallback,
      uintptr_t aUniqueIdentifier)
      : mProfilingStateSet(aProfilingStateSet),
        mProfilingStateChangeCallback(aProfilingStateChangeCallback),
        mUniqueIdentifier(aUniqueIdentifier) {}
};
using IdentifiedProfilingStateChangeCallbackUPtr =
    UniquePtr<IdentifiedProfilingStateChangeCallback>;

MOZ_RUNINIT static Vector<IdentifiedProfilingStateChangeCallbackUPtr>
    mIdentifiedProfilingStateChangeCallbacks;

void profiler_add_state_change_callback(
    ProfilingStateSet aProfilingStateSet,
    ProfilingStateChangeCallback&& aCallback,
    uintptr_t aUniqueIdentifier /* = 0 */) {
  MOZ_ASSERT(!PSAutoLock::IsLockedOnCurrentThread());
  ProfilerStateChangeLock lock(gProfilerStateChangeMutex);

#ifdef DEBUG
  // Check if a non-zero id is not already used. Bug forgive it in non-DEBUG
  // builds; in the worst case they may get removed too early.
  if (aUniqueIdentifier != 0) {
    for (const IdentifiedProfilingStateChangeCallbackUPtr& idedCallback :
         mIdentifiedProfilingStateChangeCallbacks) {
      MOZ_ASSERT(idedCallback->mUniqueIdentifier != aUniqueIdentifier);
    }
  }
#endif  // DEBUG

  if (aProfilingStateSet.contains(ProfilingState::AlreadyActive) &&
      profiler_is_active()) {
    aCallback(ProfilingState::AlreadyActive);
  }

  (void)mIdentifiedProfilingStateChangeCallbacks.append(
      MakeUnique<IdentifiedProfilingStateChangeCallback>(
          aProfilingStateSet, std::move(aCallback), aUniqueIdentifier));
}

// Remove the callback with the given identifier.
void profiler_remove_state_change_callback(uintptr_t aUniqueIdentifier) {
  MOZ_ASSERT(aUniqueIdentifier != 0);
  if (aUniqueIdentifier == 0) {
    // Forgive zero in non-DEBUG builds.
    return;
  }

  MOZ_ASSERT(!PSAutoLock::IsLockedOnCurrentThread());
  ProfilerStateChangeLock lock(gProfilerStateChangeMutex);

  mIdentifiedProfilingStateChangeCallbacks.eraseIf(
      [aUniqueIdentifier](
          const IdentifiedProfilingStateChangeCallbackUPtr& aIdedCallback) {
        if (aIdedCallback->mUniqueIdentifier != aUniqueIdentifier) {
          return false;
        }
        if (aIdedCallback->mProfilingStateSet.contains(
                ProfilingState::RemovingCallback)) {
          aIdedCallback->mProfilingStateChangeCallback(
              ProfilingState::RemovingCallback);
        }
        return true;
      });
}

static void invoke_profiler_state_change_callbacks(
    ProfilingState aProfilingState) {
  MOZ_ASSERT(!PSAutoLock::IsLockedOnCurrentThread());
  ProfilerStateChangeLock lock(gProfilerStateChangeMutex);

  for (const IdentifiedProfilingStateChangeCallbackUPtr& idedCallback :
       mIdentifiedProfilingStateChangeCallbacks) {
    if (idedCallback->mProfilingStateSet.contains(aProfilingState)) {
      idedCallback->mProfilingStateChangeCallback(aProfilingState);
    }
  }
}

Atomic<uint32_t, MemoryOrdering::Relaxed> RacyFeatures::sActiveAndFeatures(0);

// The name of the main thread.
static const char* const kMainThreadName = "GeckoMain";

////////////////////////////////////////////////////////////////////////
// BEGIN sampling/unwinding code

// Additional registers that have to be saved when thread is paused.
#if defined(GP_PLAT_x86_linux) || defined(GP_PLAT_x86_android) || \
    defined(GP_ARCH_x86)
#  define UNWINDING_REGS_HAVE_ECX_EDX
#elif defined(GP_PLAT_amd64_linux) || defined(GP_PLAT_amd64_android) || \
    defined(GP_PLAT_amd64_freebsd) || defined(GP_ARCH_amd64) ||         \
    defined(__x86_64__)
#  define UNWINDING_REGS_HAVE_R10_R12
#elif defined(GP_PLAT_arm_linux) || defined(GP_PLAT_arm_android)
#  define UNWINDING_REGS_HAVE_LR_R7
#elif defined(GP_PLAT_arm64_linux) || defined(GP_PLAT_arm64_android) || \
    defined(GP_PLAT_arm64_freebsd) || defined(GP_ARCH_arm64) ||         \
    defined(__aarch64__)
#  define UNWINDING_REGS_HAVE_LR_R11
#endif

// The registers used for stack unwinding and a few other sampling purposes.
// The ctor does nothing; users are responsible for filling in the fields.
class Registers {
 public:
  Registers() = default;

  void Clear() { memset(this, 0, sizeof(*this)); }

  // These fields are filled in by
  // Sampler::SuspendAndSampleAndResumeThread() for periodic and backtrace
  // samples, and by REGISTERS_SYNC_POPULATE for synchronous samples.
  Address mPC{nullptr};  // Instruction pointer.
  Address mSP{nullptr};  // Stack pointer.
  Address mFP{nullptr};  // Frame pointer.
#if defined(UNWINDING_REGS_HAVE_ECX_EDX)
  Address mEcx{nullptr};  // Temp for return address.
  Address mEdx{nullptr};  // Temp for frame pointer.
#elif defined(UNWINDING_REGS_HAVE_R10_R12)
  Address mR10{nullptr};  // Temp for return address.
  Address mR12{nullptr};  // Temp for frame pointer.
#elif defined(UNWINDING_REGS_HAVE_LR_R7)
  Address mLR{nullptr};  // ARM link register, or temp for return address.
  Address mR7{nullptr};  // Temp for frame pointer.
#elif defined(UNWINDING_REGS_HAVE_LR_R11)
  Address mLR{nullptr};   // ARM link register, or temp for return address.
  Address mR11{nullptr};  // Temp for frame pointer.
#endif

#if defined(GP_OS_linux) || defined(GP_OS_android) || defined(GP_OS_freebsd)
  // This contains all the registers, which means it duplicates the four fields
  // above. This is ok.

  // The context from the signal handler or below.
  ucontext_t* mContext{nullptr};
  // Storage for sync stack unwinding.
  ucontext_t mContextSyncStorage;
#endif
};

Atomic<bool> WALKING_JS_STACK(false);

struct AutoWalkJSStack {
  bool walkAllowed;

  AutoWalkJSStack() : walkAllowed(false) {
    walkAllowed = WALKING_JS_STACK.compareExchange(false, true);
  }

  ~AutoWalkJSStack() {
    if (walkAllowed) {
      WALKING_JS_STACK = false;
    }
  }
};

class StackWalkControl {
 public:
  struct ResumePoint {
    // If lost, the stack walker should resume at these values.
    void* resumeSp;  // If null, stop the walker here, don't resume again.
    void* resumeBp;
    void* resumePc;
  };

#if ((defined(USE_MOZ_STACK_WALK) || defined(USE_FRAME_POINTER_STACK_WALK)) && \
     defined(GP_ARCH_amd64))
 public:
  static constexpr bool scIsSupported = true;

  void Clear() { mResumePointCount = 0; }

  size_t ResumePointCount() const { return mResumePointCount; }

  static constexpr size_t MaxResumePointCount() {
    return scMaxResumePointCount;
  }

  // Add a resume point. Note that adding anything past MaxResumePointCount()
  // would silently fail. In practice this means that stack walking may still
  // lose native frames.
  void AddResumePoint(ResumePoint&& aResumePoint) {
    // If SP is null, we expect BP and PC to also be null.
    MOZ_ASSERT_IF(!aResumePoint.resumeSp, !aResumePoint.resumeBp);
    MOZ_ASSERT_IF(!aResumePoint.resumeSp, !aResumePoint.resumePc);

    // If BP and/or PC are not null, SP must not be null. (But we allow BP/PC to
    // be null even if SP is not null.)
    MOZ_ASSERT_IF(aResumePoint.resumeBp, aResumePoint.resumeSp);
    MOZ_ASSERT_IF(aResumePoint.resumePc, aResumePoint.resumeSp);

    if (mResumePointCount < scMaxResumePointCount) {
      mResumePoint[mResumePointCount] = std::move(aResumePoint);
      ++mResumePointCount;
    }
  }

  // Only allow non-modifying range-for loops.
  const ResumePoint* begin() const { return &mResumePoint[0]; }
  const ResumePoint* end() const { return &mResumePoint[mResumePointCount]; }

  // Find the next resume point that would be a caller of the function with the
  // given SP; i.e., the resume point with the closest resumeSp > aSp.
  const ResumePoint* GetResumePointCallingSp(void* aSp) const {
    const ResumePoint* callingResumePoint = nullptr;
    for (const ResumePoint& resumePoint : *this) {
      if (resumePoint.resumeSp &&        // This is a potential resume point.
          resumePoint.resumeSp > aSp &&  // It is a caller of the given SP.
          (!callingResumePoint ||        // This is the first candidate.
           resumePoint.resumeSp < callingResumePoint->resumeSp)  // Or better.
      ) {
        callingResumePoint = &resumePoint;
      }
    }
    return callingResumePoint;
  }

 private:
  size_t mResumePointCount = 0;
  static constexpr size_t scMaxResumePointCount = 32;
  ResumePoint mResumePoint[scMaxResumePointCount];

#else
 public:
  static constexpr bool scIsSupported = false;
  // Discarded constexpr-if statements are still checked during compilation,
  // these declarations are necessary for that, even if not actually used.
  void Clear();
  size_t ResumePointCount();
  static constexpr size_t MaxResumePointCount();
  void AddResumePoint(ResumePoint&& aResumePoint);
  const ResumePoint* begin() const;
  const ResumePoint* end() const;
  const ResumePoint* GetResumePointCallingSp(void* aSp) const;
#endif
};

// Make a copy of the JS stack into a JSFrame array, and return the number of
// copied frames.
// This copy is necessary since, like the native stack, the JS stack is iterated
// youngest-to-oldest and we need to iterate oldest-to-youngest in MergeStacks.
static uint32_t ExtractJsFrames(
    bool aIsSynchronous,
    const ThreadRegistration::UnlockedReaderAndAtomicRWOnThread& aThreadData,
    const Registers& aRegs, ProfilerStackCollector& aCollector,
    JsFrameBuffer aJsFrames, StackWalkControl* aStackWalkControlIfSupported) {
  MOZ_ASSERT(aJsFrames,
             "ExtractJsFrames should only be called if there is a "
             "JsFrameBuffer to fill.");

  uint32_t jsFramesCount = 0;

  // Only walk jit stack if profiling frame iterator is turned on.
  JSContext* context = aThreadData.GetJSContext();
  if (context && JS::IsProfilingEnabledForContext(context)) {
    AutoWalkJSStack autoWalkJSStack;

    if (autoWalkJSStack.walkAllowed) {
      JS::ProfilingFrameIterator::RegisterState registerState;
      registerState.pc = aRegs.mPC;
      registerState.sp = aRegs.mSP;
      registerState.fp = aRegs.mFP;
#if defined(UNWINDING_REGS_HAVE_ECX_EDX)
      registerState.tempRA = aRegs.mEcx;
      registerState.tempFP = aRegs.mEdx;
#elif defined(UNWINDING_REGS_HAVE_R10_R12)
      registerState.tempRA = aRegs.mR10;
      registerState.tempFP = aRegs.mR12;
#elif defined(UNWINDING_REGS_HAVE_LR_R7)
      registerState.lr = aRegs.mLR;
      registerState.tempFP = aRegs.mR7;
#elif defined(UNWINDING_REGS_HAVE_LR_R11)
      registerState.lr = aRegs.mLR;
      registerState.tempFP = aRegs.mR11;
#endif

      // Non-periodic sampling passes Nothing() as the buffer write position to
      // ProfilingFrameIterator to avoid incorrectly resetting the buffer
      // position of sampled JIT frames inside the JS engine.
      Maybe<uint64_t> samplePosInBuffer;
      if (!aIsSynchronous) {
        // aCollector.SamplePositionInBuffer() will return Nothing() when
        // profiler_suspend_and_sample_thread is called from the background hang
        // reporter.
        samplePosInBuffer = aCollector.SamplePositionInBuffer();
      }

      for (JS::ProfilingFrameIterator jsIter(context, registerState,
                                             samplePosInBuffer);
           !jsIter.done(); ++jsIter) {
        if (aIsSynchronous || jsIter.isWasm()) {
          jsFramesCount +=
              jsIter.extractStack(aJsFrames, jsFramesCount, MAX_JS_FRAMES);
          if (jsFramesCount == MAX_JS_FRAMES) {
            break;
          }
        } else {
          Maybe<JS::ProfilingFrameIterator::Frame> frame =
              jsIter.getPhysicalFrameWithoutLabel();
          if (frame.isSome()) {
            aJsFrames[jsFramesCount++] = std::move(frame).ref();
            if (jsFramesCount == MAX_JS_FRAMES) {
              break;
            }
          }
        }

        if constexpr (StackWalkControl::scIsSupported) {
          if (aStackWalkControlIfSupported) {
            jsIter.getCppEntryRegisters().apply(
                [&](const JS::ProfilingFrameIterator::RegisterState&
                        aCppEntry) {
                  StackWalkControl::ResumePoint resumePoint;
                  resumePoint.resumeSp = aCppEntry.sp;
                  resumePoint.resumeBp = aCppEntry.fp;
                  resumePoint.resumePc = aCppEntry.pc;
                  aStackWalkControlIfSupported->AddResumePoint(
                      std::move(resumePoint));
                });
          }
        } else {
          MOZ_ASSERT(!aStackWalkControlIfSupported,
                     "aStackWalkControlIfSupported should be null when "
                     "!StackWalkControl::scIsSupported");
          (void)aStackWalkControlIfSupported;
        }
      }
    }
  }

  return jsFramesCount;
}

// Merges the profiling stack, native stack, and JS stack, outputting the
// details to aCollector.
static void MergeStacks(
    bool aIsSynchronous,
    const ThreadRegistration::UnlockedReaderAndAtomicRWOnThread& aThreadData,
    const NativeStack& aNativeStack, ProfilerStackCollector& aCollector,
    JsFrame* aJsFrames, uint32_t aJsFramesCount) {
  // WARNING: this function runs within the profiler's "critical section".
  // WARNING: this function might be called while the profiler is inactive, and
  //          cannot rely on ActivePS.

  MOZ_ASSERT_IF(!aJsFrames, aJsFramesCount == 0);

  const ProfilingStack& profilingStack = aThreadData.ProfilingStackCRef();
  const js::ProfilingStackFrame* profilingStackFrames = profilingStack.frames;
  uint32_t profilingStackFrameCount = profilingStack.stackSize();

  // While the profiling stack array is ordered oldest-to-youngest, the JS and
  // native arrays are ordered youngest-to-oldest. We must add frames to aInfo
  // oldest-to-youngest. Thus, iterate over the profiling stack forwards and JS
  // and native arrays backwards. Note: this means the terminating condition
  // jsIndex and nativeIndex is being < 0.
  uint32_t profilingStackIndex = 0;
  int32_t jsIndex = aJsFramesCount - 1;
  int32_t nativeIndex = aNativeStack.mCount - 1;

  uint8_t* lastLabelFrameStackAddr = nullptr;
  uint8_t* jitEndStackAddr = nullptr;

  // Iterate as long as there is at least one frame remaining.
  while (profilingStackIndex != profilingStackFrameCount || jsIndex >= 0 ||
         nativeIndex >= 0) {
    // There are 1 to 3 frames available. Find and add the oldest.
    uint8_t* profilingStackAddr = nullptr;
    uint8_t* jsStackAddr = nullptr;
    uint8_t* nativeStackAddr = nullptr;
    uint8_t* jsActivationAddr = nullptr;

    if (profilingStackIndex != profilingStackFrameCount) {
      const js::ProfilingStackFrame& profilingStackFrame =
          profilingStackFrames[profilingStackIndex];

      if (profilingStackFrame.isLabelFrame() ||
          profilingStackFrame.isSpMarkerFrame()) {
        lastLabelFrameStackAddr = (uint8_t*)profilingStackFrame.stackAddress();
      }

      // Skip any JS_OSR frames. Such frames are used when the JS interpreter
      // enters a jit frame on a loop edge (via on-stack-replacement, or OSR).
      // To avoid both the profiling stack frame and jit frame being recorded
      // (and showing up twice), the interpreter marks the interpreter
      // profiling stack frame as JS_OSR to ensure that it doesn't get counted.
      if (profilingStackFrame.isOSRFrame()) {
        profilingStackIndex++;
        continue;
      }

      MOZ_ASSERT(lastLabelFrameStackAddr);
      profilingStackAddr = lastLabelFrameStackAddr;
    }

    if (jsIndex >= 0) {
      jsStackAddr = (uint8_t*)aJsFrames[jsIndex].stackAddress;
      jsActivationAddr = (uint8_t*)aJsFrames[jsIndex].activation;
    }

    if (nativeIndex >= 0) {
      nativeStackAddr = (uint8_t*)aNativeStack.mSPs[nativeIndex];
    }

    // If there's a native stack frame which has the same SP as a profiling
    // stack frame, pretend we didn't see the native stack frame.  Ditto for a
    // native stack frame which has the same SP as a JS stack frame.  In effect
    // this means profiling stack frames or JS frames trump conflicting native
    // frames.
    if (nativeStackAddr && (profilingStackAddr == nativeStackAddr ||
                            jsStackAddr == nativeStackAddr)) {
      nativeStackAddr = nullptr;
      nativeIndex--;
      MOZ_ASSERT(profilingStackAddr || jsStackAddr);
    }

    // Sanity checks.
    MOZ_ASSERT_IF(profilingStackAddr,
                  profilingStackAddr != jsStackAddr &&
                      profilingStackAddr != nativeStackAddr);
    MOZ_ASSERT_IF(jsStackAddr, jsStackAddr != profilingStackAddr &&
                                   jsStackAddr != nativeStackAddr);
    MOZ_ASSERT_IF(nativeStackAddr, nativeStackAddr != profilingStackAddr &&
                                       nativeStackAddr != jsStackAddr);

    // Check to see if profiling stack frame is top-most.
    if (profilingStackAddr > jsStackAddr &&
        profilingStackAddr > nativeStackAddr) {
      MOZ_ASSERT(profilingStackIndex < profilingStackFrameCount);
      const js::ProfilingStackFrame& profilingStackFrame =
          profilingStackFrames[profilingStackIndex];

      // Sp marker frames are just annotations and should not be recorded in
      // the profile.
      if (!profilingStackFrame.isSpMarkerFrame()) {
        // The JIT only allows the top-most frame to have a nullptr pc.
        MOZ_ASSERT_IF(
            profilingStackFrame.isJsFrame() && profilingStackFrame.script() &&
                !profilingStackFrame.pc(),
            &profilingStackFrame ==
                &profilingStack.frames[profilingStack.stackSize() - 1]);
        if (aIsSynchronous && profilingStackFrame.categoryPair() ==
                                  JS::ProfilingCategoryPair::PROFILER) {
          // For stacks captured synchronously (ie. marker stacks), stop
          // walking the stack as soon as we enter the profiler category,
          // to avoid showing profiler internal code in marker stacks.
          return;
        }
        aCollector.CollectProfilingStackFrame(profilingStackFrame);
      }
      profilingStackIndex++;
      continue;
    }

    // Check to see if JS jit stack frame is top-most
    if (jsStackAddr > nativeStackAddr) {
      MOZ_ASSERT(jsIndex >= 0);
      const JS::ProfilingFrameIterator::Frame& jsFrame = aJsFrames[jsIndex];
      jitEndStackAddr = (uint8_t*)jsFrame.endStackAddress;
      // Stringifying non-wasm JIT frames is delayed until streaming time. To
      // re-lookup the entry in the JitcodeGlobalTable, we need to store the
      // JIT code address (OptInfoAddr) in the circular buffer.
      //
      // Note that we cannot do this when we are sychronously sampling the
      // current thread; that is, when called from profiler_get_backtrace. The
      // captured backtrace is usually externally stored for an indeterminate
      // amount of time, such as in nsRefreshDriver. Problematically, the
      // stored backtrace may be alive across a GC during which the profiler
      // itself is disabled. In that case, the JS engine is free to discard its
      // JIT code. This means that if we inserted such OptInfoAddr entries into
      // the buffer, nsRefreshDriver would now be holding on to a backtrace
      // with stale JIT code return addresses.
      if (aIsSynchronous ||
          jsFrame.kind == JS::ProfilingFrameIterator::Frame_WasmIon ||
          jsFrame.kind == JS::ProfilingFrameIterator::Frame_WasmBaseline ||
          jsFrame.kind == JS::ProfilingFrameIterator::Frame_WasmOther) {
        aCollector.CollectWasmOrSyncJITFrame(jsFrame.profilingCategory(),
                                             jsFrame.label, jsFrame.sourceId);
      } else if (jsFrame.kind ==
                 JS::ProfilingFrameIterator::Frame_BaselineInterpreter) {
        // Materialize a ProfilingStackFrame similar to the C++ Interpreter. We
        // also set the IS_BLINTERP_FRAME flag to differentiate though.
        JSScript* script = jsFrame.interpreterScript;
        jsbytecode* pc = jsFrame.interpreterPC();
        js::ProfilingStackFrame stackFrame;
        constexpr uint32_t ExtraFlags =
            uint32_t(js::ProfilingStackFrame::Flags::IS_BLINTERP_FRAME);
        stackFrame.initJsFrame<JS::ProfilingCategoryPair::JS_BaselineInterpret,
                               ExtraFlags>("", jsFrame.label, script, pc,
                                           jsFrame.realmID, jsFrame.sourceId);
        aCollector.CollectProfilingStackFrame(stackFrame);
      } else {
        MOZ_ASSERT(jsFrame.kind == JS::ProfilingFrameIterator::Frame_Ion ||
                   jsFrame.kind == JS::ProfilingFrameIterator::Frame_Baseline);
        aCollector.CollectJitReturnAddr(jsFrame.returnAddress());
      }

      jsIndex--;
      continue;
    }

    // If we reach here, there must be a native stack frame and it must be the
    // greatest frame.
    if (nativeStackAddr &&
        // If the latest JS frame was JIT, this could be the native frame that
        // corresponds to it. In that case, skip the native frame, because
        // there's no need for the same frame to be present twice in the stack.
        // The JS frame can be considered the symbolicated version of the native
        // frame.
        (!jitEndStackAddr || nativeStackAddr < jitEndStackAddr) &&
        // This might still be a JIT operation, check to make sure that is not
        // in range of the NEXT JavaScript's stacks' activation address.
        (!jsActivationAddr || nativeStackAddr > jsActivationAddr)) {
      MOZ_ASSERT(nativeIndex >= 0);
      void* addr = (void*)aNativeStack.mPCs[nativeIndex];
      aCollector.CollectNativeLeafAddr(addr);
    }
    if (nativeIndex >= 0) {
      nativeIndex--;
    }
  }

  // Update the JS context with the current profile sample buffer generation.
  //
  // Only do this for periodic samples. We don't want to do this for
  // synchronous samples, and we also don't want to do it for calls to
  // profiler_suspend_and_sample_thread() from the background hang reporter -
  // in that case, aCollector.BufferRangeStart() will return Nothing().
  if (!aIsSynchronous) {
    aCollector.BufferRangeStart().apply(
        [&aThreadData](uint64_t aBufferRangeStart) {
          JSContext* context = aThreadData.GetJSContext();
          if (context) {
            JS::SetJSContextProfilerSampleBufferRangeStart(context,
                                                           aBufferRangeStart);
          }
        });
  }
}

#if defined(USE_FRAME_POINTER_STACK_WALK) || defined(USE_MOZ_STACK_WALK)
static void StackWalkCallback(uint32_t aFrameNumber, void* aPC, void* aSP,
                              void* aClosure) {
  NativeStack* nativeStack = static_cast<NativeStack*>(aClosure);
  MOZ_ASSERT(nativeStack->mCount < MAX_NATIVE_FRAMES);
  nativeStack->mSPs[nativeStack->mCount] = aSP;
  nativeStack->mPCs[nativeStack->mCount] = aPC;
  nativeStack->mCount++;
}
#endif

#if defined(USE_FRAME_POINTER_STACK_WALK)
static void DoFramePointerBacktrace(
    const ThreadRegistration::UnlockedReaderAndAtomicRWOnThread& aThreadData,
    const Registers& aRegs, NativeStack& aNativeStack,
    StackWalkControl* aStackWalkControlIfSupported) {
  // WARNING: this function runs within the profiler's "critical section".
  // WARNING: this function might be called while the profiler is inactive, and
  //          cannot rely on ActivePS.

  // Make a local copy of the Registers, to allow modifications.
  Registers regs = aRegs;

  // Start with the current function. We use 0 as the frame number here because
  // the FramePointerStackWalk() call below will use 1..N. This is a bit weird
  // but it doesn't matter because StackWalkCallback() doesn't use the frame
  // number argument.
  StackWalkCallback(/* frameNum */ 0, regs.mPC, regs.mSP, &aNativeStack);

  const void* const stackEnd = aThreadData.StackTop();

  // This is to check forward-progress after using a resume point.
  void* previousResumeSp = nullptr;

  for (;;) {
    if (!(regs.mSP && regs.mSP <= regs.mFP && regs.mFP <= stackEnd)) {
      break;
    }
    FramePointerStackWalk(StackWalkCallback,
                          uint32_t(MAX_NATIVE_FRAMES - aNativeStack.mCount),
                          &aNativeStack, reinterpret_cast<void**>(regs.mFP),
                          const_cast<void*>(stackEnd));

    if constexpr (!StackWalkControl::scIsSupported) {
      break;
    } else {
      if (aNativeStack.mCount >= MAX_NATIVE_FRAMES) {
        // No room to add more frames.
        break;
      }
      if (!aStackWalkControlIfSupported ||
          aStackWalkControlIfSupported->ResumePointCount() == 0) {
        // No resume information.
        break;
      }
      void* lastSP = aNativeStack.mSPs[aNativeStack.mCount - 1];
      if (previousResumeSp &&
          ((uintptr_t)lastSP <= (uintptr_t)previousResumeSp)) {
        // No progress after the previous resume point.
        break;
      }
      const StackWalkControl::ResumePoint* resumePoint =
          aStackWalkControlIfSupported->GetResumePointCallingSp(lastSP);
      if (!resumePoint) {
        break;
      }
      void* sp = resumePoint->resumeSp;
      if (!sp) {
        // Null SP in a resume point means we stop here.
        break;
      }
      void* pc = resumePoint->resumePc;
      StackWalkCallback(/* frameNum */ aNativeStack.mCount, pc, sp,
                        &aNativeStack);
      ++aNativeStack.mCount;
      if (aNativeStack.mCount >= MAX_NATIVE_FRAMES) {
        break;
      }
      // Prepare context to resume stack walking.
      regs.mPC = (Address)pc;
      regs.mSP = (Address)sp;
      regs.mFP = (Address)resumePoint->resumeBp;

      previousResumeSp = sp;
    }
  }
}
#endif

#if defined(USE_MOZ_STACK_WALK)
static void DoMozStackWalkBacktrace(
    const ThreadRegistration::UnlockedReaderAndAtomicRWOnThread& aThreadData,
    const Registers& aRegs, NativeStack& aNativeStack,
    StackWalkControl* aStackWalkControlIfSupported) {
  // WARNING: this function runs within the profiler's "critical section".
  // WARNING: this function might be called while the profiler is inactive, and
  //          cannot rely on ActivePS.

  // Start with the current function. We use 0 as the frame number here because
  // the MozStackWalkThread() call below will use 1..N. This is a bit weird but
  // it doesn't matter because StackWalkCallback() doesn't use the frame number
  // argument.
  StackWalkCallback(/* frameNum */ 0, aRegs.mPC, aRegs.mSP, &aNativeStack);

  HANDLE thread = aThreadData.PlatformDataCRef().ProfiledThread();
  MOZ_ASSERT(thread);

  CONTEXT context_buf;
  CONTEXT* context = nullptr;
  if constexpr (StackWalkControl::scIsSupported) {
    context = &context_buf;
    memset(&context_buf, 0, sizeof(CONTEXT));
    context_buf.ContextFlags = CONTEXT_FULL;
#  if defined(_M_AMD64)
    context_buf.Rsp = (DWORD64)aRegs.mSP;
    context_buf.Rbp = (DWORD64)aRegs.mFP;
    context_buf.Rip = (DWORD64)aRegs.mPC;
#  else
    static_assert(!StackWalkControl::scIsSupported,
                  "Mismatched support between StackWalkControl and "
                  "DoMozStackWalkBacktrace");
#  endif
  } else {
    context = nullptr;
  }

  // This is to check forward-progress after using a resume point.
  void* previousResumeSp = nullptr;

  for (;;) {
    MozStackWalkThread(StackWalkCallback,
                       uint32_t(MAX_NATIVE_FRAMES - aNativeStack.mCount),
                       &aNativeStack, thread, context);

    if constexpr (!StackWalkControl::scIsSupported) {
      break;
    } else {
      if (aNativeStack.mCount >= MAX_NATIVE_FRAMES) {
        // No room to add more frames.
        break;
      }
      if (!aStackWalkControlIfSupported ||
          aStackWalkControlIfSupported->ResumePointCount() == 0) {
        // No resume information.
        break;
      }
      void* lastSP = aNativeStack.mSPs[aNativeStack.mCount - 1];
      if (previousResumeSp &&
          ((uintptr_t)lastSP <= (uintptr_t)previousResumeSp)) {
        // No progress after the previous resume point.
        break;
      }
      const StackWalkControl::ResumePoint* resumePoint =
          aStackWalkControlIfSupported->GetResumePointCallingSp(lastSP);
      if (!resumePoint) {
        break;
      }
      void* sp = resumePoint->resumeSp;
      if (!sp) {
        // Null SP in a resume point means we stop here.
        break;
      }
      void* pc = resumePoint->resumePc;
      StackWalkCallback(/* frameNum */ aNativeStack.mCount, pc, sp,
                        &aNativeStack);
      ++aNativeStack.mCount;
      if (aNativeStack.mCount >= MAX_NATIVE_FRAMES) {
        break;
      }
      // Prepare context to resume stack walking.
      memset(&context_buf, 0, sizeof(CONTEXT));
      context_buf.ContextFlags = CONTEXT_FULL;
#  if defined(_M_AMD64)
      context_buf.Rsp = (DWORD64)sp;
      context_buf.Rbp = (DWORD64)resumePoint->resumeBp;
      context_buf.Rip = (DWORD64)pc;
#  else
      static_assert(!StackWalkControl::scIsSupported,
                    "Mismatched support between StackWalkControl and "
                    "DoMozStackWalkBacktrace");
#  endif
      previousResumeSp = sp;
    }
  }
}
#endif

#ifdef USE_EHABI_STACKWALK
static void DoEHABIBacktrace(
    const ThreadRegistration::UnlockedReaderAndAtomicRWOnThread& aThreadData,
    const Registers& aRegs, NativeStack& aNativeStack,
    StackWalkControl* aStackWalkControlIfSupported) {
  // WARNING: this function runs within the profiler's "critical section".
  // WARNING: this function might be called while the profiler is inactive, and
  //          cannot rely on ActivePS.

  aNativeStack.mCount = EHABIStackWalk(
      aRegs.mContext->uc_mcontext, const_cast<void*>(aThreadData.StackTop()),
      aNativeStack.mSPs, aNativeStack.mPCs, MAX_NATIVE_FRAMES);
  (void)aStackWalkControlIfSupported;  // TODO: Implement.
}
#endif

#ifdef USE_LUL_STACKWALK

// See the comment at the callsite for why this function is necessary.
#  if defined(MOZ_HAVE_ASAN_IGNORE)
MOZ_ASAN_IGNORE static void ASAN_memcpy(void* aDst, const void* aSrc,
                                        size_t aLen) {
  // The obvious thing to do here is call memcpy(). However, although
  // ASAN_memcpy() is not instrumented by ASAN, memcpy() still is, and the
  // false positive still manifests! So we must implement memcpy() ourselves
  // within this function.
  char* dst = static_cast<char*>(aDst);
  const char* src = static_cast<const char*>(aSrc);

  for (size_t i = 0; i < aLen; i++) {
    dst[i] = src[i];
  }
}
#  endif

static void DoLULBacktrace(
    const ThreadRegistration::UnlockedReaderAndAtomicRWOnThread& aThreadData,
    const Registers& aRegs, NativeStack& aNativeStack,
    StackWalkControl* aStackWalkControlIfSupported) {
  // WARNING: this function runs within the profiler's "critical section".
  // WARNING: this function might be called while the profiler is inactive, and
  //          cannot rely on ActivePS.

  (void)aStackWalkControlIfSupported;  // TODO: Implement.

  const mcontext_t* mc = &aRegs.mContext->uc_mcontext;

  lul::UnwindRegs startRegs;
  memset(&startRegs, 0, sizeof(startRegs));

#  if defined(GP_PLAT_amd64_linux) || defined(GP_PLAT_amd64_android)
  startRegs.xip = lul::TaggedUWord(mc->gregs[REG_RIP]);
  startRegs.xsp = lul::TaggedUWord(mc->gregs[REG_RSP]);
  startRegs.xbp = lul::TaggedUWord(mc->gregs[REG_RBP]);
#  elif defined(GP_PLAT_amd64_freebsd)
  startRegs.xip = lul::TaggedUWord(mc->mc_rip);
  startRegs.xsp = lul::TaggedUWord(mc->mc_rsp);
  startRegs.xbp = lul::TaggedUWord(mc->mc_rbp);
#  elif defined(GP_PLAT_arm_linux) || defined(GP_PLAT_arm_android)
  startRegs.r15 = lul::TaggedUWord(mc->arm_pc);
  startRegs.r14 = lul::TaggedUWord(mc->arm_lr);
  startRegs.r13 = lul::TaggedUWord(mc->arm_sp);
  startRegs.r12 = lul::TaggedUWord(mc->arm_ip);
  startRegs.r11 = lul::TaggedUWord(mc->arm_fp);
  startRegs.r7 = lul::TaggedUWord(mc->arm_r7);
#  elif defined(GP_PLAT_arm64_linux) || defined(GP_PLAT_arm64_android)
  startRegs.pc = lul::TaggedUWord(mc->pc);
  startRegs.x29 = lul::TaggedUWord(mc->regs[29]);
  startRegs.x30 = lul::TaggedUWord(mc->regs[30]);
  startRegs.sp = lul::TaggedUWord(mc->sp);
#  elif defined(GP_PLAT_arm64_freebsd)
  startRegs.pc = lul::TaggedUWord(mc->mc_gpregs.gp_elr);
  startRegs.x29 = lul::TaggedUWord(mc->mc_gpregs.gp_x[29]);
  startRegs.x30 = lul::TaggedUWord(mc->mc_gpregs.gp_lr);
  startRegs.sp = lul::TaggedUWord(mc->mc_gpregs.gp_sp);
#  elif defined(GP_PLAT_x86_linux) || defined(GP_PLAT_x86_android)
  startRegs.xip = lul::TaggedUWord(mc->gregs[REG_EIP]);
  startRegs.xsp = lul::TaggedUWord(mc->gregs[REG_ESP]);
  startRegs.xbp = lul::TaggedUWord(mc->gregs[REG_EBP]);
#  elif defined(GP_PLAT_mips64_linux)
  startRegs.pc = lul::TaggedUWord(mc->pc);
  startRegs.sp = lul::TaggedUWord(mc->gregs[29]);
  startRegs.fp = lul::TaggedUWord(mc->gregs[30]);
#  else
#    error "Unknown plat"
#  endif

  // Copy up to N_STACK_BYTES from rsp-REDZONE upwards, but not going past the
  // stack's registered top point.  Do some basic validity checks too.  This
  // assumes that the TaggedUWord holding the stack pointer value is valid, but
  // it should be, since it was constructed that way in the code just above.

  // We could construct |stackImg| so that LUL reads directly from the stack in
  // question, rather than from a copy of it.  That would reduce overhead and
  // space use a bit.  However, it gives a problem with dynamic analysis tools
  // (ASan, TSan, Valgrind) which is that such tools will report invalid or
  // racing memory accesses, and such accesses will be reported deep inside LUL.
  // By taking a copy here, we can either sanitise the copy (for Valgrind) or
  // copy it using an unchecked memcpy (for ASan, TSan).  That way we don't have
  // to try and suppress errors inside LUL.
  //
  // N_STACK_BYTES is set to 160KB.  This is big enough to hold all stacks
  // observed in some minutes of testing, whilst keeping the size of this
  // function (DoNativeBacktrace)'s frame reasonable.  Most stacks observed in
  // practice are small, 4KB or less, and so the copy costs are insignificant
  // compared to other profiler overhead.
  //
  // |stackImg| is allocated on this (the sampling thread's) stack.  That
  // implies that the frame for this function is at least N_STACK_BYTES large.
  // In general it would be considered unacceptable to have such a large frame
  // on a stack, but it only exists for the unwinder thread, and so is not
  // expected to be a problem.  Allocating it on the heap is troublesome because
  // this function runs whilst the sampled thread is suspended, so any heap
  // allocation risks deadlock.  Allocating it as a global variable is not
  // thread safe, which would be a problem if we ever allow multiple sampler
  // threads.  Hence allocating it on the stack seems to be the least-worst
  // option.

  lul::StackImage stackImg;

  {
#  if defined(GP_PLAT_amd64_linux) || defined(GP_PLAT_amd64_android) || \
      defined(GP_PLAT_amd64_freebsd)
    uintptr_t REDZONE_SIZE = 128;
    uintptr_t start = startRegs.xsp.Value() - REDZONE_SIZE;
#  elif defined(GP_PLAT_arm_linux) || defined(GP_PLAT_arm_android)
    uintptr_t REDZONE_SIZE = 0;
    uintptr_t start = startRegs.r13.Value() - REDZONE_SIZE;
#  elif defined(GP_PLAT_arm64_linux) || defined(GP_PLAT_arm64_android) || \
      defined(GP_PLAT_arm64_freebsd)
    uintptr_t REDZONE_SIZE = 0;
    uintptr_t start = startRegs.sp.Value() - REDZONE_SIZE;
#  elif defined(GP_PLAT_x86_linux) || defined(GP_PLAT_x86_android)
    uintptr_t REDZONE_SIZE = 0;
    uintptr_t start = startRegs.xsp.Value() - REDZONE_SIZE;
#  elif defined(GP_PLAT_mips64_linux)
    uintptr_t REDZONE_SIZE = 0;
    uintptr_t start = startRegs.sp.Value() - REDZONE_SIZE;
#  else
#    error "Unknown plat"
#  endif
    uintptr_t end = reinterpret_cast<uintptr_t>(aThreadData.StackTop());
    uintptr_t ws = sizeof(void*);
    start &= ~(ws - 1);
    end &= ~(ws - 1);
    uintptr_t nToCopy = 0;
    if (start < end) {
      nToCopy = end - start;
      if (nToCopy >= 1024u * 1024u) {
        // start is abnormally far from end, possibly due to some special code
        // that uses a separate stack elsewhere (e.g.: rr). In this case we just
        // give up on this sample.
        nToCopy = 0;
      } else if (nToCopy > lul::N_STACK_BYTES) {
        nToCopy = lul::N_STACK_BYTES;
      }
    }
    MOZ_ASSERT(nToCopy <= lul::N_STACK_BYTES);
    stackImg.mLen = nToCopy;
    stackImg.mStartAvma = start;
    if (nToCopy > 0) {
      // If this is a vanilla memcpy(), ASAN makes the following complaint:
      //
      //   ERROR: AddressSanitizer: stack-buffer-underflow ...
      //   ...
      //   HINT: this may be a false positive if your program uses some custom
      //   stack unwind mechanism or swapcontext
      //
      // This code is very much a custom stack unwind mechanism! So we use an
      // alternative memcpy() implementation that is ignored by ASAN.
#  if defined(MOZ_HAVE_ASAN_IGNORE)
      ASAN_memcpy(&stackImg.mContents[0], (void*)start, nToCopy);
#  else
      memcpy(&stackImg.mContents[0], (void*)start, nToCopy);
#  endif
      (void)VALGRIND_MAKE_MEM_DEFINED(&stackImg.mContents[0], nToCopy);
    }
  }

  size_t framePointerFramesAcquired = 0;
  lul::LUL* lul = CorePS::Lul();
  MOZ_RELEASE_ASSERT(lul);
  lul->Unwind(reinterpret_cast<uintptr_t*>(aNativeStack.mPCs),
              reinterpret_cast<uintptr_t*>(aNativeStack.mSPs),
              &aNativeStack.mCount, &framePointerFramesAcquired,
              MAX_NATIVE_FRAMES, &startRegs, &stackImg);

  // Update stats in the LUL stats object.  Unfortunately this requires
  // three global memory operations.
  lul->mStats.mContext += 1;
  lul->mStats.mCFI += aNativeStack.mCount - 1 - framePointerFramesAcquired;
  lul->mStats.mFP += framePointerFramesAcquired;
}

#endif

#ifdef HAVE_NATIVE_UNWIND
static void DoNativeBacktrace(
    const ThreadRegistration::UnlockedReaderAndAtomicRWOnThread& aThreadData,
    const Registers& aRegs, NativeStack& aNativeStack,
    StackWalkControl* aStackWalkControlIfSupported) {
  // This method determines which stackwalker is used for periodic and
  // synchronous samples. (Backtrace samples are treated differently, see
  // profiler_suspend_and_sample_thread() for details). The only part of the
  // ordering that matters is that LUL must precede FRAME_POINTER, because on
  // Linux they can both be present.
#  if defined(USE_LUL_STACKWALK)
  DoLULBacktrace(aThreadData, aRegs, aNativeStack,
                 aStackWalkControlIfSupported);
#  elif defined(USE_EHABI_STACKWALK)
  DoEHABIBacktrace(aThreadData, aRegs, aNativeStack,
                   aStackWalkControlIfSupported);
#  elif defined(USE_FRAME_POINTER_STACK_WALK)
  DoFramePointerBacktrace(aThreadData, aRegs, aNativeStack,
                          aStackWalkControlIfSupported);
#  elif defined(USE_MOZ_STACK_WALK)
  DoMozStackWalkBacktrace(aThreadData, aRegs, aNativeStack,
                          aStackWalkControlIfSupported);
#  else
#    error "Invalid configuration"
#  endif
}

void DoNativeBacktraceDirect(const void* stackTop, NativeStack& aNativeStack,
                             StackWalkControl* aStackWalkControlIfSupported) {
#  if defined(MOZ_PROFILING)
#    if defined(USE_FRAME_POINTER_STACK_WALK) || defined(USE_MOZ_STACK_WALK)
  // StackWalkCallback(/* frameNum */ 0, aRegs.mPC, aRegs.mSP, &aNativeStack);
  void* previousResumeSp = nullptr;
  for (;;) {
    MozStackWalk(StackWalkCallback, stackTop,
                 uint32_t(MAX_NATIVE_FRAMES - aNativeStack.mCount),
                 &aNativeStack);
    if constexpr (!StackWalkControl::scIsSupported) {
      break;
    } else {
      if (aNativeStack.mCount >= MAX_NATIVE_FRAMES) {
        // No room to add more frames.
        break;
      }
      if (!aStackWalkControlIfSupported ||
          aStackWalkControlIfSupported->ResumePointCount() == 0) {
        // No resume information.
        break;
      }
      void* lastSP = aNativeStack.mSPs[aNativeStack.mCount - 1];
      if (previousResumeSp &&
          ((uintptr_t)lastSP <= (uintptr_t)previousResumeSp)) {
        // No progress after the previous resume point.
        break;
      }
      const StackWalkControl::ResumePoint* resumePoint =
          aStackWalkControlIfSupported->GetResumePointCallingSp(lastSP);
      if (!resumePoint) {
        break;
      }
      void* sp = resumePoint->resumeSp;
      if (!sp) {
        // Null SP in a resume point means we stop here.
        break;
      }
      void* pc = resumePoint->resumePc;
      StackWalkCallback(/* frameNum */ aNativeStack.mCount, pc, sp,
                        &aNativeStack);
      ++aNativeStack.mCount;
      if (aNativeStack.mCount >= MAX_NATIVE_FRAMES) {
        break;
      }
      previousResumeSp = sp;
    }
  }
#    else   // defined(USE_FRAME_POINTER_STACK_WALK) ||
            // defined(USE_MOZ_STACK_WALK)
  MOZ_CRASH(
      "Cannot call DoNativeBacktraceDirect without either "
      "USE_FRAME_POINTER_STACK_WALK USE_MOZ_STACK_WALK");
#    endif  // defined(USE_FRAME_POINTER_STACK_WALK) ||
            // defined(USE_MOZ_STACK_WALK)
#  else
  aNativeStack.mCount = 0;
#  endif
}
#else
void DoNativeBacktraceDirect(const void* stackTop, NativeStack& aNativeStack,
                             StackWalkControl* aStackWalkControlIfSupported) {
  aNativeStack.mCount = 0;
}
#endif

// Writes some components shared by periodic and synchronous profiles to
// ActivePS's ProfileBuffer. (This should only be called from DoSyncSample()
// and DoPeriodicSample().)
//
// The grammar for entry sequences is in a comment above
// ProfileBuffer::StreamSamplesToJSON.
static inline void DoSharedSample(
    bool aIsSynchronous, uint32_t aFeatures,
    const ThreadRegistration::UnlockedReaderAndAtomicRWOnThread& aThreadData,
    JsFrame* aJsFrames, const Registers& aRegs, uint64_t aSamplePos,
    uint64_t aBufferRangeStart, ProfileBuffer& aBuffer,
    StackCaptureOptions aCaptureOptions = StackCaptureOptions::Full) {
  // WARNING: this function runs within the profiler's "critical section".

  MOZ_ASSERT(!aBuffer.IsThreadSafe(),
             "Mutexes cannot be used inside this critical section");

  ProfileBufferCollector collector(aBuffer, aSamplePos, aBufferRangeStart);
  StackWalkControl* stackWalkControlIfSupported = nullptr;
#if defined(HAVE_NATIVE_UNWIND)
  const bool captureNative = ProfilerFeature::HasStackWalk(aFeatures) &&
                             aCaptureOptions == StackCaptureOptions::Full;
  StackWalkControl stackWalkControl;
  if constexpr (StackWalkControl::scIsSupported) {
    if (captureNative) {
      stackWalkControlIfSupported = &stackWalkControl;
    }
  }
#endif  // defined(HAVE_NATIVE_UNWIND)
  const uint32_t jsFramesCount =
      aJsFrames ? ExtractJsFrames(aIsSynchronous, aThreadData, aRegs, collector,
                                  aJsFrames, stackWalkControlIfSupported)
                : 0;
  NativeStack nativeStack{.mCount = 0};
#if defined(HAVE_NATIVE_UNWIND)
  if (captureNative) {
    DoNativeBacktrace(aThreadData, aRegs, nativeStack,
                      stackWalkControlIfSupported);

    MergeStacks(aIsSynchronous, aThreadData, nativeStack, collector, aJsFrames,
                jsFramesCount);
  } else
#endif
  {
    MergeStacks(aIsSynchronous, aThreadData, nativeStack, collector, aJsFrames,
                jsFramesCount);

    // We can't walk the whole native stack, but we can record the top frame.
    if (aCaptureOptions == StackCaptureOptions::Full) {
      aBuffer.AddEntry(ProfileBufferEntry::NativeLeafAddr((void*)aRegs.mPC));
    }
  }
}

// Writes the components of a synchronous sample to the given ProfileBuffer.
static void DoSyncSample(
    uint32_t aFeatures,
    const ThreadRegistration::UnlockedReaderAndAtomicRWOnThread& aThreadData,
    const TimeStamp& aNow, const Registers& aRegs, ProfileBuffer& aBuffer,
    StackCaptureOptions aCaptureOptions) {
  // WARNING: this function runs within the profiler's "critical section".

  MOZ_ASSERT(aCaptureOptions != StackCaptureOptions::NoStack,
             "DoSyncSample should not be called when no capture is needed");

  const uint64_t bufferRangeStart = aBuffer.BufferRangeStart();

  const uint64_t samplePos =
      aBuffer.AddThreadIdEntry(aThreadData.Info().ThreadId());

  TimeDuration delta = aNow - CorePS::ProcessStartTime();
  aBuffer.AddEntry(ProfileBufferEntry::Time(delta.ToMilliseconds()));

  if (!aThreadData.GetJSContext()) {
    // No JSContext, there is no JS frame buffer (and no need for it).
    DoSharedSample(/* aIsSynchronous = */ true, aFeatures, aThreadData,
                   /* aJsFrames = */ nullptr, aRegs, samplePos,
                   bufferRangeStart, aBuffer, aCaptureOptions);
  } else {
    // JSContext is present, we need to lock the thread data to access the JS
    // frame buffer.
    ThreadRegistration::WithOnThreadRef([&](ThreadRegistration::OnThreadRef
                                                aOnThreadRef) {
      aOnThreadRef.WithConstLockedRWOnThread(
          [&](const ThreadRegistration::LockedRWOnThread& aLockedThreadData) {
            DoSharedSample(/* aIsSynchronous = */ true, aFeatures, aThreadData,
                           aLockedThreadData.GetJsFrameBuffer(), aRegs,
                           samplePos, bufferRangeStart, aBuffer,
                           aCaptureOptions);
          });
    });
  }
}

// Writes the components of a periodic sample to ActivePS's ProfileBuffer.
// The ThreadId entry is already written in the main ProfileBuffer, its location
// is `aSamplePos`, we can write the rest to `aBuffer` (which may be different).
static inline void DoPeriodicSample(
    PSLockRef aLock,
    const ThreadRegistration::UnlockedReaderAndAtomicRWOnThread& aThreadData,
    const Registers& aRegs, uint64_t aSamplePos, uint64_t aBufferRangeStart,
    ProfileBuffer& aBuffer) {
  // WARNING: this function runs within the profiler's "critical section".

  MOZ_RELEASE_ASSERT(ActivePS::Exists(aLock));

  JsFrameBuffer& jsFrames = CorePS::JsFrames(aLock);
  DoSharedSample(/* aIsSynchronous = */ false, ActivePS::Features(aLock),
                 aThreadData, jsFrames, aRegs, aSamplePos, aBufferRangeStart,
                 aBuffer);
}

#undef UNWINDING_REGS_HAVE_ECX_EDX
#undef UNWINDING_REGS_HAVE_R10_R12
#undef UNWINDING_REGS_HAVE_LR_R7
#undef UNWINDING_REGS_HAVE_LR_R11

// END sampling/unwinding code
////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////
// BEGIN saving/streaming code

const static uint64_t kJS_MAX_SAFE_UINTEGER = +9007199254740991ULL;

static int64_t SafeJSInteger(uint64_t aValue) {
  return aValue <= kJS_MAX_SAFE_UINTEGER ? int64_t(aValue) : -1;
}

static void AddSharedLibraryInfoToStream(JSONWriter& aWriter,
                                         const SharedLibrary& aLib) {
  aWriter.StartObjectElement();
  aWriter.IntProperty("start", SafeJSInteger(aLib.GetStart()));
  aWriter.IntProperty("end", SafeJSInteger(aLib.GetEnd()));
  aWriter.IntProperty("offset", SafeJSInteger(aLib.GetOffset()));
  aWriter.StringProperty("name", aLib.GetModuleName());
  aWriter.StringProperty("path", aLib.GetModulePath());
  aWriter.StringProperty("debugName", aLib.GetDebugName());
  aWriter.StringProperty("debugPath", aLib.GetDebugPath());
  aWriter.StringProperty("breakpadId", aLib.GetBreakpadId());
  aWriter.StringProperty("codeId", aLib.GetCodeId());
  aWriter.StringProperty("arch", aLib.GetArch());
  aWriter.EndObject();
}

void AppendSharedLibraries(JSONWriter& aWriter,
                           const SharedLibraryInfo& aInfo) {
  for (size_t i = 0; i < aInfo.GetSize(); i++) {
    AddSharedLibraryInfoToStream(aWriter, aInfo.GetEntry(i));
  }
}

static void StreamCategories(SpliceableJSONWriter& aWriter) {
  // Same order as ProfilingCategory. Format:
  // [
  //   {
  //     name: "Idle",
  //     color: "transparent",
  //     subcategories: ["Other"],
  //   },
  //   {
  //     name: "Other",
  //     color: "grey",
  //     subcategories: [
  //       "JSM loading",
  //       "Subprocess launching",
  //       "DLL loading"
  //     ]
  //   },
  //   ...
  // ]
  for (const auto& categoryInfo :
       mozilla::baseprofiler::GetProfilingCategoryList()) {
    aWriter.Start();
    aWriter.StringProperty("name", MakeStringSpan(categoryInfo.mName));
    aWriter.StringProperty("color", MakeStringSpan(categoryInfo.mColor));
    aWriter.StartArrayProperty("subcategories");
    for (const auto& subcategoryName : categoryInfo.mSubcategoryNames) {
      aWriter.StringElement(MakeStringSpan(subcategoryName));
    }
    aWriter.EndArray();
    aWriter.EndObject();
  }
}

static mozilla::StaticMutex sCustomMarkerSchemasMutex;
static mozilla::StaticAutoPtr<nsTHashMap<nsCStringHashKey, nsString>>
    sCustomMarkerSchemas;

void profiler_register_marker_schema(const nsCString& aSchemaName,
                                     const nsString& aSchemaJSON) {
  mozilla::StaticMutexAutoLock lock(sCustomMarkerSchemasMutex);
  if (!sCustomMarkerSchemas) {
    sCustomMarkerSchemas = new nsTHashMap<nsCStringHashKey, nsString>();
  }

  sCustomMarkerSchemas->InsertOrUpdate(aSchemaName, aSchemaJSON);
}

static void StreamCustomMarkerSchemas(
    baseprofiler::SpliceableJSONWriter& aWriter) {
  mozilla::StaticMutexAutoLock lock(sCustomMarkerSchemasMutex);
  if (!sCustomMarkerSchemas) {
    return;
  }

  for (auto iter = sCustomMarkerSchemas->Iter(); !iter.Done(); iter.Next()) {
    const nsString& jsonSchema = iter.Data();
    NS_ConvertUTF16toUTF8 utf8Schema(jsonSchema);

    aWriter.Splice(utf8Schema.get(), utf8Schema.Length());
  }
}

static void ClearCustomMarkerSchemas() {
  mozilla::StaticMutexAutoLock lock(sCustomMarkerSchemasMutex);
  if (sCustomMarkerSchemas) {
    sCustomMarkerSchemas->Clear();
    sCustomMarkerSchemas = nullptr;
  }
}

static void StreamMarkerSchema(SpliceableJSONWriter& aWriter) {
  // Get an array view with all registered marker-type-specific functions.
  base_profiler_markers_detail::Streaming::LockedMarkerTypeFunctionsList
      markerTypeFunctionsArray;
  // List of streamed marker names, this is used to spot duplicates.
  std::set<std::string> names;
  // Stream the display schema for each different one. (Duplications may come
  // from the same code potentially living in different libraries.)
  for (const auto& markerTypeFunctions : markerTypeFunctionsArray) {
    auto name = markerTypeFunctions.mMarkerTypeNameFunction();
    // Skip markers with empty names (used for wrapper types like
    // JSCustomMarker)
    if (name.empty()) {
      continue;
    }
    // std::set.insert(T&&) returns a pair, its `second` is true if the element
    // was actually inserted (i.e., it was not there yet.)
    const bool didInsert =
        names.insert(std::string(name.data(), name.size())).second;
    if (didInsert) {
      markerTypeFunctions.mMarkerSchemaFunction().Stream(aWriter, name);
    }
  }

  // Now stream the Rust marker schemas. Passing the names set as a void pointer
  // as well, so we can continue checking if the schemes are added already in
  // the Rust side.
  profiler::ffi::gecko_profiler_stream_marker_schemas(
      &aWriter, static_cast<void*>(&names));
}

// Some meta information that is better recorded before streaming the profile.
// This is *not* intended to be cached, as some values could change between
// profiling sessions.
struct PreRecordedMetaInformation {
  bool mAsyncStacks;

  // This struct should only live on the stack, so it's fine to use Auto
  // strings.
  nsAutoCString mHttpPlatform;
  nsAutoCString mHttpOscpu;
  nsAutoCString mHttpMisc;

  nsAutoCString mRuntimeABI;
  nsAutoCString mRuntimeToolkit;

  nsAutoCString mAppInfoProduct;
  nsAutoCString mAppInfoAppBuildID;
  nsAutoCString mAppInfoSourceURL;

  int32_t mProcessInfoCpuCount;
  int32_t mProcessInfoCpuCores;
  nsAutoCString mProcessInfoCpuName;
};

// This function should be called out of the profiler lock.
// It gathers non-trivial data that doesn't require the profiler to stop, or for
// which the request could theoretically deadlock if the profiler is locked.
static PreRecordedMetaInformation PreRecordMetaInformation(
    bool aShutdown = false) {
  MOZ_ASSERT(!PSAutoLock::IsLockedOnCurrentThread());

  PreRecordedMetaInformation info = {};  // Aggregate-init all fields.

  if (!NS_IsMainThread()) {
    // Leave these properties out if we're not on the main thread.
    // At the moment, the only case in which this function is called on a
    // background thread is if we're in a content process and are going to
    // send this profile to the parent process. In that case, the parent
    // process profile's "meta" object already has the rest of the properties,
    // and the parent process profile is dumped on that process's main thread.
    return info;
  }

  info.mAsyncStacks =
      !aShutdown && Preferences::GetBool("javascript.options.asyncstack");

  nsresult res;

  if (nsCOMPtr<nsIHttpProtocolHandler> http =
          do_GetService(NS_NETWORK_PROTOCOL_CONTRACTID_PREFIX "http", &res);
      !NS_FAILED(res) && http) {
    (void)http->GetPlatform(info.mHttpPlatform);

#if defined(XP_MACOSX)
    // On Mac, the http "oscpu" is capped at 10.15, so we need to get the real
    // OS version directly.
    int major = 0;
    int minor = 0;
    int bugfix = 0;
    nsCocoaFeatures::GetSystemVersion(major, minor, bugfix);
    if (major != 0) {
      info.mHttpOscpu.AppendLiteral("macOS ");
      info.mHttpOscpu.AppendInt(major);
      info.mHttpOscpu.AppendLiteral(".");
      info.mHttpOscpu.AppendInt(minor);
      info.mHttpOscpu.AppendLiteral(".");
      info.mHttpOscpu.AppendInt(bugfix);
    } else
#endif
#if defined(GP_OS_windows)
      // On Windows, the http "oscpu" is capped at Windows 10, so we need to get
      // the real OS version directly.
      OSVERSIONINFO ovi = {sizeof(OSVERSIONINFO)};
    if (GetVersionEx(&ovi)) {
      info.mHttpOscpu.AppendLiteral("Windows ");
      // The major version returned for Windows 11 is 10, but we can
      // identify it from the build number.
      info.mHttpOscpu.AppendInt(
          ovi.dwBuildNumber >= 22000 ? 11 : int32_t(ovi.dwMajorVersion));
      info.mHttpOscpu.AppendLiteral(".");
      info.mHttpOscpu.AppendInt(int32_t(ovi.dwMinorVersion));
#  if defined(_ARM64_)
      info.mHttpOscpu.AppendLiteral(" Arm64");
#  endif
      info.mHttpOscpu.AppendLiteral("; build=");
      info.mHttpOscpu.AppendInt(int32_t(ovi.dwBuildNumber));
    } else
#endif
    {
      (void)http->GetOscpu(info.mHttpOscpu);
    }

    // Firefox version is capped to 109.0 in the http "misc" field due to some
    // webcompat issues (Bug 1805967). We need to put the real version instead.
    info.mHttpMisc.AssignLiteral("rv:");
    info.mHttpMisc.AppendLiteral(MOZILLA_UAVERSION);
  }

  if (nsCOMPtr<nsIXULRuntime> runtime =
          do_GetService("@mozilla.org/xre/runtime;1");
      runtime) {
    (void)runtime->GetXPCOMABI(info.mRuntimeABI);
    (void)runtime->GetWidgetToolkit(info.mRuntimeToolkit);
  }

  if (nsCOMPtr<nsIXULAppInfo> appInfo =
          do_GetService("@mozilla.org/xre/app-info;1");
      appInfo) {
    (void)appInfo->GetName(info.mAppInfoProduct);
    (void)appInfo->GetAppBuildID(info.mAppInfoAppBuildID);
    (void)appInfo->GetSourceURL(info.mAppInfoSourceURL);
  }

  ProcessInfo processInfo = {};  // Aggregate-init all fields to false/zeroes.
  if (NS_SUCCEEDED(CollectProcessInfo(processInfo))) {
    info.mProcessInfoCpuCount = processInfo.cpuCount;
    info.mProcessInfoCpuCores = processInfo.cpuCores;
    info.mProcessInfoCpuName = processInfo.cpuName;
  }

  return info;
}

// Implemented in platform-specific cpps, to add object properties describing
// the units of CPU measurements in samples.
static void StreamMetaPlatformSampleUnits(PSLockRef aLock,
                                          SpliceableJSONWriter& aWriter);

static void MaybeWriteRawStartTimeValue(SpliceableJSONWriter& aWriter,
                                        const TimeStamp& aStartTime) {
#ifdef XP_LINUX
  aWriter.DoubleProperty(
      "startTimeAsClockMonotonicNanosecondsSinceBoot",
      static_cast<double>(aStartTime.RawClockMonotonicNanosecondsSinceBoot()));
#endif

#ifdef XP_DARWIN
  aWriter.DoubleProperty(
      "startTimeAsMachAbsoluteTimeNanoseconds",
      static_cast<double>(aStartTime.RawMachAbsoluteTimeNanoseconds()));
#endif

#ifdef XP_WIN
  uint64_t startTimeQPC = aStartTime.RawQueryPerformanceCounterValue();
  aWriter.DoubleProperty("startTimeAsQueryPerformanceCounterValue",
                         static_cast<double>(startTimeQPC));
#endif
}

static void StreamMetaJSCustomObject(
    PSLockRef aLock, SpliceableJSONWriter& aWriter, bool aIsShuttingDown,
    const PreRecordedMetaInformation& aPreRecordedMetaInformation) {
  MOZ_RELEASE_ASSERT(CorePS::Exists() && ActivePS::Exists(aLock));

  aWriter.IntProperty("version", GECKO_PROFILER_FORMAT_VERSION);

  // The "startTime" field holds the number of milliseconds since midnight
  // January 1, 1970 GMT (the "Unix epoch"). This grotty code computes (Now -
  // (Now - ProcessStartTime)) to convert CorePS::ProcessStartTime() into that
  // form. Note: This start time, and the platform-specific "raw start time",
  // are the only absolute time values in the profile! All other timestamps are
  // relative to this startTime.
  TimeStamp startTime = CorePS::ProcessStartTime();
  TimeStamp now = TimeStamp::Now();
  double millisecondsSinceUnixEpoch = static_cast<double>(PR_Now()) / 1000.0;
  double millisecondsSinceStartTime = (now - startTime).ToMilliseconds();
  double millisecondsBetweenUnixEpochAndStartTime =
      millisecondsSinceUnixEpoch - millisecondsSinceStartTime;
  aWriter.DoubleProperty("startTime", millisecondsBetweenUnixEpochAndStartTime);

  MaybeWriteRawStartTimeValue(aWriter, startTime);

  aWriter.DoubleProperty("profilingStartTime", (ActivePS::ProfilingStartTime() -
                                                CorePS::ProcessStartTime())
                                                   .ToMilliseconds());

  if (const TimeStamp contentEarliestTime =
          ActivePS::Buffer(aLock)
              .UnderlyingChunkedBuffer()
              .GetEarliestChunkStartTimeStamp();
      !contentEarliestTime.IsNull()) {
    aWriter.DoubleProperty(
        "contentEarliestTime",
        (contentEarliestTime - CorePS::ProcessStartTime()).ToMilliseconds());
  } else {
    aWriter.NullProperty("contentEarliestTime");
  }

  const double profilingEndTime = profiler_time();
  aWriter.DoubleProperty("profilingEndTime", profilingEndTime);

  if (aIsShuttingDown) {
    aWriter.DoubleProperty("shutdownTime", profilingEndTime);
  } else {
    aWriter.NullProperty("shutdownTime");
  }

  aWriter.StartArrayProperty("categories");
  StreamCategories(aWriter);
  aWriter.EndArray();

  aWriter.StartArrayProperty("markerSchema");
  StreamMarkerSchema(aWriter);
  StreamCustomMarkerSchemas(aWriter);
  aWriter.EndArray();

  ActivePS::WriteActiveConfiguration(aLock, aWriter,
                                     MakeStringSpan("configuration"));

  aWriter.DoubleProperty("interval", ActivePS::Interval(aLock));
  aWriter.IntProperty("stackwalk", ActivePS::FeatureStackWalk(aLock));

#ifdef DEBUG
  aWriter.IntProperty("debug", 1);
#else
  aWriter.IntProperty("debug", 0);
#endif

  aWriter.IntProperty("gcpoison", JS::IsGCPoisoning() ? 1 : 0);

  aWriter.IntProperty("asyncstack", aPreRecordedMetaInformation.mAsyncStacks);

  aWriter.IntProperty("processType", XRE_GetProcessType());

  aWriter.StringProperty("updateChannel", MOZ_STRINGIFY(MOZ_UPDATE_CHANNEL));

  if (!aPreRecordedMetaInformation.mHttpPlatform.IsEmpty()) {
    aWriter.StringProperty("platform",
                           aPreRecordedMetaInformation.mHttpPlatform);
  }
  if (!aPreRecordedMetaInformation.mHttpOscpu.IsEmpty()) {
    aWriter.StringProperty("oscpu", aPreRecordedMetaInformation.mHttpOscpu);
  }
  if (!aPreRecordedMetaInformation.mHttpMisc.IsEmpty()) {
    aWriter.StringProperty("misc", aPreRecordedMetaInformation.mHttpMisc);
  }

  if (!aPreRecordedMetaInformation.mRuntimeABI.IsEmpty()) {
    aWriter.StringProperty("abi", aPreRecordedMetaInformation.mRuntimeABI);
  }
  if (!aPreRecordedMetaInformation.mRuntimeToolkit.IsEmpty()) {
    aWriter.StringProperty("toolkit",
                           aPreRecordedMetaInformation.mRuntimeToolkit);
  }

  if (!aPreRecordedMetaInformation.mAppInfoProduct.IsEmpty()) {
    aWriter.StringProperty("product",
                           aPreRecordedMetaInformation.mAppInfoProduct);
  }
  if (!aPreRecordedMetaInformation.mAppInfoAppBuildID.IsEmpty()) {
    aWriter.StringProperty("appBuildID",
                           aPreRecordedMetaInformation.mAppInfoAppBuildID);
  }
  if (!aPreRecordedMetaInformation.mAppInfoSourceURL.IsEmpty()) {
    aWriter.StringProperty("sourceURL",
                           aPreRecordedMetaInformation.mAppInfoSourceURL);
  }

  if (!aPreRecordedMetaInformation.mProcessInfoCpuName.IsEmpty()) {
    aWriter.StringProperty("CPUName",
                           aPreRecordedMetaInformation.mProcessInfoCpuName);
  }
  if (aPreRecordedMetaInformation.mProcessInfoCpuCores > 0) {
    aWriter.IntProperty("physicalCPUs",
                        aPreRecordedMetaInformation.mProcessInfoCpuCores);
  }
  if (aPreRecordedMetaInformation.mProcessInfoCpuCount > 0) {
    aWriter.IntProperty("logicalCPUs",
                        aPreRecordedMetaInformation.mProcessInfoCpuCount);
  }

#if defined(GP_OS_android)
  jni::String::LocalRef deviceInformation =
      java::GeckoJavaSampler::GetDeviceInformation();
  aWriter.StringProperty("device", deviceInformation->ToCString());
#endif

  aWriter.StartObjectProperty("sampleUnits");
  {
    aWriter.StringProperty("time", "ms");
    aWriter.StringProperty("eventDelay", "ms");
    StreamMetaPlatformSampleUnits(aLock, aWriter);
  }
  aWriter.EndObject();

  if (!NS_IsMainThread()) {
    // Leave the rest of the properties out if we're not on the main thread.
    // At the moment, the only case in which this function is called on a
    // background thread is if we're in a content process and are going to
    // send this profile to the parent process. In that case, the parent
    // process profile's "meta" object already has the rest of the properties,
    // and the parent process profile is dumped on that process's main thread.
    return;
  }

  // We should avoid collecting extension metadata for profiler when there is no
  // observer service, since a ExtensionPolicyService could not be created then.
  if (nsCOMPtr<nsIObserverService> os = services::GetObserverService()) {
    aWriter.StartObjectProperty("extensions");
    {
      {
        JSONSchemaWriter schema(aWriter);
        schema.WriteField("id");
        schema.WriteField("name");
        schema.WriteField("baseURL");
      }

      aWriter.StartArrayProperty("data");
      {
        nsTArray<RefPtr<WebExtensionPolicy>> exts;
        ExtensionPolicyService::GetSingleton().GetAll(exts);

        for (auto& ext : exts) {
          aWriter.StartArrayElement();

          nsAutoString id;
          ext->GetId(id);
          aWriter.StringElement(NS_ConvertUTF16toUTF8(id));

          aWriter.StringElement(NS_ConvertUTF16toUTF8(ext->Name()));

          auto url = ext->GetURL(u""_ns);
          if (url.isOk()) {
            aWriter.StringElement(NS_ConvertUTF16toUTF8(url.unwrap()));
          }

          aWriter.EndArray();
        }
      }
      aWriter.EndArray();
    }
    aWriter.EndObject();
  }
}

static void StreamPages(PSLockRef aLock, SpliceableJSONWriter& aWriter) {
  MOZ_RELEASE_ASSERT(CorePS::Exists());
  ActivePS::DiscardExpiredPages(aLock);
  for (const auto& page : ActivePS::ProfiledPages(aLock)) {
    page->StreamJSON(aWriter);
  }
}

#if defined(GP_OS_android)
template <int N>
static bool StartsWith(const nsACString& string, const char (&prefix)[N]) {
  if (N - 1 > string.Length()) {
    return false;
  }
  return memcmp(string.Data(), prefix, N - 1) == 0;
}

static JS::ProfilingCategoryPair InferJavaCategory(nsACString& aName) {
  if (aName.EqualsLiteral("android.os.MessageQueue.nativePollOnce()")) {
    return JS::ProfilingCategoryPair::IDLE;
  }
  if (aName.EqualsLiteral("java.lang.Object.wait()")) {
    return JS::ProfilingCategoryPair::JAVA_BLOCKED;
  }
  if (StartsWith(aName, "android.") || StartsWith(aName, "com.android.")) {
    return JS::ProfilingCategoryPair::JAVA_ANDROID;
  }
  if (StartsWith(aName, "mozilla.") || StartsWith(aName, "org.mozilla.")) {
    return JS::ProfilingCategoryPair::JAVA_MOZILLA;
  }
  if (StartsWith(aName, "java.") || StartsWith(aName, "sun.") ||
      StartsWith(aName, "com.sun.")) {
    return JS::ProfilingCategoryPair::JAVA_LANGUAGE;
  }
  if (StartsWith(aName, "kotlin.") || StartsWith(aName, "kotlinx.")) {
    return JS::ProfilingCategoryPair::JAVA_KOTLIN;
  }
  if (StartsWith(aName, "androidx.")) {
    return JS::ProfilingCategoryPair::JAVA_ANDROIDX;
  }
  return JS::ProfilingCategoryPair::OTHER;
}

// Marker type for Java markers without any details.
struct JavaMarker {
  static constexpr Span<const char> MarkerTypeName() {
    return MakeStringSpan("Java");
  }
  static void StreamJSONMarkerData(
      baseprofiler::SpliceableJSONWriter& aWriter) {}
  static MarkerSchema MarkerTypeDisplay() {
    using MS = MarkerSchema;
    MS schema{MS::Location::TimelineOverview, MS::Location::MarkerChart,
              MS::Location::MarkerTable};
    schema.SetAllLabels("{marker.name}");
    return schema;
  }
};

// Marker type for Java markers with a detail field.
struct JavaMarkerWithDetails {
  static constexpr Span<const char> MarkerTypeName() {
    return MakeStringSpan("JavaWithDetails");
  }
  static void StreamJSONMarkerData(baseprofiler::SpliceableJSONWriter& aWriter,
                                   const ProfilerString8View& aText) {
    // This (currently) needs to be called "name" to be searchable on the
    // front-end.
    aWriter.StringProperty("name", aText);
  }
  static MarkerSchema MarkerTypeDisplay() {
    using MS = MarkerSchema;
    MS schema{MS::Location::TimelineOverview, MS::Location::MarkerChart,
              MS::Location::MarkerTable};
    schema.SetTooltipLabel("{marker.name}");
    schema.SetChartLabel("{marker.data.name}");
    schema.SetTableLabel("{marker.data.name}");
    schema.AddKeyLabelFormat("name", "Details", MS::Format::String);
    return schema;
  }
};

static void CollectJavaThreadProfileData(
    nsTArray<java::GeckoJavaSampler::ThreadInfo::LocalRef>& javaThreads,
    ProfileBuffer& aProfileBuffer) {
  // Retrieve metadata about the threads.
  const auto threadCount = java::GeckoJavaSampler::GetRegisteredThreadCount();
  for (int i = 0; i < threadCount; i++) {
    javaThreads.AppendElement(
        java::GeckoJavaSampler::GetRegisteredThreadInfo(i));
  }

  // locked_profiler_start uses sample count is 1000 for Java thread.
  // This entry size is enough now, but we might have to estimate it
  // if we can customize it
  // Pass the samples
  int sampleId = 0;
  while (true) {
    const auto threadId = java::GeckoJavaSampler::GetThreadId(sampleId);
    double sampleTime = java::GeckoJavaSampler::GetSampleTime(sampleId);
    if (threadId == 0 || sampleTime == 0.0) {
      break;
    }

    aProfileBuffer.AddThreadIdEntry(ProfilerThreadId::FromNumber(threadId));
    aProfileBuffer.AddEntry(ProfileBufferEntry::Time(sampleTime));
    int frameId = 0;
    while (true) {
      jni::String::LocalRef frameName =
          java::GeckoJavaSampler::GetFrameName(sampleId, frameId++);
      if (!frameName) {
        break;
      }
      nsCString frameNameString = frameName->ToCString();

      auto categoryPair = InferJavaCategory(frameNameString);
      aProfileBuffer.CollectCodeLocation("", frameNameString.get(), 0, 0, 0,
                                         Nothing(), Nothing(),
                                         Some(categoryPair));
    }
    sampleId++;
  }

  // Pass the markers now
  while (true) {
    // Gets the data from the Android UI thread only.
    java::GeckoJavaSampler::Marker::LocalRef marker =
        java::GeckoJavaSampler::PollNextMarker();
    if (!marker) {
      // All markers are transferred.
      break;
    }

    // Get all the marker information from the Java thread using JNI.
    const auto threadId = ProfilerThreadId::FromNumber(marker->GetThreadId());
    nsCString markerName = marker->GetMarkerName()->ToCString();
    jni::String::LocalRef text = marker->GetMarkerText();
    TimeStamp startTime =
        CorePS::ProcessStartTime() +
        TimeDuration::FromMilliseconds(marker->GetStartTime());

    double endTimeMs = marker->GetEndTime();
    // A marker can be either a duration with start and end, or a point in time
    // with only startTime. If endTime is 0, this means it's a point in time.
    TimeStamp endTime = endTimeMs == 0
                            ? startTime
                            : CorePS::ProcessStartTime() +
                                  TimeDuration::FromMilliseconds(endTimeMs);
    MarkerTiming timing = endTimeMs == 0
                              ? MarkerTiming::InstantAt(startTime)
                              : MarkerTiming::Interval(startTime, endTime);

    if (!text) {
      // This marker doesn't have a text.
      AddMarkerToBuffer(aProfileBuffer.UnderlyingChunkedBuffer(), markerName,
                        geckoprofiler::category::JAVA_ANDROID,
                        {MarkerThreadId(threadId), std::move(timing)},
                        JavaMarker{});
    } else {
      // This marker has a text.
      AddMarkerToBuffer(aProfileBuffer.UnderlyingChunkedBuffer(), markerName,
                        geckoprofiler::category::JAVA_ANDROID,
                        {MarkerThreadId(threadId), std::move(timing)},
                        JavaMarkerWithDetails{}, text->ToCString());
    }
  }
}
#endif

UniquePtr<ProfilerCodeAddressService>
profiler_code_address_service_for_presymbolication() {
  static const bool preSymbolicate = []() {
    const char* symbolicate = getenv("MOZ_PROFILER_SYMBOLICATE");
    return symbolicate && symbolicate[0] != '\0';
  }();
  return preSymbolicate ? MakeUnique<ProfilerCodeAddressService>() : nullptr;
}

static ProfilerResult<ProfileGenerationAdditionalInformation>
locked_profiler_stream_json_for_this_process(
    PSLockRef aLock, SpliceableJSONWriter& aWriter, double aSinceTime,
    const PreRecordedMetaInformation& aPreRecordedMetaInformation,
    bool aIsShuttingDown, ProfilerCodeAddressService* aService,
    mozilla::ProgressLogger aProgressLogger) {
  LOG("locked_profiler_stream_json_for_this_process");

#ifdef DEBUG
  PRIntervalTime slowWithSleeps = 0;
  if (!XRE_IsParentProcess()) {
    for (const auto& filter : ActivePS::Filters(aLock)) {
      if (filter == "test-debug-child-slow-json") {
        LOG("test-debug-child-slow-json");
        // There are 10 slow-downs below, each will sleep 250ms, for a total of
        // 2.5s, which should trigger the first progress request after 1s, and
        // the next progress which will have advanced further, so this profile
        // shouldn't get dropped.
        slowWithSleeps = PR_MillisecondsToInterval(250);
      } else if (filter == "test-debug-child-very-slow-json") {
        LOG("test-debug-child-very-slow-json");
        // Wait for more than 2s without any progress, which should get this
        // profile discarded.
        PR_Sleep(PR_SecondsToInterval(5));
      }
    }
  }
#  define SLOW_DOWN_FOR_TESTING()                                        \
    if (slowWithSleeps != 0) {                                           \
      DEBUG_LOG("progress=%.0f%%, sleep...",                             \
                aProgressLogger.GetGlobalProgress().ToDouble() * 100.0); \
      PR_Sleep(slowWithSleeps);                                          \
    }
#else                             // #ifdef DEBUG
#  define SLOW_DOWN_FOR_TESTING() /* No slow-downs */
#endif                            // #ifdef DEBUG #else

  MOZ_RELEASE_ASSERT(CorePS::Exists() && ActivePS::Exists(aLock));

  AUTO_PROFILER_STATS(locked_profiler_stream_json_for_this_process);

  const double collectionStartMs = profiler_time();

  ProfileBuffer& buffer = ActivePS::Buffer(aLock);

  aProgressLogger.SetLocalProgress(1_pc, "Locked profile buffer");

  SLOW_DOWN_FOR_TESTING();

  // If there is a set "Window length", discard older data.
  Maybe<double> durationS = ActivePS::Duration(aLock);
  if (durationS.isSome()) {
    const double durationStartMs = collectionStartMs - *durationS * 1000;
    buffer.DiscardSamplesBeforeTime(durationStartMs);
  }
  aProgressLogger.SetLocalProgress(2_pc, "Discarded old data");

  if (aWriter.Failed()) {
    return Err(ProfilerError::JsonGenerationFailed);
  }
  SLOW_DOWN_FOR_TESTING();

#if defined(GP_OS_android)
  // Java thread profile data should be collected before serializing the meta
  // object. This is because Java thread adds some markers with marker schema
  // objects. And these objects should be added before the serialization of the
  // `profile.meta.markerSchema` array, so these marker schema objects can also
  // be serialized properly. That's why java thread profile data needs to be
  // done before everything.

  // We are allocating it chunk by chunk. So this will not allocate 64 MiB
  // at once. This size should be more than enough for java threads.
  // This buffer is being created for each process but Android has
  // relatively fewer processes compared to desktop, so it's okay here.
  mozilla::ProfileBufferChunkManagerWithLocalLimit javaChunkManager(
      64 * 1024 * 1024, 1024 * 1024);
  ProfileChunkedBuffer javaBufferManager(
      ProfileChunkedBuffer::ThreadSafety::WithoutMutex, javaChunkManager);
  ProfileBuffer javaBuffer(javaBufferManager);

  nsTArray<java::GeckoJavaSampler::ThreadInfo::LocalRef> javaThreads;

  if (ActivePS::FeatureJava(aLock)) {
    CollectJavaThreadProfileData(javaThreads, javaBuffer);
    aProgressLogger.SetLocalProgress(3_pc, "Collected Java thread");
  }
#endif

  // Put shared library info
  aWriter.StartArrayProperty("libs");
  SharedLibraryInfo sharedLibraryInfo = SharedLibraryInfo::GetInfoForSelf();
  sharedLibraryInfo.SortByAddress();
  AppendSharedLibraries(aWriter, sharedLibraryInfo);
  aWriter.EndArray();
  aProgressLogger.SetLocalProgress(4_pc, "Wrote library information");

  if (aWriter.Failed()) {
    return Err(ProfilerError::JsonGenerationFailed);
  }
  SLOW_DOWN_FOR_TESTING();

  // Put meta data
  aWriter.StartObjectProperty("meta");
  {
    StreamMetaJSCustomObject(aLock, aWriter, aIsShuttingDown,
                             aPreRecordedMetaInformation);
  }
  aWriter.EndObject();
  aProgressLogger.SetLocalProgress(5_pc, "Wrote profile metadata");

  if (aWriter.Failed()) {
    return Err(ProfilerError::JsonGenerationFailed);
  }
  SLOW_DOWN_FOR_TESTING();

  // Put page data
  aWriter.StartArrayProperty("pages");
  {
    StreamPages(aLock, aWriter);
  }
  aWriter.EndArray();
  aProgressLogger.SetLocalProgress(6_pc, "Wrote pages");

  buffer.StreamProfilerOverheadToJSON(
      aWriter, CorePS::ProcessStartTime(), aSinceTime,
      aProgressLogger.CreateSubLoggerTo(10_pc, "Wrote profiler overheads"));

  buffer.StreamCountersToJSON(
      aWriter, CorePS::ProcessStartTime(), aSinceTime,
      aProgressLogger.CreateSubLoggerTo(14_pc, "Wrote counters"));

  if (aWriter.Failed()) {
    return Err(ProfilerError::JsonGenerationFailed);
  }
  SLOW_DOWN_FOR_TESTING();

  // Collect JS sources from the main thread, since script source storage is
  // shared between all threads.
  nsTArray<mozilla::JSSourceEntry> jsSourceEntries =
      ActivePS::GatherJSSources(aLock);

  // Always stream the sources table that is shared between the threads, and get
  // the UUID to index mappings needed for frame serialization.
  nsTHashMap<SourceId, IndexIntoSourceTable> sourceIdToIndexMap =
      buffer.StreamSourceTableToJSON(aWriter, jsSourceEntries);

  // Lists the samples for each thread profile
  aWriter.StartArrayProperty("threads");
  {
    ActivePS::DiscardExpiredDeadProfiledThreads(aLock);
    aProgressLogger.SetLocalProgress(15_pc, "Discarded expired profiles");

    ThreadRegistry::LockedRegistry lockedRegistry;
    ActivePS::ProfiledThreadList threads =
        ActivePS::ProfiledThreads(lockedRegistry, aLock);

    const uint32_t threadCount = uint32_t(threads.length());

    if (aWriter.Failed()) {
      return Err(ProfilerError::JsonGenerationFailed);
    }
    SLOW_DOWN_FOR_TESTING();

    // Prepare the streaming context for each thread.
    ProcessStreamingContext processStreamingContext(
        threadCount, aWriter.SourceFailureLatch(), CorePS::ProcessStartTime(),
        aSinceTime);
    for (auto&& [i, progressLogger] : aProgressLogger.CreateLoopSubLoggersTo(
             20_pc, threadCount, "Preparing thread streaming contexts...")) {
      ActivePS::ProfiledThreadListElement& thread = threads[i];
      MOZ_RELEASE_ASSERT(thread.mProfiledThreadData);
      processStreamingContext.AddThreadStreamingContext(
          *thread.mProfiledThreadData, buffer, thread.mJSContext, aService,
          std::move(progressLogger), &sourceIdToIndexMap);
      if (aWriter.Failed()) {
        return Err(ProfilerError::JsonGenerationFailed);
      }
    }

    SLOW_DOWN_FOR_TESTING();

    // Read the buffer once, and extract all samples and markers that the
    // context expects.
    buffer.StreamSamplesAndMarkersToJSON(
        processStreamingContext, aProgressLogger.CreateSubLoggerTo(
                                     "Processing samples and markers...", 80_pc,
                                     "Processed samples and markers"));

    if (aWriter.Failed()) {
      return Err(ProfilerError::JsonGenerationFailed);
    }
    SLOW_DOWN_FOR_TESTING();

    // Stream each thread from the pre-filled context.
    ThreadStreamingContext* const contextListBegin =
        processStreamingContext.begin();
    MOZ_ASSERT(uint32_t(processStreamingContext.end() - contextListBegin) ==
               threadCount);
    for (auto&& [i, progressLogger] : aProgressLogger.CreateLoopSubLoggersTo(
             92_pc, threadCount, "Streaming threads...")) {
      ThreadStreamingContext& threadStreamingContext = contextListBegin[i];
      threadStreamingContext.FinalizeWriter();
      threadStreamingContext.mProfiledThreadData.StreamJSON(
          std::move(threadStreamingContext), aWriter,
          CorePS::ProcessName(aLock), CorePS::ETLDplus1(aLock),
          CorePS::ProcessStartTime(), aService, std::move(progressLogger));
      if (aWriter.Failed()) {
        return Err(ProfilerError::JsonGenerationFailed);
      }
    }
    aProgressLogger.SetLocalProgress(92_pc, "Wrote samples and markers");

#if defined(GP_OS_android)
    if (ActivePS::FeatureJava(aLock)) {
      for (java::GeckoJavaSampler::ThreadInfo::LocalRef& threadInfo :
           javaThreads) {
        ProfiledThreadData threadData(ThreadRegistrationInfo{
            threadInfo->GetName()->ToCString().get(),
            ProfilerThreadId::FromNumber(threadInfo->GetId()), false,
            CorePS::ProcessStartTime()});

        threadData.StreamJSON(
            javaBuffer, nullptr, aWriter, CorePS::ProcessName(aLock),
            CorePS::ETLDplus1(aLock), CorePS::ProcessStartTime(), aSinceTime,
            nullptr,
            aProgressLogger.CreateSubLoggerTo("Streaming Java thread...", 96_pc,
                                              "Streamed Java thread"));
      }
      if (aWriter.Failed()) {
        return Err(ProfilerError::JsonGenerationFailed);
      }
    } else {
      aProgressLogger.SetLocalProgress(96_pc, "No Java thread");
    }
#endif

    UniquePtr<char[]> baseProfileThreads =
        ActivePS::MoveBaseProfileThreads(aLock);
    if (baseProfileThreads) {
      aWriter.Splice(MakeStringSpan(baseProfileThreads.get()));
      if (aWriter.Failed()) {
        return Err(ProfilerError::JsonGenerationFailed);
      }
      aProgressLogger.SetLocalProgress(97_pc, "Wrote baseprofiler data");
    } else {
      aProgressLogger.SetLocalProgress(97_pc, "No baseprofiler data");
    }
  }
  aWriter.EndArray();

  SLOW_DOWN_FOR_TESTING();

  aWriter.StartArrayProperty("pausedRanges");
  {
    buffer.StreamPausedRangesToJSON(
        aWriter, aSinceTime,
        aProgressLogger.CreateSubLoggerTo("Streaming pauses...", 99_pc,
                                          "Streamed pauses"));
  }
  aWriter.EndArray();

  if (aWriter.Failed()) {
    return Err(ProfilerError::JsonGenerationFailed);
  }

  ProfilingLog::Access([&](Json::Value& aProfilingLogObject) {
    aProfilingLogObject[Json::StaticString{
        "profilingLogEnd" TIMESTAMP_JSON_SUFFIX}] = ProfilingLog::Timestamp();

    aWriter.StartObjectProperty("profilingLog");
    {
      nsAutoCString pid;
      pid.AppendInt(int64_t(profiler_current_process_id().ToNumber()));
      Json::String logString = ToCompactString(aProfilingLogObject);
      aWriter.SplicedJSONProperty(pid, logString);
    }
    aWriter.EndObject();
  });

  const double collectionEndMs = profiler_time();

  // Record timestamps for the collection into the buffer, so that consumers
  // know why we didn't collect any samples for its duration.
  // We put these entries into the buffer after we've collected the profile,
  // so they'll be visible for the *next* profile collection (if they haven't
  // been overwritten due to buffer wraparound by then).
  buffer.AddEntry(ProfileBufferEntry::CollectionStart(collectionStartMs));
  buffer.AddEntry(ProfileBufferEntry::CollectionEnd(collectionEndMs));

#ifdef DEBUG
  if (slowWithSleeps != 0) {
    LOG("locked_profiler_stream_json_for_this_process done");
  }
#endif  // DEBUG

  return ProfileGenerationAdditionalInformation{std::move(sharedLibraryInfo),
                                                std::move(jsSourceEntries)};
}

// Keep this internal function non-static, so it may be used by tests.
ProfilerResult<ProfileGenerationAdditionalInformation>
do_profiler_stream_json_for_this_process(
    SpliceableJSONWriter& aWriter, double aSinceTime, bool aIsShuttingDown,
    ProfilerCodeAddressService* aService,
    mozilla::ProgressLogger aProgressLogger) {
  LOG("profiler_stream_json_for_this_process");

  MOZ_RELEASE_ASSERT(CorePS::Exists());

  const auto preRecordedMetaInformation = PreRecordMetaInformation();

  aProgressLogger.SetLocalProgress(2_pc, "PreRecordMetaInformation done");

  if (profiler_is_active()) {
    invoke_profiler_state_change_callbacks(ProfilingState::GeneratingProfile);
  }

  PSAutoLock lock;

  if (!ActivePS::Exists(lock)) {
    return Err(ProfilerError::IsInactive);
  }

  ProfileGenerationAdditionalInformation additionalInfo =
      MOZ_TRY(locked_profiler_stream_json_for_this_process(
          lock, aWriter, aSinceTime, preRecordedMetaInformation,
          aIsShuttingDown, aService,
          aProgressLogger.CreateSubLoggerFromTo(
              3_pc, "locked_profiler_stream_json_for_this_process started",
              100_pc, "locked_profiler_stream_json_for_this_process done")));

  if (aWriter.Failed()) {
    return Err(ProfilerError::JsonGenerationFailed);
  }
  return additionalInfo;
}

ProfilerResult<ProfileGenerationAdditionalInformation>
profiler_stream_json_for_this_process(SpliceableJSONWriter& aWriter,
                                      double aSinceTime, bool aIsShuttingDown,
                                      ProfilerCodeAddressService* aService,
                                      mozilla::ProgressLogger aProgressLogger) {
  MOZ_RELEASE_ASSERT(
      !XRE_IsParentProcess() || NS_IsMainThread(),
      "In the parent process, profiles should only be generated from the main "
      "thread, otherwise they will be incomplete.");

  ProfileGenerationAdditionalInformation additionalInfo =
      MOZ_TRY(do_profiler_stream_json_for_this_process(
          aWriter, aSinceTime, aIsShuttingDown, aService,
          std::move(aProgressLogger)));

  return additionalInfo;
}

// END saving/streaming code
////////////////////////////////////////////////////////////////////////

static char FeatureCategory(uint32_t aFeature) {
  if (aFeature & DefaultFeatures()) {
    if (aFeature & AvailableFeatures()) {
      return 'D';
    }
    return 'd';
  }

  if (aFeature & StartupExtraDefaultFeatures()) {
    if (aFeature & AvailableFeatures()) {
      return 'S';
    }
    return 's';
  }

  if (aFeature & AvailableFeatures()) {
    return '-';
  }
  return 'x';
}

static void PrintUsage() {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());

  printf(
      "\n"
      "Profiler environment variable usage:\n"
      "\n"
      "  MOZ_PROFILER_HELP\n"
      "  If set to any value, prints this message.\n"
      "  Use MOZ_BASE_PROFILER_HELP for BaseProfiler help.\n"
      "\n"
      "  MOZ_LOG\n"
      "  Enables logging. The levels of logging available are\n"
      "  'prof:3' (least verbose), 'prof:4', 'prof:5' (most verbose).\n"
      "\n"
      "  MOZ_PROFILER_STARTUP\n"
      "  If set to any value other than '' or '0'/'N'/'n', starts the\n"
      "  profiler immediately on start-up.\n"
      "  Useful if you want profile code that runs very early.\n"
      "\n"
      "  MOZ_PROFILER_STARTUP_ENTRIES=<%u..%u>\n"
      "  If MOZ_PROFILER_STARTUP is set, specifies the number of entries per\n"
      "  process in the profiler's circular buffer when the profiler is first\n"
      "  started.\n"
      "  If unset, the platform default is used:\n"
      "  %u entries per process, or %u when MOZ_PROFILER_STARTUP is set.\n"
      "  (%u bytes per entry -> %u or %u total bytes per process)\n"
      "  Optional units in bytes: KB, KiB, MB, MiB, GB, GiB\n"
      "\n"
      "  MOZ_PROFILER_STARTUP_DURATION=<1..>\n"
      "  If MOZ_PROFILER_STARTUP is set, specifies the maximum life time of\n"
      "  entries in the the profiler's circular buffer when the profiler is\n"
      "  first started, in seconds.\n"
      "  If unset, the life time of the entries will only be restricted by\n"
      "  MOZ_PROFILER_STARTUP_ENTRIES (or its default value), and no\n"
      "  additional time duration restriction will be applied.\n"
      "\n"
      "  MOZ_PROFILER_STARTUP_INTERVAL=<1..%d>\n"
      "  If MOZ_PROFILER_STARTUP is set, specifies the sample interval,\n"
      "  measured in milliseconds, when the profiler is first started.\n"
      "  If unset, the platform default is used.\n"
      "\n"
      "  MOZ_PROFILER_STARTUP_FEATURES_BITFIELD=<Number>\n"
      "  If MOZ_PROFILER_STARTUP is set, specifies the profiling features, as\n"
      "  the integer value of the features bitfield.\n"
      "  If unset, the value from MOZ_PROFILER_STARTUP_FEATURES is used.\n"
      "\n"
      "  MOZ_PROFILER_STARTUP_FEATURES=<Features>\n"
      "  If MOZ_PROFILER_STARTUP is set, specifies the profiling features, as\n"
      "  a comma-separated list of strings.\n"
      "  Ignored if  MOZ_PROFILER_STARTUP_FEATURES_BITFIELD is set.\n"
      "  If unset, the platform default is used.\n"
      "\n"
      "    Features: (x=unavailable, D/d=default/unavailable,\n"
      "               S/s=MOZ_PROFILER_STARTUP extra default/unavailable)\n",
      unsigned(scMinimumBufferEntries), unsigned(scMaximumBufferEntries),
      unsigned(PROFILER_DEFAULT_ENTRIES.Value()),
      unsigned(PROFILER_DEFAULT_STARTUP_ENTRIES.Value()),
      unsigned(scBytesPerEntry),
      unsigned(PROFILER_DEFAULT_ENTRIES.Value() * scBytesPerEntry),
      unsigned(PROFILER_DEFAULT_STARTUP_ENTRIES.Value() * scBytesPerEntry),
      PROFILER_MAX_INTERVAL);

#define PRINT_FEATURE(n_, str_, Name_, desc_)                                  \
  printf("    %c %7u: \"%s\" (%s)\n", FeatureCategory(ProfilerFeature::Name_), \
         ProfilerFeature::Name_, str_, desc_);

  PROFILER_FOR_EACH_FEATURE(PRINT_FEATURE)

#undef PRINT_FEATURE

  printf(
      "    -          \"default\" (All above D+S defaults)\n"
      "\n"
      "  MOZ_PROFILER_STARTUP_FILTERS=<Filters>\n"
      "  If MOZ_PROFILER_STARTUP is set, specifies the thread filters, as a\n"
      "  comma-separated list of strings. A given thread will be sampled if\n"
      "  any of the filters is a case-insensitive substring of the thread\n"
      "  name. If unset, a default is used.\n"
      "\n"
      "  MOZ_PROFILER_STARTUP_ACTIVE_TAB_ID=<Number>\n"
      "  This variable is used to propagate the activeTabID of\n"
      "  the profiler init params to subprocesses.\n"
      "\n"
      "  MOZ_PROFILER_SHUTDOWN=<Filename>\n"
      "  If set, the profiler saves a profile to the named file on shutdown.\n"
      "  If the Filename contains \"%%p\", this will be replaced with the'\n"
      "  process id of the parent process.\n"
      "\n"
      "  MOZ_PROFILER_SYMBOLICATE\n"
      "  If set, the profiler will pre-symbolicate profiles.\n"
      "  *Note* This will add a significant pause when gathering data, and\n"
      "  is intended mainly for local development.\n"
      "\n"
      "  MOZ_PROFILER_LUL_TEST\n"
      "  If set to any value, runs LUL unit tests at startup.\n"
      "\n"
      "  This platform %s native unwinding.\n"
      "\n",
#if defined(HAVE_NATIVE_UNWIND)
      "supports"
#else
      "does not support"
#endif
  );
}

////////////////////////////////////////////////////////////////////////
// BEGIN Sampler

#if defined(GP_OS_linux) || defined(GP_OS_android)
struct SigHandlerCoordinator;
#endif

// Sampler performs setup and teardown of the state required to sample with the
// profiler. Sampler may exist when ActivePS is not present.
//
// SuspendAndSampleAndResumeThread must only be called from a single thread,
// and must not sample the thread it is being called from. A separate Sampler
// instance must be used for each thread which wants to capture samples.

// WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING
//
// With the exception of SamplerThread, all Sampler objects must be Disable-d
// before releasing the lock which was used to create them. This avoids races
// on linux with the SIGPROF signal handler.

class Sampler {
 public:
  // Sets up the profiler such that it can begin sampling.
  explicit Sampler(PSLockRef aLock);

  // Disable the sampler, restoring it to its previous state. This must be
  // called once, and only once, before the Sampler is destroyed.
  void Disable(PSLockRef aLock);

  // This method suspends and resumes the samplee thread. It calls the passed-in
  // function-like object aProcessRegs (passing it a populated |const
  // Registers&| arg) while the samplee thread is suspended.  Note that
  // the aProcessRegs function must be very careful not to do anything that
  // requires a lock, since we may have interrupted the thread at any point.
  // As an example, you can't call TimeStamp::Now() since on windows it
  // takes a lock on the performance counter.
  //
  // Func must be a function-like object of type `void()`.
  template <typename Func>
  void SuspendAndSampleAndResumeThread(
      PSLockRef aLock,
      const ThreadRegistration::UnlockedReaderAndAtomicRWOnThread& aThreadData,
      const TimeStamp& aNow, const Func& aProcessRegs);

 private:
#if defined(GP_OS_linux) || defined(GP_OS_android) || defined(GP_OS_freebsd)
  // Used to restore the SIGPROF handler when ours is removed.
  struct sigaction mOldSigprofHandler;

  // This process' ID. Needed as an argument for tgkill in
  // SuspendAndSampleAndResumeThread.
  ProfilerProcessId mMyPid;

  // The sampler thread's ID.  Used to assert that it is not sampling itself,
  // which would lead to deadlock.
  ProfilerThreadId mSamplerTid;

 public:
  // This is the one-and-only variable used to communicate between the sampler
  // thread and the samplee thread's signal handler. It's static because the
  // samplee thread's signal handler is static.
  static struct SigHandlerCoordinator* sSigHandlerCoordinator;
#endif
};

// END Sampler
////////////////////////////////////////////////////////////////////////

// Platform-specific function that retrieves per-thread CPU measurements.
static RunningTimes GetThreadRunningTimesDiff(
    PSLockRef aLock,
    ThreadRegistration::UnlockedRWForLockedProfiler& aThreadData);
// Platform-specific function that *may* discard CPU measurements since the
// previous call to GetThreadRunningTimesDiff, if the way to suspend threads on
// this platform may add running times to that thread.
// No-op otherwise, if suspending a thread doesn't make it work.
static void DiscardSuspendedThreadRunningTimes(
    PSLockRef aLock,
    ThreadRegistration::UnlockedRWForLockedProfiler& aThreadData);

// Platform-specific function that retrieves process CPU measurements.
static RunningTimes GetProcessRunningTimesDiff(
    PSLockRef aLock, RunningTimes& aPreviousRunningTimesToBeUpdated);

// Template function to be used by `GetThreadRunningTimesDiff()` (unless some
// platform has a better way to achieve this).
// It help perform CPU measurements and tie them to a timestamp, such that the
// measurements and timestamp are very close together.
// This is necessary, because the relative CPU usage is computed by dividing
// consecutive CPU measurements by their timestamp difference; if there was an
// unexpected big gap, it could skew this computation and produce impossible
// spikes that would hide the rest of the data. See bug 1685938 for more info.
// Note that this may call the measurement function more than once; it is
// assumed to normally be fast.
// This was verified experimentally, but there is currently no regression
// testing for it; see follow-up bug 1687402.
template <typename GetCPURunningTimesFunction>
RunningTimes GetRunningTimesWithTightTimestamp(
    GetCPURunningTimesFunction&& aGetCPURunningTimesFunction) {
  // Once per process, compute a threshold over which running times and their
  // timestamp is considered too far apart.
  static const TimeDuration scMaxRunningTimesReadDuration = [&]() {
    // Run the main CPU measurements + timestamp a number of times and capture
    // their durations.
    constexpr int loops = 128;
    TimeDuration durations[loops];
    RunningTimes runningTimes;
    TimeStamp before = TimeStamp::Now();
    for (auto& duration : durations) {
      AUTO_PROFILER_STATS(GetRunningTimes_MaxRunningTimesReadDuration);
      aGetCPURunningTimesFunction(runningTimes);
      const TimeStamp after = TimeStamp::Now();
      duration = after - before;
      before = after;
    }
    // Move median duration to the middle.
    std::nth_element(&durations[0], &durations[loops / 2], &durations[loops]);
    // Use median*8 as cut-off point.
    // Typical durations should be around a microsecond, the cut-off should then
    // be around 10 microseconds, well below the expected minimum inter-sample
    // interval (observed as a few milliseconds), so overall this should keep
    // cpu/interval spikes
    return durations[loops / 2] * 8;
  }();

  // Record CPU measurements between two timestamps.
  RunningTimes runningTimes;
  TimeStamp before = TimeStamp::Now();
  aGetCPURunningTimesFunction(runningTimes);
  TimeStamp after = TimeStamp::Now();
  const TimeDuration duration = after - before;

  // In most cases, the above should be quick enough. But if not (e.g., because
  // of an OS context switch), repeat once:
  if (MOZ_UNLIKELY(duration > scMaxRunningTimesReadDuration)) {
    AUTO_PROFILER_STATS(GetRunningTimes_REDO);
    RunningTimes runningTimes2;
    aGetCPURunningTimesFunction(runningTimes2);
    TimeStamp after2 = TimeStamp::Now();
    const TimeDuration duration2 = after2 - after;
    if (duration2 < duration) {
      // We did it faster, use the new results. (But it could still be slower
      // than expected, see note below for why it's acceptable.)
      // This must stay *after* the CPU measurements.
      runningTimes2.SetPostMeasurementTimeStamp(after2);
      return runningTimes2;
    }
    // Otherwise use the initial results, they were slow, but faster than the
    // second attempt.
    // This means that something bad happened twice in a row on the same thread!
    // So trying more times would be unlikely to get much better, and would be
    // more expensive than the precision is worth.
    // At worst, it means that a spike of activity may be reported in the next
    // time slice. But in the end, the cumulative work is conserved, so it
    // should still be visible at about the correct time in the graph.
    AUTO_PROFILER_STATS(GetRunningTimes_RedoWasWorse);
  }

  // This must stay *after* the CPU measurements.
  runningTimes.SetPostMeasurementTimeStamp(after);

  return runningTimes;
}

////////////////////////////////////////////////////////////////////////
// BEGIN SamplerThread

// The sampler thread controls sampling and runs whenever the profiler is
// active. It periodically runs through all registered threads, finds those
// that should be sampled, then pauses and samples them.

class SamplerThread {
 public:
  // Creates a sampler thread, but doesn't start it.
  SamplerThread(PSLockRef aLock, uint32_t aActivityGeneration,
                double aIntervalMilliseconds, uint32_t aFeatures);
  ~SamplerThread();

  // This runs on (is!) the sampler thread.
  void Run();

#if defined(GP_OS_windows)
  // This runs on (is!) the thread to spy on unregistered threads.
  void RunUnregisteredThreadSpy();
#endif

  // This runs on the main thread.
  void Stop(PSLockRef aLock);

  void AppendPostSamplingCallback(PSLockRef, PostSamplingCallback&& aCallback) {
    // We are under lock, so it's safe to just modify the list pointer.
    // Also this means the sampler has not started its run yet, so any callback
    // added now will be invoked at the end of the next loop; this guarantees
    // that the callback will be invoked after at least one full sampling loop.
    mPostSamplingCallbackList = MakeUnique<PostSamplingCallbackListItem>(
        std::move(mPostSamplingCallbackList), std::move(aCallback));
  }

  SamplerThread(const SamplerThread&) = delete;
  void operator=(const SamplerThread&) = delete;

 private:
  void SpyOnUnregisteredThreads();

  // Item containing a post-sampling callback, and a tail-list of more items.
  // Using a linked list means no need to move items when adding more, and
  // "stealing" the whole list is one pointer move.
  struct PostSamplingCallbackListItem {
    UniquePtr<PostSamplingCallbackListItem> mPrev;
    PostSamplingCallback mCallback;

    PostSamplingCallbackListItem(UniquePtr<PostSamplingCallbackListItem> aPrev,
                                 PostSamplingCallback&& aCallback)
        : mPrev(std::move(aPrev)), mCallback(std::move(aCallback)) {}
  };

  [[nodiscard]] UniquePtr<PostSamplingCallbackListItem>
  TakePostSamplingCallbacks(PSLockRef) {
    return std::move(mPostSamplingCallbackList);
  }

  static void InvokePostSamplingCallbacks(
      UniquePtr<PostSamplingCallbackListItem> aCallbacks,
      SamplingState aSamplingState) {
    if (!aCallbacks) {
      return;
    }
    // We want to drill down to the last element in this list, which is the
    // oldest one, so that we invoke them in FIFO order.
    // We don't expect many callbacks, so it's safe to recurse. Note that we're
    // moving-from the UniquePtr, so the tail will implicitly get destroyed.
    InvokePostSamplingCallbacks(std::move(aCallbacks->mPrev), aSamplingState);
    // We are going to destroy this item, so we can safely move-from the
    // callback before calling it (in case it has an rvalue-ref-qualified call
    // operator).
    std::move(aCallbacks->mCallback)(aSamplingState);
    // It may be tempting for a future maintainer to change aCallbacks into an
    // rvalue reference; this will remind them not to do that!
    static_assert(
        std::is_same_v<decltype(aCallbacks),
                       UniquePtr<PostSamplingCallbackListItem>>,
        "We need to capture the list by-value, to implicitly destroy it");
  }

  // This suspends the calling thread for the given number of microseconds.
  // Best effort timing.
  void SleepMicro(uint32_t aMicroseconds);

  // The sampler used to suspend and sample threads.
  Sampler mSampler;

  // The activity generation, for detecting when the sampler thread must stop.
  const uint32_t mActivityGeneration;

  // The interval between samples, measured in microseconds.
  const int mIntervalMicroseconds;

  // The OS-specific handle for the sampler thread.
#if defined(GP_OS_windows)
  HANDLE mThread;
  HANDLE mUnregisteredThreadSpyThread = nullptr;
  enum class SpyingState {
    NoSpying,
    Spy_Initializing,
    // Spy is waiting for SamplerToSpy_Start or MainToSpy_Shutdown.
    Spy_Waiting,
    // Sampler requests spy to start working. May be pre-empted by
    // MainToSpy_Shutdown.
    SamplerToSpy_Start,
    // Spy is currently working, cannot be interrupted, only the spy is allowed
    // to change the state again.
    Spy_Working,
    // Main control requests spy to shut down.
    MainToSpy_Shutdown,
    // Spy notified main control that it's out of the loop, about to exit.
    SpyToMain_ShuttingDown
  };
  SpyingState mSpyingState = SpyingState::NoSpying;
  // The sampler will increment this while the spy is working, then while the
  // spy is waiting the sampler will decrement it until <=0 before starting the
  // spy. This will ensure that the work doesn't take more than 50% of a CPU
  // core.
  int mDelaySpyStart = 0;
  Monitor mSpyingStateMonitor MOZ_UNANNOTATED{
      "SamplerThread::mSpyingStateMonitor"};
#elif defined(GP_OS_darwin) || defined(GP_OS_linux) || \
    defined(GP_OS_android) || defined(GP_OS_freebsd)
  pthread_t mThread;
#endif

  // Post-sampling callbacks are kept in a simple linked list, which will be
  // stolen by the sampler thread at the end of its next run.
  UniquePtr<PostSamplingCallbackListItem> mPostSamplingCallbackList;

#if defined(GP_OS_windows)
  bool mNoTimerResolutionChange = true;
#endif

  struct SpiedThread {
    base::ProcessId mThreadId;
    nsCString mName;
    uint64_t mCPUTimeNs;

    SpiedThread(base::ProcessId aThreadId, const nsACString& aName,
                uint64_t aCPUTimeNs)
        : mThreadId(aThreadId), mName(aName), mCPUTimeNs(aCPUTimeNs) {}

    // Comparisons with just a thread id, for easy searching in an array.
    friend bool operator==(const SpiedThread& aSpiedThread,
                           base::ProcessId aThreadId) {
      return aSpiedThread.mThreadId == aThreadId;
    }
    friend bool operator==(base::ProcessId aThreadId,
                           const SpiedThread& aSpiedThread) {
      return aThreadId == aSpiedThread.mThreadId;
    }
  };

  // Time at which mSpiedThreads was previously updated. Null before 1st update.
  TimeStamp mLastSpying;
  // Unregistered threads that have been found, and are being spied on.
  using SpiedThreads = AutoTArray<SpiedThread, 128>;
  SpiedThreads mSpiedThreads;
};

namespace geckoprofiler::markers {
struct CPUSpeedMarker {
  static constexpr Span<const char> MarkerTypeName() {
    return MakeStringSpan("CPUSpeed");
  }
  static void StreamJSONMarkerData(baseprofiler::SpliceableJSONWriter& aWriter,
                                   uint32_t aCPUSpeedMHz) {
    aWriter.DoubleProperty("speed", double(aCPUSpeedMHz) / 1000);
  }
  static MarkerSchema MarkerTypeDisplay() {
    using MS = MarkerSchema;
    MS schema{MS::Location::MarkerChart, MS::Location::MarkerTable};
    schema.SetTableLabel("{marker.name} Speed = {marker.data.speed}GHz");
    schema.AddKeyLabelFormat("speed", "CPU Speed (GHz)", MS::Format::String);
    schema.AddChartColor("speed", MS::GraphType::Bar, MS::GraphColor::Ink);
    return schema;
  }
};
}  // namespace geckoprofiler::markers

// [[nodiscard]] static
bool ActivePS::AppendPostSamplingCallback(PSLockRef aLock,
                                          PostSamplingCallback&& aCallback) {
  if (!sInstance || !sInstance->mSamplerThread) {
    return false;
  }
  sInstance->mSamplerThread->AppendPostSamplingCallback(aLock,
                                                        std::move(aCallback));
  return true;
}

// This function is required because we need to create a SamplerThread within
// ActivePS's constructor, but SamplerThread is defined after ActivePS. It
// could probably be removed by moving some code around.
static SamplerThread* NewSamplerThread(PSLockRef aLock, uint32_t aGeneration,
                                       double aInterval, uint32_t aFeatures) {
  return new SamplerThread(aLock, aGeneration, aInterval, aFeatures);
}

// Held by the sampler thread while it writes a scheduled off-main-thread dump
// (profiler_schedule_dump_to_file). profiler_wait_for_scheduled_dump() takes it
// to wait for an in-progress dump to finish; on the exit-after-dump path the
// sampler exits the process while holding it, so such a waiter never resumes.
static mozilla::StaticMutex sScheduledDumpMutex;

// Save a profile to a file like profiler_save_profile_to_file(), but route the
// given shared progress cell into the streaming code so its 0..1 progress can
// be observed from another thread while the (long) dump runs.
static void profiler_save_profile_to_file_with_progress(
    const char* aFilename, RefPtr<ProgressLogger::SharedProgress> aProgress);

// A scheduled off-main-thread dump can take a long time to build in memory
// while the output file stays flat at ~350 KB (everything is spliced into the
// file only at the end), so file size cannot distinguish a slow-but-progressing
// dump from a hung one. The streaming code updates mProgress as it walks the
// profile buffer; this helper thread polls that cell and writes the 0..1
// fraction to a "<profile>.progress" sidecar file for a consumer that can't
// read our memory (e.g. an out-of-process test harness). If it ever blocks
// (say on the allocator the dump is contending for) the sidecar simply stops
// advancing, which a consumer already treats as a stall.
struct ScheduledDumpProgressEmitter {
  RefPtr<ProgressLogger::SharedProgress> mProgress;
  nsCString mSidecarPath;
  Atomic<bool, MemoryOrdering::Relaxed> mDone{false};
};

static void ScheduledDumpProgressEmitterThread(void* aArg) {
  NS_SetCurrentThreadName("ProfilerDumpProgress");
  auto* emitter = static_cast<ScheduledDumpProgressEmitter*>(aArg);
  // Poll the progress atomic on mProgress and write its current 0..1 value to
  // the sidecar file until the dump signals completion via mDone.
  while (!emitter->mDone) {
    {
      std::ofstream stream(emitter->mSidecarPath.get());
      stream << emitter->mProgress->Progress().ToDouble() << "\n";
    }
    // ~1s cadence (a consumer polls far less often), split into short steps so
    // the join on the non-exit-after-dump path returns promptly once mDone set.
    for (int i = 0; i < 10 && !emitter->mDone; ++i) {
      PR_Sleep(PR_MillisecondsToInterval(100));
    }
  }
}

// This function is the sampler thread.  This implementation is used for all
// targets.
void SamplerThread::Run() {
  NS_SetCurrentThreadName("SamplerThread");

  // Features won't change during this SamplerThread's lifetime, so we can read
  // them once and store them locally.
  const uint32_t features = []() -> uint32_t {
    PSAutoLock lock;
    if (!ActivePS::Exists(lock)) {
      // If there is no active profiler, it doesn't matter what we return,
      // because this thread will exit before any feature is used.
      return 0;
    }
    return ActivePS::Features(lock);
  }();

  // Not *no*-stack-sampling means we do want stack sampling.
  const bool stackSampling = !ProfilerFeature::HasNoStackSampling(features);

  // Use local ProfileBuffer and underlying buffer to capture the stack.
  // (This is to avoid touching the core buffer lock while a thread is
  // suspended, because that thread could be working with the core buffer as
  // well.
  mozilla::ProfileBufferChunkManagerSingle localChunkManager(
      ProfileBufferChunkManager::scExpectedMaximumStackSize);
  ProfileChunkedBuffer localBuffer(
      ProfileChunkedBuffer::ThreadSafety::WithoutMutex, localChunkManager);
  ProfileBuffer localProfileBuffer(localBuffer);

  // Will be kept between collections, to know what each collection does.
  auto previousState = localBuffer.GetState();

  // This will be filled at every loop, to be used by the next loop to compute
  // the CPU utilization between samples.
  RunningTimes processRunningTimes;

  // This will be set inside the loop, from inside the lock scope, to capture
  // all callbacks added before that, but none after the lock is released.
  UniquePtr<PostSamplingCallbackListItem> postSamplingCallbacks;
  // This will be set inside the loop, before invoking callbacks outside.
  SamplingState samplingState{};

  // Set inside the locked scope when a scheduled off-main-thread profile dump
  // (profiler_schedule_dump_to_file) is due, then acted on outside the lock
  // (profiler_save_profile_to_file takes the profiler lock itself).
  bool scheduledDumpDue = false;
  nsAutoCString scheduledDumpPath;
  bool scheduledDumpExitAfter = false;

  const TimeDuration sampleInterval =
      TimeDuration::FromMicroseconds(mIntervalMicroseconds);
  const uint32_t minimumIntervalSleepUs =
      static_cast<uint32_t>(mIntervalMicroseconds / 4);

  // This is the scheduled time at which each sampling loop should start.
  // It will determine the ideal next sampling start by adding the expected
  // interval, unless when sampling runs late -- See end of while() loop.
  TimeStamp scheduledSampleStart = TimeStamp::Now();

#if defined(HAVE_CPU_FREQ_SUPPORT)
  // Used to collect CPU core frequencies, if the cpufreq feature is on.
  Vector<uint32_t> CPUSpeeds;

  if (XRE_IsParentProcess() && ProfilerFeature::HasCPUFrequency(features) &&
      CPUSpeeds.resize(GetNumberOfProcessors())) {
    {
      PSAutoLock lock;
      if (ProfilerCPUFreq* cpuFreq = ActivePS::MaybeCPUFreq(lock); cpuFreq) {
        cpuFreq->Sample();
        for (size_t i = 0; i < CPUSpeeds.length(); ++i) {
          CPUSpeeds[i] = cpuFreq->GetCPUSpeedMHz(i);
        }
      }
    }
    TimeStamp now = TimeStamp::Now();
    for (size_t i = 0; i < CPUSpeeds.length(); ++i) {
      nsAutoCString name;
      name.AssignLiteral("CPU ");
      name.AppendInt(i);

      PROFILER_MARKER(name, OTHER,
                      MarkerOptions(MarkerThreadId::MainThread(),
                                    MarkerTiming::IntervalStart(now)),
                      CPUSpeedMarker, CPUSpeeds[i]);
    }
  }
#endif

  while (true) {
    const TimeStamp sampleStart = TimeStamp::Now();

    // This scope is for |lock|. It ends before we sleep below.
    {
      // There should be no local callbacks left from a previous loop.
      MOZ_ASSERT(!postSamplingCallbacks);

      PSAutoLock lock;
      TimeStamp lockAcquired = TimeStamp::Now();

      // Move all the post-sampling callbacks locally, so that new ones cannot
      // sneak in between the end of the lock scope and the invocation after it.
      postSamplingCallbacks = TakePostSamplingCallbacks(lock);

      if (!ActivePS::Exists(lock)) {
        // Exit the `while` loop, including the lock scope, before invoking
        // callbacks and returning.
        samplingState = SamplingState::JustStopped;
        break;
      }

      // At this point profiler_stop() might have been called, and
      // profiler_start() might have been called on another thread. If this
      // happens the generation won't match.
      if (ActivePS::Generation(lock) != mActivityGeneration) {
        samplingState = SamplingState::JustStopped;
        // Exit the `while` loop, including the lock scope, before invoking
        // callbacks and returning.
        break;
      }

      ActivePS::ClearExpiredExitProfiles(lock);

      if (const TimeStamp& deadline = CorePS::ScheduledDumpDeadline(lock);
          !deadline.IsNull() && sampleStart >= deadline) {
        scheduledDumpDue = true;
        scheduledDumpPath = CorePS::ScheduledDumpPath(lock);
        scheduledDumpExitAfter = CorePS::ScheduledDumpExitAfter(lock);
        CorePS::CancelScheduledDump(lock);
      }

      TimeStamp expiredMarkersCleaned = TimeStamp::Now();

      if (int(gSkipSampling) <= 0 && !ActivePS::IsSamplingPaused(lock)) {
        double sampleStartDeltaMs =
            (sampleStart - CorePS::ProcessStartTime()).ToMilliseconds();
        ProfileBuffer& buffer = ActivePS::Buffer(lock);

        // Before sampling counters, update the process CPU counter if active.
        if (ActivePS::ProcessCPUCounter* processCPUCounter =
                ActivePS::MaybeProcessCPUCounter(lock);
            processCPUCounter) {
          RunningTimes processRunningTimesDiff =
              GetProcessRunningTimesDiff(lock, processRunningTimes);
          Maybe<uint64_t> cpu = processRunningTimesDiff.GetJsonThreadCPUDelta();
          if (cpu) {
            processCPUCounter->Add(static_cast<int64_t>(*cpu));
          }
        }

#if defined(HAVE_CPU_FREQ_SUPPORT)
        if (XRE_IsParentProcess() && CPUSpeeds.length() > 0) {
          unsigned newSpeed[CPUSpeeds.length()];
          if (ProfilerCPUFreq* cpuFreq = ActivePS::MaybeCPUFreq(lock);
              cpuFreq) {
            cpuFreq->Sample();
            for (size_t i = 0; i < CPUSpeeds.length(); ++i) {
              newSpeed[i] = cpuFreq->GetCPUSpeedMHz(i);
            }
          }
          TimeStamp now = TimeStamp::Now();
          for (size_t i = 0; i < CPUSpeeds.length(); ++i) {
            if (newSpeed[i] == CPUSpeeds[i]) {
              continue;
            }

            nsAutoCString name;
            name.AssignLiteral("CPU ");
            name.AppendInt(i);

            PROFILER_MARKER_UNTYPED(
                name, OTHER,
                MarkerOptions(MarkerThreadId::MainThread(),
                              MarkerTiming::IntervalEnd(now)));
            PROFILER_MARKER(name, OTHER,
                            MarkerOptions(MarkerThreadId::MainThread(),
                                          MarkerTiming::IntervalStart(now)),
                            CPUSpeedMarker, newSpeed[i]);

            CPUSpeeds[i] = newSpeed[i];
          }
        }
#endif

        if (PowerCounters* powerCounters = ActivePS::MaybePowerCounters(lock);
            powerCounters) {
          powerCounters->Sample();
        }

        // handle per-process generic counters
        double counterSampleStartDeltaMs =
            (TimeStamp::Now() - CorePS::ProcessStartTime()).ToMilliseconds();
        const Vector<BaseProfilerCount*>& counters = CorePS::Counters(lock);
        for (auto& counter : counters) {
          if (auto sample = counter->Sample(); sample.isSampleNew) {
            // create Buffer entries for each counter
            buffer.AddEntry(ProfileBufferEntry::CounterId(counter));
            buffer.AddEntry(
                ProfileBufferEntry::Time(counterSampleStartDeltaMs));
#if defined(MOZ_MEMORY) && defined(MOZ_PROFILER_MEMORY)
            if (ActivePS::IsMemoryCounter(counter, lock)) {
              // For the memory counter, substract the size of our buffer to
              // avoid giving the misleading impression that the memory use
              // keeps on growing when it's just the profiler session that's
              // using a larger buffer as it gets longer.
              sample.count -= static_cast<int64_t>(
                  ActivePS::ControlledChunkManager(lock).TotalSize());
            }
#endif
            buffer.AddEntry(ProfileBufferEntry::Count(sample.count));
            if (sample.number) {
              buffer.AddEntry(ProfileBufferEntry::Number(sample.number));
            }
          }
        }
        TimeStamp countersSampled = TimeStamp::Now();

        {
          // Thread CPU utilization is always gathered, even when periodic
          // stack sampling is disabled, so we always run a sampling pass
          // (bug 2033816).
          samplingState = SamplingState::SamplingCompleted;

          // Prevent threads from ending (or starting) and allow access to all
          // OffThreadRef's.
          ThreadRegistry::LockedRegistry lockedRegistry;

          for (ThreadRegistry::OffThreadRef offThreadRef : lockedRegistry) {
            ThreadRegistration::UnlockedRWForLockedProfiler&
                unlockedThreadData =
                    offThreadRef.UnlockedRWForLockedProfilerRef();
            ProfiledThreadData* profiledThreadData =
                unlockedThreadData.GetProfiledThreadData(lock);
            if (!profiledThreadData) {
              // This thread is not being profiled, continue with the next one.
              continue;
            }

            const ThreadProfilingFeatures whatToProfile =
                unlockedThreadData.ProfilingFeatures();
            const bool threadCPUUtilization = DoFeaturesIntersect(
                whatToProfile, ThreadProfilingFeatures::CPUUtilization);
            const bool threadStackSampling =
                stackSampling &&
                DoFeaturesIntersect(whatToProfile,
                                    ThreadProfilingFeatures::Sampling);
            if (!threadCPUUtilization && !threadStackSampling) {
              // Nothing to profile on this thread, continue with the next one.
              continue;
            }

            const ProfilerThreadId threadId =
                unlockedThreadData.Info().ThreadId();

            const RunningTimes runningTimesDiff = [&]() {
              if (!threadCPUUtilization) {
                // If we don't need CPU measurements, we only need a timestamp.
                return RunningTimes(TimeStamp::Now());
              }
              return GetThreadRunningTimesDiff(lock, unlockedThreadData);
            }();

            const TimeStamp& now = runningTimesDiff.PostMeasurementTimeStamp();
            double threadSampleDeltaMs =
                (now - CorePS::ProcessStartTime()).ToMilliseconds();

            // If the thread is asleep and has been sampled before in the same
            // sleep episode, or otherwise(*) if there was zero CPU activity
            // since the previous sampling, find and copy the previous sample,
            // as that's cheaper than taking a new sample.
            // (*) Tech note: The asleep check is done first and always, because
            //     it is more reliable, and knows if it's the first asleep
            //     sample, which cannot be duplicated; if the test was the other
            //     way around, it could find zero CPU and then short-circuit
            //     that state-changing second-asleep-check operation, which
            //     could result in an unneeded sample.
            // However we're using current running times (instead of copying the
            // old ones) because some work could have happened.
            if (threadStackSampling &&
                (unlockedThreadData.CanDuplicateLastSampleDueToSleep() ||
                 runningTimesDiff.GetThreadCPUDelta() == Some(uint64_t(0)))) {
              const bool dup_ok = ActivePS::Buffer(lock).DuplicateLastSample(
                  threadId, threadSampleDeltaMs,
                  profiledThreadData->LastSample(), runningTimesDiff);
              if (dup_ok) {
                continue;
              }
            }

            AUTO_PROFILER_STATS(gecko_SamplerThread_Run_DoPeriodicSample);

            // Record the global profiler buffer's range start now, before
            // adding the first entry for this thread's sample.
            const uint64_t bufferRangeStart = buffer.BufferRangeStart();

            // Add the thread ID now, so we know its position in the main
            // buffer, which is used by some JS data.
            // (DoPeriodicSample only knows about the temporary local buffer.)
            const uint64_t samplePos = buffer.AddThreadIdEntry(threadId);
            profiledThreadData->LastSample() = Some(samplePos);

            // Also add the time, so it's always there after the thread ID, as
            // expected by the parser. (Other stack data is optional.)
            buffer.AddEntry(ProfileBufferEntry::TimeBeforeCompactStack(
                threadSampleDeltaMs));

            Maybe<double> unresponsiveDuration_ms;

            // If we have RunningTimes data, store it before the CompactStack.
            // Note: It is not stored inside the CompactStack so that it doesn't
            // get incorrectly duplicated when the thread is sleeping.
            if (!runningTimesDiff.IsEmpty()) {
              profiler_get_core_buffer().PutObjects(
                  ProfileBufferEntry::Kind::RunningTimes, runningTimesDiff);
            }

            if (threadStackSampling) {
              ThreadRegistry::OffThreadRef::RWFromAnyThreadWithLock
                  lockedThreadData = offThreadRef.GetLockedRWFromAnyThread();
              // Suspend the thread and collect its stack data in the local
              // buffer.
              mSampler.SuspendAndSampleAndResumeThread(
                  lock, lockedThreadData.DataCRef(), now,
                  [&](const Registers& aRegs, const TimeStamp& aNow) {
                    DoPeriodicSample(lock, lockedThreadData.DataCRef(), aRegs,
                                     samplePos, bufferRangeStart,
                                     localProfileBuffer);

                    // For "eventDelay", we want the input delay - but if
                    // there are no events in the input queue (or even if there
                    // are), we're interested in how long the delay *would* be
                    // for an input event now, which would be the time to finish
                    // the current event + the delay caused by any events
                    // already in the input queue (plus any High priority
                    // events).  Events at lower priorities (in a
                    // PrioritizedEventQueue) than Input count for input delay
                    // only for the duration that they're running, since when
                    // they finish, any queued input event would run.
                    //
                    // Unless we record the time state of all events and queue
                    // states at all times, this is hard to precisely calculate,
                    // but we can approximate it well in post-processing with
                    // RunningEventDelay and RunningEventStart.
                    //
                    // RunningEventDelay is the time duration the event was
                    // queued before starting execution.  RunningEventStart is
                    // the time the event started. (Note: since we care about
                    // Input event delays on MainThread, for
                    // PrioritizedEventQueues we return 0 for RunningEventDelay
                    // if the currently running event has a lower priority than
                    // Input (since Input events won't queue behind them).
                    //
                    // To directly measure this we would need to record the time
                    // at which the newest event currently in each queue at time
                    // X (the sample time) finishes running.  This of course
                    // would require looking into the future, or recording all
                    // this state and then post-processing it later. If we were
                    // to trace every event start and end we could do this, but
                    // it would have significant overhead to do so (and buffer
                    // usage).  From a recording of RunningEventDelays and
                    // RunningEventStarts we can infer the actual delay:
                    //
                    // clang-format off
                    // Event queue: <tail> D  :  C  :  B  : A <head>
                    // Time inserted (ms): 40 :  20 : 10  : 0
                    // Run Time (ms):      30 : 100 : 40  : 30
                    //
                    // 0    10   20   30   40   50   60   70   80   90  100  110  120  130  140  150  160  170
                    // [A||||||||||||]
                    //      ----------[B|||||||||||||||||]
                    //           -------------------------[C|||||||||||||||||||||||||||||||||||||||||||||||]
                    //                     -----------------------------------------------------------------[D|||||||||...]
                    //
                    // Calculate the delay of a new event added at time t: (run every sample)
                    //    TimeSinceRunningEventBlockedInputEvents = RunningEventDelay + (now - RunningEventStart);
                    //    effective_submission = now - TimeSinceRunningEventBlockedInputEvents;
                    //    delta = (now - last_sample_time);
                    //    last_sample_time = now;
                    //    for (t=effective_submission to now) {
                    //       delay[t] += delta;
                    //    }
                    //
                    // Can be reduced in overhead by:
                    //    TimeSinceRunningEventBlockedInputEvents = RunningEventDelay + (now - RunningEventStart);
                    //    effective_submission = now - TimeSinceRunningEventBlockedInputEvents;
                    //    if (effective_submission != last_submission) {
                    //      delta = (now - last_submision);
                    //      // this loop should be made to match each sample point in the range
                    //      // intead of assuming 1ms sampling as this pseudocode does
                    //      for (t=last_submission to effective_submission-1) {
                    //         delay[t] += delta;
                    //         delta -= 1; // assumes 1ms; adjust as needed to match for()
                    //      }
                    //      last_submission = effective_submission;
                    //    }
                    //
                    // Time  Head of queue   Running Event  RunningEventDelay  Delay of       Effective     Started    Calc (submission->now add 10ms)  Final
                    //                                                         hypothetical   Submission    Running @                                   result
                    //                                                         event E
                    // 0        Empty            A                0                30              0           0       @0=10                             30
                    // 10         B              A                0                60              0           0       @0=20, @10=10                     60
                    // 20         B              A                0               150              0           0       @0=30, @10=20, @20=10            150
                    // 30         C              B               20               140             10          30       @10=20, @20=10, @30=0            140
                    // 40         C              B               20               160                                  @10=30, @20=20...                160
                    // 50         C              B               20               150                                                                   150
                    // 60         C              B               20               140                                  @10=50, @20=40...                140
                    // 70         D              C               50               130             20          70       @20=50, @30=40...                130
                    // ...
                    // 160        D              C               50                40                                  @20=140, @30=130...               40
                    // 170      <empty>          D              140                30             40                   @40=140, @50=130... (rounding)    30
                    // 180      <empty>          D              140                20             40                   @40=150                           20
                    // 190      <empty>          D              140                10             40                   @40=160                           10
                    // 200      <empty>        <empty>            0                 0             NA                                                      0
                    //
                    // Function Delay(t) = the time between t and the time at which a hypothetical
                    // event e would start executing, if e was enqueued at time t.
                    //
                    // Delay(-1) = 0 // Before A was enqueued. No wait time, can start running
                    //               // instantly.
                    // Delay(0) = 30 // The hypothetical event e got enqueued just after A got
                    //               // enqueued. It can start running at 30, when A is done.
                    // Delay(5) = 25
                    // Delay(10) = 60 // Can start running at 70, after both A and B are done.
                    // Delay(19) = 51
                    // Delay(20) = 150 // Can start running at 170, after A, B & C.
                    // Delay(25) = 145
                    // Delay(30) = 170 // Can start running at 200, after A, B, C & D.
                    // Delay(120) = 80
                    // Delay(200) = 0 // (assuming nothing was enqueued after D)
                    //
                    // For every event that gets enqueued, the Delay time will go up by the
                    // event's running time at the time at which the event is enqueued.
                    // The Delay function will be a sawtooth of the following shape:
                    //
                    //             |\           |...
                    //             | \          |
                    //        |\   |  \         |
                    //        | \  |   \        |
                    //     |\ |  \ |    \       |
                    //  |\ | \|   \|     \      |
                    //  | \|              \     |
                    // _|                  \____|
                    //
                    //
                    // A more complex example with a PrioritizedEventQueue:
                    //
                    // Event queue: <tail> D  :  C  :  B  : A <head>
                    // Time inserted (ms): 40 :  20 : 10  : 0
                    // Run Time (ms):      30 : 100 : 40  : 30
                    // Priority:         Input: Norm: Norm: Norm
                    //
                    // 0    10   20   30   40   50   60   70   80   90  100  110  120  130  140  150  160  170
                    // [A||||||||||||]
                    //      ----------[B|||||||||||||||||]
                    //           ----------------------------------------[C|||||||||||||||||||||||||||||||||||||||||||||||]
                    //                     ---------------[D||||||||||||]
                    //
                    //
                    // Time  Head of queue   Running Event  RunningEventDelay  Delay of       Effective   Started    Calc (submission->now add 10ms)   Final
                    //                                                         hypothetical   Submission  Running @                                    result
                    //                                                         event
                    // 0        Empty            A                0                30              0           0       @0=10                             30
                    // 10         B              A                0                20              0           0       @0=20, @10=10                     20
                    // 20         B              A                0                10              0           0       @0=30, @10=20, @20=10             10
                    // 30         C              B                0                40             30          30       @30=10                            40
                    // 40         C              B                0                60             30                   @40=10, @30=20                    60
                    // 50         C              B                0                50             30                   @50=10, @40=20, @30=30            50
                    // 60         C              B                0                40             30                   @60=10, @50=20, @40=30, @30=40    40
                    // 70         C              D               30                30             40          70       @60=20, @50=30, @40=40            30
                    // 80         C              D               30                20             40          70       ...@50=40, @40=50                 20
                    // 90         C              D               30                10             40          70       ...@60=40, @50=50, @40=60         10
                    // 100      <empty>          C                0               100             100        100       @100=10                          100
                    // 110      <empty>          C                0                90             100        100       @110=10, @100=20                  90

                    //
                    // For PrioritizedEventQueue, the definition of the Delay(t) function is adjusted: the hypothetical event e has Input priority.
                    // Delay(-1) = 0 // Before A was enqueued. No wait time, can start running
                    //               // instantly.
                    // Delay(0) = 30 // The hypothetical input event e got enqueued just after A got
                    //               // enqueued. It can start running at 30, when A is done.
                    // Delay(5) = 25
                    // Delay(10) = 20
                    // Delay(25) = 5 // B has been queued, but e does not need to wait for B because e has Input priority and B does not.
                    //               // So e can start running at 30, when A is done.
                    // Delay(30) = 40 // Can start running at 70, after B is done.
                    // Delay(40) = 60 // Can start at 100, after B and D are done (D is Input Priority)
                    // Delay(80) = 20
                    // Delay(100) = 100 // Wait for C to finish

                    // clang-format on
                    //
                    // Alternatively we could insert (recycled instead of
                    // allocated/freed) input events at every sample period
                    // (1ms...), and use them to back-calculate the delay.  This
                    // might also be somewhat expensive, and would require
                    // guessing at the maximum delay, which would likely be in
                    // the seconds, and so you'd need 1000's of pre-allocated
                    // events per queue per thread - so there would be a memory
                    // impact as well.

                    TimeDuration currentEventDelay;
                    TimeDuration currentEventRunning;
                    lockedThreadData->GetRunningEventDelay(
                        aNow, currentEventDelay, currentEventRunning);

                    // Note: eventDelay is a different definition of
                    // responsiveness than the 16ms event injection.

                    // Don't suppress 0's for now; that can be a future
                    // optimization.  We probably want one zero to be stored
                    // before we start suppressing, which would be more
                    // complex.
                    unresponsiveDuration_ms =
                        Some(currentEventDelay.ToMilliseconds() +
                             currentEventRunning.ToMilliseconds());
                  });

              if (threadCPUUtilization) {
                // Suspending the thread for sampling could have added some
                // running time to it, discard any since the call to
                // GetThreadRunningTimesDiff above.
                DiscardSuspendedThreadRunningTimes(lock, unlockedThreadData);
              }

              // If we got eventDelay data, store it before the CompactStack.
              // Note: It is not stored inside the CompactStack so that it
              // doesn't get incorrectly duplicated when the thread is sleeping.
              if (unresponsiveDuration_ms.isSome()) {
                profiler_get_core_buffer().PutObjects(
                    ProfileBufferEntry::Kind::UnresponsiveDurationMs,
                    *unresponsiveDuration_ms);
              }
            }

            // There *must* be a CompactStack after a TimeBeforeCompactStack;
            // but note that other entries may have been concurrently inserted
            // between the TimeBeforeCompactStack above and now. If the captured
            // sample from `DoPeriodicSample` is complete, copy it into the
            // global buffer, otherwise add an empty one to satisfy the parser
            // that expects one.
            auto state = localBuffer.GetState();
            if (NS_WARN_IF(state.mFailedPutBytes !=
                           previousState.mFailedPutBytes)) {
              LOG("Stack sample too big for local storage, failed to store %u "
                  "bytes",
                  unsigned(state.mFailedPutBytes -
                           previousState.mFailedPutBytes));
              // There *must* be a CompactStack after a TimeBeforeCompactStack,
              // even an empty one.
              profiler_get_core_buffer().PutObjects(
                  ProfileBufferEntry::Kind::CompactStack,
                  UniquePtr<ProfileChunkedBuffer>(nullptr));
            } else if (state.mRangeEnd - previousState.mRangeEnd >=
                       *profiler_get_core_buffer().BufferLength()) {
              LOG("Stack sample too big for profiler storage, needed %u bytes",
                  unsigned(state.mRangeEnd - previousState.mRangeEnd));
              // There *must* be a CompactStack after a TimeBeforeCompactStack,
              // even an empty one.
              profiler_get_core_buffer().PutObjects(
                  ProfileBufferEntry::Kind::CompactStack,
                  UniquePtr<ProfileChunkedBuffer>(nullptr));
            } else {
              profiler_get_core_buffer().PutObjects(
                  ProfileBufferEntry::Kind::CompactStack, localBuffer);
            }

            // Clean up for the next run.
            localBuffer.Clear();
            previousState = localBuffer.GetState();
          }
        }

#if defined(USE_LUL_STACKWALK)
        // The LUL unwind object accumulates frame statistics. Periodically we
        // should poke it to give it a chance to print those statistics.  This
        // involves doing I/O (fprintf, __android_log_print, etc.) and so
        // can't safely be done from the critical section inside
        // SuspendAndSampleAndResumeThread, which is why it is done here.
        lul::LUL* lul = CorePS::Lul();
        if (lul) {
          lul->MaybeShowStats();
        }
#endif
        TimeStamp threadsSampled = TimeStamp::Now();

        {
          AUTO_PROFILER_STATS(Sampler_FulfillChunkRequests);
          ActivePS::FulfillChunkRequests(lock);
        }

        buffer.CollectOverheadStats(sampleStartDeltaMs,
                                    lockAcquired - sampleStart,
                                    expiredMarkersCleaned - lockAcquired,
                                    countersSampled - expiredMarkersCleaned,
                                    threadsSampled - countersSampled);
      } else {
        samplingState = SamplingState::SamplingPaused;
      }
    }
    // gPSMutex is not held after this point.

    // Invoke end-of-sampling callbacks outside of the locked scope.
    InvokePostSamplingCallbacks(std::move(postSamplingCallbacks),
                                samplingState);

    // We've hit the deadline for a scheduled profile dump; call
    // profiler_save_profile_to_file outside the lock to avoid a deadlock.
    if (scheduledDumpDue) {
      scheduledDumpDue = false;
      // Hold sScheduledDumpMutex across the whole write so a caller in
      // profiler_wait_for_scheduled_dump() blocks until this write finishes
      // (and, on the exit-after path, until the process is gone) rather than
      // acting while the profile is half-written.
      mozilla::StaticMutexAutoLock dumpLock(sScheduledDumpMutex);

      // Emit streaming progress to a "<profile>.progress" sidecar while the
      // dump runs, so a consumer can tell a slow-but-progressing dump from a
      // hung one instead of watching the (flat) profile file size.
      auto dumpProgress = MakeRefPtr<ProgressLogger::SharedProgress>();
      ScheduledDumpProgressEmitter emitter{dumpProgress};
      emitter.mSidecarPath = scheduledDumpPath;
      emitter.mSidecarPath.AppendLiteral(".progress");
      PRThread* emitterThread = PR_CreateThread(
          PR_USER_THREAD, ScheduledDumpProgressEmitterThread, &emitter,
          PR_PRIORITY_LOW, PR_GLOBAL_THREAD, PR_JOINABLE_THREAD, 0);

      profiler_save_profile_to_file_with_progress(scheduledDumpPath.get(),
                                                  dumpProgress);
      emitter.mDone = true;
      if (scheduledDumpExitAfter) {
        // The profile is fully written; exit now as requested. A caller parked
        // in profiler_wait_for_scheduled_dump() stays blocked on the mutex
        // until the process exits here, so it never runs its abort.
        AppShutdown::DoImmediateExit();
      }
      if (emitterThread) {
        PR_JoinThread(emitterThread);
      }
    }

    ProfilerChild::ProcessPendingUpdate();

    if (ProfilerFeature::HasUnregisteredThreads(features)) {
#if defined(GP_OS_windows)
      {
        MonitorAutoLock spyingStateLock{mSpyingStateMonitor};
        switch (mSpyingState) {
          case SpyingState::SamplerToSpy_Start:
          case SpyingState::Spy_Working:
            // If the spy is working (or about to work), record this loop
            // iteration to delay the next start.
            ++mDelaySpyStart;
            break;
          case SpyingState::Spy_Waiting:
            // The Spy is idle, waiting for instructions. Should we delay?
            if (--mDelaySpyStart <= 0) {
              mDelaySpyStart = 0;
              mSpyingState = SpyingState::SamplerToSpy_Start;
              mSpyingStateMonitor.NotifyAll();
            }
            break;
          default:
            // Otherwise the spy should be initializing or shutting down.
            MOZ_ASSERT(mSpyingState == SpyingState::Spy_Initializing ||
                       mSpyingState == SpyingState::MainToSpy_Shutdown ||
                       mSpyingState == SpyingState::SpyToMain_ShuttingDown);
            break;
        }
      }
#else
      // On non-Windows platforms, this is fast enough to run in this thread,
      // each sampling loop.
      SpyOnUnregisteredThreads();
#endif
    }

    // We expect the next sampling loop to start `sampleInterval` after this
    // loop here was scheduled to start.
    scheduledSampleStart += sampleInterval;

    // Try to sleep until we reach that next scheduled time.
    const TimeStamp beforeSleep = TimeStamp::Now();
    if (scheduledSampleStart >= beforeSleep) {
      // There is still time before the next scheduled sample time.
      const uint32_t sleepTimeUs = static_cast<uint32_t>(
          (scheduledSampleStart - beforeSleep).ToMicroseconds());
      if (sleepTimeUs >= minimumIntervalSleepUs) {
        SleepMicro(sleepTimeUs);
      } else {
        // If we're too close to that time, sleep the minimum amount of time.
        // Note that the next scheduled start is not shifted, so at the end of
        // the next loop, sleep may again be adjusted to get closer to schedule.
        SleepMicro(minimumIntervalSleepUs);
      }
    } else {
      // This sampling loop ended after the next sampling should have started!
      // There is little point to try and keep up to schedule now, it would
      // require more work, while it's likely we're late because the system is
      // already busy. Try and restart a normal schedule from now.
      scheduledSampleStart = beforeSleep + sampleInterval;
      SleepMicro(static_cast<uint32_t>(sampleInterval.ToMicroseconds()));
    }
  }

  // End of `while` loop. We can only be here from a `break` inside the loop.
  InvokePostSamplingCallbacks(std::move(postSamplingCallbacks), samplingState);
}

namespace geckoprofiler::markers {

struct UnregisteredThreadLifetimeMarker {
  static constexpr Span<const char> MarkerTypeName() {
    return MakeStringSpan("UnregisteredThreadLifetime");
  }
  static void StreamJSONMarkerData(baseprofiler::SpliceableJSONWriter& aWriter,
                                   base::ProcessId aThreadId,
                                   const ProfilerString8View& aName,
                                   const ProfilerString8View& aEndEvent) {
    aWriter.IntProperty("Thread Id", aThreadId);
    aWriter.StringProperty("Thread Name", aName.Length() != 0
                                              ? aName.AsSpan()
                                              : MakeStringSpan("~Unnamed~"));
    if (aEndEvent.Length() != 0) {
      aWriter.StringProperty("End Event", aEndEvent);
    }
  }
  static MarkerSchema MarkerTypeDisplay() {
    using MS = MarkerSchema;
    MS schema{MS::Location::MarkerChart, MS::Location::MarkerTable};
    schema.AddKeyFormat("Thread Id", MS::Format::Integer);
    schema.AddKeyFormat("Thread Name", MS::Format::String);
    schema.AddKeyFormat("End Event", MS::Format::String);
    schema.AddStaticLabelValue(
        "Note",
        "Start and end are approximate, based on first and last appearances.");
    schema.SetChartLabel(
        "{marker.data.Thread Name} (tid {marker.data.Thread Id})");
    schema.SetTableLabel("{marker.name} lifetime");
    return schema;
  }
};

struct UnregisteredThreadCPUMarker {
  static constexpr Span<const char> MarkerTypeName() {
    return MakeStringSpan("UnregisteredThreadCPU");
  }
  static void StreamJSONMarkerData(baseprofiler::SpliceableJSONWriter& aWriter,
                                   base::ProcessId aThreadId,
                                   int64_t aCPUDiffNs, const TimeStamp& aStart,
                                   const TimeStamp& aEnd) {
    aWriter.IntProperty("Thread Id", aThreadId);
    aWriter.IntProperty("CPU Time", aCPUDiffNs);
    aWriter.DoubleProperty(
        "CPU Utilization",
        double(aCPUDiffNs) / ((aEnd - aStart).ToMicroseconds() * 1000.0));
  }
  static MarkerSchema MarkerTypeDisplay() {
    using MS = MarkerSchema;
    MS schema{MS::Location::MarkerChart, MS::Location::MarkerTable};
    schema.AddKeyFormat("Thread Id", MS::Format::Integer);
    schema.AddKeyFormat("CPU Time", MS::Format::Nanoseconds);
    schema.AddKeyFormat("CPU Utilization", MS::Format::Percentage);
    schema.SetChartLabel("{marker.data.CPU Utilization}");
    schema.SetTableLabel("Activity: {marker.data.CPU Utilization}");
    return schema;
  }
};

}  // namespace geckoprofiler::markers

static bool IsThreadIdRegistered(ProfilerThreadId aThreadId) {
  ThreadRegistry::LockedRegistry lockedRegistry;
  const auto registryEnd = lockedRegistry.end();
  return std::find_if(
             lockedRegistry.begin(), registryEnd,
             [aThreadId](const ThreadRegistry::OffThreadRef& aOffThreadRef) {
               return aOffThreadRef.UnlockedConstReaderCRef()
                          .Info()
                          .ThreadId() == aThreadId;
             }) != registryEnd;
}

static nsAutoCString MakeThreadInfoMarkerName(base::ProcessId aThreadId,
                                              const nsACString& aName) {
  nsAutoCString markerName{"tid "};
  markerName.AppendInt(int64_t(aThreadId));
  if (!aName.IsEmpty()) {
    markerName.AppendLiteral(" ");
    markerName.Append(aName);
  }
  return markerName;
}

void SamplerThread::SpyOnUnregisteredThreads() {
  const TimeStamp unregisteredThreadSearchStart = TimeStamp::Now();

  const base::ProcessId currentProcessId =
      base::ProcessId(profiler_current_process_id().ToNumber());
  nsTArray<ProcInfoRequest> request(1);
  request.EmplaceBack(
      /* aPid = */ currentProcessId,
      /* aProcessType = */ ProcType::Unknown,
      /* aOrigin = */ ""_ns,
      /* aWindowInfo = */ nsTArray<WindowInfo>{},
      /* aUtilityInfo = */ nsTArray<UtilityInfo>{},
      /* aChild = */ 0
#ifdef XP_MACOSX
      ,
      /* aChildTask = */ MACH_PORT_NULL
#endif  // XP_DARWIN
  );

  const ProcInfoPromise::ResolveOrRejectValue procInfoOrError =
      GetProcInfoSync(std::move(request));

  if (!procInfoOrError.IsResolve()) {
    PROFILER_MARKER_TEXT("Failed unregistered thread search", PROFILER,
                         MarkerOptions(MarkerThreadId::MainThread(),
                                       MarkerTiming::IntervalUntilNowFrom(
                                           unregisteredThreadSearchStart)),
                         "Could not retrieve any process information");
    return;
  }

  const auto& procInfoHashMap = procInfoOrError.ResolveValue();
  // Expecting the requested (current) process information to be present in the
  // hashmap.
  const auto& procInfoPtr =
      procInfoHashMap.readonlyThreadsafeLookup(currentProcessId);
  if (!procInfoPtr) {
    PROFILER_MARKER_TEXT("Failed unregistered thread search", PROFILER,
                         MarkerOptions(MarkerThreadId::MainThread(),
                                       MarkerTiming::IntervalUntilNowFrom(
                                           unregisteredThreadSearchStart)),
                         "Could not retrieve information about this process");
    return;
  }

  // Record the time spent so far, which is OS-bound...
  PROFILER_MARKER_TEXT("Unregistered thread search", PROFILER,
                       MarkerOptions(MarkerThreadId::MainThread(),
                                     MarkerTiming::IntervalUntilNowFrom(
                                         unregisteredThreadSearchStart)),
                       "Work to discover threads");

  // ... and record the time needed to process the data, which we can control.
  AUTO_PROFILER_MARKER_TEXT(
      "Unregistered thread search", PROFILER,
      MarkerOptions(MarkerThreadId::MainThread()),
      "Work to process discovered threads and record unregistered ones"_ns);

  const Span<const mozilla::ThreadInfo> threads = procInfoPtr->value().threads;

  // mLastSpying timestamp should be null only at the beginning of a session,
  // when mSpiedThreads is still empty.
  MOZ_ASSERT_IF(mLastSpying.IsNull(), mSpiedThreads.IsEmpty());

  const TimeStamp previousSpying = std::exchange(mLastSpying, TimeStamp::Now());

  // Find threads that were spied on but are not present anymore.
  const auto threadsBegin = threads.begin();
  const auto threadsEnd = threads.end();
  for (size_t spiedThreadIndexPlus1 = mSpiedThreads.Length();
       spiedThreadIndexPlus1 != 0; --spiedThreadIndexPlus1) {
    const SpiedThread& spiedThread = mSpiedThreads[spiedThreadIndexPlus1 - 1];
    if (std::find_if(threadsBegin, threadsEnd,
                     [spiedTid = spiedThread.mThreadId](
                         const mozilla::ThreadInfo& aThreadInfo) {
                       return aThreadInfo.tid == spiedTid;
                     }) == threadsEnd) {
      // This spied thread is gone.
      PROFILER_MARKER(
          MakeThreadInfoMarkerName(spiedThread.mThreadId, spiedThread.mName),
          PROFILER,
          MarkerOptions(
              MarkerThreadId::MainThread(),
              // Place the end between this update and the previous one.
              MarkerTiming::IntervalEnd(previousSpying +
                                        (mLastSpying - previousSpying) /
                                            int64_t(2))),
          UnregisteredThreadLifetimeMarker, spiedThread.mThreadId,
          spiedThread.mName, "Thread disappeared");

      // Don't spy on it anymore, assuming it won't come back.
      mSpiedThreads.RemoveElementAt(spiedThreadIndexPlus1 - 1);
    }
  }

  for (const mozilla::ThreadInfo& threadInfo : threads) {
    // Index of this encountered thread in mSpiedThreads, or NoIndex.
    size_t spiedThreadIndex = mSpiedThreads.IndexOf(threadInfo.tid);
    if (IsThreadIdRegistered(ProfilerThreadId::FromNumber(threadInfo.tid))) {
      // This thread id is already officially registered.
      if (spiedThreadIndex != SpiedThreads::NoIndex) {
        // This now-registered thread was previously being spied.
        SpiedThread& spiedThread = mSpiedThreads[spiedThreadIndex];
        PROFILER_MARKER(
            MakeThreadInfoMarkerName(spiedThread.mThreadId, spiedThread.mName),
            PROFILER,
            MarkerOptions(
                MarkerThreadId::MainThread(),
                // Place the end between this update and the previous one.
                // TODO: Find the real time from the thread registration?
                MarkerTiming::IntervalEnd(previousSpying +
                                          (mLastSpying - previousSpying) /
                                              int64_t(2))),
            UnregisteredThreadLifetimeMarker, spiedThread.mThreadId,
            spiedThread.mName, "Thread registered itself");

        // Remove from mSpiedThreads, since it can be profiled normally.
        mSpiedThreads.RemoveElement(threadInfo.tid);
      }
    } else {
      // This thread id is not registered.
      if (spiedThreadIndex == SpiedThreads::NoIndex) {
        // This unregistered thread has not been spied yet, store it now.
        NS_ConvertUTF16toUTF8 name(threadInfo.name);
        mSpiedThreads.EmplaceBack(threadInfo.tid, name, threadInfo.cpuTime);

        PROFILER_MARKER(
            MakeThreadInfoMarkerName(threadInfo.tid, name), PROFILER,
            MarkerOptions(
                MarkerThreadId::MainThread(),
                // Place the start between this update and the previous one (or
                // the start of this search if it's the first one).
                MarkerTiming::IntervalStart(
                    mLastSpying -
                    (mLastSpying - (previousSpying.IsNull()
                                        ? unregisteredThreadSearchStart
                                        : previousSpying)) /
                        int64_t(2))),
            UnregisteredThreadLifetimeMarker, threadInfo.tid, name,
            /* aEndEvent */ "");
      } else {
        // This unregistered thread was already being spied, record its work.
        SpiedThread& spiedThread = mSpiedThreads[spiedThreadIndex];
        int64_t diffCPUTimeNs =
            int64_t(threadInfo.cpuTime) - int64_t(spiedThread.mCPUTimeNs);
        spiedThread.mCPUTimeNs = threadInfo.cpuTime;
        if (diffCPUTimeNs != 0) {
          PROFILER_MARKER(
              MakeThreadInfoMarkerName(threadInfo.tid, spiedThread.mName),
              PROFILER,
              MarkerOptions(
                  MarkerThreadId::MainThread(),
                  MarkerTiming::Interval(previousSpying, mLastSpying)),
              UnregisteredThreadCPUMarker, threadInfo.tid, diffCPUTimeNs,
              previousSpying, mLastSpying);
        }
      }
    }
  }

  PROFILER_MARKER_TEXT("Unregistered thread search", PROFILER,
                       MarkerOptions(MarkerThreadId::MainThread(),
                                     MarkerTiming::IntervalUntilNowFrom(
                                         unregisteredThreadSearchStart)),
                       "Work to discover and record unregistered threads");
}

// We #include these files directly because it means those files can use
// declarations from this file trivially.  These provide target-specific
// implementations of all SamplerThread methods except Run().
#if defined(GP_OS_windows)
#  include "platform-win32.cpp"
#elif defined(GP_OS_darwin)
#  include "platform-macos.cpp"
#elif defined(GP_OS_linux) || defined(GP_OS_android) || defined(GP_OS_freebsd)
#  include "platform-linux-android.cpp"
#else
#  include "platform-noop.cpp"
#endif

// END SamplerThread
////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////
// BEGIN externally visible functions

MOZ_DEFINE_MALLOC_SIZE_OF(GeckoProfilerMallocSizeOf)

NS_IMETHODIMP GeckoProfilerReporter::CollectReports(
    nsIHandleReportCallback* aHandleReport, nsISupports* aData,
    bool aAnonymize) {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());

  size_t profSize = 0;
  size_t lulSize = 0;

  {
    PSAutoLock lock;

    if (CorePS::Exists()) {
      CorePS::AddSizeOf(lock, GeckoProfilerMallocSizeOf, profSize, lulSize);
    }

    if (ActivePS::Exists(lock)) {
      profSize += ActivePS::SizeOf(lock, GeckoProfilerMallocSizeOf);
    }
  }

  MOZ_COLLECT_REPORT(
      "explicit/profiler/profiler-state", KIND_HEAP, UNITS_BYTES, profSize,
      "Memory used by the Gecko Profiler's global state (excluding memory used "
      "by LUL).");

#if defined(USE_LUL_STACKWALK)
  MOZ_COLLECT_REPORT(
      "explicit/profiler/lul", KIND_HEAP, UNITS_BYTES, lulSize,
      "Memory used by LUL, a stack unwinder used by the Gecko Profiler.");
#endif

  return NS_OK;
}

NS_IMPL_ISUPPORTS(GeckoProfilerReporter, nsIMemoryReporter)

static uint32_t ParseFeature(const char* aFeature, bool aIsStartup) {
  if (strcmp(aFeature, "default") == 0) {
    return (aIsStartup ? (DefaultFeatures() | StartupExtraDefaultFeatures())
                       : DefaultFeatures()) &
           AvailableFeatures();
  }

#define PARSE_FEATURE_BIT(n_, str_, Name_, desc_) \
  if (strcmp(aFeature, str_) == 0) {              \
    return ProfilerFeature::Name_;                \
  }

  PROFILER_FOR_EACH_FEATURE(PARSE_FEATURE_BIT)

#undef PARSE_FEATURE_BIT

  printf("\nUnrecognized feature \"%s\".\n\n", aFeature);
  // Since we may have an old feature we don't implement anymore, don't exit.
  PrintUsage();
  return 0;
}

uint32_t ParseFeaturesFromStringArray(const char** aFeatures,
                                      uint32_t aFeatureCount,
                                      bool aIsStartup /* = false */) {
  uint32_t features = 0;
  for (size_t i = 0; i < aFeatureCount; i++) {
    features |= ParseFeature(aFeatures[i], aIsStartup);
  }
  return features;
}

static ProfilingStack* locked_register_thread(
    PSLockRef aLock, ThreadRegistry::OffThreadRef aOffThreadRef) {
  MOZ_RELEASE_ASSERT(CorePS::Exists());

  VTUNE_REGISTER_THREAD(aOffThreadRef.UnlockedConstReaderCRef().Info().Name());

  if (ActivePS::Exists(aLock)) {
    ThreadProfilingFeatures threadProfilingFeatures =
        ActivePS::ProfilingFeaturesForThread(
            aLock, aOffThreadRef.UnlockedConstReaderCRef().Info());
    if (threadProfilingFeatures != ThreadProfilingFeatures::NotProfiled) {
      ThreadRegistry::OffThreadRef::RWFromAnyThreadWithLock
          lockedRWFromAnyThread = aOffThreadRef.GetLockedRWFromAnyThread();

      ProfiledThreadData* profiledThreadData = ActivePS::AddLiveProfiledThread(
          aLock, MakeUnique<ProfiledThreadData>(
                     aOffThreadRef.UnlockedConstReaderCRef().Info()));
      lockedRWFromAnyThread->SetProfilingFeaturesAndData(
          threadProfilingFeatures, profiledThreadData, aLock);

      if (ActivePS::FeatureJS(aLock)) {
        lockedRWFromAnyThread->StartJSSampling(ActivePS::JSFlags(aLock));
        if (lockedRWFromAnyThread->GetJSContext()) {
          profiledThreadData->NotifyReceivedJSContext(
              ActivePS::Buffer(aLock).BufferRangeEnd());
          if (ActivePS::FeatureTracing(aLock)) {
            CycleCollectedJSContext* ctx =
                lockedRWFromAnyThread->GetCycleCollectedJSContext();
            ctx->BeginExecutionTracingAsync();
          }
        }
      }
    }
  }

  return &aOffThreadRef.UnlockedConstReaderAndAtomicRWRef().ProfilingStackRef();
}

static void NotifyObservers(const char* aTopic,
                            nsISupports* aSubject = nullptr) {
  if (!NS_IsMainThread()) {
    // Dispatch a task to the main thread that notifies observers.
    // If NotifyObservers is called both on and off the main thread within a
    // short time, the order of the notifications can be different from the
    // order of the calls to NotifyObservers.
    // Getting the order 100% right isn't that important at the moment, because
    // these notifications are only observed in the parent process, where the
    // profiler_* functions are currently only called on the main thread.
    nsCOMPtr<nsISupports> subject = aSubject;
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "NotifyObservers", [=] { NotifyObservers(aTopic, subject); }));
    return;
  }

  // Notify C++ observers through the ObserverService
  if (nsCOMPtr<nsIObserverService> os = services::GetObserverService()) {
    os->NotifyObservers(aSubject, aTopic, nullptr);
  }

#if defined(GP_OS_android)
  // In the parent process, notify the GeckoJavaSampler when the profiler is
  // started / stopped.
  if (XRE_IsParentProcess()) {
    if (strcmp(aTopic, "profiler-started") == 0) {
      java::GeckoJavaSampler::NotifyProfilerStateChanged(true);
    } else if (strcmp(aTopic, "profiler-stopped") == 0) {
      java::GeckoJavaSampler::NotifyProfilerStateChanged(false);
    }
  }
#endif
}

[[nodiscard]] static RefPtr<GenericPromise> NotifyProfilerStarted(
    const PowerOfTwo32& aCapacity, const Maybe<double>& aDuration,
    double aInterval, uint32_t aFeatures, const char** aFilters,
    uint32_t aFilterCount, uint64_t aActiveTabID) {
  nsTArray<nsCString> filtersArray;
  for (size_t i = 0; i < aFilterCount; ++i) {
    filtersArray.AppendElement(aFilters[i]);
  }

  nsCOMPtr<nsIProfilerStartParams> params = new nsProfilerStartParams(
      aCapacity.Value(), aDuration, aInterval, aFeatures,
      std::move(filtersArray), aActiveTabID);

  RefPtr<GenericPromise> startPromise = ProfilerParent::ProfilerStarted(params);
  NotifyObservers("profiler-started", params);
  return startPromise;
}

static void locked_profiler_start(PSLockRef aLock, PowerOfTwo32 aCapacity,
                                  double aInterval, uint32_t aFeatures,
                                  const char** aFilters, uint32_t aFilterCount,
                                  uint64_t aActiveTabID,
                                  const Maybe<double>& aDuration);

static Vector<const char*> SplitAtCommas(const char* aString,
                                         UniquePtr<char[]>& aStorage) {
  size_t len = strlen(aString);
  aStorage = MakeUnique<char[]>(len + 1);
  PodCopy(aStorage.get(), aString, len + 1);

  // Iterate over all characters in aStorage and split at commas, by
  // overwriting commas with the null char.
  Vector<const char*> array;
  size_t currentElementStart = 0;
  for (size_t i = 0; i <= len; i++) {
    if (aStorage[i] == ',') {
      aStorage[i] = '\0';
    }
    if (aStorage[i] == '\0') {
      // Only add non-empty elements, otherwise ParseFeatures would later
      // complain about unrecognized features.
      if (currentElementStart != i) {
        MOZ_RELEASE_ASSERT(array.append(&aStorage[currentElementStart]));
      }
      currentElementStart = i + 1;
    }
  }
  return array;
}

void profiler_init_threadmanager() {
  LOG("profiler_init_threadmanager");

  ThreadRegistration::WithOnThreadRef(
      [](ThreadRegistration::OnThreadRef aOnThreadRef) {
        aOnThreadRef.WithLockedRWOnThread(
            [](ThreadRegistration::LockedRWOnThread& aThreadData) {
              if (!aThreadData.GetEventTarget()) {
                aThreadData.ResetMainThread(NS_GetCurrentThreadNoCreate());
              }
            });
      });
}

static const char* get_size_suffix(const char* str) {
  const char* ptr = str;

  while (isdigit(*ptr)) {
    ptr++;
  }

  return ptr;
}

#if defined(GECKO_PROFILER_ASYNC_POSIX_SIGNAL_CONTROL)
static void profiler_start_signal_handler(int signal, siginfo_t* info,
                                          void* context) {
  // Starting the profiler from a signal handler is a risky business: Both of
  // the main tasks that we would like to accomplish (allocating memory, and
  // starting a thread) are illegal within a UNIX signal handler. Conversely,
  // we cannot dispatch to the main thread, as this may be "stuck" (why else
  // would we be using a signal handler to start the profiler?).
  // Instead, we have a background thread running that watches a pipe for a
  // given "control" character. In this handler, we can simply write to that
  // pipe to get the background thread to start the profiler for us!
  // Note that `write` is async-signal safe (see signal-safety(7)):
  // https://www.man7.org/linux/man-pages/man7/signal-safety.7.html
  // This means that it's safe for us to call within a signal handler.
  if (sAsyncSignalControlWriteFd != -1) {
    char signalControlCharacter = sAsyncSignalControlCharStart;
    [[maybe_unused]] ssize_t _ =
        write(sAsyncSignalControlWriteFd, &signalControlCharacter,
              sizeof(signalControlCharacter));
  }
}

static void profiler_stop_signal_handler(int signal, siginfo_t* info,
                                         void* context) {
  // Signal handlers are limited in what functions they can call, for more
  // details see: https://man7.org/linux/man-pages/man7/signal-safety.7.html
  // As we have a background thread already running for checking whether or
  // not we want to start the profiler, we can re-use the same machinery to
  // stop the profiler. We use the same mechanism of writing to a pipe/file
  // descriptor, but with a different control character. Note that `write` is
  // signal safe.
  if (sAsyncSignalControlWriteFd != -1) {
    char signalControlCharacter = sAsyncSignalControlCharStop;
    [[maybe_unused]] ssize_t _ =
        write(sAsyncSignalControlWriteFd, &signalControlCharacter,
              sizeof(signalControlCharacter));
  }
}
#endif

// This may fail if we have previously had an issue finding the download
// directory, or if the directory has moved since we cached the path.
// This is non-ideal, but captured by Bug 1885000
Maybe<nsAutoCString> profiler_find_dump_path() {
// Note, this is currently a posix-only implementation, as we currently have
// issues with fetching the download directory on Windows. See Bug 1890154.
#if defined(XP_WIN)
  return Nothing();
#else
  Maybe<nsCOMPtr<nsIFile>> directory = Nothing();
  nsAutoCString path;

  {
    // Acquire the lock so that we can get things from CorePS
    PSAutoLock lock;
    Maybe<nsCOMPtr<nsIFile>> downloadDir = Nothing();
    downloadDir = CorePS::AsyncSignalDumpDirectory(lock);

    // This needs to be done within the context of the lock, as otherwise
    // another thread might modify CorePS::mAsyncSignalDumpDirectory while we're
    // cloning the pointer.
    if (downloadDir) {
      nsCOMPtr<nsIFile> d;
      downloadDir.value()->Clone(getter_AddRefs(d));
      directory = Some(d);
    } else {
      return Nothing();
    }
  }

  // Now, we can check to see if we have a directory, and use it to construct
  // the output file
  if (directory) {
    // Set up the name of our profile file
    path.AppendPrintf("profile_%i_%i.json", XRE_GetProcessType(), getpid());

    // Append it to the directory we found
    nsresult rv = directory.value()->AppendNative(path);
    if (NS_FAILED(rv)) {
      LOG("Failed to append path to profile file");
      return Nothing();
    }

    // Write the result *back* to the original path
    rv = directory.value()->GetNativePath(path);
    if (NS_FAILED(rv)) {
      LOG("Failed to get native path for temp path");
      return Nothing();
    }

    return Some(path);
  }

  return Nothing();
#endif
}

void profiler_start_from_signal() {
  // Do nothing unless we're the parent process, as we're sandboxed and can't
  // write any data that we gather anyway.
  if (XRE_IsParentProcess()) {
    // Start the profiler here directly, as we're on a background thread.
    // We use a default set of preferences, configuring them is TODO, see
    // Bug 1913297.
    uint32_t features = ProfilerFeature::JS | ProfilerFeature::StackWalk;
    // as we often don't know what threads we'll care about, tell the
    // profiler to profile all threads.
    const char* filters[] = {"*"};
    if (MOZ_UNLIKELY(NS_IsMainThread())) {
      // We are on the main thread here, so `NotifyProfilerStarted` will
      // start the profiler in content/child processes.
      profiler_start(PROFILER_DEFAULT_SIGHANDLE_ENTRIES,
                     PROFILER_DEFAULT_INTERVAL, features, filters,
                     std::size(filters), 0);
    } else {
      // Directly start the profiler on this thread. We know we're not the main
      // thread here, so this will not start the profiler in child processes,
      // but we want to make sure that we do it here in case the main thread is
      // stuck.
      profiler_start(PROFILER_DEFAULT_SIGHANDLE_ENTRIES,
                     PROFILER_DEFAULT_INTERVAL, features, filters,
                     std::size(filters), 0);
      // Now also try and start the profiler from the main thread, so that the
      // ParentProfiler will start child threads.
      NS_DispatchToMainThread(
          NS_NewRunnableFunction("StartProfilerInChildProcesses", [=] {
            (void)NotifyProfilerStarted(
                PROFILER_DEFAULT_SIGHANDLE_ENTRIES, Nothing(),
                PROFILER_DEFAULT_INTERVAL, features,
                const_cast<const char**>(filters), std::size(filters), 0);
          }));
    }
  }
}

void profiler_dump_and_stop() {
  // Do nothing unless we're the parent process, as we're sandboxed and can't
  // open a file handle anyway.
  if (!XRE_IsParentProcess()) {
    return;
  }

  // pause the profiler until we are done dumping
  profiler_pause();

  // Try to save the profile to a file
  auto path = profiler_find_dump_path();

  // Exit quickly if we can't find the path, while stopping the profiler
  if (!path) {
    LOG("Failed to find a valid dump path to write profile to disk");
    profiler_stop();
    return;
  }

  // Dump the profile of this process first, in case the multi-process
  // gathering is unsuccessful (e.g. due to a blocked main threaed).
  profiler_save_profile_to_file(path.value().get());

  // We are probably not the main thread, but check anyway, and dispatch
  // directly.
  if (NS_IsMainThread()) {
    nsCOMPtr<nsIProfiler> nsProfiler(
        do_GetService("@mozilla.org/tools/profiler;1"));
    nsProfiler->DumpProfileToFileAsyncNoJs(path.value(), 0)
        ->Then(
            GetMainThreadSerialEventTarget(), __func__,
            [](void_t ok) {
              LOG("Stopping profiler after dumping profile to disk");
              profiler_stop();
            },
            [](nsresult aRv) {
              LOG("Dumping to disk failed with error \"%s\", stopping "
                  "profiler.",
                  GetStaticErrorName(aRv));
              profiler_stop();
            });
  } else {
    // Dispatch a runnable, as nsProfiler classes are currently main-thread
    // only. We also stop the profiler within the runnable, as otherwise we
    // may find ourselves stopping the profiler before the runnable has
    // gathered all the profile data.
    NS_DispatchToMainThread(
        NS_NewRunnableFunction("WriteProfileDataToFile", [=] {
          nsCOMPtr<nsIProfiler> nsProfiler(
              do_GetService("@mozilla.org/tools/profiler;1"));
          nsProfiler->DumpProfileToFileAsyncNoJs(path.value(), 0)
              ->Then(
                  GetMainThreadSerialEventTarget(), __func__,
                  [](void_t ok) {
                    LOG("Stopping profiler after dumping profile to disk");
                    profiler_stop();
                  },
                  [](nsresult aRv) {
                    LOG("Dumping to disk failed with error \"%s\", stopping "
                        "profiler.",
                        GetStaticErrorName(aRv));
                    profiler_stop();
                  });
        }));
  }
}

#if defined(GECKO_PROFILER_ASYNC_POSIX_SIGNAL_CONTROL)
void profiler_init_signal_handlers() {
  // Set a handler to start the profiler
  struct sigaction prof_start_sa{};
  memset(&prof_start_sa, 0, sizeof(struct sigaction));
  prof_start_sa.sa_sigaction = profiler_start_signal_handler;
  prof_start_sa.sa_flags = SA_RESTART | SA_SIGINFO;
  sigemptyset(&prof_start_sa.sa_mask);
  DebugOnly<int> rstart = sigaction(SIGUSR1, &prof_start_sa, nullptr);
  MOZ_ASSERT(rstart == 0, "Failed to install Profiler SIGUSR1 handler");

  // Set a handler to stop the profiler
  struct sigaction prof_stop_sa{};
  memset(&prof_stop_sa, 0, sizeof(struct sigaction));
  prof_stop_sa.sa_sigaction = profiler_stop_signal_handler;
  prof_stop_sa.sa_flags = SA_RESTART | SA_SIGINFO;
  sigemptyset(&prof_stop_sa.sa_mask);
  DebugOnly<int> rstop = sigaction(SIGUSR2, &prof_stop_sa, nullptr);
  MOZ_ASSERT(rstop == 0, "Failed to install Profiler SIGUSR2 handler");
}
#endif

static void PollJSSamplingForCurrentThread() {
  // Don't call into the JS engine with a profiler lock held as this can
  // deadlock: the js::Enable* calls can block waiting on the JS helper threads,
  // which in turn need the profiler locks. Besides the global profiler mutex
  // asserted below, that includes the thread's own data lock -- so take that
  // lock only to flip the sampling state (TakeJSSamplingChange), then apply the
  // JS-engine change (ApplyJSSamplingChange) with no lock held.
  MOZ_ASSERT(!PSAutoLock::IsLockedOnCurrentThread());

  ThreadRegistration::LockedRWOnThread::JSSamplingChange change;
  ThreadRegistration::WithOnThreadRef(
      [&change](ThreadRegistration::OnThreadRef aOnThreadRef) {
        aOnThreadRef.WithLockedRWOnThread(
            [&change](ThreadRegistration::LockedRWOnThread& aThreadData) {
              change = aThreadData.TakeJSSamplingChange();
            });
      });
  ThreadRegistration::LockedRWOnThread::ApplyJSSamplingChange(change);
}

void profiler_init(void* aStackTop) {
  LOG("profiler_init");

  profiler_init_main_thread_id();

  VTUNE_INIT();
  ETW::Init();
  InitPerfetto();
#if defined(MOZ_REPLACE_MALLOC) && defined(MOZ_PROFILER_MEMORY)
  mozilla::profiler::memory_hooks_tls_init();
#endif

  MOZ_RELEASE_ASSERT(!CorePS::Exists());

  if (getenv("MOZ_PROFILER_HELP")) {
    PrintUsage();
    exit(0);
  }

  SharedLibraryInfo::Initialize();

  // We initialize here as well as in baseprofiler because
  // baseprofiler init doesn't happen in child processes.
  Flow::Init();

  uint32_t features = DefaultFeatures() & AvailableFeatures();

  UniquePtr<char[]> filterStorage;

  Vector<const char*> filters;
  MOZ_RELEASE_ASSERT(filters.append("GeckoMain"));
  MOZ_RELEASE_ASSERT(filters.append("Compositor"));
  MOZ_RELEASE_ASSERT(filters.append("Renderer"));
  MOZ_RELEASE_ASSERT(filters.append("DOM Worker"));

  PowerOfTwo32 capacity = PROFILER_DEFAULT_ENTRIES;
  Maybe<double> duration = Nothing();
  double interval = PROFILER_DEFAULT_INTERVAL;
  uint64_t activeTabID = PROFILER_DEFAULT_ACTIVE_TAB_ID;

  ThreadRegistration::RegisterThread(kMainThreadName, aStackTop);

  {
    PSAutoLock lock;

    // We've passed the possible failure point. Instantiate CorePS, which
    // indicates that the profiler has initialized successfully.
    CorePS::Create(lock);

    // Make sure threads already in the ThreadRegistry (like the main thread)
    // get registered in CorePS as well.
    {
      ThreadRegistry::LockedRegistry lockedRegistry;
      for (ThreadRegistry::OffThreadRef offThreadRef : lockedRegistry) {
        locked_register_thread(lock, offThreadRef);
      }
    }
    // Platform-specific initialization.
    PlatformInit(lock);

#if defined(GECKO_PROFILER_ASYNC_POSIX_SIGNAL_CONTROL)
    // Initialise the background thread to listen for signal handler
    // communication
    CorePS::SetAsyncSignalControlThread(new AsyncSignalControlThread);

    // Initialise the signal handlers needed to start/stop the profiler
    profiler_init_signal_handlers();
#endif

#if defined(GP_OS_android)
    if (jni::IsAvailable()) {
      GeckoJavaSampler::Init();
    }
#endif

    // (Linux-only) We could create CorePS::mLul and read unwind info into it
    // at this point. That would match the lifetime implied by destruction of
    // it in profiler_shutdown() just below. However, that gives a big delay on
    // startup, even if no profiling is actually to be done. So, instead, it is
    // created on demand at the first call to PlatformStart().

    const char* startupEnv = getenv("MOZ_PROFILER_STARTUP");
    if (!startupEnv || startupEnv[0] == '\0' ||
        ((startupEnv[0] == '0' || startupEnv[0] == 'N' ||
          startupEnv[0] == 'n') &&
         startupEnv[1] == '\0')) {
      return;
    }

    LOG("- MOZ_PROFILER_STARTUP is set");

    // Startup default capacity may be different.
    capacity = PROFILER_DEFAULT_STARTUP_ENTRIES;

    const char* startupCapacity = getenv("MOZ_PROFILER_STARTUP_ENTRIES");
    if (startupCapacity && startupCapacity[0] != '\0') {
      errno = 0;
      long capacityLong = strtol(startupCapacity, nullptr, 10);
      std::string_view sizeSuffix = get_size_suffix(startupCapacity);

      if (sizeSuffix == "KB") {
        capacityLong *= 1000 / scBytesPerEntry;
      } else if (sizeSuffix == "KiB") {
        capacityLong *= 1024 / scBytesPerEntry;
      } else if (sizeSuffix == "MB") {
        capacityLong *= (1000 * 1000) / scBytesPerEntry;
      } else if (sizeSuffix == "MiB") {
        capacityLong *= (1024 * 1024) / scBytesPerEntry;
      } else if (sizeSuffix == "GB") {
        capacityLong *= (1000 * 1000 * 1000) / scBytesPerEntry;
      } else if (sizeSuffix == "GiB") {
        capacityLong *= (1024 * 1024 * 1024) / scBytesPerEntry;
      } else if (!sizeSuffix.empty()) {
        LOG("- MOZ_PROFILER_STARTUP_ENTRIES unit must be one of the "
            "following: KB, KiB, MB, MiB, GB, GiB");
        PrintUsage();
        exit(1);
      }

      // `long` could be 32 or 64 bits, so we force a 64-bit comparison with
      // the maximum 32-bit signed number (as more than that is clamped down to
      // 2^31 anyway).
      if (errno == 0 && capacityLong > 0 &&
          static_cast<uint64_t>(capacityLong) <=
              static_cast<uint64_t>(INT32_MAX)) {
        capacity = PowerOfTwo32(
            ClampToAllowedEntries(static_cast<uint32_t>(capacityLong)));
        LOG("- MOZ_PROFILER_STARTUP_ENTRIES = %u", unsigned(capacity.Value()));
      } else {
        LOG("- MOZ_PROFILER_STARTUP_ENTRIES not a valid integer: %s",
            startupCapacity);
        PrintUsage();
        exit(1);
      }
    }

    const char* startupDuration = getenv("MOZ_PROFILER_STARTUP_DURATION");
    if (startupDuration && startupDuration[0] != '\0') {
      errno = 0;
      double durationVal = PR_strtod(startupDuration, nullptr);
      if (errno == 0 && durationVal >= 0.0) {
        if (durationVal > 0.0) {
          duration = Some(durationVal);
        }
        LOG("- MOZ_PROFILER_STARTUP_DURATION = %f", durationVal);
      } else {
        LOG("- MOZ_PROFILER_STARTUP_DURATION not a valid float: %s",
            startupDuration);
        PrintUsage();
        exit(1);
      }
    }

    const char* startupInterval = getenv("MOZ_PROFILER_STARTUP_INTERVAL");
    if (startupInterval && startupInterval[0] != '\0') {
      errno = 0;
      interval = PR_strtod(startupInterval, nullptr);
      if (errno == 0 && interval > 0.0 && interval <= PROFILER_MAX_INTERVAL) {
        LOG("- MOZ_PROFILER_STARTUP_INTERVAL = %f", interval);
      } else {
        LOG("- MOZ_PROFILER_STARTUP_INTERVAL not a valid float: %s",
            startupInterval);
        PrintUsage();
        exit(1);
      }
    }

    features |= StartupExtraDefaultFeatures() & AvailableFeatures();

    const char* startupFeaturesBitfield =
        getenv("MOZ_PROFILER_STARTUP_FEATURES_BITFIELD");
    if (startupFeaturesBitfield && startupFeaturesBitfield[0] != '\0') {
      errno = 0;
      features = strtol(startupFeaturesBitfield, nullptr, 10);
      if (errno == 0) {
        LOG("- MOZ_PROFILER_STARTUP_FEATURES_BITFIELD = %d", features);
      } else {
        LOG("- MOZ_PROFILER_STARTUP_FEATURES_BITFIELD not a valid integer: %s",
            startupFeaturesBitfield);
        PrintUsage();
        exit(1);
      }
    } else {
      const char* startupFeatures = getenv("MOZ_PROFILER_STARTUP_FEATURES");
      if (startupFeatures) {
        // Interpret startupFeatures as a list of feature strings, separated by
        // commas.
        UniquePtr<char[]> featureStringStorage;
        Vector<const char*> featureStringArray =
            SplitAtCommas(startupFeatures, featureStringStorage);
        features = ParseFeaturesFromStringArray(featureStringArray.begin(),
                                                featureStringArray.length(),
                                                /* aIsStartup */ true);
        LOG("- MOZ_PROFILER_STARTUP_FEATURES = %d", features);
      }
    }

    const char* startupFilters = getenv("MOZ_PROFILER_STARTUP_FILTERS");
    if (startupFilters && startupFilters[0] != '\0') {
      filters = SplitAtCommas(startupFilters, filterStorage);
      LOG("- MOZ_PROFILER_STARTUP_FILTERS = %s", startupFilters);

      if (mozilla::profiler::detail::FiltersExcludePid(filters)) {
        LOG(" -> This process is excluded and won't be profiled");
        return;
      }
    }

    const char* startupActiveTabID =
        getenv("MOZ_PROFILER_STARTUP_ACTIVE_TAB_ID");
    if (startupActiveTabID && startupActiveTabID[0] != '\0') {
      std::istringstream iss(startupActiveTabID);
      iss >> activeTabID;
      if (!iss.fail()) {
        LOG("- MOZ_PROFILER_STARTUP_ACTIVE_TAB_ID = %" PRIu64, activeTabID);
      } else {
        LOG("- MOZ_PROFILER_STARTUP_ACTIVE_TAB_ID not a valid "
            "uint64_t: %s",
            startupActiveTabID);
        PrintUsage();
        exit(1);
      }
    }

    locked_profiler_start(lock, capacity, interval, features, filters.begin(),
                          filters.length(), activeTabID, duration);
  }

  PollJSSamplingForCurrentThread();

  // The GeckoMain thread registration happened too early to record a marker,
  // so let's record it again now.
  profiler_mark_thread_awake();

#if defined(GP_OS_darwin)
  // Register thermal state observer callback on macOS (parent process only)
  if (XRE_IsParentProcess()) {
    profiler_register_thermal_state_observer();
  }
#endif

  invoke_profiler_state_change_callbacks(ProfilingState::Started);

  // We do this with gPSMutex unlocked. The comment in profiler_stop() explains
  // why.
  (void)NotifyProfilerStarted(capacity, duration, interval, features,
                              filters.begin(), filters.length(), 0);
}

static void locked_profiler_save_profile_to_file(
    PSLockRef aLock, const char* aFilename,
    const PreRecordedMetaInformation& aPreRecordedMetaInformation,
    bool aIsShuttingDown = false,
    RefPtr<ProgressLogger::SharedProgress> aProgress = nullptr);

static SamplerThread* locked_profiler_stop(PSLockRef aLock);

void profiler_shutdown(IsFastShutdown aIsFastShutdown) {
  LOG("profiler_shutdown");

  VTUNE_SHUTDOWN();
  ETW::Shutdown();

  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(CorePS::Exists());

  if (profiler_is_active()) {
    invoke_profiler_state_change_callbacks(ProfilingState::Stopping);
  }
  invoke_profiler_state_change_callbacks(ProfilingState::ShuttingDown);

  // We collect information here to be used below so it can be done outside of
  // the lock. We only need it if MOZ_PROFILER_SHUTDOWN is set and not empty.
  const char* filename = getenv("MOZ_PROFILER_SHUTDOWN");
  PreRecordedMetaInformation preRecordedMetaInformation = {};
  if (filename && filename[0] != '\0') {
    preRecordedMetaInformation = PreRecordMetaInformation(/* aShutdown */ true);
  }

  ProfilerParent::ProfilerWillStopIfStarted();

  // If the profiler is active we must get a handle to the SamplerThread before
  // ActivePS is destroyed, in order to delete it.
  SamplerThread* samplerThread = nullptr;
  {
    PSAutoLock lock;

    // Save the profile on shutdown if requested.
    if (ActivePS::Exists(lock)) {
      if (filename && filename[0] != '\0') {
        locked_profiler_save_profile_to_file(lock, filename,
                                             preRecordedMetaInformation,
                                             /* aIsShuttingDown */ true);
      }
      if (aIsFastShutdown == IsFastShutdown::Yes) {
        return;
      }

      samplerThread = locked_profiler_stop(lock);
    } else if (aIsFastShutdown == IsFastShutdown::Yes) {
      return;
    }

    CorePS::Destroy(lock);
  }

  PollJSSamplingForCurrentThread();

  // We do these operations with gPSMutex unlocked. The comments in
  // profiler_stop() explain why.
  if (samplerThread) {
    (void)ProfilerParent::ProfilerStopped();
    NotifyObservers("profiler-stopped");
    delete samplerThread;
  }

  // Clear custom marker schemas to avoid StringBuffer leaks at shutdown.
  // This is done after all profiling operations and notifications are complete.
  ClearCustomMarkerSchemas();

  // Reverse the registration done in profiler_init.
  ThreadRegistration::UnregisterThread();
}

static ProfilerResult<ProfileGenerationAdditionalInformation>
WriteProfileToJSONWriter(SpliceableChunkedJSONWriter& aWriter,
                         double aSinceTime, bool aIsShuttingDown,
                         ProfilerCodeAddressService* aService,
                         mozilla::ProgressLogger aProgressLogger) {
  LOG("WriteProfileToJSONWriter");

  MOZ_RELEASE_ASSERT(CorePS::Exists());

  aWriter.Start();
  auto rv = profiler_stream_json_for_this_process(
      aWriter, aSinceTime, aIsShuttingDown, aService,
      aProgressLogger.CreateSubLoggerFromTo(
          0_pc,
          "WriteProfileToJSONWriter: "
          "profiler_stream_json_for_this_process started",
          100_pc,
          "WriteProfileToJSONWriter: "
          "profiler_stream_json_for_this_process done"));

  if (rv.isErr()) {
    return rv;
  }

  // Don't include profiles from other processes because this is a
  // synchronous function.
  aWriter.StartArrayProperty("processes");
  aWriter.EndArray();

  aWriter.End();

  if (aWriter.Failed()) {
    return Err(ProfilerError::JsonGenerationFailed);
  }

  return rv;
}

void profiler_set_process_name(const nsACString& aProcessName,
                               const nsACString* aETLDplus1) {
  LOG("profiler_set_process_name(\"%s\", \"%s\")",
      PromiseFlatCString(aProcessName).get(),
      aETLDplus1 ? PromiseFlatCString(*aETLDplus1).get() : "<none>");
  PSAutoLock lock;
  CorePS::SetProcessName(lock, aProcessName);
  if (aETLDplus1) {
    CorePS::SetETLDplus1(lock, *aETLDplus1);
  }
}

UniquePtr<char[]> profiler_get_profile(double aSinceTime,
                                       bool aIsShuttingDown) {
  LOG("profiler_get_profile");

  UniquePtr<ProfilerCodeAddressService> service =
      profiler_code_address_service_for_presymbolication();

  FailureLatchSource failureLatch;
  SpliceableChunkedJSONWriter b{failureLatch};
  if (WriteProfileToJSONWriter(b, aSinceTime, aIsShuttingDown, service.get(),
                               ProgressLogger{})
          .isErr()) {
    return nullptr;
  }
  return b.ChunkedWriteFunc().CopyData();
}

[[nodiscard]] ProfilerResult<ProfileGenerationAdditionalInformation>
profiler_get_profile_json(
    SpliceableChunkedJSONWriter& aSpliceableChunkedJSONWriter,
    double aSinceTime, bool aIsShuttingDown,
    mozilla::ProgressLogger aProgressLogger) {
  LOG("profiler_get_profile_json");

  UniquePtr<ProfilerCodeAddressService> service =
      profiler_code_address_service_for_presymbolication();

  return WriteProfileToJSONWriter(
      aSpliceableChunkedJSONWriter, aSinceTime, aIsShuttingDown, service.get(),
      aProgressLogger.CreateSubLoggerFromTo(
          0.1_pc, "profiler_get_profile_json: WriteProfileToJSONWriter started",
          99.9_pc, "profiler_get_profile_json: WriteProfileToJSONWriter done"));
}

void profiler_get_start_params(int* aCapacity, Maybe<double>* aDuration,
                               double* aInterval, uint32_t* aFeatures,
                               Vector<const char*>* aFilters,
                               uint64_t* aActiveTabID) {
  MOZ_RELEASE_ASSERT(CorePS::Exists());

  if (NS_WARN_IF(!aCapacity) || NS_WARN_IF(!aDuration) ||
      NS_WARN_IF(!aInterval) || NS_WARN_IF(!aFeatures) ||
      NS_WARN_IF(!aFilters)) {
    return;
  }

  PSAutoLock lock;

  if (!ActivePS::Exists(lock)) {
    *aCapacity = 0;
    *aDuration = Nothing();
    *aInterval = 0;
    *aFeatures = 0;
    *aActiveTabID = 0;
    aFilters->clear();
    return;
  }

  *aCapacity = ActivePS::Capacity(lock).Value();
  *aDuration = ActivePS::Duration(lock);
  *aInterval = ActivePS::Interval(lock);
  *aFeatures = ActivePS::Features(lock);
  *aActiveTabID = ActivePS::ActiveTabID(lock);

  const Vector<std::string>& filters = ActivePS::Filters(lock);
  MOZ_ALWAYS_TRUE(aFilters->resize(filters.length()));
  for (uint32_t i = 0; i < filters.length(); ++i) {
    (*aFilters)[i] = filters[i].c_str();
  }
}

ProfileBufferControlledChunkManager* profiler_get_controlled_chunk_manager() {
  MOZ_RELEASE_ASSERT(CorePS::Exists());
  PSAutoLock lock;
  if (NS_WARN_IF(!ActivePS::Exists(lock))) {
    return nullptr;
  }
  return &ActivePS::ControlledChunkManager(lock);
}

namespace mozilla {

void GetProfilerEnvVarsForChildProcess(
    std::function<void(const char* key, const char* value)>&& aSetEnv) {
  MOZ_RELEASE_ASSERT(CorePS::Exists());

  PSAutoLock lock;

  if (!ActivePS::Exists(lock)) {
    aSetEnv("MOZ_PROFILER_STARTUP", "");
    return;
  }

  aSetEnv("MOZ_PROFILER_STARTUP", "1");

  // If MOZ_PROFILER_SHUTDOWN is defined, make sure it's empty in children, so
  // that they don't attempt to write over that file.
  if (getenv("MOZ_PROFILER_SHUTDOWN")) {
    aSetEnv("MOZ_PROFILER_SHUTDOWN", "");
  }

  // Hidden option to stop Base Profiler, mostly due to Talos intermittents,
  // see https://bugzilla.mozilla.org/show_bug.cgi?id=1638851#c3
  // TODO: Investigate root cause and remove this in bugs 1648324 and 1648325.
  if (getenv("MOZ_PROFILER_STARTUP_NO_BASE")) {
    aSetEnv("MOZ_PROFILER_STARTUP_NO_BASE", "1");
  }

  auto capacityString =
      Smprintf("%u", unsigned(ActivePS::Capacity(lock).Value()));
  aSetEnv("MOZ_PROFILER_STARTUP_ENTRIES", capacityString.get());

  // Use AppendFloat instead of Smprintf with %f because the decimal
  // separator used by %f is locale-dependent. But the string we produce needs
  // to be parseable by strtod, which only accepts the period character as a
  // decimal separator. AppendFloat always uses the period character.
  nsCString intervalString;
  intervalString.AppendFloat(ActivePS::Interval(lock));
  aSetEnv("MOZ_PROFILER_STARTUP_INTERVAL", intervalString.get());

  auto featuresString = Smprintf("%d", ActivePS::Features(lock));
  aSetEnv("MOZ_PROFILER_STARTUP_FEATURES_BITFIELD", featuresString.get());

  std::string filtersString;
  const Vector<std::string>& filters = ActivePS::Filters(lock);
  for (uint32_t i = 0; i < filters.length(); ++i) {
    if (i != 0) {
      filtersString += ",";
    }
    filtersString += filters[i];
  }
  aSetEnv("MOZ_PROFILER_STARTUP_FILTERS", filtersString.c_str());

  auto activeTabIDString = Smprintf("%" PRIu64, ActivePS::ActiveTabID(lock));
  aSetEnv("MOZ_PROFILER_STARTUP_ACTIVE_TAB_ID", activeTabIDString.get());
}

}  // namespace mozilla

void profiler_received_exit_profile(
    ProfileAndAdditionalInformation&& aExitProfileAndAdditionalInfo) {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(CorePS::Exists());
  PSAutoLock lock;
  if (!ActivePS::Exists(lock)) {
    return;
  }
  ActivePS::AddExitProfile(lock, std::move(aExitProfileAndAdditionalInfo));
}

Vector<ProfileAndAdditionalInformation> profiler_move_exit_profiles() {
  MOZ_RELEASE_ASSERT(CorePS::Exists());
  PSAutoLock lock;
  Vector<ProfileAndAdditionalInformation> profiles;
  if (ActivePS::Exists(lock)) {
    profiles = ActivePS::MoveExitProfiles(lock);
  }
  return profiles;
}

static void locked_profiler_save_profile_to_file(
    PSLockRef aLock, const char* aFilename,
    const PreRecordedMetaInformation& aPreRecordedMetaInformation,
    bool aIsShuttingDown, RefPtr<ProgressLogger::SharedProgress> aProgress) {
  nsAutoCString processedFilename(aFilename);
  const auto processInsertionIndex = processedFilename.Find("%p");
  if (processInsertionIndex != kNotFound) {
    // Replace "%p" with the process id.
    nsAutoCString process;
    process.AppendInt(profiler_current_process_id().ToNumber());
    processedFilename.Replace(processInsertionIndex, 2, process);
    LOG("locked_profiler_save_profile_to_file(\"%s\" -> \"%s\")", aFilename,
        processedFilename.get());
  } else {
    LOG("locked_profiler_save_profile_to_file(\"%s\")", aFilename);
  }

  MOZ_RELEASE_ASSERT(CorePS::Exists() && ActivePS::Exists(aLock));

  std::ofstream stream;
  stream.open(processedFilename.get());
  if (stream.is_open()) {
    OStreamJSONWriteFunc sw(stream);
    SpliceableJSONWriter w(sw, FailureLatchInfallibleSource::Singleton());
    w.Start();
    {
      (void)locked_profiler_stream_json_for_this_process(
          aLock, w, /* sinceTime */ 0, aPreRecordedMetaInformation,
          aIsShuttingDown, nullptr, ProgressLogger{std::move(aProgress)});

      w.StartArrayProperty("processes");
      Vector<ProfileAndAdditionalInformation> exitProfiles =
          ActivePS::MoveExitProfiles(aLock);
      for (auto& exitProfile : exitProfiles) {
        if (!exitProfile.mProfile.IsEmpty() && exitProfile.mProfile[0] != '*') {
          w.Splice(exitProfile.mProfile);
        }
      }
      w.EndArray();
    }
    w.End();

    stream.close();
  }
}

void profiler_save_profile_to_file(const char* aFilename) {
  LOG("profiler_save_profile_to_file(%s)", aFilename);
  profiler_save_profile_to_file_with_progress(aFilename, nullptr);
}

static void profiler_save_profile_to_file_with_progress(
    const char* aFilename, RefPtr<ProgressLogger::SharedProgress> aProgress) {
  MOZ_RELEASE_ASSERT(CorePS::Exists());

  const auto preRecordedMetaInformation = PreRecordMetaInformation();

  PSAutoLock lock;

  if (!ActivePS::Exists(lock)) {
    return;
  }

  locked_profiler_save_profile_to_file(
      lock, aFilename, preRecordedMetaInformation,
      /* aIsShuttingDown */ false, std::move(aProgress));
}

void profiler_schedule_dump_to_file(double aDelaySeconds, const char* aFilename,
                                    bool aExitAfterDump) {
  if (!aFilename || !CorePS::Exists()) {
    return;
  }

  // Compute the deadline before grabbing the lock, which may already be held
  // by another thread for a while.
  TimeStamp deadline =
      TimeStamp::Now() + TimeDuration::FromSeconds(aDelaySeconds);

  PSAutoLock lock;
  CorePS::ScheduleDumpToFile(lock, deadline, nsDependentCString(aFilename),
                             aExitAfterDump);
}

void profiler_cancel_scheduled_dump() {
  if (!CorePS::Exists()) {
    return;
  }

  PSAutoLock lock;
  CorePS::CancelScheduledDump(lock);
}

void profiler_wait_for_scheduled_dump() {
  // No-op outside automation. A scheduled dump is only ever armed by test
  // harnesses, and this can block indefinitely (until the dump finishes or, on
  // the exit-after path, until the process exits), so it must never delay a
  // fatal-abort path in a shipped browser; in automation the harness's own
  // timeout is the ultimate backstop.
  if (!xpc::IsInAutomation()) {
    return;
  }
  mozilla::StaticMutexAutoLock lock(sScheduledDumpMutex);
}

void profiler_request_dump_and_quit_for_test(const nsACString& aReason) {
  if (!profiler_is_active()) {
    return;
  }

  nsCString reason(aReason);
  auto notify = [reason] {
    MOZ_RELEASE_ASSERT(NS_IsMainThread());
    if (nsCOMPtr<nsIObserverService> os = services::GetObserverService()) {
      os->NotifyObservers(nullptr, "profiler-dump-and-quit",
                          NS_ConvertUTF8toUTF16(reason).get());
    }
  };

  if (NS_IsMainThread()) {
    notify();
    return;
  }

  // The notification, the profile gathering it triggers, and the process exit
  // all have to happen on the main thread, so dispatch there and block this
  // thread until the harness has handled it. When handled, the harness ends the
  // process, so this normally does not return.
  //
  // There is a slight risk of deadlock here: if the main thread is blocked (for
  // example waiting on this thread, or wedged), it will never run the
  // dispatched runnable and we will block forever. We accept this because
  // gathering a multi-process profile fundamentally requires the main thread's
  // event loop; in that case the test harness timeout will eventually kill the
  // process.
  nsCOMPtr<nsIRunnable> runnable = NS_NewRunnableFunction(
      "profiler_request_dump_and_quit_for_test", std::move(notify));
  SyncRunnable::DispatchToThread(GetMainThreadSerialEventTarget(), runnable);
}

uint32_t profiler_get_available_features() {
  MOZ_RELEASE_ASSERT(CorePS::Exists());
  return AvailableFeatures();
}

Maybe<ProfilerBufferInfo> profiler_get_buffer_info() {
  MOZ_RELEASE_ASSERT(CorePS::Exists());

  PSAutoLock lock;

  if (!ActivePS::Exists(lock)) {
    return Nothing();
  }

  return Some(ActivePS::Buffer(lock).GetProfilerBufferInfo());
}

// When the profiler is started on a background thread, we can't synchronously
// call PollJSSampling on the main thread's ThreadInfo. And the next regular
// call to PollJSSampling on the main thread would only happen once the main
// thread triggers a JS interrupt callback.
// This means that all the JS execution between profiler_start() and the first
// JS interrupt would happen with JS sampling disabled, and we wouldn't get any
// JS function information for that period of time.
// So in order to start JS sampling as soon as possible, we dispatch a runnable
// to the main thread which manually calls PollJSSamplingForCurrentThread().
// In some cases this runnable will lose the race with the next JS interrupt.
// That's fine; PollJSSamplingForCurrentThread() is immune to redundant calls.
static void TriggerPollJSSamplingOnMainThread() {
  nsCOMPtr<nsIThread> mainThread;
  nsresult rv = NS_GetMainThread(getter_AddRefs(mainThread));
  if (NS_SUCCEEDED(rv) && mainThread) {
    nsCOMPtr<nsIRunnable> task =
        NS_NewRunnableFunction("TriggerPollJSSamplingOnMainThread",
                               []() { PollJSSamplingForCurrentThread(); });
    SchedulerGroup::Dispatch(task.forget());
  }
}

static void locked_profiler_start(PSLockRef aLock, PowerOfTwo32 aCapacity,
                                  double aInterval, uint32_t aFeatures,
                                  const char** aFilters, uint32_t aFilterCount,
                                  uint64_t aActiveTabID,
                                  const Maybe<double>& aDuration) {
  TimeStamp profilingStartTime = TimeStamp::Now();

  if (LOG_TEST) {
    LOG("locked_profiler_start");
    LOG("- capacity  = %u", unsigned(aCapacity.Value()));
    LOG("- duration  = %.2f", aDuration ? *aDuration : -1);
    LOG("- interval = %.2f", aInterval);
    LOG("- tab ID = %" PRIu64, aActiveTabID);

#define LOG_FEATURE(n_, str_, Name_, desc_)     \
  if (ProfilerFeature::Has##Name_(aFeatures)) { \
    LOG("- feature  = %s", str_);               \
  }

    PROFILER_FOR_EACH_FEATURE(LOG_FEATURE)

#undef LOG_FEATURE

    for (uint32_t i = 0; i < aFilterCount; i++) {
      LOG("- threads  = %s", aFilters[i]);
    }
  }

  MOZ_RELEASE_ASSERT(CorePS::Exists() && !ActivePS::Exists(aLock));

  // Do this before the Base Profiler is stopped, to keep the existing buffer
  // (if any) alive for our use.
  if (NS_IsMainThread()) {
    mozilla::base_profiler_markers_detail::EnsureBufferForMainThreadAddMarker();
  } else {
    NS_DispatchToMainThread(
        NS_NewRunnableFunction("EnsureBufferForMainThreadAddMarker",
                               &mozilla::base_profiler_markers_detail::
                                   EnsureBufferForMainThreadAddMarker));
  }

  UniquePtr<ProfileBufferChunkManagerWithLocalLimit> baseChunkManager;
  bool profilersHandOver = false;
  if (baseprofiler::profiler_is_active()) {
    // Note that we still hold the lock, so the sampler cannot run yet and
    // interact negatively with the still-active BaseProfiler sampler.
    // Assume that Base Profiler is active because of MOZ_PROFILER_STARTUP.

    // Take ownership of the chunk manager from the Base Profiler, to extend its
    // lifetime during the new Gecko Profiler session. Since we're using the
    // same core buffer, all the base profiler data remains.
    baseChunkManager = baseprofiler::detail::ExtractBaseProfilerChunkManager();

    if (baseChunkManager) {
      profilersHandOver = true;
      if (const TimeStamp baseProfilingStartTime =
              baseprofiler::detail::GetProfilingStartTime();
          !baseProfilingStartTime.IsNull()) {
        profilingStartTime = baseProfilingStartTime;
      }

      BASE_PROFILER_MARKER_TEXT(
          "Profilers handover", PROFILER, MarkerTiming::IntervalStart(),
          "Transition from Base to Gecko Profiler, some data may be missing");
    }

    // Now stop Base Profiler (BP), as further recording will be ignored anyway,
    // and so that it won't clash with Gecko Profiler (GP) sampling starting
    // after the lock is dropped.
    // On Linux this is especially important to do before creating the GP
    // sampler, because the BP sampler may send a signal (to stop threads to be
    // sampled), which the GP would intercept before its own initialization is
    // complete and ready to handle such signals.
    // Note that even though `profiler_stop()` doesn't immediately destroy and
    // join the sampler thread, it safely deactivates it in such a way that the
    // thread will soon exit without doing any actual work.
    // TODO: Allow non-sampling profiling to continue.
    // TODO: Re-start BP after GP shutdown, to capture post-XPCOM shutdown.
    baseprofiler::profiler_stop();
  }

#if defined(GP_PLAT_amd64_windows) || defined(GP_PLAT_arm64_windows)
  mozilla::WindowsStackWalkInitialization();
#endif

  // Fall back to the default values if the passed-in values are unreasonable.
  // We want to be able to store at least one full stack.
  PowerOfTwo32 capacity =
      (aCapacity.Value() >=
       ProfileBufferChunkManager::scExpectedMaximumStackSize / scBytesPerEntry)
          ? aCapacity
          : PROFILER_DEFAULT_ENTRIES;
  Maybe<double> duration = aDuration;

  if (aDuration && *aDuration <= 0) {
    duration = Nothing();
  }

  double interval = aInterval > 0 ? aInterval : PROFILER_DEFAULT_INTERVAL;

  ActivePS::Create(aLock, profilingStartTime, capacity, interval, aFeatures,
                   aFilters, aFilterCount, aActiveTabID, duration,
                   std::move(baseChunkManager));

  // ActivePS::Create can only succeed or crash.
  MOZ_ASSERT(ActivePS::Exists(aLock));

  // Set up profiling for each registered thread, if appropriate.
#if defined(MOZ_REPLACE_MALLOC) && defined(MOZ_PROFILER_MEMORY)
  bool isMainThreadBeingProfiled = false;
#endif
  ThreadRegistry::LockedRegistry lockedRegistry;
  for (ThreadRegistry::OffThreadRef offThreadRef : lockedRegistry) {
    const ThreadRegistrationInfo& info =
        offThreadRef.UnlockedConstReaderCRef().Info();

    ThreadProfilingFeatures threadProfilingFeatures =
        ActivePS::ProfilingFeaturesForThread(aLock, info);
    if (threadProfilingFeatures != ThreadProfilingFeatures::NotProfiled) {
      ThreadRegistry::OffThreadRef::RWFromAnyThreadWithLock lockedThreadData =
          offThreadRef.GetLockedRWFromAnyThread();
      ProfiledThreadData* profiledThreadData = ActivePS::AddLiveProfiledThread(
          aLock, MakeUnique<ProfiledThreadData>(info));
      lockedThreadData->SetProfilingFeaturesAndData(threadProfilingFeatures,
                                                    profiledThreadData, aLock);
      lockedThreadData->GetNewCpuTimeInNs();
      if (ActivePS::FeatureJS(aLock)) {
        lockedThreadData->StartJSSampling(ActivePS::JSFlags(aLock));
        if (!lockedThreadData.GetLockedRWOnThread() && info.IsMainThread()) {
          // Dispatch a runnable to the main thread to call
          // PollJSSampling(), so that we don't have wait for the next JS
          // interrupt callback in order to start profiling JS.
          TriggerPollJSSamplingOnMainThread();
        }
      }
#if defined(MOZ_REPLACE_MALLOC) && defined(MOZ_PROFILER_MEMORY)
      if (info.IsMainThread()) {
        isMainThreadBeingProfiled = true;
      }
#endif
      lockedThreadData->ReinitializeOnResume();
      if (ActivePS::FeatureJS(aLock) && lockedThreadData->GetJSContext()) {
        profiledThreadData->NotifyReceivedJSContext(0);
        if (ActivePS::FeatureTracing(aLock)) {
          CycleCollectedJSContext* ctx =
              lockedThreadData->GetCycleCollectedJSContext();
          ctx->BeginExecutionTracingAsync();
        }
      }
    }
  }

#if defined(GP_OS_android)
  if (ActivePS::FeatureJava(aLock)) {
    int javaInterval = interval;
    // Java sampling doesn't accurately keep up with the sampling rate that is
    // lower than 1ms.
    if (javaInterval < 1) {
      javaInterval = 1;
    }

    JNIEnv* env = jni::GetEnvForThread();
    const auto& filters = ActivePS::Filters(aLock);
    jni::ObjectArray::LocalRef javaFilters =
        jni::ObjectArray::New<jni::String>(filters.length());
    for (size_t i = 0; i < filters.length(); i++) {
      javaFilters->SetElement(i, jni::StringParam(filters[i].data(), env));
    }

    // Send the interval-relative entry count, but we have 100000 hard cap in
    // the java code, it can't be more than that.
    java::GeckoJavaSampler::Start(
        javaFilters, javaInterval,
        std::round((double)(capacity.Value()) * interval /
                   (double)(javaInterval)));
  }
#endif

#if defined(MOZ_MEMORY) && defined(MOZ_PROFILER_MEMORY)
  if (ActivePS::FeatureMemory(aLock)) {
    auto counter = mozilla::profiler::create_memory_counter();
    locked_profiler_add_sampled_counter(aLock, counter.get());
    ActivePS::SetMemoryCounter(std::move(counter), aLock);
#  ifdef MOZJEMALLOC_PROFILING_CALLBACKS
    register_profiler_memory_callbacks();
#  endif
  }
#endif

#if defined(MOZ_REPLACE_MALLOC) && defined(MOZ_PROFILER_MEMORY)
  if (ActivePS::FeatureNativeAllocations(aLock)) {
    if (isMainThreadBeingProfiled) {
      mozilla::profiler::enable_native_allocations();
    } else {
      NS_WARNING(
          "The nativeallocations feature is turned on, but the main thread is "
          "not being profiled. The allocations are only stored on the main "
          "thread.");
    }
  }
#endif

  if (ProfilerFeature::HasAudioCallbackTracing(aFeatures)) {
    StartAudioCallbackTracing();
  }

  // At the very end, set up RacyFeatures.
  RacyFeatures::SetActive(ActivePS::Features(aLock));

  if (profilersHandOver) {
    PROFILER_MARKER_UNTYPED("Profilers handover", PROFILER,
                            MarkerTiming::IntervalEnd());
  }
}

RefPtr<GenericPromise> profiler_start(PowerOfTwo32 aCapacity, double aInterval,
                                      uint32_t aFeatures, const char** aFilters,
                                      uint32_t aFilterCount,
                                      uint64_t aActiveTabID,
                                      const Maybe<double>& aDuration) {
  LOG("profiler_start");

  ProfilerParent::ProfilerWillStopIfStarted();

  SamplerThread* samplerThread = nullptr;
  {
    PSAutoLock lock;

    // Initialize if necessary.
    if (!CorePS::Exists()) {
      profiler_init(nullptr);
    }

    // Reset the current state if the profiler is running.
    if (ActivePS::Exists(lock)) {
      // Note: Not invoking callbacks with ProfilingState::Stopping, because
      // we're under lock, and also it would not be useful: Any profiling data
      // will be discarded, and we're immediately restarting the profiler below
      // and then notifying ProfilingState::Started.
      samplerThread = locked_profiler_stop(lock);
    }

    locked_profiler_start(lock, aCapacity, aInterval, aFeatures, aFilters,
                          aFilterCount, aActiveTabID, aDuration);
  }

  PollJSSamplingForCurrentThread();

  invoke_profiler_state_change_callbacks(ProfilingState::Started);

  // We do these operations with gPSMutex unlocked. The comments in
  // profiler_stop() explain why.
  if (samplerThread) {
    (void)ProfilerParent::ProfilerStopped();
    NotifyObservers("profiler-stopped");
    delete samplerThread;
  }
  return NotifyProfilerStarted(aCapacity, aDuration, aInterval, aFeatures,
                               aFilters, aFilterCount, aActiveTabID);
}

void profiler_ensure_started(PowerOfTwo32 aCapacity, double aInterval,
                             uint32_t aFeatures, const char** aFilters,
                             uint32_t aFilterCount, uint64_t aActiveTabID,
                             const Maybe<double>& aDuration) {
  LOG("profiler_ensure_started");

  ProfilerParent::ProfilerWillStopIfStarted();

  bool startedProfiler = false;
  SamplerThread* samplerThread = nullptr;
  {
    PSAutoLock lock;

    // Initialize if necessary.
    if (!CorePS::Exists()) {
      profiler_init(nullptr);
    }

    if (ActivePS::Exists(lock)) {
      // The profiler is active.
      if (!ActivePS::Equals(lock, aCapacity, aDuration, aInterval, aFeatures,
                            aFilters, aFilterCount, aActiveTabID)) {
        // Stop and restart with different settings.
        // Note: Not invoking callbacks with ProfilingState::Stopping, because
        // we're under lock, and also it would not be useful: Any profiling data
        // will be discarded, and we're immediately restarting the profiler
        // below and then notifying ProfilingState::Started.
        samplerThread = locked_profiler_stop(lock);
        locked_profiler_start(lock, aCapacity, aInterval, aFeatures, aFilters,
                              aFilterCount, aActiveTabID, aDuration);
        startedProfiler = true;
      }
    } else {
      // The profiler is stopped.
      locked_profiler_start(lock, aCapacity, aInterval, aFeatures, aFilters,
                            aFilterCount, aActiveTabID, aDuration);
      startedProfiler = true;
    }
  }

  PollJSSamplingForCurrentThread();

  // We do these operations with gPSMutex unlocked. The comments in
  // profiler_stop() explain why.
  if (samplerThread) {
    (void)ProfilerParent::ProfilerStopped();
    NotifyObservers("profiler-stopped");
    delete samplerThread;
  }

  if (startedProfiler) {
    invoke_profiler_state_change_callbacks(ProfilingState::Started);

    (void)NotifyProfilerStarted(aCapacity, aDuration, aInterval, aFeatures,
                                aFilters, aFilterCount, aActiveTabID);
  }
}

[[nodiscard]] static SamplerThread* locked_profiler_stop(PSLockRef aLock) {
  LOG("locked_profiler_stop");

  MOZ_RELEASE_ASSERT(CorePS::Exists() && ActivePS::Exists(aLock));

  // At the very start, clear RacyFeatures.
  RacyFeatures::SetInactive();

  if (ActivePS::FeatureAudioCallbackTracing(aLock)) {
    StopAudioCallbackTracing();
  }

#if defined(GP_OS_android)
  if (ActivePS::FeatureJava(aLock)) {
    java::GeckoJavaSampler::Stop();
  }
#endif

  // Stop sampling live threads.
  ThreadRegistry::LockedRegistry lockedRegistry;
  for (ThreadRegistry::OffThreadRef offThreadRef : lockedRegistry) {
    if (offThreadRef.UnlockedRWForLockedProfilerRef().ProfilingFeatures() ==
        ThreadProfilingFeatures::NotProfiled) {
      continue;
    }

    ThreadRegistry::OffThreadRef::RWFromAnyThreadWithLock lockedThreadData =
        offThreadRef.GetLockedRWFromAnyThread();

    lockedThreadData->ClearProfilingFeaturesAndData(aLock);

    if (ActivePS::FeatureJS(aLock)) {
      if (ActivePS::FeatureTracing(aLock)) {
        CycleCollectedJSContext* ctx =
            lockedThreadData->GetCycleCollectedJSContext();
        if (ctx) {
          ctx->EndExecutionTracingAsync();
        }
      }

      lockedThreadData->StopJSSampling();
      if (!lockedThreadData.GetLockedRWOnThread() &&
          lockedThreadData->Info().IsMainThread()) {
        // Dispatch a runnable to the main thread to call PollJSSampling(),
        // so that we don't have wait for the next JS interrupt callback in
        // order to start profiling JS.
        TriggerPollJSSamplingOnMainThread();
      }
    }
  }

#if defined(MOZ_REPLACE_MALLOC) && defined(MOZ_PROFILER_MEMORY)
  if (ActivePS::FeatureNativeAllocations(aLock) &&
      ActivePS::ShouldInstallMemoryHooks(aLock)) {
    mozilla::profiler::disable_native_allocations();
  }
#endif

  // The Stop() call doesn't actually stop Run(); that happens in this
  // function's caller when the sampler thread is destroyed. Stop() just gives
  // the SamplerThread a chance to do some cleanup with gPSMutex locked.
  SamplerThread* samplerThread = ActivePS::Destroy(aLock);
  samplerThread->Stop(aLock);

  if (NS_IsMainThread()) {
    mozilla::base_profiler_markers_detail::
        ReleaseBufferForMainThreadAddMarker();
  } else {
    NS_DispatchToMainThread(
        NS_NewRunnableFunction("ReleaseBufferForMainThreadAddMarker",
                               &mozilla::base_profiler_markers_detail::
                                   ReleaseBufferForMainThreadAddMarker));
  }

  return samplerThread;
}

RefPtr<GenericPromise> profiler_stop() {
  LOG("profiler_stop");

  MOZ_RELEASE_ASSERT(CorePS::Exists());

  if (profiler_is_active()) {
    invoke_profiler_state_change_callbacks(ProfilingState::Stopping);
  }

  ProfilerParent::ProfilerWillStopIfStarted();

#if defined(MOZ_REPLACE_MALLOC) && defined(MOZ_PROFILER_MEMORY)
  // Remove the hooks early, as native allocations (if they are on) can be
  // quite expensive.
  mozilla::profiler::remove_memory_hooks();
#endif

  SamplerThread* samplerThread;
  {
    PSAutoLock lock;

    if (!ActivePS::Exists(lock)) {
      return GenericPromise::CreateAndResolve(/* unused */ true, __func__);
    }

    samplerThread = locked_profiler_stop(lock);
  }

  PollJSSamplingForCurrentThread();

  // We notify observers with gPSMutex unlocked. Otherwise we might get a
  // deadlock, if code run by these functions calls a profiler function that
  // locks gPSMutex, for example when it wants to insert a marker.
  // (This has been seen in practise in bug 1346356, when we were still firing
  // these notifications synchronously.)
  RefPtr<GenericPromise> promise = ProfilerParent::ProfilerStopped();
  NotifyObservers("profiler-stopped");

  // We delete with gPSMutex unlocked. Otherwise we would get a deadlock: we
  // would be waiting here with gPSMutex locked for SamplerThread::Run() to
  // return so the join operation within the destructor can complete, but Run()
  // needs to lock gPSMutex to return.
  //
  // Because this call occurs with gPSMutex unlocked, it -- including the final
  // iteration of Run()'s loop -- must be able detect deactivation and return
  // in a way that's safe with respect to other gPSMutex-locking operations
  // that may have occurred in the meantime.
  delete samplerThread;

  return promise;
}

bool profiler_is_paused() {
  MOZ_RELEASE_ASSERT(CorePS::Exists());

  PSAutoLock lock;

  if (!ActivePS::Exists(lock)) {
    return false;
  }

  return ActivePS::IsPaused(lock);
}

/* [[nodiscard]] */ bool profiler_callback_after_sampling(
    PostSamplingCallback&& aCallback) {
  LOG("profiler_callback_after_sampling");

  MOZ_RELEASE_ASSERT(CorePS::Exists());

  PSAutoLock lock;

  return ActivePS::AppendPostSamplingCallback(lock, std::move(aCallback));
}

// See `ProfilerControl.h` for more details.
void profiler_lookup_async_signal_dump_directory() {
// This implementation is causing issues on Windows (see Bug 1890154) but as it
// only exists to support the posix signal handling (on non-windows platforms)
// we can remove it for now.
#if !defined(XP_WIN)
  LOG("profiler_lookup_async_signal_dump_directory");

  MOZ_ASSERT(XRE_IsParentProcess() && NS_IsMainThread(),
             "We can only get access to the directory service from the parent "
             "process main thread");

  // Make sure the profiler is actually running~
  MOZ_RELEASE_ASSERT(CorePS::Exists());

  // take the lock so that we can write to CorePS
  PSAutoLock lock;
  nsresult rv;

  // Check to see if we have a `MOZ_UPLOAD_DIR` first - i.e., check to see if
  // we're running in CI.
  LOG("Checking if MOZ_UPLOAD_DIR exists");
  const char* mozUploadDir = getenv("MOZ_UPLOAD_DIR");
  if (mozUploadDir && mozUploadDir[0] != '\0') {
    LOG("Found MOZ_UPLOAD_DIR at: %s", mozUploadDir);
    // We want to do the right thing, and turn this into an nsIFile. Go through
    // the motions here:
    nsCOMPtr<nsIFile> mozUploadDirFile;
    rv = NS_NewNativeLocalFile(nsDependentCString(mozUploadDir),
                               getter_AddRefs(mozUploadDirFile));
    if (NS_FAILED(rv)) {
      LOG("Failed to assign a filepath while creating MOZ_UPLOAD_DIR file "
          "%s, Error %s ",
          mozUploadDir, GetStaticErrorName(rv));
      return;
    }

    CorePS::SetAsyncSignalDumpDirectory(lock, Some(mozUploadDirFile));
  } else {
#  if defined(GP_OS_android)
    // Bug 1904639: GetPreferredDownloadsDirectory is not implemented for
    // Android.
    return;
#  endif

    LOG("Defaulting to the user's Download directory for profile dumps");
    nsCOMPtr<nsIFile> tDownloadDir;
    nsresult rv;
    nsCOMPtr<nsIExternalHelperAppService> svc =
        do_GetService(NS_EXTERNALHELPERAPPSERVICE_CONTRACTID, &rv);

    if (NS_FAILED(rv)) {
      LOG("Failed to get nsIExternalHelperAppService for the download "
          "directory. Profiler signal handling will not be able to save to "
          "disk. Error: %s",
          GetStaticErrorName(rv));
      return;
    }

    rv = svc->GetPreferredDownloadsDirectory(getter_AddRefs(tDownloadDir));
    if (NS_FAILED(rv)) {
      LOG("Failed to find download directory. Profiler signal handling will "
          "not be able to save to disk. Error: %s",
          GetStaticErrorName(rv));
      return;
    }
    CorePS::SetAsyncSignalDumpDirectory(lock, Some(tDownloadDir));
  }
#endif
}

RefPtr<GenericPromise> profiler_pause() {
  LOG("profiler_pause");

  MOZ_RELEASE_ASSERT(CorePS::Exists());

  invoke_profiler_state_change_callbacks(ProfilingState::Pausing);

  {
    PSAutoLock lock;

    if (!ActivePS::Exists(lock)) {
      return GenericPromise::CreateAndResolve(/* unused */ true, __func__);
    }

#if defined(GP_OS_android)
    if (ActivePS::FeatureJava(lock) && !ActivePS::IsSamplingPaused(lock)) {
      // Not paused yet, so this is the first pause, let Java know.
      // TODO: Distinguish Pause and PauseSampling in Java.
      java::GeckoJavaSampler::PauseSampling();
    }
#endif

    RacyFeatures::SetPaused();
    ActivePS::SetIsPaused(lock, true);
    ActivePS::Buffer(lock).AddEntry(ProfileBufferEntry::Pause(profiler_time()));
  }

  // gPSMutex must be unlocked when we notify, to avoid potential deadlocks.
  RefPtr<GenericPromise> promise = ProfilerParent::ProfilerPaused();
  NotifyObservers("profiler-paused");
  return promise;
}

RefPtr<GenericPromise> profiler_resume() {
  LOG("profiler_resume");

  MOZ_RELEASE_ASSERT(CorePS::Exists());

  {
    PSAutoLock lock;

    if (!ActivePS::Exists(lock)) {
      return GenericPromise::CreateAndResolve(/* unused */ true, __func__);
    }

    ActivePS::Buffer(lock).AddEntry(
        ProfileBufferEntry::Resume(profiler_time()));
    ActivePS::SetIsPaused(lock, false);
    RacyFeatures::SetUnpaused();

#if defined(GP_OS_android)
    if (ActivePS::FeatureJava(lock) && !ActivePS::IsSamplingPaused(lock)) {
      // Not paused anymore, so this is the last unpause, let Java know.
      // TODO: Distinguish Unpause and UnpauseSampling in Java.
      java::GeckoJavaSampler::UnpauseSampling();
    }
#endif
  }

  // gPSMutex must be unlocked when we notify, to avoid potential deadlocks.
  RefPtr<GenericPromise> promise = ProfilerParent::ProfilerResumed();
  NotifyObservers("profiler-resumed");

  invoke_profiler_state_change_callbacks(ProfilingState::Resumed);

  return promise;
}

bool profiler_is_sampling_paused() {
  MOZ_RELEASE_ASSERT(CorePS::Exists());

  PSAutoLock lock;

  if (!ActivePS::Exists(lock)) {
    return false;
  }

  return ActivePS::IsSamplingPaused(lock);
}

RefPtr<GenericPromise> profiler_pause_sampling() {
  LOG("profiler_pause_sampling");

  MOZ_RELEASE_ASSERT(CorePS::Exists());

  {
    PSAutoLock lock;

    if (!ActivePS::Exists(lock)) {
      return GenericPromise::CreateAndResolve(/* unused */ true, __func__);
    }

#if defined(GP_OS_android)
    if (ActivePS::FeatureJava(lock) && !ActivePS::IsSamplingPaused(lock)) {
      // Not paused yet, so this is the first pause, let Java know.
      // TODO: Distinguish Pause and PauseSampling in Java.
      java::GeckoJavaSampler::PauseSampling();
    }
#endif

    RacyFeatures::SetSamplingPaused();
    ActivePS::SetIsSamplingPaused(lock, true);
    ActivePS::Buffer(lock).AddEntry(
        ProfileBufferEntry::PauseSampling(profiler_time()));
  }

  // gPSMutex must be unlocked when we notify, to avoid potential deadlocks.
  RefPtr<GenericPromise> promise = ProfilerParent::ProfilerPausedSampling();
  NotifyObservers("profiler-paused-sampling");
  return promise;
}

RefPtr<GenericPromise> profiler_resume_sampling() {
  LOG("profiler_resume_sampling");

  MOZ_RELEASE_ASSERT(CorePS::Exists());

  {
    PSAutoLock lock;

    if (!ActivePS::Exists(lock)) {
      return GenericPromise::CreateAndResolve(/* unused */ true, __func__);
    }

    ActivePS::Buffer(lock).AddEntry(
        ProfileBufferEntry::ResumeSampling(profiler_time()));
    ActivePS::SetIsSamplingPaused(lock, false);
    RacyFeatures::SetSamplingUnpaused();

#if defined(GP_OS_android)
    if (ActivePS::FeatureJava(lock) && !ActivePS::IsSamplingPaused(lock)) {
      // Not paused anymore, so this is the last unpause, let Java know.
      // TODO: Distinguish Unpause and UnpauseSampling in Java.
      java::GeckoJavaSampler::UnpauseSampling();
    }
#endif
  }

  // gPSMutex must be unlocked when we notify, to avoid potential deadlocks.
  RefPtr<GenericPromise> promise = ProfilerParent::ProfilerResumedSampling();
  NotifyObservers("profiler-resumed-sampling");
  return promise;
}

bool profiler_feature_active(uint32_t aFeature) {
  // This function runs both on and off the main thread.

  // This function is hot enough that we use RacyFeatures, not ActivePS.
  return RacyFeatures::IsActiveWithFeature(aFeature);
}

bool profiler_active_without_feature(uint32_t aFeature) {
  // This function runs both on and off the main thread.

  // This function is hot enough that we use RacyFeatures, not ActivePS.
  return RacyFeatures::IsActiveWithoutFeature(aFeature);
}

void profiler_write_active_configuration(JSONWriter& aWriter) {
  MOZ_RELEASE_ASSERT(CorePS::Exists());
  PSAutoLock lock;
  ActivePS::WriteActiveConfiguration(lock, aWriter);
}

void profiler_add_sampled_counter(BaseProfilerCount* aCounter) {
  DEBUG_LOG("profiler_add_sampled_counter(%s)", aCounter->mLabel);
  PSAutoLock lock;
  locked_profiler_add_sampled_counter(lock, aCounter);
}

void profiler_remove_sampled_counter(BaseProfilerCount* aCounter) {
  DEBUG_LOG("profiler_remove_sampled_counter(%s)", aCounter->mLabel);
  PSAutoLock lock;
  locked_profiler_remove_sampled_counter(lock, aCounter);
}

void profiler_count_bandwidth_bytes(int64_t aCount) {
  NS_ASSERTION(profiler_feature_active(ProfilerFeature::Bandwidth),
               "Should not call profiler_count_bandwidth_bytes when the "
               "Bandwidth feature is not set");

  ProfilerBandwidthCounter* counter = CorePS::GetBandwidthCounter();
  if (MOZ_UNLIKELY(!counter)) {
    counter = new ProfilerBandwidthCounter();
    CorePS::SetBandwidthCounter(counter);
  }

  counter->Add(aCount);
}

ProfilingStack* profiler_register_thread(const char* aName,
                                         void* aGuessStackTop) {
  DEBUG_LOG("profiler_register_thread(%s)", aName);

  // This will call `ThreadRegistry::Register()` (see below).
  return ThreadRegistration::RegisterThread(aName, aGuessStackTop);
}

/* static */
void ThreadRegistry::Register(ThreadRegistration::OnThreadRef aOnThreadRef) {
  // Set the thread name (except for the main thread, which is controlled
  // elsewhere, and influences the process name on some systems like Linux).
  if (!aOnThreadRef.UnlockedConstReaderCRef().Info().IsMainThread()) {
    // Make sure we have a nsThread wrapper for the current thread, and that
    // NSPR knows its name.
    (void)NS_GetCurrentThread();
    NS_SetCurrentThreadName(
        aOnThreadRef.UnlockedConstReaderCRef().Info().Name());
  }

  {
    PSAutoLock lock;

    {
      RegistryLockExclusive lock{sRegistryMutex};
      MOZ_RELEASE_ASSERT(sRegistryContainer.append(OffThreadRef{aOnThreadRef}));
    }

    if (!CorePS::Exists()) {
      // CorePS has not been created yet.
      // If&when that happens, it will handle already-registered threads then.
      return;
    }

    (void)locked_register_thread(lock, OffThreadRef{aOnThreadRef});
  }

  PollJSSamplingForCurrentThread();
}

void profiler_unregister_thread() {
  // This will call `ThreadRegistry::Unregister()` (see below).
  ThreadRegistration::UnregisterThread();
}

static void locked_unregister_thread(
    PSLockRef lock, ThreadRegistration::OnThreadRef aOnThreadRef) {
  if (!CorePS::Exists()) {
    // This function can be called after the main thread has already shut
    // down.
    return;
  }

  // We don't call StopJSSampling() here; there's no point doing that for a JS
  // thread that is in the process of disappearing.

  ThreadRegistration::OnThreadRef::RWOnThreadWithLock lockedThreadData =
      aOnThreadRef.GetLockedRWOnThread();

  ProfiledThreadData* profiledThreadData =
      lockedThreadData->GetProfiledThreadData(lock);
  lockedThreadData->ClearProfilingFeaturesAndData(lock);

  MOZ_RELEASE_ASSERT(
      lockedThreadData->Info().ThreadId() == profiler_current_thread_id(),
      "Thread being unregistered has changed its TID");

  DEBUG_LOG("profiler_unregister_thread: %s", lockedThreadData->Info().Name());

  if (profiledThreadData && ActivePS::Exists(lock)) {
    ActivePS::UnregisterThread(lock, profiledThreadData);
  }
}

/* static */
void ThreadRegistry::Unregister(ThreadRegistration::OnThreadRef aOnThreadRef) {
  PSAutoLock psLock;
  locked_unregister_thread(psLock, aOnThreadRef);

  RegistryLockExclusive lock{sRegistryMutex};
  for (OffThreadRef& thread : sRegistryContainer) {
    if (thread.IsPointingAt(*aOnThreadRef.mThreadRegistration)) {
      sRegistryContainer.erase(&thread);
      break;
    }
  }
}

void profiler_register_page(uint64_t aTabID, uint64_t aInnerWindowID,
                            const nsCString& aUrl,
                            uint64_t aEmbedderInnerWindowID,
                            bool aIsPrivateBrowsing) {
  DEBUG_LOG("profiler_register_page(%" PRIu64 ", %" PRIu64 ", %s, %" PRIu64
            ", %s)",
            aTabID, aInnerWindowID, aUrl.get(), aEmbedderInnerWindowID,
            aIsPrivateBrowsing ? "true" : "false");

  MOZ_RELEASE_ASSERT(CorePS::Exists());

  PSAutoLock lock;

  // When a Browsing context is first loaded, the first url loaded in it will be
  // about:blank. Because of that, this call keeps the first non-about:blank
  // registration of window and discards the previous one.
  CorePS::AppendRegisteredPage(lock, aTabID, aInnerWindowID, aUrl,
                               aEmbedderInnerWindowID, aIsPrivateBrowsing);

  // After appending the given page to CorePS, look for the expired
  // pages and remove them if there are any.
  if (ActivePS::Exists(lock)) {
    ActivePS::DiscardExpiredPages(lock);
  }
}

void profiler_unregister_page(uint64_t aRegisteredInnerWindowID) {
  PSAutoLock lock;

  if (!CorePS::Exists()) {
    // This function can be called after the main thread has already shut down.
    return;
  }

  // During unregistration, if the profiler is active, we have to keep the
  // page information since there may be some markers associated with the given
  // page. But if profiler is not active. we have no reason to keep the
  // page information here because there can't be any marker associated with it.
  if (ActivePS::Exists(lock)) {
    ActivePS::UnregisterPage(lock, aRegisteredInnerWindowID);
  } else {
    CorePS::RemoveRegisteredPage(lock, aRegisteredInnerWindowID);
  }
}

void profiler_clear_all_pages() {
  {
    PSAutoLock lock;

    if (!CorePS::Exists()) {
      // This function can be called after the main thread has already shut
      // down.
      return;
    }

    CorePS::ClearRegisteredPages(lock);
    if (ActivePS::Exists(lock)) {
      ActivePS::ClearUnregisteredPages(lock);
    }
  }

  // gPSMutex must be unlocked when we notify, to avoid potential deadlocks.
  ProfilerParent::ClearAllPages();
}

namespace geckoprofiler::markers::detail {

Maybe<uint64_t> profiler_get_inner_window_id_from_docshell(
    nsIDocShell* aDocshell) {
  Maybe<uint64_t> innerWindowID = Nothing();
  if (aDocshell) {
    auto outerWindow = aDocshell->GetWindow();
    if (outerWindow) {
      auto innerWindow = outerWindow->GetCurrentInnerWindow();
      if (innerWindow) {
        innerWindowID = Some(innerWindow->WindowID());
      }
    }
  }
  return innerWindowID;
}

}  // namespace geckoprofiler::markers::detail

namespace geckoprofiler::markers {

struct CPUAwakeMarker {
  static constexpr Span<const char> MarkerTypeName() {
    return MakeStringSpan("Awake");
  }
  static void StreamJSONMarkerData(baseprofiler::SpliceableJSONWriter& aWriter,
                                   int64_t aCPUTimeNs
#if !defined(GP_PLAT_unknown) && !defined(GP_PLAT_arm64_darwin)
                                   ,
                                   int64_t aCPUId
#endif
#ifdef GP_OS_darwin
                                   ,
                                   uint32_t aQoS
#endif
#ifdef GP_OS_windows
                                   ,
                                   int32_t aAbsolutePriority,
                                   int32_t aRelativePriority,
                                   int32_t aCurrentPriority
#endif
  ) {
    if (aCPUTimeNs) {
      constexpr double NS_PER_MS = 1'000'000;
      aWriter.DoubleProperty("CPU Time", double(aCPUTimeNs) / NS_PER_MS);
      // CPU Time is only provided for the end marker, the other fields are for
      // the start marker.
      return;
    }

#if !defined(GP_PLAT_unknown) && !defined(GP_PLAT_arm64_darwin)
    aWriter.IntProperty("CPU Id", aCPUId);
#endif
#ifdef GP_OS_windows
    if (aAbsolutePriority) {
      aWriter.IntProperty("absPriority", aAbsolutePriority);
    }
    if (aCurrentPriority) {
      aWriter.IntProperty("curPriority", aCurrentPriority);
    }
    aWriter.IntProperty("priority", aRelativePriority);
#endif
#ifdef GP_OS_darwin
    const char* QoS = "";
    switch (aQoS) {
      case QOS_CLASS_USER_INTERACTIVE:
        QoS = "User Interactive";
        break;
      case QOS_CLASS_USER_INITIATED:
        QoS = "User Initiated";
        break;
      case QOS_CLASS_DEFAULT:
        QoS = "Default";
        break;
      case QOS_CLASS_UTILITY:
        QoS = "Utility";
        break;
      case QOS_CLASS_BACKGROUND:
        QoS = "Background";
        break;
      default:
        QoS = "Unspecified";
    }

    aWriter.StringProperty("QoS",
                           ProfilerString8View::WrapNullTerminatedString(QoS));
#endif
  }

  static MarkerSchema MarkerTypeDisplay() {
    using MS = MarkerSchema;
    MS schema{MS::Location::MarkerChart, MS::Location::MarkerTable};
    schema.AddKeyFormat("CPU Time", MS::Format::Duration);
#if !defined(GP_PLAT_unknown) && !defined(GP_PLAT_arm64_darwin)
    schema.AddKeyFormat("CPU Id", MS::Format::Integer);
    schema.SetTableLabel("Awake - CPU Id = {marker.data.CPU Id}");
#endif
#ifdef GP_OS_windows
    schema.AddKeyLabelFormat("priority", "Relative Thread Priority",
                             MS::Format::Integer);
    schema.AddKeyLabelFormat("absPriority", "Base Thread Priority",
                             MS::Format::Integer);
    schema.AddKeyLabelFormat("curPriority", "Current Thread Priority",
                             MS::Format::Integer);
#endif
#ifdef GP_OS_darwin
    schema.AddKeyLabelFormat("QoS", "Quality of Service", MS::Format::String);
#endif
    return schema;
  }
};

}  // namespace geckoprofiler::markers

void profiler_mark_thread_asleep() {
  if (!profiler_thread_is_being_profiled_for_markers()) {
    return;
  }

  uint64_t cpuTimeNs = ThreadRegistration::WithOnThreadRefOr(
      [](ThreadRegistration::OnThreadRef aOnThreadRef) {
        return aOnThreadRef.UnlockedConstReaderAndAtomicRWRef()
            .GetNewCpuTimeInNs();
      },
      0);
  PROFILER_MARKER(
      "Awake", OTHER, MarkerTiming::IntervalEnd(), CPUAwakeMarker, cpuTimeNs
#if !defined(GP_PLAT_unknown) && !defined(GP_PLAT_arm64_darwin)
      ,
      0 /* cpuId */
#endif
#if defined(GP_OS_darwin)
      ,
      0 /* qos_class */
#endif
#if defined(GP_OS_windows)
      ,
      0 /* priority */, 0 /* thread priority */, 0 /* current priority */
#endif
  );
}

void profiler_thread_sleep() {
  profiler_mark_thread_asleep();
  ThreadRegistration::WithOnThreadRef(
      [](ThreadRegistration::OnThreadRef aOnThreadRef) {
        aOnThreadRef.UnlockedConstReaderAndAtomicRWRef().SetSleeping();
      });
}

#if defined(GP_OS_windows)
#  if !defined(__MINGW32__)
enum {
  ThreadBasicInformation,
};
#  endif

struct THREAD_BASIC_INFORMATION {
  NTSTATUS ExitStatus;
  PVOID TebBaseAddress;
  CLIENT_ID ClientId;
  KAFFINITY AffMask;
  DWORD Priority;
  DWORD BasePriority;
};
#endif

static mozilla::Atomic<uint64_t, mozilla::MemoryOrdering::Relaxed> gWakeCount(
    0);

namespace geckoprofiler::markers {
struct WakeUpCountMarker {
  static constexpr Span<const char> MarkerTypeName() {
    return MakeStringSpan("WakeUpCount");
  }
  static void StreamJSONMarkerData(baseprofiler::SpliceableJSONWriter& aWriter,
                                   int32_t aCount,
                                   const ProfilerString8View& aType) {
    aWriter.IntProperty("Count", aCount);
    aWriter.StringProperty("label", aType);
  }
  static MarkerSchema MarkerTypeDisplay() {
    using MS = MarkerSchema;
    MS schema{MS::Location::MarkerChart, MS::Location::MarkerTable};
    schema.AddKeyFormat("Count", MS::Format::Integer);
    schema.AddKeyFormat("label", MS::Format::String, MS::PayloadFlags::Hidden);
    schema.SetTooltipLabel("{marker.name} - {marker.data.label}");
    schema.SetTableLabel("{marker.data.label}: {marker.data.count}");
    return schema;
  }
};
}  // namespace geckoprofiler::markers

void profiler_record_wakeup_count(const nsACString& aProcessType) {
  static uint64_t previousThreadWakeCount = 0;

  uint64_t newWakeups = gWakeCount - previousThreadWakeCount;
  if (newWakeups > 0) {
    if (newWakeups < std::numeric_limits<int32_t>::max()) {
      int32_t newWakeups32 = int32_t(newWakeups);
      mozilla::glean::power::total_thread_wakeups.Add(newWakeups32);
      mozilla::glean::power::wakeups_per_process_type.Get(aProcessType)
          .Add(newWakeups32);
      PROFILER_MARKER("Thread Wake-ups", OTHER, {}, WakeUpCountMarker,
                      newWakeups32, aProcessType);
    }

    previousThreadWakeCount += newWakeups;
  }

#ifdef NIGHTLY_BUILD
  struct ThreadWakeData {
    nsCString mThreadName;
    uint64_t mCpuTimeMs;
    uint64_t mWakeCount;
  };
  // Collect the per-thread data under the ThreadRegistry lock, then report it
  // to Glean below once the lock has been released: recording it while holding
  // the lock would create a lock-order inversion with the Glean/Telemetry
  // locks.
  nsTArray<ThreadWakeData> threadWakeData;
  {
    ThreadRegistry::LockedRegistry lockedRegistry;
    for (ThreadRegistry::OffThreadRef offThreadRef : lockedRegistry) {
      const ThreadRegistry::UnlockedConstReaderAndAtomicRW& threadData =
          offThreadRef.UnlockedConstReaderAndAtomicRWRef();
      nsAutoCString threadName;
      uint64_t cpuTimeMs;
      uint64_t wakeCount;
      if (threadData.RecordWakeCount(threadName, cpuTimeMs, wakeCount)) {
        threadWakeData.AppendElement(
            ThreadWakeData{std::move(threadName), cpuTimeMs, wakeCount});
      }
    }
  }

  for (const ThreadWakeData& data : threadWakeData) {
    mozilla::glean::RecordThreadCpuUse(data.mThreadName, data.mCpuTimeMs,
                                       data.mWakeCount);
  }
#endif
}

void profiler_mark_thread_awake() {
  ++gWakeCount;
  if (!profiler_thread_is_being_profiled_for_markers()) {
    return;
  }

#if !defined(GP_PLAT_unknown) && !defined(GP_PLAT_arm64_darwin)
  int64_t cpuId = 0;
#  if defined(GP_OS_windows)
  cpuId = GetCurrentProcessorNumber();
#  elif defined(GP_OS_darwin)
#    ifdef GP_PLAT_amd64_darwin
  unsigned int eax, ebx, ecx, edx;
  __cpuid_count(1, 0, eax, ebx, ecx, edx);
  // Check if we have an APIC.
  if ((edx & (1 << 9))) {
    // APIC ID is bits 24-31 of EBX
    cpuId = ebx >> 24;
  }
#    endif
#  else
  cpuId = sched_getcpu();
#  endif
#endif

#if defined(GP_OS_windows)
  LONG priority;
  static const auto get_thread_information_fn =
      reinterpret_cast<decltype(&::GetThreadInformation)>(::GetProcAddress(
          ::GetModuleHandle(L"Kernel32.dll"), "GetThreadInformation"));

  if (!get_thread_information_fn ||
      !get_thread_information_fn(GetCurrentThread(), ThreadAbsoluteCpuPriority,
                                 &priority, sizeof(priority))) {
    priority = 0;
  }

  static const auto nt_query_information_thread_fn =
      reinterpret_cast<decltype(&::NtQueryInformationThread)>(::GetProcAddress(
          ::GetModuleHandle(L"ntdll.dll"), "NtQueryInformationThread"));

  LONG currentPriority = 0;
  if (nt_query_information_thread_fn) {
    THREAD_BASIC_INFORMATION threadInfo;
    auto status = (*nt_query_information_thread_fn)(
        GetCurrentThread(), (THREADINFOCLASS)ThreadBasicInformation,
        &threadInfo, sizeof(threadInfo), NULL);
    if (NT_SUCCESS(status)) {
      currentPriority = threadInfo.Priority;
    }
  }
#endif
  PROFILER_MARKER("Awake", OTHER, MarkerTiming::IntervalStart(), CPUAwakeMarker,
                  0 /* CPU time */
#if !defined(GP_PLAT_unknown) && !defined(GP_PLAT_arm64_darwin)
                  ,
                  cpuId
#endif
#if defined(GP_OS_darwin)
                  ,
                  qos_class_self()
#endif
#if defined(GP_OS_windows)
                      ,
                  priority, GetThreadPriority(GetCurrentThread()),
                  currentPriority
#endif
  );
}

void profiler_thread_wake() {
  profiler_mark_thread_awake();
  ThreadRegistration::WithOnThreadRef(
      [](ThreadRegistration::OnThreadRef aOnThreadRef) {
        aOnThreadRef.UnlockedConstReaderAndAtomicRWRef().SetAwake();
      });
}

void profiler_js_interrupt_callback() {
  // This function runs on JS threads being sampled.
  PollJSSamplingForCurrentThread();
}

double profiler_time() {
  MOZ_RELEASE_ASSERT(CorePS::Exists());

  TimeDuration delta = TimeStamp::Now() - CorePS::ProcessStartTime();
  return delta.ToMilliseconds();
}

bool profiler_capture_backtrace_into(ProfileChunkedBuffer& aChunkedBuffer,
                                     StackCaptureOptions aCaptureOptions) {
  MOZ_RELEASE_ASSERT(CorePS::Exists());

  if (!profiler_is_active() ||
      aCaptureOptions == StackCaptureOptions::NoStack ||
      profiler_is_locked_on_current_thread()) {
    return false;
  }

  return ThreadRegistration::WithOnThreadRefOr(
      [&](ThreadRegistration::OnThreadRef aOnThreadRef) {
        mozilla::Maybe<uint32_t> maybeFeatures =
            RacyFeatures::FeaturesIfActiveAndUnpaused();
        if (!maybeFeatures) {
          return false;
        }

        ProfileBuffer profileBuffer(aChunkedBuffer);

        Registers regs;
#if defined(HAVE_NATIVE_UNWIND)
        REGISTERS_SYNC_POPULATE(regs);
#else
        regs.Clear();
#endif

        DoSyncSample(*maybeFeatures,
                     aOnThreadRef.UnlockedReaderAndAtomicRWOnThreadCRef(),
                     TimeStamp::Now(), regs, profileBuffer, aCaptureOptions);

        return true;
      },
      // If this was called from a non-registered thread, return false and do no
      // more work. This can happen from a memory hook.
      false);
}

bool profiler_backtrace_into_buffer(ProfileChunkedBuffer& aChunkedBuffer,
                                    NativeStack& aNativeStack) {
  MOZ_RELEASE_ASSERT(CorePS::Exists());

  return ThreadRegistration::WithOnThreadRefOr(
      [&](ThreadRegistration::OnThreadRef aOnThreadRef) {
        mozilla::Maybe<uint32_t> maybeFeatures =
            RacyFeatures::FeaturesIfActiveAndUnpaused();
        if (!maybeFeatures) {
          return false;
        }

        ProfileBuffer profileBuffer(aChunkedBuffer);
        const uint64_t bufferRangeStart = profileBuffer.BufferRangeStart();
        const uint64_t samplePos = profileBuffer.AddThreadIdEntry(
            aOnThreadRef.UnlockedReaderAndAtomicRWOnThreadCRef()
                .Info()
                .ThreadId());

        TimeDuration delta = TimeStamp::Now() - CorePS::ProcessStartTime();
        profileBuffer.AddEntry(
            ProfileBufferEntry::Time(delta.ToMilliseconds()));

        ProfileBufferCollector collector(profileBuffer, samplePos,
                                         bufferRangeStart);

        for (int nativeIndex = (int)(aNativeStack.mCount) - 1; nativeIndex >= 0;
             --nativeIndex) {
          collector.CollectNativeLeafAddr(
              (void*)aNativeStack.mPCs[nativeIndex]);
        }

        return true;
      },
      // If this was called from a non-registered thread, return false and do no
      // more work. This can happen from a memory hook.
      false);
}

UniquePtr<ProfileChunkedBuffer> profiler_capture_backtrace() {
  MOZ_RELEASE_ASSERT(CorePS::Exists());
  AUTO_PROFILER_LABEL_HOT("profiler_capture_backtrace", PROFILER);

  // Quick is-active and feature check before allocating a buffer.
  // If NoMarkerStacks is set, we don't want to capture a backtrace.
  if (!profiler_active_without_feature(ProfilerFeature::NoMarkerStacks)) {
    return nullptr;
  }

  ProfileChunkedBuffer captureBuffer(
      ProfileChunkedBuffer::ThreadSafety::WithoutMutex,
      MakeUnique<ProfileBufferChunkManagerSingle>(
          ProfileBufferChunkManager::scExpectedMaximumStackSize));

  if (!profiler_capture_backtrace_into(captureBuffer,
                                       StackCaptureOptions::Full)) {
    return nullptr;
  }

  return mozilla::profiler::detail::CopyToRightSizedBuffer(captureBuffer);
}

UniqueProfilerBacktrace profiler_get_backtrace() {
  UniquePtr<ProfileChunkedBuffer> buffer = profiler_capture_backtrace();

  if (!buffer) {
    return nullptr;
  }

  return UniqueProfilerBacktrace(
      new ProfilerBacktrace("SyncProfile", std::move(buffer)));
}

void ProfilerBacktraceDestructor::operator()(ProfilerBacktrace* aBacktrace) {
  delete aBacktrace;
}

bool profiler_is_locked_on_current_thread() {
  // This function is used to help users avoid calling `profiler_...` functions
  // when the profiler may already have a lock in place, which would prevent a
  // 2nd recursive lock (resulting in a crash or a never-ending wait), or a
  // deadlock between any two mutexes. So we must return `true` for any of:
  // - The main profiler mutex, used by most functions, and/or
  // - The buffer mutex, used directly in some functions without locking the
  //   main mutex, e.g., marker-related functions.
  // - The ProfilerParent or ProfilerChild mutex, used to store and process
  //   buffer chunk updates.
  return PSAutoLock::IsLockedOnCurrentThread() ||
         ThreadRegistry::IsRegistryMutexLockedOnCurrentThread() ||
         ThreadRegistration::IsDataMutexLockedOnCurrentThread() ||
         profiler_get_core_buffer().IsThreadSafeAndLockedOnCurrentThread() ||
         ProfilerParent::IsLockedOnCurrentThread() ||
         ProfilerChild::IsLockedOnCurrentThread();
}

void profiler_set_js_context(CycleCollectedJSContext* aCx) {
  MOZ_ASSERT(aCx);
  ThreadRegistration::WithOnThreadRef(
      [&](ThreadRegistration::OnThreadRef aOnThreadRef) {
        // The profiler mutex must be locked before the ThreadRegistration's.
        PSAutoLock lock;
        aOnThreadRef.WithLockedRWOnThread(
            [&](ThreadRegistration::LockedRWOnThread& aThreadData) {
              aThreadData.SetCycleCollectedJSContext(aCx);

              if (!ActivePS::Exists(lock) || !ActivePS::FeatureJS(lock)) {
                return;
              }

              if (ProfiledThreadData* profiledThreadData =
                      aThreadData.GetProfiledThreadData(lock);
                  profiledThreadData) {
                profiledThreadData->NotifyReceivedJSContext(
                    ActivePS::Buffer(lock).BufferRangeEnd());
#ifdef MOZ_EXECUTION_TRACING
                if (ActivePS::FeatureTracing(lock)) {
                  JS_TracerBeginTracing(aCx->Context());
                }
#endif
              }
            });
      });

  // This call is on-thread, so we can call PollJSSampling() to start JS
  // sampling immediately.
  PollJSSamplingForCurrentThread();
}

void profiler_clear_js_context() {
  MOZ_RELEASE_ASSERT(CorePS::Exists());

  ThreadRegistration::WithOnThreadRef(
      [](ThreadRegistration::OnThreadRef aOnThreadRef) {
        CycleCollectedJSContext* cccx =
            aOnThreadRef.UnlockedReaderAndAtomicRWOnThreadCRef()
                .GetCycleCollectedJSContext();
        if (!cccx) {
          return;
        }

        JSContext* cx = cccx->Context();

        // The profiler mutex must be locked before the ThreadRegistration's.
        {
          PSAutoLock lock;
          ThreadRegistration::OnThreadRef::RWOnThreadWithLock lockedThreadData =
              aOnThreadRef.GetLockedRWOnThread();

          ProfiledThreadData* profiledThreadData =
              lockedThreadData->GetProfiledThreadData(lock);
          if (!(profiledThreadData && ActivePS::Exists(lock) &&
                ActivePS::FeatureJS(lock))) {
            // This thread is not being profiled or JS profiling is off, we only
            // need to clear the context pointer.
            lockedThreadData->ClearCycleCollectedJSContext();
            return;
          }

          profiledThreadData->NotifyAboutToLoseJSContext(
              cx, CorePS::ProcessStartTime(), ActivePS::Buffer(lock));

#ifdef MOZ_EXECUTION_TRACING
          if (ActivePS::FeatureTracing(lock)) {
            JS_TracerEndTracing(cx);
          }
#endif

          // Notify the JS context that profiling for this context has
          // stopped. Do this by calling StopJSSampling and PollJSSampling
          // before nulling out the JSContext.
          lockedThreadData->StopJSSampling();
        }

        // Drop profiler mutex for call into JS engine. This must happen before
        // ClearCycleCollectedJSContext below.
        PollJSSamplingForCurrentThread();

        {
          PSAutoLock lock;
          ThreadRegistration::OnThreadRef::RWOnThreadWithLock lockedThreadData =
              aOnThreadRef.GetLockedRWOnThread();

          lockedThreadData->ClearCycleCollectedJSContext();

          // Tell the thread that we'd like to have JS sampling on this
          // thread again, once it gets a new JSContext (if ever).
          lockedThreadData->StartJSSampling(ActivePS::JSFlags(lock));
        }
      });
}

static void profiler_suspend_and_sample_thread(
    const PSAutoLock* aLockIfAsynchronousSampling,
    const ThreadRegistration::UnlockedReaderAndAtomicRWOnThread& aThreadData,
    JsFrame* aJsFrames, uint32_t aFeatures, ProfilerStackCollector& aCollector,
    bool aSampleNative) {
  const ThreadRegistrationInfo& info = aThreadData.Info();

  if (info.IsMainThread()) {
    aCollector.SetIsMainThread();
  }

  // Allocate the space for the native stack
  NativeStack nativeStack{.mCount = 0};

  auto collectStack = [&](const Registers& aRegs, const TimeStamp& aNow) {
    // The target thread is now suspended. Collect a native backtrace,
    // and call the callback.
    StackWalkControl* stackWalkControlIfSupported = nullptr;
#if defined(HAVE_FASTINIT_NATIVE_UNWIND)
    StackWalkControl stackWalkControl;
    if constexpr (StackWalkControl::scIsSupported) {
      if (aSampleNative) {
        stackWalkControlIfSupported = &stackWalkControl;
      }
    }
#endif
    const uint32_t jsFramesCount =
        aJsFrames ? ExtractJsFrames(!aLockIfAsynchronousSampling, aThreadData,
                                    aRegs, aCollector, aJsFrames,
                                    stackWalkControlIfSupported)
                  : 0;

#if defined(HAVE_FASTINIT_NATIVE_UNWIND)
    if (aSampleNative) {
      // We can only use FramePointerStackWalk or MozStackWalk from
      // suspend_and_sample_thread as other stackwalking methods may not be
      // initialized.
#  if defined(USE_FRAME_POINTER_STACK_WALK)
      DoFramePointerBacktrace(aThreadData, aRegs, nativeStack,
                              stackWalkControlIfSupported);
#  elif defined(USE_MOZ_STACK_WALK)
      DoMozStackWalkBacktrace(aThreadData, aRegs, nativeStack,
                              stackWalkControlIfSupported);
#  else
#    error "Invalid configuration"
#  endif

      MergeStacks(!aLockIfAsynchronousSampling, aThreadData, nativeStack,
                  aCollector, aJsFrames, jsFramesCount);
    } else
#endif
    {
      MergeStacks(!aLockIfAsynchronousSampling, aThreadData, nativeStack,
                  aCollector, aJsFrames, jsFramesCount);

      aCollector.CollectNativeLeafAddr((void*)aRegs.mPC);
    }
  };

  if (!aLockIfAsynchronousSampling) {
    // Sampling the current thread, do NOT suspend it!
    Registers regs;
#if defined(HAVE_NATIVE_UNWIND)
    REGISTERS_SYNC_POPULATE(regs);
#else
    regs.Clear();
#endif
    collectStack(regs, TimeStamp::Now());
  } else {
    // Suspend, sample, and then resume the target thread.
    Sampler sampler(*aLockIfAsynchronousSampling);
    TimeStamp now = TimeStamp::Now();
    sampler.SuspendAndSampleAndResumeThread(*aLockIfAsynchronousSampling,
                                            aThreadData, now, collectStack);

    // NOTE: Make sure to disable the sampler before it is destroyed, in
    // case the profiler is running at the same time.
    sampler.Disable(*aLockIfAsynchronousSampling);
  }
}

// NOTE: aCollector's methods will be called while the target thread is paused.
// Doing things in those methods like allocating -- which may try to claim
// locks -- is a surefire way to deadlock.
void profiler_suspend_and_sample_thread(ProfilerThreadId aThreadId,
                                        uint32_t aFeatures,
                                        ProfilerStackCollector& aCollector,
                                        bool aSampleNative /* = true */) {
  if (!aThreadId.IsSpecified() || aThreadId == profiler_current_thread_id()) {
    // Sampling the current thread. Get its information from the TLS (no locking
    // required.)
    ThreadRegistration::WithOnThreadRef(
        [&](ThreadRegistration::OnThreadRef aOnThreadRef) {
          aOnThreadRef.WithUnlockedReaderAndAtomicRWOnThread(
              [&](const ThreadRegistration::UnlockedReaderAndAtomicRWOnThread&
                      aThreadData) {
                if (!aThreadData.GetJSContext()) {
                  // No JSContext, there is no JS frame buffer (and no need for
                  // it).
                  profiler_suspend_and_sample_thread(
                      /* aLockIfAsynchronousSampling = */ nullptr, aThreadData,
                      /* aJsFrames = */ nullptr, aFeatures, aCollector,
                      aSampleNative);
                } else {
                  // JSContext is present, we need to lock the thread data to
                  // access the JS frame buffer.
                  aOnThreadRef.WithConstLockedRWOnThread(
                      [&](const ThreadRegistration::LockedRWOnThread&
                              aLockedThreadData) {
                        profiler_suspend_and_sample_thread(
                            /* aLockIfAsynchronousSampling = */ nullptr,
                            aThreadData, aLockedThreadData.GetJsFrameBuffer(),
                            aFeatures, aCollector, aSampleNative);
                      });
                }
              });
        });
  } else {
    // Lock the profiler before accessing the ThreadRegistry.
    PSAutoLock lock;
    ThreadRegistry::WithOffThreadRef(
        aThreadId, [&](ThreadRegistry::OffThreadRef aOffThreadRef) {
          aOffThreadRef.WithLockedRWFromAnyThread(
              [&](const ThreadRegistration::UnlockedReaderAndAtomicRWOnThread&
                      aThreadData) {
                JsFrameBuffer& jsFrames = CorePS::JsFrames(lock);
                profiler_suspend_and_sample_thread(&lock, aThreadData, jsFrames,
                                                   aFeatures, aCollector,
                                                   aSampleNative);
              });
        });
  }
}

// END externally visible functions
////////////////////////////////////////////////////////////////////////
