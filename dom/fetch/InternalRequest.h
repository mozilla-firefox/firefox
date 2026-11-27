/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_InternalRequest_h
#define mozilla_dom_InternalRequest_h

#include "mozilla/LoadTainting.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/HeadersBinding.h"
#include "mozilla/dom/InternalHeaders.h"
#include "mozilla/dom/InternalResponse.h"
#include "mozilla/dom/RequestBinding.h"
#include "mozilla/dom/SafeRefPtr.h"
#include "mozilla/net/NeckoChannelParams.h"
#include "nsIChannelEventSink.h"
#include "nsICookieJarSettings.h"
#include "nsIInputStream.h"
#include "nsISupportsImpl.h"
#include "nsISupportsPriority.h"
#include "nsIURIMutator.h"
#ifdef DEBUG
#  include "nsIURLParser.h"
#  include "nsNetCID.h"
#  include "nsServiceManagerUtils.h"
#endif

using mozilla::net::RedirectHistoryEntryInfo;

namespace mozilla {

namespace ipc {
class PBackgroundChild;
class PrincipalInfo;
}  // namespace ipc

namespace dom {

/*
 * The mapping of RequestDestination and nsContentPolicyType is currently as the
 * following.
 *
 * RequestDestination| nsContentPolicyType
 * ------------------+--------------------
 * "audio"           | TYPE_INTERNAL_AUDIO
 * "audioworklet"    | TYPE_INTERNAL_AUDIOWORKLET
 * "document"        | TYPE_DOCUMENT
 * "embed"           | TYPE_INTERNAL_EMBED
 * "font"            | TYPE_FONT, TYPE_INTERNAL_FONT_PRELOAD
 * "frame"           | TYPE_INTERNAL_FRAME
 * "iframe"          | TYPE_SUBDOCUMENT, TYPE_INTERNAL_IFRAME
 * "image"           | TYPE_INTERNAL_IMAGE, TYPE_INTERNAL_IMAGE_PRELOAD,
 *                   | TYPE_IMAGE, TYPE_INTERNAL_IMAGE_FAVICON, TYPE_IMAGESET,
 *                   | TYPE_INTERNAL_IMAGE_NOTIFICATION
 * "json"            | TYPE_JSON, TYPE_INTERNAL_JSON_PRELOAD
 * "manifest"        | TYPE_WEB_MANIFEST
 * "object"          | TYPE_INTERNAL_OBJECT, TYPE_OBJECT
 * "paintworklet"    | TYPE_INTERNAL_PAINTWORKLET
 * "report"          | TYPE_CSP_REPORT
 * "script"          | TYPE_INTERNAL_SCRIPT, TYPE_INTERNAL_SCRIPT_PRELOAD,
 *                   | TYPE_INTERNAL_MODULE, TYPE_INTERNAL_MODULE_PRELOAD,
 *                   | TYPE_SCRIPT,
 *                   | TYPE_INTERNAL_SERVICE_WORKER,
 *                   | TYPE_INTERNAL_WORKER_IMPORT_SCRIPTS,
 *                   | TYPE_INTERNAL_CHROMEUTILS_COMPILED_SCRIPT
 *                   | TYPE_INTERNAL_FRAME_MESSAGEMANAGER_SCRIPT
 * "sharedworker"    | TYPE_INTERNAL_SHARED_WORKER
 * "serviceworker"   | The spec lists this as a valid value for the enum,
 *                   | however it is impossible to observe a request with this
 *                   | destination value.
 * "style"           | TYPE_INTERNAL_STYLESHEET,
 *                   | TYPE_INTERNAL_STYLESHEET_PRELOAD,
 *                   | TYPE_STYLESHEET
 * "text"            | TYPE_TEXT, TYPE_INTERNAL_TEXT_PRELOAD
 * "track"           | TYPE_INTERNAL_TRACK
 * "video"           | TYPE_INTERNAL_VIDEO
 * "worker"          | TYPE_INTERNAL_WORKER, TYPE_INTERNAL_WORKER_STATIC_MODULE
 * "xslt"            | TYPE_XSLT
 * ""                | Default for everything else.
 *
 */

class IPCInternalRequest;
class Request;

#define kFETCH_CLIENT_REFERRER_STR "about:client"
class InternalRequest final : public AtomicSafeRefCounted<InternalRequest> {
  friend class Request;

 public:
  MOZ_DECLARE_REFCOUNTED_TYPENAME(InternalRequest)
  InternalRequest(NotNull<nsIURI*> aURL, const nsACString& aFragment);

