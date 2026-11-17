/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsMemoryReporterManager_h_
#define nsMemoryReporterManager_h_

#include "mozilla/Mutex.h"
#include "nsHashKeys.h"
#include "nsIMemoryReporter.h"
#include "nsISupports.h"
#include "nsServiceManagerUtils.h"
#include "nsTHashMap.h"

#ifdef XP_WIN
#  include <windows.h>
#endif  // XP_WIN

#if defined(MOZ_MEMORY)
#  define HAVE_JEMALLOC_STATS 1
#  include "mozmemory.h"
#endif  // MOZ_MEMORY

namespace mozilla {
class MemoryReportingProcess;
namespace dom {
class MemoryReport;
}  // namespace dom
}  // namespace mozilla

class mozIDOMWindowProxy;
class nsIEventTarget;
class nsIRunnable;
class nsITimer;

class nsMemoryReporterManager final : public nsIMemoryReporterManager,
                                      public nsIMemoryReporter {
  virtual ~nsMemoryReporterManager();

 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIMEMORYREPORTERMANAGER
  NS_DECL_NSIMEMORYREPORTER

  MOZ_DEFINE_MALLOC_SIZE_OF(MallocSizeOf)

  nsMemoryReporterManager();

  // Gets the memory reporter manager service.
  static already_AddRefed<nsMemoryReporterManager> GetOrCreate() {
    nsCOMPtr<nsIMemoryReporterManager> imgr =
        do_GetService("@mozilla.org/memory-reporter-manager;1");
    return imgr.forget().downcast<nsMemoryReporterManager>();
  }

  typedef AutoTArray<nsCOMPtr<nsIMemoryReporter>, 32> StrongReportersArray;

  typedef nsTHashMap<nsRefPtrHashKey<nsIMemoryReporter>, bool>
      StrongReportersTable;
  typedef nsTHashMap<nsPtrHashKey<nsIMemoryReporter>, bool> WeakReportersTable;

  // Inter-process memory reporting proceeds as follows.
  //
  // - GetReports() (declared within NS_DECL_NSIMEMORYREPORTERMANAGER)
  //   synchronously gets memory reports for the current process, sets up some
  //   state (mPendingProcessesState) for when child processes report back --
  //   including a timer -- and starts telling child processes to get memory
  //   reports.  Control then returns to the main event loop.
  //
  //   The number of concurrent child process reports is limited by the pref
  //   "memory.report_concurrency" in order to prevent the memory overhead of
  //   memory reporting from causing problems, especially on B2G when swapping
  //   to compressed RAM; see bug 1154053.
  //
  // - HandleChildReport() is called (asynchronously) once per child process
  //   reporter callback.
  //
  // - EndProcessReport() is called (asynchronously) once per process that
  //   finishes reporting back, including the parent.  If all processes do so
  //   before time-out, the timer is cancelled.  If there are child processes
  //   whose requests have not yet been sent, they will be started until the
  //   concurrency limit is (again) reached.
  //
  // - TimeoutCallback() is called (asynchronously) if all the child processes
  //   don't respond within the time threshold.
  //
  // - FinishReporting() finishes things off.  It is *always* called -- either
  //   from EndChildReport() (if all child processes have reported back) or
  //   from TimeoutCallback() (if time-out occurs).
  //
  // All operations occur on the main thread.
  //
  // The above sequence of steps is a "request".  A partially-completed request
  // is described as "in flight".
  //
  // Each request has a "generation", a unique number that identifies it.  This
  // is used to ensure that each reports from a child process corresponds to
  // the appropriate request from the parent process.  (It's easier to
  // implement a generation system than to implement a child report request
  // cancellation mechanism.)
  //
  // Failures are mostly ignored, because it's (a) typically the most sensible
  // thing to do, and (b) often hard to do anything else.  The following are
  // the failure cases of note.
  //
  // - If a request is made while the previous request is in flight, the new
  //   request is ignored, as per getReports()'s specification.  No error is
  //   reported, because the previous request will complete soon enough.
  //
  // - If one or more child processes fail to respond within the time limit,
  //   things will proceed as if they don't exist.  No error is reported,
  //   because partial information is better than nothing.
  //
  // - If a child process reports after the time-out occurs, it is ignored.
  //   (Generation checking will ensure it is ignored even if a subsequent
  //   request is in flight;  this is the main use of generations.)  No error
  //   is reported, because there's nothing sensible to be done about it at
  //   this late stage.
  //
  // - If the time-out occurs after a child process has sent some reports but
  //   before it has signaled completion (see bug 1151597), then what it
  //   successfully sent will be included, with no explicit indication that it
  //   is incomplete.
  //
  // Now, what what happens if a child process is created/destroyed in the
  // middle of a request?  Well, PendingProcessesState is initialized with an
  // array of child process actors as of when the report started.  So...
  //
  // - If a process is created after reporting starts, it won't be sent a
  //   request for reports.  So the reported data will reflect how things were
  //   when the request began.
  //
  // - If a process is destroyed before it starts reporting back, the reported
  //   data will reflect how things are when the request ends.
  //
  // - If a process is destroyed after it starts reporting back but before it
  //   finishes, the reported data will contain a partial report for it.
  //
  // - If a process is destroyed after reporting back, but before all other
  //   child processes have reported back, it will be included in the reported
  //   data.  So the reported data will reflect how things were when the
  //   request began.
  //
  // The inconsistencies between these cases are unfortunate but difficult to
  // avoid.  It's enough of an edge case to not be worth doing more.
  //
  void HandleChildReport(uint32_t aGeneration,
                         const mozilla::dom::MemoryReport& aChildReport);
  void EndProcessReport(uint32_t aGeneration, bool aSuccess);

  // Functions that (a) implement distinguished amounts, and (b) are outside of
  // this module.
  struct AmountFns {
    mozilla::InfallibleAmountFn mJSMainRuntimeGCHeap = nullptr;
    mozilla::InfallibleAmountFn mJSMainRuntimeTemporaryPeak = nullptr;
    mozilla::InfallibleAmountFn mJSMainRuntimeCompartmentsSystem = nullptr;
    mozilla::InfallibleAmountFn mJSMainRuntimeCompartmentsUser = nullptr;
    mozilla::InfallibleAmountFn mJSMainRuntimeRealmsSystem = nullptr;
    mozilla::InfallibleAmountFn mJSMainRuntimeRealmsUser = nullptr;

    mozilla::InfallibleAmountFn mImagesContentUsedUncompressed = nullptr;

    mozilla::InfallibleAmountFn mStorageSQLite = nullptr;

    mozilla::InfallibleAmountFn mLowMemoryEventsPhysical = nullptr;

    mozilla::InfallibleAmountFn mGhostWindows = nullptr;
  };
  AmountFns mAmountFns;

  // Convenience function to get RSS easily from other code.  This is useful
  // when debugging transient memory spikes with printf instrumentation.
  static int64_t ResidentFast();

  // Convenience function to get peak RSS easily from other code.
  static int64_t ResidentPeak();

  // Convenience function to get USS easily from other code.  This is useful
  // when debugging unshared memory pages for forked processes.
  //
  // Returns 0 if, for some reason, the resident unique memory cannot be
  // determined - typically if there is a race between us and someone else
  // closing the process and we lost that race.
#ifdef XP_WIN
  static int64_t ResidentUnique(HANDLE aProcess = nullptr);
#elif XP_MACOSX
  // On MacOS this can sometimes be significantly slow. It should not be used
  // except in debugging or at the request of a user (eg about:memory).
  static int64_t ResidentUnique(mach_port_t aPort = 0);
#else
  static int64_t ResidentUnique(pid_t aPid = 0);
#endif  // XP_{WIN, MACOSX, LINUX, *}

#ifdef XP_MACOSX
  // Retrive the "phys_footprint" memory statistic on MacOS.
  static int64_t PhysicalFootprint(mach_port_t aPort = 0);
#endif

  // Functions that measure per-tab memory consumption.
  struct SizeOfTabFns {
    mozilla::JSSizeOfTabFn mJS = nullptr;
    mozilla::NonJSSizeOfTabFn mNonJS = nullptr;
  };
  SizeOfTabFns mSizeOfTabFns;

#ifdef HAVE_JEMALLOC_STATS
  // These C++ only versions of HeapAllocated and HeapOverheadFraction avoid
  // extra calls to jemalloc_stats;
  static size_t HeapAllocated(const jemalloc_stats_t& stats);
  static int64_t HeapOverheadFraction(const jemalloc_stats_t& stats);
#endif

 private:
  bool IsRegistrationBlocked() MOZ_EXCLUDES(mMutex) {
    mozilla::MutexAutoLock lock(mMutex);
    return mIsRegistrationBlocked;
  }

  [[nodiscard]] nsresult RegisterReporterHelper(nsIMemoryReporter* aReporter,
                                                bool aForce, bool aStrongRef,
                                                bool aIsAsync);

  [[nodiscard]] nsresult StartGettingReports();
  // No [[nodiscard]] here because ignoring the result is common and reasonable.
  nsresult FinishReporting();

  void DispatchReporter(nsIMemoryReporter* aReporter, bool aIsAsync,
                        nsIHandleReportCallback* aHandleReport,
                        nsISupports* aHandleReportData, bool aAnonymize);

  static void TimeoutCallback(nsITimer* aTimer, void* aData);

  mozilla::Mutex mMutex;
  bool mIsRegistrationBlocked MOZ_GUARDED_BY(mMutex);

  // This array is used for strong reporters that are not supposed to ever be
  // unregistered before the manager goes away. Currently this is only used
  // for reporters the manager defines itself. These reporters must be sync!
  mozilla::UniquePtr<StrongReportersArray> mStrongEternalReporters
      MOZ_GUARDED_BY(mMutex);

  // These hash tables are used for all additional reporters registered via
  // our nsIMemoryReporterManager interface.
  mozilla::UniquePtr<StrongReportersTable> mStrongReporters
      MOZ_GUARDED_BY(mMutex);
  mozilla::UniquePtr<WeakReportersTable> mWeakReporters MOZ_GUARDED_BY(mMutex);

  // These three are only used for testing purposes.
  mozilla::UniquePtr<StrongReportersArray> mSavedStrongEternalReporters
      MOZ_GUARDED_BY(mMutex);
  mozilla::UniquePtr<StrongReportersTable> mSavedStrongReporters
      MOZ_GUARDED_BY(mMutex);
  mozilla::UniquePtr<WeakReportersTable> mSavedWeakReporters
      MOZ_GUARDED_BY(mMutex);

  uint32_t mNextGeneration;  // MainThread only

  // Used to keep track of state of which processes are currently running and
  // waiting to run memory reports. Holds references to parameters needed when
  // requesting a memory report and finishing reporting.
  struct PendingProcessesState {
    uint32_t mGeneration;
    bool mAnonymize;
    bool mMinimize;
    nsCOMPtr<nsITimer> mTimer;
    nsTArray<RefPtr<mozilla::MemoryReportingProcess>> mChildrenPending;
    uint32_t mNumProcessesRunning;
    uint32_t mNumProcessesCompleted;
    uint32_t mConcurrencyLimit;
    nsCOMPtr<nsIHandleReportCallback> mHandleReport;
    nsCOMPtr<nsISupports> mHandleReportData;
    nsCOMPtr<nsIFinishReportingCallback> mFinishReporting;
    nsCOMPtr<nsISupports> mFinishReportingData;
    nsString mDMDDumpIdent;

    PendingProcessesState(uint32_t aGeneration, bool aAnonymize, bool aMinimize,
                          uint32_t aConcurrencyLimit,
                          nsIHandleReportCallback* aHandleReport,
                          nsISupports* aHandleReportData,
                          nsIFinishReportingCallback* aFinishReporting,
                          nsISupports* aFinishReportingData,
                          const nsAString& aDMDDumpIdent);
  };

  // Used to keep track of the state of the asynchronously run memory
  // reporters. The callback and file handle used when all memory reporters
  // have finished are also stored here.
  struct PendingReportersState {
    // Number of memory reporters currently running.
    uint32_t mReportsPending;

    // Callback for when all memory reporters have completed.
    nsCOMPtr<nsIFinishReportingCallback> mFinishReporting;
    nsCOMPtr<nsISupports> mFinishReportingData;

    // File handle to write a DMD report to if requested.
    FILE* mDMDFile;

    PendingReportersState(nsIFinishReportingCallback* aFinishReporting,
                          nsISupports* aFinishReportingData, FILE* aDMDFile)
        : mReportsPending(0),
          mFinishReporting(aFinishReporting),
          mFinishReportingData(aFinishReportingData),
          mDMDFile(aDMDFile) {}
  };

  // When this is non-null, a request is in flight.  Note: We use manual
  // new/delete for this because its lifetime doesn't match block scope or
  // anything like that.
  PendingProcessesState* mPendingProcessesState;  // MainThread only

  // This is reinitialized each time a call to GetReports is initiated.
  PendingReportersState* mPendingReportersState;  // MainThread only

  PendingProcessesState* GetStateForGeneration(uint32_t aGeneration);
  [[nodiscard]] static bool StartChildReport(
      mozilla::MemoryReportingProcess* aChild,
      const PendingProcessesState* aState);
};

#define NS_MEMORY_REPORTER_MANAGER_CID \
  {0xfb97e4f5, 0x32dd, 0x497a, {0xba, 0xa2, 0x7d, 0x1e, 0x55, 0x7, 0x99, 0x10}}

#endif  // nsMemoryReporterManager_h_
