/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebSocketChannel.h"

#include <algorithm>

#include "WebSocketFrame.h"
#include "WebSocketLog.h"
#include "mozilla/Atomics.h"
#include "mozilla/Attributes.h"
#include "mozilla/Base64.h"
#include "mozilla/Components.h"
#include "mozilla/EndianUtils.h"
#include "mozilla/MathAlgorithms.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/StaticPrefs_privacy.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/Utf8.h"
#include "mozilla/glean/NetwerkProtocolWebsocketMetrics.h"
#include "mozilla/net/WebSocketEventService.h"
#include "nsCRT.h"
#include "nsCharSeparatedTokenizer.h"
#include "nsComponentManagerUtils.h"
#include "nsError.h"
#include "nsIAsyncVerifyRedirectCallback.h"
#include "nsICancelable.h"
#include "nsIChannel.h"
#include "nsIClassOfService.h"
#include "nsICryptoHash.h"
#include "nsIDNSRecord.h"
#include "nsIDNSService.h"
#include "nsIDashboardEventNotifier.h"
#include "nsIEventTarget.h"
#include "nsIHttpChannel.h"
#include "nsIIOService.h"
#include "nsINSSErrorsService.h"
#include "nsINetworkLinkService.h"
#include "nsINode.h"
#include "nsIObserverService.h"
#include "nsIPrefBranch.h"
#include "nsIProtocolProxyService.h"
#include "nsIProxiedChannel.h"
#include "nsIProxyInfo.h"
#include "nsIRandomGenerator.h"
#include "nsIRunnable.h"
#include "nsISocketTransport.h"
#include "nsITLSSocketControl.h"
#include "nsITransportProvider.h"
#include "nsITransportSecurityInfo.h"
#include "nsIURI.h"
#include "nsIURIMutator.h"
#include "nsNetCID.h"
#include "nsNetUtil.h"
#include "nsProxyRelease.h"
#include "nsServiceManagerUtils.h"
#include "nsSocketTransportService2.h"
#include "nsStringStream.h"
#include "nsThreadUtils.h"
#include "plbase64.h"
#include "prmem.h"
#include "prnetdb.h"
#include "zlib.h"

// rather than slurp up all of nsIWebSocket.idl, which lives outside necko, just
// dupe one constant we need from it
#define CLOSE_GOING_AWAY 1001

using namespace mozilla;
using namespace mozilla::net;

namespace mozilla::net {

NS_IMPL_ISUPPORTS(WebSocketChannel, nsIWebSocketChannel, nsIHttpUpgradeListener,
                  nsIRequestObserver, nsIStreamListener, nsIInputStreamCallback,
                  nsIOutputStreamCallback, nsITimerCallback, nsIDNSListener,
                  nsIProtocolProxyCallback, nsIInterfaceRequestor,
                  nsIChannelEventSink, nsIThreadRetargetableRequest,
                  nsIObserver, nsINamed)

// We implement RFC 6455, which uses Sec-WebSocket-Version: 13 on the wire.
#define SEC_WEBSOCKET_VERSION "13"

/*
 * About SSL unsigned certificates
 *
 * wss will not work to a host using an unsigned certificate unless there
 * is already an exception (i.e. it cannot popup a dialog asking for
 * a security exception). This is similar to how an inlined img will
 * fail without a dialog if fails for the same reason. This should not
 * be a problem in practice as it is expected the websocket javascript
 * is served from the same host as the websocket server (or of course,
 * a valid cert could just be provided).
 *
 */

// some helper classes

//-----------------------------------------------------------------------------
// FailDelayManager
//
// Stores entries (searchable by {host, port}) of connections that have recently
// failed, so we can do delay of reconnects per RFC 6455 Section 7.2.3
//-----------------------------------------------------------------------------

// Initial reconnect delay is randomly chosen between 200-400 ms.
// This is a gentler backoff than the 0-5 seconds the spec offhandedly suggests.
const uint32_t kWSReconnectInitialBaseDelay = 200;
const uint32_t kWSReconnectInitialRandomDelay = 200;

// Base lifetime (in ms) of a FailDelay: kept longer if more failures occur
const uint32_t kWSReconnectBaseLifeTime = 60 * 1000;
// Maximum reconnect delay (in ms)
const uint32_t kWSReconnectMaxDelay = 60 * 1000;

// hold record of failed connections, and calculates needed delay for reconnects
// to same host/path/port.
class FailDelay {
 public:
  FailDelay(nsCString address, nsCString path, int32_t port,
            nsCString originSuffix)
      : mAddress(std::move(address)),
        mPath(std::move(path)),
        mPort(port),
        mOriginSuffix(std::move(originSuffix)) {
    mLastFailure = TimeStamp::Now();
    mNextDelay = kWSReconnectInitialBaseDelay +
                 (rand() % kWSReconnectInitialRandomDelay);
  }

  // Called to update settings when connection fails again.
  void FailedAgain() {
    mLastFailure = TimeStamp::Now();
    // We use a truncated exponential backoff as suggested by RFC 6455,
    // but multiply by 1.5 instead of 2 to be more gradual.
    mNextDelay = static_cast<uint32_t>(
        std::min<double>(kWSReconnectMaxDelay, mNextDelay * 1.5));
    LOG(
        ("WebSocket: FailedAgain: host=%s, path=%s, port=%d: incremented delay "
         "to "
         "%" PRIu32,
         mAddress.get(), mPath.get(), mPort, mNextDelay));
  }

  // returns 0 if there is no need to delay (i.e. delay interval is over)
  uint32_t RemainingDelay(TimeStamp rightNow) {
    TimeDuration dur = rightNow - mLastFailure;
    uint32_t sinceFail = (uint32_t)dur.ToMilliseconds();
    if (sinceFail > mNextDelay) return 0;

    return mNextDelay - sinceFail;
  }

  bool IsExpired(TimeStamp rightNow) {
    return (mLastFailure + TimeDuration::FromMilliseconds(
                               kWSReconnectBaseLifeTime + mNextDelay)) <=
           rightNow;
  }

  nsCString mAddress;  // IP address (or hostname if using proxy)
  nsCString mPath;
  int32_t mPort;
  nsCString mOriginSuffix;

 private:
  TimeStamp mLastFailure;  // Time of last failed attempt
  // mLastFailure + mNextDelay is the soonest we'll allow a reconnect
  uint32_t mNextDelay;  // milliseconds
};

class FailDelayManager {
 public:
  FailDelayManager() {
    MOZ_COUNT_CTOR(FailDelayManager);

    mDelaysDisabled = false;

    nsCOMPtr<nsIPrefBranch> prefService;
    prefService = mozilla::components::Preferences::Service();
    if (!prefService) {
      return;
    }
    bool boolpref = true;
    nsresult rv;
    rv = prefService->GetBoolPref("network.websocket.delay-failed-reconnects",
                                  &boolpref);
    if (NS_SUCCEEDED(rv) && !boolpref) {
      mDelaysDisabled = true;
    }
  }

  ~FailDelayManager() { MOZ_COUNT_DTOR(FailDelayManager); }

  void Add(nsCString& address, nsCString& path, int32_t port,
           nsCString& originSuffix) {
    if (mDelaysDisabled) {
      return;
    }

    mEntries.AppendElement(
        std::make_unique<FailDelay>(address, path, port, originSuffix));
  }

  // Element returned may not be valid after next main thread event: don't keep
  // pointer to it around
  FailDelay* Lookup(nsCString& address, nsCString& path, int32_t port,
                    nsCString& originSuffix, uint32_t* outIndex = nullptr) {
    if (mDelaysDisabled) {
      return nullptr;
    }

    FailDelay* result = nullptr;
    TimeStamp rightNow = TimeStamp::Now();

    // We also remove expired entries during search: iterate from end to make
    // indexing simpler
    for (int32_t i = mEntries.Length() - 1; i >= 0; --i) {
      FailDelay* fail = mEntries[i].get();
      if (fail->mAddress.Equals(address) && fail->mPath.Equals(path) &&
          fail->mPort == port && fail->mOriginSuffix.Equals(originSuffix)) {
        if (outIndex) *outIndex = i;
        result = fail;
        // break here: removing more entries would mess up *outIndex.
        // Any remaining expired entries will be deleted next time Lookup
        // finds nothing, which is the most common case anyway.
        break;
      }
      if (fail->IsExpired(rightNow)) {
        mEntries.RemoveElementAt(i);
      }
    }
    return result;
  }

  // returns true if channel connects immediately, or false if it's delayed
  void DelayOrBegin(WebSocketChannel* ws) {
    if (!mDelaysDisabled) {
      uint32_t failIndex = 0;
      FailDelay* fail = Lookup(ws->mAddress, ws->mPath, ws->mPort,
                               ws->mOriginSuffix, &failIndex);

      if (fail) {
        TimeStamp rightNow = TimeStamp::Now();

        uint32_t remainingDelay = fail->RemainingDelay(rightNow);
        if (remainingDelay) {
          // reconnecting within delay interval: delay by remaining time
          nsresult rv;
          MutexAutoLock lock(ws->mMutex);
          rv = NS_NewTimerWithCallback(getter_AddRefs(ws->mReconnectDelayTimer),
                                       ws, remainingDelay,
                                       nsITimer::TYPE_ONE_SHOT);
          if (NS_SUCCEEDED(rv)) {
            LOG(
                ("WebSocket: delaying websocket [this=%p] by %lu ms, changing"
                 " state to CONNECTING_DELAYED",
                 ws, (unsigned long)remainingDelay));
            ws->mConnecting = CONNECTING_DELAYED;
            return;
          }
          // if timer fails (which is very unlikely), drop down to BeginOpen
          // call
        } else if (fail->IsExpired(rightNow)) {
          mEntries.RemoveElementAt(failIndex);
        }
      }
    }

    // Delays disabled, or no previous failure, or we're reconnecting after
    // scheduled delay interval has passed: connect.
    ws->BeginOpen(true);
  }

  // Remove() also deletes all expired entries as it iterates: better for
  // battery life than using a periodic timer.
  void Remove(nsCString& address, nsCString& path, int32_t port,
              nsCString& originSuffix) {
    TimeStamp rightNow = TimeStamp::Now();

    // iterate from end, to make deletion indexing easier
    for (int32_t i = mEntries.Length() - 1; i >= 0; --i) {
      FailDelay* entry = mEntries[i].get();
      if ((entry->mAddress.Equals(address) && entry->mPath.Equals(path) &&
           entry->mPort == port && entry->mOriginSuffix.Equals(originSuffix)) ||
          entry->IsExpired(rightNow)) {
        mEntries.RemoveElementAt(i);
      }
    }
  }

 private:
  nsTArray<UniquePtr<FailDelay>> mEntries;
  bool mDelaysDisabled;
};

//-----------------------------------------------------------------------------
// nsWSAdmissionManager
//
// 1) Ensures that only one websocket at a time is CONNECTING to a given IP
//    address (or hostname, if using proxy), per RFC 6455 Section 4.1.
// 2) Delays reconnects to IP/host after connection failure, per Section 7.2.3
//-----------------------------------------------------------------------------

class nsWSAdmissionManager {
 public:
  static void Init() {
    StaticMutexAutoLock lock(sLock);
    if (!sManager) {
      sManager = new nsWSAdmissionManager();
    }
  }

  static void Shutdown() {
    StaticMutexAutoLock lock(sLock);
    delete sManager;
    sManager = nullptr;
  }

  // Determine if we will open connection immediately (returns true), or
  // delay/queue the connection (returns false)
  static void ConditionallyConnect(WebSocketChannel* ws) {
    LOG(("Websocket: ConditionallyConnect: [this=%p]", ws));
    MOZ_ASSERT(NS_IsMainThread(), "not main thread");
    MOZ_ASSERT(ws->mConnecting == NOT_CONNECTING, "opening state");

    StaticMutexAutoLock lock(sLock);
    if (!sManager) {
      return;
    }

    // If there is already another WS channel connecting to this IP address,
    // defer BeginOpen and mark as waiting in queue.
    bool hostFound = (sManager->IndexOf(ws->mAddress, ws->mOriginSuffix) >= 0);

    uint32_t failIndex = 0;
    FailDelay* fail = sManager->mFailures.Lookup(
        ws->mAddress, ws->mPath, ws->mPort, ws->mOriginSuffix, &failIndex);
    bool existingFail = fail != nullptr;

    // Always add ourselves to queue, even if we'll connect immediately
    auto newdata = std::make_unique<nsOpenConn>(ws->mAddress, ws->mOriginSuffix,
                                                existingFail, ws);

    // If a connection has not previously failed then prioritize it over
    // connections that have
    if (existingFail) {
      sManager->mQueue.AppendElement(std::move(newdata));
    } else {
      uint32_t insertionIndex = sManager->IndexOfFirstFailure();
      MOZ_ASSERT(insertionIndex <= sManager->mQueue.Length(),
                 "Insertion index outside bounds");
      sManager->mQueue.InsertElementAt(insertionIndex, std::move(newdata));
    }

    if (hostFound) {
      LOG(
          ("Websocket: some other channel is connecting, changing state to "
           "CONNECTING_QUEUED"));
      ws->mConnecting = CONNECTING_QUEUED;
    } else {
      sManager->mFailures.DelayOrBegin(ws);
    }
  }

  static void OnConnected(WebSocketChannel* aChannel) {
    LOG(("Websocket: OnConnected: [this=%p]", aChannel));

    MOZ_ASSERT(NS_IsMainThread(), "not main thread");
    MOZ_ASSERT(aChannel->mConnecting == CONNECTING_IN_PROGRESS,
               "Channel completed connect, but not connecting?");

    StaticMutexAutoLock lock(sLock);
    if (!sManager) {
      return;
    }

    LOG(("Websocket: changing state to NOT_CONNECTING"));
    aChannel->mConnecting = NOT_CONNECTING;

    // Remove from queue
    sManager->RemoveFromQueue(aChannel);

    // Connection succeeded, so stop keeping track of any previous failures
    sManager->mFailures.Remove(aChannel->mAddress, aChannel->mPath,
                               aChannel->mPort, aChannel->mOriginSuffix);

    // Check for queued connections to same host.
    // Note: still need to check for failures, since next websocket with same
    // host may have different port
    sManager->ConnectNext(aChannel->mAddress, aChannel->mOriginSuffix);
  }

  // Called every time a websocket channel ends its session (including going
  // away w/o ever successfully creating a connection)
  static void OnStopSession(WebSocketChannel* aChannel, nsresult aReason) {
    LOG(("Websocket: OnStopSession: [this=%p, reason=0x%08" PRIx32 "]",
         aChannel, static_cast<uint32_t>(aReason)));

    StaticMutexAutoLock lock(sLock);
    if (!sManager) {
      return;
    }

    if (NS_FAILED(aReason)) {
      // Have we seen this failure before?
      FailDelay* knownFailure =
          sManager->mFailures.Lookup(aChannel->mAddress, aChannel->mPath,
                                     aChannel->mPort, aChannel->mOriginSuffix);
      if (knownFailure) {
        if (aReason == NS_ERROR_NOT_CONNECTED) {
          // Don't count close() before connection as a network error
          LOG(
              ("Websocket close() before connection to %s, %s,  %d completed"
               " [this=%p]",
               aChannel->mAddress.get(), aChannel->mPath.get(),
               (int)aChannel->mPort, aChannel));
        } else {
          // repeated failure to connect: increase delay for next connection
          knownFailure->FailedAgain();
        }
      } else {
        // new connection failure: record it.
        LOG(("WebSocket: connection to %s, %s, %d failed: [this=%p]",
             aChannel->mAddress.get(), aChannel->mPath.get(),
             (int)aChannel->mPort, aChannel));
        sManager->mFailures.Add(aChannel->mAddress, aChannel->mPath,
                                aChannel->mPort, aChannel->mOriginSuffix);
      }
    }

    if (NS_IsMainThread()) {
      ContinueOnStopSession(aChannel, aReason);
    } else {
      NS_DispatchToMainThread(NS_NewRunnableFunction(
          "nsWSAdmissionManager::ContinueOnStopSession",
          [channel = RefPtr{aChannel}, reason = aReason]() {
            StaticMutexAutoLock lock(sLock);
            if (!sManager) {
              return;
            }

            nsWSAdmissionManager::ContinueOnStopSession(channel, reason);
          }));
    }
  }

  static void ContinueOnStopSession(WebSocketChannel* aChannel,
                                    nsresult aReason) {
    sLock.AssertCurrentThreadOwns();
    MOZ_ASSERT(NS_IsMainThread(), "not main thread");

    if (!aChannel->mConnecting) {
      return;
    }

    // Only way a connecting channel may get here w/o failing is if it
    // was closed with GOING_AWAY (1001) because of navigation, tab
    // close, etc.
#ifdef DEBUG
    {
      MutexAutoLock lock(aChannel->mMutex);
      MOZ_ASSERT(
          NS_FAILED(aReason) || aChannel->mScriptCloseCode == CLOSE_GOING_AWAY,
          "websocket closed while connecting w/o failing?");
    }
#endif
    (void)aReason;

    sManager->RemoveFromQueue(aChannel);

    bool wasNotQueued = (aChannel->mConnecting != CONNECTING_QUEUED);
    LOG(("Websocket: changing state to NOT_CONNECTING"));
    aChannel->mConnecting = NOT_CONNECTING;
    if (wasNotQueued) {
      sManager->ConnectNext(aChannel->mAddress, aChannel->mOriginSuffix);
    }
  }

  static void IncrementSessionCount() {
    StaticMutexAutoLock lock(sLock);
    if (!sManager) {
      return;
    }
    sManager->mSessionCount++;
  }

  static void DecrementSessionCount() {
    StaticMutexAutoLock lock(sLock);
    if (!sManager) {
      return;
    }
    sManager->mSessionCount--;
  }

  static void GetSessionCount(int32_t& aSessionCount) {
    StaticMutexAutoLock lock(sLock);
    if (!sManager) {
      return;
    }
    aSessionCount = sManager->mSessionCount;
  }

 private:
  nsWSAdmissionManager() : mSessionCount(0) {
    MOZ_COUNT_CTOR(nsWSAdmissionManager);
  }

  ~nsWSAdmissionManager() { MOZ_COUNT_DTOR(nsWSAdmissionManager); }

  class nsOpenConn {
   public:
    nsOpenConn(nsCString& addr, nsCString& originSuffix, bool failed,
               WebSocketChannel* channel)
        : mAddress(addr),
          mOriginSuffix(originSuffix),
          mFailed(failed),
          mChannel(channel) {
      MOZ_COUNT_CTOR(nsOpenConn);
    }
    MOZ_COUNTED_DTOR(nsOpenConn)

    nsCString mAddress;
    nsCString mOriginSuffix;
    bool mFailed = false;
    RefPtr<WebSocketChannel> mChannel;
  };

  void ConnectNext(nsCString& hostName, nsCString& originSuffix) {
    MOZ_ASSERT(NS_IsMainThread(), "not main thread");

    int32_t index = IndexOf(hostName, originSuffix);
    if (index >= 0) {
      WebSocketChannel* chan = mQueue[index]->mChannel;

      MOZ_ASSERT(chan->mConnecting == CONNECTING_QUEUED,
                 "transaction not queued but in queue");
      LOG(("WebSocket: ConnectNext: found channel [this=%p] in queue", chan));

      mFailures.DelayOrBegin(chan);
    }
  }

