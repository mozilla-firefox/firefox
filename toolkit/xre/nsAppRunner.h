/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsAppRunner_h_
#define nsAppRunner_h_

#ifdef XP_WIN
#  include <windows.h>
#  include "mozilla/WindowsConsole.h"
#else
#  include <limits.h>
#endif

#ifndef MAXPATHLEN
#  ifdef PATH_MAX
#    define MAXPATHLEN PATH_MAX
#  elif defined(_MAX_PATH)
#    define MAXPATHLEN _MAX_PATH
#  elif defined(CCHMAXPATH)
#    define MAXPATHLEN CCHMAXPATH
#  else
#    define MAXPATHLEN 1024
#  endif
#endif

#include "mozilla/Maybe.h"
#include "nsCOMPtr.h"
#include "nsIFile.h"
#include "nsString.h"
#include "nsXULAppAPI.h"
#ifdef MOZ_HAS_REMOTE
#  include "nsIRemoteService.h"
#endif

class nsINativeAppSupport;
class nsXREDirProvider;
class nsIToolkitProfileService;
class nsIFile;
class nsIProfileLock;
class nsIProfileUnlocker;
class nsIFactory;

extern nsXREDirProvider* gDirServiceProvider;

// NOTE: gAppData will be null in embedded contexts.
extern const mozilla::XREAppData* gAppData;
extern bool gSafeMode;

extern int gArgc;
extern char** gArgv;
extern int gRestartArgc;
extern char** gRestartArgv;
extern bool gRestartedByOS;
extern bool gLogConsoleErrors;
extern nsString gAbsoluteArgv0Path;

extern bool gIsGtest;

extern bool gKioskMode;
extern int gKioskMonitor;
extern bool gAllowContentAnalysisArgPresent;

namespace mozilla {
nsresult AppInfoConstructor(const nsID& aIID, void** aResult);

// Append the EncryptedDatabases marker to the running profile's
// compatibility.ini (append-only; skips if already present). Exposed as a free
// function so storage (SQLite at-rest encryption) can call it directly rather
// than via an XPCOM service lookup;
// nsIXULRuntime::MarkProfileEncryptedDatabases delegates here for the JS / test
// entry point.
nsresult MarkProfileEncryptedDatabases();
}  // namespace mozilla

// Exported for gtests.
void BuildCompatVersion(const char* aAppVersion, const char* aAppBuildID,
                        const char* aToolkitBuildID, nsACString& aBuf);

/**
 * Compares the provided compatibility versions. Returns 0 if they match,
 * < 0 if the new version is considered an upgrade from the old version and
 * > 0 if the new version is considered a downgrade from the old version.
 */
int32_t CompareCompatVersions(const nsACString& aOldCompatVersion,
                              const nsACString& aNewCompatVersion);

void ExtractCompatVersionInfo(const nsACString& aCompatVersion,
                              nsACString& aAppVersion, nsACString& aAppBuildID);

struct CompatCheckResult {
  bool isCompatible = false;
  bool cachesOK = false;
  bool isDowngrade = false;
  bool isDifferentInstall = false;
  bool hasEncryptedDatabases = false;
  nsCString lastAppVersion{VoidCString()};
  nsCString lastAppBuildID{VoidCString()};
  nsCString lastVersion;
};

CompatCheckResult CheckCompatibility(nsIFile* aProfileDir,
                                     const nsCString& aVersion,
                                     const nsCString& aOSABI,
                                     nsIFile* aXULRunnerDir, nsIFile* aAppDir,
                                     nsIFile* aFlagFile);

#ifdef MOZ_BLOCK_PROFILE_DOWNGRADE
mozilla::Maybe<mozilla::PathString> GenerateDowngradeTelemetry(
    const nsACString& aPingId, const nsCString& aLastVersion, bool aHasSync,
    int32_t aButton, const nsACString& aChannel,
    const nsACString& aProfileSelectionReason,
    mozilla::Maybe<PRTime> aReplacedLockTime, bool aIsDifferentInstall);

bool BuildDowngradePingUrl(const nsACString& aPingId,
                           const nsACString& aChannel, nsACString& aUrlOut);
#endif

/**
 * Create the nativeappsupport implementation.
 *
 * @note XPCOMInit has not happened yet.
 */
nsresult NS_CreateNativeAppSupport(nsINativeAppSupport** aResult);
already_AddRefed<nsINativeAppSupport> NS_GetNativeAppSupport();

#ifdef MOZ_HAS_REMOTE
already_AddRefed<nsIRemoteService> GetRemoteService();
#endif

/**
 * Try to acquire exclusive access to the specified profile directory.
 *
 * @param aPath
 *        The profile directory to lock.
 * @param aTempPath
 *        The corresponding profile temporary directory.
 * @param aUnlocker
 *        A callback interface used to attempt to unlock a profile that
 *        appears to be locked.
 * @param aResult
 *        The resulting profile lock object (or null if the profile could
 *        not be locked).
 *
 * @return NS_ERROR_FILE_ACCESS_DENIED to indicate that the profile
 *         directory cannot be unlocked.
 */
nsresult NS_LockProfilePath(nsIFile* aPath, nsIFile* aTempPath,
                            nsIProfileUnlocker** aUnlocker,
                            nsIProfileLock** aResult);

void WriteConsoleLog();

/**
 * Allow exit() calls to complete. This should be done from a proper Gecko
 * shutdown path. Otherwise we aim to catch improper shutdowns.
 */
void MozExpectedExit();

class nsINativeAppSupport;

// If aBlankCommandLine is true, then the application will be launched with a
// blank command line instead of being launched with the same command line that
// it was initially started with.
// If aTryExec is true then we use exec on platforms that support it to
// remain in the foreground.
nsresult LaunchChild(bool aBlankCommandLine, bool aTryExec = false);

void UnlockProfile();

#ifdef XP_WIN

BOOL WinLaunchChild(const wchar_t* exePath, int argc, char** argv,
                    HANDLE userToken = nullptr, HANDLE* hProcess = nullptr);

BOOL WinLaunchChild(const wchar_t* exePath, int argc, wchar_t** argv,
                    HANDLE userToken = nullptr, HANDLE* hProcess = nullptr);

#  define PREF_WIN_REGISTER_APPLICATION_RESTART \
    "toolkit.winRegisterApplicationRestart"

#  define PREF_WIN_ALTERED_DLL_PREFETCH "startup.experiments.alteredDllPrefetch"

#  if defined(MOZ_LAUNCHER_PROCESS)
#    define PREF_WIN_LAUNCHER_PROCESS_ENABLED "browser.launcherProcess.enabled"
#  endif  // defined(MOZ_LAUNCHER_PROCESS)
#endif

namespace mozilla {
namespace startup {
Result<nsCOMPtr<nsIFile>, nsresult> GetIncompleteStartupFile(nsIFile* aProfLD);

void IncreaseDescriptorLimits();
}  // namespace startup

const char* PlatformBuildID();

bool RunningGTest();

}  // namespace mozilla

/**
 * Set up platform specific error handling such as suppressing DLL load dialog
 * and the JIT debugger on Windows, and install unix signal handlers.
 */
void SetupErrorHandling(const char* progname);

#ifdef MOZ_ASAN_REPORTER
extern "C" {
void MOZ_EXPORT __sanitizer_set_report_path(const char* path);
}
void setASanReporterPath(nsIFile* aDir);
#endif

bool IsWaylandEnabled();

#endif  // nsAppRunner_h_
