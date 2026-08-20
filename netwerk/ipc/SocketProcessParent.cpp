/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SocketProcessParent.h"

#include "AltServiceParent.h"
#include "HttpTransactionParent.h"
#include "SSLTokensCache.h"
#include "SocketProcessHost.h"
#include "SocketProcessLogging.h"
#include "TLSClientAuthCertSelection.h"
#include "mozilla/Atomics.h"
#include "mozilla/Components.h"
#include "mozilla/FOGIPC.h"
#include "mozilla/GeckoTrace.h"
#include "mozilla/RemoteLazyInputStreamParent.h"
#include "mozilla/Telemetry.h"
#include "mozilla/TelemetryIPC.h"
#include "mozilla/dom/MemoryReportRequest.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "mozilla/net/DNSRequestParent.h"
#include "mozilla/net/ProxyConfigLookupParent.h"
#include "mozilla/net/SocketProcessBackgroundParent.h"
#include "mozilla/net/neqo_glue_ffi_generated.h"
#include "nsHttpConnectionInfo.h"
#include "nsHttpHandler.h"
#include "nsIConsoleService.h"
#include "nsIHttpActivityObserver.h"
#include "nsIOService.h"
#include "nsIObserverService.h"
#include "nsNSSCertificate.h"
#include "nsNSSComponent.h"
#include "nsSocketTransportService2.h"
#include "secerr.h"
#ifdef MOZ_WEBRTC
#  include "mozilla/dom/BrowserParent.h"
#  include "mozilla/dom/ContentProcessManager.h"
#  include "mozilla/net/WebrtcTCPSocketParent.h"
#endif
#if defined(MOZ_WIDGET_ANDROID)
#  include "mozilla/java/GeckoProcessManagerWrappers.h"
#  include "mozilla/java/GeckoProcessTypeWrappers.h"
#endif  // defined(MOZ_WIDGET_ANDROID)
#if defined(XP_WIN)
#  include "mozilla/WinDllServices.h"
#endif

namespace mozilla {
namespace net {

static Atomic<SocketProcessParent*> sSocketProcessParent;

SocketProcessParent::SocketProcessParent(SocketProcessHost* aHost)
    : mHost(aHost) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mHost);

  MOZ_COUNT_CTOR(SocketProcessParent);
  sSocketProcessParent = this;
}

SocketProcessParent::~SocketProcessParent() {
  MOZ_COUNT_DTOR(SocketProcessParent);
  sSocketProcessParent = nullptr;
}

/* static */
already_AddRefed<SocketProcessParent> SocketProcessParent::GetSingleton() {
  RefPtr<SocketProcessParent> parent(sSocketProcessParent);
  return parent.forget();
}

void SocketProcessParent::ActorDestroy(ActorDestroyReason aWhy) {
#if defined(MOZ_WIDGET_ANDROID)
  nsCOMPtr<nsIEventTarget> launcherThread(ipc::GetIPCLauncher());
  MOZ_ASSERT(launcherThread);

  auto procType = java::GeckoProcessType::SOCKET();
  auto selector =
      java::GeckoProcessManager::Selector::New(procType, OtherPid());

  launcherThread->Dispatch(NS_NewRunnableFunction(
      "SocketProcessParent::ActorDestroy",
      [selector = java::GeckoProcessManager::Selector::GlobalRef(selector)]() {
        java::GeckoProcessManager::ShutdownProcess(selector);
      }));
#endif  // defined(MOZ_WIDGET_ANDROID)

  if (aWhy == AbnormalShutdown) {
    GenerateCrashReport();
    MaybeTerminateProcess();
  }

  if (mHost) {
    mHost->OnChannelClosed();
  }
}

bool SocketProcessParent::SendRequestMemoryReport(
    const uint32_t& aGeneration, const bool& aAnonymize,
    const bool& aMinimizeMemoryUsage,
    const Maybe<ipc::FileDescriptor>& aDMDFile) {
  mMemoryReportRequest = MakeUnique<dom::MemoryReportRequestHost>(aGeneration);

  PSocketProcessParent::SendRequestMemoryReport(aGeneration, aAnonymize,
                                                aMinimizeMemoryUsage, aDMDFile)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [](uint32_t aGeneration2) {
            MOZ_ASSERT(gIOService);
            if (!gIOService->SocketProcess()) {
              return;
            }
            SocketProcessParent* actor =
                gIOService->SocketProcess()->GetActor();
            if (!actor) {
              return;
            }
            if (actor->mMemoryReportRequest) {
              actor->mMemoryReportRequest->Finish(aGeneration2);
              actor->mMemoryReportRequest = nullptr;
            }
          },
          [](mozilla::ipc::ResponseRejectReason) {
            MOZ_ASSERT(gIOService);
            if (!gIOService->SocketProcess()) {
              return;
            }
            SocketProcessParent* actor =
                gIOService->SocketProcess()->GetActor();
            if (!actor) {
              return;
            }
            actor->mMemoryReportRequest = nullptr;
          });

  return true;
}

