/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// NSApplication delegate for Mac OS X Cocoa API.

// As of 10.4 Tiger, the system can send six kinds of Apple Events to an
// application; a well-behaved XUL app should have some kind of handling for all
// of them.
//
// See
// http://developer.apple.com/documentation/Cocoa/Conceptual/ScriptableCocoaApplications/SApps_handle_AEs/chapter_11_section_3.html
// for details.

#include <AppKit/AppKit.h>
#import <Cocoa/Cocoa.h>
#include "NativeMenuMac.h"
#import <Carbon/Carbon.h>

#include "ASWebAuthSessionHandler.h"
#include "CustomCocoaEvents.h"
#include "gfxPlatform.h"
#include "nsCOMPtr.h"
#include "nsINativeAppSupport.h"
#include "nsAppRunner.h"
#include "nsAppShell.h"
#include "nsComponentManagerUtils.h"
#include "nsServiceManagerUtils.h"
#include "nsIAppStartup.h"
#include "nsIAppShell.h"
#include "nsExceptionHandler.h"
#include "mozilla/TimeStamp.h"
#include "nsIObserverService.h"
#include "nsISupportsPrimitives.h"
#include "nsObjCExceptions.h"
#include "nsIFile.h"
#include "nsDirectoryServiceDefs.h"
#include "nsCommandLine.h"
#include "nsIMacDockSupport.h"
#include "nsIStandaloneNativeMenu.h"
#include "nsILocalFileMac.h"
#include "nsString.h"
#include "nsCommandLineServiceMac.h"
#include "nsCommandLine.h"
#include "nsStandaloneNativeMenu.h"
#include "nsCocoaUtils.h"
#include "nsMenuBarX.h"
#include "mozilla/NeverDestroyed.h"
#include "MacAutoreleasePool.h"

@interface MacApplicationDelegate : NSObject <NSApplicationDelegate> {
}

// Standard Edit menu selectors. Setting these as the action on our Edit
// menu items lets native text fields (e.g. an NSSavePanel sheet's
// filename field, bug 2036608) handle Cmd+C/V/X/Z/A natively via the
// responder chain. When no responder handles them, these forwarders
// route to [nsMenuBarX::sNativeEventTarget menuItemHit:] so the
// command still reaches Gecko via the normal menu path.
- (IBAction)undo:(id)aSender;
- (IBAction)redo:(id)aSender;
- (IBAction)cut:(id)aSender;
- (IBAction)copy:(id)aSender;
- (IBAction)paste:(id)aSender;
- (IBAction)delete:(id)aSender;
- (IBAction)selectAll:(id)aSender;

@end

enum class LaunchStatus {
  Initial,
  DelegateIsSetup,
  CollectingURLs,
  CollectedURLs,
};

static LaunchStatus sLaunchStatus = LaunchStatus::Initial;
static bool sHasFinishedLaunching = false;

#ifdef NIGHTLY_BUILD
static mozilla::TimeStamp sStartupRunLoopStart;
static uint32_t sStartupRunLoopWakes = 0;
static double sStartupRunLoopLastWakeMs = 0;

static nsCString& StartupRunLoopEvents() {
  static mozilla::NeverDestroyed<nsCString> sEvents;
  return *sEvents;
}

static double StartupRunLoopMs() {
  return (mozilla::TimeStamp::Now() - sStartupRunLoopStart).ToMilliseconds();
}

static void RecordStartupRunLoopAnnotation() {
  nsAutoCString value(StartupRunLoopEvents());
  value.AppendPrintf(" wakes=%u lastwake=%.0f", sStartupRunLoopWakes,
                     sStartupRunLoopLastWakeMs);
  CrashReporter::RecordAnnotationNSCString(
      CrashReporter::Annotation::MacStartupRunLoop, value);
}

static uint32_t EventloopNestingLevel() {
  uint32_t nesting = 0;
  if (nsCOMPtr<nsIAppShell> appShell =
          do_GetService("@mozilla.org/widget/appshell/mac;1")) {
    appShell->GetEventloopNestingLevel(&nesting);
  }
  return nesting;
}

// Adds an event to the MacStartupRunLoop crash annotation while
// InitializeMacApp() spins its run loop.
static void LogStartupRunLoopEvent(const char* aFormat, ...)
    MOZ_FORMAT_PRINTF(1, 2);
