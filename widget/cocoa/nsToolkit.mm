/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsToolkit.h"

#include <stdlib.h>

#include <mach/mach_init.h>
#include <mach/mach_interface.h>
#include <mach/mach_port.h>

extern "C" {
#include <mach-o/getsect.h>
}
#include <dlfcn.h>
#include <unistd.h>

#import <Cocoa/Cocoa.h>
#import <IOKit/IOMessage.h>
#import <IOKit/pwr_mgt/IOPMLib.h>

#include "nsCocoaUtils.h"
#include "nsObjCExceptions.h"

#include "nsIRollupListener.h"
#include "nsIWidget.h"

#include "nsIObserverService.h"

#include "mozilla/Preferences.h"
#include "mozilla/Services.h"

#include "NativeMenuSupport.h"

using namespace mozilla;

static io_connect_t gRootPort = MACH_PORT_NULL;

@interface ScreenLockObserver : NSObject
- (void)screenIsLocked:(NSNotification*)aNotification;
- (void)screenIsUnlocked:(NSNotification*)aNotification;
- (void)screensaverDidStart:(NSNotification*)aNotification;
@end

@implementation ScreenLockObserver
- (void)screenIsLocked:(NSNotification*)aNotification {
  nsToolkit::PostSleepWakeNotification(NS_WIDGET_SCREEN_LOCK_OBSERVER_TOPIC);
}

- (void)screenIsUnlocked:(NSNotification*)aNotification {
  nsToolkit::PostSleepWakeNotification(NS_WIDGET_SCREEN_UNLOCK_OBSERVER_TOPIC);
}

- (void)screensaverDidStart:(NSNotification*)aNotification {
  nsToolkit::PostSleepWakeNotification(
      NS_WIDGET_SCREENSAVER_START_OBSERVER_TOPIC);
}
@end

nsToolkit* nsToolkit::gToolkit = nullptr;

nsToolkit::nsToolkit()
    : mSleepWakeNotificationRLS(nullptr),
      mPowerNotifier{0},
      mScreenLockObserver(nil),
      mAllProcessMouseMonitor(nil) {
  MOZ_COUNT_CTOR(nsToolkit);
  RegisterForSleepWakeNotifications();
  RegisterScreenLockNotifications();
}

nsToolkit::~nsToolkit() {
  MOZ_COUNT_DTOR(nsToolkit);
  RemoveSleepWakeNotifications();
  UnregisterScreenLockNotifications();
  StopMonitoringAllProcessMouseEvents();
}

void nsToolkit::PostSleepWakeNotification(const char* aNotification) {
  nsCOMPtr<nsIObserverService> observerService = services::GetObserverService();
  if (observerService)
    observerService->NotifyObservers(nullptr, aNotification, nullptr);
}

