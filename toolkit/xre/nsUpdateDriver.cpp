/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <stdlib.h>
#include <stdio.h>
#include "nsUpdateDriver.h"

#include "nsDebug.h"
#include "nsXULAppAPI.h"
#include "nsAppRunner.h"
#include "nsIFile.h"
#include "nsVariant.h"
#include "nsCOMPtr.h"
#include "nsString.h"
#include "prproces.h"
#include "mozilla/Logging.h"
#include "prenv.h"
#include "nsVersionComparator.h"
#include "nsDirectoryServiceDefs.h"
#include "nsThreadUtils.h"
#include "nsIXULAppInfo.h"
#include "mozilla/Preferences.h"
#include "nsPrintfCString.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/ErrorNames.h"
#include "mozilla/Printf.h"
#include "mozilla/UniquePtr.h"
#include "nsIObserverService.h"
#include "nsNetCID.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/Services.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/CmdLineAndEnvUtils.h"

#ifdef XP_MACOSX
#  include "nsILocalFileMac.h"
#  include "nsCommandLineServiceMac.h"
#  include "MacLaunchHelper.h"
#  include "updaterfileutils_osx.h"
#  include "mozilla/Monitor.h"
#  include "gfxPlatformMac.h"
#endif

#if defined(XP_WIN)
#  include <direct.h>
#  include <process.h>
#  include <windows.h>
#  include <shlwapi.h>
#  include <strsafe.h>
#  include <shellapi.h>
#  include "commonupdatedir.h"
#  include "nsWindowsHelpers.h"
#  include "pathhash.h"
#  include "WinUtils.h"
#  define getcwd(path, size) _getcwd(path, size)
#  define getpid() GetCurrentProcessId()
#elif defined(XP_UNIX)
#  include <unistd.h>
#  include <sys/wait.h>
#endif

using namespace mozilla;

static LazyLogModule sUpdateLog("updatedriver");
// Some other file in our unified batch might have defined LOG already.
#ifdef LOG
#  undef LOG
#endif
#define LOG(args) MOZ_LOG(sUpdateLog, mozilla::LogLevel::Debug, args)

static nsresult GetCurrentWorkingDir(nsACString& aOutPath) {
  // Cannot use NS_GetSpecialDirectory because XPCOM is not yet initialized.
  // This code is duplicated from xpcom/io/SpecialSystemDirectory.cpp:

  aOutPath.Truncate();

#if defined(XP_WIN)
  wchar_t wpath[MAX_PATH];
  if (!_wgetcwd(wpath, std::size(wpath))) {
    return NS_ERROR_FAILURE;
  }
  CopyUTF16toUTF8(nsDependentString(wpath), aOutPath);
#else
  char path[MAXPATHLEN];
  if (!getcwd(path, std::size(path))) {
    return NS_ERROR_FAILURE;
  }
  aOutPath = path;
#endif

  return NS_OK;
}

/**
 * Get the path to the installation directory. For Mac OS X this will be the
 * bundle directory.
 *
 * @param appDir         the application directory file object
 * @param installDirPath the path to the installation directory
 */
static nsresult GetInstallDirPath(nsIFile* appDir, nsACString& installDirPath) {
  nsresult rv;
#ifdef XP_MACOSX
  nsCOMPtr<nsIFile> parentDir1, parentDir2;
  rv = appDir->GetParent(getter_AddRefs(parentDir1));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = parentDir1->GetParent(getter_AddRefs(parentDir2));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = parentDir2->GetNativePath(installDirPath);
  NS_ENSURE_SUCCESS(rv, rv);
#elif XP_WIN
  nsAutoString installDirPathW;
  rv = appDir->GetPath(installDirPathW);
  NS_ENSURE_SUCCESS(rv, rv);
  CopyUTF16toUTF8(installDirPathW, installDirPath);
#else
  rv = appDir->GetNativePath(installDirPath);
  NS_ENSURE_SUCCESS(rv, rv);
#endif
  return NS_OK;
}

static bool GetFile(nsIFile* dir, const nsACString& name,
                    nsCOMPtr<nsIFile>& result) {
  nsresult rv;

  nsCOMPtr<nsIFile> file;
  rv = dir->Clone(getter_AddRefs(file));
  if (NS_FAILED(rv)) {
    return false;
  }

  rv = file->AppendNative(name);
  if (NS_FAILED(rv)) {
    return false;
  }

  result = file;
  return true;
}

static bool GetStatusFile(nsIFile* dir, nsCOMPtr<nsIFile>& result) {
  return GetFile(dir, "update.status"_ns, result);
}

static void GetPidString(nsACString& output) {
  output.Truncate(0);
  output.AppendInt((int32_t)getpid());
}