  void RemoveFromQueue(WebSocketChannel* aChannel) {
    LOG(("Websocket: RemoveFromQueue: [this=%p]", aChannel));
    int32_t index = IndexOf(aChannel);
    MOZ_ASSERT(index >= 0, "connection to remove not in queue");
    if (index >= 0) {
      mQueue.RemoveElementAt(index);
    }
  }

  int32_t IndexOf(nsCString& aAddress, nsCString& aOriginSuffix) {
    for (uint32_t i = 0; i < mQueue.Length(); i++) {
      if (aAddress == mQueue[i]->mAddress &&
          aOriginSuffix == mQueue[i]->mOriginSuffix) {
        return i;
      }
    }
    return -1;
  }

  int32_t IndexOf(WebSocketChannel* aChannel) {
    for (uint32_t i = 0; i < mQueue.Length(); i++) {
      if (aChannel == mQueue[i]->mChannel) {
        return i;
      }
    }
    return -1;
  }

  // Returns the index of the first entry that failed, or else the last entry if
  // none found
  uint32_t IndexOfFirstFailure() {
    for (uint32_t i = 0; i < mQueue.Length(); i++) {
      if (mQueue[i]->mFailed) return i;
    }
    return mQueue.Length();
  }

  // SessionCount might be decremented from the main or the socket
  // thread, so manage it with atomic counters
  Atomic<int32_t> mSessionCount;

  // Queue for websockets that have not completed connecting yet.
  // The first nsOpenConn with a given address will be either be
  // CONNECTING_IN_PROGRESS or CONNECTING_DELAYED.  Later ones with the same
  // hostname must be CONNECTING_QUEUED.
  //
  // We could hash hostnames instead of using a single big vector here, but the
  // dataset is expected to be small.
  nsTArray<UniquePtr<nsOpenConn>> mQueue;

  FailDelayManager mFailures;

  static nsWSAdmissionManager* sManager MOZ_GUARDED_BY(sLock);
  static StaticMutex sLock;
};

nsWSAdmissionManager* nsWSAdmissionManager::sManager;
StaticMutex nsWSAdmissionManager::sLock;

//-----------------------------------------------------------------------------
// CallOnMessageAvailable
//-----------------------------------------------------------------------------

class CallOnMessageAvailable final : public Runnable {
 public:
  CallOnMessageAvailable(
      WebSocketChannel* aChannel,
      RefPtr<BaseWebSocketChannel::ListenerAndContextContainer>&& aListenerMT,
      nsACString& aData, int32_t aLen)
      : Runnable("net::CallOnMessageAvailable"),
        mChannel(aChannel),
        mListenerMT(std::move(aListenerMT)),
        mData(aData),
        mLen(aLen) {}

  NS_IMETHOD Run() override {
    MOZ_ASSERT(mChannel->IsOnTargetThread());

    if (mListenerMT) {
      nsresult rv;
      if (mLen < 0) {
        rv = mListenerMT->mListener->OnMessageAvailable(mListenerMT->mContext,
                                                        mData);
      } else {
        rv = mListenerMT->mListener->OnBinaryMessageAvailable(
            mListenerMT->mContext, mData);
      }
      if (NS_FAILED(rv)) {
        LOG(
            ("OnMessageAvailable or OnBinaryMessageAvailable "
             "failed with 0x%08" PRIx32,
             static_cast<uint32_t>(rv)));
      }
    }

    return NS_OK;
  }

 private:
  ~CallOnMessageAvailable() = default;

  RefPtr<WebSocketChannel> mChannel;
  RefPtr<BaseWebSocketChannel::ListenerAndContextContainer> mListenerMT;
  nsCString mData;
  int32_t mLen;
};

//-----------------------------------------------------------------------------
// CallOnStop
//-----------------------------------------------------------------------------

class CallOnStop final : public Runnable {
 public:
  CallOnStop(
      WebSocketChannel* aChannel,
      RefPtr<BaseWebSocketChannel::ListenerAndContextContainer>&& aListenerMT,
      nsresult aReason)
      : Runnable("net::CallOnStop"),
        mChannel(aChannel),
        mListenerMT(std::move(aListenerMT)),
        mReason(aReason) {}

  NS_IMETHOD Run() override {
    MOZ_ASSERT(mChannel->IsOnTargetThread());

    if (mListenerMT) {
      nsresult rv =
          mListenerMT->mListener->OnStop(mListenerMT->mContext, mReason);
      if (NS_FAILED(rv)) {
        LOG(
            ("WebSocketChannel::CallOnStop "
             "OnStop failed (%08" PRIx32 ")\n",
             static_cast<uint32_t>(rv)));
      }
    }

    return NS_OK;
  }

 private:
  ~CallOnStop() = default;

  RefPtr<WebSocketChannel> mChannel;
  RefPtr<BaseWebSocketChannel::ListenerAndContextContainer> mListenerMT;
  nsresult mReason;
};

//-----------------------------------------------------------------------------
// CallOnServerClose
//-----------------------------------------------------------------------------

class CallOnServerClose final : public Runnable {
 public:
  CallOnServerClose(
      WebSocketChannel* aChannel,
      RefPtr<BaseWebSocketChannel::ListenerAndContextContainer>&& aListenerMT,
      uint16_t aCode, nsACString& aReason)
      : Runnable("net::CallOnServerClose"),
        mChannel(aChannel),
        mListenerMT(std::move(aListenerMT)),
        mCode(aCode),
        mReason(aReason) {}

  NS_IMETHOD Run() override {
    MOZ_ASSERT(mChannel->IsOnTargetThread());

    if (mListenerMT) {
      nsresult rv = mListenerMT->mListener->OnServerClose(mListenerMT->mContext,
                                                          mCode, mReason);
      if (NS_FAILED(rv)) {
        LOG(
            ("WebSocketChannel::CallOnServerClose "
             "OnServerClose failed (%08" PRIx32 ")\n",
             static_cast<uint32_t>(rv)));
      }
    }
    return NS_OK;
  }

 private:
  ~CallOnServerClose() = default;

  RefPtr<WebSocketChannel> mChannel;
  RefPtr<BaseWebSocketChannel::ListenerAndContextContainer> mListenerMT;
  uint16_t mCode;
  nsCString mReason;
};

//-----------------------------------------------------------------------------
// CallAcknowledge
//-----------------------------------------------------------------------------

class CallAcknowledge final : public Runnable {
 public:
  CallAcknowledge(
      WebSocketChannel* aChannel,
      RefPtr<BaseWebSocketChannel::ListenerAndContextContainer>&& aListenerMT,
      uint32_t aSize)
      : Runnable("net::CallAcknowledge"),
        mChannel(aChannel),
        mListenerMT(std::move(aListenerMT)),
        mSize(aSize) {}

  NS_IMETHOD Run() override {
    MOZ_ASSERT(mChannel->IsOnTargetThread());

    LOG(("WebSocketChannel::CallAcknowledge: Size %u\n", mSize));
    if (mListenerMT) {
      nsresult rv =
          mListenerMT->mListener->OnAcknowledge(mListenerMT->mContext, mSize);
      if (NS_FAILED(rv)) {
        LOG(("WebSocketChannel::CallAcknowledge: Acknowledge failed (%08" PRIx32
             ")\n",
             static_cast<uint32_t>(rv)));
      }
    }
    return NS_OK;
  }

 private:
  ~CallAcknowledge() = default;

  RefPtr<WebSocketChannel> mChannel;
  RefPtr<BaseWebSocketChannel::ListenerAndContextContainer> mListenerMT;
  uint32_t mSize;
};

//-----------------------------------------------------------------------------
// CallOnTransportAvailable
//-----------------------------------------------------------------------------

class CallOnTransportAvailable final : public Runnable {
 public:
  CallOnTransportAvailable(WebSocketChannel* aChannel,
                           nsISocketTransport* aTransport,
                           nsIAsyncInputStream* aSocketIn,
                           nsIAsyncOutputStream* aSocketOut)
      : Runnable("net::CallOnTransportAvailble"),
        mChannel(aChannel),
        mTransport(aTransport),
        mSocketIn(aSocketIn),
        mSocketOut(aSocketOut) {}

  NS_IMETHOD Run() override {
    LOG(("WebSocketChannel::CallOnTransportAvailable %p\n", this));
    return mChannel->OnTransportAvailable(mTransport, mSocketIn, mSocketOut);
  }

 private:
  ~CallOnTransportAvailable() = default;

  RefPtr<WebSocketChannel> mChannel;
  nsCOMPtr<nsISocketTransport> mTransport;
  nsCOMPtr<nsIAsyncInputStream> mSocketIn;
  nsCOMPtr<nsIAsyncOutputStream> mSocketOut;
};

//-----------------------------------------------------------------------------
// PMCECompression
//-----------------------------------------------------------------------------

class PMCECompression {
 public:
  PMCECompression(bool aNoContextTakeover, int32_t aLocalMaxWindowBits,
                  int32_t aRemoteMaxWindowBits)
      : mActive(false),
        mNoContextTakeover(aNoContextTakeover),
        mResetDeflater(false),
        mMessageDeflated(false) {
    this->mDeflater.next_in = nullptr;
    this->mDeflater.avail_in = 0;
    this->mDeflater.total_in = 0;
    this->mDeflater.next_out = nullptr;
    this->mDeflater.avail_out = 0;
    this->mDeflater.total_out = 0;
    this->mDeflater.msg = nullptr;
    this->mDeflater.state = nullptr;
    this->mDeflater.data_type = 0;
    this->mDeflater.adler = 0;
    this->mDeflater.reserved = 0;
    this->mInflater.next_in = nullptr;
    this->mInflater.avail_in = 0;
    this->mInflater.total_in = 0;
    this->mInflater.next_out = nullptr;
    this->mInflater.avail_out = 0;
    this->mInflater.total_out = 0;
    this->mInflater.msg = nullptr;
    this->mInflater.state = nullptr;
    this->mInflater.data_type = 0;
    this->mInflater.adler = 0;
    this->mInflater.reserved = 0;
    MOZ_COUNT_CTOR(PMCECompression);

    mDeflater.zalloc = mInflater.zalloc = Z_NULL;
    mDeflater.zfree = mInflater.zfree = Z_NULL;
    mDeflater.opaque = mInflater.opaque = Z_NULL;

    if (deflateInit2(&mDeflater, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                     -aLocalMaxWindowBits, 8, Z_DEFAULT_STRATEGY) == Z_OK) {
      if (inflateInit2(&mInflater, -aRemoteMaxWindowBits) == Z_OK) {
        mActive = true;
      } else {
        deflateEnd(&mDeflater);
      }
    }
  }

  ~PMCECompression() {
    MOZ_COUNT_DTOR(PMCECompression);

    if (mActive) {
      inflateEnd(&mInflater);
      deflateEnd(&mDeflater);
    }
  }

  bool Active() { return mActive; }

  void SetMessageDeflated() {
    MOZ_ASSERT(!mMessageDeflated);
    mMessageDeflated = true;
  }
  bool IsMessageDeflated() { return mMessageDeflated; }

  bool UsingContextTakeover() { return !mNoContextTakeover; }

  nsresult Deflate(uint8_t* data, uint32_t dataLen, nsACString& _retval) {
    if (mResetDeflater || mNoContextTakeover) {
      if (deflateReset(&mDeflater) != Z_OK) {
        return NS_ERROR_UNEXPECTED;
      }
      mResetDeflater = false;
    }

    mDeflater.avail_out = kBufferLen;
    mDeflater.next_out = mBuffer;
    mDeflater.avail_in = dataLen;
    mDeflater.next_in = data;

    while (true) {
      int zerr = deflate(&mDeflater, Z_SYNC_FLUSH);

      if (zerr != Z_OK) {
        mResetDeflater = true;
        return NS_ERROR_UNEXPECTED;
      }

      uint32_t deflated = kBufferLen - mDeflater.avail_out;
      if (deflated > 0) {
        _retval.Append(reinterpret_cast<char*>(mBuffer), deflated);
      }

      mDeflater.avail_out = kBufferLen;
      mDeflater.next_out = mBuffer;

      if (mDeflater.avail_in > 0) {
        continue;  // There is still some data to deflate
      }

      if (deflated == kBufferLen) {
        continue;  // There was not enough space in the buffer
      }

      break;
    }

    if (_retval.Length() < 4) {
      MOZ_ASSERT(false, "Expected trailing not found in deflated data!");
      mResetDeflater = true;
      return NS_ERROR_UNEXPECTED;
    }

    _retval.Truncate(_retval.Length() - 4);

    return NS_OK;
  }

  nsresult Inflate(uint8_t* data, uint32_t dataLen, nsACString& _retval) {
    mMessageDeflated = false;

    Bytef trailingData[] = {0x00, 0x00, 0xFF, 0xFF};
    bool trailingDataUsed = false;

    mInflater.avail_out = kBufferLen;
    mInflater.next_out = mBuffer;
    mInflater.avail_in = dataLen;
    mInflater.next_in = data;

    while (true) {
      int zerr = inflate(&mInflater, Z_NO_FLUSH);

      if (zerr == Z_STREAM_END) {
        Bytef* saveNextIn = mInflater.next_in;
        uint32_t saveAvailIn = mInflater.avail_in;
        Bytef* saveNextOut = mInflater.next_out;
        uint32_t saveAvailOut = mInflater.avail_out;

        inflateReset(&mInflater);

        mInflater.next_in = saveNextIn;
        mInflater.avail_in = saveAvailIn;
        mInflater.next_out = saveNextOut;
        mInflater.avail_out = saveAvailOut;
      } else if (zerr != Z_OK && zerr != Z_BUF_ERROR) {
        return NS_ERROR_INVALID_CONTENT_ENCODING;
      }

      uint32_t inflated = kBufferLen - mInflater.avail_out;
      if (inflated > 0) {
        if (!_retval.Append(reinterpret_cast<char*>(mBuffer), inflated,
                            fallible)) {
          return NS_ERROR_OUT_OF_MEMORY;
        }
      }

      mInflater.avail_out = kBufferLen;
      mInflater.next_out = mBuffer;

      if (mInflater.avail_in > 0) {
        continue;  // There is still some data to inflate
      }

      if (inflated == kBufferLen) {
        continue;  // There was not enough space in the buffer
      }

      if (!trailingDataUsed) {
        trailingDataUsed = true;
        mInflater.avail_in = sizeof(trailingData);
        mInflater.next_in = trailingData;
        continue;
      }

      return NS_OK;
    }
  }

 private:
  bool mActive;
  bool mNoContextTakeover;
  bool mResetDeflater;
  bool mMessageDeflated;
  z_stream mDeflater{};
  z_stream mInflater{};
  const static uint32_t kBufferLen = 4096;
  uint8_t mBuffer[kBufferLen]{0};
};

//-----------------------------------------------------------------------------
// OutboundMessage
//-----------------------------------------------------------------------------

enum WsMsgType {
  kMsgTypeString = 0,
  kMsgTypeBinaryString,
  kMsgTypeStream,
  kMsgTypePing,
  kMsgTypePong,
  kMsgTypeFin
};

static const char* msgNames[] = {"text", "binaryString", "binaryStream",
                                 "ping", "pong",         "close"};

class OutboundMessage {
 public:
  OutboundMessage(WsMsgType type, const nsACString& str)
      : mMsg(mozilla::AsVariant(pString(str))),
        mMsgType(type),
        mDeflated(false) {
    MOZ_COUNT_CTOR(OutboundMessage);
  }

  OutboundMessage(nsIInputStream* stream, uint32_t length)
      : mMsg(mozilla::AsVariant(StreamWithLength(stream, length))),
        mMsgType(kMsgTypeStream),
        mDeflated(false) {
    MOZ_COUNT_CTOR(OutboundMessage);
  }

  ~OutboundMessage() {
    MOZ_COUNT_DTOR(OutboundMessage);
    switch (mMsgType) {
      case kMsgTypeString:
      case kMsgTypeBinaryString:
      case kMsgTypePing:
      case kMsgTypePong:
        break;
      case kMsgTypeStream:
        // for now this only gets hit if msg deleted w/o being sent
        if (mMsg.as<StreamWithLength>().mStream) {
          mMsg.as<StreamWithLength>().mStream->Close();
        }
        break;
      case kMsgTypeFin:
        break;  // do-nothing: avoid compiler warning
    }
  }

  WsMsgType GetMsgType() const { return mMsgType; }
  int32_t Length() {
    if (mMsg.is<pString>()) {
      return mMsg.as<pString>().mValue.Length();
    }

    return mMsg.as<StreamWithLength>().mLength;
  }
  int32_t OrigLength() {
    if (mMsg.is<pString>()) {
      pString& ref = mMsg.as<pString>();
      return mDeflated ? ref.mOrigValue.Length() : ref.mValue.Length();
    }

    return mMsg.as<StreamWithLength>().mLength;
  }

  uint8_t* BeginWriting() {
    MOZ_ASSERT(mMsgType != kMsgTypeStream,
               "Stream should have been converted to string by now");
    if (!mMsg.as<pString>().mValue.IsVoid()) {
      return (uint8_t*)mMsg.as<pString>().mValue.BeginWriting();
    }
    return nullptr;
  }

  uint8_t* BeginReading() {
    MOZ_ASSERT(mMsgType != kMsgTypeStream,
               "Stream should have been converted to string by now");
    if (!mMsg.as<pString>().mValue.IsVoid()) {
      return (uint8_t*)mMsg.as<pString>().mValue.BeginReading();
    }
    return nullptr;
  }

  uint8_t* BeginOrigReading() {
    MOZ_ASSERT(mMsgType != kMsgTypeStream,
               "Stream should have been converted to string by now");
    if (!mDeflated) return BeginReading();
    if (!mMsg.as<pString>().mOrigValue.IsVoid()) {
      return (uint8_t*)mMsg.as<pString>().mOrigValue.BeginReading();
    }
    return nullptr;
  }

  nsresult ConvertStreamToString() {
    MOZ_ASSERT(mMsgType == kMsgTypeStream, "Not a stream!");
    nsAutoCString temp;
    {
      StreamWithLength& ref = mMsg.as<StreamWithLength>();
      nsresult rv = NS_ReadInputStreamToString(ref.mStream, temp, ref.mLength);

      NS_ENSURE_SUCCESS(rv, rv);
      if (temp.Length() != ref.mLength) {
        return NS_ERROR_UNEXPECTED;
      }
      ref.mStream->Close();
    }

    mMsg = mozilla::AsVariant(pString(temp));
    mMsgType = kMsgTypeBinaryString;

    return NS_OK;
  }

  bool DeflatePayload(PMCECompression* aCompressor) {
    MOZ_ASSERT(mMsgType != kMsgTypeStream,
               "Stream should have been converted to string by now");
    MOZ_ASSERT(!mDeflated);

    nsresult rv;
    pString& ref = mMsg.as<pString>();
    if (ref.mValue.Length() == 0) {
      // Empty message
      return false;
    }

    nsAutoCString temp;
    rv = aCompressor->Deflate(BeginReading(), ref.mValue.Length(), temp);
    if (NS_FAILED(rv)) {
      LOG(
          ("WebSocketChannel::OutboundMessage: Deflating payload failed "
           "[rv=0x%08" PRIx32 "]\n",
           static_cast<uint32_t>(rv)));
      return false;
    }

    if (!aCompressor->UsingContextTakeover() &&
        temp.Length() > ref.mValue.Length()) {
      // When "<local>_no_context_takeover" was negotiated, do not send deflated
      // payload if it's larger that the original one. OTOH, it makes sense
      // to send the larger deflated payload when the sliding window is not
      // reset between messages because if we would skip some deflated block
      // we would need to empty the sliding window which could affect the
      // compression of the subsequent messages.
      LOG(
          ("WebSocketChannel::OutboundMessage: Not deflating message since the "
           "deflated payload is larger than the original one [deflated=%zd, "
           "original=%zd]",
           temp.Length(), ref.mValue.Length()));
      return false;
    }

    mDeflated = true;
    mMsg.as<pString>().mOrigValue = std::move(mMsg.as<pString>().mValue);
    mMsg.as<pString>().mValue = std::move(temp);
    return true;
  }

