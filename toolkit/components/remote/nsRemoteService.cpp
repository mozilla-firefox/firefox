/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsRemoteClient.h"
#ifdef MOZ_WIDGET_GTK
#  ifdef MOZ_ENABLE_DBUS
#    include "nsDBusRemoteServer.h"
#    include "nsDBusRemoteClient.h"
#  else
#    include "nsGTKRemoteServer.h"
#    include "nsXRemoteClient.h"
#  endif
#elif defined(XP_WIN)
#  include "nsWinRemoteServer.h"
#  include "nsWinRemoteClient.h"
#elif defined(XP_DARWIN)
#  include "nsMacRemoteServer.h"
#  include "nsMacRemoteClient.h"
#endif
#include "nsRemoteService.h"

#include "nsIObserverService.h"
#include "nsString.h"
#include "nsServiceManagerUtils.h"
#include "SpecialSystemDirectory.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/UniquePtr.h"

// Time to wait for the startup lock
#define START_TIMEOUT_MSEC 5000
#define START_SLEEP_MSEC 100

extern int gArgc;
extern char** gArgv;

using namespace mozilla;

nsStartupLock::nsStartupLock(nsIFile* aDir, nsProfileLock& aLock) : mDir(aDir) {
  mLock = aLock;
}

nsStartupLock::~nsStartupLock() {
  mLock.Unlock();
  mLock.Cleanup();

  mDir->Remove(false);
}

NS_IMPL_ISUPPORTS(nsRemoteService, nsIObserver, nsIRemoteService)

nsRemoteService::nsRemoteService() : mProgram("mozilla") {
  ToLowerCase(mProgram);
}

void nsRemoteService::SetProgram(const char* aProgram) {
  mProgram = aProgram;
  ToLowerCase(mProgram);
}
void nsRemoteService::SetProfile(nsACString& aProfile) { mProfile = aProfile; }

#ifdef MOZ_WIDGET_GTK
void nsRemoteService::SetStartupToken(nsACString& aStartupToken) {
  mStartupToken = aStartupToken;
}
#endif

// Attempts to lock the given directory by polling until the timeout is reached.
static nsresult AcquireLock(nsIFile* aMutexDir, double aTimeout,
                            nsProfileLock& aProfileLock) {
  const mozilla::TimeStamp epoch = mozilla::TimeStamp::Now();
  do {
    // If we have been waiting for another instance to release the lock it will
    // have deleted the lock directory when doing so so we have to make sure it
    // exists every time we poll for the lock.
    nsresult rv = aMutexDir->Create(nsIFile::DIRECTORY_TYPE, 0700);
    if (NS_FAILED(rv) && rv != NS_ERROR_FILE_ALREADY_EXISTS) {
      NS_WARNING("Unable to create startup lock directory.");
      return rv;
    }

    rv = aProfileLock.Lock(aMutexDir, nullptr);
    if (NS_SUCCEEDED(rv)) {
      return NS_OK;
    }

    PR_Sleep(START_SLEEP_MSEC);
  } while ((mozilla::TimeStamp::Now() - epoch) <
           mozilla::TimeDuration::FromMilliseconds(aTimeout));

  return NS_ERROR_FAILURE;
}

RefPtr<nsRemoteService::StartupLockPromise> nsRemoteService::AsyncLockStartup(
    double aTimeout) {
  // If startup is already locked we can just resolve immediately.
  RefPtr<nsStartupLock> lock(mStartupLock);
  if (lock) {
    return StartupLockPromise::CreateAndResolve(lock, __func__);
  }

  // If there is already an executing promise we can just return that otherwise
  // we have to start a new one.
  if (mStartupLockPromise) {
    return mStartupLockPromise;
  }

  nsCOMPtr<nsIFile> mutexDir;
  nsresult rv = GetSpecialSystemDirectory(OS_TemporaryDirectory,
                                          getter_AddRefs(mutexDir));
  if (NS_FAILED(rv)) {
    return StartupLockPromise::CreateAndReject(rv, __func__);
  }

  rv = mutexDir->AppendNative(mProgram);
  if (NS_FAILED(rv)) {
    return StartupLockPromise::CreateAndReject(rv, __func__);
  }

  nsCOMPtr<nsISerialEventTarget> queue;
  MOZ_ALWAYS_SUCCEEDS(NS_CreateBackgroundTaskQueue("StartupLockTaskQueue",
                                                   getter_AddRefs(queue)));

  mStartupLockPromise = InvokeAsync(
      queue, __func__, [mutexDir = std::move(mutexDir), aTimeout]() {
        nsProfileLock lock;
        nsresult rv = AcquireLock(mutexDir, aTimeout, lock);
        if (NS_SUCCEEDED(rv)) {
          return StartupLockPromise::CreateAndResolve(
              new nsStartupLock(mutexDir, lock), __func__);
        }

        return StartupLockPromise::CreateAndReject(rv, __func__);
      });

  // Note this is the guaranteed first `Then` and will run before any other
  // `Then`s.
  mStartupLockPromise->Then(
      GetCurrentSerialEventTarget(), __func__,
      [this, self = RefPtr{this}](
          const StartupLockPromise::ResolveOrRejectValue& aResult) {
        if (aResult.IsResolve()) {
          mStartupLock = aResult.ResolveValue();
        }
        mStartupLockPromise = nullptr;
      });

  return mStartupLockPromise;
}

