/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "VRProcessManager.h"

#include "VRChild.h"
#include "VRGPUChild.h"
#include "VRGPUParent.h"
#include "VRProcessParent.h"
#include "mozilla/MemoryReportingProcess.h"
#include "mozilla/Preferences.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/ipc/Endpoint.h"

namespace mozilla {
namespace gfx {

static StaticAutoPtr<VRProcessManager> sSingleton;

/* static */
VRProcessManager* VRProcessManager::Get() { return sSingleton; }

/* static */
void VRProcessManager::Initialize() {
  MOZ_RELEASE_ASSERT(XRE_IsParentProcess());
  if (sSingleton == nullptr) {
    sSingleton = new VRProcessManager();
  }
}

/* static */
void VRProcessManager::Shutdown() { sSingleton = nullptr; }

VRProcessManager::VRProcessManager() : mProcess(nullptr), mVRChild(nullptr) {
  MOZ_COUNT_CTOR(VRProcessManager);

  mObserver = new Observer(this);
  nsContentUtils::RegisterShutdownObserver(mObserver);
  Preferences::AddStrongObserver(mObserver, "");
}

VRProcessManager::~VRProcessManager() {
  MOZ_COUNT_DTOR(VRProcessManager);

  if (mObserver) {
    nsContentUtils::UnregisterShutdownObserver(mObserver);
    Preferences::RemoveObserver(mObserver, "");
    mObserver = nullptr;
  }

  DestroyProcess();
  // The VR process should have already been shut down.
  MOZ_RELEASE_ASSERT(!mProcess);
}

void VRProcessManager::LaunchVRProcess() {
  if (mProcess) {
    return;
  }

  // The subprocess is launched asynchronously, so we wait for a callback to
  // acquire the IPDL actor.
  mProcess = new VRProcessParent(this);
  if (!mProcess->Launch()) {
    DisableVRProcess("Failed to launch VR process");
  }
}

void VRProcessManager::DisableVRProcess(const char* aMessage) {
  if (!StaticPrefs::dom_vr_process_enabled_AtStartup()) {
    return;
  }

  DestroyProcess();
}

void VRProcessManager::DestroyProcess() {
  if (!mProcess) {
    return;
  }

  mProcess->Shutdown();
  mProcess = nullptr;
  mVRChild = nullptr;

  CrashReporter::RecordAnnotationCString(
      CrashReporter::Annotation::VRProcessStatus, "Destroyed");
}

bool VRProcessManager::EnsureVRReady() {
  if (mProcess && !mProcess->IsConnected()) {
    if (!mProcess->WaitForLaunch()) {
      // If this fails, we should have fired OnProcessLaunchComplete and
      // removed the process.
      MOZ_ASSERT(!mProcess && !mVRChild);
      return false;
    }
  }

  if (mVRChild) {
    if (mVRChild->EnsureVRReady()) {
      return true;
    }

    // If the initialization above fails, we likely have a GPU process teardown
    // waiting in our message queue (or will soon). We need to ensure we don't
    // restart it later because if we fail here, our callers assume they should
    // fall back to a combined UI/GPU process. This also ensures our internal
    // state is consistent (e.g. process token is reset).
    DisableVRProcess("Failed to initialize VR process");
  }

  return false;
}

void VRProcessManager::OnProcessLaunchComplete(VRProcessParent* aParent) {
  MOZ_RELEASE_ASSERT(mProcess && mProcess == aParent);

  mVRChild = mProcess->GetActor();

  if (!mProcess->IsConnected()) {
    DestroyProcess();
    return;
  }

  // Flush any pref updates that happened during launch and weren't
  // included in the blobs set up in LaunchGPUProcess.
  for (const mozilla::dom::Pref& pref : mQueuedPrefs) {
    (void)NS_WARN_IF(!mVRChild->SendPreferenceUpdate(pref));
  }
  mQueuedPrefs.Clear();

  CrashReporter::RecordAnnotationCString(
      CrashReporter::Annotation::VRProcessStatus, "Running");
}

void VRProcessManager::OnProcessUnexpectedShutdown(VRProcessParent* aParent) {
  MOZ_RELEASE_ASSERT(mProcess && mProcess == aParent);

  DestroyProcess();
}

bool VRProcessManager::CreateGPUBridges(
    mozilla::ipc::EndpointProcInfo aOtherProcess,
    mozilla::ipc::Endpoint<PVRGPUChild>* aOutVRBridge) {
  if (!CreateGPUVRManager(aOtherProcess, aOutVRBridge)) {
    return false;
  }
  return true;
}

bool VRProcessManager::CreateGPUVRManager(
    mozilla::ipc::EndpointProcInfo aOtherProcess,
    mozilla::ipc::Endpoint<PVRGPUChild>* aOutEndpoint) {
  if (mProcess && !mProcess->IsConnected()) {
    NS_WARNING("VR process haven't connected with the parent process yet");
    return false;
  }

  ipc::EndpointProcInfo vrparentInfo =
      mProcess ? ipc::EndpointProcInfo{.mPid = mProcess->GetChildProcessId(),
                                       .mChildID = mProcess->GetChildID()}
               : ipc::EndpointProcInfo::Current();

  ipc::Endpoint<PVRGPUParent> vrparentPipe;
  ipc::Endpoint<PVRGPUChild> vrchildPipe;
  nsresult rv = PVRGPU::CreateEndpoints(vrparentInfo, aOtherProcess,
                                        &vrparentPipe, &vrchildPipe);

  if (NS_FAILED(rv)) {
    gfxCriticalNote << "Could not create gpu-vr bridge: " << hexa(int(rv));
    return false;
  }

  // Bind vr-gpu pipe to VRParent and make a PVRGPU connection.
  VRChild* vrChild = mProcess->GetActor();
  vrChild->SendNewGPUVRManager(std::move(vrparentPipe));

  *aOutEndpoint = std::move(vrchildPipe);
  return true;
}

NS_IMPL_ISUPPORTS(VRProcessManager::Observer, nsIObserver);

VRProcessManager::Observer::Observer(VRProcessManager* aManager)
    : mManager(aManager) {}

NS_IMETHODIMP
VRProcessManager::Observer::Observe(nsISupports* aSubject, const char* aTopic,
                                    const char16_t* aData) {
  if (!strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID)) {
    mManager->OnXPCOMShutdown();
  } else if (!strcmp(aTopic, "nsPref:changed")) {
    mManager->OnPreferenceChange(aData);
  }
  return NS_OK;
}

void VRProcessManager::CleanShutdown() { DestroyProcess(); }

void VRProcessManager::OnXPCOMShutdown() {
  if (mObserver) {
    nsContentUtils::UnregisterShutdownObserver(mObserver);
    Preferences::RemoveObserver(mObserver, "");
    mObserver = nullptr;
  }

  CleanShutdown();
}

void VRProcessManager::OnPreferenceChange(const char16_t* aData) {
  // We know prefs are ASCII here.
  NS_LossyConvertUTF16toASCII strData(aData);

  mozilla::dom::Pref pref(strData, /* isLocked */ false,
                          /* isSanitized */ false, Nothing(), Nothing());

  Preferences::GetPreference(&pref, GeckoProcessType_VR,
                             /* remoteType */ {});
  if (!!mVRChild) {
    MOZ_RELEASE_ASSERT(mQueuedPrefs.IsEmpty());
    mVRChild->SendPreferenceUpdate(pref);
  } else {
    mQueuedPrefs.AppendElement(pref);
  }
}

VRChild* VRProcessManager::GetVRChild() { return mProcess->GetActor(); }

class VRMemoryReporter : public MemoryReportingProcess {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(VRMemoryReporter, override)