mozilla::ipc::IPCResult SocketProcessParent::RecvAddMemoryReport(
    const MemoryReport& aReport) {
  if (mMemoryReportRequest) {
    mMemoryReportRequest->RecvReport(aReport);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult SocketProcessParent::RecvAccumulateChildHistograms(
    nsTArray<HistogramAccumulation>&& aAccumulations) {
  TelemetryIPC::AccumulateChildHistograms(Telemetry::ProcessID::Socket,
                                          aAccumulations);
  return IPC_OK();
}

mozilla::ipc::IPCResult SocketProcessParent::RecvAccumulateChildKeyedHistograms(
    nsTArray<KeyedHistogramAccumulation>&& aAccumulations) {
  TelemetryIPC::AccumulateChildKeyedHistograms(Telemetry::ProcessID::Socket,
                                               aAccumulations);
  return IPC_OK();
}

mozilla::ipc::IPCResult SocketProcessParent::RecvUpdateChildScalars(
    nsTArray<ScalarAction>&& aScalarActions) {
  TelemetryIPC::UpdateChildScalars(Telemetry::ProcessID::Socket,
                                   aScalarActions);
  return IPC_OK();
}

mozilla::ipc::IPCResult SocketProcessParent::RecvUpdateChildKeyedScalars(
    nsTArray<KeyedScalarAction>&& aScalarActions) {
  TelemetryIPC::UpdateChildKeyedScalars(Telemetry::ProcessID::Socket,
                                        aScalarActions);
  return IPC_OK();
}

mozilla::ipc::IPCResult SocketProcessParent::RecvRecordChildEvents(
    nsTArray<mozilla::Telemetry::ChildEventData>&& aEvents) {
  TelemetryIPC::RecordChildEvents(Telemetry::ProcessID::Socket, aEvents);
  return IPC_OK();
}

mozilla::ipc::IPCResult SocketProcessParent::RecvRecordDiscardedData(
    const mozilla::Telemetry::DiscardedData& aDiscardedData) {
  TelemetryIPC::RecordDiscardedData(Telemetry::ProcessID::Socket,
                                    aDiscardedData);
  return IPC_OK();
}

already_AddRefed<PWebrtcTCPSocketParent>
SocketProcessParent::AllocPWebrtcTCPSocketParent(const Maybe<TabId>& aTabId) {
#ifdef MOZ_WEBRTC
  return do_AddRef(new WebrtcTCPSocketParent(aTabId));
#else
  return nullptr;
#endif
}

already_AddRefed<PDNSRequestParent> SocketProcessParent::AllocPDNSRequestParent(
    const nsACString& aHost, const nsACString& aTrrServer, const int32_t& port,
    const uint16_t& aType, const OriginAttributes& aOriginAttributes,
    const nsIDNSService::DNSFlags& aFlags) {
  RefPtr<DNSRequestHandler> handler = new DNSRequestHandler();
  RefPtr<DNSRequestParent> actor = new DNSRequestParent(handler);
  return actor.forget();
}

mozilla::ipc::IPCResult SocketProcessParent::RecvPDNSRequestConstructor(
    PDNSRequestParent* aActor, const nsACString& aHost,
    const nsACString& aTrrServer, const int32_t& port, const uint16_t& aType,
    const OriginAttributes& aOriginAttributes,
    const nsIDNSService::DNSFlags& aFlags) {
  RefPtr<DNSRequestParent> actor =
      mozilla::ipc::ActorCast<DNSRequestParent>(aActor);
  RefPtr<DNSRequestHandler> handler =
      actor->GetDNSRequest()->AsDNSRequestHandler();
  handler->DoAsyncResolve(aHost, aTrrServer, port, aType, aOriginAttributes,
                          aFlags);
  return IPC_OK();
}

mozilla::ipc::IPCResult SocketProcessParent::RecvObserveHttpActivity(
    const HttpActivityArgs& aArgs, const uint32_t& aActivityType,
    const uint32_t& aActivitySubtype, const PRTime& aTimestamp,
    const uint64_t& aExtraSizeData, const nsACString& aExtraStringData) {
  nsCOMPtr<nsIHttpActivityDistributor> activityDistributor =
      components::HttpActivityDistributor::Service();
  MOZ_ASSERT(activityDistributor);

  (void)activityDistributor->ObserveActivityWithArgs(
      aArgs, aActivityType, aActivitySubtype, aTimestamp, aExtraSizeData,
      aExtraStringData);
  return IPC_OK();
}

mozilla::ipc::IPCResult SocketProcessParent::RecvInitSocketBackground(
    Endpoint<PSocketProcessBackgroundParent>&& aEndpoint) {
  if (!aEndpoint.IsValid()) {
    return IPC_FAIL(this, "Invalid endpoint");
  }

  nsCOMPtr<nsISerialEventTarget> transportQueue;
  if (NS_FAILED(NS_CreateBackgroundTaskQueue("SocketBackgroundParentQueue",
                                             getter_AddRefs(transportQueue)))) {
    return IPC_FAIL(this, "NS_CreateBackgroundTaskQueue failed");
  }

  transportQueue->Dispatch(
      NS_NewRunnableFunction("BindSocketBackgroundParent",
                             [endpoint = std::move(aEndpoint)]() mutable {
                               RefPtr<SocketProcessBackgroundParent> parent =
                                   new SocketProcessBackgroundParent();
                               endpoint.Bind(parent);
                             }));
  return IPC_OK();
}

already_AddRefed<PAltServiceParent>
SocketProcessParent::AllocPAltServiceParent() {
  RefPtr<AltServiceParent> actor = new AltServiceParent();
  return actor.forget();
}

already_AddRefed<PProxyConfigLookupParent>
SocketProcessParent::AllocPProxyConfigLookupParent(
    nsIURI* aURI, const uint32_t& aProxyResolveFlags) {
  RefPtr<ProxyConfigLookupParent> actor =
      new ProxyConfigLookupParent(aURI, aProxyResolveFlags);
  return actor.forget();
}

mozilla::ipc::IPCResult SocketProcessParent::RecvPProxyConfigLookupConstructor(
    PProxyConfigLookupParent* aActor, nsIURI* aURI,
    const uint32_t& aProxyResolveFlags) {
  mozilla::ipc::ActorCast<ProxyConfigLookupParent>(aActor)->DoProxyLookup();
  return IPC_OK();
}

// To ensure that IPDL is finished before SocketParent gets deleted.
class DeferredDeleteSocketProcessParent : public Runnable {
 public:
  explicit DeferredDeleteSocketProcessParent(
      RefPtr<SocketProcessParent>&& aParent)
      : Runnable("net::DeferredDeleteSocketProcessParent"),
        mParent(std::move(aParent)) {}

  NS_IMETHODIMP Run() override { return NS_OK; }

 private:
  RefPtr<SocketProcessParent> mParent;
};

/* static */
void SocketProcessParent::Destroy(RefPtr<SocketProcessParent>&& aParent) {
  NS_DispatchToMainThread(
      new DeferredDeleteSocketProcessParent(std::move(aParent)));
}

mozilla::ipc::IPCResult SocketProcessParent::RecvExcludeHttp2OrHttp3(
    const HttpConnectionInfoCloneArgs& aArgs) {
  RefPtr<nsHttpConnectionInfo> cinfo =
      nsHttpConnectionInfo::DeserializeHttpConnectionInfoCloneArgs(aArgs);
  if (!cinfo) {
    MOZ_ASSERT(false, "failed to deserizlize http connection info");
    return IPC_OK();
  }

  if (cinfo->IsHttp3()) {
    gHttpHandler->ExcludeHttp3(cinfo);
  } else {
    gHttpHandler->ExcludeHttp2(cinfo);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult SocketProcessParent::RecvOnConsoleMessage(
    const nsString& aMessage) {
  nsCOMPtr<nsIConsoleService> consoleService =
      do_GetService(NS_CONSOLESERVICE_CONTRACTID);
  if (consoleService) {
    consoleService->LogStringMessage(aMessage.get());
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult SocketProcessParent::RecvFOGData(ByteBuf&& aBuf) {
  glean::FOGData(std::move(aBuf));
  return IPC_OK();
}

#if defined(XP_MACOSX) || defined(XP_IOS)
mozilla::ipc::IPCResult SocketProcessParent::RecvAppleFastDatapathProbeResult(
    const bool& aAvailable) {
  if (mHost) {
    mHost->mAppleFastDatapathProbeResultReceived = true;
  }
  glean::network::apple_fast_datapath_used.Set(aAvailable);
  // When neqo runs in the parent (socket process not used for I/O), enable
  // the fast path here so parent-side QUIC sockets benefit from it too.
  if (aAvailable && !nsIOService::UseSocketProcess() &&
      gSocketTransportService) {
    gSocketTransportService->Dispatch(
        NS_NewRunnableFunction("net::EnableAppleFastPath",
                               []() { neqo_glue_enable_apple_fast_path(); }),
        NS_DISPATCH_NORMAL);
  }
  return IPC_OK();
}
#endif

mozilla::ipc::IPCResult SocketProcessParent::RecvGeckoTraceExport(
    ByteBuf&& aBuf) {
  recv_gecko_trace_export(aBuf.mData, aBuf.mLen);
  return IPC_OK();
}

mozilla::ipc::IPCResult SocketProcessParent::RecvSSLTokensCacheData(
    nsTArray<SSLTokensCacheRecordInfo>&& aRecords) {
  SSLTokensCache::ReplaceAllRecords(std::move(aRecords));
  return IPC_OK();
}

#if defined(XP_WIN)
mozilla::ipc::IPCResult SocketProcessParent::RecvGetModulesTrust(
    ModulePaths&& aModPaths, bool aRunAtNormalPriority,
    GetModulesTrustResolver&& aResolver) {
  RefPtr<DllServices> dllSvc(DllServices::Get());
  dllSvc->GetModulesTrust(std::move(aModPaths), aRunAtNormalPriority)
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [aResolver](ModulesMapResult&& aResult) {
            aResolver(Some(ModulesMapResult(std::move(aResult))));
          },
          [aResolver](nsresult aRv) { aResolver(Nothing()); });
  return IPC_OK();
}
#endif  // defined(XP_WIN)

}  // namespace net
}  // namespace mozilla