// http://developer.apple.com/documentation/DeviceDrivers/Conceptual/IOKitFundamentals/PowerMgmt/chapter_10_section_3.html
static void ToolkitSleepWakeCallback(void* refCon, io_service_t service,
                                     natural_t messageType,
                                     void* messageArgument) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  switch (messageType) {
    case kIOMessageSystemWillSleep:
      // System is going to sleep now.
      nsToolkit::PostSleepWakeNotification(NS_WIDGET_SLEEP_OBSERVER_TOPIC);
      ::IOAllowPowerChange(gRootPort, (long)messageArgument);
      break;

    case kIOMessageCanSystemSleep:
      // In this case, the computer has been idle for several minutes
      // and will sleep soon so you must either allow or cancel
      // this notification. Important: if you don’t respond, there will
      // be a 30-second timeout before the computer sleeps.
      // In Mozilla's case, we always allow sleep.
      ::IOAllowPowerChange(gRootPort, (long)messageArgument);
      break;

    case kIOMessageSystemHasPoweredOn:
      // Handle wakeup.
      nsToolkit::PostSleepWakeNotification(NS_WIDGET_WAKE_OBSERVER_TOPIC);
      break;
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

nsresult nsToolkit::RegisterForSleepWakeNotifications() {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  IONotificationPortRef notifyPortRef;

  NS_ASSERTION(!mSleepWakeNotificationRLS, "Already registered for sleep/wake");

  gRootPort = ::IORegisterForSystemPower(
      nullptr, &notifyPortRef, ToolkitSleepWakeCallback, &mPowerNotifier);
  if (gRootPort == MACH_PORT_NULL) {
    NS_ERROR("IORegisterForSystemPower failed");
    return NS_ERROR_FAILURE;
  }

  mSleepWakeNotificationRLS =
      ::IONotificationPortGetRunLoopSource(notifyPortRef);
  ::CFRunLoopAddSource(::CFRunLoopGetCurrent(), mSleepWakeNotificationRLS,
                       kCFRunLoopDefaultMode);

  return NS_OK;

  NS_OBJC_END_TRY_BLOCK_RETURN(NS_ERROR_FAILURE);
}

void nsToolkit::RemoveSleepWakeNotifications() {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (mSleepWakeNotificationRLS) {
    ::IODeregisterForSystemPower(&mPowerNotifier);
    ::CFRunLoopRemoveSource(::CFRunLoopGetCurrent(), mSleepWakeNotificationRLS,
                            kCFRunLoopDefaultMode);

    mSleepWakeNotificationRLS = nullptr;
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

void nsToolkit::RegisterScreenLockNotifications() {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  NSDistributedNotificationCenter* center =
      [NSDistributedNotificationCenter defaultCenter];
  mScreenLockObserver = [[ScreenLockObserver alloc] init];
  auto observe = ^(SEL aSelector, NSString* aName) {
    [center addObserver:mScreenLockObserver
                  selector:aSelector
                      name:aName
                    object:nil
        suspensionBehavior:NSNotificationSuspensionBehaviorDeliverImmediately];
  };
  observe(@selector(screenIsLocked:), @"com.apple.screenIsLocked");
  observe(@selector(screenIsUnlocked:), @"com.apple.screenIsUnlocked");
  observe(@selector(screensaverDidStart:), @"com.apple.screensaver.didstart");

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

void nsToolkit::UnregisterScreenLockNotifications() {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  [[NSDistributedNotificationCenter defaultCenter]
      removeObserver:mScreenLockObserver];
  [mScreenLockObserver release];
  mScreenLockObserver = nil;

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

// Cocoa Firefox's use of custom context menus requires that we explicitly
// handle mouse events from other processes that the OS handles
// "automatically" for native context menus. This is not a frequently
// exercised code path any more, so if you're working on it it's worth looking
// into whether this is really doing what you want.
void nsToolkit::MonitorAllProcessMouseEvents() {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (mozilla::widget::NativeMenuSupport::ShouldUseNativeContextMenus()) {
    // Don't do this if we are using native context menus.
    return;
  }

  if (getenv("MOZ_NO_GLOBAL_MOUSE_MONITOR")) return;

  if (mAllProcessMouseMonitor == nil) {
    mAllProcessMouseMonitor = [NSEvent
        addGlobalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown |
                                              NSEventMaskOtherMouseDown
                                      handler:^(NSEvent* evt) {
                                        if ([NSApp isActive]) {
                                          return;
                                        }

                                        nsIRollupListener* rollupListener =
                                            nsIWidget::
                                                GetActiveRollupListener();
                                        if (!rollupListener) {
                                          return;
                                        }

                                        nsCOMPtr<nsIWidget> rollupWidget =
                                            rollupListener->GetRollupWidget();
                                        if (!rollupWidget) {
                                          return;
                                        }

                                        NSWindow* ctxMenuWindow =
                                            (NSWindow*)
                                                rollupWidget->GetNativeData(
                                                    NS_NATIVE_WINDOW);
                                        if (!ctxMenuWindow) {
                                          return;
                                        }

                                        // Don't roll up the rollup widget if
                                        // our mouseDown happens over it (doing
                                        // so would break the corresponding
                                        // context menu).
                                        NSPoint screenLocation =
                                            [NSEvent mouseLocation];
                                        if (NSPointInRect(
                                                screenLocation,
                                                [ctxMenuWindow frame])) {
                                          return;
                                        }

                                        rollupListener->Rollup({});
                                      }];
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

void nsToolkit::StopMonitoringAllProcessMouseEvents() {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (mAllProcessMouseMonitor != nil) {
    [NSEvent removeMonitor:mAllProcessMouseMonitor];
    mAllProcessMouseMonitor = nil;
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

// Return the nsToolkit instance.  If a toolkit does not yet exist, then one
// will be created.
// static
nsToolkit* nsToolkit::GetToolkit() {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  if (!gToolkit) {
    gToolkit = new nsToolkit();
  }

  return gToolkit;

  NS_OBJC_END_TRY_BLOCK_RETURN(nullptr);
}

// An alternative to [NSObject poseAsClass:] that isn't deprecated on macOS
// Leopard and is available to 64-bit binaries on Leopard and above.  Based on
// ideas and code from http://www.cocoadev.com/index.pl?MethodSwizzling.
// Since the Method type becomes an opaque type as of Objective-C 2.0, we'll
// have to switch to using accessor methods like
// method_exchangeImplementations() when we build 64-bit binaries that use
// Objective-C 2.0 (on and for Leopard and above).
//
// Be aware that, if aClass doesn't have an orgMethod selector but one of its
// superclasses does, the method substitution will (in effect) take place in
// that superclass (rather than in aClass itself).  The substitution has
// effect on the class where it takes place and all of that class's
// subclasses.  In order for method swizzling to work properly, posedMethod
// needs to be unique in the class where the substitution takes place and all
// of its subclasses.
nsresult nsToolkit::SwizzleMethods(Class aClass, SEL orgMethod, SEL posedMethod,
                                   bool classMethods) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  Method original = nil;
  Method posed = nil;

  if (classMethods) {
    original = class_getClassMethod(aClass, orgMethod);
    posed = class_getClassMethod(aClass, posedMethod);
  } else {
    original = class_getInstanceMethod(aClass, orgMethod);
    posed = class_getInstanceMethod(aClass, posedMethod);
  }

  if (!original || !posed) return NS_ERROR_FAILURE;

  method_exchangeImplementations(original, posed);

  return NS_OK;

  NS_OBJC_END_TRY_BLOCK_RETURN(NS_ERROR_FAILURE);
}