 private:
  struct pString {
    nsCString mValue;
    nsCString mOrigValue;
    explicit pString(const nsACString& value)
        : mValue(value), mOrigValue(VoidCString()) {}
  };
  struct StreamWithLength {
    nsCOMPtr<nsIInputStream> mStream;
    uint32_t mLength;
    explicit StreamWithLength(nsIInputStream* stream, uint32_t Length)
        : mStream(stream), mLength(Length) {}
  };
  mozilla::Variant<pString, StreamWithLength> mMsg;
  WsMsgType mMsgType;
  bool mDeflated;
};

//-----------------------------------------------------------------------------
// OutboundEnqueuer
//-----------------------------------------------------------------------------

class OutboundEnqueuer final : public Runnable {
 public:
  OutboundEnqueuer(WebSocketChannel* aChannel, OutboundMessage* aMsg)
      : Runnable("OutboundEnquerer"), mChannel(aChannel), mMessage(aMsg) {}

  NS_IMETHOD Run() override {
    mChannel->EnqueueOutgoingMessage(mChannel->mOutgoingMessages, mMessage);
    return NS_OK;
  }

 private:
  ~OutboundEnqueuer() = default;

  RefPtr<WebSocketChannel> mChannel;
  OutboundMessage* mMessage;
};

//-----------------------------------------------------------------------------
// WebSocketChannel
//-----------------------------------------------------------------------------

WebSocketChannel::WebSocketChannel()
    : mPort(0),
      mCloseTimeout(20000),
      mOpenTimeout(20000),
      mConnecting(NOT_CONNECTING),
      mMaxConcurrentConnections(200),
      mInnerWindowID(0),
      mGotUpgradeOK(0),
      mRecvdHttpUpgradeTransport(0),
      mPingOutstanding(0),
      mReleaseOnTransmit(0),
      mDataStarted(false),
      mRequestedClose(false),
      mClientClosed(false),
      mServerClosed(false),
      mStopped(false),
      mCalledOnStop(false),
      mTCPClosed(false),
      mOpenedHttpChannel(false),
      mIncrementedSessionCount(false),
      mDecrementedSessionCount(false),
      mMaxMessageSize(INT32_MAX),
      mStopOnClose(NS_OK),
      mServerCloseCode(CLOSE_ABNORMAL),
      mScriptCloseCode(0),
      mFragmentOpcode(nsIWebSocketFrame::OPCODE_CONTINUATION),
      mFragmentAccumulator(0),
      mBuffered(0),
      mBufferSize(kIncomingBufferInitialSize),
      mCurrentOut(nullptr),
      mCurrentOutSent(0),
      mHdrOutToSend(0),
      mHdrOut(nullptr),
      mCompressorMutex("WebSocketChannel::mCompressorMutex"),
      mDynamicOutputSize(0),
      mDynamicOutput(nullptr),
      mPrivateBrowsing(false),
      mConnectionLogService(nullptr),
      mMutex("WebSocketChannel::mMutex") {
  MOZ_ASSERT(NS_IsMainThread(), "not main thread");

  LOG(("WebSocketChannel::WebSocketChannel() %p\n", this));

  nsWSAdmissionManager::Init();

  mFramePtr = mBuffer = static_cast<uint8_t*>(moz_xmalloc(mBufferSize));

  nsresult rv;
  mConnectionLogService = mozilla::components::Dashboard::Service(&rv);
  if (NS_FAILED(rv)) LOG(("Failed to initiate dashboard service."));

  mService = WebSocketEventService::GetOrCreate();
}

WebSocketChannel::~WebSocketChannel() {
  LOG(("WebSocketChannel::~WebSocketChannel() %p\n", this));

  if (mWasOpened) {
    MOZ_ASSERT(mCalledOnStop, "WebSocket was opened but OnStop was not called");
    MOZ_ASSERT(mStopped, "WebSocket was opened but never stopped");
  }
  MOZ_ASSERT(!mCancelable, "DNS/Proxy Request still alive at destruction");
  MOZ_ASSERT(!mConnecting, "Should not be connecting in destructor");

  free(mBuffer);
  free(mDynamicOutput);
  delete mCurrentOut;

  while ((mCurrentOut = mOutgoingPingMessages.PopFront())) {
    delete mCurrentOut;
  }
  while ((mCurrentOut = mOutgoingPongMessages.PopFront())) {
    delete mCurrentOut;
  }
  while ((mCurrentOut = mOutgoingMessages.PopFront())) {
    delete mCurrentOut;
  }

  mListenerMT = nullptr;

  NS_ReleaseOnMainThread("WebSocketChannel::mService", mService.forget());
}

NS_IMETHODIMP
WebSocketChannel::Observe(nsISupports* subject, const char* topic,
                          const char16_t* data) {
  LOG(("WebSocketChannel::Observe [topic=\"%s\"]\n", topic));

  if (strcmp(topic, NS_NETWORK_LINK_TOPIC) == 0) {
    nsCString converted = NS_ConvertUTF16toUTF8(data);
    const char* state = converted.get();

    if (strcmp(state, NS_NETWORK_LINK_DATA_CHANGED) == 0) {
      LOG(("WebSocket: received network CHANGED event"));

      if (!mIOThread) {
        // there has not been an asyncopen yet on the object and then we need
        // no ping.
        LOG(("WebSocket: early object, no ping needed"));
      } else {
        mIOThread->Dispatch(
            NewRunnableMethod("net::WebSocketChannel::OnNetworkChanged", this,
                              &WebSocketChannel::OnNetworkChanged),
            NS_DISPATCH_NORMAL);
      }
    }
  }

  return NS_OK;
}

nsresult WebSocketChannel::OnNetworkChanged() {
  if (!mDataStarted) {
    LOG(("WebSocket: data not started yet, no ping needed"));
    return NS_OK;
  }

  MOZ_ASSERT(mIOThread->IsOnCurrentThread(), "not on right thread");

  LOG(("WebSocketChannel::OnNetworkChanged() - on socket thread %p", this));

  if (mPingOutstanding) {
    // If there's an outstanding ping that's expected to get a pong back
    // we let that do its thing.
    LOG(("WebSocket: pong already pending"));
    return NS_OK;
  }

  if (mPingForced) {
    // avoid more than one
    LOG(("WebSocket: forced ping timer already fired"));
    return NS_OK;
  }

  LOG(("nsWebSocketChannel:: Generating Ping as network changed\n"));

  if (!mPingTimer) {
    // The ping timer is only conditionally running already. If it wasn't
    // already created do it here.
    mPingTimer = NS_NewTimer();
    if (!mPingTimer) {
      LOG(("WebSocket: unable to create ping timer!"));
      NS_WARNING("unable to create ping timer!");
      return NS_ERROR_OUT_OF_MEMORY;
    }
  }
  // Trigger the ping timeout asap to fire off a new ping. Wait just
  // a little bit to better avoid multi-triggers.
  mPingForced = true;
  mPingTimer->InitWithCallback(this, 200, nsITimer::TYPE_ONE_SHOT);

  return NS_OK;
}

void WebSocketChannel::Shutdown() { nsWSAdmissionManager::Shutdown(); }

void WebSocketChannel::GetEffectiveURL(nsAString& aEffectiveURL) const {
  aEffectiveURL = mEffectiveURL;
}

bool WebSocketChannel::IsEncrypted() const { return mEncrypted; }

void WebSocketChannel::BeginOpen(bool aCalledFromAdmissionManager) {
  MOZ_ASSERT(NS_IsMainThread(), "not main thread");

  LOG(("WebSocketChannel::BeginOpen() %p\n", this));

  // Important that we set CONNECTING_IN_PROGRESS before any call to
  // AbortSession here: ensures that any remaining queued connection(s) are
  // scheduled in OnStopSession
  LOG(("Websocket: changing state to CONNECTING_IN_PROGRESS"));
  mConnecting = CONNECTING_IN_PROGRESS;

  if (aCalledFromAdmissionManager) {
    // When called from nsWSAdmissionManager post an event to avoid potential
    // re-entering of nsWSAdmissionManager and its lock.
    NS_DispatchToMainThread(
        NewRunnableMethod("net::WebSocketChannel::BeginOpenInternal", this,
                          &WebSocketChannel::BeginOpenInternal),
        NS_DISPATCH_NORMAL);
  } else {
    BeginOpenInternal();
  }
}

// MainThread
void WebSocketChannel::BeginOpenInternal() {
  LOG(("WebSocketChannel::BeginOpenInternal() %p\n", this));
  MOZ_ASSERT(NS_IsMainThread(), "not main thread");

  nsresult rv;

  if (mRedirectCallback) {
    LOG(("WebSocketChannel::BeginOpenInternal: Resuming Redirect\n"));
    rv = mRedirectCallback->OnRedirectVerifyCallback(NS_OK);
    mRedirectCallback = nullptr;
    return;
  }

  nsCOMPtr<nsIChannel> localChannel = do_QueryInterface(mChannel, &rv);
  if (NS_FAILED(rv)) {
    LOG(("WebSocketChannel::BeginOpenInternal: cannot async open\n"));
    AbortSession(NS_ERROR_UNEXPECTED);
    return;
  }

  rv = localChannel->AsyncOpen(this);

  if (NS_FAILED(rv)) {
    LOG(("WebSocketChannel::BeginOpenInternal: cannot async open\n"));
    AbortSession(NS_ERROR_WEBSOCKET_CONNECTION_REFUSED);
    return;
  }
  mOpenedHttpChannel = true;

  rv = NS_NewTimerWithCallback(getter_AddRefs(mOpenTimer), this, mOpenTimeout,
                               nsITimer::TYPE_ONE_SHOT);
  if (NS_FAILED(rv)) {
    LOG(
        ("WebSocketChannel::BeginOpenInternal: cannot initialize open "
         "timer\n"));
    AbortSession(NS_ERROR_UNEXPECTED);
    return;
  }
}

bool WebSocketChannel::IsPersistentFramePtr() {
  return (mFramePtr >= mBuffer && mFramePtr <= mBuffer + mBufferSize);
}

// Extends the internal buffer by count and returns the total
// amount of data available for read
//
// Accumulated fragment size is passed in instead of using the member
// variable beacuse when transitioning from the stack to the persistent
// read buffer we want to explicitly include them in the buffer instead
// of as already existing data.
bool WebSocketChannel::UpdateReadBuffer(uint8_t* buffer, uint32_t count,
                                        uint32_t accumulatedFragments,
                                        uint32_t* available) {
  LOG(("WebSocketChannel::UpdateReadBuffer() %p [%p %u]\n", this, buffer,
       count));

  if (!mBuffered) mFramePtr = mBuffer;

  MOZ_ASSERT(IsPersistentFramePtr(), "update read buffer bad mFramePtr");
  MOZ_ASSERT(mFramePtr - accumulatedFragments >= mBuffer,
             "reserved FramePtr bad");

  if (mBuffered + count <= mBufferSize) {
    // append to existing buffer
    LOG(("WebSocketChannel: update read buffer absorbed %u\n", count));
  } else if (mBuffered + count - (mFramePtr - accumulatedFragments - mBuffer) <=
             mBufferSize) {
    // make room in existing buffer by shifting unused data to start
    mBuffered -= (mFramePtr - mBuffer - accumulatedFragments);
    LOG(("WebSocketChannel: update read buffer shifted %u\n", mBuffered));
    ::memmove(mBuffer, mFramePtr - accumulatedFragments, mBuffered);
    mFramePtr = mBuffer + accumulatedFragments;
  } else {
    // existing buffer is not sufficient, extend it
    uint32_t newBufferSize = mBufferSize;
    newBufferSize += count + 8192 + mBufferSize / 3;
    ptrdiff_t frameIndex = mFramePtr - mBuffer;
    LOG(("WebSocketChannel: update read buffer extended to %u\n",
         newBufferSize));
    uint8_t* newBuffer = (uint8_t*)realloc(mBuffer, newBufferSize);
    if (!newBuffer) {
      // Reallocation failed.
      return false;
    }
    mBuffer = newBuffer;
    mBufferSize = newBufferSize;

    // mBuffer was reallocated, so we need to update mFramePtr
    mFramePtr = mBuffer + frameIndex;
  }

  ::memmove(mBuffer + mBuffered, buffer, count);
  mBuffered += count;

  if (available) *available = mBuffered - (mFramePtr - mBuffer);

  return true;
}

already_AddRefed<BaseWebSocketChannel::ListenerAndContextContainer>
WebSocketChannel::GetListenerMT() {
  MutexAutoLock lock(mMutex);
  return do_AddRef(mStopped ? nullptr : mListenerMT.get());
}

already_AddRefed<BaseWebSocketChannel::ListenerAndContextContainer>
WebSocketChannel::TakeListenerMT() {
  MutexAutoLock lock(mMutex);
  return mListenerMT.forget();
}

nsresult WebSocketChannel::ProcessInput(uint8_t* buffer, uint32_t count) {
  LOG(("WebSocketChannel::ProcessInput %p [%d %d]\n", this, count, mBuffered));
  MOZ_ASSERT(mIOThread->IsOnCurrentThread(), "not on right thread");

  nsresult rv;

  // The purpose of ping/pong is to actively probe the peer so that an
  // unreachable peer is not mistaken for a period of idleness. This
  // implementation accepts any application level read activity as a sign of
  // life, it does not necessarily have to be a pong.
  ResetPingTimer();

  uint32_t avail;

  if (!mBuffered) {
    // Most of the time we can process right off the stack buffer without
    // having to accumulate anything
    mFramePtr = buffer;
    avail = count;
  } else {
    if (!UpdateReadBuffer(buffer, count, mFragmentAccumulator, &avail)) {
      return NS_ERROR_FILE_TOO_BIG;
    }
  }

  uint8_t* payload;
  uint32_t totalAvail = avail;

  while (avail >= 2) {
    int64_t payloadLength64 = mFramePtr[1] & kPayloadLengthBitsMask;
    uint8_t finBit = mFramePtr[0] & kFinalFragBit;
    uint8_t rsvBits = mFramePtr[0] & kRsvBitsMask;
    uint8_t rsvBit1 = mFramePtr[0] & kRsv1Bit;
    uint8_t rsvBit2 = mFramePtr[0] & kRsv2Bit;
    uint8_t rsvBit3 = mFramePtr[0] & kRsv3Bit;
    uint8_t opcode = mFramePtr[0] & kOpcodeBitsMask;
    uint8_t maskBit = mFramePtr[1] & kMaskBit;
    uint32_t mask = 0;

    uint32_t framingLength = 2;
    if (maskBit) framingLength += 4;

    if (payloadLength64 < 126) {
      if (avail < framingLength) break;
    } else if (payloadLength64 == 126) {
      // 16 bit length field
      framingLength += 2;
      if (avail < framingLength) break;

      payloadLength64 = mFramePtr[2] << 8 | mFramePtr[3];

      if (payloadLength64 < 126) {
        // Section 5.2 says that the minimal number of bytes MUST
        // be used to encode the length in all cases
        LOG(("WebSocketChannel:: non-minimal-encoded payload length"));
        return NS_ERROR_ILLEGAL_VALUE;
      }

    } else {
      // 64 bit length
      framingLength += 8;
      if (avail < framingLength) break;

      if (mFramePtr[2] & 0x80) {
        // Section 4.2 says that the most significant bit MUST be
        // 0. (i.e. this is really a 63 bit value)
        LOG(("WebSocketChannel:: high bit of 64 bit length set"));
        return NS_ERROR_ILLEGAL_VALUE;
      }

      // copy this in case it is unaligned
      payloadLength64 = NetworkEndian::readInt64(mFramePtr + 2);

      if (payloadLength64 <= 0xffff) {
        // Section 5.2 says that the minimal number of bytes MUST
        // be used to encode the length in all cases
        LOG(("WebSocketChannel:: non-minimal-encoded payload length"));
        return NS_ERROR_ILLEGAL_VALUE;
      }
    }

    payload = mFramePtr + framingLength;
    avail -= framingLength;

    LOG(("WebSocketChannel::ProcessInput: payload %" PRId64 " avail %" PRIu32
         "\n",
         payloadLength64, avail));

    CheckedInt<int64_t> payloadLengthChecked(payloadLength64);
    payloadLengthChecked += mFragmentAccumulator;
    if (!payloadLengthChecked.isValid() ||
        payloadLengthChecked.value() > mMaxMessageSize) {
      return NS_ERROR_FILE_TOO_BIG;
    }

    uint32_t payloadLength = static_cast<uint32_t>(payloadLength64);

    if (avail < payloadLength) break;

    LOG(("WebSocketChannel::ProcessInput: Frame accumulated - opcode %d\n",
         opcode));

    if (!maskBit && mIsServerSide) {
      LOG(
          ("WebSocketChannel::ProcessInput: unmasked frame received "
           "from client\n"));
      return NS_ERROR_ILLEGAL_VALUE;
    }

    if (maskBit) {
      if (!mIsServerSide) {
        // The server should not be allowed to send masked frames to clients.
        // But we've been allowing it for some time, so this should be
        // deprecated with care.
        LOG(("WebSocketChannel:: Client RECEIVING masked frame."));
      }

      mask = NetworkEndian::readUint32(payload - 4);
    }

    if (mask) {
      ApplyMask(mask, payload, payloadLength);
    } else if (mIsServerSide) {
      LOG(
          ("WebSocketChannel::ProcessInput: masked frame with mask 0 received"
           "from client\n"));
      return NS_ERROR_ILLEGAL_VALUE;
    }

    // Control codes are required to have the fin bit set
    if (!finBit && (opcode & kControlFrameMask)) {
      LOG(("WebSocketChannel:: fragmented control frame code %d\n", opcode));
      return NS_ERROR_ILLEGAL_VALUE;
    }

    if (rsvBits) {
      // PMCE sets RSV1 bit in the first fragment when the non-control frame
      // is deflated
      MutexAutoLock lock(mCompressorMutex);
      if (mPMCECompressor && rsvBits == kRsv1Bit && mFragmentAccumulator == 0 &&
          !(opcode & kControlFrameMask)) {
        mPMCECompressor->SetMessageDeflated();
        LOG(("WebSocketChannel::ProcessInput: received deflated frame\n"));
      } else {
        LOG(("WebSocketChannel::ProcessInput: unexpected reserved bits %x\n",
             rsvBits));
        return NS_ERROR_ILLEGAL_VALUE;
      }
    }

    if (!finBit || opcode == nsIWebSocketFrame::OPCODE_CONTINUATION) {
      // This is part of a fragment response

      // Only the first frame has a non zero op code: Make sure we don't see a
      // first frame while some old fragments are open
      if ((mFragmentAccumulator != 0) &&
          (opcode != nsIWebSocketFrame::OPCODE_CONTINUATION)) {
        LOG(("WebSocketChannel:: nested fragments\n"));
        return NS_ERROR_ILLEGAL_VALUE;
      }

      LOG(("WebSocketChannel:: Accumulating Fragment %" PRIu32 "\n",
           payloadLength));

      if (opcode == nsIWebSocketFrame::OPCODE_CONTINUATION) {
        // Make sure this continuation fragment isn't the first fragment
        if (mFragmentOpcode == nsIWebSocketFrame::OPCODE_CONTINUATION) {
          LOG(("WebSocketHeandler:: continuation code in first fragment\n"));
          return NS_ERROR_ILLEGAL_VALUE;
        }

        // For frag > 1 move the data body back on top of the headers
        // so we have contiguous stream of data
        MOZ_ASSERT(mFramePtr + framingLength == payload,
                   "payload offset from frameptr wrong");
        ::memmove(mFramePtr, payload, avail);
        payload = mFramePtr;
        if (mBuffered) mBuffered -= framingLength;
      } else {
        mFragmentOpcode = opcode;
      }

      if (finBit) {
        LOG(("WebSocketChannel:: Finalizing Fragment\n"));
        payload -= mFragmentAccumulator;
        payloadLength += mFragmentAccumulator;
        avail += mFragmentAccumulator;
        mFragmentAccumulator = 0;
        opcode = mFragmentOpcode;
        // reset to detect if next message illegally starts with continuation
        mFragmentOpcode = nsIWebSocketFrame::OPCODE_CONTINUATION;
      } else {
        opcode = nsIWebSocketFrame::OPCODE_CONTINUATION;
        mFragmentAccumulator += payloadLength;
      }
    } else if (mFragmentAccumulator != 0 && !(opcode & kControlFrameMask)) {
      // This frame is not part of a fragment sequence but we
      // have an open fragment.. it must be a control code or else
      // we have a problem
      LOG(("WebSocketChannel:: illegal fragment sequence\n"));
      return NS_ERROR_ILLEGAL_VALUE;
    }

    if (mServerClosed) {
      LOG(("WebSocketChannel:: ignoring read frame code %d after close\n",
           opcode));
      // nop
    } else if (mStopped) {
      LOG(("WebSocketChannel:: ignoring read frame code %d after completion\n",
           opcode));
    } else if (opcode == nsIWebSocketFrame::OPCODE_TEXT) {
      if (RefPtr<BaseWebSocketChannel::ListenerAndContextContainer> listener =
              GetListenerMT()) {
        nsCString utf8Data;
        {
          MutexAutoLock lock(mCompressorMutex);
          bool isDeflated =
              mPMCECompressor && mPMCECompressor->IsMessageDeflated();
          LOG(("WebSocketChannel:: %stext frame received\n",
               isDeflated ? "deflated " : ""));

          if (isDeflated) {
            rv = mPMCECompressor->Inflate(payload, payloadLength, utf8Data);
            if (NS_FAILED(rv)) {
              return rv;
            }
            LOG(
                ("WebSocketChannel:: message successfully inflated "
                 "[origLength=%d, newLength=%zd]\n",
                 payloadLength, utf8Data.Length()));
          } else {
            if (!utf8Data.Assign((const char*)payload, payloadLength,
                                 mozilla::fallible)) {
              return NS_ERROR_OUT_OF_MEMORY;
            }
          }
        }

        // Section 8.1 says to fail connection if invalid utf-8 in text message
        if (!IsUtf8(utf8Data)) {
          LOG(("WebSocketChannel:: text frame invalid utf-8\n"));
          return NS_ERROR_CANNOT_CONVERT_DATA;
        }

        RefPtr<WebSocketFrame> frame = mService->CreateFrameIfNeeded(
            finBit, rsvBit1, rsvBit2, rsvBit3, opcode, maskBit, mask, utf8Data);

        if (frame) {
          mService->FrameReceived(mSerial, mInnerWindowID, mHttpChannelId,
                                  frame.forget());
        }

        if (nsCOMPtr<nsIEventTarget> target = GetTargetThread()) {
          target->Dispatch(new CallOnMessageAvailable(this, std::move(listener),
                                                      utf8Data, -1),
                           NS_DISPATCH_NORMAL);
        } else {
          return NS_ERROR_UNEXPECTED;
        }
        if (mConnectionLogService && !mPrivateBrowsing) {
          mConnectionLogService->NewMsgReceived(mHost, mSerial, count);
          LOG(("Added new msg received for %s", mHost.get()));
        }
      }
    } else if (opcode & kControlFrameMask) {
      // control frames
      if (payloadLength > 125) {
        LOG(("WebSocketChannel:: bad control frame code %d length %d\n", opcode,
             payloadLength));
        return NS_ERROR_ILLEGAL_VALUE;
      }

      RefPtr<WebSocketFrame> frame = mService->CreateFrameIfNeeded(
          finBit, rsvBit1, rsvBit2, rsvBit3, opcode, maskBit, mask, payload,
          payloadLength);

      if (opcode == nsIWebSocketFrame::OPCODE_CLOSE) {
        LOG(("WebSocketChannel:: close received\n"));
        mServerClosed = true;

        mServerCloseCode = CLOSE_NO_STATUS;
        if (payloadLength >= 2) {
          mServerCloseCode = NetworkEndian::readUint16(payload);
          LOG(("WebSocketChannel:: close recvd code %u\n", mServerCloseCode));
          uint16_t msglen = static_cast<uint16_t>(payloadLength - 2);
          if (msglen > 0) {
            mServerCloseReason.SetLength(msglen);
            memcpy(mServerCloseReason.BeginWriting(), (const char*)payload + 2,
                   msglen);

            // section 8.1 says to replace received non utf-8 sequences
            // (which are non-conformant to send) with u+fffd,
            // but secteam feels that silently rewriting messages is
            // inappropriate - so we will fail the connection instead.
            if (!IsUtf8(mServerCloseReason)) {
              LOG(("WebSocketChannel:: close frame invalid utf-8\n"));
              return NS_ERROR_CANNOT_CONVERT_DATA;
            }

            LOG(("WebSocketChannel:: close msg %s\n",
                 mServerCloseReason.get()));
          }
        }

        if (mCloseTimer) {
          mCloseTimer->Cancel();
          mCloseTimer = nullptr;
        }

        if (frame) {
          // We send the frame immediately becuase we want to have it dispatched
          // before the CallOnServerClose.
          mService->FrameReceived(mSerial, mInnerWindowID, mHttpChannelId,
                                  frame.forget());
          frame = nullptr;
        }

        if (RefPtr<BaseWebSocketChannel::ListenerAndContextContainer> listener =
                GetListenerMT()) {
          if (nsCOMPtr<nsIEventTarget> target = GetTargetThread()) {
            target->Dispatch(
                new CallOnServerClose(this, std::move(listener),
                                      mServerCloseCode, mServerCloseReason),
                NS_DISPATCH_NORMAL);
          } else {
            return NS_ERROR_UNEXPECTED;
          }
        }

        if (mClientClosed) ReleaseSession();
      } else if (opcode == nsIWebSocketFrame::OPCODE_PING) {
        LOG(("WebSocketChannel:: ping received\n"));
        GeneratePong(payload, payloadLength);
      } else if (opcode == nsIWebSocketFrame::OPCODE_PONG) {
        // opcode OPCODE_PONG: the mere act of receiving the packet is all we
        // need to do for the pong to trigger the activity timers
        LOG(("WebSocketChannel:: pong received\n"));
      } else {
        /* unknown control frame opcode */
        LOG(("WebSocketChannel:: unknown control op code %d\n", opcode));
        return NS_ERROR_ILLEGAL_VALUE;
      }

      if (mFragmentAccumulator) {
        // Remove the control frame from the stream so we have a contiguous
        // data buffer of reassembled fragments
        LOG(("WebSocketChannel:: Removing Control From Read buffer\n"));
        MOZ_ASSERT(mFramePtr + framingLength == payload,
                   "payload offset from frameptr wrong");
        ::memmove(mFramePtr, payload + payloadLength, avail - payloadLength);
        payload = mFramePtr;
        avail -= payloadLength;
        if (mBuffered) mBuffered -= framingLength + payloadLength;
        payloadLength = 0;
      }

      if (frame) {
        mService->FrameReceived(mSerial, mInnerWindowID, mHttpChannelId,
                                frame.forget());
      }
    } else if (opcode == nsIWebSocketFrame::OPCODE_BINARY) {
      if (RefPtr<BaseWebSocketChannel::ListenerAndContextContainer> listener =
              GetListenerMT()) {
        nsCString binaryData;
        {
          MutexAutoLock lock(mCompressorMutex);
          bool isDeflated =
              mPMCECompressor && mPMCECompressor->IsMessageDeflated();
          LOG(("WebSocketChannel:: %sbinary frame received\n",
               isDeflated ? "deflated " : ""));

          if (isDeflated) {
            rv = mPMCECompressor->Inflate(payload, payloadLength, binaryData);
            if (NS_FAILED(rv)) {
              return rv;
            }
            LOG(
                ("WebSocketChannel:: message successfully inflated "
                 "[origLength=%d, newLength=%zd]\n",
                 payloadLength, binaryData.Length()));
          } else {
            if (!binaryData.Assign((const char*)payload, payloadLength,
                                   mozilla::fallible)) {
              return NS_ERROR_OUT_OF_MEMORY;
            }
          }
        }

        RefPtr<WebSocketFrame> frame =
            mService->CreateFrameIfNeeded(finBit, rsvBit1, rsvBit2, rsvBit3,
                                          opcode, maskBit, mask, binaryData);
        if (frame) {
          mService->FrameReceived(mSerial, mInnerWindowID, mHttpChannelId,
                                  frame.forget());
        }

        if (nsCOMPtr<nsIEventTarget> target = GetTargetThread()) {
          target->Dispatch(
              new CallOnMessageAvailable(this, std::move(listener), binaryData,
                                         binaryData.Length()),
              NS_DISPATCH_NORMAL);
        } else {
          return NS_ERROR_UNEXPECTED;
        }
        // To add the header to 'Networking Dashboard' log
        if (mConnectionLogService && !mPrivateBrowsing) {
          mConnectionLogService->NewMsgReceived(mHost, mSerial, count);
          LOG(("Added new received msg for %s", mHost.get()));
        }
      }
    } else if (opcode != nsIWebSocketFrame::OPCODE_CONTINUATION) {
      /* unknown opcode */
      LOG(("WebSocketChannel:: unknown op code %d\n", opcode));
      return NS_ERROR_ILLEGAL_VALUE;
    }

    mFramePtr = payload + payloadLength;
    avail -= payloadLength;
    totalAvail = avail;
  }

  // Adjust the stateful buffer. If we were operating off the stack and
  // now have a partial message then transition to the buffer, or if
  // we were working off the buffer but no longer have any active state
  // then transition to the stack
  if (!IsPersistentFramePtr()) {
    mBuffered = 0;

    if (mFragmentAccumulator) {
      LOG(("WebSocketChannel:: Setup Buffer due to fragment"));

      if (!UpdateReadBuffer(mFramePtr - mFragmentAccumulator,
                            totalAvail + mFragmentAccumulator, 0, nullptr)) {
        return NS_ERROR_FILE_TOO_BIG;
      }

      // UpdateReadBuffer will reset the frameptr to the beginning
      // of new saved state, so we need to skip past processed framgents
      mFramePtr += mFragmentAccumulator;
    } else if (totalAvail) {
      LOG(("WebSocketChannel:: Setup Buffer due to partial frame"));
      if (!UpdateReadBuffer(mFramePtr, totalAvail, 0, nullptr)) {
        return NS_ERROR_FILE_TOO_BIG;
      }
    }
  } else if (!mFragmentAccumulator && !totalAvail) {
    // If we were working off a saved buffer state and there is no partial
    // frame or fragment in process, then revert to stack behavior
    LOG(("WebSocketChannel:: Internal buffering not needed anymore"));
    mBuffered = 0;

    // release memory if we've been processing a large message
    if (mBufferSize > kIncomingBufferStableSize) {
      mBufferSize = kIncomingBufferStableSize;
      free(mBuffer);
      mBuffer = (uint8_t*)moz_xmalloc(mBufferSize);
    }
  }
  return NS_OK;
}

/* static */
void WebSocketChannel::ApplyMask(uint32_t mask, uint8_t* data, uint64_t len) {
  if (!data || len == 0) return;

  // Optimally we want to apply the mask 32 bits at a time,
  // but the buffer might not be alligned. So we first deal with
  // 0 to 3 bytes of preamble individually

  while (len && (reinterpret_cast<uintptr_t>(data) & 3)) {
    *data ^= mask >> 24;
    mask = RotateLeft(mask, 8);
    data++;
    len--;
  }

  // perform mask on full words of data

  uint32_t* iData = (uint32_t*)data;
  uint32_t* end = iData + (len / 4);
  NetworkEndian::writeUint32(&mask, mask);
  for (; iData < end; iData++) *iData ^= mask;
  mask = NetworkEndian::readUint32(&mask);
  data = (uint8_t*)iData;
  len = len % 4;

  // There maybe up to 3 trailing bytes that need to be dealt with
  // individually

  while (len) {
    *data ^= mask >> 24;
    mask = RotateLeft(mask, 8);
    data++;
    len--;
  }
}

void WebSocketChannel::GeneratePing() {
  nsAutoCString buf;
  buf.AssignLiteral("PING");
  EnqueueOutgoingMessage(mOutgoingPingMessages,
                         new OutboundMessage(kMsgTypePing, buf));
}

void WebSocketChannel::GeneratePong(uint8_t* payload, uint32_t len) {
  nsAutoCString buf;
  buf.SetLength(len);
  if (buf.Length() < len) {
    LOG(("WebSocketChannel::GeneratePong Allocation Failure\n"));
    return;
  }

  memcpy(buf.BeginWriting(), payload, len);
  EnqueueOutgoingMessage(mOutgoingPongMessages,
                         new OutboundMessage(kMsgTypePong, buf));
}

void WebSocketChannel::EnqueueOutgoingMessage(nsDeque<OutboundMessage>& aQueue,
                                              OutboundMessage* aMsg) {
  MOZ_ASSERT(mIOThread->IsOnCurrentThread(), "not on right thread");

  LOG(
      ("WebSocketChannel::EnqueueOutgoingMessage %p "
       "queueing msg %p [type=%s len=%d]\n",
       this, aMsg, msgNames[aMsg->GetMsgType()], aMsg->Length()));

  aQueue.Push(aMsg);
  OnOutputStreamReady(mSocketOut);
}

uint16_t WebSocketChannel::ResultToCloseCode(nsresult resultCode) {
  if (NS_SUCCEEDED(resultCode)) return CLOSE_NORMAL;

  switch (resultCode) {
    case NS_ERROR_FILE_TOO_BIG:
    case NS_ERROR_OUT_OF_MEMORY:
      return CLOSE_TOO_LARGE;
    case NS_ERROR_CANNOT_CONVERT_DATA:
      return CLOSE_INVALID_PAYLOAD;
    case NS_ERROR_UNEXPECTED:
      return CLOSE_INTERNAL_ERROR;
    default:
      return CLOSE_PROTOCOL_ERROR;
  }
}

void WebSocketChannel::PrimeNewOutgoingMessage() {
  LOG(("WebSocketChannel::PrimeNewOutgoingMessage() %p\n", this));
  MOZ_ASSERT(mIOThread->IsOnCurrentThread(), "not on right thread");
  MOZ_ASSERT(!mCurrentOut, "Current message in progress");

  nsresult rv = NS_OK;

  mCurrentOut = mOutgoingPongMessages.PopFront();
  if (mCurrentOut) {
    MOZ_ASSERT(mCurrentOut->GetMsgType() == kMsgTypePong, "Not pong message!");
  } else {
    mCurrentOut = mOutgoingPingMessages.PopFront();
    if (mCurrentOut) {
      MOZ_ASSERT(mCurrentOut->GetMsgType() == kMsgTypePing,
                 "Not ping message!");
    } else {
      mCurrentOut = mOutgoingMessages.PopFront();
    }
  }

  if (!mCurrentOut) return;

  auto cleanupAfterFailure =
      MakeScopeExit([&] { DeleteCurrentOutGoingMessage(); });

  WsMsgType msgType = mCurrentOut->GetMsgType();

  LOG(
      ("WebSocketChannel::PrimeNewOutgoingMessage "
       "%p found queued msg %p [type=%s len=%d]\n",
       this, mCurrentOut, msgNames[msgType], mCurrentOut->Length()));

  mCurrentOutSent = 0;
  mHdrOut = mOutHeader;

  uint8_t maskBit = mIsServerSide ? 0 : kMaskBit;
  uint8_t maskSize = mIsServerSide ? 0 : 4;

  uint8_t* payload = nullptr;

  if (msgType == kMsgTypeFin) {
    // This is a demand to create a close message
    if (mClientClosed) {
      DeleteCurrentOutGoingMessage();
      PrimeNewOutgoingMessage();
      cleanupAfterFailure.release();
      return;
    }

    mClientClosed = true;
    mOutHeader[0] = kFinalFragBit | nsIWebSocketFrame::OPCODE_CLOSE;
    mOutHeader[1] = maskBit;

    // payload is offset 2 plus size of the mask
    payload = mOutHeader + 2 + maskSize;

    // The close reason code sits in the first 2 bytes of payload
    // If the channel user provided a code and reason during Close()
    // and there isn't an internal error, use that.
    if (NS_SUCCEEDED(mStopOnClose)) {
      MutexAutoLock lock(mMutex);
      if (mScriptCloseCode) {
        NetworkEndian::writeUint16(payload, mScriptCloseCode);
        mOutHeader[1] += 2;
        mHdrOutToSend = 4 + maskSize;
        if (!mScriptCloseReason.IsEmpty()) {
          MOZ_ASSERT(mScriptCloseReason.Length() <= 123,
                     "Close Reason Too Long");
          mOutHeader[1] += mScriptCloseReason.Length();
          mHdrOutToSend += mScriptCloseReason.Length();
          memcpy(payload + 2, mScriptCloseReason.BeginReading(),
                 mScriptCloseReason.Length());
        }
      } else {
        // No close code/reason, so payload length = 0.  We must still send mask
        // even though it's not used.  Keep payload offset so we write mask
        // below.
        mHdrOutToSend = 2 + maskSize;
      }
    } else {
      NetworkEndian::writeUint16(payload, ResultToCloseCode(mStopOnClose));
      mOutHeader[1] += 2;
      mHdrOutToSend = 4 + maskSize;
    }

    if (mServerClosed) {
      /* bidi close complete */
      mReleaseOnTransmit = 1;
    } else if (NS_FAILED(mStopOnClose)) {
      /* result of abort session - give up */
      StopSession(mStopOnClose);
    } else {
      /* wait for reciprocal close from server */
      rv = NS_NewTimerWithCallback(getter_AddRefs(mCloseTimer), this,
                                   mCloseTimeout, nsITimer::TYPE_ONE_SHOT);
      if (NS_FAILED(rv)) {
        StopSession(rv);
      }
    }
  } else {
    switch (msgType) {
      case kMsgTypePong:
        mOutHeader[0] = kFinalFragBit | nsIWebSocketFrame::OPCODE_PONG;
        break;
      case kMsgTypePing:
        mOutHeader[0] = kFinalFragBit | nsIWebSocketFrame::OPCODE_PING;
        break;
      case kMsgTypeString:
        mOutHeader[0] = kFinalFragBit | nsIWebSocketFrame::OPCODE_TEXT;
        break;
      case kMsgTypeStream:
        // HACK ALERT:  read in entire stream into string.
        // Will block socket transport thread if file is blocking.
        // TODO: bug 704447:  don't block socket thread!
        rv = mCurrentOut->ConvertStreamToString();
        if (NS_FAILED(rv)) {
          AbortSession(NS_ERROR_FILE_TOO_BIG);
          return;
        }
        // Now we're a binary string
        msgType = kMsgTypeBinaryString;

        // no break: fall down into binary string case
        [[fallthrough]];

      case kMsgTypeBinaryString:
        mOutHeader[0] = kFinalFragBit | nsIWebSocketFrame::OPCODE_BINARY;
        break;
      case kMsgTypeFin:
        MOZ_ASSERT(false, "unreachable");  // avoid compiler warning
        break;
    }

    // deflate the payload if PMCE is negotiated
    MutexAutoLock lock(mCompressorMutex);
    if (mPMCECompressor &&
        (msgType == kMsgTypeString || msgType == kMsgTypeBinaryString)) {
      if (mCurrentOut->DeflatePayload(mPMCECompressor.get())) {
        // The payload was deflated successfully, set RSV1 bit
        mOutHeader[0] |= kRsv1Bit;

        LOG(
            ("WebSocketChannel::PrimeNewOutgoingMessage %p current msg %p was "
             "deflated [origLength=%d, newLength=%d].\n",
             this, mCurrentOut, mCurrentOut->OrigLength(),
             mCurrentOut->Length()));
      }
    }

    if (mCurrentOut->Length() < 126) {
      mOutHeader[1] = mCurrentOut->Length() | maskBit;
      mHdrOutToSend = 2 + maskSize;
    } else if (mCurrentOut->Length() <= 0xffff) {
      mOutHeader[1] = 126 | maskBit;
      NetworkEndian::writeUint16(mOutHeader + sizeof(uint16_t),
                                 mCurrentOut->Length());
      mHdrOutToSend = 4 + maskSize;
    } else {
      mOutHeader[1] = 127 | maskBit;
      NetworkEndian::writeUint64(mOutHeader + 2, mCurrentOut->Length());
      mHdrOutToSend = 10 + maskSize;
    }
    payload = mOutHeader + mHdrOutToSend;
  }

  MOZ_ASSERT(payload, "payload offset not found");

  uint32_t mask = 0;
  if (!mIsServerSide) {
    // Perform the sending mask. Never use a zero mask
    do {
      static_assert(4 == sizeof(mask), "Size of the mask should be equal to 4");
      nsresult rv = mRandomGenerator->GenerateRandomBytesInto(mask);
      if (NS_FAILED(rv)) {
        LOG(
            ("WebSocketChannel::PrimeNewOutgoingMessage(): "
             "GenerateRandomBytes failure %" PRIx32 "\n",
             static_cast<uint32_t>(rv)));
        AbortSession(rv);
        return;
      }
    } while (!mask);
    NetworkEndian::writeUint32(payload - sizeof(uint32_t), mask);
  }

  LOG(("WebSocketChannel::PrimeNewOutgoingMessage() using mask %08x\n", mask));

  // We don't mask the framing, but occasionally we stick a little payload
  // data in the buffer used for the framing. Close frames are the current
  // example. This data needs to be masked, but it is never more than a
  // handful of bytes and might rotate the mask, so we can just do it locally.
  // For real data frames we ship the bulk of the payload off to ApplyMask()

  RefPtr<WebSocketFrame> frame = mService->CreateFrameIfNeeded(
      mOutHeader[0] & WebSocketChannel::kFinalFragBit,
      mOutHeader[0] & WebSocketChannel::kRsv1Bit,
      mOutHeader[0] & WebSocketChannel::kRsv2Bit,
      mOutHeader[0] & WebSocketChannel::kRsv3Bit,
      mOutHeader[0] & WebSocketChannel::kOpcodeBitsMask,
      mOutHeader[1] & WebSocketChannel::kMaskBit, mask, payload,
      mHdrOutToSend - (payload - mOutHeader), mCurrentOut->BeginOrigReading(),
      mCurrentOut->OrigLength());

  if (frame) {
    mService->FrameSent(mSerial, mInnerWindowID, mHttpChannelId,
                        frame.forget());
  }

  if (mask) {
    while (payload < (mOutHeader + mHdrOutToSend)) {
      *payload ^= mask >> 24;
      mask = RotateLeft(mask, 8);
      payload++;
    }

    // Mask the real message payloads
    ApplyMask(mask, mCurrentOut->BeginWriting(), mCurrentOut->Length());
  }

  int32_t len = mCurrentOut->Length();

  // for small frames, copy it all together for a contiguous write
  if (len && len <= kCopyBreak) {
    memcpy(mOutHeader + mHdrOutToSend, mCurrentOut->BeginWriting(), len);
    mHdrOutToSend += len;
    mCurrentOutSent = len;
  }

  // Transmitting begins - mHdrOutToSend bytes from mOutHeader and
  // mCurrentOut->Length() bytes from mCurrentOut. The latter may be
  // coaleseced into the former for small messages or as the result of the
  // compression process.

  cleanupAfterFailure.release();
}

void WebSocketChannel::DeleteCurrentOutGoingMessage() {
  delete mCurrentOut;
  mCurrentOut = nullptr;
  mCurrentOutSent = 0;
}

void WebSocketChannel::EnsureHdrOut(uint32_t size) {
  LOG(("WebSocketChannel::EnsureHdrOut() %p [%d]\n", this, size));

  if (mDynamicOutputSize < size) {
    mDynamicOutputSize = size;
    mDynamicOutput = (uint8_t*)moz_xrealloc(mDynamicOutput, mDynamicOutputSize);
  }

  mHdrOut = mDynamicOutput;
}

namespace {

class RemoveObserverRunnable : public Runnable {
  RefPtr<WebSocketChannel> mChannel;