  explicit InternalRequest(const IPCInternalRequest& aIPCRequest);

  void ToIPCInternalRequest(IPCInternalRequest* aIPCRequest,
                            mozilla::ipc::PBackgroundChild* aManager);

  SafeRefPtr<InternalRequest> Clone();

  void GetMethod(nsCString& aMethod) const { aMethod.Assign(mMethod); }

  void SetMethod(const nsACString& aMethod) { mMethod.Assign(aMethod); }

  bool HasSimpleMethod() const {
    return mMethod.LowerCaseEqualsASCII("get") ||
           mMethod.LowerCaseEqualsASCII("post") ||
           mMethod.LowerCaseEqualsASCII("head");
  }
  // GetURL should get the request's current url with fragment. A request has
  // an associated current url. It is based on the last fetch URL in request's
  // url list.
  already_AddRefed<nsIURI> GetURL() const {
    if (GetFragment().IsEmpty()) {
      return do_AddRef(GetURLWithoutFragment().get());
    }

    nsCOMPtr<nsIURI> url;
    MOZ_ALWAYS_SUCCEEDS(NS_GetURIWithNewRef(
        GetURLWithoutFragment(), "#"_ns + GetFragment(), getter_AddRefs(url)));
    return url.forget();
  }

  NotNull<nsIURI*> GetURLWithoutFragment() const {
    MOZ_RELEASE_ASSERT(!mURLList.IsEmpty(),
                       "Internal Request's urlList should not be empty.");

    return mURLList.LastElement();
  }

  // A safe guard for ensuring that request's URL is only allowed to be set in a
  // sw internal redirect.
  void SetURLForInternalRedirect(const uint32_t aFlag, NotNull<nsIURI*> aURL,
                                 const nsACString& aFragment) {
    // Only check in debug build to prevent it from being used unexpectedly.
    MOZ_ASSERT(aFlag & nsIChannelEventSink::REDIRECT_INTERNAL);

    return SetURL(aURL, aFragment);
  }

  // AddURL should append the url into url list.
  // Normally we strip the fragment from the URL in Request::Constructor and
  // pass the fragment as the second argument into it.
  // If a fragment is present in the URL it must be stripped and passed in
  // separately.
  void AddURL(NotNull<nsIURI*> aURL, const nsACString& aFragment) {
#ifdef DEBUG
    bool hasRef = false;
    MOZ_ALWAYS_SUCCEEDS(aURL->GetHasRef(&hasRef));
    MOZ_ASSERT(!hasRef);
#endif

    mURLList.AppendElement(aURL);

    mFragment.Assign(aFragment);
  }
  // Get the URL list without their fragments.
  const nsTArray<NotNull<RefPtr<nsIURI>>>& GetURLListWithoutFragment() const {
    return mURLList;
  }
  void GetReferrer(nsACString& aReferrer) const { aReferrer.Assign(mReferrer); }

  void SetReferrer(const nsACString& aReferrer) {
#ifdef DEBUG
    bool validReferrer = false;
    if (aReferrer.IsEmpty() ||
        aReferrer.EqualsLiteral(kFETCH_CLIENT_REFERRER_STR)) {
      validReferrer = true;
    } else {
      nsCOMPtr<nsIURLParser> parser = do_GetService(NS_STDURLPARSER_CONTRACTID);
      if (!parser) {
        NS_WARNING("Could not get parser to validate URL!");
      } else {
        uint32_t schemePos;
        int32_t schemeLen;
        uint32_t authorityPos;
        int32_t authorityLen;
        uint32_t pathPos;
        int32_t pathLen;

        nsresult rv = parser->ParseURL(
            aReferrer.BeginReading(), aReferrer.Length(), &schemePos,
            &schemeLen, &authorityPos, &authorityLen, &pathPos, &pathLen);
        if (NS_FAILED(rv)) {
          NS_WARNING("Invalid referrer URL!");
        } else if (schemeLen < 0 || authorityLen < 0) {
          NS_WARNING("Invalid referrer URL!");
        } else {
          validReferrer = true;
        }
      }
    }

    MOZ_ASSERT(validReferrer);
#endif

    mReferrer.Assign(aReferrer);
  }

  ReferrerPolicy ReferrerPolicy_() const { return mReferrerPolicy; }

  void SetReferrerPolicy(ReferrerPolicy aReferrerPolicy) {
    mReferrerPolicy = aReferrerPolicy;
  }

