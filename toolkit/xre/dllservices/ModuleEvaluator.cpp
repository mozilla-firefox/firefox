/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "ModuleEvaluator.h"

#include <algorithm>  // For std::find()
#include <type_traits>

#include <windows.h>
#include <shlobj.h>

#include "mozilla/ArrayUtils.h"
#include "mozilla/ModuleVersionInfo.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/Unused.h"
#include "mozilla/WinDllServices.h"
#include "mozilla/WinHeaderOnlyUtils.h"
#include "nsReadableUtils.h"
#include "nsWindowsHelpers.h"
#include "nsXULAppAPI.h"
#include "prenv.h"

namespace mozilla {

// Fills a Vector with keyboard layout DLLs found in the registry.
// These are leaf names only, not full paths. Here we will convert them to
// lowercase before returning, to facilitate case-insensitive searches.
// On error, this may return partial results.
static Vector<nsString> GetKeyboardLayoutDlls() {
  Vector<nsString> result;

  HKEY rawKey;
  if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts",
                      0, KEY_ENUMERATE_SUB_KEYS, &rawKey) != ERROR_SUCCESS) {
    return result;
  }
  nsAutoRegKey key(rawKey);

  DWORD iKey = 0;
  wchar_t strTemp[MAX_PATH] = {};
  while (true) {
    DWORD strTempSize = std::size(strTemp);
    if (RegEnumKeyExW(rawKey, iKey, strTemp, &strTempSize, nullptr, nullptr,
                      nullptr, nullptr) != ERROR_SUCCESS) {
      // ERROR_NO_MORE_ITEMS or a real error: bail with what we have.
      return result;
    }
    iKey++;

    strTempSize = sizeof(strTemp);
    if (::RegGetValueW(rawKey, strTemp, L"Layout File", RRF_RT_REG_SZ, nullptr,
                       strTemp, &strTempSize) == ERROR_SUCCESS &&
        strTempSize) {
      nsString ws(strTemp, ((strTempSize + 1) / sizeof(wchar_t)) - 1);
      ToLowerCase(ws);  // To facilitate case-insensitive searches
      Unused << result.emplaceBack(std::move(ws));
    }
  }
}

/* static */
bool ModuleEvaluator::ResolveKnownFolder(REFKNOWNFOLDERID aFolderId,
                                         nsIFile** aOutFile) {
  if (!aOutFile) {
    return false;
  }

  *aOutFile = nullptr;

  // Since we're running off main thread, we can't use NS_GetSpecialDirectory
  PWSTR rawPath = nullptr;
  HRESULT hr =
      ::SHGetKnownFolderPath(aFolderId, KF_FLAG_DEFAULT, nullptr, &rawPath);
  if (FAILED(hr)) {
    return false;
  }

  using ShellStringUniquePtr =
      UniquePtr<std::remove_pointer_t<PWSTR>, CoTaskMemFreeDeleter>;

  ShellStringUniquePtr path(rawPath);

  nsresult rv = NS_NewLocalFile(nsDependentString(path.get()), aOutFile);
  return NS_SUCCEEDED(rv);
}

ModuleEvaluator::ModuleEvaluator()
    : mKeyboardLayoutDlls(GetKeyboardLayoutDlls()) {
  MOZ_ASSERT(XRE_IsParentProcess());

#if defined(_M_IX86)
  // We want to resolve to SYSWOW64 when applicable
  REFKNOWNFOLDERID systemFolderId = FOLDERID_SystemX86;
#else
  REFKNOWNFOLDERID systemFolderId = FOLDERID_System;
#endif  // defined(_M_IX86)

  bool resolveOk =
      ResolveKnownFolder(systemFolderId, getter_AddRefs(mSysDirectory));
  MOZ_ASSERT(resolveOk);
  if (!resolveOk) {
    return;
  }

  nsCOMPtr<nsIFile> winSxSDir;
  resolveOk = ResolveKnownFolder(FOLDERID_Windows, getter_AddRefs(winSxSDir));
  MOZ_ASSERT(resolveOk);
  if (!resolveOk) {
    return;
  }

  nsresult rv = winSxSDir->Append(u"WinSxS"_ns);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  if (NS_FAILED(rv)) {
    return;
  }

  mWinSxSDirectory = std::move(winSxSDir);

  nsCOMPtr<nsIFile> exeFile;
  rv = XRE_GetBinaryPath(getter_AddRefs(exeFile));
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  if (NS_FAILED(rv)) {
    return;
  }

  rv = exeFile->GetParent(getter_AddRefs(mExeDirectory));
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  if (NS_FAILED(rv)) {
    return;
  }

  nsAutoString exePath;
  rv = exeFile->GetPath(exePath);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  if (NS_FAILED(rv)) {
    return;
  }

  ModuleVersionInfo exeVi;
  if (!exeVi.GetFromImage(exePath)) {
    return;
  }

  mExeVersion = Some(ModuleVersion(exeVi.mFileVersion.Version64()));
}

