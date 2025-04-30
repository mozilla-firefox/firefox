/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/ipc/GeckoChildProcessHost.h"

#include "mozilla/AppShutdown.h"
#include "mozilla/ArrayUtils.h"
#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/Components.h"
#include "mozilla/FilePreferences.h"
#include "mozilla/ChaosMode.h"
#include "mozilla/CmdLineAndEnvUtils.h"
#include "mozilla/IOInterposer.h"
#include "mozilla/ipc/UtilityProcessChild.h"
#include "mozilla/Likely.h"
#include "mozilla/MemoryChecking.h"
#include "mozilla/Poison.h"
#include "mozilla/Preferences.h"
#include "mozilla/PreferenceSheet.h"
#include "mozilla/Printf.h"
#include "mozilla/ProcessType.h"
#include "mozilla/ResultExtensions.h"
#include "mozilla/RuntimeExceptionModule.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/StaticPrefs_fission.h"
#include "mozilla/StaticPrefs_webgl.h"
#include "mozilla/StaticPrefs_widget.h"
#include "mozilla/glean/SecuritySandboxMetrics.h"
#include "mozilla/Telemetry.h"
#include "mozilla/Try.h"
#include "mozilla/Utf8.h"
#include "mozilla/intl/LocaleService.h"
#include "mozilla/JSONWriter.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/glean/ToolkitMozappsUpdateSharedMetrics.h"
#include "mozilla/glean/ToolkitXreMetrics.h"
#include "mozilla/glean/GleanPings.h"
#include "mozilla/widget/TextRecognition.h"
#include "BaseProfiler.h"
#include "mozJSModuleLoader.h"

#include "nsAppRunner.h"
#include "mozilla/XREAppData.h"
#include "mozilla/Bootstrap.h"
#if defined(MOZ_UPDATER) && !defined(MOZ_WIDGET_ANDROID)
#  include "nsUpdateDriver.h"
#  include "nsUpdateSyncManager.h"
#endif
#include "ProfileReset.h"

#ifdef XP_MACOSX
#  include "nsVersionComparator.h"
#  include "MacLaunchHelper.h"
#  include "MacApplicationDelegate.h"
#  include "MacAutoreleasePool.h"
#  include "MacRunFromDmgUtils.h"
// these are needed for sysctl
#  include <sys/types.h>
#  include <sys/sysctl.h>
#endif

#include "prnetdb.h"
#include "prprf.h"
#include "prproces.h"
#include "prenv.h"
#include "prtime.h"

#include "nsIAppStartup.h"
#include "nsCategoryManagerUtils.h"
#include "nsIMutableArray.h"
#include "nsCommandLine.h"
#include "nsIComponentRegistrar.h"
#include "nsIDialogParamBlock.h"
#include "mozilla/ModuleUtils.h"
#include "nsIIOService.h"
#include "nsIObserverService.h"
#include "nsINativeAppSupport.h"
#include "nsIPlatformInfo.h"
#include "nsIProcess.h"
#include "nsIProfileUnlocker.h"
#include "nsIPromptService.h"
#include "nsIPropertyBag2.h"
#include "nsIServiceManager.h"
#include "nsIStringBundle.h"
#include "nsISupportsPrimitives.h"
#include "nsIToolkitProfile.h"
#include "nsToolkitProfileService.h"
#include "nsIURI.h"
#include "nsIURL.h"
#include "nsIWindowCreator.h"
#include "nsIWindowWatcher.h"
#include "nsIXULAppInfo.h"
#include "nsIXULRuntime.h"
#include "nsPIDOMWindow.h"
#include "nsIWidget.h"
#include "nsAppShellCID.h"
#include "mozilla/dom/quota/QuotaManager.h"
#include "mozilla/scache/StartupCache.h"
#include "gfxPlatform.h"
#include "PDMFactory.h"
#ifdef XP_MACOSX
#  include "gfxPlatformMac.h"
#endif

#include "mozilla/Unused.h"

#ifdef XP_WIN
#  include "nsIWinAppHelper.h"
#  include <windows.h>
#  include <intrin.h>
#  include <math.h>
#  include "cairo/cairo-features.h"
#  include "detect_win32k_conflicts.h"
#  include "mozilla/PreXULSkeletonUI.h"
#  include "mozilla/DllPrefetchExperimentRegistryInfo.h"
#  include "mozilla/WindowsBCryptInitialization.h"
#  include "mozilla/WindowsDllBlocklist.h"
#  include "mozilla/WindowsMsctfInitialization.h"
#  include "mozilla/WindowsOleAut32Initialization.h"
#  include "mozilla/WindowsProcessMitigations.h"
#  include "mozilla/WindowsVersion.h"
#  include "mozilla/WinHeaderOnlyUtils.h"
#  include "mozilla/mscom/ProcessRuntime.h"
#  include "mozilla/mscom/ProfilerMarkers.h"
#  include "WinTokenUtils.h"

#  if defined(MOZ_LAUNCHER_PROCESS)
#    include "mozilla/LauncherRegistryInfo.h"
#  endif

#  if defined(MOZ_DEFAULT_BROWSER_AGENT)
#    include "nsIWindowsRegKey.h"
#  endif

#  ifndef PROCESS_DEP_ENABLE
#    define PROCESS_DEP_ENABLE 0x1
#  endif
#endif

#if defined(MOZ_SANDBOX)
#  include "mozilla/SandboxSettings.h"
#endif

#ifdef ACCESSIBILITY
#  include "nsAccessibilityService.h"
#  if defined(XP_WIN)
#    include "mozilla/a11y/Compatibility.h"
#    include "mozilla/a11y/Platform.h"
#  endif
#endif

#include "nsCRT.h"
#include "nsCOMPtr.h"
#include "nsDirectoryServiceDefs.h"
#include "nsDirectoryServiceUtils.h"
#include "nsEmbedCID.h"
#include "nsIDUtils.h"
#include "nsNetUtil.h"
#include "nsReadableUtils.h"
#include "nsXPCOM.h"
#include "nsXPCOMCIDInternal.h"
#include "nsString.h"
#include "nsPrintfCString.h"
#include "nsVersionComparator.h"

#include "nsAppDirectoryServiceDefs.h"
#include "nsXULAppAPI.h"
#include "nsXREDirProvider.h"

#include "nsINIParser.h"
#include "mozilla/Omnijar.h"
#include "mozilla/StartupTimeline.h"
#include "mozilla/LateWriteChecks.h"

#include <stdlib.h>
#include <locale.h>

#ifdef XP_UNIX
#  include <errno.h>
#  include <pwd.h>
#  include <string.h>
#  include <sys/resource.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

#ifdef XP_WIN
#  include <process.h>
#  include <shlobj.h>
#  include "mozilla/WinDllServices.h"
#  include "nsThreadUtils.h"
#  include "WinUtils.h"
#endif

#ifdef XP_MACOSX
#  include "nsILocalFileMac.h"
#  include "nsCommandLineServiceMac.h"
#endif

// for X remote support
#if defined(MOZ_HAS_REMOTE)
#  include "nsRemoteService.h"
#endif

#if defined(DEBUG) && defined(XP_WIN)
#  include <malloc.h>
#endif

#if defined(XP_MACOSX)
#  include <Carbon/Carbon.h>
#endif

#ifdef DEBUG
#  include "mozilla/Logging.h"
#  include "mozilla/StaticPrefs_toolkit.h"
#endif

#include "nsExceptionHandler.h"
#include "nsICrashReporter.h"
#include "nsIPrefService.h"
#include "nsIMemoryInfoDumper.h"
#include "js/String.h"
#if defined(XP_LINUX) && !defined(ANDROID)
#  include "mozilla/widget/LSBUtils.h"
#endif

#include "base/command_line.h"
#include "GTestRunner.h"

#ifdef MOZ_WIDGET_ANDROID
#  include "gfxAndroidPlatform.h"
#  include "mozilla/java/GeckoAppShellWrappers.h"
#endif

#if defined(MOZ_SANDBOX)
#  if defined(XP_LINUX) && !defined(ANDROID)
#    include "mozilla/SandboxInfo.h"
#  elif defined(XP_WIN)
#    include "sandboxBroker.h"
#  endif
#endif

#ifdef MOZ_CODE_COVERAGE
#  include "mozilla/CodeCoverageHandler.h"
#endif

#include "GMPProcessChild.h"
#include "SafeMode.h"

#ifdef MOZ_BACKGROUNDTASKS
#  include "mozilla/BackgroundTasks.h"
#  include "nsIPowerManagerService.h"
#  include "nsIStringBundle.h"
#endif

#ifdef USE_GLX_TEST
#  include "mozilla/GUniquePtr.h"
#  include "mozilla/GfxInfo.h"
#endif

#ifdef MOZ_WIDGET_GTK
#  include "nsAppShell.h"
#endif
#ifdef MOZ_ENABLE_DBUS
#  include "DBusService.h"
#endif

extern uint32_t gRestartMode;
extern void InstallSignalHandlers(const char* ProgramName);

#define FILE_COMPATIBILITY_INFO "compatibility.ini"_ns
#define FILE_INVALIDATE_CACHES ".purgecaches"_ns
#define FILE_STARTUP_INCOMPLETE u".startup-incomplete"_ns

#if defined(MOZ_BLOCK_PROFILE_DOWNGRADE) || defined(MOZ_LAUNCHER_PROCESS) || \
    defined(MOZ_DEFAULT_BROWSER_AGENT)
static const char kPrefHealthReportUploadEnabled[] =
    "datareporting.healthreport.uploadEnabled";
#endif  // defined(MOZ_BLOCK_PROFILE_DOWNGRADE) || defined(MOZ_LAUNCHER_PROCESS)
        // || defined(MOZ_DEFAULT_BROWSER_AGENT)
#if defined(MOZ_DEFAULT_BROWSER_AGENT)
static const char kPrefDefaultAgentEnabled[] = "default-browser-agent.enabled";

static const char kPrefServicesSettingsServer[] = "services.settings.server";
static const char kPrefSetDefaultBrowserUserChoicePref[] =
    "browser.shell.setDefaultBrowserUserChoice";
#endif  // defined(MOZ_DEFAULT_BROWSER_AGENT)

#if defined(XP_WIN)
static const char kPrefThemeId[] = "extensions.activeThemeID";
static const char kPrefBrowserStartupBlankWindow[] =
    "browser.startup.blankWindow";
static const char kPrefPreXulSkeletonUI[] = "browser.startup.preXulSkeletonUI";
#endif  // defined(XP_WIN)

#if defined(MOZ_WIDGET_GTK)
constexpr nsLiteralCString kStartupTokenNames[] = {
    "XDG_ACTIVATION_TOKEN"_ns,
    "DESKTOP_STARTUP_ID"_ns,
};
#endif

int gArgc;
char** gArgv;

static const char gToolkitVersion[] = MOZ_STRINGIFY(GRE_MILESTONE);
// The gToolkitBuildID global is defined to MOZ_BUILDID via gen_buildid.py
// in toolkit/library. See related comment in toolkit/library/moz.build.
extern const char gToolkitBuildID[];

static nsIProfileLock* gProfileLock;
#if defined(MOZ_HAS_REMOTE)
MOZ_RUNINIT static RefPtr<nsRemoteService> gRemoteService;
MOZ_RUNINIT static RefPtr<nsStartupLock> gStartupLock;
#endif

int gRestartArgc;
char** gRestartArgv;

// If gRestartedByOS is set, we were automatically restarted by the OS.
bool gRestartedByOS = false;

bool gIsGtest = false;

bool gKioskMode = false;
int gKioskMonitor = -1;

bool gAllowContentAnalysisArgPresent = false;

MOZ_CONSTINIT nsString gAbsoluteArgv0Path;

#if defined(XP_WIN)
MOZ_CONSTINIT nsString gProcessStartupShortcut;
#endif

#if defined(MOZ_WIDGET_GTK)
#  include <glib.h>
#  include "mozilla/WidgetUtilsGtk.h"
#  include <gtk/gtk.h>
#  ifdef MOZ_WAYLAND
#    include <gdk/gdkwayland.h>
#    include "mozilla/widget/nsWaylandDisplay.h"
#    include "wayland-proxy.h"
#  endif
#  ifdef MOZ_X11
#    include <gdk/gdkx.h>
#  endif /* MOZ_X11 */
#endif

#if defined(MOZ_WAYLAND)
MOZ_RUNINIT std::unique_ptr<WaylandProxy> gWaylandProxy;
#endif

#include "BinaryPath.h"

#ifdef FUZZING
#  include "FuzzerRunner.h"

namespace mozilla {
FuzzerRunner* fuzzerRunner = 0;
}  // namespace mozilla

#  ifdef LIBFUZZER
void XRE_LibFuzzerSetDriver(LibFuzzerDriver aDriver) {
  mozilla::fuzzerRunner->setParams(aDriver);
}
#  endif
#endif  // FUZZING

// Undo X11/X.h's definition of None
#undef None

namespace mozilla {
int (*RunGTest)(int*, char**) = 0;

bool RunningGTest() { return RunGTest; }
}  // namespace mozilla

using namespace mozilla;
using namespace mozilla::widget;
using namespace mozilla::startup;
using mozilla::Unused;
using mozilla::dom::ContentChild;
using mozilla::dom::ContentParent;
using mozilla::dom::quota::QuotaManager;
using mozilla::intl::LocaleService;
using mozilla::scache::StartupCache;

// Save the given word to the specified environment variable.
static void MOZ_NEVER_INLINE SaveWordToEnv(const char* name,
                                           const nsACString& word) {
  char* expr =
      Smprintf("%s=%s", name, PromiseFlatCString(word).get()).release();
  if (expr) PR_SetEnv(expr);
  // We intentionally leak |expr| here since it is required by PR_SetEnv.
}

// Save the path of the given file to the specified environment variable.
static void SaveFileToEnv(const char* name, nsIFile* file) {
#ifdef XP_WIN
  nsAutoString path;
  file->GetPath(path);
  SetEnvironmentVariableW(NS_ConvertASCIItoUTF16(name).get(), path.get());
#else
  nsAutoCString path;
  file->GetNativePath(path);
  SaveWordToEnv(name, path);
#endif
}

static bool gIsExpectedExit = false;

void MozExpectedExit() { gIsExpectedExit = true; }

/**
 * Runs atexit() to catch unexpected exit from 3rd party libraries like the
 * Intel graphics driver calling exit in an error condition. When they
 * call exit() to report an error we won't shutdown correctly and wont catch
 * the issue with our crash reporter.
 */
static void UnexpectedExit() {
  if (!gIsExpectedExit) {
    gIsExpectedExit = true;  // Don't risk re-entrency issues when crashing.
    MOZ_CRASH("Exit called by third party code.");
  }
}

#if defined(MOZ_WAYLAND)
bool IsWaylandEnabled() {
  static bool isWaylandEnabled = []() {
    const char* waylandDisplay = PR_GetEnv("WAYLAND_DISPLAY");
    if (!waylandDisplay) {
      return false;
    }
    if (!PR_GetEnv("DISPLAY")) {
      // No X11 display, so try to run wayland.
      return true;
    }
    // MOZ_ENABLE_WAYLAND is our primary Wayland on/off switch.
    if (const char* waylandPref = PR_GetEnv("MOZ_ENABLE_WAYLAND")) {
      return *waylandPref == '1';
    }
    if (const char* backendPref = PR_GetEnv("GDK_BACKEND")) {
      if (!strncmp(backendPref, "wayland", 7)) {
        NS_WARNING(
            "Wayland backend should be enabled by MOZ_ENABLE_WAYLAND=1."
            "GDK_BACKEND is a Gtk3 debug variable and may cause issues.");
        return true;
      }
    }
    // Enable by default when we're running on a recent enough GTK version. We'd
    // like to check further details like compositor version and so on ideally
    // to make sure we don't enable it on old Mutter or what not, but we can't,
    // so let's assume that if the user is running on a Wayland session by
    // default we're ok, since either the distro has enabled Wayland by default,
    // or the user has gone out of their way to use Wayland.
    return !gtk_check_version(3, 24, 30);
  }();
  return isWaylandEnabled;
}
#else
bool IsWaylandEnabled() { return false; }
#endif

/**
 * Output a string to the user.  This method is really only meant to be used to
 * output last-ditch error messages designed for developers NOT END USERS.
 *
 * @param isError
 *        Pass true to indicate severe errors.
 * @param fmt
 *        printf-style format string followed by arguments.
 */
static MOZ_FORMAT_PRINTF(2, 3) void Output(bool isError, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);

#if defined(XP_WIN) && !MOZ_WINCONSOLE
  SmprintfPointer msg = mozilla::Vsmprintf(fmt, ap);
  if (msg) {
    UINT flags = MB_OK;
    if (isError)
      flags |= MB_ICONERROR;
    else
      flags |= MB_ICONINFORMATION;

    wchar_t wide_msg[1024];
    MultiByteToWideChar(CP_ACP, 0, msg.get(), -1, wide_msg,
                        sizeof(wide_msg) / sizeof(wchar_t));

    wchar_t wide_caption[128];
    MultiByteToWideChar(CP_ACP, 0, gAppData ? gAppData->name : "XULRunner", -1,
                        wide_caption, sizeof(wide_caption) / sizeof(wchar_t));
    MessageBoxW(nullptr, wide_msg, wide_caption, flags);
  }
#elif defined(MOZ_WIDGET_ANDROID)
  SmprintfPointer msg = mozilla::Vsmprintf(fmt, ap);
  if (msg) {
    __android_log_print(isError ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO,
                        "GeckoRuntime", "%s", msg.get());
  }
#else
  vfprintf(stderr, fmt, ap);
#endif

  va_end(ap);
}

/**
 * Check for a commandline flag. If the flag takes a parameter, the
 * parameter is returned in aParam. Flags may be in the form -arg or
 * --arg (or /arg on win32).
 *
 * @param aArg the parameter to check. Must be lowercase.
 * @param aParam if non-null, the -arg <data> will be stored in this pointer.
 *        This is *not* allocated, but rather a pointer to the argv data.
 * @param aFlags flags @see CheckArgFlag
 */
static ArgResult CheckArg(const char* aArg, const char** aParam = nullptr,
                          CheckArgFlag aFlags = CheckArgFlag::RemoveArg) {
  MOZ_ASSERT(gArgv, "gArgv must be initialized before CheckArg()");
  return CheckArg(gArgc, gArgv, aArg, aParam, aFlags);
}

/**
 * Check for a commandline flag. Ignore data that's passed in with the flag.
 * Flags may be in the form -arg or --arg (or /arg on win32).
 * Will not remove flag if found.
 *
 * @param aArg the parameter to check. Must be lowercase.
 */
static ArgResult CheckArgExists(const char* aArg) {
  return CheckArg(aArg, nullptr, CheckArgFlag::None);
}

bool gSafeMode = false;
bool gFxREmbedded = false;

enum E10sStatus {
  kE10sEnabledByDefault,
  kE10sForceDisabled,
};

static bool gBrowserTabsRemoteAutostart = false;
static E10sStatus gBrowserTabsRemoteStatus;
static bool gBrowserTabsRemoteAutostartInitialized = false;

namespace mozilla {

bool BrowserTabsRemoteAutostart() {
  if (gBrowserTabsRemoteAutostartInitialized) {
    return gBrowserTabsRemoteAutostart;
  }
  gBrowserTabsRemoteAutostartInitialized = true;

  // If we're not in the parent process, we are running E10s.
  if (!XRE_IsParentProcess()) {
    gBrowserTabsRemoteAutostart = true;
    return gBrowserTabsRemoteAutostart;
  }

  gBrowserTabsRemoteAutostart = true;
  E10sStatus status = kE10sEnabledByDefault;

// We are checking to ensure that either MOZILLA_OFFICIAL is undefined or that
// xpc::AreNonLocalConnectionsDisabled() is true. If either check matches,
// we move on to check the MOZ_FORCE_DISABLE_E10S environment variable which
// must equal "1" to proceed with disabling e10s.
#if defined(MOZILLA_OFFICIAL)
  bool allowDisablingE10s = xpc::AreNonLocalConnectionsDisabled();
#else
  bool allowDisablingE10s = true;
#endif

  if (gBrowserTabsRemoteAutostart && allowDisablingE10s) {
    const char* forceDisable = PR_GetEnv("MOZ_FORCE_DISABLE_E10S");
    if (forceDisable && !strcmp(forceDisable, "1")) {
      gBrowserTabsRemoteAutostart = false;
      status = kE10sForceDisabled;
    }
  }

  gBrowserTabsRemoteStatus = status;

  return gBrowserTabsRemoteAutostart;
}

}  // namespace mozilla

// Win32k Infrastructure ==============================================

// This bool tells us if we have initialized the following two statics
// upon startup.

static bool gWin32kInitialized = false;

// Win32k Lockdown for the current session is determined once, at startup,
// and then remains the same for the duration of the session.

static nsIXULRuntime::ContentWin32kLockdownState gWin32kStatus;

// The status of the win32k experiment. It is determined once, at startup,
// and then remains the same for the duration of the session.

static nsIXULRuntime::ExperimentStatus gWin32kExperimentStatus =
    nsIXULRuntime::eExperimentStatusUnenrolled;

#ifdef XP_WIN
// The states for win32k lockdown can be generalized to:
//  - User doesn't meet requirements (bad OS, webrender, remotegl) aka
//  PersistentRequirement
//  - User has Safe Mode enabled, Win32k Disabled by Env Var, or E10S disabled
//  by Env Var aka TemporaryRequirement
//  - User has set the win32k pref to something non-default aka NonDefaultPref
//  - User has been enrolled in the experiment and does what it says aka
//  Enrolled
//  - User does the default aka Default
//
// We expect the below behvaior.  In the code, there may be references to these
//     behaviors (e.g. [A]) but not always.
//
// [A] Becoming enrolled in the experiment while NonDefaultPref disqualifies
//     you upon next browser start
// [B] Becoming enrolled in the experiment while PersistentRequirement
//     disqualifies you upon next browser start
// [C] Becoming enrolled in the experiment while TemporaryRequirement does not
//     disqualify you
// [D] Becoming enrolled in the experiment will only take effect after restart
// [E] While enrolled, becoming PersistentRequirement will not disqualify you
//     until browser restart, but if the state is present at browser start,
//     will disqualify you. Additionally, it will not affect the session value
//     of gWin32kStatus.
//     XXX(bobowen): I hope both webrender and wbgl.out-of-process require a
//                   restart to take effect!
// [F] While enrolled, becoming NonDefaultPref will disqualify you upon next
//     browser start
// [G] While enrolled, becoming TemporaryRequirement will _not_ disqualify you,
//     but the session status will prevent win32k lockdown from taking effect.

// The win32k pref
static const char kPrefWin32k[] = "security.sandbox.content.win32k-disable";

// The current enrollment status as controlled by Normandy. This value is only
// stored in the default preference branch, and is not persisted across
// sessions by the preference service. It therefore isn't available early
// enough at startup, and needs to be synced to a preference in the user
// branch which is persisted across sessions.
static const char kPrefWin32kExperimentEnrollmentStatus[] =
    "security.sandbox.content.win32k-experiment.enrollmentStatus";

// The enrollment status to be used at browser startup. This automatically
// synced from the above enrollmentStatus preference whenever the latter is
// changed. We reused the Fission experiment enum - it can have any of the
// values defined in the `nsIXULRuntime_ExperimentStatus` enum _except_ rollout.
// Meanings are documented in the declaration of
// `nsIXULRuntime.fissionExperimentStatus`
static const char kPrefWin32kExperimentStartupEnrollmentStatus[] =
    "security.sandbox.content.win32k-experiment.startupEnrollmentStatus";

namespace mozilla {

bool Win32kExperimentEnrolled() {
  MOZ_ASSERT(XRE_IsParentProcess());
  return gWin32kExperimentStatus == nsIXULRuntime::eExperimentStatusControl ||
         gWin32kExperimentStatus == nsIXULRuntime::eExperimentStatusTreatment;
}

}  // namespace mozilla

static bool Win32kRequirementsUnsatisfied(
    nsIXULRuntime::ContentWin32kLockdownState aStatus) {
  return aStatus == nsIXULRuntime::ContentWin32kLockdownState::
                        OperatingSystemNotSupported ||
         aStatus ==
             nsIXULRuntime::ContentWin32kLockdownState::MissingWebRender ||
         aStatus ==
             nsIXULRuntime::ContentWin32kLockdownState::MissingRemoteWebGL ||
         aStatus ==
             nsIXULRuntime::ContentWin32kLockdownState::DecodersArentRemote;
}

static void Win32kExperimentDisqualify() {
  MOZ_ASSERT(XRE_IsParentProcess());
  Preferences::SetUint(kPrefWin32kExperimentEnrollmentStatus,
                       nsIXULRuntime::eExperimentStatusDisqualified);
}

static void OnWin32kEnrollmentStatusChanged(const char* aPref, void* aData) {
  auto newStatusInt =
      Preferences::GetUint(kPrefWin32kExperimentEnrollmentStatus,
                           nsIXULRuntime::eExperimentStatusUnenrolled);
  auto newStatus = newStatusInt < nsIXULRuntime::eExperimentStatusCount
                       ? nsIXULRuntime::ExperimentStatus(newStatusInt)
                       : nsIXULRuntime::eExperimentStatusDisqualified;

  // Set the startup pref for next browser start [D]
  Preferences::SetUint(kPrefWin32kExperimentStartupEnrollmentStatus, newStatus);
}

namespace {
// This observer is notified during `profile-before-change`, and ensures that
// the experiment enrollment status is synced over before the browser shuts
// down, even if it was not modified since win32k was initialized.
class Win32kEnrollmentStatusShutdownObserver final : public nsIObserver {
 public:
  NS_DECL_ISUPPORTS

  NS_IMETHOD Observe(nsISupports* aSubject, const char* aTopic,
                     const char16_t* aData) override {
    MOZ_ASSERT(!strcmp("profile-before-change", aTopic));
    OnWin32kEnrollmentStatusChanged(kPrefWin32kExperimentEnrollmentStatus,
                                    nullptr);
    return NS_OK;
  }

 private:
  ~Win32kEnrollmentStatusShutdownObserver() = default;
};
NS_IMPL_ISUPPORTS(Win32kEnrollmentStatusShutdownObserver, nsIObserver)
}  // namespace

static void OnWin32kChanged(const char* aPref, void* aData) {
  // If we're actively enrolled in the Win32k experiment, disqualify the user
  // from the experiment if the Win32k pref is modified. [F]
  if (Win32kExperimentEnrolled() && Preferences::HasUserValue(kPrefWin32k)) {
    Win32kExperimentDisqualify();
  }
}

#endif  // XP_WIN

namespace mozilla {
void EnsureWin32kInitialized();
}

nsIXULRuntime::ContentWin32kLockdownState GetLiveWin32kLockdownState() {
#ifdef XP_WIN

  // HasUserValue The Pref functions can only be called on main thread
  MOZ_ASSERT(NS_IsMainThread());

#  ifdef MOZ_BACKGROUNDTASKS
  if (BackgroundTasks::IsBackgroundTaskMode()) {
    // Let's bail out before loading all the graphics libs.
    return nsIXULRuntime::ContentWin32kLockdownState::DisabledByDefault;
  }
#  endif

  mozilla::EnsureWin32kInitialized();
  gfxPlatform::GetPlatform();

  if (gSafeMode) {
    return nsIXULRuntime::ContentWin32kLockdownState::DisabledBySafeMode;
  }

  if (EnvHasValue("MOZ_ENABLE_WIN32K")) {
    return nsIXULRuntime::ContentWin32kLockdownState::DisabledByEnvVar;
  }

  if (!mozilla::BrowserTabsRemoteAutostart()) {
    return nsIXULRuntime::ContentWin32kLockdownState::DisabledByE10S;
  }

  // Win32k lockdown is available on Win8+, but we are initially limiting it to
  // Windows 10 v1709 (build 16299) or later. Before this COM initialization
  // currently fails if user32.dll has loaded before it is called.
  if (!IsWin10FallCreatorsUpdateOrLater()) {
    return nsIXULRuntime::ContentWin32kLockdownState::
        OperatingSystemNotSupported;
  }

  {
    ConflictingMitigationStatus conflictingMitigationStatus = {};
    if (!detect_win32k_conflicting_mitigations(&conflictingMitigationStatus)) {
      return nsIXULRuntime::ContentWin32kLockdownState::
          IncompatibleMitigationPolicy;
    }
    if (conflictingMitigationStatus.caller_check ||
        conflictingMitigationStatus.sim_exec ||
        conflictingMitigationStatus.stack_pivot) {
      return nsIXULRuntime::ContentWin32kLockdownState::
          IncompatibleMitigationPolicy;
    }
  }

  // Win32k Lockdown requires Remote WebGL, but it may be disabled on
  // certain hardware or virtual machines.
  if (!gfx::gfxVars::AllowWebglOop() || !StaticPrefs::webgl_out_of_process()) {
    return nsIXULRuntime::ContentWin32kLockdownState::MissingRemoteWebGL;
  }

  // Some (not sure exactly which) decoders are not compatible
  if (!PDMFactory::AllDecodersAreRemote()) {
    return nsIXULRuntime::ContentWin32kLockdownState::DecodersArentRemote;
  }

  bool prefSetByUser =
      Preferences::HasUserValue("security.sandbox.content.win32k-disable");
  bool prefValue = Preferences::GetBool(
      "security.sandbox.content.win32k-disable", false, PrefValueKind::User);
  bool defaultValue = Preferences::GetBool(
      "security.sandbox.content.win32k-disable", false, PrefValueKind::Default);

  if (prefSetByUser) {
    if (prefValue) {
      return nsIXULRuntime::ContentWin32kLockdownState::EnabledByUserPref;
    } else {
      return nsIXULRuntime::ContentWin32kLockdownState::DisabledByUserPref;
    }
  }

  if (gWin32kExperimentStatus ==
      nsIXULRuntime::ExperimentStatus::eExperimentStatusControl) {
    return nsIXULRuntime::ContentWin32kLockdownState::DisabledByControlGroup;
  } else if (gWin32kExperimentStatus ==
             nsIXULRuntime::ExperimentStatus::eExperimentStatusTreatment) {
    return nsIXULRuntime::ContentWin32kLockdownState::EnabledByTreatmentGroup;
  }

  if (defaultValue) {
    return nsIXULRuntime::ContentWin32kLockdownState::EnabledByDefault;
  } else {
    return nsIXULRuntime::ContentWin32kLockdownState::DisabledByDefault;
  }

#else

  return nsIXULRuntime::ContentWin32kLockdownState::OperatingSystemNotSupported;

#endif
}

