/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http:mozilla.org/MPL/2.0/. */

#include "mozilla/dom/NetDashboardBinding.h"
#include "mozilla/dom/ToJSValue.h"
#include "mozilla/Components.h"
#include "mozilla/ErrorNames.h"
#include "mozilla/net/Dashboard.h"
#include "mozilla/net/HttpInfo.h"
#include "mozilla/net/HTTPSSVC.h"
#include "mozilla/net/SocketProcessParent.h"
#include "nsHttp.h"
#include "nsICancelable.h"
#include "nsIDNSListener.h"
#include "nsIDNSService.h"
#include "nsIDNSRecord.h"
#include "nsIDNSByTypeRecord.h"
#include "nsIInputStream.h"
#include "nsINamed.h"
#include "nsINetAddr.h"
#include "nsISocketTransport.h"
#include "nsProxyRelease.h"
#include "nsSocketTransportService2.h"
#include "nsThreadUtils.h"
#include "nsURLHelper.h"
#include "mozilla/Logging.h"
#include "nsIOService.h"
#include "../cache2/CacheFileUtils.h"

using mozilla::AutoSafeJSContext;
using mozilla::dom::Sequence;
using mozilla::dom::ToJSValue;

namespace mozilla {
namespace net {

class SocketData : public nsISupports {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  SocketData() = default;

  uint64_t mTotalSent{0};
  uint64_t mTotalRecv{0};
  nsTArray<SocketInfo> mData;
  nsMainThreadPtrHandle<nsINetDashboardCallback> mCallback;
  nsIEventTarget* mEventTarget{nullptr};

 private:
  virtual ~SocketData() = default;
};

static void GetErrorString(nsresult rv, nsAString& errorString);

NS_IMPL_ISUPPORTS0(SocketData)

class HttpData : public nsISupports {
  virtual ~HttpData() = default;

 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  HttpData() = default;

  nsTArray<HttpRetParams> mData;
  nsMainThreadPtrHandle<nsINetDashboardCallback> mCallback;
  nsIEventTarget* mEventTarget{nullptr};
};

NS_IMPL_ISUPPORTS0(HttpData)

class Http3ConnectionStatsData : public nsISupports {
  virtual ~Http3ConnectionStatsData() = default;

 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  Http3ConnectionStatsData() = default;

  nsTArray<Http3ConnectionStatsParams> mData;
  nsMainThreadPtrHandle<nsINetDashboardCallback> mCallback;
  nsIEventTarget* mEventTarget{nullptr};
};

NS_IMPL_ISUPPORTS0(Http3ConnectionStatsData)

class WebSocketRequest : public nsISupports {
  virtual ~WebSocketRequest() = default;

 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  WebSocketRequest() = default;

  nsMainThreadPtrHandle<nsINetDashboardCallback> mCallback;
  nsIEventTarget* mEventTarget{nullptr};
};

NS_IMPL_ISUPPORTS0(WebSocketRequest)

class DnsData : public nsISupports {
  virtual ~DnsData() = default;

 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  DnsData() = default;

  nsTArray<DNSCacheEntries> mData;
  nsMainThreadPtrHandle<nsINetDashboardCallback> mCallback;
  nsIEventTarget* mEventTarget{nullptr};
};

NS_IMPL_ISUPPORTS0(DnsData)

class ConnectionData : public nsITransportEventSink,
                       public nsITimerCallback,
                       public nsINamed {
  virtual ~ConnectionData() {
    if (mTimer) {
      mTimer->Cancel();
    }
  }

 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSITRANSPORTEVENTSINK
  NS_DECL_NSITIMERCALLBACK

  NS_IMETHOD GetName(nsACString& aName) override {
    aName.AssignLiteral("net::ConnectionData");
    return NS_OK;
  }

  void StartTimer(uint32_t aTimeout);
  void StopTimer();

  explicit ConnectionData(Dashboard* target) { mDashboard = target; }

  nsCOMPtr<nsISocketTransport> mSocket;
  nsCOMPtr<nsIInputStream> mStreamIn;
  nsCOMPtr<nsITimer> mTimer;
  nsMainThreadPtrHandle<nsINetDashboardCallback> mCallback;
  nsIEventTarget* mEventTarget{nullptr};
  Dashboard* mDashboard;

  nsCString mHost;
  uint32_t mPort{0};
  nsCString mProtocol;
  uint32_t mTimeout{0};

  nsString mStatus;
};

NS_IMPL_ISUPPORTS(ConnectionData, nsITransportEventSink, nsITimerCallback,
                  nsINamed)

class RcwnData : public nsISupports {
  virtual ~RcwnData() = default;

 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  RcwnData() = default;

  nsMainThreadPtrHandle<nsINetDashboardCallback> mCallback;
  nsIEventTarget* mEventTarget{nullptr};
};

NS_IMPL_ISUPPORTS0(RcwnData)

NS_IMETHODIMP
ConnectionData::OnTransportStatus(nsITransport* aTransport, nsresult aStatus,
                                  int64_t aProgress, int64_t aProgressMax) {
  if (aStatus == NS_NET_STATUS_CONNECTED_TO) {
    StopTimer();
  }

  GetErrorString(aStatus, mStatus);
  mEventTarget->Dispatch(NewRunnableMethod<RefPtr<ConnectionData>>(
                             "net::Dashboard::GetConnectionStatus", mDashboard,
                             &Dashboard::GetConnectionStatus, this),
                         NS_DISPATCH_NORMAL);

  return NS_OK;
}

NS_IMETHODIMP
ConnectionData::Notify(nsITimer* aTimer) {
  MOZ_ASSERT(aTimer == mTimer);

  if (mSocket) {
    mSocket->Close(NS_ERROR_ABORT);
    mSocket = nullptr;
    mStreamIn = nullptr;
  }

  mTimer = nullptr;

  mStatus.AssignLiteral(u"NS_ERROR_NET_TIMEOUT");
  mEventTarget->Dispatch(NewRunnableMethod<RefPtr<ConnectionData>>(
                             "net::Dashboard::GetConnectionStatus", mDashboard,
                             &Dashboard::GetConnectionStatus, this),
                         NS_DISPATCH_NORMAL);

  return NS_OK;
}

void ConnectionData::StartTimer(uint32_t aTimeout) {
  if (!mTimer) {
    mTimer = NS_NewTimer();
  }

  mTimer->InitWithCallback(this, aTimeout * 1000, nsITimer::TYPE_ONE_SHOT);
}

void ConnectionData::StopTimer() {
  if (mTimer) {
    mTimer->Cancel();
    mTimer = nullptr;
  }
}

class LookupHelper;

class LookupArgument : public nsISupports {
  virtual ~LookupArgument() = default;

 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  LookupArgument(nsIDNSRecord* aRecord, LookupHelper* aHelper) {
    mRecord = aRecord;
    mHelper = aHelper;
  }