  void SetAssociatedBrowsingContextID(uint64_t aAssociatedBrowsingContextID) {
    mAssociatedBrowsingContextID = aAssociatedBrowsingContextID;
  }

  uint64_t AssociatedBrowsingContextID() const {
    return mAssociatedBrowsingContextID;
  }

  ReferrerPolicy GetEnvironmentReferrerPolicy() const {
    return mEnvironmentReferrerPolicy;
  }

  void SetEnvironmentReferrerPolicy(ReferrerPolicy aReferrerPolicy) {
    mEnvironmentReferrerPolicy = aReferrerPolicy;
  }

  bool SkipServiceWorker() const { return mSkipServiceWorker; }

  void SetSkipServiceWorker() { mSkipServiceWorker = true; }

  bool SkipWasmCaching() const { return mSkipWasmCaching; }

  void SetSkipWasmCaching() { mSkipWasmCaching = true; }

  bool IsSynchronous() const { return mSynchronous; }

  RequestMode Mode() const { return mMode; }

  void SetMode(RequestMode aMode) { mMode = aMode; }

  RequestCredentials GetCredentialsMode() const { return mCredentialsMode; }

  void SetCredentialsMode(RequestCredentials aCredentialsMode) {
    mCredentialsMode = aCredentialsMode;
  }

  LoadTainting GetResponseTainting() const { return mResponseTainting; }

  void MaybeIncreaseResponseTainting(LoadTainting aTainting) {
    if (aTainting > mResponseTainting && !mNeverTaint) {
      mResponseTainting = aTainting;
    }
  }

  RequestCache GetCacheMode() const { return mCacheMode; }

  void SetCacheMode(RequestCache aCacheMode) { mCacheMode = aCacheMode; }

  RequestRedirect GetRedirectMode() const { return mRedirectMode; }

  void SetRedirectMode(RequestRedirect aRedirectMode) {
    mRedirectMode = aRedirectMode;
  }

  RequestPriority GetPriorityMode() const { return mPriorityMode; }

  void SetPriorityMode(RequestPriority aPriorityMode) {
    mPriorityMode = aPriorityMode;
  }

  const nsString& GetIntegrity() const { return mIntegrity; }

  void SetIntegrity(const nsAString& aIntegrity) {
    mIntegrity.Assign(aIntegrity);
  }

  bool GetKeepalive() const { return mKeepalive; }

  void SetKeepalive(const bool aKeepalive) { mKeepalive = aKeepalive; }

  bool HasStreamBody() const { return mHasStreamBody; }

  void SetHasStreamBody(bool aHasStreamBody) {
    mHasStreamBody = aHasStreamBody;
  }

  bool MozErrors() const { return mMozErrors; }

  void SetMozErrors() { mMozErrors = true; }

  void SetTriggeringPrincipal(nsIPrincipal* aPrincipal) {
    mTriggeringPrincipalOverride = aPrincipal;
  }

  nsIPrincipal* GetTriggeringPrincipalOverride() {
    return mTriggeringPrincipalOverride;
  }

  void SetNeverTaint(bool aNeverTaint) { mNeverTaint = aNeverTaint; }

  bool GetNeverTaint() { return mNeverTaint; }

  void SetCookieJarSettings(nsICookieJarSettings* aCookieJarSettings) {
    mCookieJarSettings = aCookieJarSettings;
  }

  nsICookieJarSettings* GetCookieJarSettings() const {
    return mCookieJarSettings;
  }

  const nsCString& GetFragment() const { return mFragment; }

  nsContentPolicyType ContentPolicyType() const { return mContentPolicyType; }
  void SetContentPolicyType(nsContentPolicyType aContentPolicyType);

  void OverrideContentPolicyType(nsContentPolicyType aContentPolicyType);

  RequestDestination Destination() const {
    return MapContentPolicyTypeToRequestDestination(mContentPolicyType);
  }

  int32_t InternalPriority() const { return mInternalPriority; }
  void SetInternalPriority(int32_t aInternalPriority) {
    mInternalPriority = aInternalPriority;
  }

  bool UnsafeRequest() const { return mUnsafeRequest; }

  void SetUnsafeRequest() { mUnsafeRequest = true; }

  InternalHeaders* Headers() const { return mHeaders; }

  void SetHeaders(InternalHeaders* aHeaders) {
    MOZ_ASSERT(aHeaders);
    mHeaders = aHeaders;
  }

