/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"

#include "mozilla/_ipdltest/IPDLUnitTest.h"
#include "mozilla/ipc/NodeController.h"
#include "mozilla/ipc/ProtocolUtils.h"
#include "mozilla/ipc/ProcessChild.h"
#include "mozilla/ipc/ProcessUtils.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "nsDebugImpl.h"
#include "nsThreadManager.h"

#ifdef MOZ_WIDGET_ANDROID
#  include "nsIAppShell.h"
#  include "nsServiceManagerUtils.h"
#  include "nsWidgetsCID.h"
#endif

namespace mozilla::_ipdltest {

MOZ_RUNINIT static std::unordered_map<std::string_view,
                                      ipc::IToplevelProtocol* (*)()>
    sAllocChildActorRegistry;

const char* RegisterAllocChildActor(const char* aName,
                                    ipc::IToplevelProtocol* (*aFunc)()) {
  sAllocChildActorRegistry[aName] = aFunc;
  return aName;
}

already_AddRefed<IPDLUnitTestParent> IPDLUnitTestParent::CreateCrossProcess() {
#ifdef MOZ_WIDGET_ANDROID
  // Force-initialize the appshell on android, as android child process
  // launching depends on widget component initialization.
  nsCOMPtr<nsIAppShell> _appShell = do_GetService(NS_APPSHELL_CID);
#endif

  RefPtr parent = MakeRefPtr<IPDLUnitTestParent>();
  parent->mSubprocess =
      new ipc::GeckoChildProcessHost(GeckoProcessType_IPDLUnitTest);

  geckoargs::ChildProcessArgs extraArgs;

  auto prefSerializer = std::make_unique<ipc::SharedPreferenceSerializer>();
  if (!prefSerializer->SerializeToSharedMemory(GeckoProcessType_IPDLUnitTest,
                                               /* remoteType */ {})) {
    ADD_FAILURE()
        << "SharedPreferenceSerializer::SerializeToSharedMemory failed";
    return nullptr;
  }
  prefSerializer->AddSharedPrefCmdLineArgs(*parent->mSubprocess, extraArgs);

  if (!parent->mSubprocess->SyncLaunch(std::move(extraArgs))) {
    ADD_FAILURE() << "Subprocess launch failed";
    return nullptr;
  }

  if (!parent->mSubprocess->TakeInitialEndpoint().Bind(parent.get())) {
    ADD_FAILURE() << "Opening the parent actor failed";
    return nullptr;
  }

  EXPECT_TRUE(parent->CanSend());
  return parent.forget();
}

already_AddRefed<IPDLUnitTestParent> IPDLUnitTestParent::CreateCrossThread() {
  RefPtr parent = MakeRefPtr<IPDLUnitTestParent>();
  RefPtr child = MakeRefPtr<IPDLUnitTestChild>();

  nsresult rv =
      NS_NewNamedThread("IPDL UnitTest", getter_AddRefs(parent->mOtherThread));
  if (NS_FAILED(rv)) {
    ADD_FAILURE() << "Failed to create IPDLUnitTest thread";
    return nullptr;
  }
  if (!parent->Open(child, parent->mOtherThread)) {
    ADD_FAILURE() << "Opening the actor failed";
    return nullptr;
  }

  EXPECT_TRUE(parent->CanSend());
  return parent.forget();
}

IPDLUnitTestParent::~IPDLUnitTestParent() {
  mSubprocess = nullptr;
  if (mOtherThread) {
    mOtherThread->Shutdown();
  }
}

bool IPDLUnitTestParent::Start(const char* aName,
                               ipc::IToplevelProtocol* aActor) {
  nsID channelId = nsID::GenerateUUID();
  auto [parentPort, childPort] =
      ipc::NodeController::GetSingleton()->CreatePortPair();
  if (!SendStart(nsDependentCString(aName), std::move(childPort), channelId)) {
    ADD_FAILURE() << "IPDLUnitTestParent::SendStart failed";
    return false;
  }
  if (!aActor->Open(std::move(parentPort), channelId,
                    OtherEndpointProcInfo())) {
    ADD_FAILURE() << "Unable to open parent actor";
    return false;
  }
  return true;
}

ipc::IPCResult IPDLUnitTestParent::RecvReport(const TestPartResult& aReport) {
  if (!aReport.failed()) {
    return IPC_OK();
  }

  // Report the failure
  ADD_FAILURE_AT(aReport.filename().get(), aReport.lineNumber())
      << "[child " << OtherPid() << "] " << aReport.summary();

  // If the failure was fatal, kill the child process to avoid hangs.
  if (aReport.fatal()) {
    KillHard();
  }
  return IPC_OK();
}

ipc::IPCResult IPDLUnitTestParent::RecvComplete() {
  mComplete = true;
  Close();
  return IPC_OK();
}

void IPDLUnitTestParent::KillHard() {
  if (mCalledKillHard) {
    return;
  }
  mCalledKillHard = true;

  // We can't effectively kill a same-process situation, but we can trigger
  // shutdown early to avoid hanging.
  if (mOtherThread) {
    Close();
    nsCOMPtr<nsIThread> otherThread = mOtherThread.forget();
    otherThread->Shutdown();
  }

  if (mSubprocess) {
    ProcessHandle handle = mSubprocess->GetChildProcessHandle();
    if (!base::KillProcess(handle, base::PROCESS_END_KILLED_BY_USER)) {
      NS_WARNING("failed to kill subprocess!");
    }
    mSubprocess->SetAlreadyDead();
  }
}

ipc::IPCResult IPDLUnitTestChild::RecvStart(const nsCString& aName,
                                            ipc::ScopedPort aPort,
                                            const nsID& aMessageChannelId) {
  auto* allocChildActor =
      sAllocChildActorRegistry[std::string_view{aName.get()}];
  if (!allocChildActor) {
    ADD_FAILURE() << "No AllocChildActor for name " << aName.get()
                  << " registered!";
    return IPC_FAIL(this, "No AllocChildActor registered!");
  }

  // Store references to the node & port to watch for test completion.
  RefPtr<ipc::NodeController> controller = aPort.Controller();
  mojo::core::ports::PortRef port = aPort.Port();

  RefPtr<IToplevelProtocol> child = allocChildActor();
  if (!child->Open(std::move(aPort), aMessageChannelId,
                   OtherEndpointProcInfo())) {
    ADD_FAILURE() << "Unable to open child actor";
    return IPC_FAIL(this, "Unable to open child actor");
  }

  // Wait for the port which was created for this actor to be fully torn down.
  SpinEventLoopUntil("IPDLUnitTestChild::RecvStart"_ns,
                     [&] { return controller->GetStatus(port).isNothing(); });

  // Tear down the test actor to end the test.
  SendComplete();
  return IPC_OK();
}

void IPDLUnitTestChild::ActorDestroy(ActorDestroyReason /*aWhy*/) {
  if (!XRE_IsParentProcess()) {
    XRE_ShutdownChildProcess();
  }
}

void IPDLTestHelper::TestWrapper(bool aCrossProcess) {
  // Create the host and start the test actor with it.
  RefPtr<IPDLUnitTestParent> host =
      aCrossProcess ? IPDLUnitTestParent::CreateCrossProcess()
                    : IPDLUnitTestParent::CreateCrossThread();
  ASSERT_TRUE(host);
  if (!host->Start(GetName(), GetActor())) {
    FAIL();
  }

  // XXX: Consider adding a test timeout?

  // Run the test body. This will send the initial messages to our actor, which
  // will eventually clean itself up.
  TestBody();

  // Spin the event loop until the test wrapper host has fully shut down.
  SpinEventLoopUntil("IPDLTestHelper::TestWrapper"_ns,
                     [&] { return !host->CanSend(); });

  EXPECT_TRUE(host->ReportedComplete())
      << "child process exited without signalling completion";
}

// Listener registered within the IPDLUnitTest process used to relay GTest
// failures to the parent process, so that the are marked as failing the overall
// gtest.
class IPDLChildProcessTestListener : public testing::EmptyTestEventListener {
 public:
  explicit IPDLChildProcessTestListener(IPDLUnitTestChild* aActor)
      : mActor(aActor) {}

