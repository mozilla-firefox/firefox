/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsIAppStartup.h"
#include "nsIFile.h"
#include "nsIStringBundle.h"
#include "nsIToolkitProfile.h"
#include "nsIWindowWatcher.h"

#include "ProfileReset.h"

#include "nsDirectoryServiceDefs.h"
#include "nsDirectoryServiceUtils.h"
#include "nsPIDOMWindow.h"
#include "nsString.h"
#include "mozilla/Components.h"
#include "mozilla/XREAppData.h"

#include "mozilla/SpinEventLoopUntil.h"

using namespace mozilla;

extern const XREAppData* gAppData;

static const char kProfileProperties[] =
    "chrome://mozapps/locale/profile/profileSelection.properties";

/**
 * Spin up a thread to backup the old profile's main directory and delete the
 * profile's local directory. Once complete have the profile service remove the
 * old profile and if necessary make the new profile the default.
 */
nsresult ProfileResetCleanup(nsToolkitProfileService* aService,
                             nsIToolkitProfile* aOldProfile) {
  nsresult rv;
  nsCOMPtr<nsIFile> profileDir;
  rv = aOldProfile->GetRootDir(getter_AddRefs(profileDir));
  if (NS_FAILED(rv)) return rv;

  nsCOMPtr<nsIFile> profileLocalDir;
  rv = aOldProfile->GetLocalDir(getter_AddRefs(profileLocalDir));
  if (NS_FAILED(rv)) return rv;

  // Get the friendly name for the backup directory.
  nsCOMPtr<nsIStringBundleService> sbs =
      mozilla::components::StringBundle::Service();
  if (!sbs) return NS_ERROR_FAILURE;

  nsCOMPtr<nsIStringBundle> sb;
  (void)sbs->CreateBundle(kProfileProperties, getter_AddRefs(sb));
  if (!sb) return NS_ERROR_FAILURE;

  NS_ConvertUTF8toUTF16 appName(gAppData->name);
  AutoTArray<nsString, 2> params = {appName, appName};

  nsAutoString resetBackupDirectoryName;

  static const char* kResetBackupDirectory = "resetBackupDirectory";
  rv = sb->FormatStringFromName(kResetBackupDirectory, params,
                                resetBackupDirectoryName);
  if (NS_FAILED(rv)) return rv;

  // Get info to copy the old root profile dir to the desktop as a backup.
  nsCOMPtr<nsIFile> backupDest, containerDest, profileDest;
  rv = NS_GetSpecialDirectory(NS_OS_DESKTOP_DIR, getter_AddRefs(backupDest));
  if (NS_FAILED(rv)) {
    // Fall back to the home directory if the desktop is not available.
    rv = NS_GetSpecialDirectory(NS_OS_HOME_DIR, getter_AddRefs(backupDest));
    if (NS_FAILED(rv)) return rv;
  }

  // Try to create a directory for all the backups
  backupDest->Clone(getter_AddRefs(containerDest));
  containerDest->Append(resetBackupDirectoryName);
  rv = containerDest->Create(nsIFile::DIRECTORY_TYPE, 0700);
  // It's OK if it already exists, if and only if it is a directory
  if (rv == NS_ERROR_FILE_ALREADY_EXISTS) {
    bool containerIsDir;
    rv = containerDest->IsDirectory(&containerIsDir);
    if (NS_FAILED(rv) || !containerIsDir) {
      return rv;
    }
  } else if (NS_FAILED(rv)) {
    return rv;
  }

  // Get the name of the profile
  nsAutoString leafName;
  rv = profileDir->GetLeafName(leafName);
  if (NS_FAILED(rv)) return rv;

  // Try to create a unique directory for the profile:
  containerDest->Clone(getter_AddRefs(profileDest));
  profileDest->Append(leafName);
  rv = profileDest->CreateUnique(nsIFile::DIRECTORY_TYPE, 0700);
  if (NS_FAILED(rv)) return rv;

  // Get the unique profile name
  rv = profileDest->GetLeafName(leafName);
  if (NS_FAILED(rv)) return rv;

  // Delete the empty directory that CreateUnique just created.
  rv = profileDest->Remove(false);
  if (NS_FAILED(rv)) return rv;

  // Show a progress window while the cleanup happens since the disk I/O can
  // take time.
  nsCOMPtr<nsIWindowWatcher> windowWatcher(
      do_GetService(NS_WINDOWWATCHER_CONTRACTID));
  if (!windowWatcher) return NS_ERROR_FAILURE;

  nsCOMPtr<nsIAppStartup> appStartup(components::AppStartup::Service());
  if (!appStartup) return NS_ERROR_FAILURE;

  nsCOMPtr<mozIDOMWindowProxy> progressWindow;
  rv =
      windowWatcher->OpenWindow(nullptr, nsDependentCString(kResetProgressURL),
                                u"_blank"_ns, "centerscreen,chrome,titlebar"_ns,
                                nullptr, getter_AddRefs(progressWindow));
  if (NS_FAILED(rv)) return rv;

  // Create a new thread to do the bulk of profile cleanup to stay responsive.
  nsCOMPtr<nsIThread> cleanupThread;
  rv = NS_NewNamedThread("ResetCleanup", getter_AddRefs(cleanupThread));
  if (NS_SUCCEEDED(rv)) {
    nsCOMPtr<nsIRunnable> runnable = new ProfileResetCleanupAsyncTask(
        profileDir, profileLocalDir, containerDest, leafName);
    cleanupThread->Dispatch(runnable, nsIThread::DISPATCH_NORMAL);
    // The result callback will shut down the worker thread.

    // Wait for the cleanup thread to complete.
    SpinEventLoopUntil("xre:ProfileResetCreateBackup"_ns,
                       [&]() { return gProfileResetCleanupCompleted; });
  } else {
    gProfileResetCleanupCompleted = true;
    NS_WARNING("Cleanup thread creation failed");
    return rv;
  }
  // Close the progress window now that the cleanup thread is done.
  // OpenWindow can succeed without returning a window, so this may be null.
  if (const nsCOMPtr<nsPIDOMWindowOuter> piWindow =
          nsPIDOMWindowOuter::From(progressWindow)) {
    piWindow->Close();
  }

  return aService->ApplyResetProfile(aOldProfile);
}
