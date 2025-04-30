/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_workers_WorkerLoadInfo_h
#define mozilla_dom_workers_WorkerLoadInfo_h

#include "mozilla/OriginAttributes.h"
#include "mozilla/StorageAccess.h"
#include "mozilla/OriginTrials.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/ChannelInfo.h"
#include "mozilla/net/NeckoChannelParams.h"
#include "mozilla/dom/ServiceWorkerRegistrationDescriptor.h"
#include "mozilla/dom/WorkerCommon.h"
#include "mozilla/dom/WorkerCSPContext.h"

#include "nsIInterfaceRequestor.h"
#include "nsILoadContext.h"
#include "nsIRequest.h"
#include "nsISupportsImpl.h"
#include "nsIWeakReferenceUtils.h"
#include "nsRFPService.h"
#include "nsTArray.h"

class nsIChannel;
class nsIContentSecurityPolicy;
class nsICookieJarSettings;
class nsILoadGroup;
class nsIPrincipal;
class nsIReferrerInfo;
class nsIRunnable;
class nsIScriptContext;
class nsIBrowserChild;
class nsIURI;
class nsPIDOMWindowInner;

namespace mozilla {

namespace ipc {
class PrincipalInfo;
}  // namespace ipc

namespace dom {

class WorkerPrivate;

struct WorkerLoadInfoData {
  // All of these should be released in
  // WorkerPrivateParent::ForgetMainThreadObjects.
  nsCOMPtr<nsIURI> mBaseURI;
  nsCOMPtr<nsIURI> mResolvedScriptURI;

  // This is the principal of the global (parent worker or a window) loading
  // the worker. It can be null if we are executing a ServiceWorker, otherwise,
  // except for data: URL, it must subsumes the worker principal.
  // If we load a data: URL, mPrincipal will be a null principal.
  nsCOMPtr<nsIPrincipal> mLoadingPrincipal;
  nsCOMPtr<nsIPrincipal> mPrincipal;
  nsCOMPtr<nsIPrincipal> mPartitionedPrincipal;

  // Taken from the parent context.
  nsCOMPtr<nsICookieJarSettings> mCookieJarSettings;

  // The CookieJarSettingsArgs of mCookieJarSettings.
  // This is specific for accessing on worker thread.
  net::CookieJarSettingsArgs mCookieJarSettingsArgs;

  nsCOMPtr<nsIScriptContext> mScriptContext;
  nsCOMPtr<nsPIDOMWindowInner> mWindow;
  nsCOMPtr<nsIContentSecurityPolicy> mCSP;
  UniquePtr<WorkerCSPContext> mCSPContext;

  nsCOMPtr<nsIChannel> mChannel;
  nsCOMPtr<nsILoadGroup> mLoadGroup;

  class InterfaceRequestor final : public nsIInterfaceRequestor {
    NS_DECL_ISUPPORTS

   public:
    InterfaceRequestor(nsIPrincipal* aPrincipal, nsILoadGroup* aLoadGroup);
    void MaybeAddBrowserChild(nsILoadGroup* aLoadGroup);
    NS_IMETHOD GetInterface(const nsIID& aIID, void** aSink) override;

    void SetOuterRequestor(nsIInterfaceRequestor* aOuterRequestor) {
      MOZ_ASSERT(!mOuterRequestor);
      MOZ_ASSERT(aOuterRequestor);
      mOuterRequestor = aOuterRequestor;
    }

   private:
    ~InterfaceRequestor() = default;

    already_AddRefed<nsIBrowserChild> GetAnyLiveBrowserChild();

    nsCOMPtr<nsILoadContext> mLoadContext;
    nsCOMPtr<nsIInterfaceRequestor> mOuterRequestor;

    // Array of weak references to nsIBrowserChild.  We do not want to keep
    // BrowserChild actors alive for long after their ActorDestroy() methods are
    // called.
    nsTArray<nsWeakPtr> mBrowserChildList;
  };

  // Only set if we have a custom overriden load group
  RefPtr<InterfaceRequestor> mInterfaceRequestor;

  UniquePtr<mozilla::ipc::PrincipalInfo> mPrincipalInfo;
  UniquePtr<mozilla::ipc::PrincipalInfo> mPartitionedPrincipalInfo;
  nsCString mDomain;

  nsString mServiceWorkerCacheName;
  Maybe<ServiceWorkerDescriptor> mServiceWorkerDescriptor;
  Maybe<ServiceWorkerRegistrationDescriptor>
      mServiceWorkerRegistrationDescriptor;
  Maybe<ClientInfo> mSourceInfo;

  Maybe<ServiceWorkerDescriptor> mParentController;

  nsID mAgentClusterId;

  ChannelInfo mChannelInfo;
  nsLoadFlags mLoadFlags;

  uint64_t mWindowID;
  uint64_t mAssociatedBrowsingContextID;

  nsCOMPtr<nsIReferrerInfo> mReferrerInfo;
  OriginTrials mTrials;
  bool mFromWindow;
  bool mXHRParamsAllowed;
  bool mWatchedByDevTools;
  StorageAccess mStorageAccess;
  bool mUseRegularPrincipal;
  bool mUsingStorageAccess;
  bool mServiceWorkersTestingInWindow;
  bool mShouldResistFingerprinting;
  Maybe<RFPTargetSet> mOverriddenFingerprintingSettings;
  OriginAttributes mOriginAttributes;
  bool mIsThirdPartyContext;
  bool mIsOn3PCBExceptionList;

  enum {
    eNotSet,
    eInsecureContext,
    eSecureContext,
  } mSecureContext;

  WorkerLoadInfoData();
  WorkerLoadInfoData(WorkerLoadInfoData&& aOther) = default;

  WorkerLoadInfoData& operator=(WorkerLoadInfoData&& aOther) = default;
};

struct WorkerLoadInfo : WorkerLoadInfoData {
  WorkerLoadInfo();
  WorkerLoadInfo(WorkerLoadInfo&& aOther) noexcept;
  ~WorkerLoadInfo();

  WorkerLoadInfo& operator=(WorkerLoadInfo&& aOther) = default;

  nsresult SetPrincipalsAndCSPOnMainThread(nsIPrincipal* aPrincipal,
                                           nsIPrincipal* aPartitionedPrincipal,
                                           nsILoadGroup* aLoadGroup,
                                           nsIContentSecurityPolicy* aCSP);

  nsresult GetPrincipalsAndLoadGroupFromChannel(
      nsIChannel* aChannel, nsIPrincipal** aPrincipalOut,
      nsIPrincipal** aPartitionedPrincipalOut, nsILoadGroup** aLoadGroupOut);

  nsresult SetPrincipalsAndCSPFromChannel(nsIChannel* aChannel);

  bool FinalChannelPrincipalIsValid(nsIChannel* aChannel);

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  bool PrincipalIsValid() const;

  bool PrincipalURIMatchesScriptURL();
#endif

  bool ProxyReleaseMainThreadObjects(WorkerPrivate* aWorkerPrivate);

  bool ProxyReleaseMainThreadObjects(
      WorkerPrivate* aWorkerPrivate,
      nsCOMPtr<nsILoadGroup>&& aLoadGroupToCancel);
};

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_workers_WorkerLoadInfo_h