  virtual void OnTestPartResult(
      const testing::TestPartResult& aTestPartResult) override {
    mActor->SendReport(TestPartResult(
        aTestPartResult.failed(), aTestPartResult.fatally_failed(),
        nsDependentCString(aTestPartResult.file_name()),
        aTestPartResult.line_number(),
        nsDependentCString(aTestPartResult.summary()),
        nsDependentCString(aTestPartResult.message())));
  }

  RefPtr<IPDLUnitTestChild> mActor;
};

// ProcessChild instance used to run the IPDLUnitTest process.
class IPDLUnitTestProcessChild : public ipc::ProcessChild {
 public:
  using ipc::ProcessChild::ProcessChild;
  bool Init(int aArgc, char* aArgv[]) override {
    nsDebugImpl::SetMultiprocessMode("IPDLUnitTest");

    if (!ProcessChild::InitPrefs(aArgc, aArgv)) {
      MOZ_CRASH("InitPrefs failed");
      return false;
    }

    if (NS_WARN_IF(NS_FAILED(nsThreadManager::get().Init()))) {
      MOZ_CRASH("nsThreadManager initialization failed");
      return false;
    }

    RefPtr child = MakeRefPtr<IPDLUnitTestChild>();
    if (!TakeInitialEndpoint().Bind(child.get())) {
      MOZ_CRASH("Bind of IPDLUnitTestChild failed");
      return false;
    }

    // Register a listener to forward test results from the child process to the
    // parent process to be handled there.
    mListener = new IPDLChildProcessTestListener(child);
    testing::UnitTest::GetInstance()->listeners().Append(mListener);

    if (NS_FAILED(NS_InitMinimalXPCOM())) {
      MOZ_CRASH("NS_InitMinimalXPCOM failed");
      return false;
    }

    ipc::SetThisProcessName("IPDLUnitTest");
    return true;
  }

  void CleanUp() override {
    // Clean up the test listener we registered to get a clean shutdown.
    if (mListener) {
      testing::UnitTest::GetInstance()->listeners().Release(mListener);
      delete mListener;
    }

    NS_ShutdownXPCOM(nullptr);
  }

  IPDLChildProcessTestListener* mListener = nullptr;
};

// Defined in nsEmbedFunctions.cpp
extern UniquePtr<ipc::ProcessChild> (*gMakeIPDLUnitTestProcessChild)(
    IPC::Channel::ChannelHandle, base::ProcessId, const nsID&);

// Initialize gMakeIPDLUnitTestProcessChild in a static constructor.
MOZ_RUNINIT int _childProcessEntryPointStaticConstructor = ([] {
  gMakeIPDLUnitTestProcessChild =
      [](IPC::Channel::ChannelHandle aClientChannel, base::ProcessId aParentPid,
         const nsID& aMessageChannelId) -> UniquePtr<ipc::ProcessChild> {
    return std::make_unique<IPDLUnitTestProcessChild>(
        std::move(aClientChannel), aParentPid, aMessageChannelId);
  };
  return 0;
})();

}  // namespace mozilla::_ipdltest
