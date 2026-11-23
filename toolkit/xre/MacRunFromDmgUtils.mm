/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <AppKit/AppKit.h>
#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>
#include <Foundation/Foundation.h>
#include <IOKit/IOKitLib.h>
#include <os/log.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/param.h>

#include "MacLaunchHelper.h"
#include "MacRunFromDmgUtils.h"

#include "mozilla/ErrorResult.h"
#include "mozilla/intl/Localization.h"
#include "mozilla/Telemetry.h"
#include "nsCocoaFeatures.h"
#include "nsCocoaUtils.h"
#include "nsCommandLine.h"
#include "nsCommandLineServiceMac.h"
#include "nsILocalFileMac.h"
#include "nsIMacDockSupport.h"
#include "nsObjCExceptions.h"
#include "prenv.h"
#include "nsString.h"
#ifdef MOZ_UPDATER
#  include "nsUpdateDriver.h"
#endif

// Exported from toolkit/mozapps/dmgInstallHelper
#import "dmgInstallProtocol.h"

#ifndef MOZ_RUNINIT
#  define MOZ_RUNINIT __attribute__((annotate("moz_global_var")))
#endif

MOZ_RUNINIT static const os_log_t runFromDMGLogger =
    os_log_create("org.mozilla.firefox", "runFromDMG");

// For IOKit docs, see:
// https://developer.apple.com/documentation/iokit
// https://developer.apple.com/library/archive/documentation/DeviceDrivers/Conceptual/IOKitFundamentals/

using namespace mozilla::MacLaunchHelper;

