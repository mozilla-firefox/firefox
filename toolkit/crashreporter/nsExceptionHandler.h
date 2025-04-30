/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This header has two implementations, the real one in nsExceptionHandler.cpp
// and a dummy in nsDummyExceptionHandler.cpp. The latter is used in builds
// configured with --disable-crashreporter. If you add or remove a function
// from this header you must update both implementations otherwise you'll break
// builds that disable the crash reporter.

#ifndef nsExceptionHandler_h__
#define nsExceptionHandler_h__

#include "mozilla/EnumeratedArray.h"
#include "mozilla/Maybe.h"
#if !defined(XP_WIN)
#  include "mozilla/UniquePtrExtensions.h"  // For UniqueFileHandle
#endif                                      // XP_WIN

#include "CrashAnnotations.h"

#include "nsError.h"
#include "nsString.h"
#include "nsXULAppAPI.h"
#include <stddef.h>
#include <stdint.h>

#if defined(XP_WIN)
#  include <handleapi.h>
#elif defined(XP_MACOSX)
#  include <mach/mach.h>
#elif defined(XP_LINUX)
#  include <signal.h>
#  if defined(MOZ_OXIDIZED_BREAKPAD)
struct DirectAuxvDumpInfo;
#  endif  // defined(MOZ_OXIDIZED_BREAKPAD)
#endif    // defined(XP_LINUX)

class nsIFile;

