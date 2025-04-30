/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_SocketProcessChild_h
#define mozilla_net_SocketProcessChild_h

#include "mozilla/net/PSocketProcessChild.h"
#include "mozilla/ipc/InputStreamUtils.h"
#include "mozilla/psm/IPCClientCertsChild.h"
#include "mozilla/Mutex.h"
#include "nsRefPtrHashtable.h"
#include "nsTHashMap.h"

#if defined(MOZ_SANDBOX) && defined(MOZ_DEBUG) && defined(ENABLE_TESTS)
#  include "mozilla/PSandboxTestingChild.h"
#endif

namespace mozilla {
class ChildProfilerController;
}

namespace mozilla {
namespace net {

class ProxyAutoConfigChild;
class SocketProcessBackgroundChild;
class SocketProcessBridgeParent;
class BackgroundDataBridgeParent;

// The IPC actor implements PSocketProcessChild in child process.
// This is allocated and kept alive by SocketProcessImpl.
class SocketProcessChild final : public PSocketProcessChild {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(SocketProcessChild, final)

  SocketProcessChild();

  static SocketProcessChild* GetSingleton();

  bool Init(mozilla::ipc::UntypedEndpoint&& aEndpoint,
            const char* aParentBuildID);

  void ActorDestroy(ActorDestroyReason aWhy) override;

  mozilla::ipc::IPCResult RecvInit(
      const SocketPorcessInitAttributes& aAttributes);
  mozilla::ipc::IPCResult RecvPreferenceUpdate(const Pref& aPref);
  mozilla::ipc::IPCResult RecvRequestMemoryReport(
      const uint32_t& generation, const bool& anonymize,
      const bool& minimizeMemoryUsage,
      const Maybe<mozilla::ipc::FileDescriptor>& DMDFile,
      const RequestMemoryReportResolver& aResolver);
  mozilla::ipc::IPCResult RecvSetOffline(const bool& aOffline);
  mozilla::ipc::IPCResult RecvSetConnectivity(const bool& aConnectivity);
  mozilla::ipc::IPCResult RecvInitLinuxSandbox(
      const Maybe<ipc::FileDescriptor>& aBrokerFd);
  mozilla::ipc::IPCResult RecvInitSocketProcessBridgeParent(
      const ProcessId& aContentProcessId,
      Endpoint<mozilla::net::PSocketProcessBridgeParent>&& aEndpoint);
  mozilla::ipc::IPCResult RecvInitProfiler(
      Endpoint<mozilla::PProfilerChild>&& aEndpoint);
#if defined(MOZ_SANDBOX) && defined(MOZ_DEBUG) && defined(ENABLE_TESTS)
  mozilla::ipc::IPCResult RecvInitSandboxTesting(
      Endpoint<PSandboxTestingChild>&& aEndpoint);
#endif
  mozilla::ipc::IPCResult RecvSocketProcessTelemetryPing();

  PWebrtcTCPSocketChild* AllocPWebrtcTCPSocketChild(const Maybe<TabId>& tabId);
  bool DeallocPWebrtcTCPSocketChild(PWebrtcTCPSocketChild* aActor);

  already_AddRefed<PHttpTransactionChild> AllocPHttpTransactionChild();

  void CleanUp();
  void DestroySocketProcessBridgeParent(ProcessId aId);

  already_AddRefed<PHttpConnectionMgrChild> AllocPHttpConnectionMgrChild(
      const HttpHandlerInitArgs& aArgs);
  mozilla::ipc::IPCResult RecvUpdateDeviceModelId(const nsACString& aModelId);
  mozilla::ipc::IPCResult RecvOnHttpActivityDistributorActivated(
      const bool& aIsActivated);
  mozilla::ipc::IPCResult RecvOnHttpActivityDistributorObserveProxyResponse(
      const bool& aIsEnabled);
  mozilla::ipc::IPCResult RecvOnHttpActivityDistributorObserveConnection(
      const bool& aIsEnabled);

  already_AddRefed<PInputChannelThrottleQueueChild>
  AllocPInputChannelThrottleQueueChild(const uint32_t& aMeanBytesPerSecond,
                                       const uint32_t& aMaxBytesPerSecond);

  already_AddRefed<PAltSvcTransactionChild> AllocPAltSvcTransactionChild(
      const HttpConnectionInfoCloneArgs& aConnInfo, const uint32_t& aCaps);

  bool IsShuttingDown();

