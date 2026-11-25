/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DecoderDoctorLogger_h_
#define DecoderDoctorLogger_h_

#include "DDLogCategory.h"
#include "DDLogValue.h"
#include "DDLoggedTypeTraits.h"
#include "mozilla/Atomics.h"
#include "mozilla/MozPromise.h"
#include "mozilla/NonDereferenceable.h"
#include "nsString.h"

namespace mozilla {

// Main class used to capture log messages from the media stack, and to
// retrieve processed messages associated with an HTMLMediaElement.
//
// The logging APIs are designed to work as fast as possible (in most cases
// only checking a couple of atomic variables, and not allocating memory), so
// as not to introduce perceptible latency.
// Consider using DDLOG...() macros, and IsDDLoggingEnabled(), to avoid any
// unneeded work when logging is not enabled.
//
// Structural logging messages are used to determine when objects are created
// and destroyed, and to link objects that depend on each other, ultimately
// tying groups of objects and their messages to HTMLMediaElement objects.
//
// A separate thread processes log messages, and can asynchronously retrieve
// processed messages that correspond to a given HTMLMediaElement.
// That thread is also responsible for removing dated messages, so as not to
// take too much memory.
class DecoderDoctorLogger {
 public:
  // Called by nsLayoutStatics::Initialize() before any other media work.
  // Pre-enables logging if MOZ_LOG requires DDLogger.
  static void Init();

  // Is logging currently enabled? This is tested anyway in all public `Log...`
  // functions, but it may be used to prevent logging-only work in clients.
  static inline bool IsDDLoggingEnabled() {
    return MOZ_UNLIKELY(sLogState.load() == scEnabled);
  }

  // Shutdown logging. This will prevent more messages to be queued, but the
  // already-queued messages may still get processed.
  static void ShutdownLogging() { sLogState = scShutdown; }

  // Something went horribly wrong, stop all logging and log processing.
  static void Panic(const char* aReason) {
    PanicInternal(aReason, /* aDontBlock */ false);
  }

  // Logging functions.
  //
  // All logging functions take:
  // - The object that produces the message, either as a template type (for
  //   which a specialized DDLoggedTypeTraits exists), or a pointer and a type
  //   name (needed for inner classes that cannot specialize
  //   DDLoggedTypeTraits.)
  // - A DDLogCategory defining the type of log message; some are used
  //   internally for capture the lifetime and linking of C++ objects, others
  //   are used to split messages into different domains.
  // - A label (string literal).
  // - An optional Variant value, see DDLogValue for the accepted types.
  //
  // The following `EagerLog...` functions always cause their arguments to be
  // pre-evaluated even if logging is disabled, in which case runtime could be
  // wasted. Consider using `DDLOG...` macros instead, or test
  // `IsDDLoggingEnabled()` first.

  template <typename Value>
  static void EagerLogValue(const char* aSubjectTypeName,
                            const void* aSubjectPointer,
                            DDLogCategory aCategory, const char* aLabel,
                            Value&& aValue) {
    Log(aSubjectTypeName, aSubjectPointer, aCategory, aLabel,
        DDLogValue{std::forward<Value>(aValue)});
  }

  template <typename Subject, typename Value>
  static void EagerLogValue(const Subject* aSubject, DDLogCategory aCategory,
                            const char* aLabel, Value&& aValue) {
    EagerLogValue(DDLoggedTypeTraits<Subject>::Name(), aSubject, aCategory,
                  aLabel, std::forward<Value>(aValue));
  }

  // EagerLogValue that can explicitly take strings, as the templated function
  // above confuses Variant when forwarding string literals.
  static void EagerLogValue(const char* aSubjectTypeName,
                            const void* aSubjectPointer,
                            DDLogCategory aCategory, const char* aLabel,
                            const char* aValue) {
    Log(aSubjectTypeName, aSubjectPointer, aCategory, aLabel,
        DDLogValue{aValue});
  }

  template <typename Subject>
  static void EagerLogValue(const Subject* aSubject, DDLogCategory aCategory,
                            const char* aLabel, const char* aValue) {
    EagerLogValue(DDLoggedTypeTraits<Subject>::Name(), aSubject, aCategory,
                  aLabel, aValue);
  }