/**
 * Get the contents of the update.status file when the update.status file can
 * be opened with read and write access. The reason it is opened for both read
 * and write is to prevent trying to update when the user doesn't have write
 * access to the update directory.
 *
 * @param statusFile the status file object.
 * @param buf        the buffer holding the file contents
 *
 * @return true if successful, false otherwise.
 */
template <size_t Size>
static bool GetStatusFileContents(nsIFile* statusFile, char (&buf)[Size]) {
  static_assert(
      Size > 16,
      "Buffer needs to be large enough to hold the known status codes");

  PRFileDesc* fd = nullptr;
  nsresult rv = statusFile->OpenNSPRFileDesc(PR_RDWR, 0660, &fd);
  if (NS_FAILED(rv)) {
    return false;
  }

  const int32_t n = PR_Read(fd, buf, Size);
  PR_Close(fd);

  return (n >= 0);
}

static nsresult WriteFile(nsIFile* file, nsACString& toWrite) {
  PRFileDesc* fd = nullptr;
  nsresult rv = file->OpenNSPRFileDesc(PR_WRONLY | PR_CREATE_FILE | PR_TRUNCATE,
                                       0660, &fd);
  NS_ENSURE_SUCCESS(rv, rv);

  const int32_t n =
      PR_Write(fd, PromiseFlatCString(toWrite).get(), toWrite.Length());
  PR_Close(fd);

  return (unsigned long)n == toWrite.Length() ? NS_OK : NS_ERROR_FAILURE;
}

enum UpdateStatus {
  eNoUpdateAction,
  ePendingUpdate,
  ePendingService,
  ePendingElevate,
  eAppliedUpdate,
  eAppliedService,
};

/**
 * Returns a value indicating what needs to be done in order to handle an
 * update.
 *
 * @param dir the directory in which we should look for an update.status file.
 * @param statusFile the update.status file found in the directory.
 *
 * @return the update action to be performed.
 */
static UpdateStatus GetUpdateStatus(nsIFile* dir,
                                    nsCOMPtr<nsIFile>& statusFile) {
  if (GetStatusFile(dir, statusFile)) {
    char buf[32];
    if (GetStatusFileContents(statusFile, buf)) {
      const char kPending[] = "pending";
      const char kPendingService[] = "pending-service";
      const char kPendingElevate[] = "pending-elevate";
      const char kApplied[] = "applied";
      const char kAppliedService[] = "applied-service";
      if (!strncmp(buf, kPendingElevate, sizeof(kPendingElevate) - 1)) {
        return ePendingElevate;
      }
      if (!strncmp(buf, kPendingService, sizeof(kPendingService) - 1)) {
        return ePendingService;
      }
      if (!strncmp(buf, kPending, sizeof(kPending) - 1)) {
        return ePendingUpdate;
      }
      if (!strncmp(buf, kAppliedService, sizeof(kAppliedService) - 1)) {
        return eAppliedService;
      }
      if (!strncmp(buf, kApplied, sizeof(kApplied) - 1)) {
        return eAppliedUpdate;
      }
    }
  }
  return eNoUpdateAction;
}

static bool GetVersionFile(nsIFile* dir, nsCOMPtr<nsIFile>& result) {
  return GetFile(dir, "update.version"_ns, result);
}

// Compares the current application version with the update's application
// version.
static bool IsOlderVersion(nsIFile* versionFile, const char* appVersion) {
  PRFileDesc* fd = nullptr;
  nsresult rv = versionFile->OpenNSPRFileDesc(PR_RDONLY, 0660, &fd);
  if (NS_FAILED(rv)) {
    return true;
  }

  char buf[32];
  const int32_t n = PR_Read(fd, buf, sizeof(buf));
  PR_Close(fd);

  if (n < 0) {
    return false;
  }

  // Trim off the trailing newline
  if (buf[n - 1] == '\n') {
    buf[n - 1] = '\0';
  }

  // If the update xml doesn't provide the application version the file will
  // contain the string "null" and it is assumed that the update is not older.
  const char kNull[] = "null";
  if (strncmp(buf, kNull, sizeof(kNull) - 1) == 0) {
    return false;
  }

  return mozilla::Version(appVersion) > buf;
}

nsresult WriteUpdateCompleteTestFile(nsIFile* updRootDir) {
  nsCOMPtr<nsIFile> outputFile;
  nsresult rv = updRootDir->Clone(getter_AddRefs(outputFile));
  NS_ENSURE_SUCCESS(rv, rv);
  outputFile->AppendNative("test_process_updates.txt"_ns);

  nsAutoCString pid;
  GetPidString(pid);

  return WriteFile(outputFile, pid);
}

/**
 * Applies, switches, or stages an update.
 *
 * @param greDir       the GRE directory
 * @param updateDir    the update root directory
 * @param appDir       the application directory
 * @param appArgc      the number of args passed to the application
 * @param appArgv      the args passed to the application
 *                     (used for restarting the application when necessary)
 * @param restart      true when a restart is necessary.
 * @param isStaged     true when the update has already been staged
 * @param outpid (out) parameter holding the handle to the updater application
 *                     when staging updates
 */
