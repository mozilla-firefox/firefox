/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ArrayUtils.h"
#include "mozilla/Preferences.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/UniquePtrExtensions.h"
#include "mozilla/WidgetUtils.h"
#include "nsProfileLock.h"

#include <stdio.h>
#include <stdlib.h>
#include <prprf.h>
#include <prtime.h>

#ifdef XP_WIN
#  include <windows.h>
#  include <shlobj.h>
#  include "mozilla/PolicyChecks.h"
#endif
#ifdef XP_UNIX
#  include <unistd.h>
#endif

#include "nsToolkitProfileService.h"
#include "CmdLineAndEnvUtils.h"
#include "nsIFile.h"

#ifdef XP_MACOSX
#  include <CoreFoundation/CoreFoundation.h>
#  include "nsILocalFileMac.h"
#endif

#ifdef MOZ_WIDGET_GTK
#  include "mozilla/WidgetUtilsGtk.h"
#endif

#include "nsAppDirectoryServiceDefs.h"
#include "nsDirectoryServiceDefs.h"
#include "nsNetCID.h"
#include "nsXULAppAPI.h"
#include "nsThreadUtils.h"

#include "nsIRunnable.h"
#include "nsXREDirProvider.h"
#include "nsAppRunner.h"
#include "nsString.h"
#include "nsReadableUtils.h"
#include "nsNativeCharsetUtils.h"
#include "mozilla/Attributes.h"
#include "mozilla/Sprintf.h"
#include "nsPrintfCString.h"
#include "mozilla/dom/DOMMozPromiseRequestHolder.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/UniquePtr.h"
#include "nsIToolkitShellService.h"
#include "mozilla/glean/ToolkitProfileMetrics.h"
#include "nsProxyRelease.h"
#ifdef MOZ_HAS_REMOTE
#  include "nsRemoteService.h"
#endif
#include "prinrval.h"
#include "prthread.h"
#include "xpcpublic.h"
#include "nsProxyRelease.h"
#ifdef MOZ_BACKGROUNDTASKS
#  include "mozilla/BackgroundTasks.h"
#  include "SpecialSystemDirectory.h"
#endif

using namespace mozilla;

#define DEV_EDITION_NAME "dev-edition-default"
#define DEFAULT_NAME "default"
#define COMPAT_FILE u"compatibility.ini"_ns
#define PROFILE_DB_VERSION "2"
#define INSTALL_PREFIX "Install"
#define INSTALL_PREFIX_LENGTH 7
#define STORE_ID_PREF "toolkit.profiles.storeID"

struct KeyValue {
  KeyValue(const char* aKey, const char* aValue) : key(aKey), value(aValue) {}

  nsCString key;
  nsCString value;
};

/**
 * Returns an array of the strings inside a section of an ini file.
 */
nsTArray<UniquePtr<KeyValue>> GetSectionStrings(nsINIParser* aParser,
                                                const char* aSection) {
  nsTArray<UniquePtr<KeyValue>> strings;
  aParser->GetStrings(
      aSection, [&strings](const char* aString, const char* aValue) {
        strings.AppendElement(MakeUnique<KeyValue>(aString, aValue));
        return true;
      });

  return strings;
}

void RemoveProfileRecursion(const nsCOMPtr<nsIFile>& aDirectoryOrFile,
                            bool aIsIgnoreRoot, bool aIsIgnoreLockfile,
                            nsTArray<nsCOMPtr<nsIFile>>& aOutUndeletedFiles) {
  auto guardDeletion = MakeScopeExit(
      [&] { aOutUndeletedFiles.AppendElement(aDirectoryOrFile); });

  // We actually would not expect to see links in our profiles, but still.
  bool isLink = false;
  NS_ENSURE_SUCCESS_VOID(aDirectoryOrFile->IsSymlink(&isLink));

  // Only check to see if we have a directory if it isn't a link.
  bool isDir = false;
  if (!isLink) {
    NS_ENSURE_SUCCESS_VOID(aDirectoryOrFile->IsDirectory(&isDir));
  }

  if (isDir) {
    nsCOMPtr<nsIDirectoryEnumerator> dirEnum;
    NS_ENSURE_SUCCESS_VOID(
        aDirectoryOrFile->GetDirectoryEntries(getter_AddRefs(dirEnum)));

    bool more = false;
    while (NS_SUCCEEDED(dirEnum->HasMoreElements(&more)) && more) {
      nsCOMPtr<nsISupports> item;
      dirEnum->GetNext(getter_AddRefs(item));
      nsCOMPtr<nsIFile> file = do_QueryInterface(item);
      if (file) {
        // Do not delete the profile lock.
        if (aIsIgnoreLockfile && nsProfileLock::IsMaybeLockFile(file)) continue;
        // If some children's remove fails, we still continue the loop.
        RemoveProfileRecursion(file, false, false, aOutUndeletedFiles);
      }
    }
  }
  // Do not delete the root directory (yet).
  if (!aIsIgnoreRoot) {
    NS_ENSURE_SUCCESS_VOID(aDirectoryOrFile->Remove(false));
  }
  guardDeletion.release();
}

/**
 * `aLockTimeout` is the number of seconds to wait to obtain the profile lock
 * before failing. Set to 0 to not wait at all and immediately fail if not lock
 * was obtained.
 */
nsresult RemoveProfileFiles(nsIFile* aRootDir, nsIFile* aLocalDir,
                            uint32_t aLockTimeout) {
  // XXX If we get here with an active quota manager,
  // something went very wrong. We want to assert this.

  // Attempt to acquire the profile lock.
  nsresult rv;
  nsCOMPtr<nsIProfileLock> lock;
  const mozilla::TimeStamp epoch = mozilla::TimeStamp::Now();
  do {
    rv = NS_LockProfilePath(aRootDir, aLocalDir, nullptr, getter_AddRefs(lock));
    if (NS_SUCCEEDED(rv)) {
      break;
    }

    // If we don't want to delay at all then bail immediately.
    if (aLockTimeout == 0) {
      return NS_ERROR_FAILURE;
    }

    // Check twice a second.
    PR_Sleep(500);
  } while ((mozilla::TimeStamp::Now() - epoch) <
           mozilla::TimeDuration::FromSeconds(aLockTimeout));

  // If we failed to acquire the lock then give up.
  if (!lock) {
    return NS_ERROR_FAILURE;
  }

  // We try to remove every single file and directory and collect
  // those whose removal failed.
  nsTArray<nsCOMPtr<nsIFile>> undeletedFiles;
  // The root dir might contain the temp dir, so remove the temp dir
  // first.
  bool equals;
  rv = aRootDir->Equals(aLocalDir, &equals);
  if (NS_SUCCEEDED(rv) && !equals) {
    RemoveProfileRecursion(aLocalDir,
                           /* aIsIgnoreRoot  */ false,
                           /* aIsIgnoreLockfile */ false, undeletedFiles);
  }
  // Now remove the content of the profile dir (except lockfile)
  RemoveProfileRecursion(aRootDir,
                         /* aIsIgnoreRoot  */ true,
                         /* aIsIgnoreLockfile */ true, undeletedFiles);

  // Retry loop if something was not deleted
  if (undeletedFiles.Length() > 0) {
    uint32_t retries = 1;
    while (undeletedFiles.Length() > 0 && retries <= 10) {
      Unused << PR_Sleep(PR_MillisecondsToInterval(10 * retries));
      for (auto&& file :
           std::exchange(undeletedFiles, nsTArray<nsCOMPtr<nsIFile>>{})) {
        RemoveProfileRecursion(file,
                               /* aIsIgnoreRoot */ false,
                               /* aIsIgnoreLockfile */ true, undeletedFiles);
      }
      retries++;
    }
  }

  if (undeletedFiles.Length() > 0) {
    NS_WARNING("Unable to remove all files from the profile directory:");
    // Log the file names of those we could not remove
    for (auto&& file : undeletedFiles) {
      nsAutoString leafName;
      if (NS_SUCCEEDED(file->GetLeafName(leafName))) {
        NS_WARNING(NS_LossyConvertUTF16toASCII(leafName).get());
      }
    }
  }
  MOZ_ASSERT(undeletedFiles.Length() == 0);

  // Now we can unlock the profile safely.
  lock->Unlock();

  if (undeletedFiles.Length() == 0) {
    // We can safely remove the (empty) remaining profile directory
    // and lockfile, no other files are here.
    // As we do this only if we had no other blockers, this is as safe
    // as deleting the lockfile explicitely after unlocking.
    Unused << aRootDir->Remove(true);
  }

  return NS_OK;
}

nsToolkitProfile::nsToolkitProfile(const nsACString& aName, nsIFile* aRootDir,
                                   nsIFile* aLocalDir, bool aFromDB,
                                   const nsACString& aStoreID = VoidCString(),
                                   bool aShowProfileSelector = false)
    : mName(aName),
      mRootDir(aRootDir),
      mLocalDir(aLocalDir),
      mStoreID(aStoreID),
      mShowProfileSelector(aShowProfileSelector),
      mLock(nullptr),
      mIndex(0),
      mSection("Profile") {
  NS_ASSERTION(aRootDir, "No file!");

  RefPtr<nsToolkitProfile> prev =
      nsToolkitProfileService::gService->mProfiles.getLast();
  if (prev) {
    mIndex = prev->mIndex + 1;
  }
  mSection.AppendInt(mIndex);

  nsToolkitProfileService::gService->mProfiles.insertBack(this);

  // If this profile isn't in the database already add it.
  if (!aFromDB) {
    nsINIParser* db = &nsToolkitProfileService::gService->mProfileDB;
    db->SetString(mSection.get(), "Name", mName.get());

    bool isRelative = false;
    nsCString descriptor;
    nsToolkitProfileService::gService->GetProfileDescriptor(this, descriptor,
                                                            &isRelative);

    db->SetString(mSection.get(), "IsRelative", isRelative ? "1" : "0");
    db->SetString(mSection.get(), "Path", descriptor.get());
    if (!mStoreID.IsVoid()) {
      db->SetString(mSection.get(), "StoreID",
                    PromiseFlatCString(mStoreID).get());
      db->SetString(mSection.get(), "ShowSelector",
                    aShowProfileSelector ? "1" : "0");
    }
  }
}

NS_IMPL_ISUPPORTS(nsToolkitProfile, nsIToolkitProfile)

NS_IMETHODIMP
nsToolkitProfile::GetRootDir(nsIFile** aResult) {
  NS_ADDREF(*aResult = mRootDir);
  return NS_OK;
}

NS_IMETHODIMP
nsToolkitProfile::SetRootDir(nsIFile* aRootDir) {
  NS_ASSERTION(nsToolkitProfileService::gService, "Where did my service go?");

  // If the new path is the old path, we're done.
  bool equals;
  nsresult rv = mRootDir->Equals(aRootDir, &equals);
  if (NS_SUCCEEDED(rv) && equals) {
    return NS_OK;
  }

  // Calculate the new paths.
  nsCString newPath;
  bool isRelative;
  rv = nsToolkitProfileService::gService->GetProfileDescriptor(
      aRootDir, newPath, &isRelative);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIFile> localDir;
  rv = nsToolkitProfileService::gService->GetLocalDirFromRootDir(
      aRootDir, getter_AddRefs(localDir));
  NS_ENSURE_SUCCESS(rv, rv);

  // Update the database entry for the current profile.
  nsINIParser* db = &nsToolkitProfileService::gService->mProfileDB;
  rv = db->SetString(mSection.get(), "Path", newPath.get());
  NS_ENSURE_SUCCESS(rv, rv);

  rv = db->SetString(mSection.get(), "IsRelative", isRelative ? "1" : "0");
  NS_ENSURE_SUCCESS(rv, rv);

  // If this profile is the dedicated default, also update the database entry
  // for the install.
  if (nsToolkitProfileService::gService->mDedicatedProfile == this) {
    rv = db->SetString(nsToolkitProfileService::gService->mInstallSection.get(),
                       "Default", newPath.get());
  }
  NS_ENSURE_SUCCESS(rv, rv);

  // Finally, set the new paths on the local object.
  mRootDir = aRootDir;
  mLocalDir = localDir;

  return NS_OK;
}

NS_IMETHODIMP
nsToolkitProfile::GetStoreID(nsACString& aResult) {
  aResult = mStoreID;
  return NS_OK;
}

