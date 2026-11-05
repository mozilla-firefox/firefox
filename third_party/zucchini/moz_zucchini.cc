/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/moz_zucchini.h"

#include "absl/types/optional.h"
#include "base/logging.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "build/build_config.h"
#include "components/zucchini/crc32.h"
#include "components/zucchini/mapped_file.h"
#include "components/zucchini/patch_reader.h"
#include "components/zucchini/zucchini.h"
#include "components/zucchini/zucchini_apply.h"

#include <cstdio>
#include <limits>
#include <memory>
#include <new>
#include <string>

#if BUILDFLAG(IS_WIN)
// Get EXCEPTION_* constants defined with DWORD type
#  include <ntstatus.h>

#  if defined(HAVE_SEH_EXCEPTIONS)
#    include "components/zucchini/exception_filter_helper_win.h"
#  endif  // HAVE_SEH_EXCEPTIONS

#  include <io.h>
#endif  // BUILDFLAG(IS_WIN)

namespace zucchini::mozilla {

#ifdef ENABLE_TESTS
// Helpers for crash test scenarios.

static TestOptions gTestOptions;

void SetTestOptions(const TestOptions& aTestOptions) {
  gTestOptions = aTestOptions;
}

// Test-only log to prove stack unwinding reaches local destructors when we
// recover from a fault originating from inside zucchini.
class ScopedDestructorMarker {
 public:
  ScopedDestructorMarker() = default;

  ~ScopedDestructorMarker() {
    if (!gTestOptions.logDestructorMarker) {
      return;
    }

    LOG(ERROR) << "MOZ_TEST_ZUCCHINI_DTOR_MARKER";
  }
};

// Test-only OOM injection used by updater tests to verify that a thrown
// bad_alloc is translated into kStatusOutOfMemory.
static void MaybeTriggerTestBadAlloc() {
  if (!gTestOptions.triggerBadAlloc) {
    return;
  }

  // This allocation is expected to throw std::bad_alloc. volatile prevents
  // the compiler from optimizing it away. If it somehow succeeded, CHECK(false)
  // ensures the test fails visibly rather than silently passing.
  void* volatile allocation = ::operator new(std::numeric_limits<size_t>::max());
  (void)allocation;
  CHECK(false);
}

// Test-only CHECK() injection used by updater tests to verify that deliberate
// crash paths are converted into kStatusFatal.
static void MaybeTriggerTestCheckFailure() {
  if (!gTestOptions.triggerCheckFailure) {
    return;
  }

  CHECK(false);
}

// Test-only injection of the exception that Windows raises when accessing a
// mapped file fails, used by updater tests to verify that we recover from it
// and release the mapped files. Writes fail because the disk is full, reads
// because of a device error.
static void MaybeTriggerTestPageError(bool aTrigger, const uint8_t* aAddress,
                                      bool aIsWrite) {
  if (!aTrigger) {
    return;
  }

#  if BUILDFLAG(IS_WIN) && defined(HAVE_SEH_EXCEPTIONS)
  const ULONG_PTR info[3] = {
      aIsWrite, reinterpret_cast<ULONG_PTR>(aAddress),
      static_cast<ULONG_PTR>(aIsWrite ? STATUS_DISK_FULL
                                      : STATUS_DEVICE_DATA_ERROR)};
  ::RaiseException(EXCEPTION_IN_PAGE_ERROR, 0, 3, info);
#  endif  // BUILDFLAG(IS_WIN) && HAVE_SEH_EXCEPTIONS
}
#endif  // ENABLE_TESTS

static LogFunctionPtr gLogFunction = nullptr;

static bool LogMessageHandler(int aSeverity, const char* aFile, int aLine,
                              size_t aMessageStart, const std::string& aStr) {
  if (!gLogFunction) {
    return false;
  }
  gLogFunction(aStr.c_str());
  return true;
}

void SetLogFunction(LogFunctionPtr aLogFunction) {
  gLogFunction = aLogFunction;
  logging::SetLogMessageHandler(LogMessageHandler);
}

#if !defined(__cpp_exceptions)
#  error The zucchini interface code requires compiler support for C++ exceptions.
#endif  // !cpp_exceptions


// Catch C++ exceptions raised within zucchini code. This lets the updater
// recover from standard library allocation failures, so it can write an OOM
// update.status and run post-failure cleanup. We wrap the entire body of
// every entry point reachable from updater.cpp, making sure we cover all the
// zucchini code it uses.
//
// Every entry point into zucchini thus returns a status code. Running into a
// C++ std::bad_alloc exception returns kStatusOutOfMemory. Any other C++
// exception results in kStatusFatal, although that should never happen.

#define BEGIN_ENTRY_POINT() try {
#define END_ENTRY_POINT()                                                 \
  }                                                                       \
  catch (const std::bad_alloc&) {                                         \
    LOG(ERROR) << "std::bad_alloc caught in zucchini.";                   \
    return status::kStatusOutOfMemory;                                    \
  }                                                                       \
  catch (...) {                                                           \
    LOG(ERROR) << "unknown exception caught in zucchini; this is a bug."; \
    return status::kStatusFatal;                                          \
  }