namespace mozilla::MacRunFromDmgUtils {

/**
 * Opens a dialog to ask the user whether the existing app in the Applications
 * folder should be launched, or if the user wants to proceed with launching
 * the app from the .dmg.
 * Returns true if the dialog is successfully opened and the user chooses to
 * launch the app from the Applications folder, otherwise returns false.
 */
static bool AskUserIfWeShouldLaunchExistingInstall() {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  // Try to get the localized strings:
  nsTArray<nsCString> resIds = {
      "branding/brand.ftl"_ns,
      "toolkit/global/run-from-dmg.ftl"_ns,
  };
  RefPtr<intl::Localization> l10n = intl::Localization::Create(resIds, true);

  ErrorResult rv;
  nsAutoCString mozTitle, mozMessage, mozLaunchExisting, mozLaunchFromDMG;
  l10n->FormatValueSync("prompt-to-launch-existing-app-title"_ns, mozTitle, rv);
  if (rv.Failed()) {
    return false;
  }
  l10n->FormatValueSync("prompt-to-launch-existing-app-message"_ns, mozMessage,
                        rv);
  if (rv.Failed()) {
    return false;
  }
  l10n->FormatValueSync("prompt-to-launch-existing-app-yes-button"_ns,
                        mozLaunchExisting, rv);
  if (rv.Failed()) {
    return false;
  }
  l10n->FormatValueSync("prompt-to-launch-existing-app-no-button"_ns,
                        mozLaunchFromDMG, rv);
  if (rv.Failed()) {
    return false;
  }

  NSString* title = [NSString
      stringWithUTF8String:reinterpret_cast<const char*>(mozTitle.get())];
  NSString* message = [NSString
      stringWithUTF8String:reinterpret_cast<const char*>(mozMessage.get())];
  NSString* launchExisting =
      [NSString stringWithUTF8String:reinterpret_cast<const char*>(
                                         mozLaunchExisting.get())];
  NSString* launchFromDMG =
      [NSString stringWithUTF8String:reinterpret_cast<const char*>(
                                         mozLaunchFromDMG.get())];

  NSAlert* alert = [[[NSAlert alloc] init] autorelease];

  // Note that we don't set an icon since the app icon is used by default.
  [alert setAlertStyle:NSAlertStyleInformational];
  [alert setMessageText:title];
  [alert setInformativeText:message];
  // Note that if the user hits 'Enter' the "Install" button is activated,
  // whereas if they hit  'Space' the "Don't Install" button is activated.
  // That's standard behavior so probably desirable.
  [alert addButtonWithTitle:launchExisting];
  NSButton* launchFromDMGButton = [alert addButtonWithTitle:launchFromDMG];
  // Since the "Don't Install" button doesn't have the title "Cancel" we need
  // to map the Escape key to it manually:
  [launchFromDMGButton setKeyEquivalent:@"\e"];

  __block NSInteger result = -1;
  dispatch_async(dispatch_get_main_queue(), ^{
    result = [alert runModal];
    [NSApp stop:nil];
  });

  // We need to call run on NSApp here for accessibility. See
  // AskUserIfWeShouldInstall for a detailed explanation.
  [NSApp run];
  MOZ_ASSERT(result != -1);

  return result == NSAlertFirstButtonReturn;

  NS_OBJC_END_TRY_BLOCK_RETURN(false);
}

/**
 * Opens a dialog to ask the user whether the app should be installed to their
 * Applications folder.  Returns true if the dialog is successfully opened and
 * the user accept, otherwise returns false.
 */
static bool AskUserIfWeShouldInstall() {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  // Try to get the localized strings:
  nsTArray<nsCString> resIds = {
      "branding/brand.ftl"_ns,
      "toolkit/global/run-from-dmg.ftl"_ns,
  };
  RefPtr<intl::Localization> l10n = intl::Localization::Create(resIds, true);

  ErrorResult rv;
  nsAutoCString mozTitle, mozMessage, mozInstall, mozDontInstall;
  l10n->FormatValueSync("prompt-to-install-title"_ns, mozTitle, rv);
  if (rv.Failed()) {
    return false;
  }
  l10n->FormatValueSync("prompt-to-install-message"_ns, mozMessage, rv);
  if (rv.Failed()) {
    return false;
  }
  l10n->FormatValueSync("prompt-to-install-yes-button"_ns, mozInstall, rv);
  if (rv.Failed()) {
    return false;
  }
  l10n->FormatValueSync("prompt-to-install-no-button"_ns, mozDontInstall, rv);
  if (rv.Failed()) {
    return false;
  }

  NSString* title = [NSString
      stringWithUTF8String:reinterpret_cast<const char*>(mozTitle.get())];
  NSString* message = [NSString
      stringWithUTF8String:reinterpret_cast<const char*>(mozMessage.get())];
  NSString* install = [NSString
      stringWithUTF8String:reinterpret_cast<const char*>(mozInstall.get())];
  NSString* dontInstall = [NSString
      stringWithUTF8String:reinterpret_cast<const char*>(mozDontInstall.get())];

  NSAlert* alert = [[[NSAlert alloc] init] autorelease];

  // Note that we don't set an icon since the app icon is used by default.
  [alert setAlertStyle:NSAlertStyleInformational];
  [alert setMessageText:title];
  [alert setInformativeText:message];
  // Note that if the user hits 'Enter' the "Install" button is activated,
  // whereas if they hit  'Space' the "Don't Install" button is activated.
  // That's standard behavior so probably desirable.
  [alert addButtonWithTitle:install];
  NSButton* dontInstallButton = [alert addButtonWithTitle:dontInstall];
  // Since the "Don't Install" button doesn't have the title "Cancel" we need
  // to map the Escape key to it manually:
  [dontInstallButton setKeyEquivalent:@"\e"];

  // We need to call run on NSApp to allow accessibility. We only run it
  // for this specific alert which blocks the app's loop until the user
  // responds, it then subsequently stops the app's loop.
  //
  // AskUserIfWeShouldInstall
  //          |
  //          | ---> [NSApp run]
  //          |         |
  //          |         | ---> task
  //          |         |       | ----> [alert runModal]
  //          |         |       |               | (User selects button)
  //          |         |       | <---------   done
  //          |         |       |
  //          |         |       | -----> task
  //          |         |       |         | ----> [NSApp stop:nil]
  //          |         |       |         | <----
  //          |         |       | <-----
  //          |         | <-----
  //          | <-------
  //        done
  __block NSInteger result = -1;
  dispatch_async(dispatch_get_main_queue(), ^{
    result = [alert runModal];

    // dispatch this back onto the queue to give the run loop
    // time to clean up after the alert has ended.
    // If we don't do this, the alert window stays on screen
    // until a runloop is running.
    dispatch_async(dispatch_get_main_queue(), ^{
      [NSApp stop:nil];
    });
  });

  [NSApp run];
  MOZ_ASSERT(result != -1);

  return result == NSAlertFirstButtonReturn;

  NS_OBJC_END_TRY_BLOCK_RETURN(false);
}

static void ShowInstallFailedDialog() {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  // Try to get the localized strings:
  nsTArray<nsCString> resIds = {
      "branding/brand.ftl"_ns,
      "toolkit/global/run-from-dmg.ftl"_ns,
  };
  RefPtr<intl::Localization> l10n = intl::Localization::Create(resIds, true);

  ErrorResult rv;
  nsAutoCString mozTitle, mozMessage;
  l10n->FormatValueSync("install-failed-title"_ns, mozTitle, rv);
  if (rv.Failed()) {
    return;
  }
  l10n->FormatValueSync("install-failed-message"_ns, mozMessage, rv);
  if (rv.Failed()) {
    return;
  }

  NSString* title = [NSString
      stringWithUTF8String:reinterpret_cast<const char*>(mozTitle.get())];
  NSString* message = [NSString
      stringWithUTF8String:reinterpret_cast<const char*>(mozMessage.get())];

  NSAlert* alert = [[[NSAlert alloc] init] autorelease];

  [alert setAlertStyle:NSAlertStyleWarning];
  [alert setMessageText:title];
  [alert setInformativeText:message];

  __block NSInteger result = -1;
  dispatch_async(dispatch_get_main_queue(), ^{
    result = [alert runModal];
    [NSApp stop:nil];
  });

  // We need to call run on NSApp here for accessibility. See
  // AskUserIfWeShouldInstall for a detailed explanation.
  [NSApp run];
  MOZ_ASSERT(result != -1);
  (void)result;

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

// Install the DMGInstallHelper as an elevated helper and use it to install
bool LaunchElevatedDmgInstall(NSString* aBundlePath, NSString* aDestPath) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  os_log_info(runFromDMGLogger, "Launch elevated DMG Install");
  bool didSucceed = InstallPrivilegedHelperWithId(
      "org.mozilla.dmgInstallHelper", runFromDMGLogger);

  os_log_info(runFromDMGLogger, "Installed helper: %s",
              didSucceed ? "Yes" : "No");
  if (!didSucceed) {
    return false;
  }

  NSXPCConnection* connection = [[[NSXPCConnection alloc]
      initWithMachServiceName:@"org.mozilla.dmgInstallHelper"
                      options:NSXPCConnectionPrivileged] autorelease];

  if (connection == nil) {
    os_log_error(runFromDMGLogger, "Failed connection");

    return false;
  }

  __block BOOL remoteResult = NO;

  NSXPCInterface* dmgInstaller =
      [NSXPCInterface interfaceWithProtocol:@protocol(DMGInstallProtocol)];
  connection.remoteObjectInterface = dmgInstaller;
  connection.invalidationHandler = ^() {
    os_log_error(runFromDMGLogger, "Connection invalidated");
  };

  connection.interruptionHandler = ^() {
    os_log_error(runFromDMGLogger, "Connection interrupted");
  };

  [connection resume];

  os_log_info(runFromDMGLogger, "Connection resumed");

  id proxy = [connection synchronousRemoteObjectProxyWithErrorHandler:^(
                             NSError* error) {
    if (error != nil) {
      remoteResult = NO;
      os_log_error(runFromDMGLogger, "Remote proxy error: %{public}@", error);
    }
  }];

  if (proxy == nil) {
    os_log_error(runFromDMGLogger, "Failed to get proxy");

    return false;
  }

  os_log_info(runFromDMGLogger, "Installing... %p", proxy);

  [proxy
      installDMGFromPath:aBundlePath
               withReply:^(BOOL result) {
                 os_log_info(runFromDMGLogger, "Got response: %s",
                             result ? "success" : "failure");
                 remoteResult = result;

                 os_log_info(runFromDMGLogger, "Requesting helper terminate");

                 [proxy terminateDMGInstallHelper];
                 [connection invalidate];
               }];

  os_log_info(runFromDMGLogger, "Exiting: %s",
              remoteResult ? "success" : "failure");
  return remoteResult;

  NS_OBJC_END_TRY_BLOCK_RETURN(false);
}

// Note: both arguments are expected to contain the app name (to end with
// '.app').
static bool InstallFromPath(NSString* aBundlePath, NSString* aDestPath) {
  os_log_info(runFromDMGLogger,
              "Installing from path: %{public}@ to %{public}@", aBundlePath,
              aDestPath);
  bool installSuccessful = false;
  NSFileManager* fileManager = [NSFileManager defaultManager];

  bool alwaysElevate = false;
#ifdef DMG_INSTALL_HELPER_DEBUG
  alwaysElevate = (getenv("MOZ_DMG_INSTALL_HELPER_ALWAYS_ELEVATE") != nullptr);
  os_log_info(runFromDMGLogger, "Always elevate: %{public}s",
              alwaysElevate ? "yes" : "no");
#endif

  if (!alwaysElevate && [fileManager copyItemAtPath:aBundlePath
                                             toPath:aDestPath
                                              error:nil]) {
    installSuccessful = true;
  }

  os_log_info(runFromDMGLogger, "Unelevated installation: %s",
              installSuccessful ? "success" : "failed");

  // The installation may have been unsuccessful if the user did not have the
  // rights to write to the Applications directory. Check for this situation and
  // launch an elevated helper application.
  NSString* destDir = [aDestPath stringByDeletingLastPathComponent];
  BOOL isWritable =
      !alwaysElevate && [fileManager isWritableFileAtPath:destDir];
  if (!installSuccessful && !isWritable) {
    os_log_info(runFromDMGLogger, "Installing elevated");

    installSuccessful = LaunchElevatedDmgInstall(aBundlePath, aDestPath);
    if (!installSuccessful) {
      os_log_error(runFromDMGLogger, "Elevated DMG install failed.");
      return false;
    }
  }

  if (!installSuccessful) {
    os_log_error(runFromDMGLogger, "Unknown install failure.");
    return false;
  }

  os_log_info(runFromDMGLogger, "Installed successful");

  // Pin to dock:
  nsresult rv;
  nsCOMPtr<nsIMacDockSupport> dockSupport =
      do_GetService("@mozilla.org/widget/macdocksupport;1", &rv);
  if (NS_SUCCEEDED(rv) && dockSupport) {
    bool isInDock;
    nsAutoString appPath, appToReplacePath;
    nsCocoaUtils::GetStringForNSString(aDestPath, appPath);
    nsCocoaUtils::GetStringForNSString(aBundlePath, appToReplacePath);
    dockSupport->EnsureAppIsPinnedToDock(appPath, appToReplacePath, &isInDock);
  }

  return true;
}

bool IsAppRunningFromDmg() {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  const char* path =
      [[[NSBundle mainBundle] bundlePath] fileSystemRepresentation];

  struct statfs statfsBuf;
  if (statfs(path, &statfsBuf) != 0) {
    return false;
  }

  // Optimization to minimize impact on startup time:
  if (!(statfsBuf.f_flags & MNT_RDONLY)) {
    return false;
  }

  // Get the "BSD device name" ("diskXsY", as found in /dev) of the filesystem
  // our app is on so we can use IOKit to get its IOMedia object:
  const char devDirPath[] = "/dev/";
  const int devDirPathLength = strlen(devDirPath);
  if (strncmp(statfsBuf.f_mntfromname, devDirPath, devDirPathLength) != 0) {
    // This has been observed to happen under App Translocation. In this case,
    // path is the translocated path, and f_mntfromname is the path under
    // /Volumes. Do another stat on that path.
    nsCString volumesPath(statfsBuf.f_mntfromname);
    if (statfs(volumesPath.get(), &statfsBuf) != 0) {
      return false;
    }

    if (strncmp(statfsBuf.f_mntfromname, devDirPath, devDirPathLength) != 0) {
      // It still doesn't begin with /dev/, bail out.
      return false;
    }
  }
  const char* bsdDeviceName = statfsBuf.f_mntfromname + devDirPathLength;

  // Get the IOMedia object:
  // (Note: IOServiceGetMatchingServices takes ownership of serviceDict's ref.)
  CFMutableDictionaryRef serviceDict =
      IOBSDNameMatching(kIOMasterPortDefault, 0, bsdDeviceName);
  if (!serviceDict) {
    return false;
  }
  io_service_t media =
      IOServiceGetMatchingService(kIOMasterPortDefault, serviceDict);
  if (!media || !IOObjectConformsTo(media, "IOMedia")) {
    return false;
  }

  // Search the parent chain for a service implementing the disk image class
  // (taking care to start with `media` itself):
  io_service_t imageDrive = IO_OBJECT_NULL;
  io_iterator_t iter;
  if (IORegistryEntryCreateIterator(
          media, kIOServicePlane,
          kIORegistryIterateRecursively | kIORegistryIterateParents,
          &iter) != KERN_SUCCESS) {
    IOObjectRelease(media);
    return false;
  }
  const char* imageClass = nsCocoaFeatures::OnMontereyOrLater()
                               ? "AppleDiskImageDevice"
                               : "IOHDIXHDDrive";
  for (imageDrive = media; imageDrive; imageDrive = IOIteratorNext(iter)) {
    if (IOObjectConformsTo(imageDrive, imageClass)) {
      break;
    }
    IOObjectRelease(imageDrive);
  }
  IOObjectRelease(iter);

  if (imageDrive) {
    IOObjectRelease(imageDrive);
    return true;
  }
  return false;

  NS_OBJC_END_TRY_BLOCK_RETURN(false);
}

bool MaybeInstallAndRelaunch() {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  @autoreleasepool {
    bool isFromDmg = IsAppRunningFromDmg();
    bool isTranslocated = false;
    if (!isFromDmg) {
      NSString* bundlePath = [[NSBundle mainBundle] bundlePath];
      if ([bundlePath containsString:@"/AppTranslocation/"]) {
        isTranslocated = true;
      }
    }

    if (!isFromDmg && !isTranslocated) {
      return false;
    }

    // The Applications directory may not be at /Applications, although in
    // practice we're unlikely to encounter since run-from-.dmg is really an
    // issue with novice mac users. Still, look it up correctly:
    NSArray* applicationsDirs = NSSearchPathForDirectoriesInDomains(
        NSApplicationDirectory, NSLocalDomainMask, YES);
    NSString* applicationsDir = applicationsDirs[0];

    // Sanity check dir exists
    NSFileManager* fileManager = [NSFileManager defaultManager];
    BOOL isDir;
    if (![fileManager fileExistsAtPath:applicationsDir isDirectory:&isDir] ||
        !isDir) {
      return false;
    }

    NSString* bundlePath = [[NSBundle mainBundle] bundlePath];
    NSString* appName = [bundlePath lastPathComponent];
    NSString* destPath =
        [applicationsDir stringByAppendingPathComponent:appName];

    // If the app (an app of the same name) is already installed we can't really
    // tell without asking if we're dealing with the edge case of an
    // inexperienced user running from .dmg by mistake, or if we're dealing with
    // a more sophisticated user intentionally running from .dmg.
    if ([fileManager fileExistsAtPath:destPath]) {
      if (AskUserIfWeShouldLaunchExistingInstall()) {
        LaunchMacAppWithBundle(destPath);
        return true;
      }
      return false;
    }

    if (!AskUserIfWeShouldInstall()) {
      return false;
    }

    if (!InstallFromPath(bundlePath, destPath)) {
      ShowInstallFailedDialog();
      return false;
    }

    LaunchMacAppWithBundle(destPath);

    return true;
  }

  NS_OBJC_END_TRY_BLOCK_RETURN(false);
}

}  // namespace mozilla::MacRunFromDmgUtils