  static void EagerLogPrintf(const char* aSubjectTypeName,
                             const void* aSubjectPointer,
                             DDLogCategory aCategory, const char* aLabel,
                             const char* aString) {
    Log(aSubjectTypeName, aSubjectPointer, aCategory, aLabel,
        DDLogValue{nsCString{aString}});
  }

  template <typename... Args>
  static void EagerLogPrintf(const char* aSubjectTypeName,
                             const void* aSubjectPointer,
                             DDLogCategory aCategory, const char* aLabel,
                             const char* aFormat, Args&&... aArgs) {
    Log(aSubjectTypeName, aSubjectPointer, aCategory, aLabel,
        DDLogValue{
            nsCString{nsPrintfCString(aFormat, std::forward<Args>(aArgs)...)}});
  }

  template <typename Subject>
  static void EagerLogPrintf(const Subject* aSubject, DDLogCategory aCategory,
                             const char* aLabel, const char* aString) {
    EagerLogPrintf(DDLoggedTypeTraits<Subject>::Name(), aSubject, aCategory,
                   aLabel, aString);
  }

  template <typename Subject, typename... Args>
  static void EagerLogPrintf(const Subject* aSubject, DDLogCategory aCategory,
                             const char* aLabel, const char* aFormat,
                             Args&&... aArgs) {
    EagerLogPrintf(DDLoggedTypeTraits<Subject>::Name(), aSubject, aCategory,
                   aLabel, aFormat, std::forward<Args>(aArgs)...);
  }

  template <typename... Args>
  static void EagerLogFmt(const char* aSubjectTypeName,
                          const void* aSubjectPointer, DDLogCategory aCategory,
                          const char* aLabel,
                          fmt::format_string<Args...> aFormat,
                          Args&&... aArgs) {
    std::string formatted =
        fmt::vformat(aFormat, fmt::make_format_args(aArgs...));
    nsAutoCString printed;
    printed.Assign(formatted.data(), formatted.length());
    Log(aSubjectTypeName, aSubjectPointer, aCategory, aLabel,
        DDLogValue{nsCString{printed}});
  }

  template <typename Subject, typename... Args>
  static void EagerLogFmt(const Subject* aSubject, DDLogCategory aCategory,
                          const char* aLabel,
                          fmt::format_string<Args...> aFormat,
                          Args&&... aArgs) {
    EagerLogFmt(DDLoggedTypeTraits<Subject>::Name(), aSubject, aCategory,
                aLabel, aFormat, std::forward<Args>(aArgs)...);
  }

  static void MozLogPrintf(const char* aSubjectTypeName,
                           const void* aSubjectPointer,
                           const LogModule* aLogModule, LogLevel aLogLevel,
                           const char* aString) {
    Log(aSubjectTypeName, aSubjectPointer, CategoryForMozLogLevel(aLogLevel),
        aLogModule->Name(),  // LogModule name as label.
        DDLogValue{nsCString{aString}});
    MOZ_LOG_FMT(aLogModule, aLogLevel, "{}[{}] {}", aSubjectTypeName,
                fmt::ptr(aSubjectPointer), aString);
  }

  template <typename... Args>
  static void MozLogPrintf(const char* aSubjectTypeName,
                           const void* aSubjectPointer,
                           const LogModule* aLogModule, LogLevel aLogLevel,
                           const char* aFormat, Args&&... aArgs) {
    nsCString printed = nsPrintfCString(aFormat, std::forward<Args>(aArgs)...);
    Log(aSubjectTypeName, aSubjectPointer, CategoryForMozLogLevel(aLogLevel),
        aLogModule->Name(),  // LogModule name as label.
        DDLogValue{printed});
    MOZ_LOG_FMT(aLogModule, aLogLevel, "{}[{}] {}", aSubjectTypeName,
                fmt::ptr(aSubjectPointer), printed.get());
  }

  template <typename Subject>
  static void MozLogPrintf(const Subject* aSubject, const LogModule* aLogModule,
                           LogLevel aLogLevel, const char* aString) {
    MozLogPrintf(DDLoggedTypeTraits<Subject>::Name(), aSubject, aLogModule,
                 aLogLevel, aString);
  }