static void ApplyUpdate(nsIFile* greDir, nsIFile* updateDir, nsIFile* appDir,
                        int appArgc, char** appArgv, bool restart,
                        bool isStaged, ProcessType* outpid) {
  MOZ_DIAGNOSTIC_ASSERT(
      !restart || NS_IsMainThread(),
      "restart may only be set when called on the main thread");
  // The following determines the update operation to perform.
  // 1. When restart is false the update will be staged.
  // 2. When restart is true and isStaged is false the update will apply the mar
  //    file to the installation directory.
  // 3. When restart is true and isStaged is true the update will switch the
  //    staged update with the installation directory.

  nsresult rv;

  nsCOMPtr<nsIFile> updater;
  nsAutoCString updaterPath;
  nsAutoCString updateDirPath;
#if defined(XP_WIN)
  // Get an nsIFile reference for the updater in the installation dir.
  if (!GetFile(greDir, nsLiteralCString(UPDATER_BIN), updater)) {
    return;
  }

  // Get the path to the updater.
  nsAutoString updaterPathW;
  rv = updater->GetPath(updaterPathW);
  if (NS_FAILED(rv)) {
    return;
  }
  CopyUTF16toUTF8(updaterPathW, updaterPath);

  // Get the path to the update dir.
  nsAutoString updateDirPathW;
  rv = updateDir->GetPath(updateDirPathW);
  if (NS_FAILED(rv)) {
    return;
  }
  CopyUTF16toUTF8(updateDirPathW, updateDirPath);
#elif defined(XP_MACOSX)
  // Get an nsIFile reference for the updater in the installation dir.
  if (!GetFile(appDir, nsLiteralCString(UPDATER_APP), updater)) {
    return;
  }
  rv = updater->AppendNative("Contents"_ns);
  if (NS_FAILED(rv)) {
    return;
  }
  rv = updater->AppendNative("MacOS"_ns);
  if (NS_FAILED(rv)) {
    return;
  }
  rv = updater->AppendNative(nsLiteralCString(UPDATER_BIN));
  if (NS_FAILED(rv)) {
    return;
  }

  // Get the path to the updater.
  rv = updater->GetNativePath(updaterPath);
  if (NS_FAILED(rv)) {
    return;
  }

  // Get the path to the update dir.
  rv = updateDir->GetNativePath(updateDirPath);
  if (NS_FAILED(rv)) {
    return;
  }
#else
  // Get an nsIFile reference for the updater in the installation dir.
  if (!GetFile(greDir, nsLiteralCString(UPDATER_BIN), updater)) {
    return;
  }

  // Get the path to the updater.
  rv = updater->GetNativePath(updaterPath);
  if (NS_FAILED(rv)) {
    return;
  }

  // Get the path to the update dir.
  rv = updateDir->GetNativePath(updateDirPath);
  if (NS_FAILED(rv)) {
    return;
  }
#endif

  // appFilePath and workingDirPath are only used when the application will be
  // restarted.
#ifndef XP_MACOSX
  nsAutoCString appFilePath;
#endif
  nsAutoCString workingDirPath;
  if (restart) {
    // Get the path to the current working directory.
    rv = GetCurrentWorkingDir(workingDirPath);
    if (NS_FAILED(rv)) {
      return;
    }

    // Get the application file path used by the updater to restart the
    // application after the update has finished. Note that macOS uses the
    // path to the application bundle, i.e. installDirPath, to relaunch the
    // application.
    nsCOMPtr<nsIFile> appFile;
    XRE_GetBinaryPath(getter_AddRefs(appFile));
    if (!appFile) {
      return;
    }

#if defined(XP_WIN)
    nsAutoString appFilePathW;
    rv = appFile->GetPath(appFilePathW);
    if (NS_FAILED(rv)) {
      return;
    }
    CopyUTF16toUTF8(appFilePathW, appFilePath);
#elif !defined(XP_MACOSX)
    rv = appFile->GetNativePath(appFilePath);
    if (NS_FAILED(rv)) {
      return;
    }
#endif
  }

  // Get the installation directory path.
  nsAutoCString installDirPath;
  rv = GetInstallDirPath(appDir, installDirPath);
  if (NS_FAILED(rv)) {
    return;
  }

#if defined(XP_MACOSX)
  // If we're going to do a restart, we need to make sure the font registration
  // thread has finished before this process exits (bug 1777332).
  if (restart) {
    gfxPlatformMac::WaitForFontRegistration();
  }

  // We need to detect whether elevation is required for this update. This can
  // occur when an admin user installs the application, but another admin
  // user attempts to update (see bug 394984).
  // We only check if we need elevation if we are restarting. We don't attempt
  // to stage if elevation is required. Staging happens without the user knowing
  // about it, and we don't want to ask for elevation for seemingly no reason.
  bool needElevation = false;
  if (restart) {
    needElevation = !IsRecursivelyWritable(installDirPath.get());
    if (needElevation) {
      // Normally we would check this via nsIAppStartup::wasSilentlyStarted,
      // but nsIAppStartup isn't available yet.
      char* mozAppSilentStart = PR_GetEnv("MOZ_APP_SILENT_START");
      bool wasSilentlyStarted =
          mozAppSilentStart && (strcmp(mozAppSilentStart, "") != 0);
      if (wasSilentlyStarted) {
        // Elevation always requires prompting for credentials on macOS. If we
        // are trying to restart silently, we must not display UI such as this
        // prompt.
        // We make this check here rather than in the updater, because it is
        // actually Firefox that shows the elevation prompt (via
        // InstallPrivilegedHelper), not the updater.
        return;
      }
    }
  }
#endif

  nsAutoCString applyToDirPath;
  nsCOMPtr<nsIFile> updatedDir;
  if (restart && !isStaged) {
    // The install directory is the same as the apply to directory.
    applyToDirPath.Assign(installDirPath);
  } else {
    // Get the directory where the update is staged or will be staged. This is
    // `updateDir` for macOS and `appDir` for all other platforms. macOS cannot
    // stage updates inside the .app bundle (`appDir`) without breaking the code
    // signature on the bundle, so we use `updateDir` instead.
#if defined(XP_MACOSX)
    if (!GetFile(updateDir, "Updated.app"_ns, updatedDir)) {
#else
    if (!GetFile(appDir, "updated"_ns, updatedDir)) {
#endif
      return;
    }
#if defined(XP_WIN)
    nsAutoString applyToDirPathW;
    rv = updatedDir->GetPath(applyToDirPathW);
    if (NS_FAILED(rv)) {
      return;
    }
    CopyUTF16toUTF8(applyToDirPathW, applyToDirPath);
#else
    rv = updatedDir->GetNativePath(applyToDirPath);
#endif
  }
  if (NS_FAILED(rv)) {
    return;
  }

  if (restart && isStaged) {
    // When the update should already be staged make sure that the updated
    // directory exists.
    bool updatedDirExists = false;
    if (NS_FAILED(updatedDir->Exists(&updatedDirExists)) || !updatedDirExists) {
      return;
    }
  }

  // On platforms where we are not calling execv, we may need to make the
  // updater executable wait for the calling process to exit.  Otherwise, the
  // updater may have trouble modifying our executable image (because it might
  // still be in use).  This is accomplished by passing our PID to the updater
  // so that it can wait for us to exit.  This is not perfect as there is a race
  // condition that could bite us.  It's possible that the calling process could
  // exit before the updater waits on the specified PID, and in the meantime a
  // new process with the same PID could be created.  This situation is
  // unlikely, however, given the way most operating systems recycle PIDs. We'll
  // take our chances ;-) Construct the PID argument for this process to pass to
  // the updater.
  nsAutoCString pid;
  if (restart) {
#if defined(XP_UNIX) & !defined(XP_MACOSX)
    // When execv is used for an update that requires a restart 0 is passed
    // which is ignored by the updater.
    pid.AssignLiteral("0");
#else
    GetPidString(pid);
#endif
    if (isStaged) {
      // Append a special token to the PID in order to inform the updater that
      // it should replace install with the updated directory.
      pid.AppendLiteral("/replace");
    }
  } else {
    // Signal the updater application that it should stage the update.
    pid.AssignLiteral("-1");
  }

  int argc = 7;
  if (restart) {
    argc += 1;  // callback working directory
    argc += appArgc;
    if (gRestartedByOS) {
      argc += 1;
    }
  }
  char** argv = static_cast<char**>(malloc((argc + 1) * sizeof(char*)));
  if (!argv) {
    return;
  }
  argv[0] = (char*)updaterPath.get();
  argv[1] = const_cast<char*>("3");
  argv[2] = (char*)updateDirPath.get();
  argv[3] = (char*)installDirPath.get();
  argv[4] = (char*)applyToDirPath.get();
  argv[5] = const_cast<char*>("first");
  argv[6] = (char*)pid.get();
  if (restart && appArgc) {
    argv[7] = (char*)workingDirPath.get();
#if defined(XP_MACOSX)
    argv[8] = (char*)installDirPath.get();
#else
    argv[8] = (char*)appFilePath.get();
#endif
    for (int i = 1; i < appArgc; ++i) {
      argv[8 + i] = appArgv[i];
    }
    if (gRestartedByOS) {
      // We haven't truly started up, restore this argument so that we will have
      // it upon restart.
      argv[8 + appArgc] = const_cast<char*>("-os-restarted");
    }
  }
  argv[argc] = nullptr;

  if (restart && gSafeMode) {
    PR_SetEnv("MOZ_SAFE_MODE_RESTART=1");
  }

  LOG(("spawning updater process [%s]\n", updaterPath.get()));

#if defined(XP_UNIX) && !defined(XP_MACOSX)
  // We use execv to spawn the updater process on all UNIX systems except Mac
  // OSX since it is known to cause problems on the Mac.  Windows has execv, but
  // it is a faked implementation that doesn't really replace the current
  // process. Instead it spawns a new process, so we gain nothing from using
  // execv on Windows.
  if (restart) {
    int execResult = execv(updaterPath.get(), argv);
    free(argv);
    exit(execResult);
  }
  *outpid = fork();
  if (*outpid == -1) {
    free(argv);
    return;
  }
  if (*outpid == 0) {
    int execResult = execv(updaterPath.get(), argv);
    free(argv);
    exit(execResult);
  }
#elif defined(XP_WIN)
  if (isStaged) {
    // Launch the updater to replace the installation with the staged updated.
    if (!WinLaunchChild(updaterPathW.get(), argc, argv)) {
      free(argv);
      return;
    }
  } else {
    // Launch the updater to either stage or apply an update.
    if (!WinLaunchChild(updaterPathW.get(), argc, argv, nullptr, outpid)) {
      free(argv);
      return;
    }
  }
#elif defined(XP_MACOSX)
if (restart) {
  // Ensure we've added URLs to load into the app command line if we're
  // restarting.
  CommandLineServiceMac::SetupMacCommandLine(argc, argv, restart);

  if (needElevation) {
    bool hasLaunched = LaunchElevatedUpdate(argc, argv, outpid);
    free(argv);
    if (!hasLaunched) {
      LOG(("Failed to launch elevated update!"));
      exit(1);
    }
    exit(0);
  }
}

if (isStaged) {
  // Launch the updater to replace the installation with the staged updated.
  LaunchChildMac(argc, argv);
} else {
  // Launch the updater to either stage or apply an update.
  LaunchChildMac(argc, argv, outpid);
}
#else
if (isStaged) {
  // Launch the updater to replace the installation with the staged updated.
  PR_CreateProcessDetached(updaterPath.get(), argv, nullptr, nullptr);
} else {
  // Launch the updater to either stage or apply an update.
  *outpid = PR_CreateProcess(updaterPath.get(), argv, nullptr, nullptr);
}
#endif
  free(argv);
  if (restart) {
    exit(0);
  }
}

#if !defined(XP_WIN)
/**
 * Wait briefly to see if a process terminates, then return true if it has.
 *
 * (Not implemented on Windows, where HandleWatcher is used instead.)
 */
static bool ProcessHasTerminated(ProcessType pt) {
#  if defined(XP_MACOSX)
  // We're waiting for the process to terminate in LaunchChildMac.
  return true;
#  elif defined(XP_UNIX)
  int exitStatus;
  pid_t exited = waitpid(pt, &exitStatus, WNOHANG);
  if (exited == 0) {
    // Process is still running.
    sleep(1);
    return false;
  }
  if (exited == -1) {
    LOG(("Error while checking if the updater process is finished"));
    // This shouldn't happen, but if it does, the updater process is lost to us,
    // so the best we can do is pretend that it's exited.
    return true;
  }
  // If we get here, the process has exited; make sure it exited normally.
  if (WIFEXITED(exitStatus) && (WEXITSTATUS(exitStatus) != 0)) {
    LOG(("Error while running the updater process, check update.log"));
  }
  return true;
#  else
  // No way to have a non-blocking implementation on these platforms,
  // because we're using NSPR and it only provides a blocking wait.
  int32_t exitCode;
  PR_WaitProcess(pt, &exitCode);
  if (exitCode != 0) {
    LOG(("Error while running the updater process, check update.log"));
  }
  return true;
#  endif
}
#endif

nsresult ProcessUpdates(nsIFile* greDir, nsIFile* appDir, nsIFile* updRootDir,
                        int argc, char** argv, const char* appVersion,
                        bool restart, ProcessType* pid) {
  nsresult rv;

#ifdef XP_WIN
  // If we're in a package, we know any updates that we find are not for us.
  if (mozilla::widget::WinUtils::HasPackageIdentity()) {
    return NS_OK;
  }
#endif

  nsCOMPtr<nsIFile> updatesDir;
  rv = updRootDir->Clone(getter_AddRefs(updatesDir));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = updatesDir->AppendNative("updates"_ns);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = updatesDir->AppendNative("0"_ns);
  NS_ENSURE_SUCCESS(rv, rv);

  // Return early since there isn't a valid update when the update application
  // version file doesn't exist or if the update's application version is less
  // than the current application version. The cleanup of the update will happen
  // during post update processing in nsUpdateService.js.
  nsCOMPtr<nsIFile> versionFile;
  if (!GetVersionFile(updatesDir, versionFile) ||
      IsOlderVersion(versionFile, appVersion)) {
    return NS_OK;
  }

  nsCOMPtr<nsIFile> statusFile;
  UpdateStatus status = GetUpdateStatus(updatesDir, statusFile);
  switch (status) {
    case ePendingUpdate:
    case ePendingService: {
      ApplyUpdate(greDir, updatesDir, appDir, argc, argv, restart, false, pid);
      break;
    }
    case eAppliedUpdate:
    case eAppliedService:
      // An update was staged and needs to be switched so the updated
      // application is used.
      ApplyUpdate(greDir, updatesDir, appDir, argc, argv, restart, true, pid);
      break;
    case ePendingElevate:
      // No action should be performed since the user hasn't opted into
      // elevating for the update so continue application startup.
    case eNoUpdateAction:
      // We don't need to do any special processing here, we'll just continue to
      // startup the application.
      break;
  }

  return NS_OK;
}

NS_IMPL_ISUPPORTS(nsUpdateProcessor, nsIUpdateProcessor)

nsUpdateProcessor::nsUpdateProcessor() : mUpdaterPID(0) {}

#ifdef XP_WIN
nsUpdateProcessor::~nsUpdateProcessor() { mProcessWatcher.Stop(); }
#else
nsUpdateProcessor::~nsUpdateProcessor() = default;
#endif

NS_IMETHODIMP
nsUpdateProcessor::ProcessUpdate() {
  nsresult rv;

  nsCOMPtr<nsIProperties> ds =
      do_GetService(NS_DIRECTORY_SERVICE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIFile> exeFile;
  rv = ds->Get(XRE_EXECUTABLE_FILE, NS_GET_IID(nsIFile),
               getter_AddRefs(exeFile));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIFile> appDir;
  rv = exeFile->GetParent(getter_AddRefs(appDir));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIFile> greDir;
  rv = ds->Get(NS_GRE_DIR, NS_GET_IID(nsIFile), getter_AddRefs(greDir));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIFile> updRoot;
  rv = ds->Get(XRE_UPDATE_ROOT_DIR, NS_GET_IID(nsIFile),
               getter_AddRefs(updRoot));
  NS_ASSERTION(NS_SUCCEEDED(rv), "Can't get the UpdRootD dir");

  // XRE_UPDATE_ROOT_DIR should not fail but if it does fallback to the
  // application directory just to be safe.
  if (NS_FAILED(rv)) {
    rv = appDir->Clone(getter_AddRefs(updRoot));
    NS_ENSURE_SUCCESS(rv, rv);
  }

  nsCOMPtr<nsIXULAppInfo> appInfo =
      do_GetService("@mozilla.org/xre/app-info;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString appVersion;
  rv = appInfo->GetVersion(appVersion);
  NS_ENSURE_SUCCESS(rv, rv);

  // Copy the parameters to the StagedUpdateInfo structure shared with the
  // worker thread.
  mInfo.mGREDir = greDir;
  mInfo.mAppDir = appDir;
  mInfo.mUpdateRoot = updRoot;
  mInfo.mArgc = 0;
  mInfo.mArgv = nullptr;
  mInfo.mAppVersion = appVersion;

  MOZ_ASSERT(NS_IsMainThread(), "not main thread");
  nsCOMPtr<nsIRunnable> r =
      NewRunnableMethod("nsUpdateProcessor::StartStagedUpdate", this,
                        &nsUpdateProcessor::StartStagedUpdate);
  return NS_NewNamedThread("UpdateProcessor", getter_AddRefs(mWorkerThread), r);
}

void nsUpdateProcessor::StartStagedUpdate() {
  MOZ_ASSERT(!NS_IsMainThread(), "main thread");

  // If we fail to launch the updater process or its monitor for some reason, we
  // need to shut down the worker thread, as there isn't anything more for us to
  // do.
  auto onExitStopThread = mozilla::MakeScopeExit([&] {
    nsresult rv = NS_DispatchToMainThread(
        NewRunnableMethod("nsUpdateProcessor::ShutdownWorkerThread", this,
                          &nsUpdateProcessor::ShutdownWorkerThread));
    NS_ENSURE_SUCCESS_VOID(rv);
  });

  // Launch updater. (We do this on a worker thread to avoid blocking the main
  // thread with file I/O.)
  nsresult rv = ProcessUpdates(mInfo.mGREDir, mInfo.mAppDir, mInfo.mUpdateRoot,
                               mInfo.mArgc, mInfo.mArgv,
                               mInfo.mAppVersion.get(), false, &mUpdaterPID);
  if (NS_FAILED(rv)) {
    MOZ_LOG(sUpdateLog, mozilla::LogLevel::Error,
            ("could not start updater process: %s", GetStaticErrorName(rv)));
    return;
  }

  if (!mUpdaterPID) {
    // not an error
    MOZ_LOG(sUpdateLog, mozilla::LogLevel::Verbose,
            ("ProcessUpdates() indicated nothing to do"));
    return;
  }

#ifdef WIN32
  // Set up a HandleWatcher to report to the main thread when we're done.
  RefPtr<nsIThread> mainThread;
  NS_GetMainThread(getter_AddRefs(mainThread));
  mProcessWatcher.Watch(mUpdaterPID, mainThread,
                        NewRunnableMethod("nsUpdateProcessor::UpdateDone", this,
                                          &nsUpdateProcessor::UpdateDone));

// On Windows, that's all we need the worker thread for. Let
// `onExitStopThread` shut us down.
#else
  // Monitor the state of the updater process while it is staging an update.
  rv = NS_DispatchToCurrentThread(
      NewRunnableMethod("nsUpdateProcessor::WaitForProcess", this,
                        &nsUpdateProcessor::WaitForProcess));
  if (NS_FAILED(rv)) {
    MOZ_LOG(sUpdateLog, mozilla::LogLevel::Error,
            ("could not start updater process poll: error %s",
             GetStaticErrorName(rv)));
    return;
  }

  // Leave the worker thread alive to run WaitForProcess. Either it or its
  // successors will be responsible for shutting down the worker thread.
  onExitStopThread.release();
#endif
}

void nsUpdateProcessor::ShutdownWorkerThread() {
  MOZ_ASSERT(NS_IsMainThread(), "not main thread");
  mWorkerThread->Shutdown();
  mWorkerThread = nullptr;
}

#ifndef WIN32
void nsUpdateProcessor::WaitForProcess() {
  MOZ_ASSERT(!NS_IsMainThread(), "main thread");
  if (ProcessHasTerminated(mUpdaterPID)) {
    NS_DispatchToMainThread(NewRunnableMethod(
        "nsUpdateProcessor::UpdateDone", this, &nsUpdateProcessor::UpdateDone));
  } else {
    NS_DispatchToCurrentThread(
        NewRunnableMethod("nsUpdateProcessor::WaitForProcess", this,
                          &nsUpdateProcessor::WaitForProcess));
  }
}
#endif

void nsUpdateProcessor::UpdateDone() {
  MOZ_ASSERT(NS_IsMainThread(), "not main thread");

  nsCOMPtr<nsIUpdateManager> um =
      do_GetService("@mozilla.org/updates/update-manager;1");
  if (um) {
    // This completes asynchronously, but nothing else that we are doing in this
    // function requires waiting for this to complete.
    RefPtr<mozilla::dom::Promise> outPromise;
    um->RefreshUpdateStatus(getter_AddRefs(outPromise));
  }

// On Windows, shutting down the worker thread is taken care of by another task.
// (Which may not have run yet, so we can't assert.)
#ifndef XP_WIN
  ShutdownWorkerThread();
#endif
}

NS_IMETHODIMP
nsUpdateProcessor::GetServiceRegKeyExists(bool* aResult) {
#ifndef XP_WIN
  return NS_ERROR_NOT_IMPLEMENTED;
#else   // #ifdef XP_WIN
  nsCOMPtr<nsIProperties> dirSvc(
      do_GetService("@mozilla.org/file/directory_service;1"));
  NS_ENSURE_TRUE(dirSvc, NS_ERROR_SERVICE_NOT_AVAILABLE);

  nsCOMPtr<nsIFile> installBin;
  nsresult rv = dirSvc->Get(XRE_EXECUTABLE_FILE, NS_GET_IID(nsIFile),
                            getter_AddRefs(installBin));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIFile> installDir;
  rv = installBin->GetParent(getter_AddRefs(installDir));
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoString installPath;
  rv = installDir->GetPath(installPath);
  NS_ENSURE_SUCCESS(rv, rv);

  wchar_t maintenanceServiceKey[MAX_PATH + 1];
  BOOL success = CalculateRegistryPathFromFilePath(
      PromiseFlatString(installPath).get(), maintenanceServiceKey);
  NS_ENSURE_TRUE(success, NS_ERROR_FAILURE);

  HKEY regHandle;
  LSTATUS ls = RegOpenKeyExW(HKEY_LOCAL_MACHINE, maintenanceServiceKey, 0,
                             KEY_QUERY_VALUE | KEY_WOW64_64KEY, &regHandle);
  if (ls == ERROR_SUCCESS) {
    RegCloseKey(regHandle);
    *aResult = true;
    return NS_OK;
  }
  if (ls == ERROR_FILE_NOT_FOUND) {
    *aResult = false;
    return NS_OK;
  }
  // We got an error we weren't expecting reading the registry.
  return NS_ERROR_NOT_AVAILABLE;
#endif  // #ifdef XP_WIN
}

NS_IMETHODIMP
nsUpdateProcessor::AttemptAutomaticApplicationRestartWithLaunchArgs(
    const nsTArray<nsString>& argvExtra, int32_t* pidRet) {
#ifndef XP_WIN
  return NS_ERROR_NOT_IMPLEMENTED;
#else
  // Retrieve current command line arguments for restart
  // GetCommandLineW() returns a read only pointer to
  // the arguments the process was launched with.
  LPWSTR currentCommandLine = GetCommandLineW();

  // Spawn a new process for the application based on the current
  // command line with the -restart-pid <pid> flag. This flag
  // can be used with MaybeWaitForProcessExit() to have
  // the process wait until the parent process has exited.
  if (currentCommandLine) {
    // Append additional command line arguments to current command line for
    // restart.
    int currentArgc = 0;
    UniquePtr<LPWSTR, LocalFreeDeleter> currentArgv(
        CommandLineToArgvW(currentCommandLine, &currentArgc));
    nsTArray<wchar_t*> restartCommandLineArgv(currentArgc + argvExtra.Length() +
                                              2);
    for (int i = 0; i < currentArgc; i++) {
      restartCommandLineArgv.AppendElement(currentArgv.get()[i]);
    }
    for (const nsString& arg : argvExtra) {
      restartCommandLineArgv.AppendElement(static_cast<wchar_t*>(arg.get()));
    }
    // Append -restart-pid flag and pid to restart command line.
    DWORD pidCurrent = GetCurrentProcessId();
    nsString pid;
    pid.AppendInt(static_cast<uint32_t>(pidCurrent));
    nsString pidFlag = u"-restart-pid"_ns;
    restartCommandLineArgv.AppendElement(pidFlag.get());
    restartCommandLineArgv.AppendElement(pid.get());

    // Create new process that interacts with MaybeWaitForProcessExit()
    // and sleeps until the original process is killed.
    wchar_t exeName[MAX_PATH];
    GetModuleFileNameW(NULL, exeName, MAX_PATH);
    HANDLE childHandle;
    WinLaunchChild(exeName, restartCommandLineArgv.Length(),
                   restartCommandLineArgv.Elements(), nullptr, &childHandle);
    *pidRet = GetProcessId(childHandle);
    CloseHandle(childHandle);
    if (!*pidRet) {
      printf_stderr("*** ApplyUpdate: !pidRet ***\n");
      return NS_ERROR_ABORT;
    }
    printf_stderr("*** ApplyUpdate: launched pidRet = %d ***\n", *pidRet);

    MOZ_LOG(sUpdateLog, mozilla::LogLevel::Debug,
            ("register application restart succeeded"));
  } else {
    MOZ_LOG(sUpdateLog, mozilla::LogLevel::Error,
            ("could not register application restart"));
    return NS_ERROR_NOT_AVAILABLE;
  }
  return NS_OK;
#endif  // #ifndef XP_WIN
}

NS_IMETHODIMP
nsUpdateProcessor::WaitForProcessExit(uint32_t pid, uint32_t timeoutMS) {
#ifndef XP_WIN
  return NS_ERROR_NOT_IMPLEMENTED;
#else

  nsAutoHandle hProcess(OpenProcess(SYNCHRONIZE, FALSE, pid));
  if (!hProcess) {
    // It's possible the pid is incorrect, or the process has exited.
    // This isn't necessarily a failure state as if the process has
    // already exited then that is the desired behavior.
    MOZ_LOG(sUpdateLog, mozilla::LogLevel::Warning,
            ("WaitForProcessExit(%d): failed to OpenProcess", pid));
    return NS_OK;
  }

  // Wait up to timeoutMS milliseconds for termination.
  DWORD waitRv = WaitForSingleObjectEx(hProcess, timeoutMS, FALSE);
  if (waitRv != WAIT_OBJECT_0) {
    if (waitRv == WAIT_TIMEOUT) {
      MOZ_LOG(
          sUpdateLog, mozilla::LogLevel::Debug,
          ("WaitForProcessExit(%d): timed out after %d MS", pid, timeoutMS));
      return NS_ERROR_ABORT;
    }

    MOZ_LOG(sUpdateLog, mozilla::LogLevel::Warning,
            ("WaitForProcessExit(%d): unexpected error %lx", pid, waitRv));
    return NS_ERROR_FAILURE;
  }

  MOZ_LOG(sUpdateLog, mozilla::LogLevel::Debug,
          ("WaitForProcessExit(%d): success", pid));
  return NS_OK;
#endif  // XP_WIN
}
