/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_fetchParent_h_
#define mozilla_dom_fetchParent_h_

#include "mozilla/Maybe.h"
#include "mozilla/MozPromise.h"
#include "mozilla/Mutex.h"
#include "mozilla/RefPtr.h"
#include "mozilla/dom/PFetchParent.h"
#include "mozilla/dom/SafeRefPtr.h"
#include "mozilla/net/NeckoChannelParams.h"
#include "nsCOMPtr.h"
#include "nsIContentSecurityPolicy.h"
#include "nsID.h"
#include "nsISerialEventTarget.h"
#include "nsString.h"
#include "nsTHashMap.h"

namespace mozilla::dom {

class ClientInfo;
class FetchServicePromises;
class InternalRequest;
class InternalResponse;
class ServiceWorkerDescriptor;

class FetchParent final : public PFetchParent {
  friend class PFetchParent;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(FetchParent, override);

  mozilla::ipc::IPCResult RecvFetchOp(FetchOpArgs&& aArgs);

  mozilla::ipc::IPCResult RecvAbortFetchOp(bool aForceAbort);

  FetchParent();

  static RefPtr<FetchParent> GetActorByID(const nsID& aID);

  void OnResponseAvailableInternal(SafeRefPtr<InternalResponse>&& aResponse);

  void OnResponseEnd(const ResponseEndArgs& aArgs);

  void OnDataAvailable();

  void OnFlushConsoleReport(
      const nsTArray<net::ConsoleReportCollected>& aReports);

  class FetchParentCSPEventListener final : public nsICSPEventListener {
   public:
    NS_DECL_THREADSAFE_ISUPPORTS
    NS_DECL_NSICSPEVENTLISTENER

    FetchParentCSPEventListener(const nsID& aActorID,
                                nsCOMPtr<nsISerialEventTarget> aEventTarget);

   private:
    ~FetchParentCSPEventListener() = default;

    nsID mActorID;
    nsCOMPtr<nsISerialEventTarget> mEventTarget;
  };

  nsICSPEventListener* GetCSPEventListener();

  void OnCSPViolationEvent(const nsAString& aJSON,
                           const nsAString& aReportGroupName);

  void OnReportPerformanceTiming(const ResponseTiming&& aTiming);

  void OnNotifyNetworkMonitorAlternateStack(uint64_t aChannelID);

 private:
  ~FetchParent();

  void ActorDestroy(ActorDestroyReason aReason) override;

  // The map of FetchParent and ID. Should only access in background thread.
  static nsTHashMap<nsIDHashKey, RefPtr<FetchParent>> sActorTable;

  // The unique ID of the FetchParent
  nsID mID;
  SafeRefPtr<InternalRequest> mRequest;
  RefPtr<FetchServicePromises> mResponsePromises;
  RefPtr<GenericPromise::Private> mPromise;
  nsCOMPtr<nsIPrincipal> mPrincipal;
  nsCString mWorkerScript;
  Maybe<ClientInfo> mClientInfo;
  Maybe<ServiceWorkerDescriptor> mController;
  Maybe<CookieJarSettingsArgs> mCookieJarSettings;
  nsCOMPtr<nsICSPEventListener> mCSPEventListener;
  bool mNeedOnDataAvailable{false};
  bool mHasCSPEventListener{false};
  bool mExtendForCSPEventListener{false};
  uint64_t mAssociatedBrowsingContextID{0};
  bool mIsThirdPartyContext{true};
  bool mIsWorkerFetch{false};
  bool mIsOn3PCBExceptionList{false};

  Atomic<bool> mIsDone{false};
  Atomic<bool> mActorDestroyed{false};
  Atomic<bool> mReceivedFetchOp{false};

  nsCOMPtr<nsISerialEventTarget> mBackgroundEventTarget;
};

}  // namespace mozilla::dom

#endif