static void LogStartupRunLoopEvent(const char* aFormat, ...) {
  if (sLaunchStatus != LaunchStatus::CollectingURLs) {
    return;
  }
  nsCString& events = StartupRunLoopEvents();
  if (events.Length() < 1024) {
    if (!events.IsEmpty()) {
      events.Append(' ');
    }
    va_list args;
    va_start(args, aFormat);
    events.AppendVprintf(aFormat, args);
    va_end(args);
  }
  RecordStartupRunLoopAnnotation();
}

#  define LOG_STARTUP_RUN_LOOP_EVENT(...) LogStartupRunLoopEvent(__VA_ARGS__)
#else
#  define LOG_STARTUP_RUN_LOOP_EVENT(...)
#endif

static nsTArray<nsCString>& StartupURLs() {
  static mozilla::NeverDestroyed<nsTArray<nsCString>> sStartupURLs;
  return *sStartupURLs;
}

// Methods that can be called from non-Objective-C code.

// This is needed, on relaunch, to force the OS to use the "Cocoa Dock API"
// instead of the "Carbon Dock API".  For more info see bmo bug 377166.
void EnsureUseCocoaDockAPI() {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  [GeckoNSApplication sharedApplication];

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

void DisableAppNap() {
  // Prevent the parent process from entering App Nap. macOS does not put our
  // child processes into App Nap and, as a result, when the parent is in
  // App Nap, child processes continue to run normally generating IPC messages
  // for the parent which can end up being queued. This can cause the browser
  // to be unresponsive for a period of time after the App Nap until the parent
  // process "catches up." NSAppSleepDisabled has to be set early during
  // startup before the OS reads the value for the process.
  [[NSUserDefaults standardUserDefaults] registerDefaults:@{
    @"NSAppSleepDisabled" : @YES,
  }];
}

void SetupMacApplicationDelegate(bool* gRestartedByOS) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  // this is called during startup, outside an event loop, and therefore
  // needs an autorelease pool to avoid cocoa object leakage (bug 559075)
  mozilla::MacAutoreleasePool pool;

  // Ensure that InitializeMacApp() doesn't regress bug 377166.
  [GeckoNSApplication sharedApplication];

  // This call makes it so that application:openFile: doesn't get bogus calls
  // from Cocoa doing its own parsing of the argument string. And yes, we need
  // to use a string with a boolean value in it. That's just how it works.
  [[NSUserDefaults standardUserDefaults]
      setObject:@"NO"
         forKey:@"NSTreatUnknownArgumentsAsOpen"];

  // Create the delegate. This should be around for the lifetime of the app.
  id<NSApplicationDelegate> delegate = [[MacApplicationDelegate alloc] init];
  [[GeckoNSApplication sharedApplication] setDelegate:delegate];

  *gRestartedByOS = !!nsCocoaUtils::ShouldRestoreStateDueToLaunchAtLogin();

  MOZ_ASSERT(
      sLaunchStatus == LaunchStatus::Initial,
      "Launch status should be in intial state when setting up delegate");
  sLaunchStatus = LaunchStatus::DelegateIsSetup;

  // Apple requires the session handler to be registered before
  // applicationDidFinishLaunching returns. We register here, as early as
  // possible, so auth requests that arrive during startup are queued rather
  // than dropped.
  RegisterASWebAuthSessionHandler();

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

// Run the mac app and stop it ASAP. This allows us to
// (a) Initialize accessibility early enough for modals that appear before
//     the main app and nest their own event loop to be accessible.
// (b) Collect URLs that were provided to the app at open time.
void InitializeMacApp() {
  if (sLaunchStatus != LaunchStatus::DelegateIsSetup) {
    // Delegate has not been set up or NSApp has been launched already.
    return;
  }

  sLaunchStatus = LaunchStatus::CollectingURLs;
  if (!gfxPlatform::IsHeadless()) {
    // Spin the event loop so that any URLs the app was launched to open are
    // buffered into StartupURLs(). Observer is released inside the callback.
    CFRunLoopObserverRef observer = CFRunLoopObserverCreate(
        kCFAllocatorDefault, kCFRunLoopBeforeWaiting, /* repeats = */ true,
        /* order = */ 0,
        [](CFRunLoopObserverRef aObserver, CFRunLoopActivity, void*) {
          LOG_STARTUP_RUN_LOOP_EVENT("obs:n%u,r%d", EventloopNestingLevel(),
                                     [NSApp isRunning] ? 1 : 0);

          // A native run loop can reach idle before launch finishes.
          if (!sHasFinishedLaunching) {
            return;
          }

          CFRunLoopObserverInvalidate(aObserver);
          CFRelease(aObserver);

          // Stop the inner run loop. We'll call `run` again when the main event
          // loop should start.
          [NSApp stop:NSApp];
          LOG_STARTUP_RUN_LOOP_EVENT("stop:r%d", [NSApp isRunning] ? 1 : 0);

          // Send a bogus event so that the internal "app stopped" flag is
          // processed. Since we aren't calling this from a responder, we need
          // to post an event to have the loop iterate and respond to the
          // stopped flag.
          [NSApp postEvent:[NSEvent
                               otherEventWithType:NSEventTypeApplicationDefined
                                         location:NSMakePoint(0, 0)
                                    modifierFlags:0
                                        timestamp:0
                                     windowNumber:0
                                          context:nullptr
                                          subtype:kEventSubtypeNone
                                            data1:0
                                            data2:0]
                   atStart:NO];
        },
        nullptr);
    CFRunLoopAddObserver(CFRunLoopGetCurrent(), observer,
                         kCFRunLoopCommonModes);
#ifdef NIGHTLY_BUILD
    sStartupRunLoopStart = mozilla::TimeStamp::Now();
    CFRunLoopObserverRef wakeObserver = CFRunLoopObserverCreate(
        kCFAllocatorDefault, kCFRunLoopAfterWaiting, /* repeats = */ true,
        /* order = */ 0,
        [](CFRunLoopObserverRef, CFRunLoopActivity, void*) {
          ++sStartupRunLoopWakes;
          sStartupRunLoopLastWakeMs = StartupRunLoopMs();
          RecordStartupRunLoopAnnotation();
        },
        nullptr);
    CFRunLoopAddObserver(CFRunLoopGetCurrent(), wakeObserver,
                         kCFRunLoopCommonModes);
#endif
    LOG_STARTUP_RUN_LOOP_EVENT("run");
    [NSApp run];
#ifdef NIGHTLY_BUILD
    CFRunLoopObserverInvalidate(wakeObserver);
    CFRelease(wakeObserver);
    CrashReporter::UnrecordAnnotation(
        CrashReporter::Annotation::MacStartupRunLoop);
    StartupRunLoopEvents().Truncate();
#endif
  }
  sLaunchStatus = LaunchStatus::CollectedURLs;
}

nsTArray<nsCString> TakeStartupURLs() { return std::move(StartupURLs()); }

@implementation MacApplicationDelegate

- (IBAction)undo:(id)aSender {
  [nsMenuBarX::sNativeEventTarget menuItemHit:aSender];
}

- (IBAction)redo:(id)aSender {
  [nsMenuBarX::sNativeEventTarget menuItemHit:aSender];
}

- (IBAction)cut:(id)aSender {
  [nsMenuBarX::sNativeEventTarget menuItemHit:aSender];
}

- (IBAction)copy:(id)aSender {
  [nsMenuBarX::sNativeEventTarget menuItemHit:aSender];
}

- (IBAction)paste:(id)aSender {
  [nsMenuBarX::sNativeEventTarget menuItemHit:aSender];
}

- (IBAction)delete:(id)aSender {
  [nsMenuBarX::sNativeEventTarget menuItemHit:aSender];
}

- (IBAction)selectAll:(id)aSender {
  [nsMenuBarX::sNativeEventTarget menuItemHit:aSender];
}

- (id)init {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  if ((self = [super init])) {
    if (![NSApp windowsMenu]) {
      // If the application has a windows menu, it will keep it up to date and
      // prepend the window list to the Dock menu automatically.
      NSMenu* windowsMenu = [[GeckoNSMenu alloc] initWithTitle:@"Window"];
      [NSApp setWindowsMenu:windowsMenu];
      [windowsMenu release];
    }
  }
  return self;

  NS_OBJC_END_TRY_BLOCK_RETURN(nil);
}

// The method that NSApplication calls upon a request to reopen, such as when
// the Dock icon is clicked and no windows are open. A "visible" window may be
// miniaturized, so we can't skip nsCocoaNativeReOpen() if 'flag' is 'true'.
- (BOOL)applicationShouldHandleReopen:(NSApplication*)theApp
                    hasVisibleWindows:(BOOL)flag {
  LOG_STARTUP_RUN_LOOP_EVENT("reopen");
  nsCOMPtr<nsINativeAppSupport> nas = NS_GetNativeAppSupport();
  NS_ENSURE_TRUE(nas, NO);

  // Go to the common Carbon/Cocoa reopen method.
  nsresult rv = nas->ReOpen();
  NS_ENSURE_SUCCESS(rv, NO);

  // NO says we don't want NSApplication to do anything else for us.
  return NO;
}

// The method that NSApplication calls when documents are requested to be
// printed from the Finder (under the "File" menu). It will be called once for
// each selected document.
- (BOOL)application:(NSApplication*)theApplication
          printFile:(NSString*)filename {
  return NO;
}

// The method that NSApplication calls for using secure state restoration.
- (BOOL)applicationSupportsSecureRestorableState:(NSApplication*)app {
  return YES;
}

// Create the menu that shows up in the Dock.
- (NSMenu*)applicationDockMenu:(NSApplication*)sender {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  // Create the NSMenu that will contain the dock menu items.
  NSMenu* menu = [[[GeckoNSMenu alloc] initWithTitle:@""] autorelease];
  [menu setAutoenablesItems:NO];

  // Add application-specific dock menu items. On error, do not insert the
  // dock menu items.
  nsresult rv;
  nsCOMPtr<nsIMacDockSupport> dockSupport =
      do_GetService("@mozilla.org/widget/macdocksupport;1", &rv);
  if (NS_FAILED(rv) || !dockSupport) return menu;

  nsCOMPtr<nsIStandaloneNativeMenu> dockMenuInterface;
  rv = dockSupport->GetDockMenu(getter_AddRefs(dockMenuInterface));
  if (NS_FAILED(rv) || !dockMenuInterface) return menu;

  RefPtr<mozilla::widget::NativeMenuMac> dockMenu =
      static_cast<nsStandaloneNativeMenu*>(dockMenuInterface.get())
          ->GetNativeMenu();

  // Give the menu the opportunity to update itself before display.
  dockMenu->MenuWillOpen();

  // Obtain a copy of the native menu.
  NSMenu* nativeDockMenu = dockMenu->NativeNSMenu();
  if (!nativeDockMenu) {
    return menu;
  }

  // Loop through the application-specific dock menu and insert its
  // contents into the dock menu that we are building for Cocoa.
  int numDockMenuItems = [nativeDockMenu numberOfItems];
  if (numDockMenuItems > 0) {
    if ([menu numberOfItems] > 0) [menu addItem:[NSMenuItem separatorItem]];

    for (int i = 0; i < numDockMenuItems; i++) {
      NSMenuItem* itemCopy = [[nativeDockMenu itemAtIndex:i] copy];
      [menu addItem:itemCopy];
      [itemCopy release];
    }
  }

  return menu;

  NS_OBJC_END_TRY_BLOCK_RETURN(nil);
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
  sHasFinishedLaunching = true;
  LOG_STARTUP_RUN_LOOP_EVENT("didfinish");
}

- (void)applicationWillFinishLaunching:(NSNotification*)notification {
  LOG_STARTUP_RUN_LOOP_EVENT("willfinish");
  // We provide our own full screen menu item, so we don't want the OS providing
  // one as well.
  [[NSUserDefaults standardUserDefaults]
      setBool:NO
       forKey:@"NSFullScreenMenuItemEverywhere"];
}

// If we don't handle applicationShouldTerminate:, a call to [NSApp terminate:]
// (from the browser or from the OS) can result in an unclean shutdown.
- (NSApplicationTerminateReply)applicationShouldTerminate:
    (NSApplication*)sender {
  LOG_STARTUP_RUN_LOOP_EVENT("term");
  nsCOMPtr<nsIObserverService> obsServ =
      do_GetService("@mozilla.org/observer-service;1");
  if (!obsServ) return NSTerminateNow;

  nsCOMPtr<nsISupportsPRBool> cancelQuit =
      do_CreateInstance(NS_SUPPORTS_PRBOOL_CONTRACTID);
  if (!cancelQuit) return NSTerminateNow;

  cancelQuit->SetData(false);
  obsServ->NotifyObservers(cancelQuit, "quit-application-requested", nullptr);

  bool abortQuit;
  cancelQuit->GetData(&abortQuit);
  if (abortQuit) return NSTerminateCancel;

  nsCOMPtr<nsIAppStartup> appService =
      do_GetService("@mozilla.org/toolkit/app-startup;1");
  if (appService) {
    // Quit does not start a shutdown if the user keeps a window open from a
    // beforeunload prompt.
    appService->Quit(nsIAppStartup::eForceQuit, 0);
    if (!appService->GetShuttingDown()) {
      return NSTerminateCancel;
    }
  }

  return NSTerminateNow;
}

- (void)application:(NSApplication*)application
           openURLs:(NSArray<NSURL*>*)urls {
  [self openURLs:urls];
}

- (BOOL)application:(NSApplication*)application
    willContinueUserActivityWithType:(NSString*)userActivityType {
  return [userActivityType isEqualToString:NSUserActivityTypeBrowsingWeb];
}

- (BOOL)application:(NSApplication*)application
    continueUserActivity:(NSUserActivity*)userActivity
      restorationHandler:
          (void (^)(NSArray<id<NSUserActivityRestoring>>*))restorationHandler {
  if (![userActivity.activityType
          isEqualToString:NSUserActivityTypeBrowsingWeb]) {
    return NO;
  }

  return [self openURLs:@[ userActivity.webpageURL ]];
}

- (void)application:(NSApplication*)application
    didFailToContinueUserActivityWithType:(NSString*)userActivityType
                                    error:(NSError*)error {
  NSLog(@"Failed to continue user activity %@: %@", userActivityType, error);
}

- (BOOL)openURLs:(NSArray<NSURL*>*)urls {
  LOG_STARTUP_RUN_LOOP_EVENT("urls");
  nsTArray<const char*> args([urls count] * 2 + 2);
  // Placeholder for unused program name.
  args.AppendElement(nullptr);
  bool bufferedURLs = false;
  bool shouldOpenInBackground = false;
#ifdef MOZ_BUILD_APP_IS_BROWSER
  NSAppleEventDescriptor* event =
      NSAppleEventManager.sharedAppleEventManager.currentAppleEvent;
  if (event.eventClass == kInternetEventClass && event.eventID == kAEGetURL) {
    if (@available(macOS 15.0, *)) {
      constexpr AEKeyword kActivationExpected = 'aapd';
      shouldOpenInBackground =
          ![event paramDescriptorForKeyword:kActivationExpected].booleanValue;
    } else {
      shouldOpenInBackground = !NSApp.isActive;
    }
  }
#endif

  for (NSURL* url in urls) {
    if (!url || !url.scheme ||
        [url.scheme caseInsensitiveCompare:@"chrome"] == NSOrderedSame) {
      continue;
    }

    const char* const urlString = [[url absoluteString] UTF8String];
    if (sLaunchStatus == LaunchStatus::CollectingURLs) {
      StartupURLs().AppendElement(urlString);
      bufferedURLs = true;
      continue;
    }

    args.AppendElement(shouldOpenInBackground ? "-url-background" : "-url");
    args.AppendElement(urlString);
  }

  if (args.Length() <= 1) {
    // No URLs were added to the command line for immediate dispatch.
    // Return YES if URLs were buffered for startup processing.
    return bufferedURLs ? YES : NO;
  }

  nsCOMPtr<nsICommandLineRunner> cmdLine(new nsCommandLine());
  nsCOMPtr<nsIFile> workingDir;
  nsresult rv = NS_GetSpecialDirectory(NS_OS_CURRENT_WORKING_DIR,
                                       getter_AddRefs(workingDir));
  if (NS_FAILED(rv)) {
    // Couldn't find a working dir. Uh oh. Good job cmdline::Init can cope.
    workingDir = nullptr;
  }

  rv = cmdLine->Init(args.Length(), args.Elements(), workingDir,
                     nsICommandLine::STATE_REMOTE_EXPLICIT);
  if (NS_FAILED(rv)) {
    return NO;
  }
  rv = cmdLine->Run();
  if (NS_FAILED(rv)) {
    return NO;
  }

  return YES;
}

@end