namespace mozilla {

void EnsureWin32kInitialized() {
  if (gWin32kInitialized) {
    return;
  }
  gWin32kInitialized = true;

#ifdef XP_WIN

  // Initialize the Win32k experiment, configuring win32k's
  // default, before checking other overrides. This allows opting-out of a
  // Win32k experiment through about:preferences or about:config from a
  // safemode session.
  uint32_t experimentRaw =
      Preferences::GetUint(kPrefWin32kExperimentStartupEnrollmentStatus,
                           nsIXULRuntime::eExperimentStatusUnenrolled);
  gWin32kExperimentStatus =
      experimentRaw < nsIXULRuntime::eExperimentStatusCount
          ? nsIXULRuntime::ExperimentStatus(experimentRaw)
          : nsIXULRuntime::eExperimentStatusDisqualified;

  // Watch the experiment enrollment status pref to detect experiment
  // disqualification, and ensure it is propagated for the next restart.
  Preferences::RegisterCallback(&OnWin32kEnrollmentStatusChanged,
                                kPrefWin32kExperimentEnrollmentStatus);
  if (nsCOMPtr<nsIObserverService> observerService =
          mozilla::services::GetObserverService()) {
    nsCOMPtr<nsIObserver> shutdownObserver =
        new Win32kEnrollmentStatusShutdownObserver();
    observerService->AddObserver(shutdownObserver, "profile-before-change",
                                 false);
  }

  // If the user no longer qualifies because they edited a required pref, check
  // that. [B] [E]
  auto tmpStatus = GetLiveWin32kLockdownState();
  if (Win32kExperimentEnrolled() && Win32kRequirementsUnsatisfied(tmpStatus)) {
    Win32kExperimentDisqualify();
    gWin32kExperimentStatus = nsIXULRuntime::eExperimentStatusDisqualified;
  }

  // If the user has overridden an active experiment by setting a user value for
  // "security.sandbox.content.win32k-disable", disqualify the user from the
  // experiment. [A] [F]
  if (Preferences::HasUserValue(kPrefWin32k) && Win32kExperimentEnrolled()) {
    Win32kExperimentDisqualify();
    gWin32kExperimentStatus = nsIXULRuntime::eExperimentStatusDisqualified;
  }

  // Unlike Fission, we do not configure the default branch based on experiment
  // enrollment status.  Instead we check the startupEnrollmentPref in
  // GetContentWin32kLockdownState()

  Preferences::RegisterCallback(&OnWin32kChanged, kPrefWin32k);

  // Set the state
  gWin32kStatus = GetLiveWin32kLockdownState();

#else
  gWin32kStatus =
      nsIXULRuntime::ContentWin32kLockdownState::OperatingSystemNotSupported;
  gWin32kExperimentStatus = nsIXULRuntime::eExperimentStatusUnenrolled;

#endif  // XP_WIN
}

nsIXULRuntime::ContentWin32kLockdownState GetWin32kLockdownState() {
#ifdef XP_WIN

  mozilla::EnsureWin32kInitialized();
  return gWin32kStatus;

#else

  return nsIXULRuntime::ContentWin32kLockdownState::OperatingSystemNotSupported;

#endif
}

}  // namespace mozilla

// End Win32k Infrastructure ==========================================

// Fission Infrastructure =============================================

// Fission enablement for the current session is determined once, at startup,
// and then remains the same for the duration of the session.
//
// The following factors determine whether or not Fission is enabled for a
// session, in order of precedence:
//
// - Safe mode: In safe mode, Fission is never enabled.
//
// - The MOZ_FORCE_ENABLE_FISSION environment variable: If set to any value,
//   Fission will be enabled.
//
// - The 'fission.autostart' preference, if it has been configured by the user.
static const char kPrefFissionAutostart[] = "fission.autostart";

// The computed FissionAutostart value for the session, read by content
// processes to initialize gFissionAutostart.
//
// This pref is locked, and only configured on the default branch, so should
// never be persisted in a profile.
static const char kPrefFissionAutostartSession[] = "fission.autostart.session";

//
// The computed FissionAutostart value for the session, read by content
// processes to initialize gFissionAutostart.

static bool gFissionAutostart = false;
static bool gFissionAutostartInitialized = false;
static nsIXULRuntime::FissionDecisionStatus gFissionDecisionStatus;
static void EnsureFissionAutostartInitialized() {
  if (gFissionAutostartInitialized) {
    return;
  }
  gFissionAutostartInitialized = true;

  if (!XRE_IsParentProcess()) {
    // This pref is configured for the current session by the parent process.
    gFissionAutostart = Preferences::GetBool(kPrefFissionAutostartSession,
                                             false, PrefValueKind::Default);
    return;
  }

  if (!BrowserTabsRemoteAutostart()) {
    gFissionAutostart = false;
    if (gBrowserTabsRemoteStatus == kE10sForceDisabled) {
      gFissionDecisionStatus = nsIXULRuntime::eFissionDisabledByE10sEnv;
    } else {
      gFissionDecisionStatus = nsIXULRuntime::eFissionDisabledByE10sOther;
    }
  } else if (EnvHasValue("MOZ_FORCE_ENABLE_FISSION")) {
    gFissionAutostart = true;
    gFissionDecisionStatus = nsIXULRuntime::eFissionEnabledByEnv;
  } else if (EnvHasValue("MOZ_FORCE_DISABLE_FISSION")) {
    gFissionAutostart = false;
    gFissionDecisionStatus = nsIXULRuntime::eFissionDisabledByEnv;
  } else {
    // NOTE: This will take into account changes to the default due to
    // `InitializeFissionExperimentStatus`.
    gFissionAutostart = Preferences::GetBool(kPrefFissionAutostart, false);
    if (Preferences::HasUserValue(kPrefFissionAutostart)) {
      gFissionDecisionStatus = gFissionAutostart
                                   ? nsIXULRuntime::eFissionEnabledByUserPref
                                   : nsIXULRuntime::eFissionDisabledByUserPref;
    } else {
      gFissionDecisionStatus = gFissionAutostart
                                   ? nsIXULRuntime::eFissionEnabledByDefault
                                   : nsIXULRuntime::eFissionDisabledByDefault;
    }
  }

  // Content processes cannot run the same logic as we're running in the parent
  // process, as the current value of various preferences may have changed
  // between launches. Instead, the content process will read the default branch
  // of the locked `fission.autostart.session` preference to determine the value
  // determined by this method.
  Preferences::Unlock(kPrefFissionAutostartSession);
  Preferences::ClearUser(kPrefFissionAutostartSession);
  Preferences::SetBool(kPrefFissionAutostartSession, gFissionAutostart,
                       PrefValueKind::Default);
  Preferences::Lock(kPrefFissionAutostartSession);
}

namespace mozilla {

bool FissionAutostart() {
  EnsureFissionAutostartInitialized();
  return gFissionAutostart;
}

}  // namespace mozilla

// End Fission Infrastructure =========================================

namespace mozilla {

bool SessionHistoryInParent() {
  return FissionAutostart() ||
         !StaticPrefs::
             fission_disableSessionHistoryInParent_AtStartup_DoNotUseDirectly();
}

bool SessionStorePlatformCollection() {
  return SessionHistoryInParent() &&
         !StaticPrefs::
             browser_sessionstore_disable_platform_collection_AtStartup_DoNotUseDirectly();
}

bool BFCacheInParent() {
  return SessionHistoryInParent() &&
         StaticPrefs::fission_bfcacheInParent_DoNotUseDirectly();
}

}  // namespace mozilla

/**
 * The nsXULAppInfo object implements nsIFactory so that it can be its own
 * singleton.
 */
class nsXULAppInfo : public nsIXULAppInfo,
#ifdef XP_WIN
                     public nsIWinAppHelper,
#endif
                     public nsICrashReporter,
                     public nsIFinishDumpingCallback,
                     public nsIXULRuntime

{
 public:
  constexpr nsXULAppInfo() = default;
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIPLATFORMINFO
  NS_DECL_NSIXULAPPINFO
  NS_DECL_NSIXULRUNTIME
  NS_DECL_NSICRASHREPORTER
  NS_DECL_NSIFINISHDUMPINGCALLBACK
#ifdef XP_WIN
  NS_DECL_NSIWINAPPHELPER
#endif
};

NS_INTERFACE_MAP_BEGIN(nsXULAppInfo)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIXULRuntime)
  NS_INTERFACE_MAP_ENTRY(nsIXULRuntime)
#ifdef XP_WIN
  NS_INTERFACE_MAP_ENTRY(nsIWinAppHelper)
#endif
  NS_INTERFACE_MAP_ENTRY(nsICrashReporter)
  NS_INTERFACE_MAP_ENTRY(nsIFinishDumpingCallback)
  NS_INTERFACE_MAP_ENTRY(nsIPlatformInfo)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIXULAppInfo,
                                     gAppData || XRE_IsContentProcess())
NS_INTERFACE_MAP_END

NS_IMETHODIMP_(MozExternalRefCountType)
nsXULAppInfo::AddRef() { return 1; }

NS_IMETHODIMP_(MozExternalRefCountType)
nsXULAppInfo::Release() { return 1; }