  void SetBody(nsIInputStream* aStream, int64_t aBodyLength) {
    // A request's body may not be reset once set.
    MOZ_ASSERT_IF(aStream, !mBodyStream);
    mBodyStream = aStream;
    mBodyLength = aBodyLength;
  }

  // Will return the original stream!
  // Use a tee or copy if you don't want to erase the original.
  void GetBody(nsIInputStream** aStream, int64_t* aBodyLength = nullptr) const {
    nsCOMPtr<nsIInputStream> s = mBodyStream;
    s.forget(aStream);

    if (aBodyLength) {
      *aBodyLength = mBodyLength;
    }
  }

  int64_t BodyLength() const { return mBodyLength; }

  void SetBodyBlobImpl(BlobImpl* aBlobImpl) { mBodyBlobImpl = aBlobImpl; }

  BlobImpl* BodyBlobImpl() const { return mBodyBlobImpl; }

  void SetBodyLocalPath(nsAString& aLocalPath) { mBodyLocalPath = aLocalPath; }

  const nsAString& BodyLocalPath() const { return mBodyLocalPath; }

  // The global is used as the client for the new object.
  SafeRefPtr<InternalRequest> GetRequestConstructorCopy(
      nsIGlobalObject* aGlobal, ErrorResult& aRv) const;

  bool IsNavigationRequest() const;

  bool IsWorkerRequest() const;

  bool IsClientRequest() const;

  void MaybeSkipCacheIfPerformingRevalidation();

  bool IsContentPolicyTypeOverridden() const {
    return mContentPolicyTypeOverridden;
  }

  static RequestMode MapChannelToRequestMode(nsIChannel* aChannel);

  static RequestCredentials MapChannelToRequestCredentials(
      nsIChannel* aChannel);

  // Takes ownership of the principal info.
  void SetPrincipalInfo(UniquePtr<mozilla::ipc::PrincipalInfo> aPrincipalInfo);
  const UniquePtr<mozilla::ipc::PrincipalInfo>& GetPrincipalInfo() const {
    return mPrincipalInfo;
  }

  const nsCString& GetPreferredAlternativeDataType() const {
    return mPreferredAlternativeDataType;
  }

  void SetPreferredAlternativeDataType(const nsACString& aDataType) {
    mPreferredAlternativeDataType = aDataType;
  }

  ~InternalRequest();

  InternalRequest(const InternalRequest& aOther) = delete;

  void SetEmbedderPolicy(nsILoadInfo::CrossOriginEmbedderPolicy aPolicy) {
    mEmbedderPolicy = aPolicy;
  }

  nsILoadInfo::CrossOriginEmbedderPolicy GetEmbedderPolicy() const {
    return mEmbedderPolicy;
  }

  void SetInterceptionTriggeringPrincipal(nsIPrincipal* aPrincipal) {
    mInterceptionTriggeringPrincipal = aPrincipal;
  }

  nsIPrincipal* GetInterceptionTriggeringPrincipal() const {
    return mInterceptionTriggeringPrincipal;
  }

  nsContentPolicyType InterceptionContentPolicyType() const {
    return mInterceptionContentPolicyType;
  }
  RequestDestination InterceptionDestination() const {
    return MapContentPolicyTypeToRequestDestination(
        mInterceptionContentPolicyType);
  }
  void SetInterceptionContentPolicyType(nsContentPolicyType aContentPolicyType);

  const nsTArray<RedirectHistoryEntryInfo>& InterceptionRedirectChain() const {
    return mInterceptionRedirectChain;
  }

  void SetInterceptionRedirectChain(
      const nsTArray<RedirectHistoryEntryInfo>& aRedirectChain) {
    mInterceptionRedirectChain = aRedirectChain;
  }

  const bool& InterceptionFromThirdParty() const {
    return mInterceptionFromThirdParty;
  }

  void SetInterceptionFromThirdParty(bool aFromThirdParty) {
    mInterceptionFromThirdParty = aFromThirdParty;
  }

 private:
  struct ConstructorGuard {};

 public:
  // Does not copy mBodyStream.  Use fallible Clone() for complete copy.
  InternalRequest(const InternalRequest& aOther, ConstructorGuard);

  // Map the content policy type to the associated fetch destination, as defined
  // by the spec at https://fetch.spec.whatwg.org/#concept-request-destination.
  // Note that while the HTML spec for the "Link" element and its "as" attribute
  // (https://html.spec.whatwg.org/#attr-link-as) reuse fetch's definition of
  // destination.
  static RequestDestination MapContentPolicyTypeToRequestDestination(
      nsContentPolicyType aContentPolicyType);
  static RequestDestination MapContentPolicyTypeToRequestDestination(
      ExtContentPolicyType aContentPolicyType);