  template <typename Subject, typename... Args>
  static void MozLogPrintf(const Subject* aSubject, const LogModule* aLogModule,
                           LogLevel aLogLevel, const char* aFormat,
                           Args&&... aArgs) {
    MozLogPrintf(DDLoggedTypeTraits<Subject>::Name(), aSubject, aLogModule,
                 aLogLevel, aFormat, std::forward<Args>(aArgs)...);
  }

  template <typename... Args>
  static void MozLogFmt(const char* aSubjectTypeName,
                        const void* aSubjectPointer,
                        const LogModule* aLogModule, LogLevel aLogLevel,
                        fmt::format_string<Args...> aFormat, Args&&... aArgs) {
    std::string formatted =
        fmt::vformat(aFormat, fmt::make_format_args(aArgs...));
    nsAutoCString printed;
    printed.Assign(formatted.data(), formatted.length());
    Log(aSubjectTypeName, aSubjectPointer, CategoryForMozLogLevel(aLogLevel),
        aLogModule->Name(),  // LogModule name as label.
        DDLogValue{nsCString{printed}});
    MOZ_LOG_FMT(aLogModule, aLogLevel, "{}[{}] {}", aSubjectTypeName,
                fmt::ptr(aSubjectPointer), printed.get());
  }

  template <typename Subject, typename... Args>
  static void MozLogFmt(const Subject* aSubject, const LogModule* aLogModule,
                        LogLevel aLogLevel, fmt::format_string<Args...> aFormat,
                        Args&&... aArgs) {
    MozLogFmt(DDLoggedTypeTraits<Subject>::Name(), aSubject, aLogModule,
              aLogLevel, aFormat, std::forward<Args>(aArgs)...);
  }

  // Special logging functions. Consider using DecoderDoctorLifeLogger to
  // automatically capture constructions & destructions.

  static void LogConstruction(const char* aSubjectTypeName,
                              const void* aSubjectPointer) {
    Log(aSubjectTypeName, aSubjectPointer, DDLogCategory::_Construction, "",
        DDLogValue{DDNoValue{}});
  }

  static void LogConstructionAndBase(const char* aSubjectTypeName,
                                     const void* aSubjectPointer,
                                     const char* aBaseTypeName,
                                     const void* aBasePointer) {
    Log(aSubjectTypeName, aSubjectPointer, DDLogCategory::_DerivedConstruction,
        "", DDLogValue{DDLogObject{aBaseTypeName, aBasePointer}});
  }

  template <typename B>
  static void LogConstructionAndBase(const char* aSubjectTypeName,
                                     const void* aSubjectPointer,
                                     const B* aBase) {
    Log(aSubjectTypeName, aSubjectPointer, DDLogCategory::_DerivedConstruction,
        "", DDLogValue{DDLogObject{DDLoggedTypeTraits<B>::Name(), aBase}});
  }

  template <typename Subject>
  static void LogConstruction(NonDereferenceable<const Subject> aSubject) {
    using Traits = DDLoggedTypeTraits<Subject>;
    if (!Traits::HasBase::value) {
      Log(DDLoggedTypeTraits<Subject>::Name(),
          reinterpret_cast<const void*>(aSubject.value()),
          DDLogCategory::_Construction, "", DDLogValue{DDNoValue{}});
    } else {
      Log(DDLoggedTypeTraits<Subject>::Name(),
          reinterpret_cast<const void*>(aSubject.value()),
          DDLogCategory::_DerivedConstruction, "",
          DDLogValue{DDLogObject{
              DDLoggedTypeTraits<typename Traits::BaseType>::Name(),
              reinterpret_cast<const void*>(
                  NonDereferenceable<const typename Traits::BaseType>(aSubject)
                      .value())}});
    }
  }

  template <typename Subject>
  static void LogConstruction(const Subject* aSubject) {
    LogConstruction(NonDereferenceable<const Subject>(aSubject));
  }

  static void LogDestruction(const char* aSubjectTypeName,
                             const void* aSubjectPointer) {
    Log(aSubjectTypeName, aSubjectPointer, DDLogCategory::_Destruction, "",
        DDLogValue{DDNoValue{}});
  }

