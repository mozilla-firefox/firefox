/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "LlamaRuntimeLinker.h"

#include "mozilla/FileUtils.h"
#include "mozilla/Logging.h"
#include "nsLocalFile.h"
#include "nsXPCOMPrivate.h"
#include "prlink.h"

#ifdef XP_WIN
#  include <windows.h>
#endif

mozilla::LazyLogModule gLlamaLinkerLog("LlamaRuntimeLinker");

#define LOG(level, fmt, ...) \
  MOZ_LOG(gLlamaLinkerLog, level, ("[LlamaRuntimeLinker] " fmt, ##__VA_ARGS__))

namespace mozilla::llama {

LlamaLibWrapper LlamaRuntimeLinker::sLlamaLib;
LlamaRuntimeLinker::LinkStatus LlamaRuntimeLinker::sLinkStatus =
    LinkStatus_INIT;

static PRLibrary* LoadLlamaLib(nsIFile* aFile) {
  PRLibSpec lspec;
  PathString path = aFile->NativePath();
#ifdef XP_WIN
  lspec.type = PR_LibSpec_PathnameU;
  lspec.value.pathname_u = path.get();
#else
#  if defined(XP_OPENBSD)
  /* on OpenBSD, libraries are preloaded before sandboxing so make sure only
   * the filename is passed to PR_LoadLibraryWithFlags(), dlopen() will return
   * the preloaded library handle instead of failing to find it due to
   * sandboxing */
  nsAutoCString leaf;
  nsresult rv = aFile->GetNativeLeafName(leaf);
  if (NS_SUCCEEDED(rv)) {
    path = PathString(leaf);
  }
#  endif  // defined(XP_OPENBSD)
  lspec.type = PR_LibSpec_Pathname;
  lspec.value.pathname = path.get();
#endif
#ifdef MOZ_WIDGET_ANDROID
  PRLibrary* lib = PR_LoadLibraryWithFlags(lspec, PR_LD_NOW | PR_LD_GLOBAL);
#else
  PRLibrary* lib = PR_LoadLibraryWithFlags(lspec, PR_LD_NOW | PR_LD_LOCAL);
#endif
  if (!lib) {
    bool exists = false;
    aFile->Exists(&exists);
#ifdef XP_WIN
    LOG(LogLevel::Error,
        "unable to load library %s (exists=%d, GetLastError=%lu)",
        aFile->HumanReadablePath().get(), exists, GetLastError());
#else
    LOG(LogLevel::Error, "unable to load library %s (exists=%d)",
        aFile->HumanReadablePath().get(), exists);
#endif
  }
  return lib;
}

LlamaLibWrapper::LinkResult LlamaLibWrapper::Link() {
  if (!mLlamaLib) {
    Unlink();
    return LinkResult::NoProvidedLib;
  }

  LOG(LogLevel::Debug, "Linking llama library symbols");

  // Use X-macros to bind all functions
#define BIND_FUNCTION(ret, name, params)                             \
  if (!((name) = (decltype(name))PR_FindSymbol(mLlamaLib, #name))) { \
    LOG(LogLevel::Error, "Couldn't load function " #name);           \
    Unlink();                                                        \
    return LinkResult::MissingFunction;                              \
  }

  MOZINFERENCE_FUNCTION_LIST(BIND_FUNCTION)

#undef BIND_FUNCTION

  LOG(LogLevel::Debug, "Successfully linked all llama functions");
  return LinkResult::Success;
}

void LlamaLibWrapper::Unlink() {
  if (mLlamaLib) {
    PR_UnloadLibrary(mLlamaLib);
    mLlamaLib = nullptr;
  }

  // Clear all function pointers using X-macros
#define CLEAR_FUNCTION(ret, name, params) name = nullptr;
  MOZINFERENCE_FUNCTION_LIST(CLEAR_FUNCTION)
#undef CLEAR_FUNCTION
}

/* static */
bool LlamaRuntimeLinker::Init() {
  // Quick return if already initialized
  if (sLinkStatus == LinkStatus_SUCCEEDED) {
    return true;
  }
  if (sLinkStatus == LinkStatus_FAILED) {
    return false;
  }

  LOG(LogLevel::Debug, "Initializing llama runtime linker");

  sLinkStatus = LinkStatus_FAILED;

  // Get the path to the current executable/library
#ifdef XP_WIN
  PathString path =
      GetLibraryFilePathname(LXUL_DLL, (PRFuncPtr)&LlamaRuntimeLinker::Init);
#else
  PathString path =
      GetLibraryFilePathname(XUL_DLL, (PRFuncPtr)&LlamaRuntimeLinker::Init);
#endif
  if (path.IsEmpty()) {
    LOG(LogLevel::Error, "Failed to get library path");
    return false;
  }

  nsCOMPtr<nsIFile> libFile;
  if (NS_FAILED(NS_NewPathStringLocalFile(path, getter_AddRefs(libFile)))) {
    LOG(LogLevel::Error, "Failed to create file object from path");
    return false;
  }

  // Handle test environments
  if (getenv("MOZ_RUN_GTEST")
#ifdef FUZZING
      || getenv("FUZZER")
#endif
  ) {
    // In test environments, the library is in the parent directory
    nsCOMPtr<nsIFile> parent;
    if (NS_FAILED(libFile->GetParent(getter_AddRefs(parent)))) {
      LOG(LogLevel::Error, "Failed to get parent directory");
      return false;
    }
    libFile = parent;
  }

  // Set the library name with proper prefix and suffix
  if (NS_FAILED(libFile->SetNativeLeafName(
          MOZ_DLL_PREFIX "mozinference" MOZ_DLL_SUFFIX ""_ns))) {
    LOG(LogLevel::Error, "Failed to set library name");
    return false;
  }

  LOG(LogLevel::Debug, "Attempting to load library from: %s",
      libFile->HumanReadablePath().get());

  sLlamaLib.mLlamaLib = LoadLlamaLib(libFile);
  if (!sLlamaLib.mLlamaLib) {
    LOG(LogLevel::Error, "Failed to load llama library");
    return false;
  }

  LlamaLibWrapper::LinkResult res = sLlamaLib.Link();
  if (res != LlamaLibWrapper::LinkResult::Success) {
    LOG(LogLevel::Error, "Failed to link llama library: %d", (int)res);
    return false;
  }

  sLinkStatus = LinkStatus_SUCCEEDED;
  LOG(LogLevel::Info, "Successfully initialized llama runtime linker");
  return true;
}

}  // namespace mozilla::llama