#if BUILDFLAG(IS_WIN) && defined(HAVE_SEH_EXCEPTIONS)
static status::Code PageErrorStatus(const ExceptionFilterHelper& aHelper) {
  LOG(ERROR) << "EXCEPTION_IN_PAGE_ERROR while "
             << (aHelper.is_write() ? "writing to" : "reading from")
             << " mapped files; NTSTATUS = " << aHelper.nt_status();
  return aHelper.nt_status() == STATUS_DISK_FULL ? status::kStatusDiskFull
                                                 : status::kStatusIoError;
}

// Narrow handler that stays around the code touching the mapped file ranges,
// where EXCEPTION_IN_PAGE_ERROR can be raised. Usable only within member
// functions, as it relies on mImpl. This reflects the exception catching
// present in upstream zucchini::ApplyCommon (zucchini_integration.cc).
// Objects that need unwinding must live outside of aFunc: the unwinding that
// follows an EXCEPTION_IN_PAGE_ERROR doesn't run their destructors.
template <typename F>
static status::Code GuardPageErrors(ExceptionFilterHelper& aHelper, F& aFunc) {
  __try {
    return aFunc();
  } __except (
      aHelper.FilterPageError(GetExceptionInformation()->ExceptionRecord)) {
    return PageErrorStatus(aHelper);
  }
}
#  define GUARD_PAGE_ERRORS(aFunc) \
    GuardPageErrors(mImpl->mExceptionFilterHelper, aFunc)
#else
#  define GUARD_PAGE_ERRORS(aFunc) aFunc()
#endif  // BUILDFLAG(IS_WIN) && HAVE_SEH_EXCEPTIONS

status::Code ComputeCrc32(const uint8_t* aBuf, size_t aBufSize,
                          uint32_t& aOutCrc32) {
  BEGIN_ENTRY_POINT()
  aOutCrc32 = CalculateCrc32(aBuf, aBuf + aBufSize);
  return status::kStatusSuccess;
  END_ENTRY_POINT()
}

class MappedPatchImpl {
 public:
  MappedPatchImpl() = default;
  ~MappedPatchImpl() = default;
  std::optional<MappedFileReader> mFileReader;
  EnsemblePatchReader mPatchReader;
#if BUILDFLAG(IS_WIN) && defined(HAVE_SEH_EXCEPTIONS)
  ExceptionFilterHelper mExceptionFilterHelper;
#endif  // BUILDFLAG(IS_WIN) && HAVE_SEH_EXCEPTIONS
};

status::Code MappedPatch::Initialize() {
  BEGIN_ENTRY_POINT()
  mImpl = new MappedPatchImpl();
  return status::kStatusSuccess;
  END_ENTRY_POINT()
}

status::Code MappedPatch::Finalize() {
  BEGIN_ENTRY_POINT()
  auto* impl = mImpl;
  mImpl = nullptr;
  delete impl;
  return status::kStatusSuccess;
  END_ENTRY_POINT()
}

// This corresponds to the first half of zucchini::ApplyCommon.
status::Code MappedPatch::LoadImpl(FILE* aPatchFile, uint32_t* aSourceSize,
                                   uint32_t* aDestinationSize,
                                   uint32_t* aSourceCrc32) {
  base::File patchFile = base::FILEToFile(aPatchFile);
  if (!patchFile.IsValid()) {
    LOG(ERROR) << "Invalid patch file.";
    return status::kStatusFileReadError;
  }
  mImpl->mFileReader.emplace(std::move(patchFile));
  auto& fileReader = *mImpl->mFileReader;
  if (fileReader.HasError()) {
    LOG(ERROR) << "Error with patch file: " << fileReader.error();
    if (fileReader.error_is_oom()) {
      return status::kStatusOutOfMemory;
    }
    return status::kStatusFileReadError;
  }
#if BUILDFLAG(IS_WIN) && defined(HAVE_SEH_EXCEPTIONS)
  mImpl->mExceptionFilterHelper.AddRange(
      {fileReader.data(), fileReader.length()});
#endif  // BUILDFLAG(IS_WIN) && HAVE_SEH_EXCEPTIONS
  auto readHeader = [&]() {
#ifdef ENABLE_TESTS
    MaybeTriggerTestPageError(gTestOptions.triggerReadPageError,
                              fileReader.data(), /* aIsWrite */ false);
#endif  // ENABLE_TESTS
    BufferSource source(fileReader.region());
    auto& patchReader = mImpl->mPatchReader;
    if (!patchReader.Initialize(&source)) {
      LOG(ERROR) << "Error reading patch header.";
      return status::kStatusPatchReadError;
    }
    auto& header = patchReader.header();
    *aSourceSize = header.old_size;
    *aDestinationSize = header.new_size;
    *aSourceCrc32 = header.old_crc;
    return status::kStatusSuccess;
  };
  return GUARD_PAGE_ERRORS(readHeader);
}

