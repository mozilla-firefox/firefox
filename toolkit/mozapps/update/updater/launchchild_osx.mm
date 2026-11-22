/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <Cocoa/Cocoa.h>
#include <CoreServices/CoreServices.h>
#include <crt_externs.h>
#include <stdlib.h>
#include <stdio.h>
#include <spawn.h>
#include <SystemConfiguration/SystemConfiguration.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include "readstrings.h"

#define ARCH_PATH "/usr/bin/arch"
#if defined(__x86_64__)
// Work around the fact that this constant is not available in the macOS SDK
#  define kCFBundleExecutableArchitectureARM64 0x0100000c
#endif

class MacAutoreleasePool {
 public:
  MacAutoreleasePool() { mPool = [[NSAutoreleasePool alloc] init]; }
  ~MacAutoreleasePool() { [mPool release]; }

 private:
  NSAutoreleasePool* mPool;
};

/**
 * Helper to launch macOS tasks via NSTask and wait for the launched task to
 * terminate.
 */
static void LaunchTask(NSString* aPath, NSArray* aArguments) {
  MacAutoreleasePool pool;

  NSTask* task = [[NSTask alloc] init];
  [task setExecutableURL:[NSURL fileURLWithPath:aPath]];
  if (aArguments) {
    [task setArguments:aArguments];
  }
  [task launchAndReturnError:nil];
  [task waitUntilExit];
  [task release];
}

static void RegisterAppWithLaunchServices(NSString* aBundlePath) {
  MacAutoreleasePool pool;

  @try {
    OSStatus status =
        LSRegisterURL((CFURLRef)[NSURL fileURLWithPath:aBundlePath], YES);
    if (status != noErr) {
      NSLog(@"We failed to register the app in the Launch Services database, "
            @"which may lead to a failure to launch the app. Launch path: %@",
            aBundlePath);
    }
  } @catch (NSException* e) {
    NSLog(@"%@: %@", e.name, e.reason);
  }
}

static void StripQuarantineBit(NSString* aBundlePath) {
  MacAutoreleasePool pool;

  NSArray* arguments = @[ @"-dr", @"com.apple.quarantine", aBundlePath ];
  LaunchTask(@"/usr/bin/xattr", arguments);
}

// How long to wait for the process we are replacing to go away before we give
// up and launch anyway.
static const NSTimeInterval kWaitForExitSeconds = 10.0;

// How often to ask Launch Services whether the process is gone yet.
static const useconds_t kWaitForExitPollMicroseconds = 50000;

/**
 * Wait for the process we are replacing to go away.
 *
 * We ask Launch Services rather than looking at the process table because the
 * dock follows the Launch Services registration, which can outlive the process
 * itself for a moment. Re-querying on every pass avoids relying on a run loop
 * to deliver property updates.
 */
static void WaitForAppToTerminate(pid_t aPid, NSTimeInterval aTimeout) {
  NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:aTimeout];
  while (true) {
    {
      MacAutoreleasePool pool;
      NSRunningApplication* app =
          [NSRunningApplication runningApplicationWithProcessIdentifier:aPid];
      if (!app || [app isTerminated]) {
        return;
      }
      if ([deadline timeIntervalSinceNow] <= 0) {
        NSLog(@"Timed out waiting for pid %d to exit before relaunching.",
              (int)aPid);
        return;
      }
    }
    usleep(kWaitForExitPollMicroseconds);
  }
}

/**
 * While two processes of the same app bundle are registered with macOS at the
 * same time, the second one counts as another instance of the app and is given
 * its own dock tile, which then sticks around in the dock's list of recently
 * used applications. So only ask for a new instance when we have to.
 *
 * We have to whenever any process of this bundle identifier is running, even
 * one running from a different copy of the application. macOS resolves the
 * open by bundle identifier rather than by the path we pass: without the
 * request it hands us that other process, drops the arguments, and the
 * application we were asked to launch never comes up.
 */
static BOOL ShouldCreateNewAppInstance(NSString* aBundlePath) {
  MacAutoreleasePool pool;

  NSString* bundleId = [[NSBundle bundleWithPath:aBundlePath] bundleIdentifier];
  if (!bundleId) {
    return YES;
  }

  return [[NSRunningApplication
             runningApplicationsWithBundleIdentifier:bundleId] count] > 0;
}