  bool IsAlive() const override {
    if (VRProcessManager* vpm = VRProcessManager::Get()) {
      return !!vpm->GetVRChild();
    }
    return false;
  }

  bool SendRequestMemoryReport(
      const uint32_t& aGeneration, const bool& aAnonymize,
      const bool& aMinimizeMemoryUsage,
      const Maybe<ipc::FileDescriptor>& aDMDFile) override {
    VRChild* child = GetChild();
    if (!child) {
      return false;
    }

    return child->SendRequestMemoryReport(aGeneration, aAnonymize,
                                          aMinimizeMemoryUsage, aDMDFile);
  }

  int32_t Pid() const override {
    if (VRChild* child = GetChild()) {
      return (int32_t)child->OtherPid();
    }
    return 0;
  }

 private:
  VRChild* GetChild() const {
    if (VRProcessManager* vpm = VRProcessManager::Get()) {
      if (VRChild* child = vpm->GetVRChild()) {
        return child;
      }
    }
    return nullptr;
  }

 protected:
  ~VRMemoryReporter() = default;
};

RefPtr<MemoryReportingProcess> VRProcessManager::GetProcessMemoryReporter() {
  if (!EnsureVRReady()) {
    return nullptr;
  }
  return new VRMemoryReporter();
}

}  // namespace gfx
}  // namespace mozilla
