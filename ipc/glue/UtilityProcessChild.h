/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef _include_ipc_glue_UtilityProcessChild_h_
#define _include_ipc_glue_UtilityProcessChild_h_
#include "mozilla/ipc/PUtilityProcessChild.h"
#include "mozilla/ipc/UtilityProcessSandboxing.h"
#include "mozilla/ipc/UtilityMediaServiceParent.h"
#ifndef ANDROID
#  include "mozilla/hwinference/HWInferenceChild.h"
#endif  // !ANDROID
#include "ChildProfilerController.h"

#if defined(NIGHTLY_BUILD) && !defined(MOZ_NO_SMART_CARDS)
#  include "mozilla/psm/PKCS11ModuleChild.h"
#endif  // NIGHTLY_BUILD && !MOZ_NO_SMART_CARDS

#if defined(MOZ_SANDBOX) && defined(MOZ_DEBUG) && defined(ENABLE_TESTS)
#  include "mozilla/PSandboxTestingChild.h"
#endif
#include "mozilla/PRemoteMediaManagerParent.h"
#include "mozilla/ipc/AsyncBlockers.h"
#include "mozilla/dom/JSOracleChild.h"
#include "mozilla/ProfilerMarkers.h"

namespace mozilla::dom {
class PJSOracleChild;
}  // namespace mozilla::dom

namespace mozilla::ipc {

class UtilityProcessHost;

class UtilityProcessChild final : public PUtilityProcessChild {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(UtilityProcessChild, override);

  UtilityProcessChild();

  static RefPtr<UtilityProcessChild> GetSingleton();
  static RefPtr<UtilityProcessChild> Get();

  SandboxingKind mSandbox{};

  bool Init(mozilla::ipc::UntypedEndpoint&& aEndpoint,
            const nsCString& aParentBuildID, SandboxingKind aSandboxingKind);

  mozilla::ipc::IPCResult RecvInit(const Maybe<ipc::FileDescriptor>& aBrokerFd,
                                   const bool& aCanRecordReleaseTelemetry,
                                   const bool& aIsReadyForBackgroundProcessing);
  mozilla::ipc::IPCResult RecvInitProfiler(
      Endpoint<PProfilerChild>&& aEndpoint);

  mozilla::ipc::IPCResult RecvPreferenceUpdate(const Pref& pref);

  mozilla::ipc::IPCResult RecvRequestMemoryReport(
      const uint32_t& generation, const bool& anonymize,
      const bool& minimizeMemoryUsage,
      const Maybe<ipc::FileDescriptor>& DMDFile,
      const RequestMemoryReportResolver& aResolver);

  mozilla::ipc::IPCResult RecvFlushFOGData(FlushFOGDataResolver&& aResolver);

  mozilla::ipc::IPCResult RecvTestTriggerMetrics(
      TestTriggerMetricsResolver&& aResolve);

  mozilla::ipc::IPCResult RecvTestTelemetryProbes();

  mozilla::ipc::IPCResult RecvStartUtilityMediaService(
      Endpoint<PUtilityMediaServiceParent>&& aEndpoint,
      nsTArray<gfx::GfxVarUpdate>&& aUpdates);

  mozilla::ipc::IPCResult RecvStartJSOracleService(
      Endpoint<dom::PJSOracleChild>&& aEndpoint);

#if defined(XP_WIN)
  mozilla::ipc::IPCResult RecvStartWindowsUtilsService(
      Endpoint<PWindowsUtilsChild>&& aEndpoint);

  mozilla::ipc::IPCResult RecvStartWinFileDialogService(
      Endpoint<PWinFileDialogChild>&& aEndpoint);

  mozilla::ipc::IPCResult RecvGetUntrustedModulesData(
      GetUntrustedModulesDataResolver&& aResolver);
  mozilla::ipc::IPCResult RecvUnblockUntrustedModulesThread();
#endif  // defined(XP_WIN)

  AsyncBlockers& AsyncShutdownService() { return mShutdownBlockers; }

#if defined(NIGHTLY_BUILD) && !defined(MOZ_NO_SMART_CARDS)
  IPCResult RecvStartPKCS11ModuleService(
      Endpoint<PPKCS11ModuleChild>&& aEndpoint, nsCString&& aProfilePath);
#endif  // NIGHTLY_BUILD && !MOZ_NO_SMART_CARDS

#ifndef ANDROID
  mozilla::ipc::IPCResult RecvStartHWInferenceService(
      Endpoint<PHWInferenceChild>&& aEndpoint);
#endif  // !ANDROID

  void ActorDestroy(ActorDestroyReason aWhy) override;

#if defined(MOZ_SANDBOX) && defined(MOZ_DEBUG) && defined(ENABLE_TESTS)
  mozilla::ipc::IPCResult RecvInitSandboxTesting(
      Endpoint<PSandboxTestingChild>&& aEndpoint);
#endif

  RefPtr<UtilityMediaServiceParent> GetMediaService() const {
    return mUtilityMediaServiceInstance;
  }

#ifndef ANDROID
  hwinference::HWInferenceChild* GetHWInferenceChild() const {
    return mHWInferenceInstance;
  }
#endif  // !ANDROID

 protected:
  friend class UtilityProcessImpl;
  ~UtilityProcessChild();

 private:
  TimeStamp mChildStartTime;
  RefPtr<ChildProfilerController> mProfilerController;
  RefPtr<UtilityMediaServiceParent> mUtilityMediaServiceInstance{};
  RefPtr<dom::JSOracleChild> mJSOracleInstance{};
#ifndef ANDROID
  RefPtr<hwinference::HWInferenceChild> mHWInferenceInstance{};
#endif  // !ANDROID
#ifdef XP_WIN
  RefPtr<PWindowsUtilsChild> mWindowsUtilsInstance;
#endif
#if defined(NIGHTLY_BUILD) && !defined(MOZ_NO_SMART_CARDS)
  RefPtr<psm::PKCS11ModuleChild> mPKCS11ModuleInstance;
#endif  // NIGHTLY_BUILD && !MOZ_NO_SMART_CARDS

  AsyncBlockers mShutdownBlockers;
};

}  // namespace mozilla::ipc

#endif  // _include_ipc_glue_UtilityProcessChild_h_