void LaunchMacApp(int argc, const char** argv, pid_t aWaitForPid) {
  MacAutoreleasePool pool;

  @try {
    NSString* launchPath = [NSString stringWithUTF8String:argv[0]];
    NSMutableArray* arguments = [NSMutableArray arrayWithCapacity:argc - 1];
    for (int i = 1; i < argc; i++) {
      [arguments addObject:[NSString stringWithUTF8String:argv[i]]];
    }
    if (![launchPath hasSuffix:@".app"]) {
      // We only support launching applications inside .app bundles.
      NSLog(@"The updater attempted to launch an app that was not a .app "
            @"bundle. Please verify launch path: %@",
            launchPath);
      return;
    }

    StripQuarantineBit(launchPath);
    RegisterAppWithLaunchServices(launchPath);

    if (aWaitForPid > 0) {
      WaitForAppToTerminate(aWaitForPid, kWaitForExitSeconds);
    }

    // We use NSWorkspace to register the application into the
    // `TALAppsToRelaunchAtLogin` list and allow for macOS session resume.
    // This API only works with `.app`s.
    __block dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
    NSWorkspaceOpenConfiguration* config =
        [NSWorkspaceOpenConfiguration configuration];
    [config setArguments:arguments];
    [config setActivates:NO];
    [config setCreatesNewApplicationInstance:ShouldCreateNewAppInstance(
                                                 launchPath)];
    [config setEnvironment:[[NSProcessInfo processInfo] environment]];

    [[NSWorkspace sharedWorkspace]
        openApplicationAtURL:[NSURL fileURLWithPath:launchPath]
               configuration:config
           completionHandler:^(NSRunningApplication* aChild, NSError* aError) {
             if (aError) {
               NSLog(@"launchchild_osx: Failed to run application. Error: %@",
                     aError);
             }
             dispatch_semaphore_signal(semaphore);
           }];

    // We use a semaphore to wait for the application to launch.
    dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
  } @catch (NSException* e) {
    NSLog(@"%@: %@", e.name, e.reason);
  }
}

void LaunchMacPostProcess(const char* aAppBundle) {
  MacAutoreleasePool pool;

  // Launch helper to perform post processing for the update; this is the Mac
  // analogue of LaunchWinPostProcess (PostUpdateWin).
  NSString* iniPath = [NSString stringWithUTF8String:aAppBundle];
  iniPath = [iniPath
      stringByAppendingPathComponent:@"Contents/Resources/updater.ini"];

  NSFileManager* fileManager = [NSFileManager defaultManager];
  if (![fileManager fileExistsAtPath:iniPath]) {
    // the file does not exist; there is nothing to run
    return;
  }

  int readResult;
  mozilla::UniquePtr<char[]> values[2];
  readResult = ReadStrings([iniPath UTF8String], "ExeRelPath\0ExeArg\0", 2,
                           values, "PostUpdateMac");
  if (readResult) {
    return;
  }

  NSString* exeRelPath = [NSString stringWithUTF8String:values[0].get()];
  NSString* exeArg = [NSString stringWithUTF8String:values[1].get()];
  if (!exeArg || !exeRelPath) {
    return;
  }

  // The path must not traverse directories and it must be a relative path.
  if ([exeRelPath isEqualToString:@".."] || [exeRelPath hasPrefix:@"/"] ||
      [exeRelPath hasPrefix:@"../"] || [exeRelPath hasSuffix:@"/.."] ||
      [exeRelPath containsString:@"/../"]) {
    return;
  }

  NSString* exeFullPath = [NSString stringWithUTF8String:aAppBundle];
  exeFullPath = [exeFullPath stringByAppendingPathComponent:exeRelPath];

  mozilla::UniquePtr<char[]> optVal;
  readResult = ReadStrings([iniPath UTF8String], "ExeAsync\0", 1, &optVal,
                           "PostUpdateMac");

  NSTask* task = [[NSTask alloc] init];
  [task setLaunchPath:exeFullPath];
  [task setArguments:[NSArray arrayWithObject:exeArg]];

  // Invoke post-update with a minimal environment to avoid environment
  // variables intended to relaunch Firefox impacting post-update operations, in
  // particular background tasks.  The updater will invoke the callback
  // application with the current (non-minimal) environment.
  //
  // Unlike on Windows, where the run without elevation gets the user's default
  // environment (bug 2078262), this always uses an empty environment.
  [task setEnvironment:@{}];

  [task launch];
  if (!readResult) {
    NSString* exeAsync = [NSString stringWithUTF8String:optVal.get()];
    if ([exeAsync isEqualToString:@"false"]) {
      [task waitUntilExit];
    }
  }
  // ignore the return value of the task, there's nothing we can do with it
  [task release];
}

