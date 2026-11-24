/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ForkServiceChild.h"
#include "ForkServer.h"
#include "chrome/common/process_watcher.h"
#include "mozilla/Atomics.h"
#include "mozilla/Logging.h"
#include "mozilla/ipc/GeckoChildProcessHost.h"
#include "mozilla/ipc/ProtocolMessageUtils.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/Services.h"
#include "ipc/IPCMessageUtilsSpecializations.h"
#include "nsIObserverService.h"

#include <unistd.h>
#include <fcntl.h>

namespace mozilla {
namespace ipc {

extern LazyLogModule gForkServiceLog;

StaticMutex ForkServiceChild::sMutex;
StaticRefPtr<ForkServiceChild> ForkServiceChild::sSingleton;
Atomic<bool> ForkServiceChild::sForkServiceUsed;

#ifndef SOCK_CLOEXEC
static bool ConfigurePipeFd(int aFd) {
  int flags = fcntl(aFd, F_GETFD, 0);
  return flags != -1 && fcntl(aFd, F_SETFD, flags | FD_CLOEXEC) != -1;
}
#endif

// Create a socketpair with both ends marked as close-on-exec
static Result<Ok, LaunchError> CreateSocketPair(UniqueFileHandle& aFD0,
                                                UniqueFileHandle& aFD1) {
  int fds[2];
#ifdef SOCK_CLOEXEC
  constexpr int type = SOCK_STREAM | SOCK_CLOEXEC;
#else
  constexpr int type = SOCK_STREAM;
#endif

  if (socketpair(AF_UNIX, type, 0, fds) < 0) {
    return Err(LaunchError("FSC::CSP::sp", errno));
  }

#ifndef SOCK_CLOEXEC
  if (!ConfigurePipeFd(server.get()) || !ConfigurePipeFd(client.get())) {
    return Err(LaunchError("FSC::CSP::cfg", errno));
  }
#endif

  aFD0.reset(fds[0]);
  aFD1.reset(fds[1]);

  return Ok();
}

void ForkServiceChild::StartForkServer() {
  UniqueFileHandle server;
  UniqueFileHandle client;
  if (CreateSocketPair(server, client).isErr()) {
    MOZ_LOG(gForkServiceLog, LogLevel::Error,
            ("failed to create fork server socket"));
    return;
  }

  RefPtr subprocess =
      MakeRefPtr<GeckoChildProcessHost>(GeckoProcessType_ForkServer, false);

  geckoargs::ChildProcessArgs extraOpts;
  geckoargs::sIPCHandle.Put(std::move(client), extraOpts);
  geckoargs::sSignalPipe.Put(ProcessWatcher::GetSignalPipe(), extraOpts);

  if (!subprocess->LaunchAndWaitForProcessHandle(std::move(extraOpts))) {
    MOZ_LOG(gForkServiceLog, LogLevel::Error, ("failed to launch fork server"));
    return;
  }

  sForkServiceUsed = true;
  StaticMutexAutoLock smal(sMutex);
  // Can't use MakeRefPtr; ctor is private.
  MOZ_ASSERT(sSingleton == nullptr);
  sSingleton = new ForkServiceChild(server.release(), subprocess);
}

void ForkServiceChild::StopForkServer() {
  RefPtr<ForkServiceChild> oldChild;
  {
    StaticMutexAutoLock smal(sMutex);
    oldChild = sSingleton.forget();
  }
  // Drop the old reference outside of the lock to avoid lock order
  // cycles via the GeckoChildProcessHost dtor.
}

RefPtr<ForkServiceChild> ForkServiceChild::Get() {
  RefPtr<ForkServiceChild> child;
  {
    StaticMutexAutoLock smal(sMutex);
    child = sSingleton;
  }
  return child;
}

ForkServiceChild::ForkServiceChild(int aFd, GeckoChildProcessHost* aProcess)
    : mMutex("mozilla.ipc.ForkServiceChild.mMutex"),
      mFailed(false),
      mProcess(aProcess) {
  mTcver = std::make_unique<MiniTransceiver>(aFd);
}

ForkServiceChild::~ForkServiceChild() { close(mTcver->GetFD()); }

Result<Ok, LaunchError> ForkServiceChild::SendForkNewSubprocess(
    geckoargs::ChildProcessArgs&& aArgs, base::LaunchOptions&& aOptions,
    pid_t* aPid) {
  // Double-check there are no unsupported options.
  MOZ_ASSERT(aOptions.workdir.empty());
  MOZ_ASSERT(!aOptions.full_env);
  MOZ_ASSERT(!aOptions.wait);
  MOZ_ASSERT(aOptions.fds_to_remap.size() == aArgs.mFiles.size());

  MutexAutoLock lock(mMutex);
  if (mFailed) {
    return Err(LaunchError("FSC::SFNS::Failed"));
  }

  UniqueFileHandle execParent;
  {
    UniqueFileHandle execChild;
    IPC::Message msg(MSG_ROUTING_CONTROL, Msg_ForkNewSubprocess__ID);

    MOZ_TRY(CreateSocketPair(execParent, execChild));

    IPC::MessageWriter writer(msg);
#if defined(XP_LINUX) && defined(MOZ_SANDBOX)
    WriteParam(&writer, aOptions.fork_flags);
    WriteParam(&writer, std::move(aOptions.sandbox_chroot_server));
#endif
    WriteParam(&writer, std::move(execChild));
    if (!mTcver->Send(msg)) {
      MOZ_LOG(gForkServiceLog, LogLevel::Verbose,
              ("the pipe to the fork server is closed or having errors"));
      OnError();
      return Err(LaunchError("FSC::SFNS::Send"));
    }
  }

  {
    MiniTransceiver execTcver(execParent.get());
    IPC::Message execMsg(MSG_ROUTING_CONTROL, Msg_SubprocessExecInfo__ID);
    IPC::MessageWriter execWriter(execMsg);
    WriteParam(&execWriter, aOptions.env_map);
    WriteParam(&execWriter, aArgs.mArgs);
    WriteParam(&execWriter, std::move(aArgs.mFiles));
    if (!execTcver.Send(execMsg)) {
      MOZ_LOG(gForkServiceLog, LogLevel::Verbose,
              ("failed to send exec info to the fork server"));
      OnError();
      return Err(LaunchError("FSC::SFNS::Send2"));
    }
  }
  execParent = nullptr;

  UniquePtr<IPC::Message> reply;
  if (!mTcver->Recv(reply)) {
    MOZ_LOG(gForkServiceLog, LogLevel::Verbose,
            ("the pipe to the fork server is closed or having errors"));
    OnError();
    return Err(LaunchError("FSC::SFNS::Recv"));
  }

  if (reply->type() != Reply_ForkNewSubprocess__ID) {
    MOZ_LOG(gForkServiceLog, LogLevel::Verbose,
            ("unknown reply type %d", reply->type()));
    return Err(LaunchError("FSC::SFNS::Type"));
  }
  IPC::MessageReader reader(*reply);

  if (!ReadParam(&reader, aPid)) {
    MOZ_CRASH("Error deserializing 'pid_t'");
  }
  reader.EndRead();

  return Ok();
}

auto ForkServiceChild::SendWaitPid(pid_t aPid, bool aBlock)
    -> Result<ProcStatus, int> {
  MutexAutoLock lock(mMutex);
  if (mFailed) {
    return Err(ECONNRESET);
  }

  IPC::Message msg(MSG_ROUTING_CONTROL, Msg_WaitPid__ID);
  IPC::MessageWriter writer(msg);
  WriteParam(&writer, aPid);
  WriteParam(&writer, aBlock);

  if (!mTcver->Send(msg)) {
    MOZ_LOG(gForkServiceLog, LogLevel::Verbose,
            ("the pipe to the fork server is closed or having errors"));
    OnError();
    return Err(ECONNRESET);
  }

  UniquePtr<IPC::Message> reply;
  if (!mTcver->Recv(reply)) {
    MOZ_LOG(gForkServiceLog, LogLevel::Verbose,
            ("the pipe to the fork server is closed or having errors"));
    OnError();
    return Err(ECONNRESET);
  }

  if (reply->type() != Reply_WaitPid__ID) {
    MOZ_LOG(gForkServiceLog, LogLevel::Verbose,
            ("unknown reply type %d", reply->type()));
    OnError();
    return Err(EPROTO);
  }
  IPC::MessageReader reader(*reply);

  // Both sides of the Result are isomorphic to int.
  bool isErr = false;
  int value = 0;
  if (!ReadParam(&reader, &isErr) || !ReadParam(&reader, &value)) {
    MOZ_LOG(gForkServiceLog, LogLevel::Verbose,
            ("deserialization error in waitpid reply"));
    OnError();
    return Err(EPROTO);
  }

  // This can't use ?: because the types are different.
  if (isErr) {
    return Err(value);
  }
  return ProcStatus{value};
}

void ForkServiceChild::OnError() {
  mFailed = true;
  ForkServerLauncher::RestartForkServer();
}

NS_IMPL_ISUPPORTS(ForkServerLauncher, nsIObserver)

bool ForkServerLauncher::sHaveStartedClient = false;
StaticRefPtr<ForkServerLauncher> ForkServerLauncher::sSingleton;

ForkServerLauncher::ForkServerLauncher() = default;

already_AddRefed<ForkServerLauncher> ForkServerLauncher::Create() {
  if (sSingleton == nullptr) {
    sSingleton = new ForkServerLauncher();
  }
  RefPtr<ForkServerLauncher> launcher = sSingleton;
  return launcher.forget();
}

NS_IMETHODIMP
ForkServerLauncher::Observe(nsISupports* /*aSubject*/, const char* aTopic,
                            const char16_t* /*aData*/) {
  if (strcmp(aTopic, NS_XPCOM_STARTUP_CATEGORY) == 0) {
    nsCOMPtr<nsIObserverService> obsSvc =
        mozilla::services::GetObserverService();
    MOZ_ASSERT(obsSvc != nullptr);
    // preferences are not available until final-ui-startup
    obsSvc->AddObserver(this, "final-ui-startup", false);
  } else if (!sHaveStartedClient && strcmp(aTopic, "final-ui-startup") == 0) {
    if (StaticPrefs::dom_ipc_forkserver_enable_AtStartup()) {
      sHaveStartedClient = true;
      ForkServiceChild::StartForkServer();

      nsCOMPtr<nsIObserverService> obsSvc =
          mozilla::services::GetObserverService();
      MOZ_ASSERT(obsSvc != nullptr);
      obsSvc->AddObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID, false);
    } else {
      sSingleton = nullptr;
    }
  }

  if (strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID) == 0) {
    // To make leak checker happy!
    sSingleton = nullptr;
  }

  return NS_OK;
}

void ForkServerLauncher::RestartForkServer() {
  // Restart fork server
  (void)NS_DispatchToMainThreadQueue(
      NS_NewRunnableFunction("OnForkServerError",
                             [] {
                               if (sSingleton) {
                                 ForkServiceChild::StopForkServer();
                                 ForkServiceChild::StartForkServer();
                               }
                             }),
      EventQueuePriority::Idle);
}

}  // namespace ipc
}  // namespace mozilla
