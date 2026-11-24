/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DecoderDoctorLogger.h"

#include "DDLogUtils.h"
#include "DDMediaLogs.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/StaticPtr.h"

namespace mozilla {

/* static */ Atomic<DecoderDoctorLogger::LogState, ReleaseAcquire>
    DecoderDoctorLogger::sLogState{DecoderDoctorLogger::scDisabled};

/* static */ const char* DecoderDoctorLogger::sShutdownReason = nullptr;

static DDMediaLogs* sMediaLogs;

/* static */
void DecoderDoctorLogger::Init() {
  MOZ_ASSERT(sLogState.load() == scDisabled);
  if (MOZ_LOG_TEST(sDecoderDoctorLoggerLog, LogLevel::Error) ||
      MOZ_LOG_TEST(sDecoderDoctorLoggerEndLog, LogLevel::Error)) {
    EnableLogging();
  }
}

#ifndef RELEASE_OR_BETA
// First DDLogShutdowner sets sLogState to scShutdown, to prevent further
// logging.
struct DDLogShutdowner {
  ~DDLogShutdowner() {
    DDL_INFO("Shutting down");
    // Prevent further logging, some may racily seep in, it's fine as the
    // logging infrastructure would still be alive until DDLogDeleter runs.
    DecoderDoctorLogger::ShutdownLogging();
  }
};
static StaticAutoPtr<DDLogShutdowner> sDDLogShutdowner;

// Later DDLogDeleter will delete the message queue and media logs.
struct DDLogDeleter {
  ~DDLogDeleter() {
    if (sMediaLogs) {
      DDL_INFO("Final processing of collected logs");
      delete sMediaLogs;
      sMediaLogs = nullptr;
    }
  }
};
static StaticAutoPtr<DDLogDeleter> sDDLogDeleter;
#endif

/* static */
void DecoderDoctorLogger::PanicInternal(const char* aReason, bool aDontBlock) {
  for (;;) {
    const LogState state = sLogState.load();
    if (state == scEnabling && !aDontBlock) {
      // Wait for the end of the enabling process (unless we're in it, in which
      // case we don't want to block.)
      continue;
    }
    if (state == scShutdown) {
      // Already shutdown, nothing more to do.
      break;
    }
    if (sLogState.compareExchange(state, scShutdown)) {
      // We are the one performing the first shutdown -> Record reason.
      sShutdownReason = aReason;
      // Free as much memory as possible.
      if (sMediaLogs) {
        // Shutdown the medialogs processing thread, and free as much memory
        // as possible.
        sMediaLogs->Panic();
      }
      // sMediaLogs and sQueue will be deleted by DDLogDeleter.
      // We don't want to delete them right now, because there could be a race
      // where another thread started logging or retrieving logs before we
      // changed the state to scShutdown, but has been delayed before actually
      // trying to write or read log messages, thereby causing a UAF.
    }
    // If someone else changed the state, we'll just loop around, and either
    // shutdown already happened elsewhere, or we'll try to shutdown again.
  }
}

/* static */
bool DecoderDoctorLogger::EnsureLogIsEnabled() {
#ifdef RELEASE_OR_BETA
  // Just refuse to enable DDLogger on release&beta because it makes it too easy
  // to trigger an OOM. See bug 1571648.
  return false;
#else
  for (;;) {
    LogState state = sLogState.load();
    switch (state) {
      case scDisabled:
        // Currently disabled, try to be the one to enable.
        if (sLogState.compareExchange(scDisabled, scEnabling)) {
          // We are the one to enable logging, state won't change (except for
          // possible shutdown.)
          // Create DDMediaLogs singleton, which will process the message queue.
          DDMediaLogs::ConstructionResult mediaLogsConstruction =
              DDMediaLogs::New();
          if (NS_FAILED(mediaLogsConstruction.mRv)) {
            PanicInternal("Failed to enable logging", /* aDontBlock */ true);
            return false;
          }
          MOZ_ASSERT(mediaLogsConstruction.mMediaLogs);
          sMediaLogs = mediaLogsConstruction.mMediaLogs;
          // Setup shutdown-time clean-up.
          MOZ_ALWAYS_SUCCEEDS(SchedulerGroup::Dispatch(
              NS_NewRunnableFunction("DDLogger shutdown setup", [] {
                sDDLogShutdowner = new DDLogShutdowner();
                ClearOnShutdown(&sDDLogShutdowner,
                                ShutdownPhase::XPCOMShutdown);
                sDDLogDeleter = new DDLogDeleter();
                ClearOnShutdown(&sDDLogDeleter,
                                ShutdownPhase::XPCOMShutdownThreads);
              })));

          // Nobody else should change the state when *we* are enabling logging.
          MOZ_ASSERT(sLogState == scEnabling);
          sLogState = scEnabled;
          DDL_INFO("Logging enabled");
          return true;
        }
        // Someone else changed the state before our compareExchange, just loop
        // around to examine the new situation.
        break;
      case scEnabled:
        return true;
      case scEnabling:
        // Someone else is currently enabling logging, actively wait by just
        // looping, until the state changes.
        break;
      case scShutdown:
        // Shutdown is non-recoverable, we cannot enable logging again.
        return false;
    }
    // Not returned yet, loop around to examine the new situation.
  }
#endif
}

/* static */
void DecoderDoctorLogger::EnableLogging() { (void)EnsureLogIsEnabled(); }

/* static */ RefPtr<DecoderDoctorLogger::LogMessagesPromise>
DecoderDoctorLogger::RetrieveMessages(
    const dom::HTMLMediaElement* aMediaElement) {
  if (MOZ_UNLIKELY(!EnsureLogIsEnabled())) {
    DDL_WARN("Request (for {}) but there are no logs", fmt::ptr(aMediaElement));
    return DecoderDoctorLogger::LogMessagesPromise::CreateAndReject(
        NS_ERROR_DOM_MEDIA_ABORT_ERR, __func__);
  }
  return sMediaLogs->RetrieveMessages(aMediaElement);
}

/* static */
void DecoderDoctorLogger::Log(const char* aSubjectTypeName,
                              const void* aSubjectPointer,
                              DDLogCategory aCategory, const char* aLabel,
                              DDLogValue&& aValue) {
  if (IsDDLoggingEnabled()) {
    MOZ_ASSERT(sMediaLogs);
    sMediaLogs->Log(aSubjectTypeName, aSubjectPointer, aCategory, aLabel,
                    std::move(aValue));
  }
}

}  // namespace mozilla
