/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Omnijar.h"

#include "nsDirectoryService.h"
#include "nsDirectoryServiceDefs.h"
#include "mozilla/GeckoArgs.h"
#include "mozilla/HelperMacros.h"
#include "mozilla/ipc/ProcessChild.h"
#include "nsIFile.h"
#include "nsZipArchive.h"
#include "nsNetUtil.h"

namespace mozilla {

StaticRefPtr<nsIFile> Omnijar::sPath[2];
StaticRefPtr<nsZipArchive> Omnijar::sReader[2];
StaticRefPtr<nsZipArchive> Omnijar::sOuterReader[2];
bool Omnijar::sInitialized = false;
bool Omnijar::sIsUnified = false;

static const char* sProp[2] = {NS_GRE_DIR, NS_XPCOM_CURRENT_PROCESS_DIR};

#define SPROP(Type) ((Type == mozilla::Omnijar::GRE) ? sProp[GRE] : sProp[APP])

void Omnijar::CleanUpOne(Type aType) {
  if (sReader[aType]) {
    sReader[aType] = nullptr;
  }
  if (sOuterReader[aType]) {
    sOuterReader[aType] = nullptr;
  }
  sPath[aType] = nullptr;
}

nsresult Omnijar::InitOne(nsIFile* aPath, Type aType) {
  constexpr auto kOmnijarName = nsLiteralCString{MOZ_STRINGIFY(OMNIJAR_NAME)};
  nsCOMPtr<nsIFile> file;
  if (aPath) {
    file = aPath;
  } else {
    nsCOMPtr<nsIFile> dir;
    MOZ_TRY(nsDirectoryService::gService->Get(SPROP(aType), NS_GET_IID(nsIFile),
                                              getter_AddRefs(dir)));
    MOZ_TRY(dir->Clone(getter_AddRefs(file)));
    MOZ_TRY(file->AppendNative(kOmnijarName));
  }

  bool isFile = false;
  if (NS_FAILED(file->IsFile(&isFile)) || !isFile) {
    // If we're not using an omni.jar for GRE, and we don't have an
    // omni.jar for APP, check if both directories are the same.
    if ((aType == APP) && (!sPath[GRE])) {
      nsCOMPtr<nsIFile> greDir, appDir;
      bool equals;
      nsDirectoryService::gService->Get(sProp[GRE], NS_GET_IID(nsIFile),
                                        getter_AddRefs(greDir));
      nsDirectoryService::gService->Get(sProp[APP], NS_GET_IID(nsIFile),
                                        getter_AddRefs(appDir));
      if (NS_SUCCEEDED(greDir->Equals(appDir, &equals)) && equals) {
        sIsUnified = true;
      }
    }
    return NS_OK;
  }

  // If we're using omni.jar on both GRE and APP and their path
  // is the same, we're also in the unified case.
  bool equals;
  if ((aType == APP) && (sPath[GRE]) &&
      NS_SUCCEEDED(sPath[GRE]->Equals(file, &equals)) && equals) {
    // If we're using omni.jar on both GRE and APP and their path
    // is the same, we're in the unified case.
    sIsUnified = true;
    return NS_OK;
  }

  // This file is our own build-time packaged omni.jar, so it's safe to let
  // nsZipArchive trust its optimized (Central-Directory-at-offset-4) layout.
  RefPtr<nsZipArchive> zipReader =
      nsZipArchive::OpenArchive(file, /* aAllowOptimizedLayout */ true);
  if (!zipReader) {
    // As file has been checked to exist as file above, any error indicates
    // that it is somehow corrupted internally.
    return NS_ERROR_FILE_CORRUPTED;
  }

  RefPtr<nsZipArchive> outerReader;
  RefPtr<nsZipHandle> handle;
  // If we find a wrapped OMNIJAR, unwrap it.
  if (NS_SUCCEEDED(
          nsZipHandle::Init(zipReader, kOmnijarName, getter_AddRefs(handle)))) {
    outerReader = zipReader;
    zipReader = nsZipArchive::OpenArchive(handle, nullptr,
                                          /* aAllowOptimizedLayout */ true);
    if (!zipReader) {
      return NS_ERROR_FILE_CORRUPTED;
    }
  }

  CleanUpOne(aType);
  sReader[aType] = zipReader;
  sOuterReader[aType] = outerReader;
  sPath[aType] = file;

  return NS_OK;
}

nsresult Omnijar::FallibleInit(nsIFile* aGrePath, nsIFile* aAppPath) {
  // Even on error we do not want to come here again.
  sInitialized = true;

  // Let's always try to init both before returning any error for the benefit
  // of callers that do not handle the error at all.
  nsresult rvGRE = InitOne(aGrePath, GRE);
  nsresult rvAPP = InitOne(aAppPath, APP);
  MOZ_TRY(rvGRE);
  MOZ_TRY(rvAPP);

  return NS_OK;
}

void Omnijar::Init(nsIFile* aGrePath, nsIFile* aAppPath) {
  nsresult rv = FallibleInit(aGrePath, aAppPath);
  if (NS_FAILED(rv)) {
    MOZ_CRASH_UNSAFE_PRINTF("Omnijar::Init failed: %s",
                            mozilla::GetStaticErrorName(rv));
  }
}

void Omnijar::CleanUp() {
  CleanUpOne(GRE);
  CleanUpOne(APP);
  sInitialized = false;
}

already_AddRefed<nsZipArchive> Omnijar::GetReader(nsIFile* aPath) {
  MOZ_ASSERT(IsInitialized(), "Omnijar not initialized");

  bool equals;
  nsresult rv;

  if (sPath[GRE]) {
    rv = sPath[GRE]->Equals(aPath, &equals);
    if (NS_SUCCEEDED(rv) && equals) {
      return IsNested(GRE) ? GetOuterReader(GRE) : GetReader(GRE);
    }
  }
  if (sPath[APP]) {
    rv = sPath[APP]->Equals(aPath, &equals);
    if (NS_SUCCEEDED(rv) && equals) {
      return IsNested(APP) ? GetOuterReader(APP) : GetReader(APP);
    }
  }
  return nullptr;
}

already_AddRefed<nsZipArchive> Omnijar::GetInnerReader(
    nsIFile* aPath, const nsACString& aEntry) {
  MOZ_ASSERT(IsInitialized(), "Omnijar not initialized");

  if (!aEntry.EqualsLiteral(MOZ_STRINGIFY(OMNIJAR_NAME))) {
    return nullptr;
  }

  bool equals;
  nsresult rv;

  if (sPath[GRE]) {
    rv = sPath[GRE]->Equals(aPath, &equals);
    if (NS_SUCCEEDED(rv) && equals) {
      return IsNested(GRE) ? GetReader(GRE) : nullptr;
    }
  }
  if (sPath[APP]) {
    rv = sPath[APP]->Equals(aPath, &equals);
    if (NS_SUCCEEDED(rv) && equals) {
      return IsNested(APP) ? GetReader(APP) : nullptr;
    }
  }
  return nullptr;
}

nsresult Omnijar::GetURIString(Type aType, nsACString& aResult) {
  MOZ_ASSERT(IsInitialized(), "Omnijar not initialized");

  aResult.Truncate();

  // Return an empty string for APP in the unified case.
  if ((aType == APP) && sIsUnified) {
    return NS_OK;
  }

  nsAutoCString omniJarSpec;
  if (sPath[aType]) {
    nsresult rv = NS_GetURLSpecFromActualFile(sPath[aType], omniJarSpec);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    aResult = "jar:";
    if (IsNested(aType)) {
      aResult += "jar:";
    }
    aResult += omniJarSpec;
    aResult += "!";
    if (IsNested(aType)) {
      aResult += "/" MOZ_STRINGIFY(OMNIJAR_NAME) "!";
    }
  } else {
    nsCOMPtr<nsIFile> dir;
    nsDirectoryService::gService->Get(SPROP(aType), NS_GET_IID(nsIFile),
                                      getter_AddRefs(dir));
    nsresult rv = NS_GetURLSpecFromActualFile(dir, aResult);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  }
  aResult += "/";
  return NS_OK;
}

#if defined(MOZ_WIDGET_ANDROID) && defined(MOZ_DIAGNOSTIC_ASSERT_ENABLED)
#  define ANDROID_DIAGNOSTIC_CRASH_OR_EXIT(_msg) MOZ_CRASH(_msg)
#elif defined(MOZ_WIDGET_ANDROID)
#  define ANDROID_DIAGNOSTIC_CRASH_OR_EXIT(_msg) ipc::ProcessChild::QuickExit()
#else
#  define ANDROID_DIAGNOSTIC_CRASH_OR_EXIT(_msg)
#endif

void Omnijar::ChildProcessInit(int& aArgc, char** aArgv) {
  nsCOMPtr<nsIFile> greOmni, appOmni;

  // Android builds are always packaged, so if we can't find anything for
  // greOmni, then this content process is useless, so kill it immediately.
  // On release, we do this via QuickExit() because the crash volume is so
  // high. See bug 1915788.
  if (auto greOmniStr = geckoargs::sGREOmni.Get(aArgc, aArgv)) {
    if (NS_WARN_IF(NS_FAILED(
            XRE_GetFileFromPath(*greOmniStr, getter_AddRefs(greOmni))))) {
      ANDROID_DIAGNOSTIC_CRASH_OR_EXIT("XRE_GetFileFromPath failed");
      greOmni = nullptr;
    }
  } else {
    ANDROID_DIAGNOSTIC_CRASH_OR_EXIT("sGREOmni.Get failed");
  }
  if (auto appOmniStr = geckoargs::sAppOmni.Get(aArgc, aArgv)) {
    if (NS_WARN_IF(NS_FAILED(
            XRE_GetFileFromPath(*appOmniStr, getter_AddRefs(appOmni))))) {
      appOmni = nullptr;
    }
  }

  // If we're unified, then only the -greomni flag is present
  // (reflecting the state of sPath in the parent process) but that
  // path should be used for both (not nullptr, which will try to
  // invoke the directory service, which probably isn't up yet.)
  if (!appOmni) {
    appOmni = greOmni;
  }

  if (greOmni) {
    Init(greOmni, appOmni);
  } else {
    // We should never have an appOmni without a greOmni.
    MOZ_ASSERT(!appOmni);
  }
}

#undef ANDROID_DIAGNOSTIC_CRASH_OR_EXIT

} /* namespace mozilla */