namespace CrashReporter {

using mozilla::Maybe;
using mozilla::Nothing;

#if defined(XP_WIN)
typedef HANDLE ProcessHandle;
typedef DWORD ProcessId;
typedef DWORD ThreadId;
typedef HANDLE FileHandle;
const FileHandle kInvalidFileHandle = INVALID_HANDLE_VALUE;
#elif defined(XP_MACOSX)
typedef task_t ProcessHandle;
typedef pid_t ProcessId;
typedef mach_port_t ThreadId;
typedef int FileHandle;
const FileHandle kInvalidFileHandle = -1;
#else
typedef int ProcessHandle;
typedef pid_t ProcessId;
typedef int ThreadId;
typedef int FileHandle;
const FileHandle kInvalidFileHandle = -1;
#endif

#if defined(XP_LINUX) && defined(MOZ_OXIDIZED_BREAKPAD)
void GetCurrentProcessAuxvInfo(DirectAuxvDumpInfo* aAuxvInfo);
void RegisterChildAuxvInfo(pid_t aChildPid,
                           const DirectAuxvDumpInfo& aAuxvInfo);
void UnregisterChildAuxvInfo(pid_t aChildPid);
#endif  // defined(XP_LINUX) && defined(MOZ_OXIDIZED_BREAKPAD)

/**
 * Returns true if the crash reporter is using the dummy implementation.
 */
static inline bool IsDummy() {
#ifdef MOZ_CRASHREPORTER
  return false;
#else
  return true;
#endif
}

nsresult SetExceptionHandler(nsIFile* aXREDirectory, bool force = false);
nsresult UnsetExceptionHandler();

/**
 * Tell the crash reporter to recalculate where crash events files should go.
 * SetCrashEventsDir is used before XPCOM is initialized from the startup
 * code.
 *
 * UpdateCrashEventsDir uses the directory service to re-set the
 * crash event directory based on the current profile.
 *
 * 1. If environment variable is present, use it. We don't expect
 *    the environment variable except for tests and other atypical setups.
 * 2. <profile>/crashes/events
 * 3. <UAppData>/Crash Reports/events
 */
void SetUserAppDataDirectory(nsIFile* aDir);
void SetProfileDirectory(nsIFile* aDir);
void UpdateCrashEventsDir();
void SetMemoryReportFile(nsIFile* aFile);
nsresult GetDefaultMemoryReportFile(nsIFile** aFile);

/**
 * Get the path where crash event files should be written.
 */
bool GetCrashEventsDir(nsAString& aPath);

bool GetEnabled();
bool GetServerURL(nsACString& aServerURL);
nsresult SetServerURL(const nsACString& aServerURL);
bool GetMinidumpPath(nsAString& aPath);
nsresult SetMinidumpPath(const nsAString& aPath);

// These functions are thread safe and can be called in both the parent and
// child processes. Annotations added in the main process will be included in
// child process crashes too unless the child process sets its own annotations.
// If it does the child-provided annotation overrides the one set in the parent.
const bool* RegisterAnnotationBool(Annotation aKey, const bool* aData);
const uint32_t* RegisterAnnotationU32(Annotation aKey, const uint32_t* aData);
const uint64_t* RegisterAnnotationU64(Annotation aKey, const uint64_t* aData);
const size_t* RegisterAnnotationUSize(Annotation aKey, const size_t* aData);
const char* RegisterAnnotationCString(Annotation aKey, const char* aData);
const nsCString* RegisterAnnotationNSCString(Annotation aKey,
                                             const nsCString* aData);

nsresult RecordAnnotationBool(Annotation aKey, bool aData);
nsresult RecordAnnotationU32(Annotation aKey, uint32_t aData);
nsresult RecordAnnotationU64(Annotation aKey, uint64_t aData);
nsresult RecordAnnotationUSize(Annotation aKey, size_t aData);
nsresult RecordAnnotationCString(Annotation aKey, const char* aData);
nsresult RecordAnnotationNSCString(Annotation aKey, const nsACString& aData);
nsresult RecordAnnotationNSString(Annotation aKey, const nsAString& aData);
nsresult UnrecordAnnotation(Annotation aKey);

nsresult AppendAppNotesToCrashReport(const nsACString& data);

// RAII class for setting a crash annotation during a limited scope of time.
// Will reset the named annotation to its previous value when destroyed.
//
// This type's behavior is identical to that of RecordAnnotation().
class MOZ_RAII AutoRecordAnnotation final {
 public:
  AutoRecordAnnotation(Annotation key, bool data);
  AutoRecordAnnotation(Annotation key, int data);
  AutoRecordAnnotation(Annotation key, unsigned int data);
  AutoRecordAnnotation(Annotation key, const nsACString& data);
  ~AutoRecordAnnotation();

#ifdef MOZ_CRASHREPORTER
 private:
  Annotation mKey;
  const nsCString mCurrent;
  const nsCString* mPrevious;
#endif
};

void AnnotateOOMAllocationSize(size_t size);
void AnnotateTexturesSize(size_t size);
nsresult SetGarbageCollecting(bool collecting);
void SetEventloopNestingLevel(uint32_t level);
void SetMinidumpAnalysisAllThreads();
void ClearInactiveStateStart();
void SetInactiveStateStart();

nsresult SetRestartArgs(int argc, char** argv);
nsresult SetupExtraData(nsIFile* aAppDataDirectory, const nsACString& aBuildID);
// Registers an additional memory region to be included in the minidump
nsresult RegisterAppMemory(void* ptr, size_t length);
nsresult UnregisterAppMemory(void* ptr);

// Include heap regions of the crash context.
void SetIncludeContextHeap(bool aValue);

// Functions for working with minidumps and .extras
typedef mozilla::EnumeratedArray<Annotation, nsCString,
                                 size_t(Annotation::Count)>
    AnnotationTable;
void DeleteMinidumpFilesForID(
    const nsAString& aId,
    const Maybe<nsString>& aAdditionalMinidump = Nothing());
bool GetMinidumpForID(const nsAString& id, nsIFile** minidump,
                      const Maybe<nsString>& aAdditionalMinidump = Nothing());
bool GetIDFromMinidump(nsIFile* minidump, nsAString& id);
bool GetExtraFileForID(const nsAString& id, nsIFile** extraFile);
bool GetExtraFileForMinidump(nsIFile* minidump, nsIFile** extraFile);
bool WriteExtraFile(const nsAString& id, const AnnotationTable& annotations);

/**
 * Copies the non-empty annotations in the source table to the destination
 * overwriting the corresponding entries.
 */
void MergeCrashAnnotations(AnnotationTable& aDst, const AnnotationTable& aSrc);

#ifdef XP_WIN
nsresult WriteMinidumpForException(EXCEPTION_POINTERS* aExceptionInfo);
#endif
#ifdef XP_LINUX
bool WriteMinidumpForSigInfo(int signo, siginfo_t* info, void* uc);
#endif
#ifdef XP_MACOSX
nsresult AppendObjCExceptionInfoToAppNotes(void* inException);
#endif
nsresult GetSubmitReports(bool* aSubmitReport);
nsresult SetSubmitReports(bool aSubmitReport);

// Out-of-process crash reporter API.

// Return true if a dump was found for |childPid|, and return the
// path in |dump|.  The caller owns the last reference to |dump| if it
// is non-nullptr. The annotations for the crash will be stored in
// |aAnnotations|.
bool TakeMinidumpForChild(ProcessId childPid, nsIFile** dump,
                          AnnotationTable& aAnnotations);

/**
 * If a dump was found for |childPid| then write a minimal .extra file to
 * complete it and remove it from the list of pending crash dumps. It's
 * required to call this method after a non-main process crash if the crash
 * report could not be finalized via the CrashReporterHost (for example because
 * it wasn't instanced yet).
 *
 * @param aChildPid The pid of the crashed child process
 * @param aType The type of the crashed process
 * @param aDumpId A string that will be filled with the dump ID
 */
[[nodiscard]] bool FinalizeOrphanedMinidump(ProcessId aChildPid,
                                            GeckoProcessType aType,
                                            nsString* aDumpId = nullptr);

// Return the current thread's ID.
//
// XXX: this is a somewhat out-of-place interface to expose through
// crashreporter, but it takes significant work to call sys_gettid()
// correctly on Linux and breakpad has already jumped through those
// hoops for us.
ThreadId CurrentThreadId();

/*
 * Take a minidump of the target process and pair it with a new minidump of the
 * calling process and thread. The caller will own both dumps after this call.
 * If this function fails it will attempt to delete any files that were created.
 *
 * The .extra information created will not include an 'additional_minidumps'
 * annotation.
 *
 * @param aTargetPid The target process for the minidump.
 * @param aTargetBlamedThread The target thread for the minidump.
 * @param aIncomingPairName The name to apply to the paired dump the caller
 *   passes in.
 * @param aTargetDumpOut The target minidump file paired up with the new one.
 * @param aTargetAnnotations The crash annotations of the target process.
 * @return bool indicating success or failure
 */
bool CreateMinidumpsAndPair(ProcessHandle aTargetPid,
                            ThreadId aTargetBlamedThread,
                            const nsACString& aIncomingPairName,
                            AnnotationTable& aTargetAnnotations,
                            nsIFile** aTargetDumpOut);

#if defined(XP_WIN) || defined(XP_MACOSX) || defined(XP_IOS)
using CrashPipeType = const char*;
#else
using CrashPipeType = mozilla::UniqueFileHandle;
#endif

// Parent-side API for children
#if defined(MOZ_WIDGET_ANDROID)
void SetCrashHelperPipes(FileHandle breakpadFd, FileHandle crashHelperFd);
#endif
CrashPipeType GetChildNotificationPipe();

#if defined(XP_LINUX) && !defined(MOZ_WIDGET_ANDROID)

// Return the pid of the crash helper process.
MOZ_EXPORT ProcessId GetCrashHelperPid();

#endif  // XP_LINUX && !defined(MOZ_WIDGET_ANDROID)

// Child-side API
MOZ_EXPORT bool SetRemoteExceptionHandler(CrashPipeType aCrashPipe,
                                          ProcessId aCrashHelperPid = 0);
bool UnsetRemoteExceptionHandler(bool wasSet = true);

}  // namespace CrashReporter

#endif /* nsExceptionHandler_h__ */