NS_IMETHODIMP
nsToolkitProfile::SetStoreID(const nsACString& aStoreID) {
#ifdef MOZ_SELECTABLE_PROFILES
  NS_ASSERTION(nsToolkitProfileService::gService, "Where did my service go?");

  if (mStoreID.Equals(aStoreID)) {
    return NS_OK;
  }

  nsresult rv;
  nsCOMPtr<nsIPrefBranch> prefs = do_GetService(NS_PREFSERVICE_CONTRACTID);

  if (!aStoreID.IsVoid()) {
    rv = nsToolkitProfileService::gService->mProfileDB.SetString(
        mSection.get(), "StoreID", PromiseFlatCString(aStoreID).get());
    NS_ENSURE_SUCCESS(rv, rv);

    rv = nsToolkitProfileService::gService->mProfileDB.SetString(
        mSection.get(), "ShowSelector", mShowProfileSelector ? "1" : "0");
    NS_ENSURE_SUCCESS(rv, rv);

    if (nsToolkitProfileService::gService->mCurrent == this) {
      rv = prefs->SetCharPref(STORE_ID_PREF, aStoreID);
      NS_ENSURE_SUCCESS(rv, rv);

      nsToolkitProfileService::gService->mGroupProfile = this;
    }
  } else {
    // If the string was not present in the ini file, just ignore the error.
    nsToolkitProfileService::gService->mProfileDB.DeleteString(mSection.get(),
                                                               "StoreID");

    // We need a StoreID to show the profile selector, so if StoreID has been
    // removed, then remove ShowSelector also.
    mShowProfileSelector = false;

    // If the string was not present in the ini file, just ignore the error.
    nsToolkitProfileService::gService->mProfileDB.DeleteString(mSection.get(),
                                                               "ShowSelector");

    if (nsToolkitProfileService::gService->mCurrent == this) {
      rv = prefs->ClearUserPref(STORE_ID_PREF);
      NS_ENSURE_SUCCESS(rv, rv);

      nsToolkitProfileService::gService->mGroupProfile = nullptr;
    }
  }
  mStoreID = aStoreID;

  return NS_OK;
#else
  return NS_ERROR_FAILURE;
#endif
}

NS_IMETHODIMP
nsToolkitProfile::GetLocalDir(nsIFile** aResult) {
  NS_ADDREF(*aResult = mLocalDir);
  return NS_OK;
}

NS_IMETHODIMP
nsToolkitProfile::GetName(nsACString& aResult) {
  aResult = mName;
  return NS_OK;
}