  already_AddRefed<PDNSRequestChild> AllocPDNSRequestChild(
      const nsACString& aHost, const nsACString& aTrrServer,
      const int32_t& aPort, const uint16_t& aType,
      const OriginAttributes& aOriginAttributes,
      const nsIDNSService::DNSFlags& aFlags);
  mozilla::ipc::IPCResult RecvPDNSRequestConstructor(
      PDNSRequestChild* aActor, const nsACString& aHost,
      const nsACString& aTrrServer, const int32_t& aPort, const uint16_t& aType,
      const OriginAttributes& aOriginAttributes,
      const nsIDNSService::DNSFlags& aFlags) override;

  void AddDataBridgeToMap(uint64_t aChannelId,
                          BackgroundDataBridgeParent* aActor);
  void RemoveDataBridgeFromMap(uint64_t aChannelId);
  Maybe<RefPtr<BackgroundDataBridgeParent>> GetAndRemoveDataBridge(
      uint64_t aChannelId);

  mozilla::ipc::IPCResult RecvClearSessionCache(
      ClearSessionCacheResolver&& aResolve);

  already_AddRefed<PTRRServiceChild> AllocPTRRServiceChild(
      const bool& aCaptiveIsPassed, const bool& aParentalControlEnabled,
      const nsTArray<nsCString>& aDNSSuffixList);
  mozilla::ipc::IPCResult RecvPTRRServiceConstructor(
      PTRRServiceChild* aActor, const bool& aCaptiveIsPassed,
      const bool& aParentalControlEnabled,
      nsTArray<nsCString>&& aDNSSuffixList) override;

  already_AddRefed<PNativeDNSResolverOverrideChild>
  AllocPNativeDNSResolverOverrideChild();
  mozilla::ipc::IPCResult RecvPNativeDNSResolverOverrideConstructor(
      PNativeDNSResolverOverrideChild* aActor) override;

  mozilla::ipc::IPCResult RecvNotifyObserver(const nsACString& aTopic,
                                             const nsAString& aData);

  mozilla::ipc::IPCResult RecvGetSocketData(GetSocketDataResolver&& aResolve);
  mozilla::ipc::IPCResult RecvGetDNSCacheEntries(
      GetDNSCacheEntriesResolver&& aResolve);
  mozilla::ipc::IPCResult RecvGetHttpConnectionData(
      GetHttpConnectionDataResolver&& aResolve);
  mozilla::ipc::IPCResult RecvGetHttp3ConnectionStatsData(
      GetHttp3ConnectionStatsDataResolver&& aResolve);

  mozilla::ipc::IPCResult RecvInitProxyAutoConfigChild(
      Endpoint<PProxyAutoConfigChild>&& aEndpoint);

  mozilla::ipc::IPCResult RecvRecheckIPConnectivity();
  mozilla::ipc::IPCResult RecvRecheckDNS();

  mozilla::ipc::IPCResult RecvFlushFOGData(FlushFOGDataResolver&& aResolver);

  mozilla::ipc::IPCResult RecvTestTriggerMetrics(
      TestTriggerMetricsResolver&& aResolve);

#if defined(XP_WIN)
  mozilla::ipc::IPCResult RecvGetUntrustedModulesData(
      GetUntrustedModulesDataResolver&& aResolver);
  mozilla::ipc::IPCResult RecvUnblockUntrustedModulesThread();
#endif  // defined(XP_WIN)

  already_AddRefed<psm::IPCClientCertsChild> GetIPCClientCertsActor();
  void CloseIPCClientCertsActor();

  mozilla::ipc::IPCResult RecvAddNetAddrOverride(const NetAddr& aFrom,
                                                 const NetAddr& aTo);
  mozilla::ipc::IPCResult RecvClearNetAddrOverrides();

 protected:
  friend class SocketProcessImpl;
  ~SocketProcessChild();

  void InitSocketBackground();

 private:
  // Mapping of content process id and the SocketProcessBridgeParent.
  // This table keeps SocketProcessBridgeParent alive in socket process.
  nsRefPtrHashtable<nsUint32HashKey, SocketProcessBridgeParent>
      mSocketProcessBridgeParentMap;

  RefPtr<ChildProfilerController> mProfilerController;

  // Protect the table below.
  Mutex mMutex MOZ_UNANNOTATED{"SocketProcessChild::mMutex"};
  nsTHashMap<uint64_t, RefPtr<BackgroundDataBridgeParent>>
      mBackgroundDataBridgeMap;

  bool mShuttingDown MOZ_GUARDED_BY(mMutex) = false;

  nsCOMPtr<nsIEventTarget> mSocketThread;
  // mIPCClientCertsChild is only accessed on the socket thread.
  RefPtr<psm::IPCClientCertsChild> mIPCClientCertsChild;
};

}  // namespace net
}  // namespace mozilla

#endif  // mozilla_net_SocketProcessChild_h