id ConnectToUpdateServer() {
  MacAutoreleasePool pool;

  id updateServer = nil;
  BOOL isConnected = NO;
  int currTry = 0;
  const int numRetries = 10;  // Number of IPC connection retries before
                              // giving up.
  while (!isConnected && currTry < numRetries) {
    @try {
      updateServer = (id)[NSConnection
          rootProxyForConnectionWithRegisteredName:@"org.mozilla.updater.server"
                                              host:nil
                                   usingNameServer:[NSSocketPortNameServer
                                                       sharedInstance]];
      if (!updateServer ||
          ![updateServer respondsToSelector:@selector(abort)] ||
          ![updateServer respondsToSelector:@selector(getArguments)] ||
          ![updateServer respondsToSelector:@selector(shutdown)]) {
        NSLog(@"Server doesn't exist or doesn't provide correct selectors.");
        sleep(1);  // Wait 1 second.
        currTry++;
      } else {
        isConnected = YES;
      }
    } @catch (NSException* e) {
      NSLog(@"Encountered exception, retrying: %@: %@", e.name, e.reason);
      sleep(1);  // Wait 1 second.
      currTry++;
    }
  }
  if (!isConnected) {
    NSLog(@"Failed to connect to update server after several retries.");
    return nil;
  }
  return updateServer;
}

void CleanupElevatedMacUpdate(bool aFailureOccurred) {
  MacAutoreleasePool pool;

  id updateServer = ConnectToUpdateServer();
  if (updateServer) {
    @try {
      if (aFailureOccurred) {
        [updateServer performSelector:@selector(abort)];
      } else {
        [updateServer performSelector:@selector(shutdown)];
      }
    } @catch (NSException* e) {
    }
  }

  NSFileManager* manager = [NSFileManager defaultManager];
  [manager
      removeItemAtPath:@"/Library/PrivilegedHelperTools/org.mozilla.updater"
                 error:nil];
  [manager removeItemAtPath:@"/Library/LaunchDaemons/org.mozilla.updater.plist"
                      error:nil];

  // The following call will terminate the current process due to the "remove"
  // argument.
  LaunchTask(@"/bin/launchctl", @[ @"remove", @"org.mozilla.updater" ]);
}

// Note: Caller is responsible for freeing aArgv.
bool ObtainUpdaterArguments(int* aArgc, char*** aArgv,
                            MARChannelStringTable* aMARStrings) {
  MacAutoreleasePool pool;

  id updateServer = ConnectToUpdateServer();
  if (!updateServer) {
    // Let's try our best and clean up.
    CleanupElevatedMacUpdate(true);
    return false;  // Won't actually get here due to CleanupElevatedMacUpdate.
  }

  @try {
    NSArray* updaterArguments =
        [updateServer performSelector:@selector(getArguments)];
    *aArgc = [updaterArguments count];
    char** tempArgv = (char**)malloc(sizeof(char*) * (*aArgc));
    for (int i = 0; i < *aArgc; i++) {
      int argLen = [[updaterArguments objectAtIndex:i] length] + 1;
      tempArgv[i] = (char*)malloc(argLen);
      strncpy(tempArgv[i], [[updaterArguments objectAtIndex:i] UTF8String],
              argLen);
    }
    *aArgv = tempArgv;

    NSString* channelID =
        [updateServer performSelector:@selector(getMARChannelID)];
    const char* channelIDStr = [channelID UTF8String];
    aMARStrings->MARChannelID =
        std::make_unique<char[]>(strlen(channelIDStr) + 1);
    strcpy(aMARStrings->MARChannelID.get(), channelIDStr);
  } @catch (NSException* e) {
    // Let's try our best and clean up.
    CleanupElevatedMacUpdate(true);
    return false;  // Won't actually get here due to CleanupElevatedMacUpdate.
  }
  return true;
}

/**
 * The ElevatedUpdateServer is launched from a non-elevated updater process.
 * It allows an elevated updater process (usually a privileged helper tool) to
 * connect to it and receive all the necessary arguments to complete a
 * successful update.
 */
@interface ElevatedUpdateServer : NSObject {
  NSArray* mUpdaterArguments;
  BOOL mShouldKeepRunning;
  BOOL mAborted;
  NSString* mMARChannelID;
}
- (id)initWithArgs:(NSArray*)aArgs marChannelID:(NSString*)aMARChannelID;
- (BOOL)runServer;
- (NSArray*)getArguments;
- (NSString*)getMARChannelID;
- (void)abort;
- (BOOL)wasAborted;
- (void)shutdown;
- (BOOL)shouldKeepRunning;
@end

@implementation ElevatedUpdateServer

- (id)initWithArgs:(NSArray*)aArgs marChannelID:(NSString*)aMARChannelID {
  self = [super init];
  if (!self) {
    return nil;
  }
  mUpdaterArguments = aArgs;
  mMARChannelID = aMARChannelID;
  mShouldKeepRunning = YES;
  mAborted = NO;
  return self;
}