NS_IMETHODIMP
nsToolkitProfile::SetName(const nsACString& aName) {
  NS_ASSERTION(nsToolkitProfileService::gService, "Where did my service go?");

  if (mName.Equals(aName)) {
    return NS_OK;
  }

  // Changing the name from the dev-edition default profile name makes this
  // profile no longer the dev-edition default.
  if (mName.EqualsLiteral(DEV_EDITION_NAME) &&
      nsToolkitProfileService::gService->mDevEditionDefault == this) {
    nsToolkitProfileService::gService->mDevEditionDefault = nullptr;
  }

  mName = aName;

  nsresult rv = nsToolkitProfileService::gService->mProfileDB.SetString(
      mSection.get(), "Name", mName.get());
  NS_ENSURE_SUCCESS(rv, rv);

  // Setting the name to the dev-edition default profile name will cause this
  // profile to become the dev-edition default.
  if (aName.EqualsLiteral(DEV_EDITION_NAME) &&
      !nsToolkitProfileService::gService->mDevEditionDefault) {
    nsToolkitProfileService::gService->mDevEditionDefault = this;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsToolkitProfile::GetShowProfileSelector(bool* aShowProfileSelector) {
#ifdef MOZ_SELECTABLE_PROFILES
  *aShowProfileSelector = mShowProfileSelector;
#else
  *aShowProfileSelector = false;
#endif
  return NS_OK;
}

NS_IMETHODIMP
nsToolkitProfile::SetShowProfileSelector(bool aShowProfileSelector) {
#ifdef MOZ_SELECTABLE_PROFILES
  NS_ASSERTION(nsToolkitProfileService::gService, "Where did my service go?");

  // We need a StoreID to show the profile selector; bail out if it's missing.
  if (mStoreID.IsVoid()) {
    return NS_ERROR_FAILURE;
  }

  if (mShowProfileSelector == aShowProfileSelector) {
    return NS_OK;
  }

  nsresult rv = nsToolkitProfileService::gService->mProfileDB.SetString(
      mSection.get(), "ShowSelector", aShowProfileSelector ? "1" : "0");
  NS_ENSURE_SUCCESS(rv, rv);

  mShowProfileSelector = aShowProfileSelector;
  return NS_OK;
#else
  return NS_ERROR_FAILURE;
#endif
}

nsresult nsToolkitProfile::RemoveInternal(bool aRemoveFiles,
                                          bool aInBackground) {
  NS_ASSERTION(nsToolkitProfileService::gService, "Whoa, my service is gone.");

  if (mLock) return NS_ERROR_FILE_IS_LOCKED;

  if (!isInList()) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  if (aRemoveFiles) {
    if (aInBackground) {
      NS_DispatchBackgroundTask(NS_NewRunnableFunction(
          __func__, [rootDir = mRootDir, localDir = mLocalDir]() mutable {
            RemoveProfileFiles(rootDir, localDir, 5);
          }));
    } else {
      // Failure is ignored here.
      RemoveProfileFiles(mRootDir, mLocalDir, 0);
    }
  }

  nsINIParser* db = &nsToolkitProfileService::gService->mProfileDB;
  db->DeleteSection(mSection.get());

  // We make some assumptions that the profile's index in the database is based
  // on its position in the linked list. Removing a profile means we have to fix
  // the index of later profiles in the list. The easiest way to do that is just
  // to move the last profile into the profile's position and just update its
  // index.
  RefPtr<nsToolkitProfile> last =
      nsToolkitProfileService::gService->mProfiles.getLast();
  if (last != this) {
    // Update the section in the db.
    last->mIndex = mIndex;
    db->RenameSection(last->mSection.get(), mSection.get());
    last->mSection = mSection;

    if (last != getNext()) {
      last->remove();
      setNext(last);
    }
  }

  remove();

  if (nsToolkitProfileService::gService->mNormalDefault == this) {
    nsToolkitProfileService::gService->mNormalDefault = nullptr;
  }
  if (nsToolkitProfileService::gService->mDevEditionDefault == this) {
    nsToolkitProfileService::gService->mDevEditionDefault = nullptr;
  }
  if (nsToolkitProfileService::gService->mDedicatedProfile == this) {
    nsToolkitProfileService::gService->SetDefaultProfile(nullptr);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsToolkitProfile::Remove(bool removeFiles) {
  return RemoveInternal(removeFiles, false /* in background */);
}

NS_IMETHODIMP
nsToolkitProfile::RemoveInBackground(bool removeFiles) {
  return RemoveInternal(removeFiles, true /* in background */);
}

NS_IMETHODIMP
nsToolkitProfile::Lock(nsIProfileUnlocker** aUnlocker,
                       nsIProfileLock** aResult) {
  if (mLock) {
    NS_ADDREF(*aResult = mLock);
    return NS_OK;
  }

  RefPtr<nsToolkitProfileLock> lock = new nsToolkitProfileLock();

  nsresult rv = lock->Init(this, aUnlocker);
  if (NS_FAILED(rv)) return rv;

  NS_ADDREF(*aResult = lock);
  return NS_OK;
}

NS_IMPL_ISUPPORTS(nsToolkitProfileLock, nsIProfileLock)

nsresult nsToolkitProfileLock::Init(nsToolkitProfile* aProfile,
                                    nsIProfileUnlocker** aUnlocker) {
  nsresult rv;
  rv = Init(aProfile->mRootDir, aProfile->mLocalDir, aUnlocker);
  if (NS_SUCCEEDED(rv)) mProfile = aProfile;

  return rv;
}

nsresult nsToolkitProfileLock::Init(nsIFile* aDirectory,
                                    nsIFile* aLocalDirectory,
                                    nsIProfileUnlocker** aUnlocker) {
  nsresult rv;

  rv = mLock.Lock(aDirectory, aUnlocker);

  if (NS_SUCCEEDED(rv)) {
    mDirectory = aDirectory;
    mLocalDirectory = aLocalDirectory;
  }

  return rv;
}

NS_IMETHODIMP
nsToolkitProfileLock::GetDirectory(nsIFile** aResult) {
  if (!mDirectory) {
    NS_ERROR("Not initialized, or unlocked!");
    return NS_ERROR_NOT_INITIALIZED;
  }

  NS_ADDREF(*aResult = mDirectory);
  return NS_OK;
}

NS_IMETHODIMP
nsToolkitProfileLock::GetLocalDirectory(nsIFile** aResult) {
  if (!mLocalDirectory) {
    NS_ERROR("Not initialized, or unlocked!");
    return NS_ERROR_NOT_INITIALIZED;
  }

  NS_ADDREF(*aResult = mLocalDirectory);
  return NS_OK;
}

NS_IMETHODIMP
nsToolkitProfileLock::Unlock() {
  if (!mDirectory) {
    NS_ERROR("Unlocking a never-locked nsToolkitProfileLock!");
    return NS_ERROR_UNEXPECTED;
  }

  // XXX If we get here with an active quota manager,
  // something went very wrong. We want to assert this.

  mLock.Unlock();

  if (mProfile) {
    mProfile->mLock = nullptr;
    mProfile = nullptr;
  }
  mDirectory = nullptr;
  mLocalDirectory = nullptr;

  return NS_OK;
}

NS_IMETHODIMP
nsToolkitProfileLock::GetReplacedLockTime(PRTime* aResult) {
  mLock.GetReplacedLockTime(aResult);
  return NS_OK;
}

nsToolkitProfileLock::~nsToolkitProfileLock() {
  if (mDirectory) {
    Unlock();
  }
}

nsToolkitProfileService* nsToolkitProfileService::gService = nullptr;

NS_IMPL_ISUPPORTS(nsToolkitProfileService, nsIToolkitProfileService)

nsToolkitProfileService::nsToolkitProfileService()
    : mStartupProfileSelected(false),
      mStartWithLast(true),
      mIsFirstRun(true),
      mUseDevEditionProfile(false),
#ifdef MOZ_DEDICATED_PROFILES
      mUseDedicatedProfile(!IsSnapEnvironment() && !UseLegacyProfiles()),
#else
      mUseDedicatedProfile(false),
#endif
      mStartupReason("unknown"_ns),
      mStartupFileVersion("0"_ns),
      mMaybeLockProfile(false),
      mUpdateChannel(MOZ_STRINGIFY(MOZ_UPDATE_CHANNEL)),
      mProfileDBExists(false),
      mProfileDBFileSize(0),
      mProfileDBModifiedTime(0) {
#ifdef MOZ_DEV_EDITION
  mUseDevEditionProfile = true;
#endif
}

nsToolkitProfileService::~nsToolkitProfileService() {
  gService = nullptr;
  mProfiles.clear();
}

void nsToolkitProfileService::CompleteStartup() {
  if (!mStartupProfileSelected) {
    return;
  }

  glean::startup::profile_selection_reason.Set(mStartupReason);
  glean::startup::profile_database_version.Set(mStartupFileVersion);
  glean::startup::profile_count.Set(static_cast<uint32_t>(mProfiles.length()));

  nsresult rv;
  bool needsFlush = false;

  nsCOMPtr<nsIPrefBranch> prefs = do_GetService(NS_PREFSERVICE_CONTRACTID);
  nsCString storeID;
  rv = prefs->GetCharPref(STORE_ID_PREF, storeID);

  if (NS_SUCCEEDED(rv) && !storeID.IsEmpty()) {
    // We have a storeID from prefs.
    if (!mCurrent) {
      // We started into an unmanaged profile. Try to set the group profile to
      // be the managed profile belonging to the group.
      mGroupProfile = GetProfileByStoreID(storeID);
    }
  } else if (mCurrent && !mCurrent->mStoreID.IsVoid()) {
    // No store ID in prefs. If the current profile has one we will use it.
    mGroupProfile = mCurrent;
    rv = prefs->SetCharPref(STORE_ID_PREF, mCurrent->mStoreID);
    NS_ENSURE_SUCCESS_VOID(rv);
  }

  if (mMaybeLockProfile) {
    nsCOMPtr<nsIToolkitShellService> shell =
        do_GetService(NS_TOOLKITSHELLSERVICE_CONTRACTID);
    if (shell) {
      bool isDefaultApp;
      rv = shell->IsDefaultApplication(&isDefaultApp);
      if (NS_SUCCEEDED(rv) && isDefaultApp) {
        mProfileDB.SetString(mInstallSection.get(), "Locked", "1");

        needsFlush = true;
      }
    }
  }

  if (needsFlush) {
    // There is a very small chance that this could fail if something else
    // overwrote the profiles database since we started up, probably less than
    // a second ago. There isn't really a sane response here, all the other
    // profile changes are already flushed so whether we fail to flush here or
    // force quit the app makes no difference.
    NS_ENSURE_SUCCESS_VOID(Flush());
  }
}

// Tests whether the passed profile was last used by this install.
bool nsToolkitProfileService::IsProfileForCurrentInstall(
    nsToolkitProfile* aProfile) {
  nsCOMPtr<nsIFile> compatFile;
  nsresult rv = aProfile->mRootDir->Clone(getter_AddRefs(compatFile));
  NS_ENSURE_SUCCESS(rv, false);

  rv = compatFile->Append(COMPAT_FILE);
  NS_ENSURE_SUCCESS(rv, false);

  nsINIParser compatData;
  rv = compatData.Init(compatFile);
  NS_ENSURE_SUCCESS(rv, false);

  /**
   * In xpcshell gDirServiceProvider doesn't have all the correct directories
   * set so using NS_GetSpecialDirectory works better there. But in a normal
   * app launch the component registry isn't initialized so
   * NS_GetSpecialDirectory doesn't work. So we have to use two different
   * paths to support testing.
   */
  nsCOMPtr<nsIFile> currentGreDir;
  rv = NS_GetSpecialDirectory(NS_GRE_DIR, getter_AddRefs(currentGreDir));
  if (rv == NS_ERROR_NOT_INITIALIZED) {
    currentGreDir = gDirServiceProvider->GetGREDir();
    MOZ_ASSERT(currentGreDir, "No GRE dir found.");
  } else if (NS_FAILED(rv)) {
    return false;
  }

  nsCString lastGreDirStr;
  rv = compatData.GetString("Compatibility", "LastPlatformDir", lastGreDirStr);
  // If this string is missing then this profile is from an ancient version.
  // We'll opt to use it in this case.
  if (NS_FAILED(rv)) {
    return true;
  }

  nsCOMPtr<nsIFile> lastGreDir;
  rv = NS_NewLocalFileWithPersistentDescriptor(lastGreDirStr,
                                               getter_AddRefs(lastGreDir));
  NS_ENSURE_SUCCESS(rv, false);

#ifdef XP_WIN
#  if defined(MOZ_THUNDERBIRD) || defined(MOZ_SUITE)
  mozilla::PathString lastGreDirPath, currentGreDirPath;
  lastGreDirPath = lastGreDir->NativePath();
  currentGreDirPath = currentGreDir->NativePath();
  if (lastGreDirPath.Equals(currentGreDirPath,
                            nsCaseInsensitiveStringComparator)) {
    return true;
  }

  // Convert a 64-bit install path to what would have been the 32-bit install
  // path to allow users to migrate their profiles from one to the other.
  PWSTR pathX86 = nullptr;
  HRESULT hres =
      SHGetKnownFolderPath(FOLDERID_ProgramFilesX86, 0, nullptr, &pathX86);
  if (SUCCEEDED(hres)) {
    nsDependentString strPathX86(pathX86);
    if (!StringBeginsWith(currentGreDirPath, strPathX86,
                          nsCaseInsensitiveStringComparator)) {
      PWSTR path = nullptr;
      hres = SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, nullptr, &path);
      if (SUCCEEDED(hres)) {
        if (StringBeginsWith(currentGreDirPath, nsDependentString(path),
                             nsCaseInsensitiveStringComparator)) {
          currentGreDirPath.Replace(0, wcslen(path), strPathX86);
        }
      }
      CoTaskMemFree(path);
    }
  }
  CoTaskMemFree(pathX86);

  return lastGreDirPath.Equals(currentGreDirPath,
                               nsCaseInsensitiveStringComparator);
#  endif
#endif

  bool equal;
  rv = lastGreDir->Equals(currentGreDir, &equal);
  NS_ENSURE_SUCCESS(rv, false);

  return equal;
}

/**
 * Used the first time an install with dedicated profile support runs. Decides
 * whether to mark the passed profile as the default for this install.
 *
 * The goal is to reduce disruption but ideally end up with the OS default
 * install using the old default profile.
 *
 * If the decision is to use the profile then it will be unassigned as the
 * dedicated default for other installs.
 *
 * We won't attempt to use the profile if it was last used by a different
 * install.
 *
 * If the profile is currently in use by an install that was either the OS
 * default install or the profile has been explicitely chosen by some other
 * means then we won't use it.
 *
 * aResult will be set to true if we chose to make the profile the new dedicated
 * default.
 */
nsresult nsToolkitProfileService::MaybeMakeDefaultDedicatedProfile(
    nsToolkitProfile* aProfile, bool* aResult) {
  nsresult rv;
  *aResult = false;

  // If the profile was last used by a different install then we won't use it.
  if (!IsProfileForCurrentInstall(aProfile)) {
    return NS_OK;
  }

  nsCString descriptor;
  rv = GetProfileDescriptor(aProfile, descriptor, nullptr);
  NS_ENSURE_SUCCESS(rv, rv);

  // Get a list of all the installs.
  nsTArray<nsCString> installs = GetKnownInstalls();

  // Cache the installs that use the profile.
  nsTArray<nsCString> inUseInstalls;

  // See if the profile is already in use by an install that hasn't locked it.
  for (uint32_t i = 0; i < installs.Length(); i++) {
    const nsCString& install = installs[i];

    nsCString path;
    rv = mProfileDB.GetString(install.get(), "Default", path);
    if (NS_FAILED(rv)) {
      continue;
    }

    // Is this install using the profile we care about?
    if (!descriptor.Equals(path)) {
      continue;
    }

    // Is this profile locked to this other install?
    nsCString isLocked;
    rv = mProfileDB.GetString(install.get(), "Locked", isLocked);
    if (NS_SUCCEEDED(rv) && isLocked.Equals("1")) {
      return NS_OK;
    }

    inUseInstalls.AppendElement(install);
  }

  // At this point we've decided to take the profile. Strip it from other
  // installs.
  for (uint32_t i = 0; i < inUseInstalls.Length(); i++) {
    // Removing the default setting entirely will make the install go through
    // the first run process again at startup and create itself a new profile.
    mProfileDB.DeleteString(inUseInstalls[i].get(), "Default");
  }

  // Set this as the default profile for this install.
  SetDefaultProfile(aProfile);

  // SetDefaultProfile will have locked this profile to this install so no
  // other installs will steal it, but this was auto-selected so we want to
  // unlock it so that other installs can potentially take it.
  mProfileDB.DeleteString(mInstallSection.get(), "Locked");

  // Persist the changes.
  rv = Flush();
  NS_ENSURE_SUCCESS(rv, rv);

  // Once XPCOM is available check if this is the default application and if so
  // lock the profile again.
  mMaybeLockProfile = true;
  *aResult = true;

  return NS_OK;
}

bool IsFileOutdated(nsIFile* aFile, bool aExists, PRTime aLastModified,
                    int64_t aLastSize) {
  nsCOMPtr<nsIFile> file;
  nsresult rv = aFile->Clone(getter_AddRefs(file));
  if (NS_FAILED(rv)) {
    return false;
  }

  bool exists;
  rv = aFile->Exists(&exists);
  if (NS_FAILED(rv) || exists != aExists) {
    return true;
  }

  if (!exists) {
    return false;
  }

  int64_t size;
  rv = aFile->GetFileSize(&size);
  if (NS_FAILED(rv) || size != aLastSize) {
    return true;
  }

  PRTime time;
  rv = aFile->GetLastModifiedTime(&time);
  return NS_FAILED(rv) || time != aLastModified;
}

nsresult UpdateFileStats(nsIFile* aFile, bool* aExists, PRTime* aLastModified,
                         int64_t* aLastSize) {
  nsCOMPtr<nsIFile> file;
  nsresult rv = aFile->Clone(getter_AddRefs(file));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = file->Exists(aExists);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!(*aExists)) {
    *aLastModified = 0;
    *aLastSize = 0;
    return NS_OK;
  }

  rv = file->GetFileSize(aLastSize);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = file->GetLastModifiedTime(aLastModified);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
nsToolkitProfileService::GetIsListOutdated(bool* aResult) {
  *aResult = IsFileOutdated(mProfileDBFile, mProfileDBExists,
                            mProfileDBModifiedTime, mProfileDBFileSize);
  return NS_OK;
}

nsresult nsToolkitProfileService::Init() {
  NS_ASSERTION(gDirServiceProvider, "No dirserviceprovider!");
  nsresult rv;

  rv = nsXREDirProvider::GetUserAppDataDirectory(getter_AddRefs(mAppData));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = nsXREDirProvider::GetUserLocalDataDirectory(getter_AddRefs(mTempData));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mAppData->Clone(getter_AddRefs(mProfileDBFile));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mProfileDBFile->AppendNative("profiles.ini"_ns);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mAppData->Clone(getter_AddRefs(mInstallDBFile));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mInstallDBFile->AppendNative("installs.ini"_ns);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString buffer;

  rv = UpdateFileStats(mProfileDBFile, &mProfileDBExists,
                       &mProfileDBModifiedTime, &mProfileDBFileSize);
  if (NS_SUCCEEDED(rv) && mProfileDBExists) {
    rv = mProfileDB.Init(mProfileDBFile);
    // Init does not fail on parsing errors, only on OOM/really unexpected
    // conditions.
    if (NS_FAILED(rv)) {
      return rv;
    }

    rv = mProfileDB.GetString("General", "StartWithLastProfile", buffer);
    if (NS_SUCCEEDED(rv)) {
      mStartWithLast = !buffer.EqualsLiteral("0");
    }

    rv = mProfileDB.GetString("General", "Version", mStartupFileVersion);
    if (NS_FAILED(rv)) {
      // This is a profiles.ini written by an older version. We must restore
      // any install data from the backup. We consider this old format to be
      // a version 1 file.
      mStartupFileVersion.AssignLiteral("1");
      nsINIParser installDB;

      if (NS_SUCCEEDED(installDB.Init(mInstallDBFile))) {
        // There is install data to import.
        installDB.GetSections([installDB = &installDB,
                               profileDB = &mProfileDB](const char* aSection) {
          nsTArray<UniquePtr<KeyValue>> strings =
              GetSectionStrings(installDB, aSection);
          if (strings.IsEmpty()) {
            return true;
          }

          nsCString newSection(INSTALL_PREFIX);
          newSection.Append(aSection);

          for (uint32_t i = 0; i < strings.Length(); i++) {
            profileDB->SetString(newSection.get(), strings[i]->key.get(),
                                 strings[i]->value.get());
          }

          return true;
        });
      }

      rv = mProfileDB.SetString("General", "Version", PROFILE_DB_VERSION);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  } else {
    rv = mProfileDB.SetString("General", "StartWithLastProfile",
                              mStartWithLast ? "1" : "0");
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mProfileDB.SetString("General", "Version", PROFILE_DB_VERSION);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  nsCString installProfilePath;

  if (mUseDedicatedProfile) {
    nsString installHash;
    rv = gDirServiceProvider->GetInstallHash(installHash);
    NS_ENSURE_SUCCESS(rv, rv);
    CopyUTF16toUTF8(installHash, mInstallSection);
    mInstallSection.Insert(INSTALL_PREFIX, 0);

    // Try to find the descriptor for the default profile for this install.
    rv = mProfileDB.GetString(mInstallSection.get(), "Default",
                              installProfilePath);

    // Not having a value means this install doesn't appear in installs.ini so
    // this is the first run for this install.
    if (NS_FAILED(rv)) {
      mIsFirstRun = true;

      // Gets the install section that would have been created if the install
      // path has incorrect casing (see bug 1555319). We use this later during
      // profile selection.
      rv = gDirServiceProvider->GetLegacyInstallHash(installHash);
      NS_ENSURE_SUCCESS(rv, rv);
      CopyUTF16toUTF8(installHash, mLegacyInstallSection);
      mLegacyInstallSection.Insert(INSTALL_PREFIX, 0);
    } else {
      mIsFirstRun = false;
    }
  }

  nsToolkitProfile* currentProfile = nullptr;

#ifdef MOZ_DEV_EDITION
  nsCOMPtr<nsIFile> ignoreDevEditionProfile;
  rv = mAppData->Clone(getter_AddRefs(ignoreDevEditionProfile));
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = ignoreDevEditionProfile->AppendNative("ignore-dev-edition-profile"_ns);
  if (NS_FAILED(rv)) {
    return rv;
  }

  bool shouldIgnoreSeparateProfile;
  rv = ignoreDevEditionProfile->Exists(&shouldIgnoreSeparateProfile);
  if (NS_FAILED(rv)) return rv;

  mUseDevEditionProfile = !shouldIgnoreSeparateProfile;
#endif

  RefPtr<nsToolkitProfile> autoSelectProfile;

  unsigned int nonDevEditionProfiles = 0;
  unsigned int c = 0;
  for (c = 0; true; ++c) {
    nsAutoCString profileID("Profile");
    profileID.AppendInt(c);

    rv = mProfileDB.GetString(profileID.get(), "IsRelative", buffer);
    if (NS_FAILED(rv)) break;

    bool isRelative = buffer.EqualsLiteral("1");

    nsAutoCString filePath;

    rv = mProfileDB.GetString(profileID.get(), "Path", filePath);
    if (NS_FAILED(rv)) {
      NS_ERROR("Malformed profiles.ini: Path= not found");
      continue;
    }

    nsAutoCString name;

    rv = mProfileDB.GetString(profileID.get(), "Name", name);
    if (NS_FAILED(rv)) {
      NS_ERROR("Malformed profiles.ini: Name= not found");
      continue;
    }

    nsCOMPtr<nsIFile> rootDir;
    if (isRelative) {
      rv = NS_NewLocalFileWithRelativeDescriptor(mAppData, filePath,
                                                 getter_AddRefs(rootDir));
    } else {
      rv = NS_NewLocalFileWithPersistentDescriptor(filePath,
                                                   getter_AddRefs(rootDir));
    }
    if (NS_FAILED(rv)) continue;

    nsCOMPtr<nsIFile> localDir;
    rv = nsToolkitProfileService::gService->GetLocalDirFromRootDir(
        rootDir, getter_AddRefs(localDir));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCString storeID;
    bool showProfileSelector = false;

    rv = mProfileDB.GetString(profileID.get(), "StoreID", storeID);

    // If the StoreID was not found, just set it to an empty string.
    if (NS_FAILED(rv) && rv == NS_ERROR_FAILURE) {
      storeID = VoidCString();
    }

    // Only get the ShowSelector value if StoreID is nonempty.
    if (!storeID.IsVoid()) {
      rv = mProfileDB.GetString(profileID.get(), "ShowSelector", buffer);
      if (NS_SUCCEEDED(rv)) {
        showProfileSelector = buffer.EqualsLiteral("1");
      }
    }

    currentProfile = new nsToolkitProfile(name, rootDir, localDir, true,
                                          storeID, showProfileSelector);

    // If a user has modified the ini file path it may make for a valid profile
    // path but not match what we would have serialised and so may not match
    // the path in the install section. Re-serialise it to get it in the
    // expected form again.
    bool nowRelative;
    nsCString descriptor;
    GetProfileDescriptor(currentProfile, descriptor, &nowRelative);

    if (isRelative != nowRelative || !descriptor.Equals(filePath)) {
      mProfileDB.SetString(profileID.get(), "IsRelative",
                           nowRelative ? "1" : "0");
      mProfileDB.SetString(profileID.get(), "Path", descriptor.get());

      // Should we flush now? It costs some startup time and we will fix it on
      // the next startup anyway. If something else causes a flush then it will
      // be fixed in the ini file then.
    }

    rv = mProfileDB.GetString(profileID.get(), "Default", buffer);
    if (NS_SUCCEEDED(rv) && buffer.EqualsLiteral("1")) {
      mNormalDefault = currentProfile;
    }

    // Is this the default profile for this install?
    if (mUseDedicatedProfile && !mDedicatedProfile &&
        installProfilePath.Equals(descriptor)) {
      // Found a profile for this install.
      mDedicatedProfile = currentProfile;
    }

    if (name.EqualsLiteral(DEV_EDITION_NAME)) {
      mDevEditionDefault = currentProfile;
    } else {
      nonDevEditionProfiles++;
      autoSelectProfile = currentProfile;
    }
  }

  // If there is only one non-dev-edition profile then mark it as the default.
  if (!mNormalDefault && nonDevEditionProfiles == 1) {
    SetNormalDefault(autoSelectProfile);
  }

  if (!mUseDedicatedProfile) {
    if (mUseDevEditionProfile) {
      // When using the separate dev-edition profile not finding it means this
      // is a first run.
      mIsFirstRun = !mDevEditionDefault;
    } else {
      // If there are no normal profiles then this is a first run.
      mIsFirstRun = nonDevEditionProfiles == 0;
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsToolkitProfileService::SetStartWithLastProfile(bool aValue) {
  if (mStartWithLast != aValue) {
    // Note: the skeleton ui (see PreXULSkeletonUI.cpp) depends on this
    // having this name and being under General. If that ever changes,
    // the skeleton UI will just need to be updated. If it changes frequently,
    // it's probably best we just mirror the value to the registry here.
    nsresult rv = mProfileDB.SetString("General", "StartWithLastProfile",
                                       aValue ? "1" : "0");
    NS_ENSURE_SUCCESS(rv, rv);
    mStartWithLast = aValue;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsToolkitProfileService::GetStartWithLastProfile(bool* aResult) {
  *aResult = mStartWithLast;
  return NS_OK;
}

NS_IMETHODIMP
nsToolkitProfileService::GetProfiles(nsISimpleEnumerator** aResult) {
  *aResult = new ProfileEnumerator(mProfiles.getFirst());

  NS_ADDREF(*aResult);
  return NS_OK;
}

NS_IMETHODIMP
nsToolkitProfileService::ProfileEnumerator::HasMoreElements(bool* aResult) {
  *aResult = mCurrent ? true : false;
  return NS_OK;
}

NS_IMETHODIMP
nsToolkitProfileService::ProfileEnumerator::GetNext(nsISupports** aResult) {
  if (!mCurrent) return NS_ERROR_FAILURE;

  NS_ADDREF(*aResult = mCurrent);

  mCurrent = mCurrent->getNext();
  return NS_OK;
}

NS_IMETHODIMP
nsToolkitProfileService::GetCurrentProfile(nsIToolkitProfile** aResult) {
  NS_IF_ADDREF(*aResult = mCurrent);
  return NS_OK;
}

NS_IMETHODIMP
nsToolkitProfileService::GetGroupProfile(nsIToolkitProfile** aResult) {
  NS_IF_ADDREF(*aResult = mGroupProfile);
  return NS_OK;
}

NS_IMETHODIMP
nsToolkitProfileService::GetDefaultProfile(nsIToolkitProfile** aResult) {
  RefPtr<nsToolkitProfile> profile = GetDefaultProfile();
  profile.forget(aResult);
  return NS_OK;
}

already_AddRefed<nsToolkitProfile>
nsToolkitProfileService::GetDefaultProfile() {
  if (mUseDedicatedProfile) {
    return do_AddRef(mDedicatedProfile);
  }

  if (mUseDevEditionProfile) {
    return do_AddRef(mDevEditionDefault);
  }

  return do_AddRef(mNormalDefault);
}

void nsToolkitProfileService::SetNormalDefault(nsToolkitProfile* aProfile) {
  if (mNormalDefault == aProfile) {
    return;
  }

  if (mNormalDefault) {
    mProfileDB.DeleteString(mNormalDefault->mSection.get(), "Default");
  }

  mNormalDefault = aProfile;

  if (mNormalDefault) {
    mProfileDB.SetString(mNormalDefault->mSection.get(), "Default", "1");
  }
}

NS_IMETHODIMP
nsToolkitProfileService::SetDefaultProfile(nsIToolkitProfile* aProfile) {
  nsToolkitProfile* profile = static_cast<nsToolkitProfile*>(aProfile);

  if (mUseDedicatedProfile) {
    if (mDedicatedProfile != profile) {
      if (!profile) {
        // Setting this to the empty string means no profile will be found on
        // startup but we'll recognise that this install has been used
        // previously.
        mProfileDB.SetString(mInstallSection.get(), "Default", "");
      } else {
        nsCString profilePath;
        nsresult rv = GetProfileDescriptor(profile, profilePath, nullptr);
        NS_ENSURE_SUCCESS(rv, rv);

        mProfileDB.SetString(mInstallSection.get(), "Default",
                             profilePath.get());
      }
      mDedicatedProfile = profile;

      // Some kind of choice has happened here, lock this profile to this
      // install.
      mProfileDB.SetString(mInstallSection.get(), "Locked", "1");
    }
    return NS_OK;
  }

  if (mUseDevEditionProfile && profile != mDevEditionDefault) {
    // The separate profile is hardcoded.
    return NS_ERROR_FAILURE;
  }

  SetNormalDefault(profile);

  return NS_OK;
}

// Gets the profile root directory descriptor for storing in profiles.ini or
// installs.ini.
nsresult nsToolkitProfileService::GetProfileDescriptor(
    nsToolkitProfile* aProfile, nsACString& aDescriptor, bool* aIsRelative) {
  return GetProfileDescriptor(aProfile->mRootDir, aDescriptor, aIsRelative);
}

nsresult nsToolkitProfileService::GetProfileDescriptor(nsIFile* aRootDir,
                                                       nsACString& aDescriptor,
                                                       bool* aIsRelative) {
  // if the profile dir is relative to appdir...
  bool isRelative;
  nsresult rv = mAppData->Contains(aRootDir, &isRelative);

  nsCString profilePath;
  if (NS_SUCCEEDED(rv) && isRelative) {
    // we use a relative descriptor
    rv = aRootDir->GetRelativeDescriptor(mAppData, profilePath);
  } else {
    // otherwise, a persistent descriptor
    rv = aRootDir->GetPersistentDescriptor(profilePath);
  }
  NS_ENSURE_SUCCESS(rv, rv);

  aDescriptor.Assign(profilePath);
  if (aIsRelative) {
    *aIsRelative = isRelative;
  }

  return NS_OK;
}

nsresult nsToolkitProfileService::CreateDefaultProfile(
    nsToolkitProfile** aResult) {
  // Create a new default profile
  nsAutoCString name;
  if (mUseDevEditionProfile) {
    name.AssignLiteral(DEV_EDITION_NAME);
  } else if (mUseDedicatedProfile) {
    name.AppendPrintf("default-%s", mUpdateChannel.get());
  } else {
    name.AssignLiteral(DEFAULT_NAME);
  }

  nsresult rv = CreateUniqueProfile(nullptr, name, aResult);
  NS_ENSURE_SUCCESS(rv, rv);

  if (mUseDedicatedProfile) {
    SetDefaultProfile(mCurrent);
  } else if (mUseDevEditionProfile) {
    mDevEditionDefault = mCurrent;
  } else {
    SetNormalDefault(mCurrent);
  }

  return NS_OK;
}

/**
 * An implementation of SelectStartupProfile callable from JavaScript via XPCOM.
 * See nsIToolkitProfileService.idl.
 */
NS_IMETHODIMP
nsToolkitProfileService::SelectStartupProfile(
    const nsTArray<nsCString>& aArgv, bool aIsResetting,
    const nsACString& aUpdateChannel, const nsACString& aLegacyInstallHash,
    nsIFile** aRootDir, nsIFile** aLocalDir, nsIToolkitProfile** aProfile,
    bool* aDidCreate) {
  int argc = aArgv.Length();
  // Our command line handling expects argv to be null-terminated so construct
  // an appropriate array.
  auto argv = MakeUnique<char*[]>(argc + 1);
  // Also, our command line handling removes things from the array without
  // freeing them so keep track of what we've created separately.
  auto allocated = MakeUnique<UniqueFreePtr<char>[]>(argc);

  for (int i = 0; i < argc; i++) {
    allocated[i].reset(ToNewCString(aArgv[i]));
    argv[i] = allocated[i].get();
  }
  argv[argc] = nullptr;

  mUpdateChannel = aUpdateChannel;
  if (!aLegacyInstallHash.IsEmpty()) {
    mLegacyInstallSection.Assign(aLegacyInstallHash);
    mLegacyInstallSection.Insert(INSTALL_PREFIX, 0);
  }

  bool wasDefault;
  nsresult rv =
      SelectStartupProfile(&argc, argv.get(), aIsResetting, aRootDir, aLocalDir,
                           aProfile, aDidCreate, &wasDefault);

  // Since we were called outside of the normal startup path complete any
  // startup tasks.
  if (NS_SUCCEEDED(rv)) {
    CompleteStartup();
  }

  return rv;
}

static void SaltProfileName(nsACString& aName);

nsresult EnsureDirExists(nsIFile* aPath) {
  bool isDir;
  nsresult rv = aPath->IsDirectory(&isDir);
  if (NS_SUCCEEDED(rv)) {
    return isDir ? NS_OK : NS_ERROR_FILE_NOT_DIRECTORY;
  }
  if (rv != NS_ERROR_FILE_NOT_FOUND) {
    return rv;
  }
  return aPath->Create(nsIFile::DIRECTORY_TYPE, 0700);
}

/**
 * Selects or creates a profile to use based on the profiles database, any
 * environment variables and any command line arguments. Will not create
 * a profile if aIsResetting is true. The profile is selected based on this
 * order of preference:
 * * Environment variables (set when restarting the application).
 * * --profile command line argument.
 * * --createprofile command line argument (this also causes the app to exit).
 * * -p command line argument.
 * * A new profile created if this is the first run of the application.
 * * The default profile.
 * aRootDir and aLocalDir are set to the data and local directories for the
 * profile data. If a profile from the database was selected it will be
 * returned in aProfile.
 * aDidCreate will be set to true if a new profile was created.
 * This function should be called once at startup and will fail if called again.
 * aArgv should be an array of aArgc + 1 strings, the last element being null.
 * Both aArgv and aArgc will be mutated.
 */
nsresult nsToolkitProfileService::SelectStartupProfile(
    int* aArgc, char* aArgv[], bool aIsResetting, nsIFile** aRootDir,
    nsIFile** aLocalDir, nsIToolkitProfile** aProfile, bool* aDidCreate,
    bool* aWasDefaultSelection) {
  if (mStartupProfileSelected) {
    return NS_ERROR_ALREADY_INITIALIZED;
  }

  mStartupProfileSelected = true;
  *aDidCreate = false;
  *aWasDefaultSelection = false;

  nsresult rv;
  const char* arg;

  // Use the profile specified in the environment variables (generally from an
  // app initiated restart).
  nsCOMPtr<nsIFile> lf = GetFileFromEnv("XRE_PROFILE_PATH");
  if (lf) {
    nsCOMPtr<nsIFile> localDir = GetFileFromEnv("XRE_PROFILE_LOCAL_PATH");
    if (!localDir) {
      rv = nsToolkitProfileService::gService->GetLocalDirFromRootDir(
          lf, getter_AddRefs(localDir));
      NS_ENSURE_SUCCESS(rv, rv);
    }

    // Clear out flags that we handled (or should have handled!) last startup.
    const char* dummy;
    CheckArg(*aArgc, aArgv, "p", &dummy);
    CheckArg(*aArgc, aArgv, "profile", &dummy);
    CheckArg(*aArgc, aArgv, "profilemanager");

    RefPtr<nsToolkitProfile> profile;
    GetProfileByDir(lf, localDir, getter_AddRefs(profile));

    if (profile && mIsFirstRun && mUseDedicatedProfile) {
      if (profile ==
          (mUseDevEditionProfile ? mDevEditionDefault : mNormalDefault)) {
        // This is the first run of a dedicated profile build where the selected
        // profile is the previous default so we should either make it the
        // default profile for this install or push the user to a new profile.

        bool result;
        rv = MaybeMakeDefaultDedicatedProfile(profile, &result);
        NS_ENSURE_SUCCESS(rv, rv);
        if (result) {
          mStartupReason = "restart-claimed-default"_ns;

          mCurrent = profile;
        } else {
          rv = CreateDefaultProfile(getter_AddRefs(mCurrent));
          if (NS_FAILED(rv)) {
            *aProfile = nullptr;
            return rv;
          }

          rv = Flush();
          NS_ENSURE_SUCCESS(rv, rv);

          mStartupReason = "restart-skipped-default"_ns;
          *aDidCreate = true;
        }

        NS_IF_ADDREF(*aProfile = mCurrent);
        mCurrent->GetRootDir(aRootDir);
        mCurrent->GetLocalDir(aLocalDir);

        return NS_OK;
      }
    }

    if (EnvHasValue("XRE_RESTARTED_BY_PROFILE_MANAGER")) {
      mStartupReason = "profile-manager"_ns;
    } else if (EnvHasValue("XRE_RESTARTED_BY_PROFILE_SELECTOR")) {
      mStartupReason = "profile-selector"_ns;
    } else if (aIsResetting) {
      mStartupReason = "profile-reset"_ns;
    } else {
      mStartupReason = "restart"_ns;
    }

    mCurrent = profile;
    lf.forget(aRootDir);
    localDir.forget(aLocalDir);
    NS_IF_ADDREF(*aProfile = profile);
    return NS_OK;
  }

  // Check the -profile command line argument. It accepts a single argument that
  // gives the path to use for the profile.
  ArgResult ar = CheckArg(*aArgc, aArgv, "profile", &arg);
  if (ar == ARG_BAD) {
    PR_fprintf(PR_STDERR, "Error: argument --profile requires a path\n");
    return NS_ERROR_FAILURE;
  }
  if (ar) {
    nsCOMPtr<nsIFile> lf;
    rv = XRE_GetFileFromPath(arg, getter_AddRefs(lf));
    NS_ENSURE_SUCCESS(rv, rv);

    // Make sure that the profile path exists and it's a directory.
    rv = EnsureDirExists(lf);
    if (NS_FAILED(rv)) {
      PR_fprintf(PR_STDERR,
                 "Error: argument --profile requires a path to a directory\n");
      return NS_ERROR_FAILURE;
    }

    mStartupReason = "argument-profile"_ns;

    GetProfileByDir(lf, nullptr, getter_AddRefs(mCurrent));
    NS_ADDREF(*aRootDir = lf);

    nsCOMPtr<nsIFile> localDir;
    rv = nsToolkitProfileService::gService->GetLocalDirFromRootDir(
        lf, getter_AddRefs(localDir));
    NS_ENSURE_SUCCESS(rv, rv);

    NS_IF_ADDREF(*aProfile = mCurrent);

    localDir.forget(aLocalDir);

    return NS_OK;
  }

  // Check the -createprofile command line argument. It accepts a single
  // argument that is either the name for the new profile or the name followed
  // by the path to use.
  ar = CheckArg(*aArgc, aArgv, "createprofile", &arg, CheckArgFlag::RemoveArg);
  if (ar == ARG_BAD) {
    PR_fprintf(PR_STDERR,
               "Error: argument --createprofile requires a profile name\n");
    return NS_ERROR_FAILURE;
  }
  if (ar) {
    const char* delim = strchr(arg, ' ');
    nsCOMPtr<nsIToolkitProfile> profile;
    if (delim) {
      nsCOMPtr<nsIFile> lf;
      rv = NS_NewNativeLocalFile(nsDependentCString(delim + 1),
                                 getter_AddRefs(lf));
      if (NS_FAILED(rv)) {
        PR_fprintf(PR_STDERR, "Error: profile path not valid.\n");
        return rv;
      }

      // As with --profile, assume that the given path will be used for the
      // main profile directory.
      rv = CreateProfile(lf, nsDependentCSubstring(arg, delim),
                         getter_AddRefs(profile));
    } else {
      rv = CreateProfile(nullptr, nsDependentCString(arg),
                         getter_AddRefs(profile));
    }
    // Some pathological arguments can make it this far
    if (NS_FAILED(rv) || NS_FAILED(Flush())) {
      PR_fprintf(PR_STDERR, "Error creating profile.\n");
    }
    return NS_ERROR_ABORT;
  }

  // Check the -p command line argument. It either accepts a profile name and
  // uses that named profile or without a name it opens the profile manager.
  ar = CheckArg(*aArgc, aArgv, "p", &arg);
  if (ar == ARG_BAD) {
    return NS_ERROR_SHOW_PROFILE_MANAGER;
  }
  if (ar) {
    mCurrent = GetProfileByName(nsDependentCString(arg));
    if (mCurrent) {
      mStartupReason = "argument-p"_ns;

      mCurrent->GetRootDir(aRootDir);
      mCurrent->GetLocalDir(aLocalDir);

      NS_ADDREF(*aProfile = mCurrent);
      return NS_OK;
    }

    return NS_ERROR_SHOW_PROFILE_MANAGER;
  }

  ar = CheckArg(*aArgc, aArgv, "profilemanager");
  if (ar == ARG_FOUND) {
    return NS_ERROR_SHOW_PROFILE_MANAGER;
  }

#ifdef MOZ_BACKGROUNDTASKS
  if (BackgroundTasks::IsBackgroundTaskMode()) {
    // There are two cases:
    // 1. ephemeral profile: create a new one in temporary directory.
    // 2. non-ephemeral (persistent) profile:
    //    a. if no salted profile is known, create a new one in
    //       background task-specific directory.
    //    b. if salted profile is know, use salted path.
    nsString installHash;
    rv = gDirServiceProvider->GetInstallHash(installHash);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCString profilePrefix(BackgroundTasks::GetProfilePrefix(
        NS_LossyConvertUTF16toASCII(installHash)));

    nsCString taskName(BackgroundTasks::GetBackgroundTasks().ref());

    nsCOMPtr<nsIFile> file;

    if (BackgroundTasks::IsEphemeralProfileTaskName(taskName)) {
      // Background task mode does not enable legacy telemetry, so this is for
      // completeness and testing only.
      mStartupReason = "backgroundtask-ephemeral"_ns;

      nsCOMPtr<nsIFile> rootDir;
      rv = GetSpecialSystemDirectory(OS_TemporaryDirectory,
                                     getter_AddRefs(rootDir));
      NS_ENSURE_SUCCESS(rv, rv);

      nsresult rv = BackgroundTasks::CreateEphemeralProfileDirectory(
          rootDir, profilePrefix, getter_AddRefs(file));
      if (NS_WARN_IF(NS_FAILED(rv))) {
        // In background task mode, NS_ERROR_UNEXPECTED is handled specially to
        // exit with a non-zero exit code.
        return NS_ERROR_UNEXPECTED;
      }
      *aDidCreate = true;
    } else {
      // Background task mode does not enable legacy telemetry, so this is for
      // completeness and testing only.
      mStartupReason = "backgroundtask-not-ephemeral"_ns;

      // A non-ephemeral profile is required.
      nsCOMPtr<nsIFile> rootDir;
      nsresult rv = gDirServiceProvider->GetBackgroundTasksProfilesRootDir(
          getter_AddRefs(rootDir));
      NS_ENSURE_SUCCESS(rv, rv);

      nsAutoCString buffer;
      rv = mProfileDB.GetString("BackgroundTasksProfiles", profilePrefix.get(),
                                buffer);
      bool exists = false;

      if (NS_SUCCEEDED(rv)) {
        // We have a record of one!  Use it.
        rv = rootDir->Clone(getter_AddRefs(file));
        NS_ENSURE_SUCCESS(rv, rv);

        rv = file->AppendNative(buffer);
        NS_ENSURE_SUCCESS(rv, rv);

        rv = file->Exists(&exists);
        NS_ENSURE_SUCCESS(rv, rv);

        if (!exists) {
          printf_stderr(
              "Profile directory does not exist, create a new directory");
        }
      }

      if (!exists) {
        nsCString saltedProfilePrefix = profilePrefix;
        SaltProfileName(saltedProfilePrefix);

        nsresult rv = BackgroundTasks::CreateNonEphemeralProfileDirectory(
            rootDir, saltedProfilePrefix, getter_AddRefs(file));
        if (NS_WARN_IF(NS_FAILED(rv))) {
          // In background task mode, NS_ERROR_UNEXPECTED is handled specially
          // to exit with a non-zero exit code.
          return NS_ERROR_UNEXPECTED;
        }
        *aDidCreate = true;

        // Keep a record of the salted name.  It's okay if this doesn't succeed:
        // not great, but it's better for tasks (particularly,
        // `backgroundupdate`) to run and not persist state correctly than to
        // not run at all.
        rv =
            mProfileDB.SetString("BackgroundTasksProfiles", profilePrefix.get(),
                                 saltedProfilePrefix.get());
        Unused << NS_WARN_IF(NS_FAILED(rv));

        if (NS_SUCCEEDED(rv)) {
          rv = Flush();
          Unused << NS_WARN_IF(NS_FAILED(rv));
        }
      }
    }

    nsCOMPtr<nsIFile> localDir = file;
    file.forget(aRootDir);
    localDir.forget(aLocalDir);

    // Background tasks never use profiles known to the profile service.
    *aProfile = nullptr;

    return NS_OK;
  }
#endif

  if (mIsFirstRun && mUseDedicatedProfile &&
      !mInstallSection.Equals(mLegacyInstallSection)) {
    // The default profile could be assigned to a hash generated from an
    // incorrectly cased version of the installation directory (see bug
    // 1555319). Ideally we'd do all this while loading profiles.ini but we
    // can't override the legacy section value before that for tests.
    nsCString defaultDescriptor;
    rv = mProfileDB.GetString(mLegacyInstallSection.get(), "Default",
                              defaultDescriptor);

    if (NS_SUCCEEDED(rv)) {
      // There is a default here, need to see if it matches any profiles.
      bool isRelative;
      nsCString descriptor;

      for (RefPtr<nsToolkitProfile> profile : mProfiles) {
        GetProfileDescriptor(profile, descriptor, &isRelative);

        if (descriptor.Equals(defaultDescriptor)) {
          // Found the default profile. Copy the install section over to
          // the correct location. We leave the old info in place for older
          // versions of Firefox to use.
          nsTArray<UniquePtr<KeyValue>> strings =
              GetSectionStrings(&mProfileDB, mLegacyInstallSection.get());
          for (const auto& kv : strings) {
            mProfileDB.SetString(mInstallSection.get(), kv->key.get(),
                                 kv->value.get());
          }

          // Flush now. This causes a small blip in startup but it should be
          // one time only whereas not flushing means we have to do this search
          // on every startup.
          Flush();

          // Now start up with the found profile.
          mDedicatedProfile = profile;
          mIsFirstRun = false;
          break;
        }
      }
    }
  }

  // If this is a first run then create a new profile.
  if (mIsFirstRun) {
    // If we're configured to always show the profile manager then don't create
    // a new profile to use.
    if (!mStartWithLast) {
      return NS_ERROR_SHOW_PROFILE_MANAGER;
    }

    bool skippedDefaultProfile = false;

    if (mUseDedicatedProfile) {
      // This is the first run of a dedicated profile install. We have to decide
      // whether to use the default profile used by non-dedicated-profile
      // installs or to create a new profile.

      // Find what would have been the default profile for old installs.
      RefPtr<nsToolkitProfile> profile = mNormalDefault;
      if (mUseDevEditionProfile) {
        profile = mDevEditionDefault;
      }

      if (profile) {
        nsCOMPtr<nsIFile> rootDir = profile->GetRootDir();

        nsCOMPtr<nsIFile> compat;
        rootDir->Clone(getter_AddRefs(compat));
        compat->Append(COMPAT_FILE);

        bool exists;
        rv = compat->Exists(&exists);
        NS_ENSURE_SUCCESS(rv, rv);

        // If the file is missing then either this is an empty profile (likely
        // generated by bug 1518591) or it is from an ancient version. We'll opt
        // to leave it for older versions in this case.
        if (exists) {
          bool result;
          rv = MaybeMakeDefaultDedicatedProfile(profile, &result);
          NS_ENSURE_SUCCESS(rv, rv);
          if (result) {
            mStartupReason = "firstrun-claimed-default"_ns;

            mCurrent = profile;
            rootDir.forget(aRootDir);
            profile->GetLocalDir(aLocalDir);
            profile.forget(aProfile);
            return NS_OK;
          }

          // We're going to create a new profile for this install even though
          // another default exists.
          skippedDefaultProfile = true;
        }
      }
    }

    rv = CreateDefaultProfile(getter_AddRefs(mCurrent));
    if (NS_SUCCEEDED(rv)) {
#ifdef MOZ_CREATE_LEGACY_PROFILE
      // If there is only one profile and it isn't meant to be the profile that
      // older versions of Firefox use then we must create a default profile
      // for older versions of Firefox to avoid the existing profile being
      // auto-selected.
      if ((mUseDedicatedProfile || mUseDevEditionProfile) &&
          mProfiles.getFirst() == mProfiles.getLast()) {
        RefPtr<nsToolkitProfile> newProfile;
        CreateProfile(nullptr, nsLiteralCString(DEFAULT_NAME),
                      getter_AddRefs(newProfile));
        SetNormalDefault(newProfile);
      }
#endif

      rv = Flush();
      NS_ENSURE_SUCCESS(rv, rv);

      if (skippedDefaultProfile) {
        mStartupReason = "firstrun-skipped-default"_ns;
      } else {
        mStartupReason = "firstrun-created-default"_ns;
      }

      // Use the new profile.
      mCurrent->GetRootDir(aRootDir);
      mCurrent->GetLocalDir(aLocalDir);
      NS_ADDREF(*aProfile = mCurrent);

      *aDidCreate = true;
      return NS_OK;
    }
  }

  mCurrent = GetDefaultProfile();

  // None of the profiles was marked as default (generally only happens if the
  // user modifies profiles.ini manually). Let the user choose.
  if (!mCurrent) {
    return NS_ERROR_SHOW_PROFILE_MANAGER;
  }

  // Let the caller know that the profile was selected by default.
  *aWasDefaultSelection = true;
  mStartupReason = "default"_ns;

  // Use the selected profile.
  mCurrent->GetRootDir(aRootDir);
  mCurrent->GetLocalDir(aLocalDir);
  NS_ADDREF(*aProfile = mCurrent);

  return NS_OK;
}

/**
 * Creates a new profile for reset and mark it as the current profile.
 */
nsresult nsToolkitProfileService::CreateResetProfile(
    nsIToolkitProfile** aNewProfile) {
  nsAutoCString oldProfileName;
  mCurrent->GetName(oldProfileName);

  RefPtr<nsToolkitProfile> newProfile;
  // Make the new profile name the old profile (or "default-") + the time in
  // seconds since epoch for uniqueness.
  nsAutoCString newProfileName;
  if (!oldProfileName.IsEmpty()) {
    newProfileName.Assign(oldProfileName);
    newProfileName.Append("-");
  } else {
    newProfileName.AssignLiteral("default-");
  }
  newProfileName.AppendPrintf("%" PRId64, PR_Now() / 1000);
  nsresult rv = CreateProfile(nullptr,  // choose a default dir for us
                              newProfileName, getter_AddRefs(newProfile));
  if (NS_FAILED(rv)) return rv;

  mCurrent = newProfile;
  newProfile.forget(aNewProfile);

  // Don't flush the changes yet. That will happen once the migration
  // successfully completes.
  return NS_OK;
}

/**
 * This is responsible for deleting the old profile, copying its name to the
 * current profile and if the old profile was default making the new profile
 * default as well.
 */
nsresult nsToolkitProfileService::ApplyResetProfile(
    nsIToolkitProfile* aOldProfile) {
  // If the old profile would have been the default for old installs then mark
  // the new profile as such.
  if (mNormalDefault == aOldProfile) {
    SetNormalDefault(mCurrent);
  }

  if (mUseDedicatedProfile && mDedicatedProfile == aOldProfile) {
    bool wasLocked = false;
    nsCString val;
    if (NS_SUCCEEDED(
            mProfileDB.GetString(mInstallSection.get(), "Locked", val))) {
      wasLocked = val.Equals("1");
    }

    SetDefaultProfile(mCurrent);

    // Make the locked state match if necessary.
    if (!wasLocked) {
      mProfileDB.DeleteString(mInstallSection.get(), "Locked");
    }
  }

  nsCString name;
  nsresult rv = aOldProfile->GetName(name);
  NS_ENSURE_SUCCESS(rv, rv);

  // Don't remove the old profile's files until after we've successfully flushed
  // the profile changes to disk.
  rv = aOldProfile->Remove(false);
  NS_ENSURE_SUCCESS(rv, rv);

  // Switching the name will make this the default for dev-edition if
  // appropriate.
  rv = mCurrent->SetName(name);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = Flush();
  NS_ENSURE_SUCCESS(rv, rv);

  // Now that the profile changes are flushed, try to remove the old profile's
  // files. If we fail the worst that will happen is that an orphan directory is
  // left. Let this run in the background while we start up.
  nsCOMPtr<nsIFile> rootDir = aOldProfile->GetRootDir();
  nsCOMPtr<nsIFile> localDir = aOldProfile->GetLocalDir();
  NS_DispatchBackgroundTask(NS_NewRunnableFunction(
      __func__, [rootDir = rootDir, localDir = localDir]() mutable {
        RemoveProfileFiles(rootDir, localDir, 5);
      }));

  return NS_OK;
}

NS_IMETHODIMP
nsToolkitProfileService::GetProfileByName(const nsACString& aName,
                                          nsIToolkitProfile** aResult) {
  RefPtr<nsToolkitProfile> profile = GetProfileByName(aName);
  if (profile) {
    profile.forget(aResult);
    return NS_OK;
  }

  return NS_ERROR_FAILURE;
}

already_AddRefed<nsToolkitProfile> nsToolkitProfileService::GetProfileByName(
    const nsACString& aName) {
  for (RefPtr<nsToolkitProfile> profile : mProfiles) {
    if (profile->mName.Equals(aName)) {
      return profile.forget();
    }
  }

  return nullptr;
}

already_AddRefed<nsToolkitProfile> nsToolkitProfileService::GetProfileByStoreID(
    const nsACString& aStoreID) {
  if (aStoreID.IsVoid()) {
    return nullptr;
  }

  for (RefPtr<nsToolkitProfile> profile : mProfiles) {
    if (profile->mStoreID.Equals(aStoreID)) {
      return profile.forget();
    }
  }

  return nullptr;
}

/**
 * Finds a profile from the database that uses the given root and local
 * directories.
 */
void nsToolkitProfileService::GetProfileByDir(nsIFile* aRootDir,
                                              nsIFile* aLocalDir,
                                              nsToolkitProfile** aResult) {
  for (RefPtr<nsToolkitProfile> profile : mProfiles) {
    bool equal;
    nsresult rv = profile->mRootDir->Equals(aRootDir, &equal);
    if (NS_SUCCEEDED(rv) && equal) {
      if (!aLocalDir) {
        // If no local directory was given then we will just use the normal
        // local directory for the profile.
        profile.forget(aResult);
        return;
      }

      rv = profile->mLocalDir->Equals(aLocalDir, &equal);
      if (NS_SUCCEEDED(rv) && equal) {
        profile.forget(aResult);
        return;
      }
    }
  }
}

NS_IMETHODIMP
nsToolkitProfileService::GetProfileByDir(nsIFile* aRootDir, nsIFile* aLocalDir,
                                         nsIToolkitProfile** aResult) {
  RefPtr<nsToolkitProfile> result;
  GetProfileByDir(aRootDir, aLocalDir, getter_AddRefs(result));
  result.forget(aResult);

  return NS_OK;
}

nsresult NS_LockProfilePath(nsIFile* aPath, nsIFile* aTempPath,
                            nsIProfileUnlocker** aUnlocker,
                            nsIProfileLock** aResult) {
  RefPtr<nsToolkitProfileLock> lock = new nsToolkitProfileLock();

  nsresult rv = lock->Init(aPath, aTempPath, aUnlocker);
  if (NS_FAILED(rv)) return rv;

  lock.forget(aResult);
  return NS_OK;
}

static void SaltProfileName(nsACString& aName) {
  char salt[9];
  NS_MakeRandomString(salt, 8);
  salt[8] = '.';

  aName.Insert(salt, 0, 9);
}

NS_IMETHODIMP
nsToolkitProfileService::CreateUniqueProfile(nsIFile* aRootDir,
                                             const nsACString& aNamePrefix,
                                             nsIToolkitProfile** aResult) {
  RefPtr<nsToolkitProfile> profile;
  nsresult rv =
      CreateUniqueProfile(aRootDir, aNamePrefix, getter_AddRefs(profile));
  profile.forget(aResult);
  return rv;
}

nsresult nsToolkitProfileService::CreateUniqueProfile(
    nsIFile* aRootDir, const nsACString& aNamePrefix,
    nsToolkitProfile** aResult) {
  nsCOMPtr<nsIToolkitProfile> profile;
  nsresult rv = GetProfileByName(aNamePrefix, getter_AddRefs(profile));
  if (NS_FAILED(rv)) {
    return CreateProfile(aRootDir, aNamePrefix, aResult);
  }

  uint32_t suffix = 1;
  while (true) {
    nsPrintfCString name("%s-%d", PromiseFlatCString(aNamePrefix).get(),
                         suffix);
    rv = GetProfileByName(name, getter_AddRefs(profile));
    if (NS_FAILED(rv)) {
      return CreateProfile(aRootDir, name, aResult);
    }
    suffix++;
  }
}

NS_IMETHODIMP
nsToolkitProfileService::CreateProfile(nsIFile* aRootDir,
                                       const nsACString& aName,
                                       nsIToolkitProfile** aResult) {
  RefPtr<nsToolkitProfile> profile;
  nsresult rv = CreateProfile(aRootDir, aName, getter_AddRefs(profile));
  profile.forget(aResult);
  return rv;
}

nsresult nsToolkitProfileService::CreateProfile(nsIFile* aRootDir,
                                                const nsACString& aName,
                                                nsToolkitProfile** aResult) {
  RefPtr<nsToolkitProfile> profile = GetProfileByName(aName);
  if (profile) {
    profile.forget(aResult);
    return NS_OK;
  }

  nsresult rv;
  nsCOMPtr<nsIFile> rootDir(aRootDir);

  nsAutoCString dirName;
  if (!rootDir) {
    rv = gDirServiceProvider->GetUserProfilesRootDir(getter_AddRefs(rootDir));
    NS_ENSURE_SUCCESS(rv, rv);

    dirName = aName;
    SaltProfileName(dirName);

    if (NS_IsNativeUTF8()) {
      rootDir->AppendNative(dirName);
    } else {
      rootDir->Append(NS_ConvertUTF8toUTF16(dirName));
    }
  }

  nsCOMPtr<nsIFile> localDir;
  rv = nsToolkitProfileService::gService->GetLocalDirFromRootDir(
      rootDir, getter_AddRefs(localDir));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = EnsureDirExists(rootDir);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIFile> profileDirParent;
  nsAutoString profileDirName;
  rv = rootDir->GetParent(getter_AddRefs(profileDirParent));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = rootDir->GetLeafName(profileDirName);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = EnsureDirExists(localDir);
  NS_ENSURE_SUCCESS(rv, rv);

  // We created a new profile dir. Let's store a creation timestamp.
  // Note that this code path does not apply if the profile dir was
  // created prior to launching.
  rv = CreateTimesInternal(rootDir);
  NS_ENSURE_SUCCESS(rv, rv);

  profile = new nsToolkitProfile(aName, rootDir, localDir, false);

  if (aName.Equals(DEV_EDITION_NAME)) {
    mDevEditionDefault = profile;
  }

  profile.forget(aResult);
  return NS_OK;
}

/**
 * Snaps (https://snapcraft.io/) use a different installation directory for
 * every version of an application. Since dedicated profiles uses the
 * installation directory to determine which profile to use this would lead
 * snap users getting a new profile on every application update.
 *
 * However the only way to have multiple installation of a snap is to install
 * a new snap instance. Different snap instances have different user data
 * directories and so already will not share profiles, in fact one instance
 * will not even be able to see the other instance's profiles since
 * profiles.ini will be stored in different places.
 *
 * So we can just disable dedicated profile support in this case and revert
 * back to the old method of just having a single default profile and still
 * get essentially the same benefits as dedicated profiles provides.
 */
bool nsToolkitProfileService::IsSnapEnvironment() {
#ifdef MOZ_WIDGET_GTK
  return widget::IsRunningUnderSnap();
#else
  return false;
#endif
}

/**
 * In some situations dedicated profile support does not work well. This
 * includes a handful of linux distributions which always install different
 * application versions to different locations, some application sandboxing
 * systems as well as enterprise deployments. This environment variable provides
 * a way to opt out of dedicated profiles for these cases.
 *
 * For Windows, we provide a policy to accomplish the same thing.
 */
bool nsToolkitProfileService::UseLegacyProfiles() {
  bool legacyProfiles = !!PR_GetEnv("MOZ_LEGACY_PROFILES");
#ifdef XP_WIN
  legacyProfiles |= PolicyCheckBoolean(L"LegacyProfiles");
#endif
  return legacyProfiles;
}

nsTArray<nsCString> nsToolkitProfileService::GetKnownInstalls() {
  nsTArray<nsCString> installs;

  mProfileDB.GetSections([&installs](const char* aSection) {
    // Check if the section starts with "Install"
    if (strncmp(aSection, INSTALL_PREFIX, INSTALL_PREFIX_LENGTH) != 0) {
      return true;
    }

    installs.AppendElement(aSection);

    return true;
  });

  return installs;
}

nsresult nsToolkitProfileService::CreateTimesInternal(nsIFile* aProfileDir) {
  nsresult rv = NS_ERROR_FAILURE;
  nsCOMPtr<nsIFile> creationLog;
  rv = aProfileDir->Clone(getter_AddRefs(creationLog));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = creationLog->AppendNative("times.json"_ns);
  NS_ENSURE_SUCCESS(rv, rv);

  bool exists = false;
  creationLog->Exists(&exists);
  if (exists) {
    return NS_OK;
  }

  rv = creationLog->Create(nsIFile::NORMAL_FILE_TYPE, 0700);
  NS_ENSURE_SUCCESS(rv, rv);

  // We don't care about microsecond resolution.
  int64_t msec = PR_Now() / PR_USEC_PER_MSEC;

  // Write it out.
  PRFileDesc* writeFile;
  rv = creationLog->OpenNSPRFileDesc(PR_WRONLY, 0700, &writeFile);
  NS_ENSURE_SUCCESS(rv, rv);

  PR_fprintf(writeFile, "{\n\"created\": %lld,\n\"firstUse\": null\n}\n", msec);
  PR_Close(writeFile);
  return NS_OK;
}

NS_IMETHODIMP
nsToolkitProfileService::GetProfileCount(uint32_t* aResult) {
  *aResult = 0;
  for (nsToolkitProfile* profile : mProfiles) {
    Unused << profile;
    (*aResult)++;
  }

  return NS_OK;
}

// Attempts to merge the given profile data into the on-disk versions which may
// have changed since rthey were loaded.
nsresult WriteProfileInfo(nsIFile* profilesDBFile, nsIFile* installDBFile,
                          const nsCString& installSection,
                          const GroupProfileData* profileInfo) {
  nsINIParser profilesIni;
  nsresult rv = profilesIni.Init(profilesDBFile);
  NS_ENSURE_SUCCESS(rv, rv);

  // The INI data may have changed on disk so we cannot guarantee the section
  // mapping remains the same. So we attempt to find the current profile's info
  // by path or store ID.
  nsCString iniSection;
  profilesIni.GetSections(
      [&profileInfo, &profilesIni, &iniSection](const char* section) {
        nsCString value;
        nsresult rv = profilesIni.GetString(section, "StoreID", value);

        if (NS_SUCCEEDED(rv)) {
          if (profileInfo->mStoreID.Equals(value)) {
            iniSection = section;
            // This is definitely the right one so no need to continue.
            return false;
          }
        }

        if (iniSection.IsEmpty()) {
          rv = profilesIni.GetString(section, "Path", value);
          if (NS_SUCCEEDED(rv) && profileInfo->mPath.Equals(value)) {
            // This might be right but we would prefer to find by store ID.
            iniSection = section;
          }
        }

        return true;
      });

  if (iniSection.IsEmpty()) {
    // No section found. Should we write a new one?
    return NS_ERROR_UNEXPECTED;
  }

  bool changed = false;
  nsCString oldValue;
  rv = profilesIni.GetString(iniSection.get(), "StoreID", oldValue);
  if (NS_FAILED(rv) || !oldValue.Equals(profileInfo->mStoreID)) {
    rv = profilesIni.SetString(iniSection.get(), "StoreID",
                               profileInfo->mStoreID.get());
    NS_ENSURE_SUCCESS(rv, rv);
    changed = true;
  }

  rv = profilesIni.GetString(iniSection.get(), "ShowSelector", oldValue);
  if (NS_FAILED(rv) ||
      !oldValue.Equals(profileInfo->mShowSelector ? "1" : "0")) {
    rv = profilesIni.SetString(iniSection.get(), "ShowSelector",
                               profileInfo->mShowSelector ? "1" : "0");
    NS_ENSURE_SUCCESS(rv, rv);
    changed = true;
  }

  profilesIni.GetString(iniSection.get(), "Path", oldValue);
  if (NS_FAILED(rv) || !oldValue.Equals(profileInfo->mPath)) {
    rv = profilesIni.SetString(iniSection.get(), "Path",
                               profileInfo->mPath.get());
    NS_ENSURE_SUCCESS(rv, rv);
    changed = true;

    // We must update the install default profile if it matches the old profile.

    nsCString oldDefault;
    rv = profilesIni.GetString(installSection.get(), "Default", oldDefault);
    if (NS_SUCCEEDED(rv) && oldDefault.Equals(oldValue)) {
      rv = profilesIni.SetString(installSection.get(), "Default",
                                 profileInfo->mPath.get());
      NS_ENSURE_SUCCESS(rv, rv);

      // We don't care so much if we fail to update the backup DB.
      const nsDependentCSubstring& installHash =
          Substring(installSection, INSTALL_PREFIX_LENGTH);

      nsINIParser installsIni;
      rv = installsIni.Init(installDBFile);
      if (NS_SUCCEEDED(rv)) {
        rv = installsIni.SetString(PromiseFlatCString(installHash).get(),
                                   "Default", profileInfo->mPath.get());
        if (NS_SUCCEEDED(rv)) {
          installsIni.WriteToFile(installDBFile);
        }
      }
    }
  }

  if (changed) {
    rv = profilesIni.WriteToFile(profilesDBFile);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

nsISerialEventTarget* nsToolkitProfileService::AsyncQueue() {
  if (!mAsyncQueue) {
    MOZ_ALWAYS_SUCCEEDS(NS_CreateBackgroundTaskQueue(
        "nsToolkitProfileService", getter_AddRefs(mAsyncQueue)));
  }

  return mAsyncQueue;
}

NS_IMETHODIMP
nsToolkitProfileService::AsyncFlushGroupProfile(JSContext* aCx,
                                                dom::Promise** aPromise) {
#ifndef MOZ_HAS_REMOTE
  return NS_ERROR_FAILURE;
#else
  if (!mGroupProfile) {
    return NS_ERROR_ILLEGAL_VALUE;
  }

  nsIGlobalObject* global = xpc::CurrentNativeGlobal(aCx);

  if (!global) {
    return NS_ERROR_DOM_INVALID_STATE_ERR;
  }

  ErrorResult result;
  RefPtr<dom::Promise> promise = dom::Promise::Create(global, result);

  if (MOZ_UNLIKELY(result.Failed())) {
    return result.StealNSResult();
  }

  UniquePtr<GroupProfileData> profileData = MakeUnique<GroupProfileData>();
  profileData->mStoreID = mGroupProfile->mStoreID;
  profileData->mShowSelector = mGroupProfile->mShowProfileSelector;

  bool isRelative;
  GetProfileDescriptor(mGroupProfile, profileData->mPath, &isRelative);

  nsCOMPtr<nsIRemoteService> rs = GetRemoteService();
  RefPtr<nsRemoteService> remoteService =
      static_cast<nsRemoteService*>(rs.get());

  RefPtr<AsyncFlushPromise> p = remoteService->AsyncLockStartup(5000)->Then(
      AsyncQueue(), __func__,
      [self = RefPtr{this}, this, profileData = std::move(profileData)](
          const nsRemoteService::StartupLockPromise::ResolveOrRejectValue&
              aValue) {
        if (aValue.IsReject()) {
          // Locking failed.
          return AsyncFlushPromise::CreateAndReject(aValue.RejectValue(),
                                                    __func__);
        }

        nsresult rv = WriteProfileInfo(mProfileDBFile, mInstallDBFile,
                                       mInstallSection, profileData.get());

        if (NS_FAILED(rv)) {
          return AsyncFlushPromise::CreateAndReject(rv, __func__);
        }

        return AsyncFlushPromise::CreateAndResolve(true, __func__);
      });

  // This is responsible for cancelling the MozPromise if the global goes
  // away.
  auto requestHolder =
      MakeRefPtr<dom::DOMMozPromiseRequestHolder<AsyncFlushPromise>>(global);

  // This keeps the promise alive after this method returns.
  nsMainThreadPtrHandle<dom::Promise> promiseHolder(
      new nsMainThreadPtrHolder<dom::Promise>(
          "nsToolkitProfileService::AsyncFlushGroupProfile", promise));

  p->Then(GetCurrentSerialEventTarget(), __func__,
          [requestHolder, promiseHolder](
              const AsyncFlushPromise::ResolveOrRejectValue& result) {
            requestHolder->Complete();

            if (result.IsReject()) {
              promiseHolder->MaybeReject(result.RejectValue());
            } else {
              promiseHolder->MaybeResolveWithUndefined();
            }
          })
      ->Track(*requestHolder);

  promise.forget(aPromise);

  return NS_OK;
#endif
}

NS_IMETHODIMP
nsToolkitProfileService::AsyncFlush(JSContext* aCx, dom::Promise** aPromise) {
#ifndef MOZ_HAS_REMOTE
  return NS_ERROR_FAILURE;
#else
  nsIGlobalObject* global = xpc::CurrentNativeGlobal(aCx);

  if (!global) {
    return NS_ERROR_DOM_INVALID_STATE_ERR;
  }

  ErrorResult result;
  RefPtr<dom::Promise> promise = dom::Promise::Create(global, result);

  if (MOZ_UNLIKELY(result.Failed())) {
    return result.StealNSResult();
  }

  UniquePtr<IniData> iniData = MakeUnique<IniData>();
  BuildIniData(iniData->mProfiles, iniData->mInstalls);

  nsCOMPtr<nsIRemoteService> rs = GetRemoteService();
  RefPtr<nsRemoteService> remoteService =
      static_cast<nsRemoteService*>(rs.get());

  RefPtr<AsyncFlushPromise> p = remoteService->AsyncLockStartup(5000)->Then(
      AsyncQueue(), __func__,
      [self = RefPtr{this}, this, iniData = std::move(iniData)](
          const nsRemoteService::StartupLockPromise::ResolveOrRejectValue&
              aValue) {
        if (aValue.IsReject()) {
          // Locking failed.
          return AsyncFlushPromise::CreateAndReject(aValue.RejectValue(),
                                                    __func__);
        }

        nsresult rv = FlushData(iniData->mProfiles, iniData->mInstalls);

        if (NS_FAILED(rv)) {
          return AsyncFlushPromise::CreateAndReject(rv, __func__);
        }

        return AsyncFlushPromise::CreateAndResolve(true, __func__);
      });

  // This is responsible for cancelling the MozPromise if the global goes
  // away.
  auto requestHolder =
      MakeRefPtr<dom::DOMMozPromiseRequestHolder<AsyncFlushPromise>>(global);

  // This keeps the promise alive after this method returns.
  nsMainThreadPtrHandle<dom::Promise> promiseHolder(
      new nsMainThreadPtrHolder<dom::Promise>(
          "nsToolkitProfileService::AsyncFlushGroupProfile", promise));

  p->Then(GetCurrentSerialEventTarget(), __func__,
          [requestHolder, promiseHolder](
              const AsyncFlushPromise::ResolveOrRejectValue& result) {
            requestHolder->Complete();

            if (result.IsReject()) {
              promiseHolder->MaybeReject(result.RejectValue());
            } else {
              promiseHolder->MaybeResolveWithUndefined();
            }
          })
      ->Track(*requestHolder);

  promise.forget(aPromise);

  return NS_OK;
#endif
}

nsresult nsToolkitProfileService::FlushData(const nsCString& aProfilesIniData,
                                            const nsCString& aInstallsIniData) {
  if (GetIsListOutdated()) {
    return NS_ERROR_DATABASE_CHANGED;
  }

  nsresult rv;

  // If we aren't using dedicated profiles then nothing about the list of
  // installs can have changed, so no need to update the backup.
  if (mUseDedicatedProfile) {
    if (!aInstallsIniData.IsEmpty()) {
      FILE* writeFile;
      rv = mInstallDBFile->OpenANSIFileDesc("w", &writeFile);
      NS_ENSURE_SUCCESS(rv, rv);

      uint32_t length = aInstallsIniData.Length();
      if (fwrite(aInstallsIniData.get(), sizeof(char), length, writeFile) !=
          length) {
        fclose(writeFile);
        return NS_ERROR_UNEXPECTED;
      }

      fclose(writeFile);
    } else {
      rv = mInstallDBFile->Remove(false);
      if (NS_FAILED(rv) && rv != NS_ERROR_FILE_NOT_FOUND) {
        return rv;
      }
    }
  }

  FILE* writeFile;
  rv = mProfileDBFile->OpenANSIFileDesc("w", &writeFile);
  NS_ENSURE_SUCCESS(rv, rv);

  uint32_t length = aProfilesIniData.Length();
  if (fwrite(aProfilesIniData.get(), sizeof(char), length, writeFile) !=
      length) {
    fclose(writeFile);
    return NS_ERROR_UNEXPECTED;
  }

  fclose(writeFile);

  rv = UpdateFileStats(mProfileDBFile, &mProfileDBExists,
                       &mProfileDBModifiedTime, &mProfileDBFileSize);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

void nsToolkitProfileService::BuildIniData(nsCString& aProfilesIniData,
                                           nsCString& aInstallsIniData) {
  // If we aren't using dedicated profiles then nothing about the list of
  // installs can have changed, so no need to update the backup.
  if (mUseDedicatedProfile) {
    // Export the installs to the backup.
    nsTArray<nsCString> installs = GetKnownInstalls();

    if (!installs.IsEmpty()) {
      nsCString buffer;

      for (uint32_t i = 0; i < installs.Length(); i++) {
        nsTArray<UniquePtr<KeyValue>> strings =
            GetSectionStrings(&mProfileDB, installs[i].get());
        if (strings.IsEmpty()) {
          continue;
        }

        // Strip "Install" from the start.
        const nsDependentCSubstring& install =
            Substring(installs[i], INSTALL_PREFIX_LENGTH);
        aInstallsIniData.AppendPrintf("[%s]\n",
                                      PromiseFlatCString(install).get());

        for (uint32_t j = 0; j < strings.Length(); j++) {
          aInstallsIniData.AppendPrintf("%s=%s\n", strings[j]->key.get(),
                                        strings[j]->value.get());
        }

        aInstallsIniData.Append("\n");
      }
    }
  }

  mProfileDB.WriteToString(aProfilesIniData);
}

NS_IMETHODIMP
nsToolkitProfileService::RemoveProfileFilesByPath(nsIFile* aRootDir,
                                                  nsIFile* aLocalDir,
                                                  uint32_t aTimeout,
                                                  JSContext* aCx,
                                                  dom::Promise** aPromise) {
  nsIGlobalObject* global = xpc::CurrentNativeGlobal(aCx);

  if (!global) {
    return NS_ERROR_DOM_INVALID_STATE_ERR;
  }

  ErrorResult result;
  RefPtr<dom::Promise> promise = dom::Promise::Create(global, result);

  if (MOZ_UNLIKELY(result.Failed())) {
    return result.StealNSResult();
  }

  nsCOMPtr<nsIFile> localDir = aLocalDir;
  if (!localDir) {
    GetLocalDirFromRootDir(aRootDir, getter_AddRefs(localDir));
  }

  using RemoveProfilesPromise = MozPromise<bool, nsresult, false>;
  // This is responsible for cancelling the MozPromise if the global goes
  // away.
  auto requestHolder =
      MakeRefPtr<dom::DOMMozPromiseRequestHolder<RemoveProfilesPromise>>(
          global);

  // This keeps the promise alive after this method returns.
  nsMainThreadPtrHandle<dom::Promise> promiseHolder(
      new nsMainThreadPtrHolder<dom::Promise>(
          "nsToolkitProfileService::AsyncFlushCurrentProfile", promise));

  InvokeAsync(AsyncQueue(), __func__,
              [rootDir = nsCOMPtr{aRootDir}, localDir = nsCOMPtr{localDir},
               aTimeout]() {
                nsresult rv = RemoveProfileFiles(rootDir, localDir, aTimeout);
                if (NS_SUCCEEDED(rv)) {
                  return RemoveProfilesPromise::CreateAndResolve(true,
                                                                 __func__);
                }

                return RemoveProfilesPromise::CreateAndReject(rv, __func__);
              })
      ->Then(GetCurrentSerialEventTarget(), __func__,
             [requestHolder, promiseHolder](
                 const RemoveProfilesPromise::ResolveOrRejectValue& result) {
               requestHolder->Complete();

               if (result.IsReject()) {
                 promiseHolder->MaybeReject(result.RejectValue());
               } else {
                 promiseHolder->MaybeResolveWithUndefined();
               }
             })
      ->Track(*requestHolder);

  promise.forget(aPromise);

  return NS_OK;
}

NS_IMETHODIMP
nsToolkitProfileService::Flush() {
  nsCString profilesIniData;
  nsCString installsIniData;

  BuildIniData(profilesIniData, installsIniData);
  return FlushData(profilesIniData, installsIniData);
}

nsresult nsToolkitProfileService::GetLocalDirFromRootDir(nsIFile* aRootDir,
                                                         nsIFile** aResult) {
  NS_ASSERTION(nsToolkitProfileService::gService, "Where did my service go?");
  nsCString path;
  bool isRelative;
  nsresult rv = nsToolkitProfileService::gService->GetProfileDescriptor(
      aRootDir, path, &isRelative);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIFile> localDir;
  if (isRelative) {
    rv = NS_NewLocalFileWithRelativeDescriptor(
        nsToolkitProfileService::gService->mTempData, path,
        getter_AddRefs(localDir));
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    localDir = aRootDir;
  }

  localDir.forget(aResult);

  return NS_OK;
}

already_AddRefed<nsToolkitProfileService> NS_GetToolkitProfileService() {
  if (!nsToolkitProfileService::gService) {
    nsToolkitProfileService::gService = new nsToolkitProfileService();
    nsresult rv = nsToolkitProfileService::gService->Init();
    if (NS_FAILED(rv)) {
      NS_ERROR("nsToolkitProfileService::Init failed!");
      delete nsToolkitProfileService::gService;
      return nullptr;
    }
  }

  return do_AddRef(nsToolkitProfileService::gService);
}

nsresult XRE_GetFileFromPath(const char* aPath, nsIFile** aResult) {
#if defined(XP_MACOSX)
  int32_t pathLen = strlen(aPath);
  if (pathLen > MAXPATHLEN) return NS_ERROR_INVALID_ARG;

  CFURLRef fullPath = CFURLCreateFromFileSystemRepresentation(
      nullptr, (const UInt8*)aPath, pathLen, true);
  if (!fullPath) return NS_ERROR_FAILURE;

  nsCOMPtr<nsILocalFileMac> lfMac;
  nsresult rv = NS_NewLocalFileWithCFURL(fullPath, getter_AddRefs(lfMac));
  lfMac.forget(aResult);
  CFRelease(fullPath);
  return rv;
#elif defined(XP_UNIX)
  char fullPath[MAXPATHLEN];

  if (!realpath(aPath, fullPath)) return NS_ERROR_FAILURE;

  return NS_NewNativeLocalFile(nsDependentCString(fullPath), aResult);
#elif defined(XP_WIN)
  WCHAR fullPath[MAXPATHLEN];

  if (!_wfullpath(fullPath, NS_ConvertUTF8toUTF16(aPath).get(), MAXPATHLEN))
    return NS_ERROR_FAILURE;

  return NS_NewLocalFile(nsDependentString(fullPath), aResult);
#else
#  error Platform-specific logic needed here.
#endif
}