  nsCOMPtr<nsIDNSRecord> mRecord;
  RefPtr<LookupHelper> mHelper;
};

NS_IMPL_ISUPPORTS0(LookupArgument)

class LookupHelper final : public nsIDNSListener {
  virtual ~LookupHelper() {
    if (mCancel) {
      mCancel->Cancel(NS_ERROR_ABORT);
    }
  }

 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIDNSLISTENER

  LookupHelper() = default;

  nsresult ConstructAnswer(LookupArgument* aArgument);
  nsresult ConstructHTTPSRRAnswer(LookupArgument* aArgument);

 public:
  nsCOMPtr<nsICancelable> mCancel;
  nsMainThreadPtrHandle<nsINetDashboardCallback> mCallback;
  nsIEventTarget* mEventTarget{nullptr};
  nsresult mStatus{NS_ERROR_NOT_INITIALIZED};
};

NS_IMPL_ISUPPORTS(LookupHelper, nsIDNSListener)

NS_IMETHODIMP
LookupHelper::OnLookupComplete(nsICancelable* aRequest, nsIDNSRecord* aRecord,
                               nsresult aStatus) {
  MOZ_ASSERT(aRequest == mCancel);
  mCancel = nullptr;
  mStatus = aStatus;

  nsCOMPtr<nsIDNSHTTPSSVCRecord> httpsRecord = do_QueryInterface(aRecord);
  if (httpsRecord) {
    RefPtr<LookupArgument> arg = new LookupArgument(aRecord, this);
    mEventTarget->Dispatch(
        NewRunnableMethod<RefPtr<LookupArgument>>(
            "net::LookupHelper::ConstructHTTPSRRAnswer", this,
            &LookupHelper::ConstructHTTPSRRAnswer, arg),
        NS_DISPATCH_NORMAL);
    return NS_OK;
  }

  RefPtr<LookupArgument> arg = new LookupArgument(aRecord, this);
  mEventTarget->Dispatch(NewRunnableMethod<RefPtr<LookupArgument>>(
                             "net::LookupHelper::ConstructAnswer", this,
                             &LookupHelper::ConstructAnswer, arg),
                         NS_DISPATCH_NORMAL);

  return NS_OK;
}

nsresult LookupHelper::ConstructAnswer(LookupArgument* aArgument) {
  nsIDNSRecord* aRecord = aArgument->mRecord;
  AutoSafeJSContext cx;

  mozilla::dom::DNSLookupDict dict;
  dict.mAddress.Construct();

  Sequence<nsString>& addresses = dict.mAddress.Value();
  nsCOMPtr<nsIDNSAddrRecord> record = do_QueryInterface(aRecord);
  if (NS_SUCCEEDED(mStatus) && record) {
    dict.mAnswer = true;
    bool hasMore;
    record->HasMore(&hasMore);
    while (hasMore) {
      nsString* nextAddress = addresses.AppendElement(fallible);
      if (!nextAddress) {
        return NS_ERROR_OUT_OF_MEMORY;
      }

      nsCString nextAddressASCII;
      record->GetNextAddrAsString(nextAddressASCII);
      CopyASCIItoUTF16(nextAddressASCII, *nextAddress);
      record->HasMore(&hasMore);
    }
  } else {
    dict.mAnswer = false;
    GetErrorString(mStatus, dict.mError);
  }

  JS::Rooted<JS::Value> val(cx);
  if (!ToJSValue(cx, dict, &val)) {
    return NS_ERROR_FAILURE;
  }

  this->mCallback->OnDashboardDataAvailable(val);

  return NS_OK;
}

static void CStringToHexString(const nsACString& aIn, nsAString& aOut) {
  static const char* const lut = "0123456789ABCDEF";

  size_t len = aIn.Length();

  aOut.SetCapacity(2 * len);
  for (size_t i = 0; i < aIn.Length(); ++i) {
    const char c = static_cast<char>(aIn[i]);
    aOut.Append(lut[(c >> 4) & 0x0F]);
    aOut.Append(lut[c & 15]);
  }
}

nsresult LookupHelper::ConstructHTTPSRRAnswer(LookupArgument* aArgument) {
  nsCOMPtr<nsIDNSHTTPSSVCRecord> httpsRecord =
      do_QueryInterface(aArgument->mRecord);

  AutoSafeJSContext cx;

  mozilla::dom::HTTPSRRLookupDict dict;
  dict.mRecords.Construct();

  Sequence<dom::HTTPSRecord>& records = dict.mRecords.Value();
  if (NS_SUCCEEDED(mStatus) && httpsRecord) {
    dict.mAnswer = true;
    nsTArray<RefPtr<nsISVCBRecord>> svcbRecords;
    httpsRecord->GetRecords(svcbRecords);

    for (const auto& record : svcbRecords) {
      dom::HTTPSRecord* nextRecord = records.AppendElement(fallible);
      if (!nextRecord) {
        return NS_ERROR_OUT_OF_MEMORY;
      }

      Unused << record->GetPriority(&nextRecord->mPriority);
      nsCString name;
      Unused << record->GetName(name);
      CopyASCIItoUTF16(name, nextRecord->mTargetName);

      nsTArray<RefPtr<nsISVCParam>> values;
      Unused << record->GetValues(values);
      if (values.IsEmpty()) {
        continue;
      }

      for (const auto& value : values) {
        uint16_t type;
        Unused << value->GetType(&type);
        switch (type) {
          case SvcParamKeyAlpn: {
            nextRecord->mAlpn.Construct();
            nextRecord->mAlpn.Value().mType = type;
            nsCOMPtr<nsISVCParamAlpn> alpnParam = do_QueryInterface(value);
            nsTArray<nsCString> alpn;
            Unused << alpnParam->GetAlpn(alpn);
            nsAutoCString alpnStr;
            for (const auto& str : alpn) {
              alpnStr.Append(str);
              alpnStr.Append(',');
            }
            CopyASCIItoUTF16(Span(alpnStr.BeginReading(), alpnStr.Length() - 1),
                             nextRecord->mAlpn.Value().mAlpn);
            break;
          }
          case SvcParamKeyNoDefaultAlpn: {
            nextRecord->mNoDefaultAlpn.Construct();
            nextRecord->mNoDefaultAlpn.Value().mType = type;
            break;
          }
          case SvcParamKeyPort: {
            nextRecord->mPort.Construct();
            nextRecord->mPort.Value().mType = type;
            nsCOMPtr<nsISVCParamPort> portParam = do_QueryInterface(value);
            Unused << portParam->GetPort(&nextRecord->mPort.Value().mPort);
            break;
          }
          case SvcParamKeyIpv4Hint: {
            nextRecord->mIpv4Hint.Construct();
            nextRecord->mIpv4Hint.Value().mType = type;
            nsCOMPtr<nsISVCParamIPv4Hint> ipv4Param = do_QueryInterface(value);
            nsTArray<RefPtr<nsINetAddr>> ipv4Hint;
            Unused << ipv4Param->GetIpv4Hint(ipv4Hint);
            if (!ipv4Hint.IsEmpty()) {
              nextRecord->mIpv4Hint.Value().mAddress.Construct();
              for (const auto& address : ipv4Hint) {
                nsString* nextAddress = nextRecord->mIpv4Hint.Value()
                                            .mAddress.Value()
                                            .AppendElement(fallible);
                if (!nextAddress) {
                  return NS_ERROR_OUT_OF_MEMORY;
                }

                nsCString addressASCII;
                Unused << address->GetAddress(addressASCII);
                CopyASCIItoUTF16(addressASCII, *nextAddress);
              }
            }
            break;
          }
          case SvcParamKeyIpv6Hint: {
            nextRecord->mIpv6Hint.Construct();
            nextRecord->mIpv6Hint.Value().mType = type;
            nsCOMPtr<nsISVCParamIPv6Hint> ipv6Param = do_QueryInterface(value);
            nsTArray<RefPtr<nsINetAddr>> ipv6Hint;
            Unused << ipv6Param->GetIpv6Hint(ipv6Hint);
            if (!ipv6Hint.IsEmpty()) {
              nextRecord->mIpv6Hint.Value().mAddress.Construct();
              for (const auto& address : ipv6Hint) {
                nsString* nextAddress = nextRecord->mIpv6Hint.Value()
                                            .mAddress.Value()
                                            .AppendElement(fallible);
                if (!nextAddress) {
                  return NS_ERROR_OUT_OF_MEMORY;
                }

                nsCString addressASCII;
                Unused << address->GetAddress(addressASCII);
                CopyASCIItoUTF16(addressASCII, *nextAddress);
              }
            }
            break;
          }
          case SvcParamKeyEchConfig: {
            nextRecord->mEchConfig.Construct();
            nextRecord->mEchConfig.Value().mType = type;
            nsCOMPtr<nsISVCParamEchConfig> echConfigParam =
                do_QueryInterface(value);
            nsCString echConfigStr;
            Unused << echConfigParam->GetEchconfig(echConfigStr);
            CStringToHexString(echConfigStr,
                               nextRecord->mEchConfig.Value().mEchConfig);
            break;
          }
          case SvcParamKeyODoHConfig: {
            nextRecord->mODoHConfig.Construct();
            nextRecord->mODoHConfig.Value().mType = type;
            nsCOMPtr<nsISVCParamODoHConfig> ODoHConfigParam =
                do_QueryInterface(value);
            nsCString ODoHConfigStr;
            Unused << ODoHConfigParam->GetODoHConfig(ODoHConfigStr);
            CStringToHexString(ODoHConfigStr,
                               nextRecord->mODoHConfig.Value().mODoHConfig);
            break;
          }
          default:
            break;
        }
      }
    }
  } else {
    dict.mAnswer = false;
    GetErrorString(mStatus, dict.mError);
  }

  JS::Rooted<JS::Value> val(cx);
  if (!ToJSValue(cx, dict, &val)) {
    return NS_ERROR_FAILURE;
  }

  this->mCallback->OnDashboardDataAvailable(val);

  return NS_OK;
}

NS_IMPL_ISUPPORTS(Dashboard, nsIDashboard, nsIDashboardEventNotifier)

Dashboard::Dashboard() { mEnableLogging = false; }

NS_IMETHODIMP
Dashboard::RequestSockets(nsINetDashboardCallback* aCallback) {
  RefPtr<SocketData> socketData = new SocketData();
  socketData->mCallback = new nsMainThreadPtrHolder<nsINetDashboardCallback>(
      "nsINetDashboardCallback", aCallback, true);
  socketData->mEventTarget = GetCurrentSerialEventTarget();

  if (nsIOService::UseSocketProcess()) {
    if (!gIOService->SocketProcessReady()) {
      return NS_ERROR_NOT_AVAILABLE;
    }

    RefPtr<Dashboard> self(this);
    RefPtr<SocketProcessParent> socketParent =
        SocketProcessParent::GetSingleton();
    socketParent->SendGetSocketData()->Then(
        GetMainThreadSerialEventTarget(), __func__,
        [self{std::move(self)},
         socketData{std::move(socketData)}](SocketDataArgs&& args) {
          socketData->mData.Assign(args.info());
          socketData->mTotalSent = args.totalSent();
          socketData->mTotalRecv = args.totalRecv();
          socketData->mEventTarget->Dispatch(
              NewRunnableMethod<RefPtr<SocketData>>(
                  "net::Dashboard::GetSockets", self, &Dashboard::GetSockets,
                  socketData),
              NS_DISPATCH_NORMAL);
        },
        [self](const mozilla::ipc::ResponseRejectReason) {});
    return NS_OK;
  }

  gSocketTransportService->Dispatch(
      NewRunnableMethod<RefPtr<SocketData>>(
          "net::Dashboard::GetSocketsDispatch", this,
          &Dashboard::GetSocketsDispatch, socketData),
      NS_DISPATCH_NORMAL);
  return NS_OK;
}

nsresult Dashboard::GetSocketsDispatch(SocketData* aSocketData) {
  RefPtr<SocketData> socketData = aSocketData;
  if (gSocketTransportService) {
    gSocketTransportService->GetSocketConnections(&socketData->mData);
    socketData->mTotalSent = gSocketTransportService->GetSentBytes();
    socketData->mTotalRecv = gSocketTransportService->GetReceivedBytes();
  }
  socketData->mEventTarget->Dispatch(
      NewRunnableMethod<RefPtr<SocketData>>("net::Dashboard::GetSockets", this,
                                            &Dashboard::GetSockets, socketData),
      NS_DISPATCH_NORMAL);
  return NS_OK;
}

nsresult Dashboard::GetSockets(SocketData* aSocketData) {
  RefPtr<SocketData> socketData = aSocketData;
  AutoSafeJSContext cx;

  mozilla::dom::SocketsDict dict;
  dict.mSockets.Construct();
  dict.mSent = 0;
  dict.mReceived = 0;

  Sequence<mozilla::dom::SocketElement>& sockets = dict.mSockets.Value();

  uint32_t length = socketData->mData.Length();
  if (!sockets.SetCapacity(length, fallible)) {
    JS_ReportOutOfMemory(cx);
    return NS_ERROR_OUT_OF_MEMORY;
  }

  for (uint32_t i = 0; i < socketData->mData.Length(); i++) {
    dom::SocketElement& mSocket = *sockets.AppendElement(fallible);
    CopyASCIItoUTF16(socketData->mData[i].host, mSocket.mHost);
    mSocket.mPort = socketData->mData[i].port;
    mSocket.mActive = socketData->mData[i].active;
    CopyASCIItoUTF16(socketData->mData[i].type, mSocket.mType);
    mSocket.mSent = (double)socketData->mData[i].sent;
    mSocket.mReceived = (double)socketData->mData[i].received;
    dict.mSent += socketData->mData[i].sent;
    dict.mReceived += socketData->mData[i].received;
  }

  dict.mSent += socketData->mTotalSent;
  dict.mReceived += socketData->mTotalRecv;
  JS::Rooted<JS::Value> val(cx);
  if (!ToJSValue(cx, dict, &val)) return NS_ERROR_FAILURE;
  socketData->mCallback->OnDashboardDataAvailable(val);

  return NS_OK;
}

NS_IMETHODIMP
Dashboard::RequestHttpConnections(nsINetDashboardCallback* aCallback) {
  RefPtr<HttpData> httpData = new HttpData();
  httpData->mCallback = new nsMainThreadPtrHolder<nsINetDashboardCallback>(
      "nsINetDashboardCallback", aCallback, true);
  httpData->mEventTarget = GetCurrentSerialEventTarget();

  if (nsIOService::UseSocketProcess()) {
    if (!gIOService->SocketProcessReady()) {
      return NS_ERROR_NOT_AVAILABLE;
    }

    RefPtr<Dashboard> self(this);
    RefPtr<SocketProcessParent> socketParent =
        SocketProcessParent::GetSingleton();
    socketParent->SendGetHttpConnectionData()->Then(
        GetMainThreadSerialEventTarget(), __func__,
        [self{std::move(self)}, httpData](nsTArray<HttpRetParams>&& params) {
          httpData->mData.Assign(std::move(params));
          self->GetHttpConnections(httpData);
          httpData->mEventTarget->Dispatch(
              NewRunnableMethod<RefPtr<HttpData>>(
                  "net::Dashboard::GetHttpConnections", self,
                  &Dashboard::GetHttpConnections, httpData),
              NS_DISPATCH_NORMAL);
        },
        [self](const mozilla::ipc::ResponseRejectReason) {});
    return NS_OK;
  }

  gSocketTransportService->Dispatch(NewRunnableMethod<RefPtr<HttpData>>(
                                        "net::Dashboard::GetHttpDispatch", this,
                                        &Dashboard::GetHttpDispatch, httpData),
                                    NS_DISPATCH_NORMAL);
  return NS_OK;
}

nsresult Dashboard::GetHttpDispatch(HttpData* aHttpData) {
  RefPtr<HttpData> httpData = aHttpData;
  HttpInfo::GetHttpConnectionData(&httpData->mData);
  httpData->mEventTarget->Dispatch(
      NewRunnableMethod<RefPtr<HttpData>>("net::Dashboard::GetHttpConnections",
                                          this, &Dashboard::GetHttpConnections,
                                          httpData),
      NS_DISPATCH_NORMAL);
  return NS_OK;
}

nsresult Dashboard::GetHttpConnections(HttpData* aHttpData) {
  RefPtr<HttpData> httpData = aHttpData;
  AutoSafeJSContext cx;

  mozilla::dom::HttpConnDict dict;
  dict.mConnections.Construct();

  using mozilla::dom::DnsAndSockInfoDict;
  using mozilla::dom::HttpConnectionElement;
  using mozilla::dom::HttpConnInfo;
  Sequence<HttpConnectionElement>& connections = dict.mConnections.Value();

  uint32_t length = httpData->mData.Length();
  if (!connections.SetCapacity(length, fallible)) {
    JS_ReportOutOfMemory(cx);
    return NS_ERROR_OUT_OF_MEMORY;
  }

  for (uint32_t i = 0; i < httpData->mData.Length(); i++) {
    HttpConnectionElement& connection = *connections.AppendElement(fallible);

    CopyASCIItoUTF16(httpData->mData[i].host, connection.mHost);
    connection.mPort = httpData->mData[i].port;
    CopyASCIItoUTF16(httpData->mData[i].httpVersion, connection.mHttpVersion);
    connection.mSsl = httpData->mData[i].ssl;

    connection.mActive.Construct();
    connection.mIdle.Construct();
    connection.mDnsAndSocks.Construct();

    Sequence<HttpConnInfo>& active = connection.mActive.Value();
    Sequence<HttpConnInfo>& idle = connection.mIdle.Value();
    Sequence<DnsAndSockInfoDict>& dnsAndSocks = connection.mDnsAndSocks.Value();

    if (!active.SetCapacity(httpData->mData[i].active.Length(), fallible) ||
        !idle.SetCapacity(httpData->mData[i].idle.Length(), fallible) ||
        !dnsAndSocks.SetCapacity(httpData->mData[i].dnsAndSocks.Length(),
                                 fallible)) {
      JS_ReportOutOfMemory(cx);
      return NS_ERROR_OUT_OF_MEMORY;
    }

    for (uint32_t j = 0; j < httpData->mData[i].active.Length(); j++) {
      HttpConnInfo& info = *active.AppendElement(fallible);
      info.mRtt = httpData->mData[i].active[j].rtt;
      info.mTtl = httpData->mData[i].active[j].ttl;
      info.mProtocolVersion = httpData->mData[i].active[j].protocolVersion;
    }

    for (uint32_t j = 0; j < httpData->mData[i].idle.Length(); j++) {
      HttpConnInfo& info = *idle.AppendElement(fallible);
      info.mRtt = httpData->mData[i].idle[j].rtt;
      info.mTtl = httpData->mData[i].idle[j].ttl;
      info.mProtocolVersion = httpData->mData[i].idle[j].protocolVersion;
    }

    for (uint32_t j = 0; j < httpData->mData[i].dnsAndSocks.Length(); j++) {
      DnsAndSockInfoDict& info = *dnsAndSocks.AppendElement(fallible);
      info.mSpeculative = httpData->mData[i].dnsAndSocks[j].speculative;
    }
  }

  JS::Rooted<JS::Value> val(cx);
  if (!ToJSValue(cx, dict, &val)) {
    return NS_ERROR_FAILURE;
  }

  httpData->mCallback->OnDashboardDataAvailable(val);

  return NS_OK;
}

NS_IMETHODIMP
Dashboard::RequestHttp3ConnectionStats(nsINetDashboardCallback* aCallback) {
  RefPtr<Http3ConnectionStatsData> data = new Http3ConnectionStatsData();
  data->mCallback = new nsMainThreadPtrHolder<nsINetDashboardCallback>(
      "nsINetDashboardCallback", aCallback, true);
  data->mEventTarget = GetCurrentSerialEventTarget();

  if (nsIOService::UseSocketProcess()) {
    if (!gIOService->SocketProcessReady()) {
      return NS_ERROR_NOT_AVAILABLE;
    }

    RefPtr<Dashboard> self(this);
    RefPtr<SocketProcessParent> socketParent =
        SocketProcessParent::GetSingleton();
    socketParent->SendGetHttp3ConnectionStatsData()->Then(
        GetMainThreadSerialEventTarget(), __func__,
        [self{std::move(self)},
         data](nsTArray<Http3ConnectionStatsParams>&& params) {
          data->mData.Assign(std::move(params));
          self->GetHttp3ConnectionStats(data);
          data->mEventTarget->Dispatch(
              NewRunnableMethod<RefPtr<Http3ConnectionStatsData>>(
                  "net::Dashboard::GetHttp3ConnectionStats", self,
                  &Dashboard::GetHttp3ConnectionStats, data),
              NS_DISPATCH_NORMAL);
        },
        [self](const mozilla::ipc::ResponseRejectReason) {});
    return NS_OK;
  }

  gSocketTransportService->Dispatch(
      NewRunnableMethod<RefPtr<Http3ConnectionStatsData>>(
          "net::Dashboard::GetHttp3ConnectionStatsDispatch", this,
          &Dashboard::GetHttp3ConnectionStatsDispatch, data),
      NS_DISPATCH_NORMAL);
  return NS_OK;
}

nsresult Dashboard::GetHttp3ConnectionStatsDispatch(
    Http3ConnectionStatsData* aData) {
  RefPtr<Http3ConnectionStatsData> data = aData;
  HttpInfo::GetHttp3ConnectionStatsData(&data->mData);
  data->mEventTarget->Dispatch(
      NewRunnableMethod<RefPtr<Http3ConnectionStatsData>>(
          "net::Dashboard::GetHttp3ConnectionStats", this,
          &Dashboard::GetHttp3ConnectionStats, data),
      NS_DISPATCH_NORMAL);
  return NS_OK;
}

nsresult Dashboard::GetHttp3ConnectionStats(Http3ConnectionStatsData* aData) {
  RefPtr<Http3ConnectionStatsData> data = aData;
  AutoSafeJSContext cx;

  mozilla::dom::Http3ConnStatsDict dict;
  dict.mConnections.Construct();

  using mozilla::dom::Http3ConnectionStatsElement;
  using mozilla::dom::Http3ConnStats;
  Sequence<Http3ConnectionStatsElement>& connections =
      dict.mConnections.Value();

  uint32_t length = data->mData.Length();
  if (!connections.SetCapacity(length, fallible)) {
    JS_ReportOutOfMemory(cx);
    return NS_ERROR_OUT_OF_MEMORY;
  }

  for (uint32_t i = 0; i < data->mData.Length(); i++) {
    Http3ConnectionStatsElement& connection =
        *connections.AppendElement(fallible);

    CopyASCIItoUTF16(data->mData[i].host, connection.mHost);
    connection.mPort = data->mData[i].port;

    connection.mStats.Construct();

    Sequence<Http3ConnStats>& stats = connection.mStats.Value();

    if (!stats.SetCapacity(data->mData[i].stats.Length(), fallible)) {
      JS_ReportOutOfMemory(cx);
      return NS_ERROR_OUT_OF_MEMORY;
    }

    for (uint32_t j = 0; j < data->mData[i].stats.Length(); j++) {
      Http3ConnStats& info = *stats.AppendElement(fallible);
      info.mPacketsRx = data->mData[i].stats[j].packetsRx;
      info.mDupsRx = data->mData[i].stats[j].dupsRx;
      info.mDroppedRx = data->mData[i].stats[j].droppedRx;
      info.mSavedDatagrams = data->mData[i].stats[j].savedDatagrams;
      info.mPacketsTx = data->mData[i].stats[j].packetsTx;
      info.mLost = data->mData[i].stats[j].lost;
      info.mLateAck = data->mData[i].stats[j].lateAck;
      info.mPtoAck = data->mData[i].stats[j].ptoAck;
      info.mWouldBlockRx = data->mData[i].stats[j].wouldBlockRx;
      info.mWouldBlockTx = data->mData[i].stats[j].wouldBlockTx;
      info.mPtoCounts.Construct();
      Sequence<uint64_t>& ptoCounts = info.mPtoCounts.Value();
      if (!ptoCounts.SetCapacity(data->mData[i].stats[j].ptoCounts.Length(),
                                 fallible)) {
        JS_ReportOutOfMemory(cx);
        return NS_ERROR_OUT_OF_MEMORY;
      }
      for (auto pto : data->mData[i].stats[j].ptoCounts) {
        uint64_t& element = *ptoCounts.AppendElement(fallible);
        element = pto;
      }
    }
  }

  JS::Rooted<JS::Value> val(cx);
  if (!ToJSValue(cx, dict, &val)) {
    return NS_ERROR_FAILURE;
  }

  data->mCallback->OnDashboardDataAvailable(val);

  return NS_OK;
}

NS_IMETHODIMP
Dashboard::GetEnableLogging(bool* value) {
  *value = mEnableLogging;
  return NS_OK;
}

NS_IMETHODIMP
Dashboard::SetEnableLogging(const bool value) {
  mEnableLogging = value;
  return NS_OK;
}

NS_IMETHODIMP
Dashboard::AddHost(const nsACString& aHost, uint32_t aSerial, bool aEncrypted) {
  if (mEnableLogging) {
    mozilla::MutexAutoLock lock(mWs.lock);
    LogData mData(nsCString(aHost), aSerial, aEncrypted);
    if (mWs.data.Contains(mData)) {
      return NS_OK;
    }
    // XXX(Bug 1631371) Check if this should use a fallible operation as it
    // pretended earlier.
    mWs.data.AppendElement(mData);
    return NS_OK;
  }
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
Dashboard::RemoveHost(const nsACString& aHost, uint32_t aSerial) {
  if (mEnableLogging) {
    mozilla::MutexAutoLock lock(mWs.lock);
    int32_t index = mWs.IndexOf(nsCString(aHost), aSerial);
    if (index == -1) return NS_ERROR_FAILURE;
    mWs.data.RemoveElementAt(index);
    return NS_OK;
  }
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
Dashboard::NewMsgSent(const nsACString& aHost, uint32_t aSerial,
                      uint32_t aLength) {
  if (mEnableLogging) {
    mozilla::MutexAutoLock lock(mWs.lock);
    int32_t index = mWs.IndexOf(nsCString(aHost), aSerial);
    if (index == -1) return NS_ERROR_FAILURE;
    mWs.data[index].mMsgSent++;
    mWs.data[index].mSizeSent += aLength;
    return NS_OK;
  }
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
Dashboard::NewMsgReceived(const nsACString& aHost, uint32_t aSerial,
                          uint32_t aLength) {
  if (mEnableLogging) {
    mozilla::MutexAutoLock lock(mWs.lock);
    int32_t index = mWs.IndexOf(nsCString(aHost), aSerial);
    if (index == -1) return NS_ERROR_FAILURE;
    mWs.data[index].mMsgReceived++;
    mWs.data[index].mSizeReceived += aLength;
    return NS_OK;
  }
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
Dashboard::RequestWebsocketConnections(nsINetDashboardCallback* aCallback) {
  RefPtr<WebSocketRequest> wsRequest = new WebSocketRequest();
  wsRequest->mCallback = new nsMainThreadPtrHolder<nsINetDashboardCallback>(
      "nsINetDashboardCallback", aCallback, true);
  wsRequest->mEventTarget = GetCurrentSerialEventTarget();

  wsRequest->mEventTarget->Dispatch(
      NewRunnableMethod<RefPtr<WebSocketRequest>>(
          "net::Dashboard::GetWebSocketConnections", this,
          &Dashboard::GetWebSocketConnections, wsRequest),
      NS_DISPATCH_NORMAL);
  return NS_OK;
}

nsresult Dashboard::GetWebSocketConnections(WebSocketRequest* aWsRequest) {
  RefPtr<WebSocketRequest> wsRequest = aWsRequest;
  AutoSafeJSContext cx;

  mozilla::dom::WebSocketDict dict;
  dict.mWebsockets.Construct();
  Sequence<mozilla::dom::WebSocketElement>& websockets =
      dict.mWebsockets.Value();

  mozilla::MutexAutoLock lock(mWs.lock);
  uint32_t length = mWs.data.Length();
  if (!websockets.SetCapacity(length, fallible)) {
    JS_ReportOutOfMemory(cx);
    return NS_ERROR_OUT_OF_MEMORY;
  }

  for (uint32_t i = 0; i < mWs.data.Length(); i++) {
    dom::WebSocketElement& websocket = *websockets.AppendElement(fallible);
    CopyASCIItoUTF16(mWs.data[i].mHost, websocket.mHostport);
    websocket.mMsgsent = mWs.data[i].mMsgSent;
    websocket.mMsgreceived = mWs.data[i].mMsgReceived;
    websocket.mSentsize = mWs.data[i].mSizeSent;
    websocket.mReceivedsize = mWs.data[i].mSizeReceived;
    websocket.mEncrypted = mWs.data[i].mEncrypted;
  }

  JS::Rooted<JS::Value> val(cx);
  if (!ToJSValue(cx, dict, &val)) {
    return NS_ERROR_FAILURE;
  }
  wsRequest->mCallback->OnDashboardDataAvailable(val);

  return NS_OK;
}

NS_IMETHODIMP
Dashboard::RequestDNSInfo(nsINetDashboardCallback* aCallback) {
  RefPtr<DnsData> dnsData = new DnsData();
  dnsData->mCallback = new nsMainThreadPtrHolder<nsINetDashboardCallback>(
      "nsINetDashboardCallback", aCallback, true);

  nsresult rv;
  dnsData->mData.Clear();
  dnsData->mEventTarget = GetCurrentSerialEventTarget();

  if (!mDnsService) {
    mDnsService = mozilla::components::DNS::Service(&rv);
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  if (nsIOService::UseSocketProcess()) {
    if (!gIOService->SocketProcessReady()) {
      return NS_ERROR_NOT_AVAILABLE;
    }

    RefPtr<Dashboard> self(this);
    RefPtr<SocketProcessParent> socketParent =
        SocketProcessParent::GetSingleton();
    socketParent->SendGetDNSCacheEntries()->Then(
        GetMainThreadSerialEventTarget(), __func__,
        [self{std::move(self)},
         dnsData{std::move(dnsData)}](nsTArray<DNSCacheEntries>&& entries) {
          dnsData->mData.Assign(std::move(entries));
          dnsData->mEventTarget->Dispatch(
              NewRunnableMethod<RefPtr<DnsData>>(
                  "net::Dashboard::GetDNSCacheEntries", self,
                  &Dashboard::GetDNSCacheEntries, dnsData),
              NS_DISPATCH_NORMAL);
        },
        [self](const mozilla::ipc::ResponseRejectReason) {});
    return NS_OK;
  }

  gSocketTransportService->Dispatch(
      NewRunnableMethod<RefPtr<DnsData>>("net::Dashboard::GetDnsInfoDispatch",
                                         this, &Dashboard::GetDnsInfoDispatch,
                                         dnsData),
      NS_DISPATCH_NORMAL);
  return NS_OK;
}

nsresult Dashboard::GetDnsInfoDispatch(DnsData* aDnsData) {
  RefPtr<DnsData> dnsData = aDnsData;
  if (mDnsService) {
    mDnsService->GetDNSCacheEntries(&dnsData->mData);
  }
  dnsData->mEventTarget->Dispatch(
      NewRunnableMethod<RefPtr<DnsData>>("net::Dashboard::GetDNSCacheEntries",
                                         this, &Dashboard::GetDNSCacheEntries,
                                         dnsData),
      NS_DISPATCH_NORMAL);
  return NS_OK;
}

nsresult Dashboard::GetDNSCacheEntries(DnsData* dnsData) {
  AutoSafeJSContext cx;

  mozilla::dom::DNSCacheDict dict;
  dict.mEntries.Construct();
  Sequence<mozilla::dom::DnsCacheEntry>& entries = dict.mEntries.Value();

  uint32_t length = dnsData->mData.Length();
  if (!entries.SetCapacity(length, fallible)) {
    JS_ReportOutOfMemory(cx);
    return NS_ERROR_OUT_OF_MEMORY;
  }

  for (uint32_t i = 0; i < dnsData->mData.Length(); i++) {
    dom::DnsCacheEntry& entry = *entries.AppendElement(fallible);
    entry.mHostaddr.Construct();

    Sequence<nsString>& addrs = entry.mHostaddr.Value();
    if (!addrs.SetCapacity(dnsData->mData[i].hostaddr.Length(), fallible)) {
      JS_ReportOutOfMemory(cx);
      return NS_ERROR_OUT_OF_MEMORY;
    }

    CopyASCIItoUTF16(dnsData->mData[i].hostname, entry.mHostname);
    entry.mExpiration = dnsData->mData[i].expiration;
    entry.mTrr = dnsData->mData[i].TRR;

    for (uint32_t j = 0; j < dnsData->mData[i].hostaddr.Length(); j++) {
      nsString* addr = addrs.AppendElement(fallible);
      if (!addr) {
        JS_ReportOutOfMemory(cx);
        return NS_ERROR_OUT_OF_MEMORY;
      }
      CopyASCIItoUTF16(dnsData->mData[i].hostaddr[j], *addr);
    }

    entry.mType = dnsData->mData[i].resolveType;
    if (entry.mType == nsIDNSService::RESOLVE_TYPE_DEFAULT) {
      if (dnsData->mData[i].family == PR_AF_INET6) {
        entry.mFamily.AssignLiteral(u"ipv6");
      } else {
        entry.mFamily.AssignLiteral(u"ipv4");
      }
    }

    entry.mOriginAttributesSuffix =
        NS_ConvertUTF8toUTF16(dnsData->mData[i].originAttributesSuffix);
    entry.mFlags = NS_ConvertUTF8toUTF16(dnsData->mData[i].flags);
  }

  JS::Rooted<JS::Value> val(cx);
  if (!ToJSValue(cx, dict, &val)) {
    return NS_ERROR_FAILURE;
  }
  dnsData->mCallback->OnDashboardDataAvailable(val);

  return NS_OK;
}

NS_IMETHODIMP
Dashboard::RequestDNSLookup(const nsACString& aHost,
                            nsINetDashboardCallback* aCallback) {
  nsresult rv;

  if (!mDnsService) {
    mDnsService = mozilla::components::DNS::Service(&rv);
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  RefPtr<LookupHelper> helper = new LookupHelper();
  helper->mCallback = new nsMainThreadPtrHolder<nsINetDashboardCallback>(
      "nsINetDashboardCallback", aCallback, true);
  helper->mEventTarget = GetCurrentSerialEventTarget();
  OriginAttributes attrs;
  rv = mDnsService->AsyncResolveNative(
      aHost, nsIDNSService::RESOLVE_TYPE_DEFAULT,
      nsIDNSService::RESOLVE_DEFAULT_FLAGS, nullptr, helper.get(),
      NS_GetCurrentThread(), attrs, getter_AddRefs(helper->mCancel));
  return rv;
}

NS_IMETHODIMP
Dashboard::RequestDNSHTTPSRRLookup(const nsACString& aHost,
                                   nsINetDashboardCallback* aCallback) {
  nsresult rv;

  if (!mDnsService) {
    mDnsService = mozilla::components::DNS::Service(&rv);
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  RefPtr<LookupHelper> helper = new LookupHelper();
  helper->mCallback = new nsMainThreadPtrHolder<nsINetDashboardCallback>(
      "nsINetDashboardCallback", aCallback, true);
  helper->mEventTarget = GetCurrentSerialEventTarget();
  OriginAttributes attrs;
  rv = mDnsService->AsyncResolveNative(
      aHost, nsIDNSService::RESOLVE_TYPE_HTTPSSVC,
      nsIDNSService::RESOLVE_DEFAULT_FLAGS, nullptr, helper.get(),
      NS_GetCurrentThread(), attrs, getter_AddRefs(helper->mCancel));
  return rv;
}

NS_IMETHODIMP
Dashboard::RequestRcwnStats(nsINetDashboardCallback* aCallback) {
  RefPtr<RcwnData> rcwnData = new RcwnData();
  rcwnData->mEventTarget = GetCurrentSerialEventTarget();
  rcwnData->mCallback = new nsMainThreadPtrHolder<nsINetDashboardCallback>(
      "nsINetDashboardCallback", aCallback, true);

  return rcwnData->mEventTarget->Dispatch(
      NewRunnableMethod<RefPtr<RcwnData>>("net::Dashboard::GetRcwnData", this,
                                          &Dashboard::GetRcwnData, rcwnData),
      NS_DISPATCH_NORMAL);
}

nsresult Dashboard::GetRcwnData(RcwnData* aData) {
  AutoSafeJSContext cx;
  mozilla::dom::RcwnStatus dict;

  dict.mTotalNetworkRequests = gIOService->GetTotalRequestNumber();
  dict.mRcwnCacheWonCount = gIOService->GetCacheWonRequestNumber();
  dict.mRcwnNetWonCount = gIOService->GetNetWonRequestNumber();

  uint32_t cacheSlow, cacheNotSlow;
  CacheFileUtils::CachePerfStats::GetSlowStats(&cacheSlow, &cacheNotSlow);
  dict.mCacheSlowCount = cacheSlow;
  dict.mCacheNotSlowCount = cacheNotSlow;

  dict.mPerfStats.Construct();
  Sequence<mozilla::dom::RcwnPerfStats>& perfStats = dict.mPerfStats.Value();
  uint32_t length = CacheFileUtils::CachePerfStats::LAST;
  if (!perfStats.SetCapacity(length, fallible)) {
    JS_ReportOutOfMemory(cx);
    return NS_ERROR_OUT_OF_MEMORY;
  }

  for (uint32_t i = 0; i < length; i++) {
    CacheFileUtils::CachePerfStats::EDataType perfType =
        static_cast<CacheFileUtils::CachePerfStats::EDataType>(i);
    dom::RcwnPerfStats& elem = *perfStats.AppendElement(fallible);
    elem.mAvgShort =
        CacheFileUtils::CachePerfStats::GetAverage(perfType, false);
    elem.mAvgLong = CacheFileUtils::CachePerfStats::GetAverage(perfType, true);
    elem.mStddevLong =
        CacheFileUtils::CachePerfStats::GetStdDev(perfType, true);
  }

  JS::Rooted<JS::Value> val(cx);
  if (!ToJSValue(cx, dict, &val)) {
    return NS_ERROR_FAILURE;
  }

  aData->mCallback->OnDashboardDataAvailable(val);

  return NS_OK;
}

void HttpConnInfo::SetHTTPProtocolVersion(HttpVersion pv) {
  switch (pv) {
    case HttpVersion::v0_9:
      protocolVersion.AssignLiteral(u"http/0.9");
      break;
    case HttpVersion::v1_0:
      protocolVersion.AssignLiteral(u"http/1.0");
      break;
    case HttpVersion::v1_1:
      protocolVersion.AssignLiteral(u"http/1.1");
      break;
    case HttpVersion::v2_0:
      protocolVersion.AssignLiteral(u"http/2");
      break;
    case HttpVersion::v3_0:
      protocolVersion.AssignLiteral(u"http/3");
      break;
    default:
      protocolVersion.AssignLiteral(u"unknown protocol version");
  }
}

NS_IMETHODIMP
Dashboard::GetLogPath(nsACString& aLogPath) {
  aLogPath.SetLength(2048);
  uint32_t len = LogModule::GetLogFile(aLogPath.BeginWriting(), 2048);
  aLogPath.SetLength(len);
  return NS_OK;
}

NS_IMETHODIMP
Dashboard::RequestConnection(const nsACString& aHost, uint32_t aPort,
                             const char* aProtocol, uint32_t aTimeout,
                             nsINetDashboardCallback* aCallback) {
  nsresult rv;
  RefPtr<ConnectionData> connectionData = new ConnectionData(this);
  connectionData->mHost = aHost;
  connectionData->mPort = aPort;
  connectionData->mProtocol = aProtocol;
  connectionData->mTimeout = aTimeout;

  connectionData->mCallback =
      new nsMainThreadPtrHolder<nsINetDashboardCallback>(
          "nsINetDashboardCallback", aCallback, true);
  connectionData->mEventTarget = GetCurrentSerialEventTarget();

  rv = TestNewConnection(connectionData);
  if (NS_FAILED(rv)) {
    mozilla::net::GetErrorString(rv, connectionData->mStatus);
    connectionData->mEventTarget->Dispatch(
        NewRunnableMethod<RefPtr<ConnectionData>>(
            "net::Dashboard::GetConnectionStatus", this,
            &Dashboard::GetConnectionStatus, connectionData),
        NS_DISPATCH_NORMAL);
    return rv;
  }

  return NS_OK;
}

nsresult Dashboard::GetConnectionStatus(ConnectionData* aConnectionData) {
  RefPtr<ConnectionData> connectionData = aConnectionData;
  AutoSafeJSContext cx;

  mozilla::dom::ConnStatusDict dict;
  dict.mStatus = connectionData->mStatus;

  JS::Rooted<JS::Value> val(cx);
  if (!ToJSValue(cx, dict, &val)) return NS_ERROR_FAILURE;

  connectionData->mCallback->OnDashboardDataAvailable(val);

  return NS_OK;
}

nsresult Dashboard::TestNewConnection(ConnectionData* aConnectionData) {
  RefPtr<ConnectionData> connectionData = aConnectionData;

  nsresult rv;
  if (!connectionData->mHost.Length() ||
      !net_IsValidDNSHost(connectionData->mHost)) {
    return NS_ERROR_UNKNOWN_HOST;
  }

  if (connectionData->mProtocol.EqualsLiteral("ssl")) {
    AutoTArray<nsCString, 1> socketTypes = {connectionData->mProtocol};
    rv = gSocketTransportService->CreateTransport(
        socketTypes, connectionData->mHost, connectionData->mPort, nullptr,
        nullptr, getter_AddRefs(connectionData->mSocket));
  } else {
    rv = gSocketTransportService->CreateTransport(
        nsTArray<nsCString>(), connectionData->mHost, connectionData->mPort,
        nullptr, nullptr, getter_AddRefs(connectionData->mSocket));
  }
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = connectionData->mSocket->SetEventSink(connectionData,
                                             GetCurrentSerialEventTarget());
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = connectionData->mSocket->OpenInputStream(
      nsITransport::OPEN_BLOCKING, 0, 0,
      getter_AddRefs(connectionData->mStreamIn));
  if (NS_FAILED(rv)) {
    return rv;
  }

  connectionData->StartTimer(connectionData->mTimeout);

  return rv;
}

using ErrorEntry = struct {
  nsresult key;
  const char* error;
};

#undef ERROR
#define ERROR(key, val) {key, #key}

ErrorEntry socketTransportStatuses[] = {
    ERROR(NS_NET_STATUS_RESOLVING_HOST, FAILURE(3)),
    ERROR(NS_NET_STATUS_RESOLVED_HOST, FAILURE(11)),
    ERROR(NS_NET_STATUS_CONNECTING_TO, FAILURE(7)),
    ERROR(NS_NET_STATUS_CONNECTED_TO, FAILURE(4)),
    ERROR(NS_NET_STATUS_TLS_HANDSHAKE_STARTING, FAILURE(12)),
    ERROR(NS_NET_STATUS_TLS_HANDSHAKE_ENDED, FAILURE(13)),
    ERROR(NS_NET_STATUS_SENDING_TO, FAILURE(5)),
    ERROR(NS_NET_STATUS_WAITING_FOR, FAILURE(10)),
    ERROR(NS_NET_STATUS_RECEIVING_FROM, FAILURE(6)),
};
#undef ERROR

static void GetErrorString(nsresult rv, nsAString& errorString) {
  for (auto& socketTransportStatus : socketTransportStatuses) {
    if (socketTransportStatus.key == rv) {
      errorString.AssignASCII(socketTransportStatus.error);
      return;
    }
  }
  nsAutoCString errorCString;
  mozilla::GetErrorName(rv, errorCString);
  CopyUTF8toUTF16(errorCString, errorString);
}

}  // namespace net
}  // namespace mozilla