status::Code MappedPatch::Load(FILE* aPatchFile, uint32_t* aSourceSize,
                               uint32_t* aDestinationSize,
                               uint32_t* aSourceCrc32) {
  if (!mImpl) {
    return mInitStatus;
  }
  BEGIN_ENTRY_POINT()
  return LoadImpl(aPatchFile, aSourceSize, aDestinationSize, aSourceCrc32);
  END_ENTRY_POINT()
}

// This is zucchini::ApplyBuffer, except that we *assume* that aCheckedOldImage
// has the correct size and crc32 instead of checking it.
static status::Code ApplyBufferUnsafe(ConstBufferView aCheckedOldImage,
                                      const EnsemblePatchReader& aPatchReader,
                                      MutableBufferView aNewImage) {
#ifdef ENABLE_TESTS
  // We want updater tests to simulate zucchini failures occurring in the worst
  // possible location from the updater recovery's point of view. Failing here
  // simulates a failure in the middle of patch application, with the new image
  // still open and mapped, which makes recovery quite subtle.
  ScopedDestructorMarker destructorTester;
  MaybeTriggerTestBadAlloc();
  MaybeTriggerTestCheckFailure();
  MaybeTriggerTestPageError(gTestOptions.triggerWritePageError,
                            aNewImage.begin(), /* aIsWrite */ true);
#endif  // ENABLE_TESTS

  for (const auto& elementPatch : aPatchReader.elements()) {
    ElementMatch match = elementPatch.element_match();
    if (!ApplyElement(match.exe_type(),
                      aCheckedOldImage[match.old_element.region()],
                      elementPatch, aNewImage[match.new_element.region()]))
      return status::kStatusFatal;
  }

  if (!aPatchReader.CheckNewFile(ConstBufferView(aNewImage))) {
    LOG(ERROR) << "Invalid aNewImage.";
    return status::kStatusInvalidNewImage;
  }

  return status::kStatusSuccess;
}

// This corresponds to the second half of zucchini::ApplyCommon.
status::Code MappedPatch::ApplyUnsafeImpl(const uint8_t* aCheckedOldImage,
                                          size_t aCheckedOldImageSize,
                                          FILE* aNewFile) {
  ConstBufferView oldImageView(aCheckedOldImage, aCheckedOldImageSize);

  base::File newFile = base::FILEToFile(aNewFile);
  if (!newFile.IsValid()) {
    LOG(ERROR) << "Invalid new file.";
    return status::kStatusFileWriteError;
  }

  PatchHeader header = mImpl->mPatchReader.header();
  base::FilePath name = base::FilePath().AppendASCII("old_name");
  MappedFileWriter mappedNew(name, std::move(newFile), header.new_size,
                             /*keep*/ true);
  if (mappedNew.HasError()) {
    LOG(ERROR) << "Error with new file: " << mappedNew.error();
    if (mappedNew.error_is_oom()) {
      return status::kStatusOutOfMemory;
    }
    return status::kStatusFileWriteError;
  }

#if BUILDFLAG(IS_WIN) && defined(HAVE_SEH_EXCEPTIONS)
  mImpl->mExceptionFilterHelper.AddRange(
      {mappedNew.data(), mappedNew.length()});
#endif  // BUILDFLAG(IS_WIN) && HAVE_SEH_EXCEPTIONS

  auto applyToNewFile = [&]() {
    status::Code result = ApplyBufferUnsafe(oldImageView, mImpl->mPatchReader,
                                            mappedNew.region());
    if (result != status::kStatusSuccess) {
      LOG(ERROR) << "Fatal error encountered while applying patch.";
      return result;
    }

    if (!mappedNew.Flush()) {
      LOG(ERROR) << "Error flushing changes to disk: " << mappedNew.error();
      return status::kStatusFileWriteError;
    }

    return status::kStatusSuccess;
  };
  return GUARD_PAGE_ERRORS(applyToNewFile);
}

status::Code MappedPatch::ApplyUnsafe(const uint8_t* aCheckedOldImage,
                                      size_t aCheckedOldImageSize,
                                      FILE* aNewFile) {
  if (!mImpl) {
    return mInitStatus;
  }
  BEGIN_ENTRY_POINT()
  return ApplyUnsafeImpl(aCheckedOldImage, aCheckedOldImageSize, aNewFile);
  END_ENTRY_POINT()
}

}  // namespace zucchini::mozilla