  template <typename Subject>
  static void LogDestruction(NonDereferenceable<const Subject> aSubject) {
    Log(DDLoggedTypeTraits<Subject>::Name(),
        reinterpret_cast<const void*>(aSubject.value()),
        DDLogCategory::_Destruction, "", DDLogValue{DDNoValue{}});
  }

  template <typename Subject>
  static void LogDestruction(const Subject* aSubject) {
    LogDestruction(NonDereferenceable<const Subject>(aSubject));
  }

  template <typename P, typename C>
  static void LinkParentAndChild(const P* aParent, const char* aLinkName,
                                 const C* aChild) {
    if (aChild) {
      Log(DDLoggedTypeTraits<P>::Name(), aParent, DDLogCategory::_Link,
          aLinkName,
          DDLogValue{DDLogObject{DDLoggedTypeTraits<C>::Name(), aChild}});
    }
  }

  template <typename C>
  static void LinkParentAndChild(const char* aParentTypeName,
                                 const void* aParentPointer,
                                 const char* aLinkName, const C* aChild) {
    if (aChild) {
      Log(aParentTypeName, aParentPointer, DDLogCategory::_Link, aLinkName,
          DDLogValue{DDLogObject{DDLoggedTypeTraits<C>::Name(), aChild}});
    }
  }

  template <typename P>
  static void LinkParentAndChild(const P* aParent, const char* aLinkName,
                                 const char* aChildTypeName,
                                 const void* aChildPointer) {
    if (aChildPointer) {
      Log(DDLoggedTypeTraits<P>::Name(), aParent, DDLogCategory::_Link,
          aLinkName, DDLogValue{DDLogObject{aChildTypeName, aChildPointer}});
    }
  }

  template <typename C>
  static void UnlinkParentAndChild(const char* aParentTypeName,
                                   const void* aParentPointer,
                                   const C* aChild) {
    if (aChild) {
      Log(aParentTypeName, aParentPointer, DDLogCategory::_Unlink, "",
          DDLogValue{DDLogObject{DDLoggedTypeTraits<C>::Name(), aChild}});
    }
  }

  template <typename P, typename C>
  static void UnlinkParentAndChild(const P* aParent, const C* aChild) {
    if (aChild) {
      Log(DDLoggedTypeTraits<P>::Name(), aParent, DDLogCategory::_Unlink, "",
          DDLogValue{DDLogObject{DDLoggedTypeTraits<C>::Name(), aChild}});
    }
  }

  // Retrieval functions.

  // Enable logging, if not done already. No effect otherwise.
  static void EnableLogging();

  using LogMessagesPromise =
      MozPromise<nsCString, nsresult, /* IsExclusive = */ true>;

  // Retrieve all messages related to a given HTMLMediaElement object.
  // This call will trigger a processing run (to ensure the most recent data
  // will be returned), and the returned promise will be resolved with all
  // relevant log messages and object lifetimes in a JSON string.
  // The first call will enable logging, until shutdown.
  static RefPtr<LogMessagesPromise> RetrieveMessages(
      const dom::HTMLMediaElement* aMediaElement);

 private:
  // If logging is not enabled yet, initiate it, return true.
  // If logging has been shutdown, don't start it, return false.
  // Otherwise return true.
  static bool EnsureLogIsEnabled();

  // Note that this call may block while the state is scEnabling;
  // set aDontBlock to true to avoid blocking, most importantly when the
  // caller is the one doing the enabling, this would cause an endless loop.
  static void PanicInternal(const char* aReason, bool aDontBlock);

  static void Log(const char* aSubjectTypeName, const void* aSubjectPointer,
                  DDLogCategory aCategory, const char* aLabel,
                  DDLogValue&& aValue);

  static void Log(const char* aSubjectTypeName, const void* aSubjectPointer,
                  const LogModule* aLogModule, LogLevel aLogLevel,
                  DDLogValue&& aValue);

  static DDLogCategory CategoryForMozLogLevel(LogLevel aLevel) {
    switch (aLevel) {
      default:
      case LogLevel::Error:
        return DDLogCategory::MozLogError;
      case LogLevel::Warning:
        return DDLogCategory::MozLogWarning;
      case LogLevel::Info:
        return DDLogCategory::MozLogInfo;
      case LogLevel::Debug:
        return DDLogCategory::MozLogDebug;
      case LogLevel::Verbose:
        return DDLogCategory::MozLogVerbose;
    }
  }