- (BOOL)runServer {
  NSPort* serverPort = [NSSocketPort port];
  NSConnection* server = [NSConnection connectionWithReceivePort:serverPort
                                                        sendPort:serverPort];
  [server setRootObject:self];
  if ([server registerName:@"org.mozilla.updater.server"
            withNameServer:[NSSocketPortNameServer sharedInstance]] == NO) {
    NSLog(@"Unable to register as DirectoryServer.");
    NSLog(@"Is another copy running?");
    return NO;
  }

  while ([self shouldKeepRunning] &&
         [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                  beforeDate:[NSDate distantFuture]]);
  return ![self wasAborted];
}

- (NSArray*)getArguments {
  return mUpdaterArguments;
}

/**
 * The MAR channel ID(s) are stored in the UpdateSettings.framework that ships
 * with the updater.app bundle. When an elevated update is occurring, the
 * org.mozilla.updater binary is extracted and installed individually as a
 * Privileged Helper Tool. This Privileged Helper Tool does not have access to
 * the UpdateSettings.framework and we therefore rely on the unelevated updater
 * process to pass this information to the elevated updater process in the same
 * fashion that the command line arguments are passed to the elevated updater
 * process by `getArguments`.
 */
- (NSString*)getMARChannelID {
  return mMARChannelID;
}

- (void)abort {
  mAborted = YES;
  [self shutdown];
}

- (BOOL)wasAborted {
  return mAborted;
}

- (void)shutdown {
  mShouldKeepRunning = NO;
}

- (BOOL)shouldKeepRunning {
  return mShouldKeepRunning;
}

@end

bool ServeElevatedUpdate(int aArgc, const char** aArgv,
                         const char* aMARChannelID) {
  MacAutoreleasePool pool;

  NSMutableArray* updaterArguments = [NSMutableArray arrayWithCapacity:aArgc];
  for (int i = 0; i < aArgc; i++) {
    [updaterArguments addObject:[NSString stringWithUTF8String:aArgv[i]]];
  }

  NSString* channelID = [NSString stringWithUTF8String:aMARChannelID];
  ElevatedUpdateServer* updater =
      [[ElevatedUpdateServer alloc] initWithArgs:updaterArguments
                                    marChannelID:channelID];
  bool didSucceed = [updater runServer];

  [updater release];
  return didSucceed;
}

bool IsOwnedByGroupAdmin(const char* aAppBundle) {
  MacAutoreleasePool pool;

  NSString* appDir = [NSString stringWithUTF8String:aAppBundle];
  NSFileManager* fileManager = [NSFileManager defaultManager];

  NSDictionary* attributes = [fileManager attributesOfItemAtPath:appDir
                                                           error:nil];
  bool isOwnedByAdmin = false;
  if (attributes &&
      [[attributes valueForKey:NSFileGroupOwnerAccountID] intValue] == 80) {
    isOwnedByAdmin = true;
  }
  return isOwnedByAdmin;
}

void SetGroupOwnershipAndPermissions(const char* aAppBundle) {
  MacAutoreleasePool pool;

  NSString* appDir = [NSString stringWithUTF8String:aAppBundle];
  NSFileManager* fileManager = [NSFileManager defaultManager];
  NSError* error = nil;
  NSArray* paths = [fileManager subpathsOfDirectoryAtPath:appDir error:&error];
  if (error) {
    return;
  }

  // Set group ownership of Firefox.app to 80 ("admin") and permissions to
  // 0775.
  if (![fileManager setAttributes:@{
        NSFileGroupOwnerAccountID : @(80),
        NSFilePosixPermissions : @(0775)
      }
                     ofItemAtPath:appDir
                            error:&error] ||
      error) {
    return;
  }

  NSArray* permKeys = [NSArray
      arrayWithObjects:NSFileGroupOwnerAccountID, NSFilePosixPermissions, nil];
  // For all descendants of Firefox.app, set group ownership to 80 ("admin") and
  // ensure write permission for the group.
  for (NSString* currPath in paths) {
    NSString* child = [appDir stringByAppendingPathComponent:currPath];
    NSDictionary* oldAttributes = [fileManager attributesOfItemAtPath:child
                                                                error:&error];
    if (error) {
      return;
    }
    // Skip symlinks, since they could be pointing to files outside of the .app
    // bundle.
    if ([oldAttributes fileType] == NSFileTypeSymbolicLink) {
      continue;
    }
    NSNumber* oldPerms =
        (NSNumber*)[oldAttributes valueForKey:NSFilePosixPermissions];
    NSArray* permObjects = [NSArray
        arrayWithObjects:[NSNumber numberWithUnsignedLong:80],
                         [NSNumber
                             numberWithUnsignedLong:[oldPerms shortValue] |
                                                    020],
                         nil];
    NSDictionary* attributes = [NSDictionary dictionaryWithObjects:permObjects
                                                           forKeys:permKeys];
    if (![fileManager setAttributes:attributes
                       ofItemAtPath:child
                              error:&error] ||
        error) {
      return;
    }
  }
}
