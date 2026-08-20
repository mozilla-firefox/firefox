/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <signal.h>
#include <time.h>

#include <cstdint>

#include "mozilla/ProfilerState.h"
#include "mozilla/SharedLibraries.h"
#include "platform.h"
#include "mozilla/ProfilerPlatformMacros.h"
#include "LulMain.h"
#include "AutoObjectMapper.h"

#if defined(GP_OS_android)
#  include <cstdio>
#  include <fstream>
#  include <string>
#endif

// Contains miscellaneous helpers that are used to connect the Gecko Profiler
// and LUL.

#if defined(GP_OS_android)
// When Android's "legacy packaging" is disabled, the dynamic linker loads
// native libraries (e.g. libxul.so) directly from inside the APK without
// extracting them to disk. dl_iterate_phdr then reports their path using the
// "<apk-path>!/<library-path-inside-apk>" syntax, which cannot be passed to
// open(). Such libraries are stored uncompressed and page-aligned inside the
// APK, so we can still map them by opening the APK and mapping at the library's
// offset within it. That offset isn't available from dl_iterate_phdr, but it is
// the file offset of the mapping that backs the library's first loaded page, so
// we recover it from /proc/self/maps. Returns true and sets |aOffsetOut| on
// success.
static bool GetApkEmbeddedLibraryOffset(uintptr_t aLibStart,
                                        uint64_t* aOffsetOut) {
  std::ifstream maps("/proc/self/maps");
  std::string line;
  while (std::getline(maps, line)) {
    unsigned long start = 0;
    unsigned long end = 0;
    unsigned long offset = 0;
    if (sscanf(line.c_str(), "%lx-%lx %*s %lx", &start, &end, &offset) == 3 &&
        aLibStart >= start && aLibStart < end) {
      *aOffsetOut = offset;
      return true;
    }
  }
  return false;
}
#endif

// Find out, in a platform-dependent way, where the code modules got
// mapped in the process' virtual address space, and get |aLUL| to
// load unwind info for them.
void read_procmaps(lul::LUL* aLUL) {
  MOZ_ASSERT(aLUL->CountMappings() == 0);

#if defined(GP_OS_linux) || defined(GP_OS_android) || defined(GP_OS_freebsd)
  SharedLibraryInfo info = SharedLibraryInfo::GetInfoForSelf();

  for (size_t i = 0; i < info.GetSize(); i++) {
    const SharedLibrary& lib = info.GetEntry(i);

    uint64_t fileOffset = 0;
    bool isApkEmbedded = false;

#  if defined(GP_OS_android)
    std::string nativePath = lib.GetDebugPath();
    // Handle libraries loaded directly from inside the APK, whose path uses the
    // "<apk>!/<entry>" syntax (see GetApkEmbeddedLibraryOffset). We map the APK
    // file at the embedded library's offset instead of the unopenable path.
    size_t apkSeparator = nativePath.find("!/");
    if (apkSeparator != std::string::npos) {
      isApkEmbedded = true;
      if (!GetApkEmbeddedLibraryOffset(lib.GetStart(), &fileOffset)) {
        // We couldn't determine the library's offset within the APK, so we
        // can't read its unwind information. Still notify LUL of the executable
        // area so that stack scanning works.
        aLUL->NotifyExecutableArea(lib.GetStart(),
                                   lib.GetEnd() - lib.GetStart());
        continue;
      }
      nativePath.erase(apkSeparator);
    }
#  else
    const std::string& nativePath = lib.GetDebugPath();
#  endif

    // We can use the standard POSIX-based mapper.
    AutoObjectMapperPOSIX mapper(aLUL->mLog);

    // Ask |mapper| to map the object.  Then hand its mapped address
    // to NotifyAfterMap().
    void* image = nullptr;
    size_t size = 0;
    bool ok = mapper.Map(&image, &size, nativePath, fileOffset);
    if (ok && image && size > 0) {
      aLUL->NotifyAfterMap(lib.GetStart(), lib.GetEnd() - lib.GetStart(),
                           nativePath.c_str(), image);
    } else if (!ok && (lib.GetDebugName().empty() || isApkEmbedded)) {
      // We failed to map the object, so we can't read any unwind information
      // for it. This happens in two cases: an object with no name, which on
      // Linux is how GetInfoForSelf() reports the VDSO (and lack of knowledge
      // about that area inhibits LUL's special __kernel_syscall handling on
      // x86-{linux,android}). And an APK-embedded library whose mapping failed
      // (e.g. a page-misaligned offset). In both cases, notify |aLUL| of at
      // least the mapping so that stack scanning still works.
      aLUL->NotifyExecutableArea(lib.GetStart(), lib.GetEnd() - lib.GetStart());
    }

    // |mapper| goes out of scope at this point and so its destructor
    // unmaps the object.
  }

#else
#  error "Unknown platform"
#endif
}

// LUL needs a callback for its logging sink.
void logging_sink_for_LUL(const char* str) {
  // These are only printed when Verbose logging is enabled (e.g. with
  // MOZ_LOG="prof:5"). This is because LUL's logging is much more verbose than
  // the rest of the profiler's logging, which occurs at the Info (3) and Debug
  // (4) levels.
  MOZ_LOG(gProfilerLog, mozilla::LogLevel::Verbose,
          ("[%" PRIu64 "] %s",
           uint64_t(profiler_current_process_id().ToNumber()), str));
}
