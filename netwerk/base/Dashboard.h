/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsDashboard_h__
#define nsDashboard_h__

#include "mozilla/Mutex.h"
#include "mozilla/net/DashboardTypes.h"
#include "nsIDashboard.h"
#include "nsIDashboardEventNotifier.h"

class nsIDNSService;

namespace mozilla {
namespace net {

class SocketData;
class HttpData;
class Http3ConnectionStatsData;
class DnsData;
class WebSocketRequest;
class ConnectionData;
class RcwnData;

class Dashboard final : public nsIDashboard, public nsIDashboardEventNotifier {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIDASHBOARD
  NS_DECL_NSIDASHBOARDEVENTNOTIFIER

  Dashboard();
  static const char* GetErrorString(nsresult rv);
  nsresult GetConnectionStatus(ConnectionData* aConnectionData);

 private:
  struct LogData {
    LogData(nsCString host, uint32_t serial, bool encryption)
        : mHost(host),
          mSerial(serial),
          mMsgSent(0),
          mMsgReceived(0),
          mSizeSent(0),
          mSizeReceived(0),
          mEncrypted(encryption) {}
    nsCString mHost;
    uint32_t mSerial;
    uint32_t mMsgSent;
    uint32_t mMsgReceived;
    uint64_t mSizeSent;
    uint64_t mSizeReceived;
    bool mEncrypted;
    bool operator==(const LogData& a) const {
      return mHost.Equals(a.mHost) && (mSerial == a.mSerial);
    }
  };

  struct WebSocketData {
    WebSocketData() : lock("Dashboard.webSocketData") {}
    uint32_t IndexOf(const nsCString& hostname, uint32_t mSerial) {
      LogData temp(hostname, mSerial, false);
      return data.IndexOf(temp);
    }
    nsTArray<LogData> data;
    mozilla::Mutex lock MOZ_UNANNOTATED;
  };

  bool mEnableLogging;
  WebSocketData mWs;

 private:
  virtual ~Dashboard() = default;

  nsresult GetSocketsDispatch(SocketData*);
  nsresult GetHttpDispatch(HttpData*);
  nsresult GetHttp3ConnectionStatsDispatch(Http3ConnectionStatsData*);
  nsresult GetDnsInfoDispatch(DnsData*);
  nsresult TestNewConnection(ConnectionData*);

  /* Helper methods that pass the JSON to the callback function. */
  nsresult GetSockets(SocketData*);
  nsresult GetHttpConnections(HttpData*);
  nsresult GetHttp3ConnectionStats(Http3ConnectionStatsData*);
  nsresult GetDNSCacheEntries(DnsData*);
  nsresult GetWebSocketConnections(WebSocketRequest*);
  nsresult GetRcwnData(RcwnData*);

  nsCOMPtr<nsIDNSService> mDnsService;
};

}  // namespace net
}  // namespace mozilla

#endif  // nsDashboard_h__
