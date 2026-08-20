/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/IOInterposer.h"
#include "mozilla/PoisonIOInterposer.h"
#include "mozilla/ProcessedStack.h"
#include "mozilla/SHA1.h"
#include "mozilla/StackWalk.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/Telemetry.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsDirectoryServiceUtils.h"
#include "nsLocalFile.h"
#include "nsPrintfCString.h"
#include "prio.h"

#ifdef XP_WIN
#  define NS_SLASH "\\"
#  include <fcntl.h>
#  include <io.h>
#  include <stdio.h>
#  include <stdlib.h>
#  include <sys/stat.h>
#  include <windows.h>
#else
#  define NS_SLASH "/"
#endif

#include "LateWriteChecks.h"

/*************************** Auxiliary Declarations ***************************/

static MOZ_THREAD_LOCAL(int) tlsSuspendLateWriteChecks;

bool SuspendingLateWriteChecksForCurrentThread() {
  if (!tlsSuspendLateWriteChecks.init()) {
    return true;
  }
  return tlsSuspendLateWriteChecks.get() > 0;
}

// This a wrapper over a file descriptor that provides a Printf method and
// computes the sha1 of the data that passes through it.
class SHA1Stream {
 public:
  explicit SHA1Stream(FILE* aStream) : mFile(aStream) {
    MozillaRegisterDebugFILE(mFile);
  }

  void Printf(const char* aFormat, ...) MOZ_FORMAT_PRINTF(2, 3) {
    MOZ_ASSERT(mFile);
    va_list list;
    va_start(list, aFormat);
    nsAutoCString str;
    str.AppendVprintf(aFormat, list);
    va_end(list);
    mSHA1.update(str.get(), str.Length());
    (void)fwrite(str.get(), 1, str.Length(), mFile);
  }
  void Finish(mozilla::SHA1Sum::Hash& aHash) {
    int fd = fileno(mFile);
    fflush(mFile);
    MozillaUnRegisterDebugFD(fd);
    fclose(mFile);
    mSHA1.finish(aHash);
    mFile = nullptr;
  }

 private:
  FILE* mFile;
  mozilla::SHA1Sum mSHA1;
};

static void RecordStackWalker(uint32_t aFrameNumber, void* aPC, void* aSP,
                              void* aClosure) {
  std::vector<uintptr_t>* stack =
      static_cast<std::vector<uintptr_t>*>(aClosure);
  stack->push_back(reinterpret_cast<uintptr_t>(aPC));
}

/**************************** Late-Write Observer  ****************************/

/**
 * An implementation of IOInterposeObserver to be registered with IOInterposer.
 * This observer logs all writes as late writes.
 */
class LateWriteObserver final : public mozilla::IOInterposeObserver {
  using char_type = mozilla::filesystem::Path::value_type;

 public:
  explicit LateWriteObserver(const char_type* aProfileDirectory)
      : mProfileDirectory(NS_xstrdup(aProfileDirectory)) {}
  ~LateWriteObserver() {
    free(mProfileDirectory);
    mProfileDirectory = nullptr;
  }

  void Observe(
      mozilla::IOInterposeObserver::Observation& aObservation) override;

 private:
  char_type* mProfileDirectory;
};