  // True for content policy types that are only ever legitimately used for
  // nsDocShell-initiated navigations, never for a fetch() call.
  static bool IsNavigationContentPolicy(nsContentPolicyType aContentPolicyType);

 private:
  static bool IsWorkerContentPolicy(nsContentPolicyType aContentPolicyType);

  // It should only be called while there is a service-worker-internal-redirect.
  void SetURL(NotNull<nsIURI*> aURL, const nsACString& aFragment) {
    MOZ_ASSERT(!mURLList.IsEmpty());
#ifdef DEBUG
    bool hasRef = false;
    MOZ_ALWAYS_SUCCEEDS(aURL->GetHasRef(&hasRef));
    MOZ_ASSERT(!hasRef);
#endif

    mURLList.LastElement() = aURL;
    mFragment.Assign(aFragment);
  }

  nsCString mMethod;
  // mURLList: a list of one or more fetch URLs
  nsTArray<NotNull<RefPtr<nsIURI>>> mURLList;
  RefPtr<InternalHeaders> mHeaders;
  RefPtr<BlobImpl> mBodyBlobImpl;
  nsString mBodyLocalPath;
  nsCOMPtr<nsIInputStream> mBodyStream;

  nsCOMPtr<nsIPrincipal> mTriggeringPrincipalOverride;
  bool mNeverTaint = false;
  nsCOMPtr<nsICookieJarSettings> mCookieJarSettings;
  int64_t mBodyLength{InternalResponse::UNKNOWN_BODY_SIZE};

  nsCString mPreferredAlternativeDataType;

  nsContentPolicyType mContentPolicyType;

  int32_t mInternalPriority = nsISupportsPriority::PRIORITY_NORMAL;

  // Empty string: no-referrer
  // "about:client": client (default)
  // URL: an URL
  nsCString mReferrer;
  ReferrerPolicy mReferrerPolicy;

  // Used to track this fetch request in scenarions where determining load
  // context is tricky like for Reporting API. There we provide this explicitly
  // so devtools can track us accordingly.
  uint64_t mAssociatedBrowsingContextID{0};

  // This will be used for request created from Window or Worker contexts
  // In case there's no Referrer Policy in Request, this will be passed to
  // channel.
  ReferrerPolicy mEnvironmentReferrerPolicy;
  RequestMode mMode;
  RequestCredentials mCredentialsMode;
  LoadTainting mResponseTainting = LoadTainting::Basic;
  RequestCache mCacheMode;
  RequestRedirect mRedirectMode;
  RequestPriority mPriorityMode = RequestPriority::Auto;
  nsString mIntegrity;
  bool mKeepalive = false;
  bool mMozErrors = false;
  bool mHasStreamBody = false;
  nsCString mFragment;
  bool mSkipServiceWorker = false;
  bool mSkipWasmCaching = false;
  bool mSynchronous = false;
  bool mUnsafeRequest = false;
  bool mUseURLCredentials = false;
  // This is only set when Request.overrideContentPolicyType() has been set.
  // It is illegal to pass such a Request object to a fetch() method unless
  // if the caller has chrome privileges.
  bool mContentPolicyTypeOverridden = false;
  nsILoadInfo::CrossOriginEmbedderPolicy mEmbedderPolicy =
      nsILoadInfo::EMBEDDER_POLICY_NULL;

  UniquePtr<mozilla::ipc::PrincipalInfo> mPrincipalInfo;

  // Following members are specific for the FetchEvent.request or
  // NavigationPreload request which is extracted from the
  // InterceptedHttpChannel.
  // Notice that these members would not be copied when calling
  // InternalRequest::GetRequestConstructorCopy() since these information should
  // not be propagated when copying the Request in ServiceWorker script.

  // This is the trigging principal of the InterceptedHttpChannel.
  nsCOMPtr<nsIPrincipal> mInterceptionTriggeringPrincipal;

  // This is the contentPolicyType of the InterceptedHttpChannel.
  nsContentPolicyType mInterceptionContentPolicyType{
      nsIContentPolicy::TYPE_INVALID};

  // This is the redirect history of the InterceptedHttpChannel.
  CopyableTArray<RedirectHistoryEntryInfo> mInterceptionRedirectChain;

  // This indicates that the InterceptedHttpChannel is a third party channel.
  bool mInterceptionFromThirdParty{false};
};

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_InternalRequest_h