already_AddRefed<nsStartupLock> nsRemoteService::LockStartup() {
  MOZ_RELEASE_ASSERT(!mStartupLockPromise,
                     "Should not have started an asynchronous lock attempt");

  // If we're already locked just return the current lock.
  RefPtr<nsStartupLock> lock(mStartupLock);
  if (lock) {
    return lock.forget();
  }

  nsCOMPtr<nsIFile> mutexDir;
  nsresult rv = GetSpecialSystemDirectory(OS_TemporaryDirectory,
                                          getter_AddRefs(mutexDir));
  if (NS_FAILED(rv)) {
    return nullptr;
  }

  rv = mutexDir->AppendNative(mProgram);
  if (NS_FAILED(rv)) {
    return nullptr;
  }

  nsProfileLock profileLock;
  rv = AcquireLock(mutexDir, START_TIMEOUT_MSEC, profileLock);

  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to lock for startup, continuing anyway.");
    return nullptr;
  }

  lock = new nsStartupLock(mutexDir, profileLock);
  mStartupLock = lock;
  return lock.forget();
}

nsresult nsRemoteService::SendCommandLine(const nsACString& aProfile,
                                          size_t aArgc, const char** aArgv,
                                          bool aRaise) {
  if (aProfile.IsEmpty()) {
    return NS_ERROR_FAILURE;
  }

  UniquePtr<nsRemoteClient> client;
#ifdef MOZ_WIDGET_GTK
#  if defined(MOZ_ENABLE_DBUS)
  client = std::make_unique<nsDBusRemoteClient>(mStartupToken);
#  else
  client = std::make_unique<nsXRemoteClient>(mStartupToken);
#  endif
#elif defined(XP_WIN)
  client = std::make_unique<nsWinRemoteClient>();
#elif defined(XP_DARWIN)
  client = std::make_unique<nsMacRemoteClient>();
#else
  return NS_ERROR_NOT_AVAILABLE;
#endif

  nsresult rv = client->Init();
  NS_ENSURE_SUCCESS(rv, rv);

  return client->SendCommandLine(mProgram.get(),
                                 PromiseFlatCString(aProfile).get(), aArgc,
                                 const_cast<const char**>(aArgv), aRaise);
}

NS_IMETHODIMP
nsRemoteService::SendCommandLine(const nsACString& aProfile,
                                 const nsTArray<nsCString>& aArgs,
                                 bool aRaise) {
#ifdef MOZ_WIDGET_GTK
  // Linux clients block until they receive a response so it is impossible to
  // send a remote command to the current profile.
  if (aProfile.Equals(mProfile)) {
    return NS_ERROR_INVALID_ARG;
  }
#endif
  nsAutoCString binaryPath;

  nsTArray<const char*> args;
  // Note that the command line must include an initial path to the binary but
  // this is generally ignored.
  args.SetCapacity(aArgs.Length() + 1);
  args.AppendElement(binaryPath.get());

  for (const nsCString& arg : aArgs) {
    args.AppendElement(arg.get());
  }

  return SendCommandLine(aProfile, args.Length(), args.Elements(), aRaise);
}

nsresult nsRemoteService::StartClient() {
  return SendCommandLine(mProfile, gArgc, const_cast<const char**>(gArgv),
                         true);
}

void nsRemoteService::StartupServer() {
  if (mRemoteServer) {
    return;
  }

  if (mProfile.IsEmpty()) {
    return;
  }

#ifdef MOZ_WIDGET_GTK
#  if defined(MOZ_ENABLE_DBUS)
  mRemoteServer = std::make_unique<nsDBusRemoteServer>();
#  else
  mRemoteServer = std::make_unique<nsGTKRemoteServer>();
#  endif
#elif defined(XP_WIN)
  mRemoteServer = std::make_unique<nsWinRemoteServer>();
#elif defined(XP_DARWIN)
  mRemoteServer = std::make_unique<nsMacRemoteServer>();
#else
  return;
#endif

  if (!mRemoteServer) {
    return;
  }

  nsresult rv = mRemoteServer->Startup(mProgram.get(), mProfile.get());

  if (NS_FAILED(rv)) {
    mRemoteServer = nullptr;
    return;
  }

  nsCOMPtr<nsIObserverService> obs(
      do_GetService("@mozilla.org/observer-service;1"));
  if (obs) {
    obs->AddObserver(this, "xpcom-shutdown", false);
    obs->AddObserver(this, "quit-application", false);
  }
}

void nsRemoteService::ShutdownServer() { mRemoteServer = nullptr; }

nsRemoteService::~nsRemoteService() { ShutdownServer(); }

NS_IMETHODIMP
nsRemoteService::Observe(nsISupports* /*aSubject*/, const char* /*aTopic*/,
                         const char16_t* /*aData*/) {
  // This can be xpcom-shutdown or quit-application, but it's the same either
  // way.
  ShutdownServer();
  return NS_OK;
}