NS_IMETHODIMP
nsXULAppInfo::GetVendor(nsACString& aResult) {
  if (XRE_IsContentProcess()) {
    ContentChild* cc = ContentChild::GetSingleton();
    aResult = cc->GetAppInfo().vendor;
    return NS_OK;
  }
  aResult.Assign(gAppData->vendor);

  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetName(nsACString& aResult) {
  if (XRE_IsContentProcess()) {
    ContentChild* cc = ContentChild::GetSingleton();
    aResult = cc->GetAppInfo().name;
    return NS_OK;
  }

#ifdef MOZ_WIDGET_ANDROID
  nsCString name = java::GeckoAppShell::GetAppName()->ToCString();
  aResult.Assign(std::move(name));
#else
  aResult.Assign(gAppData->name);
#endif

  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetID(nsACString& aResult) {
  if (XRE_IsContentProcess()) {
    ContentChild* cc = ContentChild::GetSingleton();
    aResult = cc->GetAppInfo().ID;
    return NS_OK;
  }
  aResult.Assign(gAppData->ID);

  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetVersion(nsACString& aResult) {
  if (XRE_IsContentProcess()) {
    ContentChild* cc = ContentChild::GetSingleton();
    aResult = cc->GetAppInfo().version;
    return NS_OK;
  }
  aResult.Assign(gAppData->version);

  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetPlatformVersion(nsACString& aResult) {
  aResult.Assign(gToolkitVersion);

  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetAppBuildID(nsACString& aResult) {
  if (XRE_IsContentProcess()) {
    ContentChild* cc = ContentChild::GetSingleton();
    aResult = cc->GetAppInfo().buildID;
    return NS_OK;
  }
  aResult.Assign(gAppData->buildID);

  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetPlatformBuildID(nsACString& aResult) {
  aResult.Assign(gToolkitBuildID);

  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetUAName(nsACString& aResult) {
  if (XRE_IsContentProcess()) {
    ContentChild* cc = ContentChild::GetSingleton();
    aResult = cc->GetAppInfo().UAName;
    return NS_OK;
  }
  aResult.Assign(gAppData->UAName);

  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetSourceURL(nsACString& aResult) {
  if (XRE_IsContentProcess()) {
    ContentChild* cc = ContentChild::GetSingleton();
    aResult = cc->GetAppInfo().sourceURL;
    return NS_OK;
  }
  aResult.Assign(gAppData->sourceURL);

  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetUpdateURL(nsACString& aResult) {
  if (XRE_IsContentProcess()) {
    ContentChild* cc = ContentChild::GetSingleton();
    aResult = cc->GetAppInfo().updateURL;
    return NS_OK;
  }
  aResult.Assign(gAppData->updateURL);

  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetLogConsoleErrors(bool* aResult) {
  *aResult = gLogConsoleErrors;
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::SetLogConsoleErrors(bool aValue) {
  gLogConsoleErrors = aValue;
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetInSafeMode(bool* aResult) {
  *aResult = gSafeMode;
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetOS(nsACString& aResult) {
  aResult.AssignLiteral(OS_TARGET);
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetXPCOMABI(nsACString& aResult) {
#ifdef TARGET_XPCOM_ABI
  aResult.AssignLiteral(TARGET_XPCOM_ABI);
  return NS_OK;
#else
  return NS_ERROR_NOT_AVAILABLE;
#endif
}

NS_IMETHODIMP
nsXULAppInfo::GetWidgetToolkit(nsACString& aResult) {
  aResult.AssignLiteral(MOZ_WIDGET_TOOLKIT);
  return NS_OK;
}

// Ensure that the GeckoProcessType enum, defined in xpcom/build/nsXULAppAPI.h,
// is synchronized with the const unsigned longs defined in
// xpcom/system/nsIXULRuntime.idl.
#define GECKO_PROCESS_TYPE(enum_value, enum_name, string_name, proc_typename, \
                           process_bin_type, procinfo_typename,               \
                           webidl_typename, allcaps_name)                     \
  static_assert(nsIXULRuntime::PROCESS_TYPE_##allcaps_name ==                 \
                    static_cast<int>(GeckoProcessType_##enum_name),           \
                "GeckoProcessType in nsXULAppAPI.h not synchronized with "    \
                "nsIXULRuntime.idl");
#include "mozilla/GeckoProcessTypes.h"
#undef GECKO_PROCESS_TYPE

// .. and ensure that that is all of them:
static_assert(GeckoProcessType_Utility + 1 == GeckoProcessType_End,
              "Did not find the final GeckoProcessType");

NS_IMETHODIMP
nsXULAppInfo::GetProcessType(uint32_t* aResult) {
  NS_ENSURE_ARG_POINTER(aResult);
  *aResult = XRE_GetProcessType();
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetProcessID(uint32_t* aResult) {
#ifdef XP_WIN
  *aResult = GetCurrentProcessId();
#else
  *aResult = getpid();
#endif
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetUniqueProcessID(uint64_t* aResult) {
  if (XRE_IsContentProcess()) {
    ContentChild* cc = ContentChild::GetSingleton();
    *aResult = cc->GetID();
  } else {
    *aResult = 0;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetRemoteType(nsACString& aRemoteType) {
  if (XRE_IsContentProcess()) {
    aRemoteType = ContentChild::GetSingleton()->GetRemoteType();
  } else {
    aRemoteType = NOT_REMOTE_TYPE;
  }

  return NS_OK;
}

MOZ_CONSTINIT static nsCString gLastAppVersion;
MOZ_CONSTINIT static nsCString gLastAppBuildID;

NS_IMETHODIMP
nsXULAppInfo::GetLastAppVersion(nsACString& aResult) {
  if (XRE_IsContentProcess()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  if (!gLastAppVersion.IsVoid() && gLastAppVersion.IsEmpty()) {
    NS_WARNING("Attempt to retrieve lastAppVersion before it has been set.");
    return NS_ERROR_NOT_AVAILABLE;
  }

  aResult.Assign(gLastAppVersion);
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetLastAppBuildID(nsACString& aResult) {
  if (XRE_IsContentProcess()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  if (!gLastAppBuildID.IsVoid() && gLastAppBuildID.IsEmpty()) {
    NS_WARNING("Attempt to retrieve lastAppBuildID before it has been set.");
    return NS_ERROR_NOT_AVAILABLE;
  }

  aResult.Assign(gLastAppBuildID);
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetFissionAutostart(bool* aResult) {
  *aResult = FissionAutostart();
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetWin32kExperimentStatus(ExperimentStatus* aResult) {
  if (!XRE_IsParentProcess()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  EnsureWin32kInitialized();
  *aResult = gWin32kExperimentStatus;
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetWin32kLiveStatusTestingOnly(
    nsIXULRuntime::ContentWin32kLockdownState* aResult) {
  if (!XRE_IsParentProcess()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  EnsureWin32kInitialized();
  *aResult = GetLiveWin32kLockdownState();
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetWin32kSessionStatus(
    nsIXULRuntime::ContentWin32kLockdownState* aResult) {
  if (!XRE_IsParentProcess()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  EnsureWin32kInitialized();
  *aResult = gWin32kStatus;
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetFissionDecisionStatus(FissionDecisionStatus* aResult) {
  if (!XRE_IsParentProcess()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  EnsureFissionAutostartInitialized();

  MOZ_ASSERT(gFissionDecisionStatus != eFissionStatusUnknown);
  *aResult = gFissionDecisionStatus;
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetFissionDecisionStatusString(nsACString& aResult) {
  if (!XRE_IsParentProcess()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  EnsureFissionAutostartInitialized();
  switch (gFissionDecisionStatus) {
    case eFissionDisabledByE10sEnv:
      aResult = "disabledByE10sEnv";
      break;
    case eFissionEnabledByEnv:
      aResult = "enabledByEnv";
      break;
    case eFissionDisabledByEnv:
      aResult = "disabledByEnv";
      break;
    case eFissionEnabledByDefault:
      aResult = "enabledByDefault";
      break;
    case eFissionDisabledByDefault:
      aResult = "disabledByDefault";
      break;
    case eFissionEnabledByUserPref:
      aResult = "enabledByUserPref";
      break;
    case eFissionDisabledByUserPref:
      aResult = "disabledByUserPref";
      break;
    case eFissionDisabledByE10sOther:
      aResult = "disabledByE10sOther";
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Unexpected enum value");
  }
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetSessionHistoryInParent(bool* aResult) {
  *aResult = SessionHistoryInParent();
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetSessionStorePlatformCollection(bool* aResult) {
  *aResult = SessionStorePlatformCollection();
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetBrowserTabsRemoteAutostart(bool* aResult) {
  *aResult = BrowserTabsRemoteAutostart();
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetMaxWebProcessCount(uint32_t* aResult) {
  *aResult = mozilla::GetMaxWebProcessCount();
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetAccessibilityEnabled(bool* aResult) {
#ifdef ACCESSIBILITY
  *aResult = GetAccService() != nullptr;
#else
  *aResult = false;
#endif
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetAccessibilityInstantiator(nsAString& aInstantiator) {
#if defined(ACCESSIBILITY) && defined(XP_WIN)
  if (!GetAccService()) {
    aInstantiator.Truncate();
    return NS_OK;
  }
  nsAutoString ipClientInfo;
  a11y::Compatibility::GetHumanReadableConsumersStr(ipClientInfo);
  aInstantiator.Append(ipClientInfo);
  aInstantiator.AppendLiteral("|");

  nsCOMPtr<nsIFile> oopClientExe;
  if (a11y::GetInstantiator(getter_AddRefs(oopClientExe))) {
    nsAutoString oopClientInfo;
    if (NS_SUCCEEDED(oopClientExe->GetPath(oopClientInfo))) {
      aInstantiator.Append(oopClientInfo);
    }
  }
#else
  aInstantiator.Truncate();
#endif
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetIs64Bit(bool* aResult) {
#ifdef HAVE_64BIT_BUILD
  *aResult = true;
#else
  *aResult = false;
#endif
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetIsTextRecognitionSupported(bool* aResult) {
  *aResult = widget::TextRecognition::IsSupported();
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::InvalidateCachesOnRestart() {
  nsCOMPtr<nsIFile> file;
  nsresult rv =
      NS_GetSpecialDirectory(NS_APP_PROFILE_DIR_STARTUP, getter_AddRefs(file));
  if (NS_FAILED(rv)) return rv;
  if (!file) return NS_ERROR_NOT_AVAILABLE;

  file->AppendNative(FILE_COMPATIBILITY_INFO);

  nsINIParser parser;
  rv = parser.Init(file);
  if (NS_FAILED(rv)) {
    // This fails if compatibility.ini is not there, so we'll
    // flush the caches on the next restart anyways.
    return NS_OK;
  }

  nsAutoCString buf;
  rv = parser.GetString("Compatibility", "InvalidateCaches", buf);

  if (NS_FAILED(rv)) {
    PRFileDesc* fd;
    rv = file->OpenNSPRFileDesc(PR_RDWR | PR_APPEND, 0600, &fd);
    if (NS_FAILED(rv)) {
      NS_ERROR("could not create output stream");
      return NS_ERROR_NOT_AVAILABLE;
    }
    static const char kInvalidationHeader[] =
        NS_LINEBREAK "InvalidateCaches=1" NS_LINEBREAK;
    PR_Write(fd, kInvalidationHeader, sizeof(kInvalidationHeader) - 1);
    PR_Close(fd);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetReplacedLockTime(PRTime* aReplacedLockTime) {
  if (!gProfileLock) return NS_ERROR_NOT_AVAILABLE;
  gProfileLock->GetReplacedLockTime(aReplacedLockTime);
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetDefaultUpdateChannel(nsACString& aResult) {
  aResult.AssignLiteral(MOZ_STRINGIFY(MOZ_UPDATE_CHANNEL));
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetDistributionID(nsACString& aResult) {
  aResult.AssignLiteral(MOZ_DISTRIBUTION_ID);
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetWindowsDLLBlocklistStatus(bool* aResult) {
#if defined(HAS_DLL_BLOCKLIST)
  *aResult = DllBlocklist_CheckStatus();
#else
  *aResult = false;
#endif
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetRestartedByOS(bool* aResult) {
  *aResult = gRestartedByOS;
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetChromeColorSchemeIsDark(bool* aResult) {
  *aResult = PreferenceSheet::ColorSchemeForChrome() == ColorScheme::Dark;
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetNativeMenubar(bool* aResult) {
  *aResult = !!LookAndFeel::GetInt(LookAndFeel::IntID::NativeMenubar);
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetContentThemeDerivedColorSchemeIsDark(bool* aResult) {
  *aResult =
      PreferenceSheet::ThemeDerivedColorSchemeForContent() == ColorScheme::Dark;
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetPrefersReducedMotion(bool* aResult) {
  *aResult =
      LookAndFeel::GetInt(LookAndFeel::IntID::PrefersReducedMotion, 0) == 1;
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetDrawInTitlebar(bool* aResult) {
  *aResult = LookAndFeel::DrawInTitlebar();
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetCaretBlinkCount(int32_t* aResult) {
  *aResult = LookAndFeel::CaretBlinkCount();
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetCaretBlinkTime(int32_t* aResult) {
  *aResult = LookAndFeel::CaretBlinkTime();
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetDesktopEnvironment(nsACString& aDesktopEnvironment) {
#ifdef MOZ_WIDGET_GTK
  aDesktopEnvironment.Assign(GetDesktopEnvironmentIdentifier());
#endif
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetIsWayland(bool* aResult) {
#ifdef MOZ_WIDGET_GTK
  *aResult = GdkIsWaylandDisplay();
#else
  *aResult = false;
#endif
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetProcessStartupShortcut(nsAString& aShortcut) {
#if defined(XP_WIN)
  if (XRE_IsParentProcess()) {
    aShortcut.Assign(gProcessStartupShortcut);
    return NS_OK;
  }
#endif
  return NS_ERROR_NOT_AVAILABLE;
}

#if defined(XP_WIN) && defined(MOZ_LAUNCHER_PROCESS)
// Forward declaration
void SetupLauncherProcessPref();

static Maybe<LauncherRegistryInfo::EnabledState> gLauncherProcessState;
#endif  // defined(XP_WIN) && defined(MOZ_LAUNCHER_PROCESS)

NS_IMETHODIMP
nsXULAppInfo::GetLauncherProcessState(uint32_t* aResult) {
#if defined(XP_WIN) && defined(MOZ_LAUNCHER_PROCESS)
  SetupLauncherProcessPref();

  if (!gLauncherProcessState) {
    return NS_ERROR_UNEXPECTED;
  }

  *aResult = static_cast<uint32_t>(gLauncherProcessState.value());
  return NS_OK;
#else
  return NS_ERROR_NOT_AVAILABLE;
#endif
}

#ifdef XP_WIN
NS_IMETHODIMP
nsXULAppInfo::GetUserCanElevate(bool* aUserCanElevate) {
  HANDLE rawToken;
  if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &rawToken)) {
    *aUserCanElevate = false;
    return NS_OK;
  }

  nsAutoHandle token(rawToken);
  LauncherResult<TOKEN_ELEVATION_TYPE> elevationType = GetElevationType(token);
  if (elevationType.isErr()) {
    *aUserCanElevate = false;
    return NS_OK;
  }

  // The possible values returned for elevationType and their meanings are:
  //   TokenElevationTypeDefault: The token does not have a linked token
  //     (e.g. UAC disabled or a standard user, so they can't be elevated)
  //   TokenElevationTypeFull: The token is linked to an elevated token
  //     (e.g. UAC is enabled and the user is already elevated so they can't
  //      be elevated again)
  //   TokenElevationTypeLimited: The token is linked to a limited token
  //     (e.g. UAC is enabled and the user is not elevated, so they can be
  //      elevated)
  *aUserCanElevate = (elevationType.inspect() == TokenElevationTypeLimited);
  return NS_OK;
}
#endif

// Get the CrashReporter::Annotation referred to by the `aKey` string. This
// wraps `CrashReporter::AnnotationFromString` to assert the annotation string
// is valid in debug builds, and otherwise returns an NS_ERROR_INVALID_ARG if
// it is invalid.
static Result<CrashReporter::Annotation, nsresult> GetCrashAnnotation(
    const nsACString& aKey) {
  auto maybeAnnotation = CrashReporter::AnnotationFromString(aKey);
#ifdef DEBUG
  if (!maybeAnnotation.isSome()) {
    std::string warning = "Annotation identifier not found: \"";
    warning += aKey.View();
    warning += "\"";
    NS_WARNING(warning.c_str());
  }
  if (!StaticPrefs::toolkit_crash_annotation_testing_validation()) {
    MOZ_ASSERT(maybeAnnotation.isSome(),
               "Invalid annotation identifier; ensure it exists in "
               "CrashAnnotations.yaml");
  }
#endif
  NS_ENSURE_TRUE(maybeAnnotation.isSome(), Err(NS_ERROR_INVALID_ARG));
  return maybeAnnotation.extract();
}

NS_IMETHODIMP
nsXULAppInfo::GetCrashReporterEnabled(bool* aEnabled) {
  *aEnabled = CrashReporter::GetEnabled();
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::SetEnabled(bool aEnabled) {
  if (aEnabled) {
    if (CrashReporter::GetEnabled()) {
      // no point in erroring for double-enabling
      return NS_OK;
    }

    nsCOMPtr<nsIFile> greBinDir;
    NS_GetSpecialDirectory(NS_GRE_BIN_DIR, getter_AddRefs(greBinDir));
    if (!greBinDir) {
      return NS_ERROR_FAILURE;
    }

    nsCOMPtr<nsIFile> xreBinDirectory = greBinDir;
    if (!xreBinDirectory) {
      return NS_ERROR_FAILURE;
    }

    return CrashReporter::SetExceptionHandler(xreBinDirectory, true);
  }

  if (!CrashReporter::GetEnabled()) {
    // no point in erroring for double-disabling
    return NS_OK;
  }

  return CrashReporter::UnsetExceptionHandler();
}

NS_IMETHODIMP
nsXULAppInfo::GetServerURL(nsIURL** aServerURL) {
  NS_ENSURE_ARG_POINTER(aServerURL);
  if (!CrashReporter::GetEnabled()) return NS_ERROR_NOT_INITIALIZED;

  nsAutoCString data;
  if (!CrashReporter::GetServerURL(data)) {
    return NS_ERROR_FAILURE;
  }
  nsCOMPtr<nsIURI> uri;
  NS_NewURI(getter_AddRefs(uri), data);
  if (!uri) return NS_ERROR_FAILURE;

  nsCOMPtr<nsIURL> url;
  url = do_QueryInterface(uri);
  NS_ADDREF(*aServerURL = url);

  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::SetServerURL(nsIURL* aServerURL) {
  // Only allow https or http URLs
  if (!net::SchemeIsHttpOrHttps(aServerURL)) {
    return NS_ERROR_INVALID_ARG;
  }

  nsAutoCString spec;
  nsresult rv = aServerURL->GetSpec(spec);
  NS_ENSURE_SUCCESS(rv, rv);

  return CrashReporter::SetServerURL(spec);
}

NS_IMETHODIMP
nsXULAppInfo::GetMinidumpPath(nsIFile** aMinidumpPath) {
  if (!CrashReporter::GetEnabled()) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  nsAutoString path;
  if (!CrashReporter::GetMinidumpPath(path)) {
    return NS_ERROR_FAILURE;
  }

  return NS_NewLocalFile(path, aMinidumpPath);
}

NS_IMETHODIMP
nsXULAppInfo::SetMinidumpPath(nsIFile* aMinidumpPath) {
  nsAutoString path;
  nsresult rv = aMinidumpPath->GetPath(path);
  NS_ENSURE_SUCCESS(rv, rv);
  return CrashReporter::SetMinidumpPath(path);
}

NS_IMETHODIMP
nsXULAppInfo::GetMinidumpForID(const nsAString& aId, nsIFile** aMinidump) {
  if (!CrashReporter::GetMinidumpForID(aId, aMinidump)) {
    return NS_ERROR_FILE_NOT_FOUND;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::GetExtraFileForID(const nsAString& aId, nsIFile** aExtraFile) {
  if (!CrashReporter::GetExtraFileForID(aId, aExtraFile)) {
    return NS_ERROR_FILE_NOT_FOUND;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::AnnotateCrashReport(const nsACString& key,
                                  JS::Handle<JS::Value> data, JSContext* cx) {
  CrashReporter::Annotation annotation;
  MOZ_TRY_VAR(annotation, GetCrashAnnotation(key));
  switch (data.type()) {
    case JS::ValueType::Int32:
      CrashReporter::RecordAnnotationU32(annotation, data.toInt32());
      break;
    case JS::ValueType::Boolean:
      CrashReporter::RecordAnnotationBool(annotation, data.toBoolean());
      break;
    case JS::ValueType::String: {
      JSString* value = data.toString();
      size_t length = (JS_GetStringLength(value) * 3) + 1;
      nsCString buffer;
      auto res = JS_EncodeStringToUTF8BufferPartial(
          cx, value, buffer.GetMutableData(length));

      if (!res) {
        return NS_ERROR_OUT_OF_MEMORY;
      }

      size_t written;
      std::tie(std::ignore, written) = *res;

      buffer.SetLength(written);

      CrashReporter::RecordAnnotationNSCString(annotation, buffer);
    } break;
    default:
      return NS_ERROR_INVALID_ARG;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::RemoveCrashReportAnnotation(const nsACString& key) {
  CrashReporter::Annotation annotation;
  MOZ_TRY_VAR(annotation, GetCrashAnnotation(key));
  CrashReporter::UnrecordAnnotation(annotation);
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::IsAnnotationValid(const nsACString& aValue, bool* aIsValid) {
  auto annotation = CrashReporter::AnnotationFromString(aValue);
  *aIsValid = annotation.isSome();
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::IsAnnotationAllowedForPing(const nsACString& aValue,
                                         bool* aIsAllowed) {
  CrashReporter::Annotation annotation;
  MOZ_TRY_VAR(annotation, GetCrashAnnotation(aValue));
  *aIsAllowed = CrashReporter::IsAnnotationAllowedForPing(annotation);
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::IsAnnotationAllowedForReport(const nsACString& aValue,
                                           bool* aIsAllowed) {
  CrashReporter::Annotation annotation;
  MOZ_TRY_VAR(annotation, GetCrashAnnotation(aValue));
  *aIsAllowed = CrashReporter::IsAnnotationAllowedForReport(annotation);
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::AppendAppNotesToCrashReport(const nsACString& data) {
  return CrashReporter::AppendAppNotesToCrashReport(data);
}

NS_IMETHODIMP
nsXULAppInfo::RegisterAppMemory(uint64_t pointer, uint64_t len) {
  return CrashReporter::RegisterAppMemory((void*)pointer, len);
}

NS_IMETHODIMP
nsXULAppInfo::WriteMinidumpForException(void* aExceptionInfo) {
#ifdef XP_WIN
  return CrashReporter::WriteMinidumpForException(
      static_cast<EXCEPTION_POINTERS*>(aExceptionInfo));
#else
  return NS_ERROR_NOT_IMPLEMENTED;
#endif
}

NS_IMETHODIMP
nsXULAppInfo::AppendObjCExceptionInfoToAppNotes(void* aException) {
#ifdef XP_MACOSX
  return CrashReporter::AppendObjCExceptionInfoToAppNotes(aException);
#else
  return NS_ERROR_NOT_IMPLEMENTED;
#endif
}

NS_IMETHODIMP
nsXULAppInfo::GetSubmitReports(bool* aEnabled) {
  return CrashReporter::GetSubmitReports(aEnabled);
}

NS_IMETHODIMP
nsXULAppInfo::SetSubmitReports(bool aEnabled) {
  return CrashReporter::SetSubmitReports(aEnabled);
}

NS_IMETHODIMP
nsXULAppInfo::UpdateCrashEventsDir() {
  CrashReporter::UpdateCrashEventsDir();
  return NS_OK;
}

NS_IMETHODIMP
nsXULAppInfo::SaveMemoryReport() {
  if (!CrashReporter::GetEnabled()) {
    return NS_ERROR_NOT_INITIALIZED;
  }
  nsCOMPtr<nsIFile> file;
  nsresult rv = CrashReporter::GetDefaultMemoryReportFile(getter_AddRefs(file));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  nsString path;
  file->GetPath(path);

  nsCOMPtr<nsIMemoryInfoDumper> dumper =
      do_GetService("@mozilla.org/memory-info-dumper;1");
  if (NS_WARN_IF(!dumper)) {
    return NS_ERROR_UNEXPECTED;
  }

  rv = dumper->DumpMemoryReportsToNamedFile(
      path, this, file, true /* anonymize */, false /* minimizeMemoryUsage */);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  return NS_OK;
}

// This method is from nsIFInishDumpingCallback.
NS_IMETHODIMP
nsXULAppInfo::Callback(nsISupports* aData) {
  nsCOMPtr<nsIFile> file = do_QueryInterface(aData);
  MOZ_ASSERT(file);

  CrashReporter::SetMemoryReportFile(file);
  return NS_OK;
}

static const nsXULAppInfo kAppInfo;
namespace mozilla {
nsresult AppInfoConstructor(REFNSIID aIID, void** aResult) {
  return const_cast<nsXULAppInfo*>(&kAppInfo)->QueryInterface(aIID, aResult);
}
}  // namespace mozilla

bool gLogConsoleErrors = false;

#define NS_ENSURE_TRUE_LOG(x, ret)               \
  PR_BEGIN_MACRO                                 \
  if (MOZ_UNLIKELY(!(x))) {                      \
    NS_WARNING("NS_ENSURE_TRUE(" #x ") failed"); \
    gLogConsoleErrors = true;                    \
    return ret;                                  \
  }                                              \
  PR_END_MACRO

#define NS_ENSURE_SUCCESS_LOG(res, ret) \
  NS_ENSURE_TRUE_LOG(NS_SUCCEEDED(res), ret)

/**
 * Because we're starting/stopping XPCOM several times in different scenarios,
 * this class is a stack-based critter that makes sure that XPCOM is shut down
 * during early returns.
 */

class ScopedXPCOMStartup {
 public:
  ScopedXPCOMStartup() : mServiceManager(nullptr) {}
  ~ScopedXPCOMStartup();

  nsresult Initialize(bool aInitJSContext = true);
  nsresult SetWindowCreator(nsINativeAppSupport* native);

 private:
  nsIServiceManager* mServiceManager;
  static nsINativeAppSupport* gNativeAppSupport;

  friend already_AddRefed<nsINativeAppSupport> NS_GetNativeAppSupport();
};

ScopedXPCOMStartup::~ScopedXPCOMStartup() {
  NS_IF_RELEASE(gNativeAppSupport);

  if (mServiceManager) {
#ifdef XP_MACOSX
    // On OS X, we need a pool to catch cocoa objects that are autoreleased
    // during teardown.
    mozilla::MacAutoreleasePool pool;
#endif

    nsCOMPtr<nsIAppStartup> appStartup(components::AppStartup::Service());
    if (appStartup) appStartup->DestroyHiddenWindow();

    gDirServiceProvider->DoShutdown();
    PROFILER_MARKER_UNTYPED("Shutdown early", OTHER);

    WriteConsoleLog();

    NS_ShutdownXPCOM(mServiceManager);
    mServiceManager = nullptr;
  }
}

nsresult ScopedXPCOMStartup::Initialize(bool aInitJSContext) {
  NS_ASSERTION(gDirServiceProvider, "Should not get here!");

  nsresult rv;

  rv = NS_InitXPCOM(&mServiceManager, gDirServiceProvider->GetAppDir(),
                    gDirServiceProvider, aInitJSContext);
  if (NS_FAILED(rv)) {
    NS_ERROR("Couldn't start xpcom!");
    mServiceManager = nullptr;
  } else {
#ifdef DEBUG
    nsCOMPtr<nsIComponentRegistrar> reg = do_QueryInterface(mServiceManager);
    NS_ASSERTION(reg, "Service Manager doesn't QI to Registrar.");
#endif
  }

  return rv;
}

/**
 * This is a little factory class that serves as a singleton-service-factory
 * for the nativeappsupport object.
 */
class nsSingletonFactory final : public nsIFactory {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIFACTORY

  explicit nsSingletonFactory(nsISupports* aSingleton);

 private:
  ~nsSingletonFactory() = default;
  nsCOMPtr<nsISupports> mSingleton;
};

nsSingletonFactory::nsSingletonFactory(nsISupports* aSingleton)
    : mSingleton(aSingleton) {
  NS_ASSERTION(mSingleton, "Singleton was null!");
}

NS_IMPL_ISUPPORTS(nsSingletonFactory, nsIFactory)

NS_IMETHODIMP
nsSingletonFactory::CreateInstance(const nsIID& aIID, void** aResult) {
  return mSingleton->QueryInterface(aIID, aResult);
}

/**
 * Set our windowcreator on the WindowWatcher service.
 */
nsresult ScopedXPCOMStartup::SetWindowCreator(nsINativeAppSupport* native) {
  nsresult rv;

  NS_IF_ADDREF(gNativeAppSupport = native);

  nsCOMPtr<nsIWindowCreator> creator(components::AppStartup::Service());
  if (!creator) return NS_ERROR_UNEXPECTED;

  nsCOMPtr<nsIWindowWatcher> wwatch(
      do_GetService(NS_WINDOWWATCHER_CONTRACTID, &rv));
  NS_ENSURE_SUCCESS(rv, rv);

  return wwatch->SetWindowCreator(creator);
}

/* static */ already_AddRefed<nsINativeAppSupport> NS_GetNativeAppSupport() {
  if (!ScopedXPCOMStartup::gNativeAppSupport) {
    return nullptr;
  }

  return do_AddRef(ScopedXPCOMStartup::gNativeAppSupport);
}

#ifdef MOZ_HAS_REMOTE
/* static */ already_AddRefed<nsIRemoteService> GetRemoteService() {
  AssertIsOnMainThread();

  if (!gRemoteService) {
    gRemoteService = new nsRemoteService();
  }
  nsCOMPtr<nsIRemoteService> remoteService = gRemoteService.get();
  return remoteService.forget();
}
#endif

nsINativeAppSupport* ScopedXPCOMStartup::gNativeAppSupport;

static void DumpArbitraryHelp() {
  nsresult rv;

  ScopedLogging log;

  {
    ScopedXPCOMStartup xpcom;
    xpcom.Initialize();

    nsCOMPtr<nsICommandLineRunner> cmdline(new nsCommandLine());

    nsCString text;
    rv = cmdline->GetHelpText(text);
    if (NS_SUCCEEDED(rv)) printf("%s", text.get());
  }
}

// English text needs to go into a dtd file.
// But when this is called we have no components etc. These strings must either
// be here, or in a native resource file.
static void DumpHelp() {
  printf(
      "Usage: %s [ options ... ] [URL]\n"
      "       where options include:\n\n",
      gArgv[0]);

#ifdef MOZ_X11
  printf(
      "X11 options\n"
      "  --display=DISPLAY  X display to use\n"
      "  --sync             Make X calls synchronous\n");
#endif
#ifdef XP_UNIX
  printf(
      "  --g-fatal-warnings Make all warnings fatal\n"
      "\n%s options\n",
      (const char*)gAppData->name);
#endif

  printf(
      "  -h or --help       Print this message.\n"
      "  -v or --version    Print %s version.\n"
      "  --full-version     Print %s version, build and platform build ids.\n"
      "  -P <profile>       Start with <profile>.\n"
      "  --profile <path>   Start with profile at <path>.\n"
      "  --migration        Start with migration wizard.\n"
      "  --ProfileManager   Start with ProfileManager.\n"
      "  --origin-to-force-quic-on <origin>\n"
      "                     Force to use QUIC for the specified origin.\n"
#ifdef MOZ_HAS_REMOTE
      "  --new-instance     Open new instance, not a new window in running "
      "instance.\n"
#endif
      "  --safe-mode        Disables extensions and themes for this session.\n"
#ifdef MOZ_BLOCK_PROFILE_DOWNGRADE
      "  --allow-downgrade  Allows downgrading a profile.\n"
#endif
      "  --MOZ_LOG=<modules> Treated as MOZ_LOG=<modules> environment "
      "variable,\n"
      "                     overrides it.\n"
      "  --MOZ_LOG_FILE=<file> Treated as MOZ_LOG_FILE=<file> environment "
      "variable,\n"
      "                     overrides it. If MOZ_LOG_FILE is not specified as "
      "an\n"
      "                     argument or as an environment variable, logging "
      "will be\n"
      "                     written to stdout.\n",
      (const char*)gAppData->name, (const char*)gAppData->name);

#if defined(XP_WIN)
  printf("  --console          Start %s with a debugging console.\n",
         (const char*)gAppData->name);
#endif

#if defined(XP_WIN) || defined(MOZ_WIDGET_GTK) || defined(XP_MACOSX)
  printf("  --headless         Run without a GUI.\n");
#endif

#if defined(MOZ_ENABLE_DBUS)
  printf(
      "  --dbus-service <launcher>  Run as DBus service for "
      "org.freedesktop.Application and\n"
      "                             set a launcher (usually /usr/bin/appname "
      "script) for it.\n");
#endif

  // this works, but only after the components have registered.  so if you drop
  // in a new command line handler, --help won't not until the second run. out
  // of the bug, because we ship a component.reg file, it works correctly.
  DumpArbitraryHelp();
}

static inline void DumpVersion() {
  if (gAppData->vendor && *gAppData->vendor) {
    printf("%s ", (const char*)gAppData->vendor);
  }
  printf("%s ", (const char*)gAppData->name);

  // Use the displayed version
  // For example, for beta, we would display 42.0b2 instead of 42.0
  printf("%s", MOZ_STRINGIFY(MOZ_APP_VERSION_DISPLAY));

  if (gAppData->copyright && *gAppData->copyright) {
    printf(", %s", (const char*)gAppData->copyright);
  }
  printf("\n");
}

static inline void DumpFullVersion() {
  if (gAppData->vendor && *gAppData->vendor) {
    printf("%s ", (const char*)gAppData->vendor);
  }
  printf("%s ", (const char*)gAppData->name);

  // Use the displayed version
  // For example, for beta, we would display 42.0b2 instead of 42.0
  printf("%s ", MOZ_STRINGIFY(MOZ_APP_VERSION_DISPLAY));

  printf("%s ", (const char*)gAppData->buildID);
  printf("%s ", (const char*)PlatformBuildID());
  if (gAppData->copyright && *gAppData->copyright) {
    printf(", %s", (const char*)gAppData->copyright);
  }
  printf("\n");
}

void XRE_InitOmnijar(nsIFile* greOmni, nsIFile* appOmni) {
  mozilla::Omnijar::Init(greOmni, appOmni);
}

nsresult XRE_GetBinaryPath(nsIFile** aResult) {
  return mozilla::BinaryPath::GetFile(aResult);
}

#ifdef XP_WIN
#  include "nsWindowsRestart.cpp"
#  include <shellapi.h>

typedef BOOL(WINAPI* SetProcessDEPPolicyFunc)(DWORD dwFlags);

static void RegisterApplicationRestartChanged(const char* aPref, void* aData) {
  DWORD cchCmdLine = 0;
  HRESULT rc = ::GetApplicationRestartSettings(::GetCurrentProcess(), nullptr,
                                               &cchCmdLine, nullptr);
  bool wasRegistered = false;
  if (rc == S_OK) {
    wasRegistered = true;
  }

  if (Preferences::GetBool(PREF_WIN_REGISTER_APPLICATION_RESTART, false) &&
      !wasRegistered) {
    // Make the command line to use when restarting.
    // Excludes argv[0] because RegisterApplicationRestart adds the
    // executable name, replace that temporarily with -os-restarted
    char* exeName = gRestartArgv[0];
    gRestartArgv[0] = const_cast<char*>("-os-restarted");
    wchar_t** restartArgvConverted =
        AllocConvertUTF8toUTF16Strings(gRestartArgc, gRestartArgv);
    gRestartArgv[0] = exeName;

    mozilla::UniquePtr<wchar_t[]> restartCommandLine;
    if (restartArgvConverted) {
      restartCommandLine =
          mozilla::MakeCommandLine(gRestartArgc, restartArgvConverted);
      FreeAllocStrings(gRestartArgc, restartArgvConverted);
    }

    if (restartCommandLine) {
      // Flags RESTART_NO_PATCH and RESTART_NO_REBOOT are not set, so we
      // should be restarted if terminated by an update or restart.
      ::RegisterApplicationRestart(restartCommandLine.get(),
                                   RESTART_NO_CRASH | RESTART_NO_HANG);
    }
  } else if (wasRegistered) {
    ::UnregisterApplicationRestart();
  }
}

static void OnAlteredPrefetchPrefChanged(const char* aPref, void* aData) {
  int32_t prefVal = Preferences::GetInt(PREF_WIN_ALTERED_DLL_PREFETCH, 0);

  mozilla::DllPrefetchExperimentRegistryInfo prefetchRegInfo;
  mozilla::DebugOnly<mozilla::Result<Ok, nsresult>> reflectResult =
      prefetchRegInfo.ReflectPrefToRegistry(prefVal);

  MOZ_ASSERT(reflectResult.value.isOk());
}

static void SetupAlteredPrefetchPref() {
  mozilla::DllPrefetchExperimentRegistryInfo prefetchRegInfo;

  mozilla::DebugOnly<mozilla::Result<Ok, nsresult>> reflectResult =
      prefetchRegInfo.ReflectPrefToRegistry(
          Preferences::GetInt(PREF_WIN_ALTERED_DLL_PREFETCH, 0));
  MOZ_ASSERT(reflectResult.value.isOk());

  Preferences::RegisterCallback(&OnAlteredPrefetchPrefChanged,
                                PREF_WIN_ALTERED_DLL_PREFETCH);
}

static void ReflectSkeletonUIPrefToRegistry(const char* aPref, void* aData) {
  Unused << aPref;
  Unused << aData;

  bool shouldBeEnabled =
      Preferences::GetBool(kPrefPreXulSkeletonUI, false) &&
      Preferences::GetBool(kPrefBrowserStartupBlankWindow, false) &&
      LookAndFeel::DrawInTitlebar();
  if (shouldBeEnabled && Preferences::HasUserValue(kPrefThemeId)) {
    nsCString themeId;
    Preferences::GetCString(kPrefThemeId, themeId);
    if (themeId.EqualsLiteral("default-theme@mozilla.org")) {
      Unused << SetPreXULSkeletonUIThemeId(ThemeMode::Default);
    } else if (themeId.EqualsLiteral("firefox-compact-dark@mozilla.org")) {
      Unused << SetPreXULSkeletonUIThemeId(ThemeMode::Dark);
    } else if (themeId.EqualsLiteral("firefox-compact-light@mozilla.org")) {
      Unused << SetPreXULSkeletonUIThemeId(ThemeMode::Light);
    } else {
      shouldBeEnabled = false;
    }
  } else if (shouldBeEnabled) {
    Unused << SetPreXULSkeletonUIThemeId(ThemeMode::Default);
  }

  if (GetPreXULSkeletonUIEnabled() != shouldBeEnabled) {
    Unused << SetPreXULSkeletonUIEnabledIfAllowed(shouldBeEnabled);
  }
}

static void SetupSkeletonUIPrefs() {
  ReflectSkeletonUIPrefToRegistry(nullptr, nullptr);
  Preferences::RegisterCallback(&ReflectSkeletonUIPrefToRegistry,
                                kPrefPreXulSkeletonUI);
  Preferences::RegisterCallback(&ReflectSkeletonUIPrefToRegistry,
                                kPrefBrowserStartupBlankWindow);
  Preferences::RegisterCallback(&ReflectSkeletonUIPrefToRegistry, kPrefThemeId);
  Preferences::RegisterCallback(
      &ReflectSkeletonUIPrefToRegistry,
      nsDependentCString(StaticPrefs::GetPrefName_browser_tabs_inTitlebar()));
}

#  if defined(MOZ_LAUNCHER_PROCESS)

static void OnLauncherPrefChanged(const char* aPref, void* aData) {
  bool prefVal = Preferences::GetBool(PREF_WIN_LAUNCHER_PROCESS_ENABLED, true);

  mozilla::LauncherRegistryInfo launcherRegInfo;
  mozilla::DebugOnly<mozilla::LauncherVoidResult> reflectResult =
      launcherRegInfo.ReflectPrefToRegistry(prefVal);
  MOZ_ASSERT(reflectResult.inspect().isOk());
}

static void OnLauncherTelemetryPrefChanged(const char* aPref, void* aData) {
  bool prefVal = Preferences::GetBool(kPrefHealthReportUploadEnabled, true);

  mozilla::LauncherRegistryInfo launcherRegInfo;
  mozilla::DebugOnly<mozilla::LauncherVoidResult> reflectResult =
      launcherRegInfo.ReflectTelemetryPrefToRegistry(prefVal);
  MOZ_ASSERT(reflectResult.inspect().isOk());
}

static void SetupLauncherProcessPref() {
  if (gLauncherProcessState) {
    // We've already successfully run
    return;
  }

  mozilla::LauncherRegistryInfo launcherRegInfo;

  mozilla::LauncherResult<mozilla::LauncherRegistryInfo::EnabledState>
      enabledState = launcherRegInfo.IsEnabled();

  if (enabledState.isOk()) {
    gLauncherProcessState = Some(enabledState.unwrap());

    CrashReporter::RecordAnnotationU32(
        CrashReporter::Annotation::LauncherProcessState,
        static_cast<uint32_t>(enabledState.unwrap()));

    // Reflect the launcher process registry state into user prefs
    Preferences::SetBool(
        PREF_WIN_LAUNCHER_PROCESS_ENABLED,
        enabledState.unwrap() !=
            mozilla::LauncherRegistryInfo::EnabledState::ForceDisabled);
  }

  mozilla::DebugOnly<mozilla::LauncherVoidResult> reflectResult =
      launcherRegInfo.ReflectTelemetryPrefToRegistry(
          Preferences::GetBool(kPrefHealthReportUploadEnabled, true));
  MOZ_ASSERT(reflectResult.inspect().isOk());

  Preferences::RegisterCallback(&OnLauncherPrefChanged,
                                PREF_WIN_LAUNCHER_PROCESS_ENABLED);
  Preferences::RegisterCallback(&OnLauncherTelemetryPrefChanged,
                                kPrefHealthReportUploadEnabled);
}

#  endif  // defined(MOZ_LAUNCHER_PROCESS)

#  if defined(MOZ_DEFAULT_BROWSER_AGENT)

#    define DEFAULT_BROWSER_AGENT_KEY_NAME \
      "SOFTWARE\\" MOZ_APP_VENDOR "\\" MOZ_APP_NAME "\\Default Browser Agent"

static nsresult PrependRegistryValueName(nsAutoString& aValueName) {
  nsresult rv;

  nsCOMPtr<nsIFile> binaryPath;
  rv = XRE_GetBinaryPath(getter_AddRefs(binaryPath));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIFile> binaryDir;
  rv = binaryPath->GetParent(getter_AddRefs(binaryDir));
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoString prefix;
  rv = binaryDir->GetPath(prefix);
  NS_ENSURE_SUCCESS(rv, rv);

  prefix.AppendLiteral("|");
  aValueName.Insert(prefix, 0);

  return NS_OK;
}

static void OnDefaultAgentTelemetryPrefChanged(const char* aPref, void* aData) {
  nsresult rv;
  nsAutoString valueName;
  if (strcmp(aPref, kPrefHealthReportUploadEnabled) == 0) {
    valueName.AssignLiteral("DisableTelemetry");
  } else if (strcmp(aPref, kPrefDefaultAgentEnabled) == 0) {
    valueName.AssignLiteral("DisableDefaultBrowserAgent");
  } else {
    return;
  }
  rv = PrependRegistryValueName(valueName);
  NS_ENSURE_SUCCESS_VOID(rv);

  nsCOMPtr<nsIWindowsRegKey> regKey =
      do_CreateInstance("@mozilla.org/windows-registry-key;1", &rv);
  NS_ENSURE_SUCCESS_VOID(rv);

  nsAutoString keyName;
  keyName.AppendLiteral(DEFAULT_BROWSER_AGENT_KEY_NAME);
  rv = regKey->Create(nsIWindowsRegKey::ROOT_KEY_CURRENT_USER, keyName,
                      nsIWindowsRegKey::ACCESS_WRITE);

  bool prefVal = Preferences::GetBool(aPref, true);

  // We're recording whether the pref is *disabled*, so invert the value.
  rv = regKey->WriteIntValue(valueName, prefVal ? 0 : 1);
  NS_ENSURE_SUCCESS_VOID(rv);
}

static void OnSetDefaultBrowserUserChoicePrefChanged(const char* aPref,
                                                     void* aData) {
  nsresult rv;
  if (strcmp(aPref, kPrefSetDefaultBrowserUserChoicePref) != 0) {
    return;
  }
  nsAutoString valueName;
  valueName.AssignLiteral("SetDefaultBrowserUserChoice");
  rv = PrependRegistryValueName(valueName);
  NS_ENSURE_SUCCESS_VOID(rv);

  nsCOMPtr<nsIWindowsRegKey> regKey =
      do_CreateInstance("@mozilla.org/windows-registry-key;1", &rv);
  NS_ENSURE_SUCCESS_VOID(rv);

  nsAutoString keyName;
  keyName.AppendLiteral(DEFAULT_BROWSER_AGENT_KEY_NAME);
  rv = regKey->Create(nsIWindowsRegKey::ROOT_KEY_CURRENT_USER, keyName,
                      nsIWindowsRegKey::ACCESS_WRITE);

  bool prefVal = Preferences::GetBool(aPref, true);

  rv = regKey->WriteIntValue(valueName, prefVal);
  NS_ENSURE_SUCCESS_VOID(rv);
}

static void OnDefaultAgentRemoteSettingsPrefChanged(const char* aPref,
                                                    void* aData) {
  nsresult rv;
  nsAutoString valueName;
  if (strcmp(aPref, kPrefServicesSettingsServer) == 0) {
    valueName.AssignLiteral("ServicesSettingsServer");
  } else {
    return;
  }
  rv = PrependRegistryValueName(valueName);
  NS_ENSURE_SUCCESS_VOID(rv);

  nsCOMPtr<nsIWindowsRegKey> regKey =
      do_CreateInstance("@mozilla.org/windows-registry-key;1", &rv);
  NS_ENSURE_SUCCESS_VOID(rv);

  nsAutoString keyName;
  keyName.AppendLiteral(DEFAULT_BROWSER_AGENT_KEY_NAME);
  rv = regKey->Create(nsIWindowsRegKey::ROOT_KEY_CURRENT_USER, keyName,
                      nsIWindowsRegKey::ACCESS_WRITE);

  NS_ENSURE_SUCCESS_VOID(rv);

  nsAutoString prefVal;
  rv = Preferences::GetString(aPref, prefVal);
  if (NS_FAILED(rv)) {
    return;
  }

  if (prefVal.IsEmpty()) {
    rv = regKey->RemoveValue(valueName);
  } else {
    rv = regKey->WriteStringValue(valueName, prefVal);
  }
  NS_ENSURE_SUCCESS_VOID(rv);
}

static void SetDefaultAgentLastRunTime() {
  nsresult rv;
  nsAutoString valueName;
  valueName.AppendLiteral("AppLastRunTime");
  rv = PrependRegistryValueName(valueName);
  NS_ENSURE_SUCCESS_VOID(rv);

  nsCOMPtr<nsIWindowsRegKey> regKey =
      do_CreateInstance("@mozilla.org/windows-registry-key;1", &rv);
  NS_ENSURE_SUCCESS_VOID(rv);

  nsAutoString keyName;
  keyName.AppendLiteral(DEFAULT_BROWSER_AGENT_KEY_NAME);
  rv = regKey->Create(nsIWindowsRegKey::ROOT_KEY_CURRENT_USER, keyName,
                      nsIWindowsRegKey::ACCESS_WRITE);
  NS_ENSURE_SUCCESS_VOID(rv);

  FILETIME fileTime;
  GetSystemTimeAsFileTime(&fileTime);

  ULARGE_INTEGER integerTime;
  integerTime.u.LowPart = fileTime.dwLowDateTime;
  integerTime.u.HighPart = fileTime.dwHighDateTime;

  rv = regKey->WriteInt64Value(valueName, integerTime.QuadPart);
  NS_ENSURE_SUCCESS_VOID(rv);
}

#  endif  // defined(MOZ_DEFAULT_BROWSER_AGENT)

#endif  // XP_WIN

void UnlockProfile() {
  if (gProfileLock) {
    gProfileLock->Unlock();
  }
}

nsresult LaunchChild(bool aBlankCommandLine, bool aTryExec) {
  // Restart this process by exec'ing it into the current process
  // if supported by the platform.  Otherwise, use NSPR.

  if (aBlankCommandLine) {
    gRestartArgc = 1;
    gRestartArgv[gRestartArgc] = nullptr;
  }

  SaveToEnv("MOZ_LAUNCHED_CHILD=1");
#if defined(MOZ_LAUNCHER_PROCESS)
  SaveToEnv("MOZ_LAUNCHER_PROCESS=1");
#endif  // defined(MOZ_LAUNCHER_PROCESS)

#if !defined(MOZ_WIDGET_ANDROID)  // Android has separate restart code.
#  if defined(XP_MACOSX)
  InitializeMacApp();
  CommandLineServiceMac::SetupMacCommandLine(gRestartArgc, gRestartArgv, true);
  LaunchMacApp(gRestartArgc, gRestartArgv);
#  else
  nsCOMPtr<nsIFile> lf;
  nsresult rv = XRE_GetBinaryPath(getter_AddRefs(lf));
  if (NS_FAILED(rv)) return rv;

#    if defined(XP_WIN)
  nsAutoString exePath;
  rv = lf->GetPath(exePath);
  if (NS_FAILED(rv)) return rv;

  HANDLE hProcess;
  if (!WinLaunchChild(exePath.get(), gRestartArgc, gRestartArgv, nullptr,
                      &hProcess))
    return NS_ERROR_FAILURE;
  // Keep the current process around until the restarted process has created
  // its message queue, to avoid the launched process's windows being forced
  // into the background.
  mozilla::WaitForInputIdle(hProcess);
  ::CloseHandle(hProcess);

#    else
  nsAutoCString exePath;
  rv = lf->GetNativePath(exePath);
  if (NS_FAILED(rv)) return rv;

#      if defined(XP_UNIX)
  if (aTryExec) {
    execv(exePath.get(), gRestartArgv);

    // If execv returns we know it's because it failed.
    return NS_ERROR_FAILURE;
  }
#      endif
  if (PR_CreateProcessDetached(exePath.get(), gRestartArgv, nullptr, nullptr) ==
      PR_FAILURE) {
    return NS_ERROR_FAILURE;
  }

  // Note that we don't know if the child process starts okay, if it
  // immediately returns non-zero then we may mask that by returning a zero
  // exit status.

#    endif  // WP_WIN
#  endif    // WP_MACOSX
#endif      // MOZ_WIDGET_ANDROID

  return NS_ERROR_LAUNCHED_CHILD_PROCESS;
}

static const char kProfileProperties[] =
    "chrome://mozapps/locale/profile/profileSelection.properties";

namespace {

/**
 * This class, instead of a raw nsresult, should be the return type of any
 * function called by SelectProfile that initializes XPCOM.
 */
class ReturnAbortOnError {
 public:
  MOZ_IMPLICIT ReturnAbortOnError(nsresult aRv) { mRv = ConvertRv(aRv); }

  operator nsresult() { return mRv; }

 private:
  inline nsresult ConvertRv(nsresult aRv) {
    if (NS_SUCCEEDED(aRv) || aRv == NS_ERROR_LAUNCHED_CHILD_PROCESS) {
      return aRv;
    }
#ifdef MOZ_BACKGROUNDTASKS
    // A background task that fails to lock its profile will return
    // NS_ERROR_UNEXPECTED and this will allow the task to exit with a
    // non-zero exit code.
    if (aRv == NS_ERROR_UNEXPECTED && BackgroundTasks::IsBackgroundTaskMode()) {
      return aRv;
    }
#endif
    return NS_ERROR_ABORT;
  }

  nsresult mRv;
};

}  // namespace

static nsresult ProfileMissingDialog(nsINativeAppSupport* aNative) {
#ifdef MOZ_WIDGET_ANDROID
  // We cannot really do anything this early during initialization, so we just
  // return as this is likely the effect of misconfiguration on the test side.
  // Non-test code paths cannot set the profile folder, which is always the
  // default one.
  Output(true, "Could not find profile folder.\n");
  return NS_ERROR_ABORT;
#else
#  ifdef MOZ_BACKGROUNDTASKS
  if (BackgroundTasks::IsBackgroundTaskMode()) {
    // We should never get to this point in background task mode.
    printf_stderr(
        "Could not determine any profile running in backgroundtask mode!\n");
    return NS_ERROR_ABORT;
  }
#  endif  // MOZ_BACKGROUNDTASKS

  nsresult rv;

  ScopedXPCOMStartup xpcom;
  rv = xpcom.Initialize();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = xpcom.SetWindowCreator(aNative);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);

#  ifdef XP_MACOSX
  InitializeMacApp();
#  endif

  {  // extra scoping is needed so we release these components before xpcom
     // shutdown
    nsCOMPtr<nsIStringBundleService> sbs =
        mozilla::components::StringBundle::Service();
    NS_ENSURE_TRUE(sbs, NS_ERROR_FAILURE);

    nsCOMPtr<nsIStringBundle> sb;
    sbs->CreateBundle(kProfileProperties, getter_AddRefs(sb));
    NS_ENSURE_TRUE_LOG(sbs, NS_ERROR_FAILURE);

    NS_ConvertUTF8toUTF16 appName(gAppData->name);
    AutoTArray<nsString, 2> params = {appName, appName};

    // profileMissing
    nsAutoString missingMessage;
    rv = sb->FormatStringFromName("profileMissing", params, missingMessage);
    NS_ENSURE_SUCCESS(rv, NS_ERROR_ABORT);

    nsAutoString missingTitle;
    params.SetLength(1);
    rv = sb->FormatStringFromName("profileMissingTitle", params, missingTitle);
    NS_ENSURE_SUCCESS(rv, NS_ERROR_ABORT);

    nsCOMPtr<nsIPromptService> ps(do_GetService(NS_PROMPTSERVICE_CONTRACTID));
    NS_ENSURE_TRUE(ps, NS_ERROR_FAILURE);

    ps->Alert(nullptr, missingTitle.get(), missingMessage.get());

    return NS_ERROR_ABORT;
  }
#endif  // MOZ_WIDGET_ANDROID
}

static ReturnAbortOnError ProfileLockedDialog(nsIFile* aProfileDir,
                                              nsIFile* aProfileLocalDir,
                                              nsIProfileUnlocker* aUnlocker,
                                              nsINativeAppSupport* aNative,
                                              nsIProfileLock** aResult) {
  nsresult rv;

  // We aren't going to start this instance so we can unblock other instances
  // from starting up.
#if defined(MOZ_HAS_REMOTE)
  gStartupLock = nullptr;
#endif

  bool exists;
  aProfileDir->Exists(&exists);
  if (!exists) {
    return ProfileMissingDialog(aNative);
  }

  ScopedXPCOMStartup xpcom;
  rv = xpcom.Initialize();
  NS_ENSURE_SUCCESS(rv, rv);

#if defined(MOZ_TELEMETRY_REPORTING)
  // We cannot check if telemetry has been disabled by the user, yet.
  // So, rely on the build time settings, instead.
  mozilla::Telemetry::WriteFailedProfileLock(aProfileDir);
#endif

  rv = xpcom.SetWindowCreator(aNative);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);

#ifdef XP_MACOSX
  InitializeMacApp();
#endif

  {  // extra scoping is needed so we release these components before xpcom
     // shutdown
    nsCOMPtr<nsIStringBundleService> sbs =
        mozilla::components::StringBundle::Service();
    NS_ENSURE_TRUE(sbs, NS_ERROR_FAILURE);

    nsCOMPtr<nsIStringBundle> sb;
    sbs->CreateBundle(kProfileProperties, getter_AddRefs(sb));
    NS_ENSURE_TRUE_LOG(sbs, NS_ERROR_FAILURE);

    NS_ConvertUTF8toUTF16 appName(gAppData->name);
    AutoTArray<nsString, 3> params = {appName, appName, appName};

    nsAutoString killMessage;
#ifndef XP_MACOSX
    rv = sb->FormatStringFromName(
        aUnlocker ? "restartMessageUnlocker" : "restartMessageNoUnlocker2",
        params, killMessage);
#else
    rv = sb->FormatStringFromName(
        aUnlocker ? "restartMessageUnlockerMac" : "restartMessageNoUnlockerMac",
        params, killMessage);
#endif
    NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);

    params.SetLength(1);
    nsAutoString killTitle;
    rv = sb->FormatStringFromName("restartTitle", params, killTitle);
    NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);

#ifdef MOZ_BACKGROUNDTASKS
    if (BackgroundTasks::IsBackgroundTaskMode()) {
      // This error is handled specially to exit with a non-zero exit code.
      printf_stderr("%s\n", NS_LossyConvertUTF16toASCII(killMessage).get());
      return NS_ERROR_UNEXPECTED;
    }
#endif

    if (gfxPlatform::IsHeadless()) {
      // TODO: make a way to turn off all dialogs when headless.
      Output(true, "%s\n", NS_LossyConvertUTF16toASCII(killMessage).get());
      return NS_ERROR_FAILURE;
    }

    nsCOMPtr<nsIPromptService> ps(do_GetService(NS_PROMPTSERVICE_CONTRACTID));
    NS_ENSURE_TRUE(ps, NS_ERROR_FAILURE);

    if (aUnlocker) {
      int32_t button;
#ifdef MOZ_WIDGET_ANDROID
      // On Android we always kill the process if the lock is still being held
      button = 0;
#else
      const uint32_t flags = (nsIPromptService::BUTTON_TITLE_IS_STRING *
                              nsIPromptService::BUTTON_POS_0) +
                             (nsIPromptService::BUTTON_TITLE_CANCEL *
                              nsIPromptService::BUTTON_POS_1);

      bool checkState = false;
      rv = ps->ConfirmEx(nullptr, killTitle.get(), killMessage.get(), flags,
                         killTitle.get(), nullptr, nullptr, nullptr,
                         &checkState, &button);
      NS_ENSURE_SUCCESS_LOG(rv, rv);
#endif

      if (button == 0) {
        rv = aUnlocker->Unlock(nsIProfileUnlocker::FORCE_QUIT);
        if (NS_FAILED(rv)) {
          return rv;
        }

        SaveFileToEnv("XRE_PROFILE_PATH", aProfileDir);
        SaveFileToEnv("XRE_PROFILE_LOCAL_PATH", aProfileLocalDir);

        return LaunchChild(false, true);
      }
    } else {
      rv = ps->Alert(nullptr, killTitle.get(), killMessage.get());
      NS_ENSURE_SUCCESS_LOG(rv, rv);
    }

    return NS_ERROR_ABORT;
  }
}

static ReturnAbortOnError ShowProfileDialog(
    nsIToolkitProfileService* aProfileSvc, nsINativeAppSupport* aNative,
    const char* aDialogURL, const char* aTelemetryEnvVar) {
  nsresult rv;

  nsCOMPtr<nsIFile> profD, profLD;
  bool offline = false;
  int32_t dialogReturn;

  // We aren't going to start this instance so we can unblock other instances
  // from starting up.
#if defined(MOZ_HAS_REMOTE)
  gStartupLock = nullptr;
#endif

  {
    ScopedXPCOMStartup xpcom;
    rv = xpcom.Initialize();
    NS_ENSURE_SUCCESS(rv, rv);

    rv = xpcom.SetWindowCreator(aNative);
    NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);

#ifdef XP_MACOSX
    InitializeMacApp();
    CommandLineServiceMac::SetupMacCommandLine(gRestartArgc, gRestartArgv,
                                               true);
#endif

    {  // extra scoping is needed so we release these components before xpcom
       // shutdown
      nsCOMPtr<nsIWindowWatcher> windowWatcher(
          do_GetService(NS_WINDOWWATCHER_CONTRACTID));
      nsCOMPtr<nsIDialogParamBlock> ioParamBlock(
          do_CreateInstance(NS_DIALOGPARAMBLOCK_CONTRACTID));
      nsCOMPtr<nsIMutableArray> dlgArray(
          do_CreateInstance(NS_ARRAY_CONTRACTID));
      NS_ENSURE_TRUE(windowWatcher && ioParamBlock && dlgArray,
                     NS_ERROR_FAILURE);

      ioParamBlock->SetObjects(dlgArray);

      nsCOMPtr<nsIAppStartup> appStartup(components::AppStartup::Service());
      NS_ENSURE_TRUE(appStartup, NS_ERROR_FAILURE);

      nsAutoCString features("centerscreen,chrome,modal,titlebar");
      // If we're launching a private browsing window make sure to set that
      // feature for the Profile Manager window as well, so it groups correctly
      // on the Windows taskbar.
      if (CheckArgExists("private-window") == ARG_FOUND) {
        features.AppendLiteral(",private");
      }
      nsCOMPtr<mozIDOMWindowProxy> newWindow;
      rv = windowWatcher->OpenWindow(nullptr, nsDependentCString(aDialogURL),
                                     "_blank"_ns, features, ioParamBlock,
                                     getter_AddRefs(newWindow));

      NS_ENSURE_SUCCESS_LOG(rv, rv);

      rv = ioParamBlock->GetInt(0, &dialogReturn);
      if (NS_FAILED(rv) || dialogReturn == nsIToolkitProfileService::exit) {
        return NS_ERROR_ABORT;
      }

      int32_t startOffline;
      rv = ioParamBlock->GetInt(1, &startOffline);
      offline = NS_SUCCEEDED(rv) && startOffline == 1;

      rv = dlgArray->QueryElementAt(0, NS_GET_IID(nsIFile),
                                    getter_AddRefs(profD));
      NS_ENSURE_SUCCESS_LOG(rv, rv);

      rv = dlgArray->QueryElementAt(1, NS_GET_IID(nsIFile),
                                    getter_AddRefs(profLD));
      NS_ENSURE_SUCCESS_LOG(rv, rv);

      if (dialogReturn == nsIToolkitProfileService::launchWithProfile) {
        int32_t newArguments;
        rv = ioParamBlock->GetInt(2, &newArguments);

        if (NS_SUCCEEDED(rv) && newArguments > 0) {
          char** newArgv = (char**)realloc(
              gRestartArgv, sizeof(char*) * (gRestartArgc + newArguments + 1));
          NS_ENSURE_TRUE(newArgv, NS_ERROR_OUT_OF_MEMORY);

          gRestartArgv = newArgv;

          for (auto i = 0; i < newArguments; i++) {
            char16_t* arg;
            ioParamBlock->GetString(i, &arg);
            gRestartArgv[gRestartArgc++] =
                strdup(NS_ConvertUTF16toUTF8(nsDependentString(arg)).get());
          }

          gRestartArgv[gRestartArgc] = nullptr;
        }
      }
    }
  }

  if (offline) {
    SaveToEnv("XRE_START_OFFLINE=1");
  }

  // User requested that we restart back into the profile manager.
  if (dialogReturn == nsIToolkitProfileService::restart) {
    SaveToEnv("XRE_RESTART_TO_PROFILE_MANAGER=1");
  } else {
    MOZ_ASSERT(dialogReturn == nsIToolkitProfileService::launchWithProfile);
    SaveFileToEnv("XRE_PROFILE_PATH", profD);
    SaveFileToEnv("XRE_PROFILE_LOCAL_PATH", profLD);
  }
  SaveToEnv(aTelemetryEnvVar);

  if (gRestartedByOS) {
    // Re-add this argument when actually starting the application.
    char** newArgv =
        (char**)realloc(gRestartArgv, sizeof(char*) * (gRestartArgc + 2));
    NS_ENSURE_TRUE(newArgv, NS_ERROR_OUT_OF_MEMORY);
    gRestartArgv = newArgv;
    gRestartArgv[gRestartArgc++] = const_cast<char*>("-os-restarted");
    gRestartArgv[gRestartArgc] = nullptr;
  }

  return LaunchChild(false, true);
}

static ReturnAbortOnError ShowProfileManager(
    nsIToolkitProfileService* aProfileSvc, nsINativeAppSupport* aNative) {
  static const char kProfileManagerURL[] =
      "chrome://mozapps/content/profile/profileSelection.xhtml";
  static const char kTelemetryEnv[] = "XRE_RESTARTED_BY_PROFILE_MANAGER=1";

  return ShowProfileDialog(aProfileSvc, aNative, kProfileManagerURL,
                           kTelemetryEnv);
}

static ReturnAbortOnError ShowProfileSelector(
    nsIToolkitProfileService* aProfileSvc, nsINativeAppSupport* aNative) {
  static const char kProfileSelectorURL[] = "about:profilemanager";
  static const char kTelemetryEnv[] = "XRE_RESTARTED_BY_PROFILE_SELECTOR=1";

  return ShowProfileDialog(aProfileSvc, aNative, kProfileSelectorURL,
                           kTelemetryEnv);
}

static bool gDoMigration = false;
static bool gDoProfileReset = false;
MOZ_RUNINIT static nsCOMPtr<nsIToolkitProfile> gResetOldProfile;

static nsresult LockProfile(nsINativeAppSupport* aNative, nsIFile* aRootDir,
                            nsIFile* aLocalDir, nsIToolkitProfile* aProfile,
                            nsIProfileLock** aResult) {
  // If you close Firefox and very quickly reopen it, the old Firefox may
  // still be closing down. Rather than immediately showing the
  // "Firefox is running but is not responding" message, we spend a few
  // seconds retrying first.

  static const int kLockRetrySeconds = 5;
  static const int kLockRetrySleepMS = 100;

  nsresult rv;
  nsCOMPtr<nsIProfileUnlocker> unlocker;
  const TimeStamp start = TimeStamp::Now();
  do {
    if (aProfile) {
      rv = aProfile->Lock(getter_AddRefs(unlocker), aResult);
    } else {
      rv = NS_LockProfilePath(aRootDir, aLocalDir, getter_AddRefs(unlocker),
                              aResult);
    }
    if (NS_SUCCEEDED(rv)) {
      StartupTimeline::Record(StartupTimeline::AFTER_PROFILE_LOCKED);
      return NS_OK;
    }
    PR_Sleep(kLockRetrySleepMS);
  } while (TimeStamp::Now() - start <
           TimeDuration::FromSeconds(kLockRetrySeconds));

  return ProfileLockedDialog(aRootDir, aLocalDir, unlocker, aNative, aResult);
}

// Pick a profile. We need to end up with a profile root dir, local dir and
// potentially an nsIToolkitProfile instance.
//
// 1) check for --profile <path>
// 2) check for -P <name>
// 3) check for --ProfileManager
// 4) use the default profile, if there is one
// 5) if there are *no* profiles, set up profile-migration
// 6) display the profile-manager UI
static nsresult SelectProfile(nsToolkitProfileService* aProfileSvc,
                              nsINativeAppSupport* aNative, nsIFile** aRootDir,
                              nsIFile** aLocalDir, nsIToolkitProfile** aProfile,
                              bool* aWasDefaultSelection) {
  StartupTimeline::Record(StartupTimeline::SELECT_PROFILE);

  nsresult rv;

  if (EnvHasValue("MOZ_RESET_PROFILE_RESTART")) {
    gDoProfileReset = true;
    gDoMigration = true;
  }

  // reset-profile and migration args need to be checked before any profiles are
  // chosen below.
  ArgResult ar = CheckArg("reset-profile");
  if (ar == ARG_FOUND) {
    gDoProfileReset = true;
  }

  ar = CheckArg("migration");
  if (ar == ARG_FOUND) {
    gDoMigration = true;
  }

#if defined(XP_WIN)
  // This arg is only used to indicate to telemetry that a profile refresh
  // (reset+migration) was requested from the uninstaller, pass this along
  // via an environment variable for simplicity.
  ar = CheckArg("uninstaller-profile-refresh");
  if (ar == ARG_FOUND) {
    SaveToEnv("MOZ_UNINSTALLER_PROFILE_REFRESH=1");
  }
#endif

  if (EnvHasValue("XRE_RESTART_TO_PROFILE_MANAGER")) {
    return ShowProfileManager(aProfileSvc, aNative);
  }

  // Ask the profile manager to select the profile directories to use.
  bool didCreate = false;
  rv = aProfileSvc->SelectStartupProfile(&gArgc, gArgv, gDoProfileReset,
                                         aRootDir, aLocalDir, aProfile,
                                         &didCreate, aWasDefaultSelection);

  if (rv == NS_ERROR_SHOW_PROFILE_MANAGER) {
    return ShowProfileManager(aProfileSvc, aNative);
  }

  NS_ENSURE_SUCCESS(rv, rv);

  if (didCreate) {
    // For a fresh install, we would like to let users decide
    // to do profile migration on their own later after using.
    gDoProfileReset = false;
    gDoMigration = false;
  }

  if (gDoProfileReset && !*aProfile) {
    NS_WARNING("Profile reset is only supported for named profiles.");
    return NS_ERROR_ABORT;
  }

  // No profile could be found. This generally shouldn't happen, a new profile
  // should be created in all cases except for profile reset which is covered
  // above, but just in case...
  if (!*aRootDir) {
    NS_WARNING("Failed to select or create profile.");
    return NS_ERROR_ABORT;
  }

  return NS_OK;
}

#ifdef MOZ_BLOCK_PROFILE_DOWNGRADE
struct FileWriteFunc final : public JSONWriteFunc {
  FILE* mFile;
  explicit FileWriteFunc(FILE* aFile) : mFile(aFile) {}

  void Write(const Span<const char>& aStr) final {
    fprintf(mFile, "%.*s", int(aStr.size()), aStr.data());
  }
};

static void SubmitDowngradeTelemetry(const nsCString& aLastVersion,
                                     bool aHasSync, int32_t aButton) {
  nsCOMPtr<nsIPrefService> prefSvc =
      do_GetService("@mozilla.org/preferences-service;1");
  NS_ENSURE_TRUE_VOID(prefSvc);

  nsCOMPtr<nsIPrefBranch> prefBranch = do_QueryInterface(prefSvc);
  NS_ENSURE_TRUE_VOID(prefBranch);

  bool enabled;
  nsresult rv =
      prefBranch->GetBoolPref(kPrefHealthReportUploadEnabled, &enabled);
  NS_ENSURE_SUCCESS_VOID(rv);
  if (!enabled) {
    return;
  }

  nsCString server;
  rv = prefBranch->GetCharPref("toolkit.telemetry.server", server);
  NS_ENSURE_SUCCESS_VOID(rv);

  nsCString clientId;
  rv = prefBranch->GetCharPref("toolkit.telemetry.cachedClientID", clientId);
  NS_ENSURE_SUCCESS_VOID(rv);

  nsCString profileGroupId;
  rv = prefBranch->GetCharPref("toolkit.telemetry.cachedProfileGroupID",
                               profileGroupId);
  NS_ENSURE_SUCCESS_VOID(rv);

  rv = prefSvc->GetDefaultBranch(nullptr, getter_AddRefs(prefBranch));
  NS_ENSURE_SUCCESS_VOID(rv);

  nsCString channel("default");
  rv = prefBranch->GetCharPref("app.update.channel", channel);
  NS_ENSURE_SUCCESS_VOID(rv);

  nsID uuid;
  rv = nsID::GenerateUUIDInPlace(uuid);
  NS_ENSURE_SUCCESS_VOID(rv);

  nsCString arch("null");
  nsCOMPtr<nsIPropertyBag2> sysInfo =
      do_GetService("@mozilla.org/system-info;1");
  NS_ENSURE_TRUE_VOID(sysInfo);
  sysInfo->GetPropertyAsACString(u"arch"_ns, arch);

  time_t now;
  time(&now);
  char date[sizeof "YYYY-MM-DDThh:mm:ss.000Z"];
  strftime(date, sizeof date, "%FT%T.000Z", gmtime(&now));

  NSID_TrimBracketsASCII pingId(uuid);
  constexpr auto pingType = "downgrade"_ns;

  int32_t pos = aLastVersion.Find("_");
  if (pos == kNotFound) {
    return;
  }

  const nsDependentCSubstring lastVersion = Substring(aLastVersion, 0, pos);
  const nsDependentCSubstring lastBuildId =
      Substring(aLastVersion, pos + 1, 14);

  nsPrintfCString url("%s/submit/telemetry/%s/%s/%s/%s/%s/%s?v=%d",
                      server.get(), PromiseFlatCString(pingId).get(),
                      pingType.get(), (const char*)gAppData->name,
                      (const char*)gAppData->version, channel.get(),
                      (const char*)gAppData->buildID,
                      TELEMETRY_PING_FORMAT_VERSION);

  nsCOMPtr<nsIFile> pingFile;
  rv = NS_GetSpecialDirectory(XRE_USER_APP_DATA_DIR, getter_AddRefs(pingFile));
  NS_ENSURE_SUCCESS_VOID(rv);
  rv = pingFile->Append(u"Pending Pings"_ns);
  NS_ENSURE_SUCCESS_VOID(rv);
  rv = pingFile->Create(nsIFile::DIRECTORY_TYPE, 0755);
  if (NS_FAILED(rv) && rv != NS_ERROR_FILE_ALREADY_EXISTS) {
    return;
  }
  rv = pingFile->Append(NS_ConvertUTF8toUTF16(pingId));
  NS_ENSURE_SUCCESS_VOID(rv);

  nsCOMPtr<nsIFile> pingSender;
  rv = NS_GetSpecialDirectory(NS_GRE_BIN_DIR, getter_AddRefs(pingSender));
  NS_ENSURE_SUCCESS_VOID(rv);
#  ifdef XP_WIN
  pingSender->Append(u"pingsender.exe"_ns);
#  else
  pingSender->Append(u"pingsender"_ns);
#  endif

  bool exists;
  rv = pingSender->Exists(&exists);
  NS_ENSURE_SUCCESS_VOID(rv);
  if (!exists) {
    return;
  }

  FILE* file;
  rv = pingFile->OpenANSIFileDesc("w", &file);
  NS_ENSURE_SUCCESS_VOID(rv);

  JSONWriter w(MakeUnique<FileWriteFunc>(file));
  w.Start();
  {
    w.StringProperty("type",
                     Span<const char>(pingType.Data(), pingType.Length()));
    w.StringProperty("id", PromiseFlatCString(pingId));
    w.StringProperty("creationDate", MakeStringSpan(date));
    w.IntProperty("version", TELEMETRY_PING_FORMAT_VERSION);
    w.StringProperty("clientId", clientId);
    w.StringProperty("profileGroupId", profileGroupId);
    w.StartObjectProperty("application");
    {
      w.StringProperty("architecture", arch);
      w.StringProperty(
          "buildId",
          MakeStringSpan(static_cast<const char*>(gAppData->buildID)));
      w.StringProperty(
          "name", MakeStringSpan(static_cast<const char*>(gAppData->name)));
      w.StringProperty(
          "version",
          MakeStringSpan(static_cast<const char*>(gAppData->version)));
      w.StringProperty("displayVersion",
                       MOZ_STRINGIFY(MOZ_APP_VERSION_DISPLAY));
      w.StringProperty(
          "vendor", MakeStringSpan(static_cast<const char*>(gAppData->vendor)));
      w.StringProperty("platformVersion", gToolkitVersion);
#  ifdef TARGET_XPCOM_ABI
      w.StringProperty("xpcomAbi", TARGET_XPCOM_ABI);
#  else
      w.StringProperty("xpcomAbi", "unknown");
#  endif
      w.StringProperty("channel", channel);
    }
    w.EndObject();
    w.StartObjectProperty("payload");
    {
      w.StringProperty("lastVersion", PromiseFlatCString(lastVersion));
      w.StringProperty("lastBuildId", PromiseFlatCString(lastBuildId));
      w.BoolProperty("hasSync", aHasSync);
      w.IntProperty("button", aButton);
    }
    w.EndObject();
  }
  w.End();

  fclose(file);

  PathString filePath = pingFile->NativePath();
  const filesystem::Path::value_type* args[2];
#  ifdef XP_WIN
  nsString urlw = NS_ConvertUTF8toUTF16(url);
  args[0] = urlw.get();
#  else
  args[0] = url.get();
#  endif
  args[1] = filePath.get();

  nsCOMPtr<nsIProcess> process =
      do_CreateInstance("@mozilla.org/process/util;1");
  NS_ENSURE_TRUE_VOID(process);
  process->Init(pingSender);
  process->SetStartHidden(true);
  process->SetNoShell(true);

#  ifdef XP_WIN
  process->Runw(false, args, 2);
#  else
  process->Run(false, args, 2);
#  endif
}

static const char kProfileDowngradeURL[] =
    "chrome://mozapps/content/profile/profileDowngrade.xhtml";

static ReturnAbortOnError CheckDowngrade(nsIFile* aProfileDir,
                                         nsINativeAppSupport* aNative,
                                         nsIToolkitProfileService* aProfileSvc,
                                         const nsCString& aLastVersion) {
  int32_t result = 0;
  nsresult rv;

  {
    if (gfxPlatform::IsHeadless()) {
      // TODO: make a way to turn off all dialogs when headless.
      Output(true,
             "This profile was last used with a newer version of this "
             "application. Please create a new profile.\n");
      return NS_ERROR_ABORT;
    }

    ScopedXPCOMStartup xpcom;
    rv = xpcom.Initialize();
    NS_ENSURE_SUCCESS(rv, rv);

    rv = xpcom.SetWindowCreator(aNative);
    NS_ENSURE_SUCCESS(rv, rv);

#  ifdef XP_MACOSX
    InitializeMacApp();
#  endif

    {  // extra scoping is needed so we release these components before xpcom
       // shutdown
      bool hasSync = false;
      nsCOMPtr<nsIPrefService> prefSvc =
          do_GetService("@mozilla.org/preferences-service;1");
      NS_ENSURE_TRUE(prefSvc, rv);

      nsCOMPtr<nsIFile> prefsFile;
      rv = aProfileDir->Clone(getter_AddRefs(prefsFile));
      NS_ENSURE_SUCCESS(rv, rv);

      rv = prefsFile->Append(u"prefs.js"_ns);
      NS_ENSURE_SUCCESS(rv, rv);

      rv = prefSvc->ReadUserPrefsFromFile(prefsFile);
      if (NS_SUCCEEDED(rv)) {
        nsCOMPtr<nsIPrefBranch> prefBranch = do_QueryInterface(prefSvc);

        rv = prefBranch->PrefHasUserValue("services.sync.username", &hasSync);
        NS_ENSURE_SUCCESS(rv, rv);
      }

      nsCOMPtr<nsIWindowWatcher> windowWatcher =
          do_GetService(NS_WINDOWWATCHER_CONTRACTID);
      NS_ENSURE_TRUE(windowWatcher, NS_ERROR_ABORT);

      nsCOMPtr<nsIAppStartup> appStartup(components::AppStartup::Service());
      NS_ENSURE_TRUE(appStartup, NS_ERROR_FAILURE);

      nsCOMPtr<nsIDialogParamBlock> paramBlock =
          do_CreateInstance(NS_DIALOGPARAMBLOCK_CONTRACTID);
      NS_ENSURE_TRUE(paramBlock, NS_ERROR_ABORT);

      uint8_t flags = 0;
      if (hasSync) {
        flags |= nsIToolkitProfileService::hasSync;
      }

      paramBlock->SetInt(0, flags);

      nsAutoCString features("centerscreen,chrome,modal,titlebar");
      // If we're launching a private browsing window make sure to set that
      // feature for the Profile Manager window as well, so it groups correctly
      // on the Windows taskbar.
      if (CheckArgExists("private-window") == ARG_FOUND) {
        features.AppendLiteral(",private");
      }
      nsCOMPtr<mozIDOMWindowProxy> newWindow;
      rv = windowWatcher->OpenWindow(
          nullptr, nsDependentCString(kProfileDowngradeURL), "_blank"_ns,
          features, paramBlock, getter_AddRefs(newWindow));
      NS_ENSURE_SUCCESS(rv, rv);

      paramBlock->GetInt(1, &result);

      SubmitDowngradeTelemetry(aLastVersion, hasSync, result);
    }
  }

  if (result == nsIToolkitProfileService::createNewProfile) {
    // Create a new profile and start it.
    nsCString profileName;
    profileName.AssignLiteral("default");
#  ifdef MOZ_DEDICATED_PROFILES
    profileName.Append("-" MOZ_STRINGIFY(MOZ_UPDATE_CHANNEL));
#  endif
    nsCOMPtr<nsIToolkitProfile> newProfile;
    rv = aProfileSvc->CreateUniqueProfile(nullptr, profileName,
                                          getter_AddRefs(newProfile));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = aProfileSvc->SetDefaultProfile(newProfile);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = aProfileSvc->Flush();
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIFile> profD, profLD;
    rv = newProfile->GetRootDir(getter_AddRefs(profD));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = newProfile->GetLocalDir(getter_AddRefs(profLD));
    NS_ENSURE_SUCCESS(rv, rv);

    SaveFileToEnv("XRE_PROFILE_PATH", profD);
    SaveFileToEnv("XRE_PROFILE_LOCAL_PATH", profLD);

    return LaunchChild(false, true);
  }

  // Cancel
  return NS_ERROR_ABORT;
}
#endif

/**
 * Extracts the various parts of a compatibility version string.
 *
 * Compatibility versions are of the form
 * "<appversion>_<appbuildid>/<platformbuildid>". The toolkit version comparator
 * can only handle 32-bit numbers and in the normal case build IDs are larger
 * than this. So if the build ID is numeric we split it into two version parts.
 */
static void ExtractCompatVersionInfo(const nsACString& aCompatVersion,
                                     nsACString& aAppVersion,
                                     nsACString& aAppBuildID) {
  int32_t underscorePos = aCompatVersion.FindChar('_');
  int32_t slashPos = aCompatVersion.FindChar('/');

  if (underscorePos == kNotFound || slashPos == kNotFound ||
      slashPos < underscorePos) {
    NS_WARNING(
        "compatibility.ini Version string does not match the expected format.");

    // Fall back to just using the entire string as the version.
    aAppVersion = aCompatVersion;
    aAppBuildID.Truncate(0);
    return;
  }

  aAppVersion = Substring(aCompatVersion, 0, underscorePos);
  aAppBuildID = Substring(aCompatVersion, underscorePos + 1,
                          slashPos - (underscorePos + 1));
}

/**
 * Compares the provided compatibility versions. Returns 0 if they match,
 * < 0 if the new version is considered an upgrade from the old version and
 * > 0 if the new version is considered a downgrade from the old version.
 */
int32_t CompareCompatVersions(const nsACString& aOldCompatVersion,
                              const nsACString& aNewCompatVersion) {
  // Hardcode the case where the last run was in safe mode (Bug 1556612). We
  // cannot tell if this is a downgrade or not so just assume it isn't and let
  // the user proceed.
  if (aOldCompatVersion.EqualsLiteral("Safe Mode")) {
    return -1;
  }

  // Extract the major version part from the version string and only use that
  // for version comparison.
  int32_t index = aOldCompatVersion.FindChar('.');
  const nsACString& oldMajorVersion = Substring(
      aOldCompatVersion, 0, index < 0 ? aOldCompatVersion.Length() : index);
  index = aNewCompatVersion.FindChar('.');
  const nsACString& newMajorVersion = Substring(
      aNewCompatVersion, 0, index < 0 ? aNewCompatVersion.Length() : index);

  return CompareVersions(PromiseFlatCString(oldMajorVersion).get(),
                         PromiseFlatCString(newMajorVersion).get());
}

/**
 * Checks the compatibility.ini file to see if we have updated our application
 * or otherwise invalidated our caches. If the application has been updated,
 * we return false; otherwise, we return true.
 *
 * We also write the status of the caches (valid/invalid) into the return param
 * aCachesOK. The aCachesOK is always invalid if the application has been
 * updated.
 *
 * Finally, aIsDowngrade is set to true if the current application is older
 * than that previously used by the profile.
 */
static bool CheckCompatibility(nsIFile* aProfileDir, const nsCString& aVersion,
                               const nsCString& aOSABI, nsIFile* aXULRunnerDir,
                               nsIFile* aAppDir, nsIFile* aFlagFile,
                               bool* aCachesOK, bool* aIsDowngrade,
                               nsCString& aLastVersion) {
  *aCachesOK = false;
  *aIsDowngrade = false;
  gLastAppVersion.SetIsVoid(true);
  gLastAppBuildID.SetIsVoid(true);

  nsCOMPtr<nsIFile> file;
  aProfileDir->Clone(getter_AddRefs(file));
  if (!file) return false;
  file->AppendNative(FILE_COMPATIBILITY_INFO);

  nsINIParser parser;
  nsresult rv = parser.Init(file);
  if (NS_FAILED(rv)) return false;

  rv = parser.GetString("Compatibility", "LastVersion", aLastVersion);
  if (NS_FAILED(rv)) {
    return false;
  }

  if (!aLastVersion.Equals(aVersion)) {
    // The version is not the same. Whether it's a downgrade depends on an
    // actual comparison:
    *aIsDowngrade = 0 < CompareCompatVersions(aLastVersion, aVersion);
    ExtractCompatVersionInfo(aLastVersion, gLastAppVersion, gLastAppBuildID);
    return false;
  }

  // If we get here, the version matched, but there may still be other
  // differences between us and the build that the profile last ran under.

  gLastAppVersion.Assign(gAppData->version);
  gLastAppBuildID.Assign(gAppData->buildID);

  nsAutoCString buf;
  rv = parser.GetString("Compatibility", "LastOSABI", buf);
  if (NS_FAILED(rv) || !aOSABI.Equals(buf)) return false;

  rv = parser.GetString("Compatibility", "LastPlatformDir", buf);
  if (NS_FAILED(rv)) return false;

  nsCOMPtr<nsIFile> lf;
  rv = NS_NewLocalFileWithPersistentDescriptor(buf, getter_AddRefs(lf));
  if (NS_FAILED(rv)) return false;

  bool eq;
  rv = lf->Equals(aXULRunnerDir, &eq);
  if (NS_FAILED(rv) || !eq) return false;

  if (aAppDir) {
    rv = parser.GetString("Compatibility", "LastAppDir", buf);
    if (NS_FAILED(rv)) return false;

    rv = NS_NewLocalFileWithPersistentDescriptor(buf, getter_AddRefs(lf));
    if (NS_FAILED(rv)) return false;

    rv = lf->Equals(aAppDir, &eq);
    if (NS_FAILED(rv) || !eq) return false;
  }

  // If we see this flag, caches are invalid.
  rv = parser.GetString("Compatibility", "InvalidateCaches", buf);
  *aCachesOK = (NS_FAILED(rv) || !buf.EqualsLiteral("1"));

  bool purgeCaches = false;
  if (aFlagFile && NS_SUCCEEDED(aFlagFile->Exists(&purgeCaches)) &&
      purgeCaches) {
    *aCachesOK = false;
  }

  return true;
}

void BuildCompatVersion(const char* aAppVersion, const char* aAppBuildID,
                        const char* aToolkitBuildID, nsACString& aBuf) {
  aBuf.Assign(aAppVersion);
  aBuf.Append('_');
  aBuf.Append(aAppBuildID);
  aBuf.Append('/');
  aBuf.Append(aToolkitBuildID);
}

static void BuildVersion(nsCString& aBuf) {
  BuildCompatVersion(gAppData->version, gAppData->buildID, gToolkitBuildID,
                     aBuf);
}

static void WriteVersion(nsIFile* aProfileDir, const nsCString& aVersion,
                         const nsCString& aOSABI, nsIFile* aXULRunnerDir,
                         nsIFile* aAppDir, bool invalidateCache) {
  nsCOMPtr<nsIFile> file;
  aProfileDir->Clone(getter_AddRefs(file));
  if (!file) return;
  file->AppendNative(FILE_COMPATIBILITY_INFO);

  nsAutoCString platformDir;
  Unused << aXULRunnerDir->GetPersistentDescriptor(platformDir);

  nsAutoCString appDir;
  if (aAppDir) Unused << aAppDir->GetPersistentDescriptor(appDir);

  PRFileDesc* fd;
  nsresult rv = file->OpenNSPRFileDesc(PR_WRONLY | PR_CREATE_FILE | PR_TRUNCATE,
                                       0600, &fd);
  if (NS_FAILED(rv)) {
    NS_ERROR("could not create output stream");
    return;
  }

  static const char kHeader[] = "[Compatibility]" NS_LINEBREAK "LastVersion=";

  PR_Write(fd, kHeader, sizeof(kHeader) - 1);
  PR_Write(fd, aVersion.get(), aVersion.Length());

  static const char kOSABIHeader[] = NS_LINEBREAK "LastOSABI=";
  PR_Write(fd, kOSABIHeader, sizeof(kOSABIHeader) - 1);
  PR_Write(fd, aOSABI.get(), aOSABI.Length());

  static const char kPlatformDirHeader[] = NS_LINEBREAK "LastPlatformDir=";

  PR_Write(fd, kPlatformDirHeader, sizeof(kPlatformDirHeader) - 1);
  PR_Write(fd, platformDir.get(), platformDir.Length());

  static const char kAppDirHeader[] = NS_LINEBREAK "LastAppDir=";
  if (aAppDir) {
    PR_Write(fd, kAppDirHeader, sizeof(kAppDirHeader) - 1);
    PR_Write(fd, appDir.get(), appDir.Length());
  }

  static const char kInvalidationHeader[] = NS_LINEBREAK "InvalidateCaches=1";
  if (invalidateCache)
    PR_Write(fd, kInvalidationHeader, sizeof(kInvalidationHeader) - 1);

  static const char kNL[] = NS_LINEBREAK;
  PR_Write(fd, kNL, sizeof(kNL) - 1);

  PR_Close(fd);
}

/**
 * Returns true if the startup cache file was successfully removed.
 * Returns false if file->Clone fails at any point (OOM) or if unable
 * to remove the startup cache file. Note in particular the return value
 * is unaffected by a failure to remove extensions.ini
 */
static bool RemoveComponentRegistries(nsIFile* aProfileDir,
                                      nsIFile* aLocalProfileDir,
                                      bool aRemoveEMFiles) {
  nsCOMPtr<nsIFile> file;
  aProfileDir->Clone(getter_AddRefs(file));
  if (!file) return false;

  if (aRemoveEMFiles) {
    file->SetNativeLeafName("extensions.ini"_ns);
    file->Remove(false);
  }

  aLocalProfileDir->Clone(getter_AddRefs(file));
  if (!file) return false;

  file->AppendNative("startupCache"_ns);
  nsresult rv = file->Remove(true);
  return NS_SUCCEEDED(rv) || rv == NS_ERROR_FILE_NOT_FOUND;
}

// When we first initialize the crash reporter we don't have a profile,
// so we set the minidump path to $TEMP.  Once we have a profile,
// we set it to $PROFILE/minidumps, creating the directory
// if needed.
static void MakeOrSetMinidumpPath(nsIFile* profD) {
  nsCOMPtr<nsIFile> dumpD;
  profD->Clone(getter_AddRefs(dumpD));

  if (dumpD) {
    bool fileExists;
    // XXX: do some more error checking here
    dumpD->Append(u"minidumps"_ns);
    dumpD->Exists(&fileExists);
    if (!fileExists) {
      nsresult rv = dumpD->Create(nsIFile::DIRECTORY_TYPE, 0700);
      NS_ENSURE_SUCCESS_VOID(rv);
    }

    nsAutoString pathStr;
    if (NS_SUCCEEDED(dumpD->GetPath(pathStr)))
      CrashReporter::SetMinidumpPath(pathStr);
  }
}

const XREAppData* gAppData = nullptr;

/**
 * NSPR will search for the "nspr_use_zone_allocator" symbol throughout
 * the process and use it to determine whether the application defines its own
 * memory allocator or not.
 *
 * Since most applications (e.g. Firefox and Thunderbird) don't use any special
 * allocators and therefore don't define this symbol, NSPR must search the
 * entire process, which reduces startup performance.
 *
 * By defining the symbol here, we can avoid the wasted lookup and hopefully
 * improve startup performance.
 */
NS_VISIBILITY_DEFAULT PRBool nspr_use_zone_allocator = PR_FALSE;

#ifdef CAIRO_HAS_DWRITE_FONT

#  include <dwrite.h>
#  include "nsWindowsHelpers.h"

#  ifdef DEBUG_DWRITE_STARTUP

#    define LOGREGISTRY(msg) LogRegistryEvent(msg)

// for use when monitoring process
static void LogRegistryEvent(const wchar_t* msg) {
  HKEY dummyKey;
  HRESULT hr;
  wchar_t buf[512];

  wsprintf(buf, L" log %s", msg);
  hr = RegOpenKeyEx(HKEY_LOCAL_MACHINE, buf, 0, KEY_READ, &dummyKey);
  if (SUCCEEDED(hr)) {
    RegCloseKey(dummyKey);
  }
}
#  else

#    define LOGREGISTRY(msg)

#  endif

static DWORD WINAPI InitDwriteBG(LPVOID lpdwThreadParam) {
  SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
  LOGREGISTRY(L"loading dwrite.dll");
  HMODULE dwdll = LoadLibrarySystem32(L"dwrite.dll");
  if (dwdll) {
    decltype(DWriteCreateFactory)* createDWriteFactory =
        (decltype(DWriteCreateFactory)*)GetProcAddress(dwdll,
                                                       "DWriteCreateFactory");
    if (createDWriteFactory) {
      LOGREGISTRY(L"creating dwrite factory");
      IDWriteFactory* factory;
      HRESULT hr = createDWriteFactory(DWRITE_FACTORY_TYPE_SHARED,
                                       __uuidof(IDWriteFactory),
                                       reinterpret_cast<IUnknown**>(&factory));
      if (SUCCEEDED(hr)) {
        LOGREGISTRY(L"dwrite factory done");
        factory->Release();
        LOGREGISTRY(L"freed factory");
      } else {
        LOGREGISTRY(L"failed to create factory");
      }
    }
  }
  SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
  return 0;
}
#endif

#include "GeckoProfiler.h"
#include "ProfilerControl.h"

// Encapsulates startup and shutdown state for XRE_main
class XREMain {
 public:
  XREMain() = default;

  ~XREMain() {
    mScopedXPCOM = nullptr;
    mAppData = nullptr;
  }

  int XRE_main(int argc, char* argv[], const BootstrapConfig& aConfig);
  int XRE_mainInit(bool* aExitFlag);
  int XRE_mainStartup(bool* aExitFlag);
  nsresult XRE_mainRun();

  bool CheckLastStartupWasCrash();

  nsCOMPtr<nsINativeAppSupport> mNativeApp;
  RefPtr<nsToolkitProfileService> mProfileSvc;
  nsCOMPtr<nsIFile> mProfD;
  nsCOMPtr<nsIFile> mProfLD;
  nsCOMPtr<nsIProfileLock> mProfileLock;

  UniquePtr<ScopedXPCOMStartup> mScopedXPCOM;
  UniquePtr<XREAppData> mAppData;

  nsXREDirProvider mDirProvider;

#ifdef MOZ_WIDGET_GTK
  nsAutoCString mXDGActivationToken;
  nsAutoCString mDesktopStartupID;
#endif

  bool mStartOffline = false;
  nsAutoCString mOriginToForceQUIC;
#if defined(MOZ_HAS_REMOTE)
  bool mDisableRemoteClient = false;
#endif
};

#if defined(XP_UNIX) && !defined(ANDROID)
static SmprintfPointer FormatUid(uid_t aId) {
  if (const auto pw = getpwuid(aId)) {
    return mozilla::Smprintf("%s", pw->pw_name);
  }
  return mozilla::Smprintf("uid %d", static_cast<int>(aId));
}

// Bug 1323302: refuse to run under sudo or similar.
static bool CheckForUserMismatch() {
  static char const* const kVars[] = {
      "HOME",
#  ifdef MOZ_WIDGET_GTK
      "XDG_RUNTIME_DIR",
#  endif
#  ifdef MOZ_X11
      "XAUTHORITY",
#  endif
  };

  const uid_t euid = geteuid();
  if (euid != 0) {
    // On Linux it's possible to have superuser capabilities with a
    // nonzero uid, but anyone who knows enough to make that happen
    // probably knows enough to debug the resulting problems.
    // Otherwise, a non-root user can't cause the problems we're
    // concerned about.
    return false;
  }

  for (const auto var : kVars) {
    if (const auto path = PR_GetEnv(var)) {
      struct stat st;
      if (stat(path, &st) == 0) {
        if (st.st_uid != euid) {
          const auto owner = FormatUid(st.st_uid);
          Output(true,
                 "Running " MOZ_APP_DISPLAYNAME
                 " as root in a regular"
                 " user's session is not supported.  ($%s is %s which is"
                 " owned by %s.)\n",
                 var, path, owner.get());
          return true;
        }
      }
    }
  }
  return false;
}
#else  // !XP_UNIX || ANDROID
static bool CheckForUserMismatch() { return false; }
#endif

void mozilla::startup::IncreaseDescriptorLimits() {
#ifdef XP_UNIX
  // Increase the fd limit to accomodate IPC resources like shared memory.
#  ifdef XP_DARWIN
  // We use Mach IPC for shared memory, so a lower limit suffices.
  // See also the Darwin case in config/external/nspr/pr/moz.build
  static constexpr rlim_t kFDs = 4096;
#  else   // Unix but not Darwin
  // This can be increased if needed, but also be aware that Linux
  // distributions may impose hard limits less than this.
  static constexpr rlim_t kFDs = 65536;
#  endif  // Darwin or not
  struct rlimit rlim;

  if (getrlimit(RLIMIT_NOFILE, &rlim) != 0) {
    Output(false, "getrlimit: %s\n", strerror(errno));
    return;
  }
  // Don't decrease the limit if it's already high enough, but don't
  // try to go over the hard limit.  (RLIM_INFINITY isn't required to
  // be the numerically largest rlim_t, so don't assume that.)
  if (rlim.rlim_cur != RLIM_INFINITY && rlim.rlim_cur < kFDs &&
      rlim.rlim_cur < rlim.rlim_max) {
    if (rlim.rlim_max != RLIM_INFINITY && rlim.rlim_max < kFDs) {
      rlim.rlim_cur = rlim.rlim_max;
    } else {
      rlim.rlim_cur = kFDs;
    }
    if (setrlimit(RLIMIT_NOFILE, &rlim) != 0) {
      Output(false, "setrlimit: %s\n", strerror(errno));
    }
  }
#endif
}

#ifdef XP_WIN
// Looks for the current CPU microcode version, returns 0 if it cannot find it
static uint32_t FindCPUMicrocodeVersion(HKEY key) {
  // Windows uses different registry values to surface the current microcode
  // value, and sometimes different update levels of the same version use
  // different values (e.g. Windows 11 24H2 uses "Current Record Version",
  // older Windows 11 and Windows 8/10 use "Update Revision", Windows 7 uses
  // "Update Signature" or sometimes "CurrentPatchLevel" for AMD CPUs.
  // Microcode version is represented by a 32-bit value but sometimes a 64-bit
  // value is surfaced instead with only half of it populated (e.g.
  // "Update Revision" on Windows 8, 10 and 11 pre-24H2). Since in these cases
  // the other half is zero, we pick the first non-zero 32-bit value we find
  // starting with the most "modern" registry value.
  LPCWSTR choices[] = {L"Current Record Version", L"Update Revision",
                       L"Update Signature", L"CurrentPatchLevel"};
  for (const auto& oneChoice : choices) {
    DWORD keyValue[2] = {};
    DWORD len = sizeof(keyValue);
    DWORD vtype;

    if (RegQueryValueExW(key, oneChoice, nullptr, &vtype,
                         reinterpret_cast<LPBYTE>(keyValue),
                         &len) == ERROR_SUCCESS) {
      if ((vtype == REG_BINARY) || (vtype == REG_DWORD)) {
        for (size_t i = 0; i < (len / sizeof(DWORD)); i++) {
          uint32_t value = keyValue[i];

          if (value != 0) {
            return value;
          }
        }
      }
    }
  }

  return 0;
}
#endif

static void MaybeAddCPUMicrocodeCrashAnnotation() {
#ifdef XP_WIN
  // Add CPU microcode version to the crash report as "CPUMicrocodeVersion".
  // It feels like this code may belong in nsSystemInfo instead.
  HKEY key;
  static const WCHAR keyName[] =
      L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0";

  if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, keyName, 0, KEY_QUERY_VALUE, &key) ==
      ERROR_SUCCESS) {
    uint32_t cpuMicrocodeVersion = FindCPUMicrocodeVersion(key);

    if (cpuMicrocodeVersion != 0) {
      CrashReporter::RecordAnnotationNSCString(
          CrashReporter::Annotation::CPUMicrocodeVersion,
          nsPrintfCString("0x%" PRIx32, cpuMicrocodeVersion));
    }
  }
#endif
}

#if defined(MOZ_BACKGROUNDTASKS)
static void SetupConsoleForBackgroundTask(
    const nsCString& aBackgroundTaskName) {
  // We do not suppress output on Windows because:
  // 1. Background task subprocesses launched via LaunchApp() does not attach to
  //    the console.
  // 2. Suppressing output intermittently causes failures on when running
  //    multiple tasks (see bug 1831631)
#  ifndef XP_WIN
  if (BackgroundTasks::IsNoOutputTaskName(aBackgroundTaskName) &&
      !CheckArg("attach-console") &&
      !EnvHasValue("MOZ_BACKGROUNDTASKS_IGNORE_NO_OUTPUT")) {
    // Suppress output, somewhat crudely.  We need to suppress stderr as well
    // as stdout because assertions, of which there are many, write to stderr.
    Unused << freopen("/dev/null", "w", stdout);
    Unused << freopen("/dev/null", "w", stderr);
    return;
  }
#  endif
  printf_stderr("*** You are running in background task mode. ***\n");
}
#endif

/*
 * XRE_mainInit - Initial setup and command line parameter processing.
 * Main() will exit early if either return value != 0 or if aExitFlag is
 * true.
 */
int XREMain::XRE_mainInit(bool* aExitFlag) {
  if (!aExitFlag) return 1;
  *aExitFlag = false;

  atexit(UnexpectedExit);
  auto expectedShutdown = mozilla::MakeScopeExit([&] { MozExpectedExit(); });

  StartupTimeline::Record(StartupTimeline::MAIN);

  // On Windows, to get working console arrangements so help/version/etc
  // print something, we need to initialize the native app support.
  nsresult rv = NS_CreateNativeAppSupport(getter_AddRefs(mNativeApp));
  if (NS_FAILED(rv)) return 1;

  // Handle --*version arguments early to avoid initializing full XPCOM.
  // Note: we *cannot* handle --help here, as it requires more components
  // to be initialized.
  if (CheckArg("v") || CheckArg("version")) {
    DumpVersion();
    *aExitFlag = true;
    return 0;
  }

  if (CheckArg("full-version")) {
    DumpFullVersion();
    *aExitFlag = true;
    return 0;
  }

  if (CheckForUserMismatch()) {
    return 1;
  }

#ifdef XP_MACOSX
  mozilla::MacAutoreleasePool pool;

  DisableAppNap();
#endif

#ifdef MOZ_BACKGROUNDTASKS
  Maybe<nsCString> backgroundTask = Nothing();
  const char* backgroundTaskName = nullptr;
  if (ARG_FOUND ==
      CheckArg("backgroundtask", &backgroundTaskName, CheckArgFlag::None)) {
    backgroundTask = Some(backgroundTaskName);

    SetupConsoleForBackgroundTask(backgroundTask.ref());
  }

  BackgroundTasks::Init(backgroundTask);
#endif

#ifndef ANDROID
  if (PR_GetEnv("MOZ_RUN_GTEST")
#  ifdef FUZZING
      || PR_GetEnv("FUZZER")
#  endif
#  ifdef MOZ_BACKGROUNDTASKS
      || BackgroundTasks::IsBackgroundTaskMode()
#  endif
  ) {
    // Enable headless mode and assert that it worked, since gfxPlatform
    // uses a static bool set after the first call to `IsHeadless`.
    // Note: Android gtests seem to require an Activity and fail to start
    // with headless mode enabled.
    PR_SetEnv("MOZ_HEADLESS=1");
    MOZ_ASSERT(gfxPlatform::IsHeadless());
  }
#endif  // ANDROID

  if (auto* featureStr = PR_GetEnv("MOZ_CHAOSMODE")) {
    ChaosFeature feature = ChaosFeature::Any;
    long featureInt = strtol(featureStr, nullptr, 16);
    if (featureInt) {
      // NOTE: MOZ_CHAOSMODE=0 or a non-hex value maps to Any feature.
      feature = static_cast<ChaosFeature>(featureInt);
    }
    ChaosMode::SetChaosFeature(feature);
    ChaosMode::enterChaosMode();
    MOZ_ASSERT(ChaosMode::isActive(ChaosFeature::Any));
  }

  if (CheckArgExists("fxr")) {
    gFxREmbedded = true;
  }

  if (ChaosMode::isActive(ChaosFeature::Any)) {
    printf_stderr(
        "*** You are running in chaos test mode. See ChaosMode.h. ***\n");
  }

  if (CheckArg("headless") || CheckArgExists("screenshot")) {
    PR_SetEnv("MOZ_HEADLESS=1");
  }

  if (gfxPlatform::IsHeadless()) {
#if defined(XP_WIN) || defined(MOZ_WIDGET_GTK) || defined(XP_MACOSX)
    printf_stderr("*** You are running in headless mode.\n");
#else
    Output(
        true,
        "Error: headless mode is not currently supported on this platform.\n");
    return 1;
#endif

#ifdef XP_MACOSX
    // To avoid taking focus when running in headless mode immediately
    // transition Firefox to a background application.
    ProcessSerialNumber psn = {0, kCurrentProcess};
    OSStatus transformStatus =
        TransformProcessType(&psn, kProcessTransformToBackgroundApplication);
    if (transformStatus != noErr) {
      NS_ERROR("Failed to make process a background application.");
      return 1;
    }
#endif
  }

  gKioskMode = CheckArg("kiosk", nullptr, CheckArgFlag::None);
  const char* kioskMonitorNumber = nullptr;
  if (CheckArg("kiosk-monitor", &kioskMonitorNumber, CheckArgFlag::None)) {
    gKioskMode = true;
    gKioskMonitor = atoi(kioskMonitorNumber);
  }

#if defined(NIGHTLY_BUILD)
  if (XRE_IsParentProcess()) {
    gAllowContentAnalysisArgPresent =
        CheckArg("allow-content-analysis", nullptr, CheckArgFlag::None) ==
        ARG_FOUND;
  }
#endif

  ArgResult ar;

#ifdef DEBUG
  if (PR_GetEnv("XRE_MAIN_BREAK")) NS_BREAK();
#endif

  mozilla::startup::IncreaseDescriptorLimits();

  SetupErrorHandling(gArgv[0]);

#ifdef CAIRO_HAS_DWRITE_FONT
  {
    // Bug 602792 - when DWriteCreateFactory is called the dwrite client dll
    // starts the FntCache service if it isn't already running (it's set
    // to manual startup by default in Windows 7 RTM).  Subsequent DirectWrite
    // calls cause the IDWriteFactory object to communicate with the FntCache
    // service with a timeout; if there's no response after the timeout, the
    // DirectWrite client library will assume the service isn't around and do
    // manual font file I/O on _all_ system fonts.  To avoid this, load the
    // dwrite library and create a factory as early as possible so that the
    // FntCache service is ready by the time it's needed.

    CreateThread(nullptr, 0, &InitDwriteBG, nullptr, 0, nullptr);
  }
#endif

#ifdef XP_UNIX
  const char* home = PR_GetEnv("HOME");
  if (!home || !*home) {
    struct passwd* pw = getpwuid(geteuid());
    if (!pw || !pw->pw_dir) {
      Output(true, "Could not determine HOME directory");
      return 1;
    }
    SaveWordToEnv("HOME", nsDependentCString(pw->pw_dir));
  }
#endif

#ifdef MOZ_ACCESSIBILITY_ATK
  // Suppress atk-bridge init at startup, until mozilla accessibility is
  // initialized.  This works after gnome 2.24.2.
  SaveToEnv("NO_AT_BRIDGE=1");
#endif

  // Check for application.ini overrides
  const char* override = nullptr;
  ar = CheckArg("override", &override);
  if (ar == ARG_BAD) {
    Output(true, "Incorrect number of arguments passed to --override");
    return 1;
  }
  if (ar == ARG_FOUND) {
    nsCOMPtr<nsIFile> overrideLF;
    rv = XRE_GetFileFromPath(override, getter_AddRefs(overrideLF));
    if (NS_FAILED(rv)) {
      Output(true, "Error: unrecognized override.ini path.\n");
      return 1;
    }

    rv = XRE_ParseAppData(overrideLF, *mAppData);
    if (NS_FAILED(rv)) {
      Output(true, "Couldn't read override.ini");
      return 1;
    }
  }

  // Check sanity and correctness of app data.

  if (!mAppData->name) {
    Output(true, "Error: App:Name not specified in application.ini\n");
    return 1;
  }
  if (!mAppData->buildID) {
    Output(true, "Error: App:BuildID not specified in application.ini\n");
    return 1;
  }

  // XXX Originally ScopedLogging was here? Now it's in XRE_main above
  // XRE_mainInit.

  if (!mAppData->minVersion) {
    Output(true, "Error: Gecko:MinVersion not specified in application.ini\n");
    return 1;
  }

  if (!mAppData->maxVersion) {
    // If no maxVersion is specified, we assume the app is only compatible
    // with the initial preview release. Do not increment this number ever!
    mAppData->maxVersion = "1.*";
  }

  if (mozilla::Version(mAppData->minVersion) > gToolkitVersion ||
      mozilla::Version(mAppData->maxVersion) < gToolkitVersion) {
    Output(true,
           "Error: Platform version '%s' is not compatible with\n"
           "minVersion >= %s\nmaxVersion <= %s\n"
           "Maybe try to reinstall " MOZ_APP_DISPLAYNAME "?\n",
           (const char*)gToolkitVersion, (const char*)mAppData->minVersion,
           (const char*)mAppData->maxVersion);
    return 1;
  }

  rv = mDirProvider.Initialize(mAppData->directory, mAppData->xreDirectory);
  if (NS_FAILED(rv)) return 1;

  if (EnvHasValue("MOZ_CRASHREPORTER")) {
    mAppData->flags |= NS_XRE_ENABLE_CRASH_REPORTER;
  }

  nsCOMPtr<nsIFile> xreBinDirectory;
  xreBinDirectory = mDirProvider.GetGREBinDir();

  if ((mAppData->flags & NS_XRE_ENABLE_CRASH_REPORTER) &&
      NS_SUCCEEDED(CrashReporter::SetExceptionHandler(xreBinDirectory))) {
    nsCOMPtr<nsIFile> file;
    rv = nsXREDirProvider::GetUserAppDataDirectory(getter_AddRefs(file));
    if (NS_SUCCEEDED(rv)) {
      CrashReporter::SetUserAppDataDirectory(file);
    }
    if (mAppData->crashReporterURL) {
      CrashReporter::SetServerURL(
          nsDependentCString(mAppData->crashReporterURL));
    }

    // We overwrite this once we finish starting up.
    CrashReporter::RecordAnnotationBool(CrashReporter::Annotation::StartupCrash,
                                        true);

    // pass some basic info from the app data
    if (mAppData->vendor) {
      CrashReporter::RecordAnnotationCString(CrashReporter::Annotation::Vendor,
                                             mAppData->vendor);
    }
    if (mAppData->name) {
      CrashReporter::RecordAnnotationCString(
          CrashReporter::Annotation::ProductName, mAppData->name);
    }
    if (mAppData->ID) {
      CrashReporter::RecordAnnotationCString(
          CrashReporter::Annotation::ProductID, mAppData->ID);
    }
    if (mAppData->version) {
      CrashReporter::RecordAnnotationCString(CrashReporter::Annotation::Version,
                                             mAppData->version);
    }
    if (mAppData->buildID) {
      CrashReporter::RecordAnnotationCString(CrashReporter::Annotation::BuildID,
                                             mAppData->buildID);
    }

    nsDependentCString releaseChannel(MOZ_STRINGIFY(MOZ_UPDATE_CHANNEL));
    CrashReporter::RecordAnnotationNSCString(
        CrashReporter::Annotation::ReleaseChannel, releaseChannel);

#ifdef XP_WIN
    nsAutoString appInitDLLs;
    if (widget::WinUtils::GetAppInitDLLs(appInitDLLs)) {
      CrashReporter::RecordAnnotationNSString(
          CrashReporter::Annotation::AppInitDLLs, appInitDLLs);
    }

    nsString packageFamilyName = widget::WinUtils::GetPackageFamilyName();
    if (StringBeginsWith(packageFamilyName, u"Mozilla."_ns) ||
        StringBeginsWith(packageFamilyName, u"MozillaCorporation."_ns)) {
      CrashReporter::RecordAnnotationNSCString(
          CrashReporter::Annotation::WindowsPackageFamilyName,
          NS_ConvertUTF16toUTF8(packageFamilyName));
    }
#endif

    bool isBackgroundTaskMode = false;
#ifdef MOZ_BACKGROUNDTASKS
    Maybe<nsCString> backgroundTasks = BackgroundTasks::GetBackgroundTasks();
    if (backgroundTasks.isSome()) {
      isBackgroundTaskMode = true;
      CrashReporter::RecordAnnotationNSCString(
          CrashReporter::Annotation::BackgroundTaskName, backgroundTasks.ref());
    }
#endif
    CrashReporter::RecordAnnotationBool(
        CrashReporter::Annotation::BackgroundTaskMode, isBackgroundTaskMode);

    CrashReporter::RecordAnnotationBool(CrashReporter::Annotation::HeadlessMode,
                                        gfxPlatform::IsHeadless());

    CrashReporter::SetRestartArgs(gArgc, gArgv);

    // annotate other data (user id etc)
    nsCOMPtr<nsIFile> userAppDataDir;
    if (NS_SUCCEEDED(mDirProvider.GetUserAppDataDirectory(
            getter_AddRefs(userAppDataDir)))) {
      CrashReporter::SetupExtraData(userAppDataDir,
                                    nsDependentCString(mAppData->buildID));

      // see if we have a crashreporter-override.ini in the application
      // directory
      nsCOMPtr<nsIFile> overrideini;
      if (NS_SUCCEEDED(
              mDirProvider.GetAppDir()->Clone(getter_AddRefs(overrideini))) &&
          NS_SUCCEEDED(
              overrideini->AppendNative("crashreporter-override.ini"_ns))) {
#ifdef XP_WIN
        nsAutoString overridePathW;
        overrideini->GetPath(overridePathW);
        NS_ConvertUTF16toUTF8 overridePath(overridePathW);
#else
        nsAutoCString overridePath;
        overrideini->GetNativePath(overridePath);
#endif

        SaveWordToEnv("MOZ_CRASHREPORTER_STRINGS_OVERRIDE", overridePath);
      }
    }
  } else {
    // We might have registered a runtime exception module very early in process
    // startup to catch early crashes. This is before we have access to ini file
    // data, so unregister here if it turns out the crash reporter is disabled.
    CrashReporter::UnregisterRuntimeExceptionModule();
  }

#if defined(MOZ_SANDBOX) && defined(XP_WIN)
  if (mAppData->sandboxBrokerServices) {
    nsAutoString binDirPath;
    MOZ_ALWAYS_SUCCEEDS(xreBinDirectory->GetPath(binDirPath));
    SandboxBroker::Initialize(mAppData->sandboxBrokerServices, binDirPath);
  } else {
#  if defined(MOZ_SANDBOX)
    // If we're sandboxing content and we fail to initialize, then crashing here
    // seems like the sensible option.
    if (BrowserTabsRemoteAutostart()) {
      MOZ_CRASH("Failed to initialize broker services, can't continue.");
    }
#  endif
    // Otherwise just warn for the moment, as most things will work.
    NS_WARNING(
        "Failed to initialize broker services, sandboxed processes will "
        "fail to start.");
  }
#endif

#ifdef XP_MACOSX
  // Set up ability to respond to system (Apple) events. This must occur before
  // ProcessUpdates to ensure that links clicked in external applications aren't
  // lost when updates are pending.
  SetupMacApplicationDelegate(&gRestartedByOS);

  if (EnvHasValue("MOZ_LAUNCHED_CHILD")) {
    // This is needed, on relaunch, to force the OS to use the "Cocoa Dock
    // API".  Otherwise the call to ReceiveNextEvent() below will make it
    // use the "Carbon Dock API".  For more info see bmo bug 377166.
    EnsureUseCocoaDockAPI();

    // When the app relaunches, the original process exits.  This causes
    // the dock tile to stop bouncing, lose the "running" triangle, and
    // if the tile does not permanently reside in the Dock, even disappear.
    // This can be confusing to the user, who is expecting the app to launch.
    // Calling ReceiveNextEvent without requesting any event is enough to
    // cause a dock tile for the child process to appear.
    const EventTypeSpec kFakeEventList[] = {{INT_MAX, INT_MAX}};
    EventRef event;
    ::ReceiveNextEvent(GetEventTypeCount(kFakeEventList), kFakeEventList,
                       kEventDurationNoWait, false, &event);
  }

  if (CheckArg("foreground")) {
    // The original process communicates that it was in the foreground by
    // adding this argument.  This new process, which is taking over for
    // the old one, should make itself the active application.
    ProcessSerialNumber psn;
    if (::GetCurrentProcess(&psn) == noErr) {
      ::SetFrontProcess(&psn);
    }
  }
#endif

  SaveToEnv("MOZ_LAUNCHED_CHILD=");

  // On Windows, the -os-restarted command line switch lets us know when we are
  // restarted via RegisterApplicationRestart. May be used for other OSes later.
  if (CheckArg("os-restarted", nullptr, CheckArgFlag::RemoveArg) == ARG_FOUND) {
    gRestartedByOS = true;
  }

  gRestartArgc = gArgc;
  gRestartArgv =
      (char**)malloc(sizeof(char*) * (gArgc + 1 + (override ? 2 : 0)));
  if (!gRestartArgv) {
    return 1;
  }

  int i;
  for (i = 0; i < gArgc; ++i) {
    gRestartArgv[i] = gArgv[i];
  }

  // Add the -override argument back (it is removed automatically be CheckArg)
  // if there is one
  if (override) {
    gRestartArgv[gRestartArgc++] = const_cast<char*>("-override");
    gRestartArgv[gRestartArgc++] = const_cast<char*>(override);
  }

  gRestartArgv[gRestartArgc] = nullptr;

  Maybe<bool> safeModeRequested = IsSafeModeRequested(gArgc, gArgv);
  if (!safeModeRequested) {
    return 1;
  }
#ifdef MOZ_BACKGROUNDTASKS
  if (BackgroundTasks::IsBackgroundTaskMode()) {
    safeModeRequested = Some(false);

    // Remove the --backgroundtask arg now that it has been saved in
    // gRestartArgv.
    const char* tmpBackgroundTaskName = nullptr;
    Unused << CheckArg("backgroundtask", &tmpBackgroundTaskName,
                       CheckArgFlag::RemoveArg);
  }
#endif

  gSafeMode = safeModeRequested.value();

  MaybeAddCPUMicrocodeCrashAnnotation();
  CrashReporter::RegisterAnnotationBool(CrashReporter::Annotation::SafeMode,
                                        &gSafeMode);

  // Strip the now unsupported no-remote command line argument.
  CheckArg("no-remote");

#if defined(MOZ_HAS_REMOTE)
  // Handle the --new-instance command line arguments.
  ar = CheckArg("new-instance");
  if (ar == ARG_FOUND || EnvHasValue("MOZ_NEW_INSTANCE")) {
    mDisableRemoteClient = true;
  }
#else
  // These arguments do nothing in platforms with no remoting support but we
  // should remove them from the command line anyway.
  CheckArg("new-instance");
#endif

  ar = CheckArg("offline");
  if (ar || EnvHasValue("XRE_START_OFFLINE")) {
    mStartOffline = true;
  }

  const char* origin = nullptr;
  if (!PR_GetEnv("MOZ_FORCE_QUIC_ON") &&
      ARG_FOUND == CheckArg("origin-to-force-quic-on", &origin,
                            CheckArgFlag::RemoveArg)) {
    mOriginToForceQUIC.Assign(origin);
  }

  // Handle --help command line argument.
  // It should return rather quickly, so we deal with it here.
  // Note: we *cannot* handle the argument as early as --*version because it
  // requires ScopedXPCOMStartup and other components be registered.
  if (CheckArg("h") || CheckArg("help") || CheckArg("?")) {
    DumpHelp();
    *aExitFlag = true;
    return 0;
  }

#ifdef MOZ_ENABLE_DBUS
  const char* dbusServiceLauncher = nullptr;
  ar = CheckArg("dbus-service", &dbusServiceLauncher, CheckArgFlag::None);
  if (ar == ARG_BAD) {
    Output(true, "Missing launcher param for --dbus-service\n");
    return 1;
  }
  if (ar == ARG_FOUND) {
    UniquePtr<DBusService> dbusService =
        MakeUnique<DBusService>(dbusServiceLauncher);
    if (dbusService->Init()) {
      dbusService->Run();
    }
    *aExitFlag = true;
    return 0;
  }
#endif

  rv = XRE_InitCommandLine(gArgc, gArgv);
  NS_ENSURE_SUCCESS(rv, 1);

  return 0;
}

#if defined(XP_LINUX) && !defined(ANDROID)

static void AnnotateLSBRelease(void*) {
  nsCString dist, desc, release, codename;
  if (widget::lsb::GetLSBRelease(dist, desc, release, codename)) {
    CrashReporter::AppendAppNotesToCrashReport(desc);
  }
}

#endif  // defined(XP_LINUX) && !defined(ANDROID)

#ifdef XP_WIN
static void ReadAheadSystemDll(const wchar_t* dllName) {
  wchar_t dllPath[MAX_PATH];
  if (ConstructSystem32Path(dllName, dllPath, MAX_PATH)) {
    ReadAheadLib(dllPath);
  }
}

static void ReadAheadPackagedDll(const wchar_t* dllName,
                                 const wchar_t* aGREDir) {
  wchar_t dllPath[MAX_PATH];
  swprintf(dllPath, MAX_PATH, L"%s\\%s", aGREDir, dllName);
  ReadAheadLib(dllPath);
}

static void ReadAheadDlls(const wchar_t* greDir) {
  // In Bug 1628903, we investigated which DLLs we should prefetch in
  // order to reduce disk I/O and improve startup on Windows machines.
  // Our ultimate goal is to measure the impact of these improvements on
  // retention (see Bug 1640087). Before we place this within a pref,
  // we should ensure this feature only ships to the nightly channel
  // and monitor results from that subset.
  if (greDir) {
    // Prefetch the DLLs shipped with firefox
    ReadAheadPackagedDll(L"libegl.dll", greDir);
    ReadAheadPackagedDll(L"libGLESv2.dll", greDir);
    ReadAheadPackagedDll(L"freebl3.dll", greDir);
    ReadAheadPackagedDll(L"softokn3.dll", greDir);

    // Prefetch the system DLLs
    ReadAheadSystemDll(L"DWrite.dll");
    ReadAheadSystemDll(L"D3DCompiler_47.dll");
  } else {
    // Load DataExchange.dll and twinapi.appcore.dll for
    // nsWindow::EnableDragDrop
    ReadAheadSystemDll(L"DataExchange.dll");
    ReadAheadSystemDll(L"twinapi.appcore.dll");

    // Load twinapi.dll for WindowsUIUtils::UpdateTabletModeState
    ReadAheadSystemDll(L"twinapi.dll");

    // Load explorerframe.dll for WinTaskbar::Initialize
    ReadAheadSystemDll(L"ExplorerFrame.dll");

    // Load WinTypes.dll for nsOSHelperAppService::GetApplicationDescription
    ReadAheadSystemDll(L"WinTypes.dll");
  }
}
#endif

#if defined(MOZ_UPDATER) && !defined(MOZ_WIDGET_ANDROID)
enum struct ShouldNotProcessUpdatesReason {
  DevToolsLaunching,
  NotAnUpdatingTask,
  OtherInstanceRunning,
  FirstStartup
};

const char* ShouldNotProcessUpdatesReasonAsString(
    ShouldNotProcessUpdatesReason aReason) {
  switch (aReason) {
    case ShouldNotProcessUpdatesReason::DevToolsLaunching:
      return "DevToolsLaunching";
    case ShouldNotProcessUpdatesReason::NotAnUpdatingTask:
      return "NotAnUpdatingTask";
    case ShouldNotProcessUpdatesReason::OtherInstanceRunning:
      return "OtherInstanceRunning";
    default:
      MOZ_CRASH("impossible value for ShouldNotProcessUpdatesReason");
  }
}

Maybe<ShouldNotProcessUpdatesReason> ShouldNotProcessUpdates(
    nsXREDirProvider& aDirProvider) {
  // Don't process updates when launched from the installer.
  // It's possible for a stale update to be present in the case of a paveover;
  // ignore it and leave the update service to discard it.
  if (ARG_FOUND == CheckArgExists("first-startup")) {
    NS_WARNING("ShouldNotProcessUpdates(): FirstStartup");
    return Some(ShouldNotProcessUpdatesReason::FirstStartup);
  }

  // Do not process updates if we're launching devtools, as evidenced by
  // "--chrome ..." with the browser toolbox chrome document URL.

  // Keep this synchronized with the value of the same name in
  // devtools/client/framework/browser-toolbox/Launcher.sys.mjs.  Or, for bonus
  // points, lift this value to nsIXulRuntime or similar, so that it can be
  // accessed in both locations.  (The prefs service isn't available at this
  // point so the simplest manner of sharing the value is not available to us.)
  const char* BROWSER_TOOLBOX_WINDOW_URL =
      "chrome://devtools/content/framework/browser-toolbox/window.html";

  const char* chromeParam = nullptr;
  if (ARG_FOUND == CheckArg("chrome", &chromeParam, CheckArgFlag::None)) {
    if (!chromeParam || !strcmp(BROWSER_TOOLBOX_WINDOW_URL, chromeParam)) {
      NS_WARNING("ShouldNotProcessUpdates(): DevToolsLaunching");
      return Some(ShouldNotProcessUpdatesReason::DevToolsLaunching);
    }
  }

#  ifdef MOZ_BACKGROUNDTASKS
  // Do not process updates if we're running a background task mode and another
  // instance is already running.  This avoids periodic maintenance updating
  // underneath a browsing session.
  Maybe<nsCString> backgroundTasks = BackgroundTasks::GetBackgroundTasks();
  if (backgroundTasks.isSome()) {
    // Only process updates for specific tasks: at this time, the
    // `backgroundupdate` task and the test-only `shouldprocessupdates` task.
    //
    // Background tasks can be sparked by Firefox instances that are shutting
    // down, which can cause races between the task startup trying to update and
    // Firefox trying to invoke the updater.  This happened when converting
    // `pingsender` to a background task, since it is launched to send pings at
    // shutdown: Bug 1736373.
    //
    // We'd prefer to have this be a property of the task definition sibling to
    // `backgroundTaskTimeoutSec`, but when we reach this code we're well before
    // we can load the task JSM.
    if (!BackgroundTasks::IsUpdatingTaskName(backgroundTasks.ref())) {
      NS_WARNING("ShouldNotProcessUpdates(): NotAnUpdatingTask");
      return Some(ShouldNotProcessUpdatesReason::NotAnUpdatingTask);
    }

    // At this point we have a dir provider but no XPCOM directory service.  We
    // launch the update sync manager using that information so that it doesn't
    // need to ask for (and fail to find) the directory service.
    nsCOMPtr<nsIFile> anAppFile;
    bool persistent;
    nsresult rv = aDirProvider.GetFile(XRE_EXECUTABLE_FILE, &persistent,
                                       getter_AddRefs(anAppFile));
    if (NS_FAILED(rv) || !anAppFile) {
      // Strange, but not a reason to skip processing updates.
      return Nothing();
    }

    auto updateSyncManager = new nsUpdateSyncManager(anAppFile);

    bool otherInstance = false;
    updateSyncManager->IsOtherInstanceRunning(&otherInstance);
    if (otherInstance) {
      NS_WARNING("ShouldNotProcessUpdates(): OtherInstanceRunning");
      return Some(ShouldNotProcessUpdatesReason::OtherInstanceRunning);
    }
  }
#  endif

  return Nothing();
}
#endif

namespace mozilla::startup {
Result<nsCOMPtr<nsIFile>, nsresult> GetIncompleteStartupFile(nsIFile* aProfLD) {
  nsCOMPtr<nsIFile> crashFile;
  MOZ_TRY(aProfLD->Clone(getter_AddRefs(crashFile)));
  MOZ_TRY(crashFile->Append(FILE_STARTUP_INCOMPLETE));
  return std::move(crashFile);
}
}  // namespace mozilla::startup

// Check whether the last startup attempt resulted in a crash or hang.
// This is distinct from the definition of a startup crash from
// nsAppStartup::TrackStartupCrashBegin.
bool XREMain::CheckLastStartupWasCrash() {
  Result<nsCOMPtr<nsIFile>, nsresult> crashFile =
      GetIncompleteStartupFile(mProfLD);
  if (crashFile.isErr()) {
    return true;
  }

  // Attempt to create the incomplete startup canary file. If the file already
  // exists, this fails, and we know the last startup was a crash. If it
  // doesn't already exist, it is created, and will be removed at the end of
  // the startup crash detection window.
  AutoFDClose fd;
  Unused << crashFile.inspect()->OpenNSPRFileDesc(
      PR_WRONLY | PR_CREATE_FILE | PR_EXCL, 0666, getter_Transfers(fd));
  return !fd;
}

/*
 * XRE_mainStartup - Initializes the profile and various other services.
 * Main() will exit early if either return value != 0 or if aExitFlag is
 * true.
 */
int XREMain::XRE_mainStartup(bool* aExitFlag) {
  nsresult rv;

  if (!aExitFlag) return 1;
  *aExitFlag = false;

#ifdef XP_MACOSX
  mozilla::MacAutoreleasePool pool;
#endif

  // Enable Telemetry IO Reporting on DEBUG, nightly and local builds,
  // but disable it on FUZZING builds and for ANDROID.
#ifndef FUZZING
#  ifndef ANDROID
#    ifdef DEBUG
  mozilla::Telemetry::InitIOReporting(gAppData->xreDirectory);
#    else
  {
    const char* releaseChannel = MOZ_STRINGIFY(MOZ_UPDATE_CHANNEL);
    if (strcmp(releaseChannel, "nightly") == 0 ||
        strcmp(releaseChannel, "default") == 0) {
      mozilla::Telemetry::InitIOReporting(gAppData->xreDirectory);
    }
  }
#    endif /* DEBUG */
#  endif   /* ANDROID */
#endif     /* FUZZING */

#if defined(XP_WIN)
  // Enable the HeapEnableTerminationOnCorruption exploit mitigation. We ignore
  // the return code because it always returns success, although it has no
  // effect on Windows older than XP SP3.
  HeapSetInformation(NULL, HeapEnableTerminationOnCorruption, NULL, 0);
#endif /* XP_WIN */

#ifdef MOZ_WIDGET_GTK
  // Stash startup token in owned memory because gtk_init will clear
  // DESKTOP_STARTUP_ID it.
  if (const char* v = PR_GetEnv("DESKTOP_STARTUP_ID")) {
    mDesktopStartupID.Assign(v);
  }
  if (const char* v = PR_GetEnv("XDG_ACTIVATION_TOKEN")) {
    mXDGActivationToken.Assign(v);
  }
#endif

#if defined(XP_WIN)
  {
    // Save the shortcut path before lpTitle is replaced by an AUMID,
    // such as by WinTaskbar
    STARTUPINFOW si;
    GetStartupInfoW(&si);
    if (si.dwFlags & STARTF_TITLEISAPPID) {
      NS_WARNING("AUMID was already set, shortcut may have been lost.");
    } else if ((si.dwFlags & STARTF_TITLEISLINKNAME) && si.lpTitle) {
      gProcessStartupShortcut.Assign(si.lpTitle);
    }
  }
#endif /* XP_WIN */

#if defined(MOZ_WIDGET_GTK)
  // setup for private colormap.  Ideally we'd like to do this
  // in nsAppShell::Create, but we need to get in before gtk
  // has been initialized to make sure everything is running
  // consistently.

  // Set program name to the one defined in application.ini.
  g_set_prgname(gAppData->remotingName);

  // Initialize GTK here for splash.

#  if defined(MOZ_WIDGET_GTK) && defined(MOZ_X11)
  // Disable XInput2 multidevice support due to focus bugginess.
  // See bugs 1182700, 1170342.
  // gdk_disable_multidevice() affects Gdk X11 backend only,
  // the multidevice support is always enabled on Wayland backend.
  const char* useXI2 = PR_GetEnv("MOZ_USE_XINPUT2");
  if (!useXI2 || (*useXI2 == '0')) gdk_disable_multidevice();
#  endif

  // Open the display ourselves instead of using gtk_init, so that we can
  // close it without fear that one day gtk might clean up the display it
  // opens.
  if (!gtk_parse_args(&gArgc, &gArgv)) return 1;
#endif /* MOZ_WIDGET_GTK */

#ifdef FUZZING
  if (PR_GetEnv("FUZZER")) {
    *aExitFlag = true;
    return mozilla::fuzzerRunner->Run(&gArgc, &gArgv);
  }
#endif

  if (PR_GetEnv("MOZ_RUN_GTEST")) {
    int result;
#ifdef XP_WIN
    UseParentConsole();
#endif
    // RunGTest will only be set if we're in xul-unit
    if (mozilla::RunGTest) {
      gIsGtest = true;
      result = mozilla::RunGTest(&gArgc, gArgv);
      gIsGtest = false;
    } else {
      result = 1;
      printf("TEST-UNEXPECTED-FAIL | gtest | Not compiled with enable-tests\n");
    }
    *aExitFlag = true;
    return result;
  }

  bool isBackgroundTaskMode = false;
#ifdef MOZ_BACKGROUNDTASKS
  isBackgroundTaskMode = BackgroundTasks::IsBackgroundTaskMode();
#endif

#ifdef MOZ_HAS_REMOTE
  if (gfxPlatform::IsHeadless()) {
    mDisableRemoteClient = true;
  }
#endif

#ifdef MOZ_X11
  // Init X11 in thread-safe mode. Must be called prior to the first call to
  // XOpenDisplay (called inside gdk_display_open). This is a requirement for
  // off main tread compositing.
  if (!isBackgroundTaskMode && !gfxPlatform::IsHeadless()) {
    XInitThreads();
  }
#endif
#if defined(MOZ_WIDGET_GTK)
  if (!isBackgroundTaskMode && !gfxPlatform::IsHeadless()) {
    const char* display_name = nullptr;
    bool saveDisplayArg = false;

    // display_name is owned by gdk.
    display_name = gdk_get_display_arg_name();
    bool waylandEnabled = IsWaylandEnabled();
#  ifdef MOZ_WAYLAND
    if (!display_name) {
      auto* proxyEnv = getenv("MOZ_DISABLE_WAYLAND_PROXY");
      bool disableWaylandProxy = proxyEnv && *proxyEnv;
      if (!disableWaylandProxy && XRE_IsParentProcess() && waylandEnabled) {
        auto* proxyLog = getenv("WAYLAND_PROXY_LOG");
        WaylandProxy::SetVerbose(proxyLog && *proxyLog);
        WaylandProxy::SetCompositorCrashHandler(WlCompositorCrashHandler);
        WaylandProxy::AddState(WAYLAND_PROXY_ENABLED);
        gWaylandProxy = WaylandProxy::Create();
        if (gWaylandProxy) {
          if (!gWaylandProxy->RunThread()) {
            Output(true, "Failed to run Wayland proxy\n");
          }
        }
      } else {
        WaylandProxy::AddState(WAYLAND_PROXY_DISABLED);
      }
    }
#  endif

    // if --display argument is given make sure it's
    // also passed to ContentChild::Init() by MOZ_GDK_DISPLAY.
    if (display_name) {
      SaveWordToEnv("MOZ_GDK_DISPLAY", nsDependentCString(display_name));
      saveDisplayArg = true;
    }

    // On Wayland disabled builds read X11 DISPLAY env exclusively
    // and don't care about different displays.
    if (!waylandEnabled && !display_name) {
      display_name = PR_GetEnv("DISPLAY");
      if (!display_name) {
        PR_fprintf(PR_STDERR,
                   "Error: no DISPLAY environment variable specified\n");
        return 1;
      }
    }

    if (display_name) {
      GdkDisplay* disp = gdk_display_open(display_name);
      if (!disp) {
        PR_fprintf(PR_STDERR, "Error: cannot open display: %s\n", display_name);
        return 1;
      }
      if (saveDisplayArg) {
        if (GdkIsX11Display(disp)) {
          SaveWordToEnv("DISPLAY", nsDependentCString(display_name));
        } else if (GdkIsWaylandDisplay(disp)) {
          SaveWordToEnv("WAYLAND_DISPLAY", nsDependentCString(display_name));
        }
      }
    } else {
      gdk_display_manager_open_display(gdk_display_manager_get(), nullptr);
    }
#  if defined(MOZ_WAYLAND)
    // We want to use proxy for main connection only so
    // restore original Wayland display for next potential Wayland connections
    // from gfx probe code and so on.
    if (gWaylandProxy) {
      gWaylandProxy->RestoreWaylandDisplay();
    }
    if (waylandEnabled && PR_GetEnv("WAYLAND_DISPLAY") && GdkIsX11Display()) {
      // Gtk somehow switched to X11 display but we want Wayland.
      // It may happen if compositor response is missig or it's slow
      // or WAYLAND_DISPLAY is wrong. In such case throw warning but
      // run with X11.
      Output(true,
             "Error: Failed to open Wayland display, fallback to X11. "
             "WAYLAND_DISPLAY='%s' DISPLAY='%s'\n",
             PR_GetEnv("WAYLAND_DISPLAY"), PR_GetEnv("DISPLAY"));

      // We need to unset WAYLAND_DISPLAY. Gfx probe code doesn't have fallback
      // to X11 and we'll end with Browser running SW rendering only then.
      g_unsetenv("WAYLAND_DISPLAY");
      gWaylandProxy = nullptr;
    }
#  endif
    if (!gdk_display_get_default()) {
      Output(true,
             "Error: we don't have any display, WAYLAND_DISPLAY='%s' "
             "DISPLAY='%s'\n",
             PR_GetEnv("WAYLAND_DISPLAY"), PR_GetEnv("DISPLAY"));
      return 1;
    }
    // Check that Wayland only and X11 only builds
    // use appropriate displays.
#  if defined(MOZ_WAYLAND) && !defined(MOZ_X11)
    if (!GdkIsWaylandDisplay()) {
      Output(true, "Wayland only build is missig Wayland display!\n");
      return 1;
    }
#  endif
#  if !defined(MOZ_WAYLAND) && defined(MOZ_X11)
    if (!GdkIsX11Display()) {
      Output(true, "X11 only build is missig X11 display!\n");
      return 1;
    }
#  endif
  }
#endif
#if defined(MOZ_HAS_REMOTE)
  // handle --remote now that xpcom is fired up
  gRemoteService = new nsRemoteService();
  if (gRemoteService) {
    gRemoteService->SetProgram(gAppData->remotingName);
    gStartupLock = gRemoteService->LockStartup();
    if (!gStartupLock) {
      NS_WARNING("Failed to lock for startup, continuing anyway.");
    }
  }
#endif
#if defined(MOZ_WIDGET_GTK)
  g_set_application_name(mAppData->name);

#endif /* defined(MOZ_WIDGET_GTK) */
#ifdef MOZ_X11
  // Do this after initializing GDK, or GDK will install its own handler.
  XRE_InstallX11ErrorHandler();
#endif

  bool canRun = false;
  rv = mNativeApp->Start(&canRun);
  if (NS_FAILED(rv) || !canRun) {
    return 1;
  }

#ifdef MOZ_WIDGET_GTK
  // startup token might be cleared now, we recover it in case we need a
  // restart.
  if (!mDesktopStartupID.IsEmpty()) {
    // Leak it with extreme prejudice!
    PR_SetEnv(ToNewCString("DESKTOP_STARTUP_ID="_ns + mDesktopStartupID));
  }

  if (!mXDGActivationToken.IsEmpty()) {
    // Leak it with extreme prejudice!
    PR_SetEnv(ToNewCString("XDG_ACTIVATION_TOKEN="_ns + mXDGActivationToken));
  }
#endif

  // Support exiting early for testing startup sequence. Bug 1360493
  if (CheckArg("test-launch-without-hang")) {
    *aExitFlag = true;
    return 0;
  }

  mProfileSvc = NS_GetToolkitProfileService();
  if (!mProfileSvc) {
    // We failed to choose or create profile - notify user and quit
    ProfileMissingDialog(mNativeApp);
    return 1;
  }

  bool wasDefaultSelection;
  nsCOMPtr<nsIToolkitProfile> profile;
  rv = SelectProfile(mProfileSvc, mNativeApp, getter_AddRefs(mProfD),
                     getter_AddRefs(mProfLD), getter_AddRefs(profile),
                     &wasDefaultSelection);
  if (rv == NS_ERROR_LAUNCHED_CHILD_PROCESS || rv == NS_ERROR_ABORT) {
    *aExitFlag = true;
    return 0;
  }

  if (NS_FAILED(rv)) {
    // We failed to choose or create profile - notify user and quit
    ProfileMissingDialog(mNativeApp);
    return 1;
  }

#if defined(MOZ_HAS_REMOTE)
  if (gRemoteService) {
    // Use the full profile path to identify the profile.
    nsCString profilePath;

#  ifdef XP_WIN
    nsString path;
    rv = mProfD->GetPath(path);
    if (NS_SUCCEEDED(rv)) {
      CopyUTF16toUTF8(path, profilePath);
    }
#  else
    rv = mProfD->GetNativePath(profilePath);
#  endif

    if (NS_SUCCEEDED(rv)) {
      gRemoteService->SetProfile(profilePath);

      if (!mDisableRemoteClient) {
        // Try to remote the entire command line. If this fails, start up
        // normally.
#  ifdef MOZ_WIDGET_GTK
        auto& startupToken =
            GdkIsWaylandDisplay() ? mXDGActivationToken : mDesktopStartupID;
#    ifdef MOZ_X11
        if (GdkIsX11Display() && startupToken.IsEmpty()) {
          startupToken = SynthesizeStartupToken();
        }
#    endif /* MOZ_X11 */
        gRemoteService->SetStartupToken(startupToken);
#  endif
        rv = gRemoteService->StartClient();

        if (rv == NS_ERROR_NOT_AVAILABLE && profile) {
          // Older versions would use the profile name in preference to the
          // path.
          nsCString profileName;
          profile->GetName(profileName);
          gRemoteService->SetProfile(profileName);

          rv = gRemoteService->StartClient();

          // Reset back to the path.
          gRemoteService->SetProfile(profilePath);
        }

        if (NS_SUCCEEDED(rv)) {
          *aExitFlag = true;
          gStartupLock = nullptr;
          return 0;
        }

        if (rv == NS_ERROR_INVALID_ARG) {
          gStartupLock = nullptr;
          return 1;
        }
      }
    }
  }
#endif /* MOZ_HAS_REMOTE */

#if defined(MOZ_UPDATER) && !defined(MOZ_WIDGET_ANDROID)
#  ifdef XP_WIN
  {
    // When automatically restarting for a staged background update
    // we want the child process to wait here so that the updater
    // does not register two instances running and use that as a
    // reason to not process updates. This function requires having
    // -restart-pid <param> in the command line to function properly.
    // Ensure we keep -restart-pid if we are running tests
    if (ARG_FOUND == CheckArgExists("restart-pid") &&
        !CheckArg("test-only-automatic-restart-no-wait")) {
      // We're not testing and can safely remove it now and read the pid.
      const char* restartPidString = nullptr;
      CheckArg("restart-pid", &restartPidString, CheckArgFlag::RemoveArg);
      // pid should be first parameter following -restart-pid.
      uint32_t pid = nsDependentCString(restartPidString).ToInteger(&rv, 10U);
      if (NS_SUCCEEDED(rv) && pid > 0) {
        printf_stderr(
            "*** MaybeWaitForProcessExit: launched pidDWORD = %u ***\n", pid);
        RefPtr<nsUpdateProcessor> updater = new nsUpdateProcessor();
        if (NS_FAILED(
                updater->WaitForProcessExit(pid, MAYBE_WAIT_TIMEOUT_MS))) {
          NS_WARNING("Failed to MaybeWaitForProcessExit.");
        }
      } else {
        NS_WARNING("Failed to parse pid from -restart-pid.");
      }
    }
  }
#  endif
  // Check for and process any available updates
  nsCOMPtr<nsIFile> updRoot;
  bool persistent;
  rv = mDirProvider.GetFile(XRE_UPDATE_ROOT_DIR, &persistent,
                            getter_AddRefs(updRoot));
  // XRE_UPDATE_ROOT_DIR may fail. Fallback to appDir if failed
  if (NS_FAILED(rv)) {
    updRoot = mDirProvider.GetAppDir();
  }

  Maybe<ShouldNotProcessUpdatesReason> shouldNotProcessUpdatesReason =
      ShouldNotProcessUpdates(mDirProvider);
  if (shouldNotProcessUpdatesReason.isNothing()) {
    nsCOMPtr<nsIFile> exeFile, exeDir;
    rv = mDirProvider.GetFile(XRE_EXECUTABLE_FILE, &persistent,
                              getter_AddRefs(exeFile));
    NS_ENSURE_SUCCESS(rv, 1);
    rv = exeFile->GetParent(getter_AddRefs(exeDir));
    NS_ENSURE_SUCCESS(rv, 1);
    ProcessUpdates(mDirProvider.GetGREDir(), exeDir, updRoot, gRestartArgc,
                   gRestartArgv, mAppData->version);
  } else {
    if (CheckArg("test-should-not-process-updates") ||
        EnvHasValue("MOZ_TEST_SHOULD_NOT_PROCESS_UPDATES")) {
      // Support for testing *not* processing an update.  The launched process
      // can witness this environment variable and conclude that its runtime
      // environment resulted in not processing updates.

      SaveToEnv(nsPrintfCString("MOZ_TEST_SHOULD_NOT_PROCESS_UPDATES="
                                "ShouldNotProcessUpdates(): %s",
                                ShouldNotProcessUpdatesReasonAsString(
                                    shouldNotProcessUpdatesReason.value()))
                    .get());
    }
  }
  if (CheckArg("test-process-updates") ||
      EnvHasValue("MOZ_TEST_PROCESS_UPDATES")) {
    SaveToEnv("MOZ_TEST_PROCESS_UPDATES=");

    // If the caller has asked us to log our arguments, do so.  This is used
    // to make sure that the maintenance service successfully launches the
    // callback application.
    const char* logFile = nullptr;
    if (ARG_FOUND == CheckArg("dump-args", &logFile)) {
      FILE* logFP = fopen(logFile, "wb");
      if (logFP) {
        for (int i = 1; i < gRestartArgc; ++i) {
          fprintf(logFP, "%s\n", gRestartArgv[i]);
        }
        fclose(logFP);
      }
    }

    // Tests may want to know when we are done. We are about to exit, so we are
    // done here. Write out a file to indicate this to the caller.
    WriteUpdateCompleteTestFile(updRoot);

    *aExitFlag = true;
    return 0;
  }
#endif

  // We now know there is no existing instance using the selected profile.

  // We only ever show the profile selector if a specific profile wasn't chosen
  // via command line arguments or environment variables.
  if (wasDefaultSelection) {
    if (!mProfileSvc->GetStartWithLastProfile()) {
      // First check the old style profile manager
      rv = ShowProfileManager(mProfileSvc, mNativeApp);
    } else if (profile && profile->GetShowProfileSelector()) {
      // Now check the new profile group selector
      rv = ShowProfileSelector(mProfileSvc, mNativeApp);
    } else {
      rv = NS_OK;
    }

    if (rv == NS_ERROR_LAUNCHED_CHILD_PROCESS || rv == NS_ERROR_ABORT) {
      *aExitFlag = true;
      return 0;
    }

    if (NS_FAILED(rv)) {
      return 1;
    }
  }

  // We always want to lock the profile even if we're actually going to reset
  // it later.
  rv = LockProfile(mNativeApp, mProfD, mProfLD, profile,
                   getter_AddRefs(mProfileLock));
  if (rv == NS_ERROR_LAUNCHED_CHILD_PROCESS || rv == NS_ERROR_ABORT) {
    *aExitFlag = true;
    return 0;
  }
  if (NS_FAILED(rv)) {
    return 1;
  }

  if (gDoProfileReset) {
    if (!EnvHasValue("MOZ_RESET_PROFILE_RESTART")) {
      if (ARG_FOUND == CheckArgExists("first-startup")) {
        // If the profile reset was initiated by the stub installer, we want to
        // set MOZ_RESET_PROFILE_SESSION so we can check for it later when the
        // Firefox Profile Migrator runs. At that point we set overrides to
        // ensure users see the right homepage.
        SaveToEnv("MOZ_RESET_PROFILE_SESSION=0");
      } else if (gDoMigration) {
        // Otherwise this is a profile reset and migration triggered via the
        // command line during development and we want to restore the session
        // and perform the approriate homepage overrides.
        SaveToEnv("MOZ_RESET_PROFILE_SESSION=1");
      }
    } else {
      // If the profile reset was initiated by the user, such as through
      // about:support, we want to restore their session.
      SaveToEnv("MOZ_RESET_PROFILE_SESSION=1");
      SaveToEnv("MOZ_RESET_PROFILE_RESTART=");
    }
    // Unlock the source profile.
    mProfileLock->Unlock();

    // If we're resetting a profile, create a new one and use it to startup.
    gResetOldProfile = profile;
    rv = mProfileSvc->CreateResetProfile(getter_AddRefs(profile));
    if (NS_SUCCEEDED(rv)) {
      rv = profile->GetRootDir(getter_AddRefs(mProfD));
      NS_ENSURE_SUCCESS(rv, 1);
      SaveFileToEnv("XRE_PROFILE_PATH", mProfD);

      rv = profile->GetLocalDir(getter_AddRefs(mProfLD));
      NS_ENSURE_SUCCESS(rv, 1);
      SaveFileToEnv("XRE_PROFILE_LOCAL_PATH", mProfLD);

      // Lock the new profile
      rv = LockProfile(mNativeApp, mProfD, mProfLD, profile,
                       getter_AddRefs(mProfileLock));
      if (rv == NS_ERROR_LAUNCHED_CHILD_PROCESS || rv == NS_ERROR_ABORT) {
        *aExitFlag = true;
        return 0;
      }
      if (NS_FAILED(rv)) {
        return 1;
      }
    } else {
      NS_WARNING("Profile reset failed.");
      return 1;
    }
  }

  gProfileLock = mProfileLock;

  nsAutoCString version;
  BuildVersion(version);

#ifdef TARGET_OS_ABI
  constexpr auto osABI = nsLiteralCString{TARGET_OS_ABI};
#else
  // No TARGET_XPCOM_ABI, but at least the OS is known
  constexpr auto osABI = nsLiteralCString{OS_TARGET "_UNKNOWN"};
#endif

  // Check for version compatibility with the last version of the app this
  // profile was started with.  The format of the version stamp is defined
  // by the BuildVersion function.
  // Also check to see if something has happened to invalidate our
  // fastload caches, like an app upgrade.

  // If we see .purgecaches, that means someone did a make.
  // Re-register components to catch potential changes.
  nsCOMPtr<nsIFile> flagFile;
  if (mAppData->directory) {
    Unused << mAppData->directory->Clone(getter_AddRefs(flagFile));
  }
  if (flagFile) {
    flagFile->AppendNative(FILE_INVALIDATE_CACHES);
  }

  bool cachesOK;
  bool isDowngrade;
  nsCString lastVersion;
  bool versionOK = CheckCompatibility(
      mProfD, version, osABI, mDirProvider.GetGREDir(), mAppData->directory,
      flagFile, &cachesOK, &isDowngrade, lastVersion);

  MOZ_RELEASE_ASSERT(!cachesOK || lastVersion.Equals(version),
                     "Caches cannot be good if the version has changed.");

#ifdef MOZ_BLOCK_PROFILE_DOWNGRADE
  // The argument check must come first so the argument is always removed from
  // the command line regardless of whether this is a downgrade or not.
  if (!CheckArg("allow-downgrade") && isDowngrade &&
      !EnvHasValue("MOZ_ALLOW_DOWNGRADE")) {
#  ifdef XP_MACOSX
    InitializeMacApp();
#  endif
    rv = CheckDowngrade(mProfD, mNativeApp, mProfileSvc, lastVersion);
    if (rv == NS_ERROR_LAUNCHED_CHILD_PROCESS || rv == NS_ERROR_ABORT) {
      *aExitFlag = true;
      return 0;
    }
  }
#endif

  rv = mDirProvider.SetProfile(mProfD, mProfLD);
  NS_ENSURE_SUCCESS(rv, 1);

  //////////////////////// NOW WE HAVE A PROFILE ////////////////////////

  mozilla::Telemetry::SetProfileDir(mProfD);

  if (mAppData->flags & NS_XRE_ENABLE_CRASH_REPORTER) {
    MakeOrSetMinidumpPath(mProfD);
  }

  CrashReporter::SetProfileDirectory(mProfD);

#ifdef MOZ_ASAN_REPORTER
  // In ASan reporter builds, we need to set ASan's log_path as early as
  // possible, so it dumps its errors into files there instead of using
  // the default stderr location. Since this is crucial for ASan reporter
  // to work at all (and we don't want people to use a non-functional
  // ASan reporter build), all failures while setting log_path are fatal.
  setASanReporterPath(mProfD);

  // Export to env for child processes
  SaveFileToEnv("ASAN_REPORTER_PATH", mProfD);
#endif

  bool lastStartupWasCrash = CheckLastStartupWasCrash();

  CrashReporter::RecordAnnotationBool(
      CrashReporter::Annotation::LastStartupWasCrash, lastStartupWasCrash);

  if (CheckArg("purgecaches") || PR_GetEnv("MOZ_PURGE_CACHES") ||
      lastStartupWasCrash || gSafeMode) {
    cachesOK = false;
  }

  CrashReporter::RecordAnnotationBool(
      CrashReporter::Annotation::StartupCacheValid, cachesOK && versionOK);

  // Every time a profile is loaded by a build with a different version,
  // it updates the compatibility.ini file saying what version last wrote
  // the fastload caches.  On subsequent launches if the version matches,
  // there is no need for re-registration.  If the user loads the same
  // profile in different builds the component registry must be
  // re-generated to prevent mysterious component loading failures.
  //
  bool startupCacheValid = true;

  if (!cachesOK || !versionOK) {
    QuotaManager::InvalidateQuotaCache();

    startupCacheValid = RemoveComponentRegistries(mProfD, mProfLD, false);

    // Rewrite compatibility.ini to match the current build. The next run
    // should attempt to invalidate the caches if either this run is safe mode
    // or the attempt to invalidate the caches this time failed.
    WriteVersion(mProfD, version, osABI, mDirProvider.GetGREDir(),
                 mAppData->directory, gSafeMode || !startupCacheValid);
  }

  if (!startupCacheValid) StartupCache::IgnoreDiskCache();

  if (flagFile) {
    flagFile->Remove(true);
  }

  // Flush any pending page load events.
  mozilla::glean_pings::Pageload.Submit("startup"_ns);

  if (!isBackgroundTaskMode) {
#ifdef USE_GLX_TEST
    GfxInfo::FireGLXTestProcess();
#endif
#ifdef MOZ_WAYLAND
    // Make sure we have wayland connection for main thread.
    // It's used as template to create display connections
    // for different threads.
    if (IsWaylandEnabled()) {
      MOZ_UNUSED(WaylandDisplayGet());
    }
#endif
#ifdef MOZ_WIDGET_GTK
    nsAppShell::InstallTermSignalHandler();
#endif
  }

  return 0;
}

#if defined(MOZ_SANDBOX)
void AddSandboxAnnotations() {
  CrashReporter::RecordAnnotationU32(
      CrashReporter::Annotation::ContentSandboxLevel,
      GetEffectiveContentSandboxLevel());
  CrashReporter::RecordAnnotationU32(CrashReporter::Annotation::GpuSandboxLevel,
                                     GetEffectiveGpuSandboxLevel());

  // Include whether or not this instance is capable of content sandboxing
  bool sSandboxCapable = false;

#  if defined(XP_WIN)
  // All supported Windows versions support some level of content sandboxing
  sSandboxCapable = true;
#  elif defined(XP_MACOSX)
  // All supported OS X versions are capable
  sSandboxCapable = true;
#  elif defined(XP_LINUX)
  sSandboxCapable = SandboxInfo::Get().CanSandboxContent();
#  elif defined(__OpenBSD__)
  sSandboxCapable = true;
  StartOpenBSDSandbox(GeckoProcessType_Default);
#  endif

  CrashReporter::RecordAnnotationBool(
      CrashReporter::Annotation::ContentSandboxCapable, sSandboxCapable);
}
#endif /* MOZ_SANDBOX */

/*
 * XRE_mainRun - Command line startup, profile migration, and
 * the calling of appStartup->Run().
 */
nsresult XREMain::XRE_mainRun() {
  nsresult rv = NS_OK;
  NS_ASSERTION(mScopedXPCOM, "Scoped xpcom not initialized.");

#if defined(XP_WIN)
  RefPtr<mozilla::DllServices> dllServices(mozilla::DllServices::Get());
  dllServices->StartUntrustedModulesProcessor(false);
  auto dllServicesDisable =
      MakeScopeExit([&dllServices]() { dllServices->DisableFull(); });

  mozilla::mscom::InitProfilerMarkers();
#endif  // defined(XP_WIN)

  // We need the appStartup pointer to span multiple scopes, so we declare
  // it here.
  nsCOMPtr<nsIAppStartup> appStartup;
  // Ditto with the command line.
  nsCOMPtr<nsICommandLineRunner> cmdLine;

  {
#ifdef XP_MACOSX
    // In this scope, create an autorelease pool that will leave scope with
    // it just before entering our event loop.
    mozilla::MacAutoreleasePool pool;
#endif

    rv = mScopedXPCOM->SetWindowCreator(mNativeApp);
    NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);

    // tell the crash reporter to also send the release channel
    nsCOMPtr<nsIPrefService> prefs =
        do_GetService("@mozilla.org/preferences-service;1", &rv);
    if (NS_SUCCEEDED(rv)) {
      nsCOMPtr<nsIPrefBranch> defaultPrefBranch;
      rv = prefs->GetDefaultBranch(nullptr, getter_AddRefs(defaultPrefBranch));

      if (NS_SUCCEEDED(rv)) {
        nsAutoCString sval;
        rv = defaultPrefBranch->GetCharPref("app.update.channel", sval);
        if (NS_SUCCEEDED(rv)) {
          CrashReporter::RecordAnnotationNSCString(
              CrashReporter::Annotation::ReleaseChannel, sval);
        }
      }
    }
    // Needs to be set after xpcom initialization.
    bool includeContextHeap = Preferences::GetBool(
        "toolkit.crashreporter.include_context_heap", false);
    CrashReporter::SetIncludeContextHeap(includeContextHeap);

#if defined(XP_LINUX) && !defined(ANDROID)
    PR_CreateThread(PR_USER_THREAD, AnnotateLSBRelease, 0, PR_PRIORITY_LOW,
                    PR_GLOBAL_THREAD, PR_UNJOINABLE_THREAD, 0);
#endif

    if (mStartOffline) {
      nsCOMPtr<nsIIOService> io(
          do_GetService("@mozilla.org/network/io-service;1"));
      NS_ENSURE_TRUE(io, NS_ERROR_FAILURE);
      io->SetManageOfflineStatus(false);
      io->SetOffline(true);
    }

    if (!mOriginToForceQUIC.IsEmpty()) {
      PR_SetEnv(ToNewCString("MOZ_FORCE_QUIC_ON="_ns + mOriginToForceQUIC));
    }

#ifdef XP_WIN
    mozilla::DllPrefetchExperimentRegistryInfo prefetchRegInfo;
    mozilla::AlteredDllPrefetchMode dllPrefetchMode =
        prefetchRegInfo.GetAlteredDllPrefetchMode();

    if (!PR_GetEnv("XRE_NO_DLL_READAHEAD") &&
        dllPrefetchMode != mozilla::AlteredDllPrefetchMode::NoPrefetch) {
      nsCOMPtr<nsIFile> greDir = mDirProvider.GetGREDir();
      nsAutoString path;
      rv = greDir->GetPath(path);
      if (NS_SUCCEEDED(rv)) {
        // We use the presence of a path argument inside the thread to determine
        // which list of Dlls to use. The old list does not need access to the
        // GRE dir, so the path argument is voided.
        if (dllPrefetchMode !=
            mozilla::AlteredDllPrefetchMode::OptimizedPrefetch) {
          path.SetIsVoid(true);
        }

        NS_DispatchBackgroundTask(
            NS_NewRunnableFunction("ReadAheadDlls", [path = std::move(path)] {
              ReadAheadDlls(path.IsVoid() ? nullptr : path.get());
            }));
      }
    }
#endif

    if (gDoMigration) {
      nsCOMPtr<nsIFile> file;
      mDirProvider.GetAppDir()->Clone(getter_AddRefs(file));
      file->AppendNative("override.ini"_ns);
      nsINIParser parser;
      nsresult rv = parser.Init(file);
      // if override.ini doesn't exist, also check for distribution.ini
      if (NS_FAILED(rv)) {
        bool persistent;
        mDirProvider.GetFile(XRE_APP_DISTRIBUTION_DIR, &persistent,
                             getter_AddRefs(file));
        file->AppendNative("distribution.ini"_ns);
        rv = parser.Init(file);
      }
      if (NS_SUCCEEDED(rv)) {
        nsAutoCString buf;
        rv = parser.GetString("XRE", "EnableProfileMigrator", buf);
        if (NS_SUCCEEDED(rv)) {
          if (buf[0] == '0' || buf[0] == 'f' || buf[0] == 'F') {
            gDoMigration = false;
          }
        }
      }
    }

    // We'd like to initialize the JSContext *after* reading the user prefs.
    // Unfortunately that's not possible if we have to do profile migration
    // because that requires us to execute JS before reading user prefs.
    // Restarting the browser after profile migration would fix this. See
    // bug 1592523.
    bool initializedJSContext = false;

    {
      // Profile Migration
      if (mAppData->flags & NS_XRE_ENABLE_PROFILE_MIGRATOR && gDoMigration) {
        gDoMigration = false;

        xpc::InitializeJSContext();
        initializedJSContext = true;

        nsCOMPtr<nsIProfileMigrator> pm(
            do_CreateInstance(NS_PROFILEMIGRATOR_CONTRACTID));
        if (pm) {
          nsAutoCString aKey;
          nsAutoCString aName;
          if (gDoProfileReset) {
            // Automatically migrate from the current application if we just
            // reset the profile.
            aKey = MOZ_APP_NAME;
            gResetOldProfile->GetName(aName);
          }
#ifdef XP_MACOSX
          // Necessary for migration wizard to be accessible.
          InitializeMacApp();
#endif
          pm->Migrate(&mDirProvider, aKey, aName);
        }
      }

      if (gDoProfileReset) {
        if (!initializedJSContext) {
          xpc::InitializeJSContext();
          initializedJSContext = true;
        }

        nsresult backupCreated =
            ProfileResetCleanup(mProfileSvc, gResetOldProfile);
        if (NS_FAILED(backupCreated)) {
          NS_WARNING("Could not cleanup the profile that was reset");
        }
      }
    }

    // Initialize user preferences before notifying startup observers so they're
    // ready in time for early consumers, such as the component loader.
    mDirProvider.InitializeUserPrefs();

    // Now that all (user) prefs have been loaded we can initialize the main
    // thread's JSContext.
    if (!initializedJSContext) {
      xpc::InitializeJSContext();
    }

    // Finally, now that JS has been initialized, we can finish pref loading.
    // This needs to happen after JS and XPConnect initialization because
    // AutoConfig files require JS execution. Note that this means AutoConfig
    // files can't override JS engine start-up prefs.
    mDirProvider.FinishInitializingUserPrefs();

    // Do a canary load of a JS based module here. This will help us detect
    // missing resources during startup and make us react appropriate, that
    // is inform the user before exiting with a crash.
    {
      mozilla::dom::AutoJSAPI jsapi;
      MOZ_ALWAYS_TRUE(jsapi.Init(xpc::PrivilegedJunkScope()));
      JS::Rooted<JSObject*> mod(jsapi.cx());
      // AppConstants.sys.mjs is small, widely used and most likely will
      // never go away.
      rv = mozJSModuleLoader::Get()->ImportESModule(
          jsapi.cx(), "resource://gre/modules/AppConstants.sys.mjs"_ns, &mod);
      if (NS_FAILED(rv)) {
        return NS_ERROR_OMNIJAR_OR_DIR_MISSING;
      }
    }

#if defined(MOZ_SANDBOX) && defined(XP_WIN)
    // Now that we have preferences and the directory provider, we can
    // finish initializing SandboxBroker. This must happen before the GFX
    // platform is initialized (which will launch the GPU process), which
    // occurs when the "app-startup" category is started up below
    //
    // After this completes, we are ready to launch sandboxed processes
    mozilla::SandboxBroker::GeckoDependentInitialize();
#endif

    nsCOMPtr<nsIFile> workingDir;
    rv = NS_GetSpecialDirectory(NS_OS_CURRENT_WORKING_DIR,
                                getter_AddRefs(workingDir));
    if (NS_FAILED(rv)) {
      // No working dir? This can happen if it gets deleted before we start.
      workingDir = nullptr;
    }

    cmdLine = new nsCommandLine();

    rv = cmdLine->Init(gArgc, gArgv, workingDir,
                       nsICommandLine::STATE_INITIAL_LAUNCH);
    NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);

    // "app-startup" is the name of both the category and the event
    NS_CreateServicesFromCategory("app-startup", cmdLine, "app-startup",
                                  nullptr);

    appStartup = components::AppStartup::Service();
    NS_ENSURE_TRUE(appStartup, NS_ERROR_FAILURE);

    mDirProvider.DoStartup();

#ifdef XP_WIN
    // It needs to be called on the main thread because it has to use
    // nsObserverService.
    EnsureWin32kInitialized();
#endif

    // As FilePreferences need the profile directory, we must initialize right
    // here.
    mozilla::FilePreferences::InitDirectoriesAllowlist();
    mozilla::FilePreferences::InitPrefs();

    nsCString userAgentLocale;
    LocaleService::GetInstance()->GetAppLocaleAsBCP47(userAgentLocale);
    CrashReporter::RecordAnnotationNSCString(
        CrashReporter::Annotation::useragent_locale, userAgentLocale);

    if (!AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
      /* Special-case services that need early access to the command
          line. */
      nsCOMPtr<nsIObserverService> obsService =
          mozilla::services::GetObserverService();
      if (obsService) {
        obsService->NotifyObservers(cmdLine, "command-line-startup", nullptr);
      }
    }

#ifdef XP_WIN
    // Hack to sync up the various environment storages. XUL_APP_FILE is special
    // in that it comes from a different CRT (firefox.exe's static-linked copy).
    // Ugly details in http://bugzil.la/1175039#c27
    char appFile[MAX_PATH];
    if (GetEnvironmentVariableA("XUL_APP_FILE", appFile, sizeof(appFile))) {
      SmprintfPointer saved = mozilla::Smprintf("XUL_APP_FILE=%s", appFile);
      // We intentionally leak the string here since it is required by
      // PR_SetEnv.
      PR_SetEnv(saved.release());
    }
#endif

    mozilla::AppShutdown::SaveEnvVarsForPotentialRestart();

    // clear out any environment variables which may have been set
    // during the relaunch process now that we know we won't be relaunching.
    SaveToEnv("XRE_PROFILE_PATH=");
    SaveToEnv("XRE_PROFILE_LOCAL_PATH=");
    SaveToEnv("XRE_START_OFFLINE=");
    SaveToEnv("XUL_APP_FILE=");
    SaveToEnv("XRE_BINARY_PATH=");
    SaveToEnv("XRE_RESTARTED_BY_PROFILE_MANAGER=");

    if (!AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
#ifdef XP_MACOSX
      bool isBackgroundTaskMode = false;
#  ifdef MOZ_BACKGROUNDTASKS
      isBackgroundTaskMode = BackgroundTasks::IsBackgroundTaskMode();
#  endif
      if (!isBackgroundTaskMode) {
        rv = appStartup->CreateHiddenWindow();
        NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);
      }
#endif

#ifdef XP_WIN
      Preferences::RegisterCallbackAndCall(
          RegisterApplicationRestartChanged,
          PREF_WIN_REGISTER_APPLICATION_RESTART);
      SetupAlteredPrefetchPref();
      SetupSkeletonUIPrefs();
#  if defined(MOZ_LAUNCHER_PROCESS)
      SetupLauncherProcessPref();
#  endif  // defined(MOZ_LAUNCHER_PROCESS)
#  if defined(MOZ_DEFAULT_BROWSER_AGENT)
#    if defined(MOZ_BACKGROUNDTASKS)
      // The backgroundtask profile is not a browsing profile, let alone the new
      // default profile, so don't mirror its properties into the registry.
      if (!BackgroundTasks::IsBackgroundTaskMode())
#    endif  // defined(MOZ_BACKGROUNDTASKS)
      {
        Preferences::RegisterCallbackAndCall(
            &OnDefaultAgentTelemetryPrefChanged,
            kPrefHealthReportUploadEnabled);
        Preferences::RegisterCallbackAndCall(
            &OnDefaultAgentTelemetryPrefChanged, kPrefDefaultAgentEnabled);

        Preferences::RegisterCallbackAndCall(
            &OnDefaultAgentRemoteSettingsPrefChanged,
            kPrefServicesSettingsServer);

        Preferences::RegisterCallbackAndCall(
            &OnSetDefaultBrowserUserChoicePrefChanged,
            kPrefSetDefaultBrowserUserChoicePref);

        SetDefaultAgentLastRunTime();
      }
#  endif  // defined(MOZ_DEFAULT_BROWSER_AGENT)
#endif

#if defined(MOZ_WIDGET_GTK)
      // Clear the environment variables so they won't be inherited by child
      // processes and confuse things.
      for (const auto& name : kStartupTokenNames) {
        g_unsetenv(name.get());
      }
#endif

#ifdef XP_MACOSX
      InitializeMacApp();

      // we re-initialize the command-line service and do appleevents munging
      // after we are sure that we're not restarting
      cmdLine = new nsCommandLine();

      char** tempArgv = static_cast<char**>(malloc(gArgc * sizeof(char*)));
      for (int i = 0; i < gArgc; i++) {
        tempArgv[i] = strdup(gArgv[i]);
      }
      CommandLineServiceMac::SetupMacCommandLine(gArgc, tempArgv, false);
      rv = cmdLine->Init(gArgc, tempArgv, workingDir,
                         nsICommandLine::STATE_INITIAL_LAUNCH);
      free(tempArgv);
      NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);

#  ifdef MOZILLA_OFFICIAL
      // Check if we're running from a DMG or an app translocated location and
      // allow the user to install to the Applications directory.
      if (MacRunFromDmgUtils::MaybeInstallAndRelaunch()) {
        bool userAllowedQuit = true;
        appStartup->Quit(nsIAppStartup::eForceQuit, 0, &userAllowedQuit);
      }
#  endif
#endif

      nsCOMPtr<nsIObserverService> obsService =
          mozilla::services::GetObserverService();
      if (obsService)
        obsService->NotifyObservers(nullptr, "final-ui-startup", nullptr);

      (void)appStartup->DoneStartingUp();

      CrashReporter::RecordAnnotationBool(
          CrashReporter::Annotation::StartupCrash, false);

      AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed);
    }

    if (!AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
      rv = cmdLine->Run();
      NS_ENSURE_SUCCESS_LOG(rv, NS_ERROR_FAILURE);
    }

    if (!AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
#if defined(MOZ_HAS_REMOTE)
      // if we have X remote support, start listening for requests on the
      // proxy window.
      if (gRemoteService) {
        gRemoteService->StartupServer();
        gStartupLock = nullptr;
      }
#endif

      mNativeApp->Enable();
    }

    // Send Telemetry about Gecko version and buildid
    mozilla::glean::gecko::version.Set(nsDependentCString(gAppData->version));
    mozilla::glean::gecko::build_id.Set(nsDependentCString(gAppData->buildID));

#if defined(MOZ_SANDBOX) && defined(XP_LINUX)
    // If we're on Linux, we now have information about the OS capabilities
    // available to us.
    SandboxInfo sandboxInfo = SandboxInfo::Get();
    glean::sandbox::has_user_namespaces
        .EnumGet(static_cast<glean::sandbox::HasUserNamespacesLabel>(
            sandboxInfo.Test(SandboxInfo::kHasUserNamespaces)))
        .Add();

    CrashReporter::RecordAnnotationU32(
        CrashReporter::Annotation::ContentSandboxCapabilities,
        sandboxInfo.AsInteger());
#endif /* MOZ_SANDBOX && XP_LINUX */

#if defined(XP_WIN)
    LauncherResult<bool> isAdminWithoutUac = IsAdminWithoutUac();
    if (isAdminWithoutUac.isOk()) {
      glean::os_environment::is_admin_without_uac.Set(
          isAdminWithoutUac.unwrap());
    }
#endif /* XP_WIN */

#if defined(MOZ_SANDBOX)
    AddSandboxAnnotations();
#endif /* MOZ_SANDBOX */

    mProfileSvc->CompleteStartup();
  }

#ifdef MOZ_BACKGROUNDTASKS
  if (BackgroundTasks::IsBackgroundTaskMode()) {
    // In background task mode, we don't fire various delayed initialization
    // notifications, which in the regular browser is how startup crash tracking
    // is marked as finished.  Here, getting this far means we don't have a
    // startup crash.
    rv = appStartup->TrackStartupCrashEnd();
    NS_ENSURE_SUCCESS(rv, rv);

    // We never open a window, but don't want to exit immediately.
    rv = appStartup->EnterLastWindowClosingSurvivalArea();
    NS_ENSURE_SUCCESS(rv, rv);

    // Avoid some small differences in initialization order across platforms.
    nsCOMPtr<nsIPowerManagerService> powerManagerService =
        do_GetService(POWERMANAGERSERVICE_CONTRACTID);
    nsCOMPtr<nsIStringBundleService> stringBundleService =
        do_GetService(NS_STRINGBUNDLE_CONTRACTID);

    rv = BackgroundTasks::RunBackgroundTask(cmdLine);
    NS_ENSURE_SUCCESS(rv, rv);
  }
#endif

  {
    rv = appStartup->Run();
    if (NS_FAILED(rv)) {
      NS_ERROR("failed to run appstartup");
      gLogConsoleErrors = true;
    }
  }

  return rv;
}

#if defined(MOZ_WIDGET_ANDROID)
static already_AddRefed<nsIFile> GreOmniPath(int argc, char** argv) {
  nsresult rv;

  const char* path = nullptr;
  ArgResult ar = CheckArg(argc, argv, "greomni", &path, CheckArgFlag::None);
  if (ar == ARG_BAD) {
    PR_fprintf(PR_STDERR,
               "Error: argument --greomni requires a path argument\n");
    return nullptr;
  }

  if (!path) return nullptr;

  nsCOMPtr<nsIFile> greOmni;
  rv = XRE_GetFileFromPath(path, getter_AddRefs(greOmni));
  if (NS_FAILED(rv)) {
    PR_fprintf(PR_STDERR, "Error: argument --greomni requires a valid path\n");
    return nullptr;
  }

  return greOmni.forget();
}
#endif

/*
 * XRE_main - A class based main entry point used by most platforms.
 *            Note that on OSX, aAppData->xreDirectory will point to
 *            .app/Contents/Resources.
 */
int XREMain::XRE_main(int argc, char* argv[], const BootstrapConfig& aConfig) {
  gArgc = argc;
  gArgv = argv;

  ScopedLogging log;

  mozilla::LogModule::Init(gArgc, gArgv);

#ifndef XP_LINUX
  NS_SetCurrentThreadName("MainThread");
#endif

  AUTO_BASE_PROFILER_LABEL("XREMain::XRE_main (around Gecko Profiler)", OTHER);
  AUTO_PROFILER_INIT;
  AUTO_PROFILER_LABEL("XREMain::XRE_main", OTHER);

#ifdef XP_MACOSX
  // We call this early because it will kick off a background-thread task
  // to register the fonts, and we'd like it to have a chance to complete
  // before gfxPlatform initialization actually requires it.
  gfxPlatformMac::RegisterSupplementalFonts();
#endif

#ifdef MOZ_WIDGET_ANDROID
  // We call the early because we run system font API on a background-thread
  // to initialize internal font API data in Android. (Bug 1895926)
  gfxAndroidPlatform::InitializeFontAPI();
#endif

#ifdef MOZ_CODE_COVERAGE
  CodeCoverageHandler::Init();
#endif

  nsresult rv = NS_OK;

  if (aConfig.appData) {
    mAppData = MakeUnique<XREAppData>(*aConfig.appData);
  } else {
    MOZ_RELEASE_ASSERT(aConfig.appDataPath);
    nsCOMPtr<nsIFile> appini;
    rv = XRE_GetFileFromPath(aConfig.appDataPath, getter_AddRefs(appini));
    if (NS_FAILED(rv)) {
      Output(true, "Error: unrecognized path: %s\n", aConfig.appDataPath);
      return 1;
    }

    mAppData = MakeUnique<XREAppData>();
    rv = XRE_ParseAppData(appini, *mAppData);
    if (NS_FAILED(rv)) {
      Output(true, "Couldn't read application.ini");
      return 1;
    }

    appini->GetParent(getter_AddRefs(mAppData->directory));
  }

  const char* appRemotingName = getenv("MOZ_APP_REMOTINGNAME");
  if (appRemotingName) {
    mAppData->remotingName = appRemotingName;
  } else if (!mAppData->remotingName) {
    mAppData->remotingName = mAppData->name;
  }
  // used throughout this file
  gAppData = mAppData.get();

  nsCOMPtr<nsIFile> binFile;
  rv = XRE_GetBinaryPath(getter_AddRefs(binFile));
  NS_ENSURE_SUCCESS(rv, 1);

  rv = binFile->GetPath(gAbsoluteArgv0Path);
  NS_ENSURE_SUCCESS(rv, 1);

  if (!mAppData->xreDirectory) {
    nsCOMPtr<nsIFile> greDir;

#if defined(MOZ_WIDGET_ANDROID)
    greDir = GreOmniPath(argc, argv);
    if (!greDir) {
      return 2;
    }
#else
    rv = binFile->GetParent(getter_AddRefs(greDir));
    if (NS_FAILED(rv)) return 2;
#endif

#ifdef XP_MACOSX
    nsCOMPtr<nsIFile> parent;
    greDir->GetParent(getter_AddRefs(parent));
    greDir = parent.forget();
    greDir->AppendNative("Resources"_ns);
#endif

    mAppData->xreDirectory = greDir;
  }

#if defined(MOZ_WIDGET_ANDROID)
  nsCOMPtr<nsIFile> dataDir;
  rv = binFile->GetParent(getter_AddRefs(dataDir));
  if (NS_FAILED(rv)) return 2;

  mAppData->directory = dataDir;
#else
  if (aConfig.appData && aConfig.appDataPath) {
    mAppData->xreDirectory->Clone(getter_AddRefs(mAppData->directory));
    mAppData->directory->AppendNative(nsDependentCString(aConfig.appDataPath));
  }
#endif

  if (!mAppData->directory) {
    mAppData->directory = mAppData->xreDirectory;
  }

#if defined(XP_WIN)
#  if defined(MOZ_SANDBOX)
  mAppData->sandboxBrokerServices = aConfig.sandboxBrokerServices;
#  endif  // defined(MOZ_SANDBOX)

  {
    DebugOnly<bool> result = WindowsBCryptInitialization();
    MOZ_ASSERT(result);
  }

#  if defined(_M_IX86) || defined(_M_X64)
  {
    DebugOnly<bool> result = WindowsMsctfInitialization();
    MOZ_ASSERT(result);
  }
#  endif  // _M_IX86 || _M_X64

  // Bug 1924623: Detouring VariantClear resulted in a huge crash spike for
  //              Thunderbird, so let's only do that for Firefox.
#  ifdef MOZ_BUILD_APP_IS_BROWSER
  {
    DebugOnly<bool> result = WindowsOleAut32Initialization();
    MOZ_ASSERT(result);
  }
#  endif  // MOZ_BUILD_APP_IS_BROWSER

#endif  // defined(XP_WIN)

  // Once we unset the exception handler, we lose the ability to properly
  // detect hangs -- they show up as crashes.  We do this as late as possible.
  // In particular, after ProcessRuntime is destroyed on Windows.
  auto unsetExceptionHandler = MakeScopeExit([&] {
    if (mAppData->flags & NS_XRE_ENABLE_CRASH_REPORTER)
      return CrashReporter::UnsetExceptionHandler();
    return NS_OK;
  });

#if defined(MOZ_WIDGET_ANDROID)
  CrashReporter::SetCrashHelperPipes(aConfig.crashChildNotificationSocket,
                                     aConfig.crashHelperSocket);
#endif  // defined(MOZ_WIDGET_ANDROID)

  mozilla::AutoIOInterposer ioInterposerGuard;
  ioInterposerGuard.Init();

#if defined(XP_WIN)
  // We should have already done this when we created the skeleton UI. However,
  // there is code in here which needs xul in order to work, like EnsureMTA. It
  // should be setup that running it again is safe.
  mozilla::mscom::ProcessRuntime msCOMRuntime;
#endif

  // init
  bool exit = false;
  int result = XRE_mainInit(&exit);
  if (result != 0 || exit) return result;

  // If we exit gracefully, remove the startup crash canary file.
  auto cleanup = MakeScopeExit([&]() -> nsresult {
    if (mProfLD) {
      nsCOMPtr<nsIFile> crashFile;
      MOZ_TRY_VAR(crashFile, GetIncompleteStartupFile(mProfLD));
      crashFile->Remove(false);
    }
    return NS_OK;
  });

  // startup
  result = XRE_mainStartup(&exit);
  if (result != 0 || exit) return result;

  // Start the real application. We use |aInitJSContext = false| because
  // XRE_mainRun wants to initialize the JSContext after reading user prefs.

  mScopedXPCOM = MakeUnique<ScopedXPCOMStartup>();

  rv = mScopedXPCOM->Initialize(/* aInitJSContext = */ false);
  if (rv == NS_ERROR_OMNIJAR_CORRUPT) {
    if (XRE_IsParentProcess()
#ifdef MOZ_BACKGROUNDTASKS
        && !mozilla::BackgroundTasks::IsBackgroundTaskMode()
#endif
    ) {
      Output(
          true,
          "The installation seems to be corrupt.\nPlease check your hardware "
          "and disk setup\nand/or re-install.\n");
    }
    MOZ_CRASH("NS_ERROR_OMNIJAR_CORRUPT");
  }
  NS_ENSURE_SUCCESS(rv, 1);

  // run!
  rv = XRE_mainRun();
  if (rv == NS_ERROR_OMNIJAR_OR_DIR_MISSING) {
    if (XRE_IsParentProcess()
#ifdef MOZ_BACKGROUNDTASKS
        && !mozilla::BackgroundTasks::IsBackgroundTaskMode()
#endif
    ) {
      Output(true,
             "The installation seems to be incomplete.\nPlease check your "
             "hardware and disk setup\nand/or re-install.\n");
    }
    if (mozilla::IsPackagedBuild()) {
      // IsPackagedBuild just looks for omni.ja on disk. If we were able to
      // find and open it before but not to load the expected JS module from
      // it now, signal corruption.
      MOZ_CRASH("NS_ERROR_OMNIJAR_CORRUPT");
    }
    MOZ_CRASH("NS_ERROR_OMNIJAR_OR_DIR_MISSING");
  }

#ifdef MOZ_X11
  XRE_CleanupX11ErrorHandler();
#endif

  gAbsoluteArgv0Path.Truncate();

#if defined(MOZ_HAS_REMOTE)
  // Shut down the remote service. We must do this before calling LaunchChild
  // if we're restarting because otherwise the new instance will attempt to
  // remote to this instance.
  if (gRemoteService) {
    gRemoteService->ShutdownServer();
    gStartupLock = nullptr;
    gRemoteService = nullptr;
  }
#endif

  mScopedXPCOM = nullptr;

  // unlock the profile after ScopedXPCOMStartup object (xpcom)
  // has gone out of scope.  see bug #386739 for more details
  mProfileLock->Unlock();
  gProfileLock = nullptr;

  gLastAppVersion.Truncate();
  gLastAppBuildID.Truncate();

#ifdef MOZ_WIDGET_GTK
  // gdk_display_close also calls gdk_display_manager_set_default_display
  // appropriately when necessary.
  if (!gfxPlatform::IsHeadless()) {
#  ifdef MOZ_WAYLAND
    WaylandDisplayRelease();
    gWaylandProxy = nullptr;
#  endif
  }
#endif

  mozilla::AppShutdown::MaybeDoRestart();

  XRE_DeinitCommandLine();

  if (NS_FAILED(rv)) {
    return 1;
  }
  return mozilla::AppShutdown::GetExitCode();
}

void XRE_StopLateWriteChecks(void) { mozilla::StopLateWriteChecks(); }

int XRE_main(int argc, char* argv[], const BootstrapConfig& aConfig) {
  XREMain main;

  int result = main.XRE_main(argc, argv, aConfig);
  mozilla::RecordShutdownEndTimeStamp();
#ifdef MOZ_BACKGROUNDTASKS
  // This is well after the profile has been unlocked, so it's okay if this does
  // delete this background task's temporary profile.
  mozilla::BackgroundTasks::Shutdown();
#endif
  return result;
}

nsresult XRE_InitCommandLine(int aArgc, char* aArgv[]) {
  nsresult rv = NS_OK;

#if defined(XP_WIN)
  CommandLine::Init(aArgc, aArgv);
#else

  // these leak on error, but that's OK: we'll just exit()
  char** canonArgs = new char*[aArgc];

  // get the canonical version of the binary's path
  nsCOMPtr<nsIFile> binFile;
  rv = XRE_GetBinaryPath(getter_AddRefs(binFile));
  if (NS_FAILED(rv)) return NS_ERROR_FAILURE;

  nsAutoCString canonBinPath;
  rv = binFile->GetNativePath(canonBinPath);
  if (NS_FAILED(rv)) return NS_ERROR_FAILURE;

  canonArgs[0] = strdup(canonBinPath.get());

  for (int i = 1; i < aArgc; ++i) {
    if (aArgv[i]) {
      canonArgs[i] = strdup(aArgv[i]);
    }
  }

  NS_ASSERTION(!CommandLine::IsInitialized(), "Bad news!");
  CommandLine::Init(aArgc, canonArgs);

  for (int i = 0; i < aArgc; ++i) free(canonArgs[i]);
  delete[] canonArgs;
#endif

#if defined(MOZ_WIDGET_ANDROID)
  // gAppData is non-null iff this is the parent process.  Otherwise,
  // the `-greomni`/`-appomni` flags are cross-platform and handled in
  // ContentProcess::Init.
  if (gAppData) {
    nsCOMPtr<nsIFile> greOmni = gAppData->xreDirectory;
    if (!greOmni) {
      return NS_ERROR_FAILURE;
    }
    mozilla::Omnijar::Init(greOmni, greOmni);
    MOZ_RELEASE_ASSERT(IsPackagedBuild(), "Android builds are always packaged");
  }
#endif

  return rv;
}

nsresult XRE_DeinitCommandLine() {
  nsresult rv = NS_OK;

  CommandLine::Terminate();

  return rv;
}

GeckoProcessType XRE_GetProcessType() { return GetGeckoProcessType(); }

const char* XRE_GetProcessTypeString() {
  return XRE_GeckoProcessTypeToString(XRE_GetProcessType());
}

GeckoChildID XRE_GetChildID() { return GetGeckoChildID(); }

bool XRE_IsE10sParentProcess() {
#ifdef MOZ_WIDGET_ANDROID
  return XRE_IsParentProcess() && BrowserTabsRemoteAutostart() &&
         mozilla::jni::IsAvailable();
#else
  return XRE_IsParentProcess() && BrowserTabsRemoteAutostart();
#endif
}

#define GECKO_PROCESS_TYPE(enum_value, enum_name, string_name, proc_typename, \
                           process_bin_type, procinfo_typename,               \
                           webidl_typename, allcaps_name)                     \
  bool XRE_Is##proc_typename##Process() {                                     \
    return XRE_GetProcessType() == GeckoProcessType_##enum_name;              \
  }
#include "mozilla/GeckoProcessTypes.h"
#undef GECKO_PROCESS_TYPE

bool XRE_UseNativeEventProcessing() {
#if defined(XP_WIN)
  // If win32k is locked down we can't use native event processing.
  if (IsWin32kLockedDown()) {
    return false;
  }
#endif

  switch (XRE_GetProcessType()) {
#if defined(XP_MACOSX) || defined(XP_WIN)
    case GeckoProcessType_RDD:
    case GeckoProcessType_Socket:
      return false;
    case GeckoProcessType_Utility: {
#  if defined(XP_WIN)
      auto upc = mozilla::ipc::UtilityProcessChild::Get();
      MOZ_ASSERT(upc);

      using SboxKind = mozilla::ipc::SandboxingKind;
      // These processes are used as external hosts for accessing Windows
      // APIs which (may) require a Windows native event loop.
      return upc->mSandbox == SboxKind::WINDOWS_UTILS ||
             upc->mSandbox == SboxKind::WINDOWS_FILE_DIALOG;
#  else
      return false;
#  endif  // defined(XP_WIN)
    }
#endif  // defined(XP_MACOSX) || defined(XP_WIN)
    case GeckoProcessType_GMPlugin:
      return mozilla::gmp::GMPProcessChild::UseNativeEventProcessing();
    case GeckoProcessType_Content:
      return StaticPrefs::dom_ipc_useNativeEventProcessing_content();
    default:
      return true;
  }
}

namespace mozilla {

uint32_t GetMaxWebProcessCount() {
  // multiOptOut is in int to allow us to run multiple experiments without
  // introducing multiple prefs a la the autostart.N prefs.
  if (Preferences::GetInt("dom.ipc.multiOptOut", 0) >=
      nsIXULRuntime::E10S_MULTI_EXPERIMENT) {
    return 1;
  }

  const char* optInPref = "dom.ipc.processCount";
  uint32_t optInPrefValue = Preferences::GetInt(optInPref, 1);
  return std::max(1u, optInPrefValue);
}

const char* PlatformBuildID() { return gToolkitBuildID; }

}  // namespace mozilla

void SetupErrorHandling(const char* progname) {
#ifdef XP_WIN
  /* On Windows XPSP3 and Windows Vista if DEP is configured off-by-default
     we still want DEP protection: enable it explicitly and programmatically.

     This function is not available on WinXPSP2 so we dynamically load it.
  */

  HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
  SetProcessDEPPolicyFunc _SetProcessDEPPolicy =
      (SetProcessDEPPolicyFunc)GetProcAddress(kernel32, "SetProcessDEPPolicy");
  if (_SetProcessDEPPolicy) _SetProcessDEPPolicy(PROCESS_DEP_ENABLE);
#endif

#ifdef XP_WIN
  // Suppress the "DLL Foo could not be found" dialog, such that if dependent
  // libraries (such as GDI+) are not preset, we gracefully fail to load those
  // XPCOM components, instead of being ungraceful.
  UINT realMode = SetErrorMode(0);
  realMode |= SEM_FAILCRITICALERRORS;
  // If XRE_NO_WINDOWS_CRASH_DIALOG is set, suppress displaying the "This
  // application has crashed" dialog box.  This is mainly useful for
  // automated testing environments, e.g. tinderbox, where there's no need
  // for a dozen of the dialog boxes to litter the console
  if (getenv("XRE_NO_WINDOWS_CRASH_DIALOG"))
    realMode |= SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX;

  SetErrorMode(realMode);

#endif

  InstallSignalHandlers(progname);

  // Unbuffer stdout, needed for tinderbox tests.
  setbuf(stdout, 0);
}

static bool gRunSelfAsContentProc = false;

void XRE_EnableSameExecutableForContentProc() {
  if (!PR_GetEnv("MOZ_SEPARATE_CHILD_PROCESS")) {
    gRunSelfAsContentProc = true;
  }
}

mozilla::BinPathType XRE_GetChildProcBinPathType(
    GeckoProcessType aProcessType) {
  MOZ_ASSERT(aProcessType != GeckoProcessType_Default);

  if (!gRunSelfAsContentProc) {
    return BinPathType::PluginContainer;
  }

#ifdef XP_WIN
  // On Windows, plugin-container may or may not be used depending on
  // the process type (e.g., actual plugins vs. content processes)
  switch (aProcessType) {
#  define GECKO_PROCESS_TYPE(enum_value, enum_name, string_name,               \
                             proc_typename, process_bin_type,                  \
                             procinfo_typename, webidl_typename, allcaps_name) \
    case GeckoProcessType_##enum_name:                                         \
      return BinPathType::process_bin_type;
#  include "mozilla/GeckoProcessTypes.h"
#  undef GECKO_PROCESS_TYPE
    default:
      return BinPathType::PluginContainer;
  }
#else
  // On (non-macOS) Unix, plugin-container isn't used (except in cases
  // like xpcshell that are handled by the gRunSelfAsContentProc check
  // above).  It isn't necessary the way it is on other platforms, and
  // it interferes with using the fork server.
  return BinPathType::Self;
#endif
}

// From mozglue/static/rust/lib.rs
extern "C" void install_rust_hooks();

struct InstallRustHooks {
  InstallRustHooks() { install_rust_hooks(); }
};

MOZ_RUNINIT InstallRustHooks sInstallRustHooks;

#ifdef MOZ_ASAN_REPORTER
void setASanReporterPath(nsIFile* aDir) {
  nsCOMPtr<nsIFile> dir;
  aDir->Clone(getter_AddRefs(dir));

  dir->Append(u"asan"_ns);
  nsresult rv = dir->Create(nsIFile::DIRECTORY_TYPE, 0700);
  if (NS_WARN_IF(NS_FAILED(rv) && rv != NS_ERROR_FILE_ALREADY_EXISTS)) {
    MOZ_CRASH("[ASan Reporter] Unable to create crash directory.");
  }

  dir->Append(u"ff_asan_log"_ns);

#  ifdef XP_WIN
  nsAutoString nspathW;
  rv = dir->GetPath(nspathW);
  NS_ConvertUTF16toUTF8 nspath(nspathW);
#  else
  nsAutoCString nspath;
  rv = dir->GetNativePath(nspath);
#  endif
  if (NS_FAILED(rv)) {
    MOZ_CRASH("[ASan Reporter] Unable to get native path for crash directory.");
  }

  __sanitizer_set_report_path(nspath.get());
}
#endif