void LateWriteObserver::Observe(
    mozilla::IOInterposeObserver::Observation& aOb) {
  if (SuspendingLateWriteChecksForCurrentThread()) {
    return;
  }

#ifdef DEBUG
  MOZ_CRASH();
#endif

  // If we can't record then abort
  if (!mozilla::Telemetry::CanRecordExtended()) {
    return;
  }

  // Write the stack and loaded libraries to a file. We can get here
  // concurrently from many writes, so we use multiple temporary files.
  std::vector<uintptr_t> rawStack;

  MozStackWalk(RecordStackWalker, nullptr, /* maxFrames */ 0, &rawStack);
  mozilla::Telemetry::ProcessedStack stack =
      mozilla::Telemetry::GetStackAndModules(rawStack);

  nsTAutoString<char_type> nameAux(mProfileDirectory);
  nameAux.AppendLiteral(NS_SLASH "Telemetry.LateWriteTmpXXXXXX");
  char_type* name = nameAux.BeginWriting();

  // We want the sha1 of the entire file, so please don't write to fd
  // directly; use sha1Stream.
  FILE* stream;
#ifdef XP_WIN
  HANDLE hFile;
  do {
    // mkstemp isn't supported so keep trying until we get a file
    _wmktemp_s(char16ptr_t(name), NS_strlen(name) + 1);
    hFile = CreateFileW(char16ptr_t(name), GENERIC_WRITE, 0, nullptr,
                        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  } while (GetLastError() == ERROR_FILE_EXISTS);

  if (hFile == INVALID_HANDLE_VALUE) {
    MOZ_CRASH("Um, how did we get here?");
  }

  // http://support.microsoft.com/kb/139640
  int fd = _open_osfhandle((intptr_t)hFile, _O_APPEND);
  if (fd == -1) {
    MOZ_CRASH("Um, how did we get here?");
  }

  stream = _fdopen(fd, "w");
#else
  int fd = mkstemp(name);
  if (fd == -1) {
    MOZ_CRASH("mkstemp failed");
  }
  stream = fdopen(fd, "w");
#endif

  SHA1Stream sha1Stream(stream);

  size_t numModules = stack.GetNumModules();
  sha1Stream.Printf("%u\n", (unsigned)numModules);
  for (size_t i = 0; i < numModules; ++i) {
    const mozilla::Telemetry::ProcessedStack::Module& module =
        stack.GetModule(i);
    sha1Stream.Printf("%s %s\n", module.mBreakpadId.get(),
                      NS_ConvertUTF16toUTF8(module.mName).get());
  }

  size_t numFrames = stack.GetStackSize();
  sha1Stream.Printf("%u\n", (unsigned)numFrames);
  for (size_t i = 0; i < numFrames; ++i) {
    const mozilla::Telemetry::ProcessedStack::Frame& frame = stack.GetFrame(i);
    // NOTE: We write the offsets, while the atos tool expects a value with
    // the virtual address added. For example, running otool -l on the the
    // firefox binary shows
    //      cmd LC_SEGMENT_64
    //      cmdsize 632
    //      segname __TEXT
    //      vmaddr 0x0000000100000000
    // so to print the line matching the offset 123 one has to run
    // atos -o firefox 0x100000123.
    sha1Stream.Printf("%d %x\n", frame.mModIndex, (unsigned)frame.mOffset);
  }

  mozilla::SHA1Sum::Hash sha1;
  sha1Stream.Finish(sha1);

  // Note: These files should be deleted by telemetry once it reads them. If
  // there were no telemetry runs by the time we shut down, we just add files
  // to the existing ones instead of replacing them. Given that each of these
  // files is a bug to be fixed, that is probably the right thing to do.

  // We append the sha1 of the contents to the file name. This provides a simple
  // client side deduplication.
  nsAutoString finalName(u"Telemetry.LateWriteFinal-"_ns);
  for (unsigned char c : sha1) {
    finalName.AppendPrintf("%02x", c);
  }
  RefPtr<nsIFile> file;
  if (NS_SUCCEEDED(NS_NewPathStringLocalFile(nameAux, getter_AddRefs(file)))) {
    file->RenameTo(nullptr, finalName);
  }
}

/******************************* Setup/Teardown *******************************/

static mozilla::StaticAutoPtr<LateWriteObserver> sLateWriteObserver;

namespace mozilla {

void InitLateWriteChecks() {
  nsCOMPtr<nsIFile> mozFile;
  NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR, getter_AddRefs(mozFile));
  if (mozFile) {
    PathString nativePath = mozFile->NativePath();
    if (nativePath.get()) {
      sLateWriteObserver = new LateWriteObserver(nativePath.get());
    }
  }
}

void BeginLateWriteChecks() {
  if (sLateWriteObserver) {
    IOInterposer::Register(IOInterposeObserver::OpWriteFSync,
                           sLateWriteObserver);
  }
}

void StopLateWriteChecks() {
  if (sLateWriteObserver) {
    IOInterposer::Unregister(IOInterposeObserver::OpAll, sLateWriteObserver);
    // Deallocation would not be thread-safe, and StopLateWriteChecks() is
    // called at shutdown and only in special cases.
    // sLateWriteObserver = nullptr;
  }
}

void PushSuspendLateWriteChecks() {
  if (!tlsSuspendLateWriteChecks.init()) {
    return;
  }
  tlsSuspendLateWriteChecks.set(tlsSuspendLateWriteChecks.get() + 1);
}

void PopSuspendLateWriteChecks() {
  if (!tlsSuspendLateWriteChecks.init()) {
    return;
  }
  int current = tlsSuspendLateWriteChecks.get();
  MOZ_ASSERT(current > 0);
  tlsSuspendLateWriteChecks.set(current - 1);
}

}  // namespace mozilla