 public:
  explicit RemoveObserverRunnable(WebSocketChannel* aChannel)
      : Runnable("net::RemoveObserverRunnable"), mChannel(aChannel) {}

  NS_IMETHOD Run() override {
    nsCOMPtr<nsIObserverService> observerService =
        mozilla::services::GetObserverService();
    if (!observerService) {
      NS_WARNING("failed to get observer service");
      return NS_OK;
    }

    observerService->RemoveObserver(mChannel, NS_NETWORK_LINK_TOPIC);
    return NS_OK;
  }
};

}  // namespace

void WebSocketChannel::CleanupConnection() {
  // normally this should be called on socket thread, but it may be called
  // on MainThread

  LOG(("WebSocketChannel::CleanupConnection() %p", this));
  // This needs to run on the IOThread so we don't need to lock a bunch of these
  if (!mIOThread->IsOnCurrentThread()) {
    mIOThread->Dispatch(
        NewRunnableMethod("net::WebSocketChannel::CleanupConnection", this,
                          &WebSocketChannel::CleanupConnection),
        NS_DISPATCH_NORMAL);
    return;
  }

  if (mLingeringCloseTimer) {
    mLingeringCloseTimer->Cancel();
    mLingeringCloseTimer = nullptr;
  }

  if (mSocketIn) {
    if (mDataStarted) {
      mSocketIn->AsyncWait(nullptr, 0, 0, nullptr);
    }
    mSocketIn = nullptr;
  }

  if (mSocketOut) {
    mSocketOut->AsyncWait(nullptr, 0, 0, nullptr);
    mSocketOut = nullptr;
  }

  if (mTransport) {
    mTransport->SetSecurityCallbacks(nullptr);
    mTransport->SetEventSink(nullptr, nullptr);
    mTransport->Close(NS_BASE_STREAM_CLOSED);
    mTransport = nullptr;
  }

  if (mConnectionLogService && !mPrivateBrowsing) {
    mConnectionLogService->RemoveHost(mHost, mSerial);
  }

  // The observer has to be removed on the main-thread.
  NS_DispatchToMainThread(new RemoveObserverRunnable(this));

  DecrementSessionCount();
}

void WebSocketChannel::StopSession(nsresult reason) {
  LOG(("WebSocketChannel::StopSession() %p [%" PRIx32 "]\n", this,
       static_cast<uint32_t>(reason)));

  {
    MutexAutoLock lock(mMutex);
    if (mStopped) {
      return;
    }
    mStopped = true;
  }

  DoStopSession(reason);
}

void WebSocketChannel::DoStopSession(nsresult reason) {
  LOG(("WebSocketChannel::DoStopSession() %p [%" PRIx32 "]\n", this,
       static_cast<uint32_t>(reason)));

  // normally this should be called on socket thread, but it is ok to call it
  // from OnStartRequest before the socket thread machine has gotten underway.
  // If mDataStarted is false, this is called on MainThread for Close().
  // Otherwise it should be called on the IO thread

  MOZ_ASSERT(mStopped);
  MOZ_ASSERT(mIOThread->IsOnCurrentThread() || mTCPClosed || !mDataStarted);

  if (!mOpenedHttpChannel) {
    // The HTTP channel information will never be used in this case
    NS_ReleaseOnMainThread("WebSocketChannel::mChannel", mChannel.forget());
    NS_ReleaseOnMainThread("WebSocketChannel::mHttpChannel",
                           mHttpChannel.forget());
    NS_ReleaseOnMainThread("WebSocketChannel::mLoadGroup", mLoadGroup.forget());
    NS_ReleaseOnMainThread("WebSocketChannel::mCallbacks", mCallbacks.forget());
  }

  if (mCloseTimer) {
    mCloseTimer->Cancel();
    mCloseTimer = nullptr;
  }

  // mOpenTimer must be null if mDataStarted is true and we're not on MainThread
  if (mOpenTimer) {
    MOZ_ASSERT(NS_IsMainThread(), "not main thread");
    mOpenTimer->Cancel();
    mOpenTimer = nullptr;
  }

  {
    MutexAutoLock lock(mMutex);
    if (mReconnectDelayTimer) {
      mReconnectDelayTimer->Cancel();
      NS_ReleaseOnMainThread("WebSocketChannel::mMutex",
                             mReconnectDelayTimer.forget());
    }
  }

  if (mPingTimer) {
    mPingTimer->Cancel();
    mPingTimer = nullptr;
  }

  if (!mTCPClosed && mDataStarted) {
    if (mSocketIn) {
      // Drain, within reason, this socket. if we leave any data
      // unconsumed (including the tcp fin) a RST will be generated
      // The right thing to do here is shutdown(SHUT_WR) and then wait
      // a little while to see if any data comes in.. but there is no
      // reason to delay things for that when the websocket handshake
      // is supposed to guarantee a quiet connection except for that fin.

      char buffer[512];
      uint32_t count = 0;
      uint32_t total = 0;
      nsresult rv;
      do {
        total += count;
        rv = mSocketIn->Read(buffer, 512, &count);
        if (rv != NS_BASE_STREAM_WOULD_BLOCK && (NS_FAILED(rv) || count == 0)) {
          mTCPClosed = true;
        }
      } while (NS_SUCCEEDED(rv) && count > 0 && total < 32000);
    }
  }

  int32_t sessionCount = kLingeringCloseThreshold;
  nsWSAdmissionManager::GetSessionCount(sessionCount);

  if (!mTCPClosed && mTransport && sessionCount < kLingeringCloseThreshold) {
    // 7.1.1 says that the client SHOULD wait for the server to close the TCP
    // connection. This is so we can reuse port numbers before 2 MSL expires,
    // which is not really as much of a concern for us as the amount of state
    // that might be accrued by keeping this channel object around waiting for
    // the server. We handle the SHOULD by waiting a short time in the common
    // case, but not waiting in the case of high concurrency.
    //
    // Normally this will be taken care of in AbortSession() after mTCPClosed
    // is set when the server close arrives without waiting for the timeout to
    // expire.

    LOG(("WebSocketChannel::DoStopSession: Wait for Server TCP close"));

    nsresult rv;
    rv = NS_NewTimerWithCallback(getter_AddRefs(mLingeringCloseTimer), this,
                                 kLingeringCloseTimeout,
                                 nsITimer::TYPE_ONE_SHOT);
    if (NS_FAILED(rv)) CleanupConnection();
  } else {
    CleanupConnection();
  }

  {
    MutexAutoLock lock(mMutex);
    if (mCancelable) {
      mCancelable->Cancel(NS_ERROR_UNEXPECTED);
      mCancelable = nullptr;
    }
  }

  {
    MutexAutoLock lock(mCompressorMutex);
    mPMCECompressor = nullptr;
  }
  if (!mCalledOnStop) {
    mCalledOnStop = true;

    nsWSAdmissionManager::OnStopSession(this, reason);

    RefPtr<CallOnStop> runnable =
        new CallOnStop(this, TakeListenerMT(), reason);
    if (nsCOMPtr<nsIEventTarget> target = GetTargetThread()) {
      target->Dispatch(runnable, NS_DISPATCH_NORMAL);
    }
  }
}

// Called from MainThread, and called from IOThread in
// PrimeNewOutgoingMessage
void WebSocketChannel::AbortSession(nsresult reason) {
  LOG(("WebSocketChannel::AbortSession() %p [reason %" PRIx32
       "] stopped = %d\n",
       this, static_cast<uint32_t>(reason), !!mStopped));

  MOZ_ASSERT(NS_FAILED(reason), "reason must be a failure!");

  // normally this should be called on socket thread, but it is ok to call it
  // from the main thread before StartWebsocketData() has completed
  MOZ_ASSERT(mIOThread->IsOnCurrentThread() || !mDataStarted);

  // When we are failing we need to close the TCP connection immediately
  // as per 7.1.1
  mTCPClosed = true;

  if (mLingeringCloseTimer) {
    MOZ_ASSERT(mStopped, "Lingering without Stop");
    LOG(("WebSocketChannel:: Cleanup connection based on TCP Close"));
    CleanupConnection();
    return;
  }

  {
    MutexAutoLock lock(mMutex);
    if (mStopped) {
      return;
    }

    if (mTransport && reason != NS_BASE_STREAM_CLOSED && !mRequestedClose &&
        !mClientClosed && !mServerClosed && mDataStarted) {
      mRequestedClose = true;
      mStopOnClose = reason;
      mIOThread->Dispatch(
          new OutboundEnqueuer(this,
                               new OutboundMessage(kMsgTypeFin, VoidCString())),
          nsIEventTarget::DISPATCH_NORMAL);
      return;
    }

    mStopped = true;
  }

  DoStopSession(reason);
}

// ReleaseSession is called on orderly shutdown
void WebSocketChannel::ReleaseSession() {
  LOG(("WebSocketChannel::ReleaseSession() %p stopped = %d\n", this,
       !!mStopped));
  MOZ_ASSERT(mIOThread->IsOnCurrentThread(), "not on right thread");

  StopSession(NS_OK);
}

void WebSocketChannel::IncrementSessionCount() {
  if (!mIncrementedSessionCount) {
    nsWSAdmissionManager::IncrementSessionCount();
    mIncrementedSessionCount = true;
  }
}

void WebSocketChannel::DecrementSessionCount() {
  // Make sure we decrement session count only once, and only if we incremented
  // it. This code is thread-safe: sWebSocketAdmissions->DecrementSessionCount
  // is atomic, and mIncrementedSessionCount/mDecrementedSessionCount are set at
  // times when they'll never be a race condition for checking/setting them.
  if (mIncrementedSessionCount && !mDecrementedSessionCount) {
    nsWSAdmissionManager::DecrementSessionCount();
    mDecrementedSessionCount = true;
  }
}

namespace {
enum ExtensionParseMode { eParseServerSide, eParseClientSide };
}

static nsresult ParseWebSocketExtension(const nsACString& aExtension,
                                        ExtensionParseMode aMode,
                                        bool& aClientNoContextTakeover,
                                        bool& aServerNoContextTakeover,
                                        int32_t& aClientMaxWindowBits,
                                        int32_t& aServerMaxWindowBits) {
  nsCCharSeparatedTokenizer tokens(aExtension, ';');

  if (!tokens.hasMoreTokens() ||
      !tokens.nextToken().EqualsLiteral("permessage-deflate")) {
    LOG(
        ("WebSocketChannel::ParseWebSocketExtension: "
         "HTTP Sec-WebSocket-Extensions negotiated unknown value %s\n",
         PromiseFlatCString(aExtension).get()));
    return NS_ERROR_ILLEGAL_VALUE;
  }

  aClientNoContextTakeover = aServerNoContextTakeover = false;
  aClientMaxWindowBits = aServerMaxWindowBits = -1;

  while (tokens.hasMoreTokens()) {
    auto token = tokens.nextToken();

    int32_t nameEnd, valueStart;
    int32_t delimPos = token.FindChar('=');
    if (delimPos == kNotFound) {
      nameEnd = token.Length();
      valueStart = token.Length();
    } else {
      nameEnd = delimPos;
      valueStart = delimPos + 1;
    }

    auto paramName = Substring(token, 0, nameEnd);
    auto paramValue = Substring(token, valueStart);

    if (paramName.EqualsLiteral("client_no_context_takeover")) {
      if (!paramValue.IsEmpty()) {
        LOG(
            ("WebSocketChannel::ParseWebSocketExtension: parameter "
             "client_no_context_takeover must not have value, found %s\n",
             PromiseFlatCString(paramValue).get()));
        return NS_ERROR_ILLEGAL_VALUE;
      }
      if (aClientNoContextTakeover) {
        LOG(
            ("WebSocketChannel::ParseWebSocketExtension: found multiple "
             "parameters client_no_context_takeover\n"));
        return NS_ERROR_ILLEGAL_VALUE;
      }
      aClientNoContextTakeover = true;
    } else if (paramName.EqualsLiteral("server_no_context_takeover")) {
      if (!paramValue.IsEmpty()) {
        LOG(
            ("WebSocketChannel::ParseWebSocketExtension: parameter "
             "server_no_context_takeover must not have value, found %s\n",
             PromiseFlatCString(paramValue).get()));
        return NS_ERROR_ILLEGAL_VALUE;
      }
      if (aServerNoContextTakeover) {
        LOG(
            ("WebSocketChannel::ParseWebSocketExtension: found multiple "
             "parameters server_no_context_takeover\n"));
        return NS_ERROR_ILLEGAL_VALUE;
      }
      aServerNoContextTakeover = true;
    } else if (paramName.EqualsLiteral("client_max_window_bits")) {
      if (aClientMaxWindowBits != -1) {
        LOG(
            ("WebSocketChannel::ParseWebSocketExtension: found multiple "
             "parameters client_max_window_bits\n"));
        return NS_ERROR_ILLEGAL_VALUE;
      }

      if (aMode == eParseServerSide && paramValue.IsEmpty()) {
        // Use -2 to indicate that "client_max_window_bits" has been parsed,
        // but had no value.
        aClientMaxWindowBits = -2;
      } else {
        nsresult errcode;
        aClientMaxWindowBits =
            PromiseFlatCString(paramValue).ToInteger(&errcode);
        if (NS_FAILED(errcode) || aClientMaxWindowBits < 8 ||
            aClientMaxWindowBits > 15) {
          LOG(
              ("WebSocketChannel::ParseWebSocketExtension: found invalid "
               "parameter client_max_window_bits %s\n",
               PromiseFlatCString(paramValue).get()));
          return NS_ERROR_ILLEGAL_VALUE;
        }
      }
    } else if (paramName.EqualsLiteral("server_max_window_bits")) {
      if (aServerMaxWindowBits != -1) {
        LOG(
            ("WebSocketChannel::ParseWebSocketExtension: found multiple "
             "parameters server_max_window_bits\n"));
        return NS_ERROR_ILLEGAL_VALUE;
      }

      nsresult errcode;
      aServerMaxWindowBits = PromiseFlatCString(paramValue).ToInteger(&errcode);
      if (NS_FAILED(errcode) || aServerMaxWindowBits < 8 ||
          aServerMaxWindowBits > 15) {
        LOG(
            ("WebSocketChannel::ParseWebSocketExtension: found invalid "
             "parameter server_max_window_bits %s\n",
             PromiseFlatCString(paramValue).get()));
        return NS_ERROR_ILLEGAL_VALUE;
      }
    } else {
      LOG(
          ("WebSocketChannel::ParseWebSocketExtension: found unknown "
           "parameter %s\n",
           PromiseFlatCString(paramName).get()));
      return NS_ERROR_ILLEGAL_VALUE;
    }
  }

  if (aClientMaxWindowBits == -2) {
    aClientMaxWindowBits = -1;
  }

  return NS_OK;
}

nsresult WebSocketChannel::HandleExtensions() {
  LOG(("WebSocketChannel::HandleExtensions() %p\n", this));

  nsresult rv;
  nsAutoCString extensions;

  MOZ_ASSERT(NS_IsMainThread(), "not main thread");

  rv = mHttpChannel->GetResponseHeader("Sec-WebSocket-Extensions"_ns,
                                       extensions);
  extensions.CompressWhitespace();
  if (extensions.IsEmpty()) {
    return NS_OK;
  }

  LOG(
      ("WebSocketChannel::HandleExtensions: received "
       "Sec-WebSocket-Extensions header: %s\n",
       extensions.get()));

  bool clientNoContextTakeover;
  bool serverNoContextTakeover;
  int32_t clientMaxWindowBits;
  int32_t serverMaxWindowBits;

  rv = ParseWebSocketExtension(extensions, eParseClientSide,
                               clientNoContextTakeover, serverNoContextTakeover,
                               clientMaxWindowBits, serverMaxWindowBits);
  if (NS_FAILED(rv)) {
    AbortSession(rv);
    return rv;
  }

  if (clientMaxWindowBits == -1) {
    clientMaxWindowBits = 15;
  }
  if (serverMaxWindowBits == -1) {
    serverMaxWindowBits = 15;
  }

  MutexAutoLock lock(mCompressorMutex);
  mPMCECompressor = std::make_unique<PMCECompression>(
      clientNoContextTakeover, clientMaxWindowBits, serverMaxWindowBits);
  if (mPMCECompressor->Active()) {
    LOG(
        ("WebSocketChannel::HandleExtensions: PMCE negotiated, %susing "
         "context takeover, clientMaxWindowBits=%d, "
         "serverMaxWindowBits=%d\n",
         clientNoContextTakeover ? "NOT " : "", clientMaxWindowBits,
         serverMaxWindowBits));

    mNegotiatedExtensions = "permessage-deflate";
  } else {
    LOG(
        ("WebSocketChannel::HandleExtensions: Cannot init PMCE "
         "compression object\n"));
    mPMCECompressor = nullptr;
    AbortSession(NS_ERROR_UNEXPECTED);
    return NS_ERROR_UNEXPECTED;
  }

  return NS_OK;
}

void ProcessServerWebSocketExtensions(const nsACString& aExtensions,
                                      nsACString& aNegotiatedExtensions) {
  aNegotiatedExtensions.Truncate();

  for (const auto& ext :
       nsCCharSeparatedTokenizer(aExtensions, ',').ToRange()) {
    bool clientNoContextTakeover;
    bool serverNoContextTakeover;
    int32_t clientMaxWindowBits;
    int32_t serverMaxWindowBits;

    nsresult rv = ParseWebSocketExtension(
        ext, eParseServerSide, clientNoContextTakeover, serverNoContextTakeover,
        clientMaxWindowBits, serverMaxWindowBits);
    if (NS_FAILED(rv)) {
      // Ignore extensions that we can't parse
      continue;
    }

    aNegotiatedExtensions.AssignLiteral("permessage-deflate");
    if (clientNoContextTakeover) {
      aNegotiatedExtensions.AppendLiteral(";client_no_context_takeover");
    }
    if (serverNoContextTakeover) {
      aNegotiatedExtensions.AppendLiteral(";server_no_context_takeover");
    }
    if (clientMaxWindowBits != -1) {
      aNegotiatedExtensions.AppendLiteral(";client_max_window_bits=");
      aNegotiatedExtensions.AppendInt(clientMaxWindowBits);
    }
    if (serverMaxWindowBits != -1) {
      aNegotiatedExtensions.AppendLiteral(";server_max_window_bits=");
      aNegotiatedExtensions.AppendInt(serverMaxWindowBits);
    }

    return;
  }
}

nsresult CalculateWebSocketHashedSecret(const nsACString& aKey,
                                        nsACString& aHash) {
  nsresult rv;
  nsCString key = aKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"_ns;
  nsCOMPtr<nsICryptoHash> hasher =
      do_CreateInstance(NS_CRYPTO_HASH_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = hasher->Init(nsICryptoHash::SHA1);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = hasher->Update((const uint8_t*)key.BeginWriting(), key.Length());
  NS_ENSURE_SUCCESS(rv, rv);
  return hasher->Finish(true, aHash);
}

nsresult WebSocketChannel::SetupRequest() {
  LOG(("WebSocketChannel::SetupRequest() %p\n", this));

  nsresult rv;

  if (mLoadGroup) {
    rv = mHttpChannel->SetLoadGroup(mLoadGroup);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = mHttpChannel->SetLoadFlags(
      nsIRequest::LOAD_BACKGROUND | nsIRequest::INHIBIT_CACHING |
      nsIRequest::LOAD_BYPASS_CACHE | nsIChannel::LOAD_BYPASS_SERVICE_WORKER);
  NS_ENSURE_SUCCESS(rv, rv);

  // we never let websockets be blocked by head CSS/JS loads to avoid
  // potential deadlock where server generation of CSS/JS requires
  // an XHR signal.
  nsCOMPtr<nsIClassOfService> cos(do_QueryInterface(mChannel));
  if (cos) {
    cos->AddClassFlags(nsIClassOfService::Unblocked);
  }

  // draft-ietf-hybi-thewebsocketprotocol-07 illustrates Upgrade: websocket
  // in lower case, so go with that. It is technically case insensitive.
  rv = mChannel->HTTPUpgrade("websocket"_ns, this);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mHttpChannel->SetRequestHeader("Sec-WebSocket-Version"_ns,
                                      nsLiteralCString(SEC_WEBSOCKET_VERSION),
                                      false);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  if (!mOrigin.IsEmpty()) {
    rv = mHttpChannel->SetRequestHeader("Origin"_ns, mOrigin, false);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
  }

  if (!mProtocol.IsEmpty()) {
    rv = mHttpChannel->SetRequestHeader("Sec-WebSocket-Protocol"_ns, mProtocol,
                                        true);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
  }

  rv = mHttpChannel->SetRequestHeader("Sec-WebSocket-Extensions"_ns,
                                      "permessage-deflate"_ns, false);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  uint8_t* secKey;
  nsAutoCString secKeyString;

  rv = mRandomGenerator->GenerateRandomBytes(16, &secKey);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = Base64Encode(reinterpret_cast<const char*>(secKey), 16, secKeyString);
  free(secKey);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = mHttpChannel->SetRequestHeader("Sec-WebSocket-Key"_ns, secKeyString,
                                      false);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  LOG(("WebSocketChannel::SetupRequest: client key %s\n", secKeyString.get()));

  // prepare the value we expect to see in
  // the sec-websocket-accept response header
  rv = CalculateWebSocketHashedSecret(secKeyString, mHashedSecret);
  NS_ENSURE_SUCCESS(rv, rv);
  LOG(("WebSocketChannel::SetupRequest: expected server key %s\n",
       mHashedSecret.get()));

  mHttpChannelId = mHttpChannel->ChannelId();

  return NS_OK;
}

nsresult WebSocketChannel::DoAdmissionDNS() {
  nsresult rv;

  nsCString hostName;
  rv = mURI->GetHost(hostName);
  NS_ENSURE_SUCCESS(rv, rv);
  mAddress = hostName;
  nsCString path;
  rv = mURI->GetFilePath(path);
  NS_ENSURE_SUCCESS(rv, rv);
  mPath = std::move(path);
  rv = mURI->GetPort(&mPort);
  NS_ENSURE_SUCCESS(rv, rv);
  if (mPort == -1) mPort = (mEncrypted ? kDefaultWSSPort : kDefaultWSPort);
  nsCOMPtr<nsIDNSService> dns;
  dns = mozilla::components::DNS::Service(&rv);
  NS_ENSURE_SUCCESS(rv, rv);
  nsCOMPtr<nsIEventTarget> main = GetMainThreadSerialEventTarget();
  nsCOMPtr<nsICancelable> cancelable;
  rv = dns->AsyncResolveNative(hostName, nsIDNSService::RESOLVE_TYPE_DEFAULT,
                               nsIDNSService::RESOLVE_DEFAULT_FLAGS, nullptr,
                               this, main, mLoadInfo->GetOriginAttributes(),
                               getter_AddRefs(cancelable));
  if (NS_FAILED(rv)) {
    return rv;
  }

  MutexAutoLock lock(mMutex);
  MOZ_ASSERT(!mCancelable);
  mCancelable = std::move(cancelable);
  return rv;
}

nsresult WebSocketChannel::ApplyForAdmission() {
  LOG(("WebSocketChannel::ApplyForAdmission() %p\n", this));

  // Websockets has a policy of 1 session at a time being allowed in the
  // CONNECTING state per server IP address (not hostname)

  // Check to see if a proxy is being used before making DNS call
  nsCOMPtr<nsIProtocolProxyService> pps;
  pps = mozilla::components::ProtocolProxy::Service();

  if (!pps) {
    // go straight to DNS
    // expect the callback in ::OnLookupComplete
    LOG((
        "WebSocketChannel::ApplyForAdmission: checking for concurrent open\n"));
    return DoAdmissionDNS();
  }

  nsresult rv;
  nsCOMPtr<nsICancelable> cancelable;
  rv = pps->AsyncResolve(
      mHttpChannel,
      nsIProtocolProxyService::RESOLVE_PREFER_SOCKS_PROXY |
          nsIProtocolProxyService::RESOLVE_PREFER_HTTPS_PROXY |
          nsIProtocolProxyService::RESOLVE_ALWAYS_TUNNEL,
      this, nullptr, getter_AddRefs(cancelable));

  MutexAutoLock lock(mMutex);
  MOZ_ASSERT(!mCancelable);
  mCancelable = std::move(cancelable);
  return rv;
}

// Called after both OnStartRequest and OnTransportAvailable have
// executed. This essentially ends the handshake and starts the websockets
// protocol state machine.
nsresult WebSocketChannel::CallStartWebsocketData() {
  LOG(("WebSocketChannel::CallStartWebsocketData() %p", this));
  MOZ_ASSERT(NS_IsMainThread(), "not main thread");

  if (mOpenTimer) {
    mOpenTimer->Cancel();
    mOpenTimer = nullptr;
  }

  nsCOMPtr<nsIEventTarget> target = GetTargetThread();
  if (target && !target->IsOnCurrentThread()) {
    return target->Dispatch(
        NewRunnableMethod("net::WebSocketChannel::StartWebsocketData", this,
                          &WebSocketChannel::StartWebsocketData),
        NS_DISPATCH_NORMAL);
  }

  return StartWebsocketData();
}

nsresult WebSocketChannel::StartWebsocketData() {
  {
    MutexAutoLock lock(mMutex);
    LOG(("WebSocketChannel::StartWebsocketData() %p", this));
    MOZ_ASSERT(!mDataStarted, "StartWebsocketData twice");

    if (mStopped) {
      LOG(
          ("WebSocketChannel::StartWebsocketData channel already closed, not "
           "starting data"));
      return NS_ERROR_NOT_AVAILABLE;
    }
  }

  RefPtr<WebSocketChannel> self = this;
  mIOThread->Dispatch(NS_NewRunnableFunction(
      "WebSocketChannel::StartWebsocketData", [self{std::move(self)}] {
        LOG(("WebSocketChannel::DoStartWebsocketData() %p", self.get()));
        {
          MutexAutoLock lock(self->mMutex);
          if (self->mStopped) {
            return;
          }
          self->mDataStarted = true;
        }

        NS_DispatchToMainThread(
            NewRunnableMethod("net::WebSocketChannel::NotifyOnStart", self,
                              &WebSocketChannel::NotifyOnStart),
            NS_DISPATCH_NORMAL);

        nsresult rv = self->mSocketIn->AsyncWait(self, 0, 0, self->mIOThread);
        if (NS_FAILED(rv)) {
          self->AbortSession(rv);
        }

        if (self->mPingInterval) {
          rv = self->StartPinging();
          if (NS_FAILED(rv)) {
            LOG((
                "WebSocketChannel::StartWebsocketData Could not start pinging, "
                "rv=0x%08" PRIx32,
                static_cast<uint32_t>(rv)));
            self->AbortSession(rv);
          }
        }
      }));

  return NS_OK;
}

void WebSocketChannel::NotifyOnStart() {
  RefPtr<BaseWebSocketChannel::ListenerAndContextContainer> listener =
      GetListenerMT();
  LOG(("WebSocketChannel::NotifyOnStart Notifying Listener %p",
       listener ? listener->mListener.get() : nullptr));
  if (listener) {
    nsresult rv = listener->mListener->OnStart(listener->mContext);
    if (NS_FAILED(rv)) {
      LOG(
          ("WebSocketChannel::NotifyOnStart "
           "listener->mListener->OnStart() failed with error 0x%08" PRIx32,
           static_cast<uint32_t>(rv)));
    }
  }
}

nsresult WebSocketChannel::StartPinging() {
  LOG(("WebSocketChannel::StartPinging() %p", this));
  MOZ_ASSERT(mIOThread->IsOnCurrentThread(), "not on right thread");
  MOZ_ASSERT(mPingInterval);
  MOZ_ASSERT(!mPingTimer);

  nsresult rv;
  rv = NS_NewTimerWithCallback(getter_AddRefs(mPingTimer), this, mPingInterval,
                               nsITimer::TYPE_ONE_SHOT);
  if (NS_SUCCEEDED(rv)) {
    LOG(("WebSocketChannel will generate ping after %d ms of receive silence\n",
         mPingInterval.load()));
  } else {
    NS_WARNING("unable to create ping timer. Carrying on.");
  }

  return NS_OK;
}

void WebSocketChannel::ReportConnectionTelemetry(nsresult aStatusCode) {
  // 3 bits are used. high bit is for wss, middle bit for failed,
  // and low bit for proxy..
  // 0 - 7 : ws-ok-plain, ws-ok-proxy, ws-failed-plain, ws-failed-proxy,
  //         wss-ok-plain, wss-ok-proxy, wss-failed-plain, wss-failed-proxy

  bool didProxy = false;

  nsCOMPtr<nsIProxyInfo> pi;
  nsCOMPtr<nsIProxiedChannel> pc = do_QueryInterface(mChannel);
  if (pc) pc->GetProxyInfo(getter_AddRefs(pi));
  if (pi) {
    nsAutoCString proxyType;
    pi->GetType(proxyType);
    if (!proxyType.IsEmpty() && !proxyType.EqualsLiteral("direct")) {
      didProxy = true;
    }
  }

  uint8_t value =
      (mEncrypted ? (1 << 2) : 0) |
      (!(mGotUpgradeOK && NS_SUCCEEDED(aStatusCode)) ? (1 << 1) : 0) |
      (didProxy ? (1 << 0) : 0);

  LOG(("WebSocketChannel::ReportConnectionTelemetry() %p %d", this, value));
  glean::websockets::handshake_type.AccumulateSingleSample(value);
}

// nsIDNSListener

NS_IMETHODIMP
WebSocketChannel::OnLookupComplete(nsICancelable* aRequest,
                                   nsIDNSRecord* aRecord, nsresult aStatus) {
  LOG(("WebSocketChannel::OnLookupComplete() %p [%p %p %" PRIx32 "]\n", this,
       aRequest, aRecord, static_cast<uint32_t>(aStatus)));

  MOZ_ASSERT(NS_IsMainThread(), "not main thread");

  {
    MutexAutoLock lock(mMutex);
    mCancelable = nullptr;
  }

  if (mStopped) {
    LOG(("WebSocketChannel::OnLookupComplete: Request Already Stopped\n"));
    return NS_OK;
  }

  // These failures are not fatal - we just use the hostname as the key
  if (NS_FAILED(aStatus)) {
    LOG(("WebSocketChannel::OnLookupComplete: No DNS Response\n"));

    // set host in case we got here without calling DoAdmissionDNS()
    mURI->GetHost(mAddress);
  } else {
    nsCOMPtr<nsIDNSAddrRecord> record = do_QueryInterface(aRecord);
    MOZ_ASSERT(record);
    nsresult rv = record->GetNextAddrAsString(mAddress);
    if (NS_FAILED(rv)) {
      LOG(("WebSocketChannel::OnLookupComplete: Failed GetNextAddr\n"));
    }
  }

  LOG(("WebSocket OnLookupComplete: Proceeding to ConditionallyConnect\n"));
  nsWSAdmissionManager::ConditionallyConnect(this);

  return NS_OK;
}

// nsIProtocolProxyCallback
NS_IMETHODIMP
WebSocketChannel::OnProxyAvailable(nsICancelable* aRequest,
                                   nsIChannel* aChannel, nsIProxyInfo* pi,
                                   nsresult status) {
  {
    MutexAutoLock lock(mMutex);
    MOZ_ASSERT(!mCancelable || (aRequest == mCancelable));
    mCancelable = nullptr;
  }

  if (mStopped) {
    LOG(("WebSocketChannel::OnProxyAvailable: [%p] Request Already Stopped\n",
         this));
    return NS_OK;
  }

  nsAutoCString type;
  if (NS_SUCCEEDED(status) && pi && NS_SUCCEEDED(pi->GetType(type)) &&
      !type.EqualsLiteral("direct")) {
    LOG(("WebSocket OnProxyAvailable [%p] Proxy found skip DNS lookup\n",
         this));
    // call DNS callback directly without DNS resolver
    OnLookupComplete(nullptr, nullptr, NS_ERROR_FAILURE);
  } else {
    LOG(("WebSocketChannel::OnProxyAvailable[%p] checking DNS resolution\n",
         this));
    nsresult rv = DoAdmissionDNS();
    if (NS_FAILED(rv)) {
      LOG(("WebSocket OnProxyAvailable [%p] DNS lookup failed\n", this));
      // call DNS callback directly without DNS resolver
      OnLookupComplete(nullptr, nullptr, NS_ERROR_FAILURE);
    }
  }

  // notify listener of OnProxyAvailable
  RefPtr<BaseWebSocketChannel::ListenerAndContextContainer> listener =
      GetListenerMT();
  LOG(("WebSocketChannel::OnProxyAvailable Notifying Listener %p",
       listener ? listener->mListener.get() : nullptr));
  if (!listener) {
    return NS_OK;
  }
  nsresult rv;
  nsCOMPtr<nsIProtocolProxyCallback> ppc(
      do_QueryInterface(listener->mListener, &rv));
  if (NS_SUCCEEDED(rv)) {
    rv = ppc->OnProxyAvailable(aRequest, aChannel, pi, status);
    if (NS_FAILED(rv)) {
      LOG(
          ("WebSocketChannel::OnProxyAvailable notify"
           " failed with error 0x%08" PRIx32,
           static_cast<uint32_t>(rv)));
    }
  }

  return NS_OK;
}

// nsIInterfaceRequestor

NS_IMETHODIMP
WebSocketChannel::GetInterface(const nsIID& iid, void** result) {
  LOG(("WebSocketChannel::GetInterface() %p\n", this));

  if (iid.Equals(NS_GET_IID(nsIChannelEventSink))) {
    return QueryInterface(iid, result);
  }

  if (mCallbacks) return mCallbacks->GetInterface(iid, result);

  return NS_ERROR_NO_INTERFACE;
}

// nsIChannelEventSink

NS_IMETHODIMP
WebSocketChannel::AsyncOnChannelRedirect(
    nsIChannel* oldChannel, nsIChannel* newChannel, uint32_t flags,
    nsIAsyncVerifyRedirectCallback* callback) {
  LOG(("WebSocketChannel::AsyncOnChannelRedirect() %p\n", this));

  MOZ_ASSERT(NS_IsMainThread(), "not main thread");

  nsresult rv;

  nsCOMPtr<nsIURI> newuri;
  rv = newChannel->GetURI(getter_AddRefs(newuri));
  NS_ENSURE_SUCCESS(rv, rv);

  // newuri is expected to be http or https
  bool newuriIsHttps = newuri->SchemeIs("https");

  // allow insecure->secure redirects for HTTP Strict Transport Security (from
  // ws://FOO to https://FOO (mapped to wss://FOO)
  if (!(flags & (nsIChannelEventSink::REDIRECT_INTERNAL |
                 nsIChannelEventSink::REDIRECT_STS_UPGRADE))) {
    nsAutoCString newSpec;
    rv = newuri->GetSpec(newSpec);
    NS_ENSURE_SUCCESS(rv, rv);

    LOG(("WebSocketChannel: Redirect to %s denied by configuration\n",
         newSpec.get()));
    return NS_ERROR_FAILURE;
  }

  if (mEncrypted && !newuriIsHttps) {
    nsAutoCString spec;
    if (NS_SUCCEEDED(newuri->GetSpec(spec))) {
      LOG(("WebSocketChannel: Redirect to %s violates encryption rule\n",
           spec.get()));
    }
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIHttpChannel> newHttpChannel = do_QueryInterface(newChannel, &rv);
  if (NS_FAILED(rv)) {
    LOG(("WebSocketChannel: Redirect could not QI to HTTP\n"));
    return rv;
  }

  nsCOMPtr<nsIHttpChannelInternal> newUpgradeChannel =
      do_QueryInterface(newChannel, &rv);

  if (NS_FAILED(rv)) {
    LOG(("WebSocketChannel: Redirect could not QI to HTTP Upgrade\n"));
    return rv;
  }

  // The redirect is likely OK

  newChannel->SetNotificationCallbacks(this);

  mEncrypted = newuriIsHttps;
  rv = NS_MutateURI(newuri)
           .SetScheme(mEncrypted ? "wss"_ns : "ws"_ns)
           .Finalize(mURI);

  if (NS_FAILED(rv)) {
    LOG(("WebSocketChannel: Could not set the proper scheme\n"));
    return rv;
  }

  mHttpChannel = newHttpChannel;
  mChannel = newUpgradeChannel;
  rv = SetupRequest();
  if (NS_FAILED(rv)) {
    LOG(("WebSocketChannel: Redirect could not SetupRequest()\n"));
    return rv;
  }

  // Redirected-to URI may need to be delayed by 1-connecting-per-host and
  // delay-after-fail algorithms.  So hold off calling OnRedirectVerifyCallback
  // until BeginOpen, when we know it's OK to proceed with new channel.
  mRedirectCallback = callback;

  // Mark old channel as successfully connected so we'll clear any FailDelay
  // associated with the old URI.  Note: no need to also call OnStopSession:
  // it's a no-op for successful, already-connected channels.
  nsWSAdmissionManager::OnConnected(this);

  // ApplyForAdmission as if we were starting from fresh...
  mAddress.Truncate();
  mOpenedHttpChannel = false;
  rv = ApplyForAdmission();
  if (NS_FAILED(rv)) {
    LOG(("WebSocketChannel: Redirect failed due to DNS failure\n"));
    mRedirectCallback = nullptr;
    return rv;
  }

  return NS_OK;
}

// nsITimerCallback

NS_IMETHODIMP
WebSocketChannel::Notify(nsITimer* timer) {
  LOG(("WebSocketChannel::Notify() %p [%p]\n", this, timer));

  if (timer == mCloseTimer) {
    MOZ_ASSERT(mClientClosed, "Close Timeout without local close");
    MOZ_ASSERT(mIOThread->IsOnCurrentThread(), "not on right thread");

    mCloseTimer = nullptr;
    if (mStopped || mServerClosed) { /* no longer relevant */
      return NS_OK;
    }

    LOG(("WebSocketChannel:: Expecting Server Close - Timed Out\n"));
    AbortSession(NS_ERROR_NET_TIMEOUT_EXTERNAL);
  } else if (timer == mOpenTimer) {
    MOZ_ASSERT(NS_IsMainThread(), "not main thread");

    mOpenTimer = nullptr;
    LOG(("WebSocketChannel:: Connection Timed Out\n"));
    if (mStopped || mServerClosed) { /* no longer relevant */
      return NS_OK;
    }

    AbortSession(NS_ERROR_NET_TIMEOUT_EXTERNAL);
    MOZ_PUSH_IGNORE_THREAD_SAFETY
    // mReconnectDelayTimer is only modified on MainThread, we can read it
    // without a lock, but ONLY if we're on MainThread!   And if we're not
    // on MainThread, it can't be mReconnectDelayTimer
  } else if (NS_IsMainThread() && timer == mReconnectDelayTimer) {
    MOZ_POP_THREAD_SAFETY
    MOZ_ASSERT(mConnecting == CONNECTING_DELAYED,
               "woke up from delay w/o being delayed?");

    {
      MutexAutoLock lock(mMutex);
      mReconnectDelayTimer = nullptr;
    }
    LOG(("WebSocketChannel: connecting [this=%p] after reconnect delay", this));
    BeginOpen(false);
  } else if (timer == mPingTimer) {
    MOZ_ASSERT(mIOThread->IsOnCurrentThread(), "not on right thread");

    if (mClientClosed || mServerClosed || mRequestedClose) {
      // no point in worrying about ping now
      mPingTimer = nullptr;
      return NS_OK;
    }

    if (!mPingOutstanding) {
      // Ping interval must be non-null or PING was forced by OnNetworkChanged()
      MOZ_ASSERT(mPingInterval || mPingForced);
      LOG(("nsWebSocketChannel:: Generating Ping\n"));
      mPingOutstanding = 1;
      mPingForced = false;
      mPingTimer->InitWithCallback(this, mPingResponseTimeout,
                                   nsITimer::TYPE_ONE_SHOT);
      GeneratePing();
    } else {
      LOG(("nsWebSocketChannel:: Timed out Ping\n"));
      mPingTimer = nullptr;
      AbortSession(NS_ERROR_NET_TIMEOUT_EXTERNAL);
    }
  } else if (timer == mLingeringCloseTimer) {
    LOG(("WebSocketChannel:: Lingering Close Timer"));
    CleanupConnection();
  } else {
    MOZ_ASSERT(0, "Unknown Timer");
  }

  return NS_OK;
}

// nsINamed

NS_IMETHODIMP
WebSocketChannel::GetName(nsACString& aName) {
  aName.AssignLiteral("WebSocketChannel");
  return NS_OK;
}

// nsIWebSocketChannel

NS_IMETHODIMP
WebSocketChannel::GetSecurityInfo(nsITransportSecurityInfo** aSecurityInfo) {
  LOG(("WebSocketChannel::GetSecurityInfo() %p\n", this));
  MOZ_ASSERT(NS_IsMainThread(), "not main thread");

  *aSecurityInfo = nullptr;

  if (mTransport) {
    nsCOMPtr<nsITLSSocketControl> tlsSocketControl;
    nsresult rv =
        mTransport->GetTlsSocketControl(getter_AddRefs(tlsSocketControl));
    if (NS_FAILED(rv)) {
      return rv;
    }
    nsCOMPtr<nsITransportSecurityInfo> securityInfo(
        do_QueryInterface(tlsSocketControl));
    if (securityInfo) {
      securityInfo.forget(aSecurityInfo);
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
WebSocketChannel::AsyncOpen(nsIURI* aURI, const nsACString& aOrigin,
                            JS::Handle<JS::Value> aOriginAttributes,
                            uint64_t aInnerWindowID,
                            nsIWebSocketListener* aListener,
                            nsISupports* aContext, JSContext* aCx) {
  OriginAttributes attrs;
  if (!aOriginAttributes.isObject() || !attrs.Init(aCx, aOriginAttributes)) {
    return NS_ERROR_INVALID_ARG;
  }
  return AsyncOpenNative(aURI, aOrigin, attrs, aInnerWindowID, aListener,
                         aContext);
}

NS_IMETHODIMP
WebSocketChannel::AsyncOpenNative(nsIURI* aURI, const nsACString& aOrigin,
                                  const OriginAttributes& aOriginAttributes,
                                  uint64_t aInnerWindowID,
                                  nsIWebSocketListener* aListener,
                                  nsISupports* aContext) {
  LOG(("WebSocketChannel::AsyncOpen() %p\n", this));

  aOriginAttributes.CreateSuffix(mOriginSuffix);

  if (!NS_IsMainThread()) {
    MOZ_ASSERT(false, "not main thread");
    LOG(("WebSocketChannel::AsyncOpen() called off the main thread"));
    return NS_ERROR_UNEXPECTED;
  }

  if ((!aURI && !mIsServerSide) || !aListener) {
    LOG(("WebSocketChannel::AsyncOpen() Uri or Listener null"));
    return NS_ERROR_UNEXPECTED;
  }

  {
    MutexAutoLock lock(mMutex);
    if (mWasOpened || mListenerMT) return NS_ERROR_ALREADY_OPENED;
  }

  nsresult rv;

  // Ensure target thread is set if RetargetDeliveryTo isn't called
  {
    auto lock = mTargetThread.Lock();
    if (!lock.ref()) {
      lock.ref() = GetMainThreadSerialEventTarget();
    }
  }

  mIOThread = mozilla::components::SocketTransport::Service(&rv);
  if (NS_FAILED(rv)) {
    NS_WARNING("unable to continue without socket transport service");
    return rv;
  }

  nsCOMPtr<nsIPrefBranch> prefService;
  prefService = mozilla::components::Preferences::Service();

  if (prefService) {
    int32_t intpref;
    rv =
        prefService->GetIntPref("network.websocket.max-message-size", &intpref);
    if (NS_SUCCEEDED(rv)) {
      mMaxMessageSize = std::clamp(intpref, 1024, INT32_MAX);
    }
    rv = prefService->GetIntPref("network.websocket.timeout.close", &intpref);
    if (NS_SUCCEEDED(rv)) {
      mCloseTimeout = std::clamp(intpref, 1, 1800) * 1000;
    }
    rv = prefService->GetIntPref("network.websocket.timeout.open", &intpref);
    if (NS_SUCCEEDED(rv)) {
      mOpenTimeout = std::clamp(intpref, 1, 1800) * 1000;
    }
    rv = prefService->GetIntPref("network.websocket.timeout.ping.request",
                                 &intpref);
    if (NS_SUCCEEDED(rv) && !mClientSetPingInterval) {
      mPingInterval = std::clamp(intpref, 0, 86400) * 1000;
    }
    rv = prefService->GetIntPref("network.websocket.timeout.ping.response",
                                 &intpref);
    if (NS_SUCCEEDED(rv) && !mClientSetPingTimeout) {
      mPingResponseTimeout = std::clamp(intpref, 1, 3600) * 1000;
    }
    rv = prefService->GetIntPref("network.websocket.max-connections", &intpref);
    if (NS_SUCCEEDED(rv)) {
      mMaxConcurrentConnections = std::clamp(intpref, 1, 0xffff);
    }
  }

  int32_t sessionCount = -1;
  nsWSAdmissionManager::GetSessionCount(sessionCount);
  if (sessionCount >= 0) {
    LOG(("WebSocketChannel::AsyncOpen %p sessionCount=%d max=%d\n", this,
         sessionCount, mMaxConcurrentConnections));
  }

  if (sessionCount >= mMaxConcurrentConnections) {
    LOG(("WebSocketChannel: max concurrency %d exceeded (%d)",
         mMaxConcurrentConnections, sessionCount));

    // WebSocket connections are expected to be long lived, so return
    // an error here instead of queueing
    return NS_ERROR_SOCKET_CREATE_FAILED;
  }

  mInnerWindowID = aInnerWindowID;
  mOriginalURI = aURI;
  mURI = mOriginalURI;
  mOrigin = aOrigin;

  if (mIsServerSide) {
    // IncrementSessionCount();
    mWasOpened = 1;
    {
      MutexAutoLock lock(mMutex);
      mListenerMT =
          MakeRefPtr<ListenerAndContextContainer>(aListener, aContext);
    }
    rv = mServerTransportProvider->SetListener(this);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    mServerTransportProvider = nullptr;

    return NS_OK;
  }

  mURI->GetHostPort(mHost);

  mRandomGenerator = mozilla::components::RandomGenerator::Service(&rv);
  if (NS_FAILED(rv)) {
    NS_WARNING("unable to continue without random number generator");
    return rv;
  }

  nsCOMPtr<nsIURI> localURI;
  nsCOMPtr<nsIChannel> localChannel;

  LOG(("WebSocketChannel::AsyncOpen uri=%s", mURI->GetSpecOrDefault().get()));

  rv = NS_MutateURI(mURI)
           .SetScheme(mEncrypted ? "https"_ns : "http"_ns)
           .Finalize(localURI);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIIOService> ioService;
  ioService = mozilla::components::IO::Service(&rv);
  if (NS_FAILED(rv)) {
    NS_WARNING("unable to continue without io service");
    return rv;
  }

  rv = ioService->NewChannelFromURIWithProxyFlagsAndLoadInfo(
      localURI, mURI,
      nsIProtocolProxyService::RESOLVE_PREFER_SOCKS_PROXY |
          nsIProtocolProxyService::RESOLVE_PREFER_HTTPS_PROXY |
          nsIProtocolProxyService::RESOLVE_ALWAYS_TUNNEL,
      mLoadInfo, getter_AddRefs(localChannel));
  NS_ENSURE_SUCCESS(rv, rv);

  // Pass most GetInterface() requests through to our instantiator, but handle
  // nsIChannelEventSink in this object in order to deal with redirects
  localChannel->SetNotificationCallbacks(this);

  class MOZ_STACK_CLASS CleanUpOnFailure {
   public:
    explicit CleanUpOnFailure(WebSocketChannel* aWebSocketChannel)
        : mWebSocketChannel(aWebSocketChannel) {}

    ~CleanUpOnFailure() {
      if (!mWebSocketChannel->mWasOpened) {
        mWebSocketChannel->mChannel = nullptr;
        mWebSocketChannel->mHttpChannel = nullptr;
      }
    }

    WebSocketChannel* mWebSocketChannel;
  };

  CleanUpOnFailure cuof(this);

  mChannel = do_QueryInterface(localChannel, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  mHttpChannel = do_QueryInterface(localChannel, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = SetupRequest();
  if (NS_FAILED(rv)) return rv;

  mPrivateBrowsing = NS_UsePrivateBrowsing(localChannel);

  if (mConnectionLogService && !mPrivateBrowsing) {
    mConnectionLogService->AddHost(mHost, mSerial,
                                   BaseWebSocketChannel::mEncrypted);
  }

  rv = ApplyForAdmission();
  if (NS_FAILED(rv)) return rv;

  // Register for prefs change notifications
  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();
  if (!observerService) {
    NS_WARNING("failed to get observer service");
    return NS_ERROR_FAILURE;
  }

  rv = observerService->AddObserver(this, NS_NETWORK_LINK_TOPIC, false);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // Only set these if the open was successful:
  //
  mWasOpened = 1;
  {
    MutexAutoLock lock(mMutex);
    mListenerMT = MakeRefPtr<ListenerAndContextContainer>(aListener, aContext);
  }
  IncrementSessionCount();

  return rv;
}

NS_IMETHODIMP
WebSocketChannel::Close(uint16_t code, const nsACString& reason) {
  LOG(("WebSocketChannel::Close() %p\n", this));
  MOZ_ASSERT(NS_IsMainThread(), "not main thread");

  {
    MutexAutoLock lock(mMutex);

    if (mRequestedClose) {
      return NS_OK;
    }

    if (mStopped) {
      return NS_ERROR_NOT_AVAILABLE;
    }

    // The API requires the UTF-8 string to be 123 or less bytes
    if (reason.Length() > 123) return NS_ERROR_ILLEGAL_VALUE;

    mRequestedClose = true;
    mScriptCloseReason = reason;
    mScriptCloseCode = code;

    if (mDataStarted) {
      return mIOThread->Dispatch(
          new OutboundEnqueuer(this,
                               new OutboundMessage(kMsgTypeFin, VoidCString())),
          nsIEventTarget::DISPATCH_NORMAL);
    }

    mStopped = true;
  }

  nsresult rv;
  if (code == CLOSE_GOING_AWAY) {
    // Not an error: for example, tab has closed or navigated away
    LOG(("WebSocketChannel::Close() GOING_AWAY without transport."));
    rv = NS_OK;
  } else {
    LOG(("WebSocketChannel::Close() without transport - error."));
    rv = NS_ERROR_NOT_CONNECTED;
  }

  DoStopSession(rv);
  return rv;
}

NS_IMETHODIMP
WebSocketChannel::SendMsg(const nsACString& aMsg) {
  LOG(("WebSocketChannel::SendMsg() %p\n", this));

  return SendMsgCommon(aMsg, false, aMsg.Length());
}

NS_IMETHODIMP
WebSocketChannel::SendBinaryMsg(const nsACString& aMsg) {
  LOG(("WebSocketChannel::SendBinaryMsg() %p len=%zu\n", this, aMsg.Length()));
  return SendMsgCommon(aMsg, true, aMsg.Length());
}

NS_IMETHODIMP
WebSocketChannel::SendBinaryStream(nsIInputStream* aStream, uint32_t aLength) {
  LOG(("WebSocketChannel::SendBinaryStream() %p\n", this));

  return SendMsgCommon(VoidCString(), true, aLength, aStream);
}

nsresult WebSocketChannel::SendMsgCommon(const nsACString& aMsg, bool aIsBinary,
                                         uint32_t aLength,
                                         nsIInputStream* aStream) {
  MOZ_ASSERT(IsOnTargetThread(), "not target thread");

  if (!mDataStarted) {
    LOG(("WebSocketChannel:: Error: data not started yet\n"));
    return NS_ERROR_UNEXPECTED;
  }

  if (mRequestedClose) {
    LOG(("WebSocketChannel:: Error: send when closed\n"));
    return NS_ERROR_UNEXPECTED;
  }

  if (mStopped) {
    LOG(("WebSocketChannel:: Error: send when stopped\n"));
    return NS_ERROR_NOT_CONNECTED;
  }

  MOZ_ASSERT(mMaxMessageSize >= 0, "max message size negative");
  if (aLength > static_cast<uint32_t>(mMaxMessageSize)) {
    LOG(("WebSocketChannel:: Error: message too big\n"));
    return NS_ERROR_FILE_TOO_BIG;
  }

  if (mConnectionLogService && !mPrivateBrowsing) {
    mConnectionLogService->NewMsgSent(mHost, mSerial, aLength);
    LOG(("Added new msg sent for %s", mHost.get()));
  }

  return mIOThread->Dispatch(
      aStream
          ? new OutboundEnqueuer(this, new OutboundMessage(aStream, aLength))
          : new OutboundEnqueuer(
                this,
                new OutboundMessage(
                    aIsBinary ? kMsgTypeBinaryString : kMsgTypeString, aMsg)),
      nsIEventTarget::DISPATCH_NORMAL);
}

// nsIHttpUpgradeListener

NS_IMETHODIMP
WebSocketChannel::OnTransportAvailable(nsISocketTransport* aTransport,
                                       nsIAsyncInputStream* aSocketIn,
                                       nsIAsyncOutputStream* aSocketOut) {
  if (!NS_IsMainThread()) {
    return NS_DispatchToMainThread(
        new CallOnTransportAvailable(this, aTransport, aSocketIn, aSocketOut));
  }

  LOG(("WebSocketChannel::OnTransportAvailable %p [%p %p %p] rcvdonstart=%d\n",
       this, aTransport, aSocketIn, aSocketOut, mGotUpgradeOK));

  if (mStopped) {
    LOG(("WebSocketChannel::OnTransportAvailable: Already stopped"));
    return NS_OK;
  }

  MOZ_ASSERT(NS_IsMainThread(), "not main thread");
  MOZ_ASSERT(!mRecvdHttpUpgradeTransport, "OTA duplicated");
  MOZ_ASSERT(aSocketIn, "OTA with invalid socketIn");

  mTransport = aTransport;
  mSocketIn = aSocketIn;
  mSocketOut = aSocketOut;

  nsresult rv;
  rv = mTransport->SetEventSink(nullptr, nullptr);
  if (NS_WARN_IF(NS_FAILED(rv))) return rv;
  rv = mTransport->SetSecurityCallbacks(this);
  if (NS_WARN_IF(NS_FAILED(rv))) return rv;

  mRecvdHttpUpgradeTransport = 1;
  if (mGotUpgradeOK) {
    // We're now done CONNECTING, which means we can now open another,
    // perhaps parallel, connection to the same host if one
    // is pending
    nsWSAdmissionManager::OnConnected(this);

    return CallStartWebsocketData();
  }

  if (mIsServerSide) {
    if (!mNegotiatedExtensions.IsEmpty()) {
      bool clientNoContextTakeover;
      bool serverNoContextTakeover;
      int32_t clientMaxWindowBits;
      int32_t serverMaxWindowBits;

      rv = ParseWebSocketExtension(
          mNegotiatedExtensions, eParseServerSide, clientNoContextTakeover,
          serverNoContextTakeover, clientMaxWindowBits, serverMaxWindowBits);
      MOZ_RELEASE_ASSERT(NS_SUCCEEDED(rv), "illegal value provided by server");

      if (clientMaxWindowBits == -1) {
        clientMaxWindowBits = 15;
      }
      if (serverMaxWindowBits == -1) {
        serverMaxWindowBits = 15;
      }

      MutexAutoLock lock(mCompressorMutex);
      mPMCECompressor = std::make_unique<PMCECompression>(
          serverNoContextTakeover, serverMaxWindowBits, clientMaxWindowBits);
      if (mPMCECompressor->Active()) {
        LOG(
            ("WebSocketChannel::OnTransportAvailable: PMCE negotiated, %susing "
             "context takeover, serverMaxWindowBits=%d, "
             "clientMaxWindowBits=%d\n",
             serverNoContextTakeover ? "NOT " : "", serverMaxWindowBits,
             clientMaxWindowBits));

        mNegotiatedExtensions = "permessage-deflate";
      } else {
        LOG(
            ("WebSocketChannel::OnTransportAvailable: Cannot init PMCE "
             "compression object\n"));
        mPMCECompressor = nullptr;
        AbortSession(NS_ERROR_UNEXPECTED);
        return NS_ERROR_UNEXPECTED;
      }
    }

    return CallStartWebsocketData();
  }

  return NS_OK;
}

NS_IMETHODIMP
WebSocketChannel::OnUpgradeFailed(nsresult aErrorCode) {
  if (!NS_IsMainThread()) {
    return NS_DispatchToMainThread(NewRunnableMethod<nsresult>(
        "net::WebSocketChannel::OnUpgradeFailed", this,
        &WebSocketChannel::OnUpgradeFailed, aErrorCode));
  }

  LOG(("WebSocketChannel::OnUpgradeFailed() %p [aErrorCode %" PRIx32 "]", this,
       static_cast<uint32_t>(aErrorCode)));

  if (mStopped) {
    LOG(("WebSocketChannel::OnUpgradeFailed: Already stopped"));
    return NS_OK;
  }

  MOZ_ASSERT(!mRecvdHttpUpgradeTransport, "OTA already called");

  AbortSession(aErrorCode);
  return NS_OK;
}

// nsIRequestObserver (from nsIStreamListener)

NS_IMETHODIMP
WebSocketChannel::OnStartRequest(nsIRequest* aRequest) {
  LOG(("WebSocketChannel::OnStartRequest(): %p [%p %p] recvdhttpupgrade=%d\n",
       this, aRequest, mHttpChannel.get(), mRecvdHttpUpgradeTransport));
  MOZ_ASSERT(NS_IsMainThread(), "not main thread");
  MOZ_ASSERT(!mGotUpgradeOK, "OTA duplicated");

  if (mStopped) {
    LOG(("WebSocketChannel::OnStartRequest: Channel Already Done\n"));
    AbortSession(NS_ERROR_WEBSOCKET_CONNECTION_REFUSED);
    return NS_ERROR_WEBSOCKET_CONNECTION_REFUSED;
  }

  nsresult rv;
  uint32_t status;
  char *val, *token;

  rv = mHttpChannel->GetResponseStatus(&status);
  if (NS_FAILED(rv)) {
    nsresult httpStatus;
    rv = NS_ERROR_WEBSOCKET_CONNECTION_REFUSED;

    // If we failed to connect due to unsuccessful TLS handshake, we must
    // propagate a specific error to mozilla::dom::WebSocketImpl so it can set
    // status code to 1015. Otherwise return
    // NS_ERROR_WEBSOCKET_CONNECTION_REFUSED.
    if (NS_SUCCEEDED(mHttpChannel->GetStatus(&httpStatus))) {
      uint32_t errorClass;
      nsCOMPtr<nsINSSErrorsService> errSvc;
      errSvc = mozilla::components::NSSErrors::Service();
      // If GetErrorClass succeeds httpStatus is TLS related failure.
      if (errSvc &&
          NS_SUCCEEDED(errSvc->GetErrorClass(httpStatus, &errorClass))) {
        rv = NS_ERROR_NET_INADEQUATE_SECURITY;
      }
    }

    LOG(("WebSocketChannel::OnStartRequest: No HTTP Response\n"));
    AbortSession(rv);
    return rv;
  }

  LOG(("WebSocketChannel::OnStartRequest: HTTP status %d\n", status));
  nsCOMPtr<nsIHttpChannelInternal> internalChannel =
      do_QueryInterface(mHttpChannel);
  uint32_t versionMajor, versionMinor;
  rv = internalChannel->GetResponseVersion(&versionMajor, &versionMinor);
  if (NS_FAILED(rv) ||
      !((versionMajor == 1 && versionMinor != 0) || versionMajor == 2) ||
      (versionMajor == 1 && status != 101) ||
      (versionMajor == 2 && status != 200)) {
    AbortSession(NS_ERROR_WEBSOCKET_CONNECTION_REFUSED);
    return NS_ERROR_WEBSOCKET_CONNECTION_REFUSED;
  }

  if (versionMajor == 1) {
    // These are only present on http/1.x websocket upgrades
    nsAutoCString respUpgrade;
    rv = mHttpChannel->GetResponseHeader("Upgrade"_ns, respUpgrade);

    if (NS_SUCCEEDED(rv)) {
      rv = NS_ERROR_ILLEGAL_VALUE;
      if (!respUpgrade.IsEmpty()) {
        val = respUpgrade.BeginWriting();
        while ((token = nsCRT::strtok(val, ", \t", &val))) {
          if (nsCRT::strcasecmp(token, "Websocket") == 0) {
            rv = NS_OK;
            break;
          }
        }
      }
    }

    if (NS_FAILED(rv)) {
      LOG(
          ("WebSocketChannel::OnStartRequest: "
           "HTTP response header Upgrade: websocket not found\n"));
      AbortSession(NS_ERROR_ILLEGAL_VALUE);
      return rv;
    }

    nsAutoCString respConnection;
    rv = mHttpChannel->GetResponseHeader("Connection"_ns, respConnection);

    if (NS_SUCCEEDED(rv)) {
      rv = NS_ERROR_ILLEGAL_VALUE;
      if (!respConnection.IsEmpty()) {
        val = respConnection.BeginWriting();
        while ((token = nsCRT::strtok(val, ", \t", &val))) {
          if (nsCRT::strcasecmp(token, "Upgrade") == 0) {
            rv = NS_OK;
            break;
          }
        }
      }
    }

    if (NS_FAILED(rv)) {
      LOG(
          ("WebSocketChannel::OnStartRequest: "
           "HTTP response header 'Connection: Upgrade' not found\n"));
      AbortSession(NS_ERROR_ILLEGAL_VALUE);
      return rv;
    }

    nsAutoCString respAccept;
    rv = mHttpChannel->GetResponseHeader("Sec-WebSocket-Accept"_ns, respAccept);

    if (NS_FAILED(rv) || respAccept.IsEmpty() ||
        !respAccept.Equals(mHashedSecret)) {
      LOG(
          ("WebSocketChannel::OnStartRequest: "
           "HTTP response header Sec-WebSocket-Accept check failed\n"));
      LOG(("WebSocketChannel::OnStartRequest: Expected %s received %s\n",
           mHashedSecret.get(), respAccept.get()));
#ifdef FUZZING
      if (NS_FAILED(rv) || respAccept.IsEmpty()) {
#endif
        AbortSession(NS_ERROR_ILLEGAL_VALUE);
        return NS_ERROR_ILLEGAL_VALUE;
#ifdef FUZZING
      }
#endif
    }
  }

  // If we sent a sub protocol header, verify the response matches.
  // If response contains protocol that was not in request, fail.
  // If response contained no protocol header, set to "" so the protocol
  // attribute of the WebSocket JS object reflects that
  if (!mProtocol.IsEmpty()) {
    nsAutoCString respProtocol;
    rv = mHttpChannel->GetResponseHeader("Sec-WebSocket-Protocol"_ns,
                                         respProtocol);
    if (NS_SUCCEEDED(rv)) {
      rv = NS_ERROR_ILLEGAL_VALUE;
      val = mProtocol.BeginWriting();
      while ((token = nsCRT::strtok(val, ", \t", &val))) {
        if (strcmp(token, respProtocol.get()) == 0) {
          rv = NS_OK;
          break;
        }
      }

      if (NS_SUCCEEDED(rv)) {
        LOG(("WebsocketChannel::OnStartRequest: subprotocol %s confirmed",
             respProtocol.get()));
        mProtocol = std::move(respProtocol);
      } else {
        LOG(
            ("WebsocketChannel::OnStartRequest: "
             "Server replied with non-matching subprotocol [%s]: aborting",
             respProtocol.get()));
        mProtocol.Truncate();
        AbortSession(NS_ERROR_ILLEGAL_VALUE);
        return NS_ERROR_ILLEGAL_VALUE;
      }
    } else {
      LOG(
          ("WebsocketChannel::OnStartRequest "
           "subprotocol [%s] not found - none returned",
           mProtocol.get()));
      mProtocol.Truncate();
    }
  }

  rv = HandleExtensions();
  if (NS_FAILED(rv)) return rv;

  // Update mEffectiveURL for off main thread URI access.
  nsCOMPtr<nsIURI> uri = mURI ? mURI : mOriginalURI;
  nsAutoCString spec;
  rv = uri->GetSpec(spec);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  CopyUTF8toUTF16(spec, mEffectiveURL);

  mGotUpgradeOK = 1;
  if (mRecvdHttpUpgradeTransport) {
    // We're now done CONNECTING, which means we can now open another,
    // perhaps parallel, connection to the same host if one
    // is pending
    nsWSAdmissionManager::OnConnected(this);

    return CallStartWebsocketData();
  }

  return NS_OK;
}

NS_IMETHODIMP
WebSocketChannel::OnStopRequest(nsIRequest* aRequest, nsresult aStatusCode) {
  LOG(("WebSocketChannel::OnStopRequest() %p [%p %p %" PRIx32 "]\n", this,
       aRequest, mHttpChannel.get(), static_cast<uint32_t>(aStatusCode)));
  MOZ_ASSERT(NS_IsMainThread(), "not main thread");

  // OnTransportAvailable won't be called if the request is stopped with
  // an error. Abort the session now instead of waiting for timeout.
  if (NS_FAILED(aStatusCode) && !mRecvdHttpUpgradeTransport) {
    AbortSession(aStatusCode);
  }

  ReportConnectionTelemetry(aStatusCode);

  // This is the end of the HTTP upgrade transaction, the
  // upgraded streams live on

  mChannel = nullptr;
  mHttpChannel = nullptr;
  mLoadGroup = nullptr;
  mCallbacks = nullptr;

  return NS_OK;
}

// nsIInputStreamCallback

NS_IMETHODIMP
WebSocketChannel::OnInputStreamReady(nsIAsyncInputStream* aStream) {
  LOG(("WebSocketChannel::OnInputStreamReady() %p\n", this));
  MOZ_DIAGNOSTIC_ASSERT(mIOThread->IsOnCurrentThread(), "not on right thread");

  if (!mSocketIn) {  // did we we clean up the socket after scheduling
                     // InputReady?
    return NS_OK;
  }

  // this is after the http upgrade - so we are speaking websockets
  char buffer[2048];
  uint32_t count;
  nsresult rv;

  do {
    rv = mSocketIn->Read((char*)buffer, sizeof(buffer), &count);
    LOG(("WebSocketChannel::OnInputStreamReady: read %u rv %" PRIx32 "\n",
         count, static_cast<uint32_t>(rv)));

    if (rv == NS_BASE_STREAM_WOULD_BLOCK) {
      mSocketIn->AsyncWait(this, 0, 0, mIOThread);
      return NS_OK;
    }

    if (NS_FAILED(rv)) {
      AbortSession(rv);
      return rv;
    }

    if (count == 0) {
      AbortSession(NS_BASE_STREAM_CLOSED);
      return NS_OK;
    }

    if (mStopped) {
      continue;
    }

    rv = ProcessInput((uint8_t*)buffer, count);
    if (NS_FAILED(rv)) {
      AbortSession(rv);
      return rv;
    }
  } while (NS_SUCCEEDED(rv) && mSocketIn);

  return NS_OK;
}

// nsIOutputStreamCallback

NS_IMETHODIMP
WebSocketChannel::OnOutputStreamReady(nsIAsyncOutputStream* aStream) {
  LOG(("WebSocketChannel::OnOutputStreamReady() %p\n", this));
  MOZ_DIAGNOSTIC_ASSERT(mIOThread->IsOnCurrentThread(), "not on right thread");
  nsresult rv;

  if (!mCurrentOut) PrimeNewOutgoingMessage();

  while (mCurrentOut && mSocketOut) {
    const char* sndBuf;
    uint32_t toSend;
    uint32_t amtSent;

    if (mHdrOut) {
      sndBuf = (const char*)mHdrOut;
      toSend = mHdrOutToSend;
      LOG(
          ("WebSocketChannel::OnOutputStreamReady: "
           "Try to send %u of hdr/copybreak\n",
           toSend));
    } else {
      sndBuf = (char*)mCurrentOut->BeginReading() + mCurrentOutSent;
      toSend = mCurrentOut->Length() - mCurrentOutSent;
      if (toSend > 0) {
        LOG(
            ("WebSocketChannel::OnOutputStreamReady [%p]: "
             "Try to send %u of data\n",
             this, toSend));
      }
    }

    if (toSend == 0) {
      amtSent = 0;
    } else {
      rv = mSocketOut->Write(sndBuf, toSend, &amtSent);
      LOG(("WebSocketChannel::OnOutputStreamReady [%p]: write %u rv %" PRIx32
           "\n",
           this, amtSent, static_cast<uint32_t>(rv)));

      if (rv == NS_BASE_STREAM_WOULD_BLOCK) {
        mSocketOut->AsyncWait(this, 0, 0, mIOThread);
        return NS_OK;
      }

      if (NS_FAILED(rv)) {
        AbortSession(rv);
        return NS_OK;
      }
    }

    if (mHdrOut) {
      if (amtSent == toSend) {
        mHdrOut = nullptr;
        mHdrOutToSend = 0;
      } else {
        mHdrOut += amtSent;
        mHdrOutToSend -= amtSent;
        mSocketOut->AsyncWait(this, 0, 0, mIOThread);
      }
    } else {
      if (amtSent == toSend) {
        if (RefPtr<BaseWebSocketChannel::ListenerAndContextContainer> listener =
                GetListenerMT()) {
          if (nsCOMPtr<nsIEventTarget> target = GetTargetThread()) {
            target->Dispatch(new CallAcknowledge(this, std::move(listener),
                                                 mCurrentOut->OrigLength()),
                             NS_DISPATCH_NORMAL);
          } else {
            return NS_ERROR_UNEXPECTED;
          }
        }
        DeleteCurrentOutGoingMessage();
        PrimeNewOutgoingMessage();
      } else {
        mCurrentOutSent += amtSent;
        mSocketOut->AsyncWait(this, 0, 0, mIOThread);
      }
    }
  }

  if (mReleaseOnTransmit) ReleaseSession();
  return NS_OK;
}

// nsIStreamListener

NS_IMETHODIMP
WebSocketChannel::OnDataAvailable(nsIRequest* aRequest,
                                  nsIInputStream* aInputStream,
                                  uint64_t aOffset, uint32_t aCount) {
  LOG(("WebSocketChannel::OnDataAvailable() %p [%p %p %p %" PRIu64 " %u]\n",
       this, aRequest, mHttpChannel.get(), aInputStream, aOffset, aCount));

  // This is the HTTP OnDataAvailable Method, which means this is http data in
  // response to the upgrade request and there should be no http response body
  // if the upgrade succeeded. This generally should be caught by a non 101
  // response code in OnStartRequest().. so we can ignore the data here

  LOG(("WebSocketChannel::OnDataAvailable: HTTP data unexpected len>=%u\n",
       aCount));

  return NS_OK;
}

}  // namespace mozilla::net

#undef CLOSE_GOING_AWAY