  using LogState = int;
  // Currently disabled, may be enabled on request.
  static constexpr LogState scDisabled = 0;
  // Currently enabled (logging happens), may be shutdown.
  static constexpr LogState scEnabled = 1;
  // Still disabled, but one thread is working on enabling it, nobody else
  // should interfere during this time.
  static constexpr LogState scEnabling = 2;
  // Shutdown, cannot be re-enabled.
  static constexpr LogState scShutdown = 3;
  // Current state.
  // "ReleaseAcquire" because when changing to scEnabled, the just-created
  // sMediaLogs must be accessible to consumers that see scEnabled.
  static Atomic<LogState, ReleaseAcquire> sLogState;

  // If non-null, reason for an abnormal shutdown.
  static const char* sShutdownReason;
};

// Base class to automatically record a class lifetime. Usage:
//   class SomeClass : public DecoderDoctorLifeLogger<SomeClass>
//   {
//     ...
template <typename T>
class DecoderDoctorLifeLogger {
 protected:
  DecoderDoctorLifeLogger() {
    DecoderDoctorLogger::LogConstruction(NonDereferenceable<const T>(this));
  }
  ~DecoderDoctorLifeLogger() {
    DecoderDoctorLogger::LogDestruction(NonDereferenceable<const T>(this));
  }
};

// Macros to help lazily-evaluate arguments, only after we have checked that
// logging is enabled.

// Log a single value; see DDLogValue for allowed types.
#define DDLOG(_category, _label, _arg)                                   \
  do {                                                                   \
    if (DecoderDoctorLogger::IsDDLoggingEnabled()) {                     \
      DecoderDoctorLogger::EagerLogValue(this, _category, _label, _arg); \
    }                                                                    \
  } while (0)
// Log a single value, with an EXplicit `this`.
#define DDLOGEX(_this, _category, _label, _arg)                           \
  do {                                                                    \
    if (DecoderDoctorLogger::IsDDLoggingEnabled()) {                      \
      DecoderDoctorLogger::EagerLogValue(_this, _category, _label, _arg); \
    }                                                                     \
  } while (0)
// Log a single value, with EXplicit type name and `this`.
#define DDLOGEX2(_typename, _this, _category, _label, _arg)                   \
  do {                                                                        \
    if (DecoderDoctorLogger::IsDDLoggingEnabled()) {                          \
      DecoderDoctorLogger::EagerLogValue(_typename, _this, _category, _label, \
                                         _arg);                               \
    }                                                                         \
  } while (0)

#ifdef DEBUG
// Do a printf format check in DEBUG, with the downside that side-effects (from
// evaluating the arguments) may happen twice! Who would do that anyway?
static void inline MOZ_FORMAT_PRINTF(1, 2) DDLOGPRCheck(const char*, ...) {}
#  define DDLOGPR_CHECK(_fmt, ...) DDLOGPRCheck(_fmt, ##__VA_ARGS__)
#else
#  define DDLOGPR_CHECK(_fmt, ...)
#endif

// Log a printf'd string. Discouraged, please try using DDLOG instead.
#define DDLOGPR(_category, _label, _format, ...)                            \
  do {                                                                      \
    if (DecoderDoctorLogger::IsDDLoggingEnabled()) {                        \
      DDLOGPR_CHECK(_format, ##__VA_ARGS__);                                \
      DecoderDoctorLogger::EagerLogPrintf(this, _category, _label, _format, \
                                          ##__VA_ARGS__);                   \
    }                                                                       \
  } while (0)

// Record an fmt-formatted string in DDLogger only (no MOZ_LOG). Pick the macro
// by what is being logged:
//   - a single value -> DDLOG (preferred: no string formatting/allocation, and
//     the value stays typed and machine-readable in the DD store);
//   - a free-form composed message that is not a single value (e.g. a
//     multi-field JSON line) -> this macro (or DDLOGPR for printf style);
//   - either of the above, but also wanted in MOZ_LOG output -> DDMOZ_LOG_FMT.
#define DDLOGPR_FMT(_category, _label, _format, ...)                     \
  do {                                                                   \
    if (DecoderDoctorLogger::IsDDLoggingEnabled()) {                     \
      DecoderDoctorLogger::EagerLogFmt(this, _category, _label, _format, \
                                       ##__VA_ARGS__);                   \
    }                                                                    \
  } while (0)

// Link a child object.
#define DDLINKCHILD(...)                                          \
  do {                                                            \
    if (DecoderDoctorLogger::IsDDLoggingEnabled()) {              \
      DecoderDoctorLogger::LinkParentAndChild(this, __VA_ARGS__); \
    }                                                             \
  } while (0)

// Unlink a child object.
#define DDUNLINKCHILD(...)                                          \
  do {                                                              \
    if (DecoderDoctorLogger::IsDDLoggingEnabled()) {                \
      DecoderDoctorLogger::UnlinkParentAndChild(this, __VA_ARGS__); \
    }                                                               \
  } while (0)

// Log a printf'd string to DDLogger and/or MOZ_LOG, with an EXplicit `this`.
// Don't even call MOZ_LOG on Android non-release/beta; See Logging.h.
#if !defined(ANDROID) || !defined(RELEASE_OR_BETA)
#  define DDMOZ_LOGEX(_this, _logModule, _logLevel, _format, ...)       \
    do {                                                                \
      if (DecoderDoctorLogger::IsDDLoggingEnabled() ||                  \
          MOZ_LOG_TEST(_logModule, _logLevel)) {                        \
        DDLOGPR_CHECK(_format, ##__VA_ARGS__);                          \
        DecoderDoctorLogger::MozLogPrintf(_this, _logModule, _logLevel, \
                                          _format, ##__VA_ARGS__);      \
      }                                                                 \
    } while (0)
#else
#  define DDMOZ_LOGEX(_this, _logModule, _logLevel, _format, ...)       \
    do {                                                                \
      if (DecoderDoctorLogger::IsDDLoggingEnabled()) {                  \
        DDLOGPR_CHECK(_format, ##__VA_ARGS__);                          \
        DecoderDoctorLogger::MozLogPrintf(_this, _logModule, _logLevel, \
                                          _format, ##__VA_ARGS__);      \
      }                                                                 \
    } while (0)
#endif

// Log a printf'd string to DDLogger and/or MOZ_LOG.
#define DDMOZ_LOG(_logModule, _logLevel, _format, ...) \
  DDMOZ_LOGEX(this, _logModule, _logLevel, _format, ##__VA_ARGS__)

// Log an fmt-formatted string to DDLogger and/or MOZ_LOG, with an explicit
// `this`. On Android release/beta builds the MOZ_LOG branch is skipped and only
// DDLogger is used, matching DDMOZ_LOGEX above; MOZ_LOG was historically
// compiled out there to save binary size (bug 1841818 later enabled it).
#if !defined(ANDROID) || !defined(RELEASE_OR_BETA)
#  define DDMOZ_LOGEX_FMT(_this, _logModule, _logLevel, _format, ...)         \
    do {                                                                      \
      if (DecoderDoctorLogger::IsDDLoggingEnabled() ||                        \
          MOZ_LOG_TEST(_logModule, _logLevel)) {                              \
        DecoderDoctorLogger::MozLogFmt(_this, _logModule, _logLevel, _format, \
                                       ##__VA_ARGS__);                        \
      }                                                                       \
    } while (0)
#else
#  define DDMOZ_LOGEX_FMT(_this, _logModule, _logLevel, _format, ...)         \
    do {                                                                      \
      if (DecoderDoctorLogger::IsDDLoggingEnabled()) {                        \
        DecoderDoctorLogger::MozLogFmt(_this, _logModule, _logLevel, _format, \
                                       ##__VA_ARGS__);                        \
      }                                                                       \
    } while (0)
#endif

// Log an fmt-formatted string to DDLogger and/or MOZ_LOG.
#define DDMOZ_LOG_FMT(_logModule, _logLevel, _format, ...) \
  DDMOZ_LOGEX_FMT(this, _logModule, _logLevel, _format, ##__VA_ARGS__)

}  // namespace mozilla

#endif  // DecoderDoctorLogger_h_