ModuleEvaluator::operator bool() const {
  return mExeVersion.isSome() && mExeDirectory && mSysDirectory &&
         mWinSxSDirectory;
}

Maybe<ModuleTrustFlags> ModuleEvaluator::GetTrust(
    const ModuleRecord& aModuleRecord) const {
  MOZ_ASSERT(XRE_IsParentProcess());

  // We start by checking authenticode signatures, as the presence of any
  // signature will produce an immediate pass/fail.
  if (aModuleRecord.mVendorInfo.isSome() &&
      aModuleRecord.mVendorInfo.ref().mHasNestedMicrosoftSignature) {
    return Some(ModuleTrustFlags::MicrosoftWindowsSignature);
  }
  if (aModuleRecord.mVendorInfo.isSome() &&
      aModuleRecord.mVendorInfo.ref().mSource ==
          VendorInfo::Source::Signature) {
    const nsString& signedBy = aModuleRecord.mVendorInfo.ref().mVendor;

    if (signedBy.EqualsLiteral("Microsoft Windows")) {
      return Some(ModuleTrustFlags::MicrosoftWindowsSignature);
    } else if (signedBy.EqualsLiteral("Microsoft Corporation")) {
      return Some(ModuleTrustFlags::MicrosoftWindowsSignature);
    } else if (signedBy.EqualsLiteral("Mozilla Corporation")) {
      return Some(ModuleTrustFlags::MozillaSignature);
    } else {
      // Being signed by somebody who is neither Microsoft nor us is an
      // automatic and immediate disqualification.
      return Some(ModuleTrustFlags::None);
    }
  }

  const nsCOMPtr<nsIFile>& dllFile = aModuleRecord.mResolvedDosName;
  MOZ_ASSERT(!!dllFile);
  if (!dllFile) {
    return Nothing();
  }

  nsAutoString dllLeafLower;
  if (NS_FAILED(dllFile->GetLeafName(dllLeafLower))) {
    return Nothing();
  }

  ToLowerCase(dllLeafLower);  // To facilitate case-insensitive searching

  // The JIT profiling module doesn't really have any other practical way to
  // match; hard-code it as being trusted.
  if (dllLeafLower.EqualsLiteral("jitpi.dll")) {
    if (PR_GetEnvSecure("JS_LOAD_VTUNE_LIB")) {
      return Some(ModuleTrustFlags::JitPI);
    } else {
      return Some(ModuleTrustFlags::None);
    }
  }

  ModuleTrustFlags result = ModuleTrustFlags::None;

  nsresult rv;
  bool contained;

  // Is the DLL in the system directory?
  rv = mSysDirectory->Contains(dllFile, &contained);
  if (NS_SUCCEEDED(rv) && contained) {
    result |= ModuleTrustFlags::SystemDirectory;
  }

  // Is the DLL in the WinSxS directory? Some Microsoft DLLs (e.g. comctl32) are
  // loaded from here and don't have digital signatures. So while this is not a
  // guarantee of trustworthiness, but is at least as valid as system32.
  rv = mWinSxSDirectory->Contains(dllFile, &contained);
  if (NS_SUCCEEDED(rv) && contained) {
    result |= ModuleTrustFlags::WinSxSDirectory;
  }

  // Is it a keyboard layout DLL?
  if (std::find(mKeyboardLayoutDlls.begin(), mKeyboardLayoutDlls.end(),
                dllLeafLower) != mKeyboardLayoutDlls.end()) {
    result |= ModuleTrustFlags::KeyboardLayout;
    // This doesn't guarantee trustworthiness by itself. Keyboard layouts also
    // must be in the system directory.
  }

  if (aModuleRecord.mVendorInfo.isSome() &&
      aModuleRecord.mVendorInfo.ref().mSource ==
          VendorInfo::Source::VersionInfo) {
    const nsString& companyName = aModuleRecord.mVendorInfo.ref().mVendor;

    if (companyName.EqualsLiteral("Microsoft Corporation")) {
      result |= ModuleTrustFlags::MicrosoftVersion;
    }
  }

  rv = mExeDirectory->Contains(dllFile, &contained);
  if (NS_SUCCEEDED(rv) && contained) {
    result |= ModuleTrustFlags::FirefoxDirectory;

    // If the DLL is in the Firefox directory, does it also share the Firefox
    // version info?
    if (mExeVersion.isSome() && aModuleRecord.mVersion.isSome() &&
        mExeVersion.value() == aModuleRecord.mVersion.value()) {
      result |= ModuleTrustFlags::FirefoxDirectoryAndVersion;
    }
  }

  return Some(result);
}

}  // namespace mozilla
