/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsExceptionHandler.h"

namespace CrashReporter {

void AnnotateOOMAllocationSize(size_t size) {}

void AnnotateTexturesSize(size_t size) {}

void AnnotatePendingIPC(size_t aNumOfPendingIPC, uint32_t aTopPendingIPCCount,
                        const char* aTopPendingIPCName,
                        uint32_t aTopPendingIPCType) {}

nsresult SetExceptionHandler(nsIFile* aXREDirectory, bool force /*=false*/) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

bool GetEnabled() { return false; }

bool GetMinidumpPath(nsAString& aPath) { return false; }

nsresult SetMinidumpPath(const nsAString& aPath) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult SetupExtraData(nsIFile* aAppDataDirectory,
                        const nsACString& aBuildID) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult UnsetExceptionHandler() { return NS_ERROR_NOT_IMPLEMENTED; }

const bool* RegisterAnnotationBool(Annotation aKey, const bool* aData) {
  return nullptr;
}

const uint32_t* RegisterAnnotationU32(Annotation aKey, const uint32_t* aData) {
  return nullptr;
}

const uint64_t* RegisterAnnotationU64(Annotation aKey, const uint64_t* aData) {
  return nullptr;
}

const size_t* RegisterAnnotationUSize(Annotation aKey, const size_t* aData) {
  return nullptr;
}

const char* RegisterAnnotationCString(Annotation aKey, const char* aData) {
  return nullptr;
}

const nsCString* RegisterAnnotationNSCString(Annotation aKey,
                                             const nsCString* aData) {
  return nullptr;
}

nsresult RecordAnnotationBool(Annotation aKey, bool aData) {
  return NS_ERROR_FAILURE;
}

nsresult RecordAnnotationU32(Annotation aKey, uint32_t aData) {
  return NS_ERROR_FAILURE;
}

nsresult RecordAnnotationU64(Annotation aKey, uint64_t aData) {
  return NS_ERROR_FAILURE;
}

nsresult RecordAnnotationUSize(Annotation aKey, size_t aData) {
  return NS_ERROR_FAILURE;
}

nsresult RecordAnnotationCString(Annotation aKey, const char* aData) {
  return NS_ERROR_FAILURE;
}

nsresult RecordAnnotationNSCString(Annotation aKey, const nsACString& aData) {
  return NS_ERROR_FAILURE;
}

nsresult RecordAnnotationNSString(Annotation aKey, const nsAString& aData) {
  return NS_ERROR_FAILURE;
}

nsresult UnrecordAnnotation(Annotation aKey) { return NS_ERROR_FAILURE; }

AutoRecordAnnotation::AutoRecordAnnotation(Annotation key, bool data) {}

AutoRecordAnnotation::AutoRecordAnnotation(Annotation key, int data) {}

AutoRecordAnnotation::AutoRecordAnnotation(Annotation key, unsigned data) {}

AutoRecordAnnotation::AutoRecordAnnotation(Annotation key,
                                           const nsACString& data) {}

AutoRecordAnnotation::~AutoRecordAnnotation() {}

void MergeCrashAnnotations(AnnotationTable& aDst, const AnnotationTable& aSrc) {
}

nsresult SetGarbageCollecting(bool collecting) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

void SetEventloopNestingLevel(uint32_t level) {}

void SetMinidumpAnalysisAllThreads() {}

nsresult AppendAppNotesToCrashReport(const nsACString& data) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

bool GetAnnotation(const nsACString& key, nsACString& data) { return false; }

nsresult RegisterAppMemory(void* ptr, size_t length) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult UnregisterAppMemory(void* ptr) { return NS_ERROR_NOT_IMPLEMENTED; }

void SetIncludeContextHeap(bool aValue) {}

bool GetServerURL(nsACString& aServerURL) { return false; }

nsresult SetServerURL(const nsACString& aServerURL) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult SetRestartArgs(int argc, char** argv) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

#ifdef XP_WIN
nsresult WriteMinidumpForException(EXCEPTION_POINTERS* aExceptionInfo) {
  return NS_ERROR_NOT_IMPLEMENTED;
}
#endif

#ifdef XP_LINUX
bool WriteMinidumpForSigInfo(int signo, siginfo_t* info, void* uc) {
  return false;
}
#endif

#ifdef XP_MACOSX
nsresult AppendObjCExceptionInfoToAppNotes(void* inException) {
  return NS_ERROR_NOT_IMPLEMENTED;
}
#endif

nsresult GetSubmitReports(bool* aSubmitReports) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult SetSubmitReports(bool aSubmitReports) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

void SetProfileDirectory(nsIFile* aDir) {}

void SetUserAppDataDirectory(nsIFile* aDir) {}

void UpdateCrashEventsDir() {}

bool GetCrashEventsDir(nsAString& aPath) { return false; }

void SetMemoryReportFile(nsIFile* aFile) {}

nsresult GetDefaultMemoryReportFile(nsIFile** aFile) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

void DeleteMinidumpFilesForID(const nsAString& aId,
                              const Maybe<nsString>& aAdditionalMinidump) {}

bool GetMinidumpForID(const nsAString& id, nsIFile** minidump,
                      const Maybe<nsString>& aAdditionalMinidump) {
  return false;
}

bool GetIDFromMinidump(nsIFile* minidump, nsAString& id) { return false; }

bool GetExtraFileForID(const nsAString& id, nsIFile** extraFile) {
  return false;
}

bool GetExtraFileForMinidump(nsIFile* minidump, nsIFile** extraFile) {
  return false;
}

bool WriteExtraFile(const nsAString& id, const AnnotationTable& annotations) {
  return false;
}

#if defined(MOZ_WIDGET_ANDROID)
void SetNotificationPipeForChild(FileHandle breakpadFd,
                                 FileHandle crashHelperFd) {}
#endif  // defined(MOZ_WIDGET_ANDROID)

CrashPipeType GetChildNotificationPipe() { return nullptr; }

#if defined(MOZ_WIDGET_ANDROID)
void SetCrashHelperPipes(FileHandle breakpadFd, FileHandle crashHelperFd) {}
#endif // defined(MOZ_WIDGET_ANDROID)

#if defined(XP_LINUX) && !defined(MOZ_WIDGET_ANDROID)
MOZ_EXPORT ProcessId GetCrashHelperPid() { return -1; };
#endif  // XP_LINUX && !defined(MOZ_WIDGET_ANDROID)

bool GetLastRunCrashID(nsAString& id) { return false; }

bool SetRemoteExceptionHandler(CrashPipeType aCrashPipe,
                               ProcessId aCrashHelperPid) {
  return false;
}

bool TakeMinidumpForChild(ProcessId childPid, nsIFile** dump,
                          AnnotationTable& aAnnotations) {
  return false;
}

bool FinalizeOrphanedMinidump(ProcessId aChildPid, GeckoProcessType aType,
                              nsString* aDumpId) {
  return false;
}

#if defined(XP_WIN)

DWORD WINAPI WerNotifyProc(LPVOID aParameter) { return 0; }

#endif  // defined(XP_WIN)

ThreadId CurrentThreadId() { return -1; }

bool TakeMinidump(nsIFile** aResult, bool aMoveToPending) { return false; }

bool CreateMinidumpsAndPair(ProcessHandle aTargetPid,
                            ThreadId aTargetBlamedThread,
                            const nsACString& aIncomingPairName,
                            AnnotationTable& aTargetAnnotations,
                            nsIFile** aTargetDumpOut) {
  return false;
}

bool UnsetRemoteExceptionHandler(bool wasSet) { return false; }

#if defined(MOZ_WIDGET_ANDROID)
void SetNotificationPipeForChild(FileHandle childCrashFd) {}

void AddLibraryMapping(const char* library_name, uintptr_t start_address,
                       size_t mapping_length, size_t file_offset) {}
#endif

}  // namespace CrashReporter
