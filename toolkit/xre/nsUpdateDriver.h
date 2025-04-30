/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsUpdateDriver_h__
#define nsUpdateDriver_h__

#include "nscore.h"
#include "nsIUpdateService.h"
#include "nsIThread.h"
#include "nsCOMPtr.h"
#include "nsString.h"
#include "mozilla/Attributes.h"

class nsIFile;

#if defined(XP_WIN)
#  include <windows.h>
#  include "mozilla/WinHandleWatcher.h"
typedef HANDLE ProcessType;
#elif defined(XP_UNIX)
typedef pid_t ProcessType;
#else
#  include "prproces.h"
typedef PRProcess* ProcessType;
#endif

#ifdef XP_WIN
#  define UPDATER_BIN "updater.exe"
#  define MAINTENANCE_SVC_NAME L"MozillaMaintenance"
#  define MAYBE_WAIT_TIMEOUT_MS (60U * 1000U)
#elif XP_MACOSX
#  define UPDATER_APP "updater.app"
#  define UPDATER_BIN "org.mozilla.updater"
#else
#  define UPDATER_BIN "updater"
#endif

/**
 * This function processes any available updates.  As part of that process, it
 * may exit the current process and relaunch it at a later time.
 *
 * Two directories are passed to this function: greDir (where the actual
 * binary resides) and appDir (which contains application.ini for XULRunner
 * apps). If this is not a XULRunner app then appDir is identical to greDir.
 *
 * The argc and argv passed to this function should be what is needed to
 * relaunch the current process.
 *
 * The appVersion param passed to this function is the current application's
 * version and is used to determine if an update's version is older than the
 * current application version.
 *
 * If you want the update to be processed without restarting, set the restart
 * parameter to false.
 *
 * This function does not modify appDir.
 */
nsresult ProcessUpdates(nsIFile* greDir, nsIFile* appDir, nsIFile* updRootDir,
                        int argc, char** argv, const char* appVersion,
                        bool restart = true, ProcessType* pid = nullptr);

/**
 * This function is only needed for testing. When Firefox is started up with
 * `--test-process-updates`, we go through all the "update at startup" logic,
 * apply an update and restart, if applicable, and then, instead of starting
 * the browser, we call this function to write out a file signalling to the test
 * that the process has completed since otherwise it's not totally clear to the
 * the test when and if the browser relaunches and exits.
 */
nsresult WriteUpdateCompleteTestFile(nsIFile* updRootDir);

// The implementation of the update processor handles the task of loading the
// updater application for staging an update.
// XXX ehsan this is living in this file in order to make use of the existing
// stuff here, we might want to move it elsewhere in the future.
class nsUpdateProcessor final : public nsIUpdateProcessor {
 public:
  nsUpdateProcessor();

  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIUPDATEPROCESSOR

 private:
  ~nsUpdateProcessor();

  struct StagedUpdateInfo {
    StagedUpdateInfo() : mArgc(0), mArgv(nullptr) {}
    ~StagedUpdateInfo() {
      for (int i = 0; i < mArgc; ++i) {
        delete[] mArgv[i];
      }
      delete[] mArgv;
    }

    nsCOMPtr<nsIFile> mGREDir;
    nsCOMPtr<nsIFile> mAppDir;
    nsCOMPtr<nsIFile> mUpdateRoot;
    int mArgc;
    char** mArgv;
    nsCString mAppVersion;
  };

 private:
  void StartStagedUpdate();
  void UpdateDone();
  void ShutdownWorkerThread();

#ifndef XP_WIN
  void WaitForProcess();
#endif

 private:
  ProcessType mUpdaterPID;
  nsCOMPtr<nsIThread> mWorkerThread;
#ifdef XP_WIN
  mozilla::HandleWatcher mProcessWatcher;
#endif
  StagedUpdateInfo mInfo;
};
#endif  // nsUpdateDriver_h__
