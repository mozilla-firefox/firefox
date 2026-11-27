/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "InternalRequest.h"

#include "InternalResponse.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/RemoteLazyInputStreamChild.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/FetchTypes.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/WorkerCommon.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/ipc/IPCStreamUtils.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "nsIContentPolicy.h"
#include "nsStreamUtils.h"

namespace mozilla::dom {
// The global is used to extract the principal.
SafeRefPtr<InternalRequest> InternalRequest::GetRequestConstructorCopy(
    nsIGlobalObject* aGlobal, ErrorResult& aRv) const {
  MOZ_RELEASE_ASSERT(!mURLList.IsEmpty(),
                     "Internal Request's urlList should not be empty when "
                     "copied from constructor.");
  auto copy =
      MakeSafeRefPtr<InternalRequest>(mURLList.LastElement(), mFragment);
  copy->SetMethod(mMethod);
  copy->mHeaders = new InternalHeaders(*mHeaders);
  copy->mTriggeringPrincipalOverride = mTriggeringPrincipalOverride;
  copy->mNeverTaint = mNeverTaint;
  copy->mCookieJarSettings = mCookieJarSettings;
  copy->SetUnsafeRequest();
  copy->mBodyStream = mBodyStream;
  copy->mBodyLength = mBodyLength;
  // The "client" is not stored in our implementation. Fetch API users should
  // use the appropriate window/document/principal and other Gecko security
  // mechanisms as appropriate.
  copy->mReferrer = mReferrer;
  copy->mReferrerPolicy = mReferrerPolicy;
  copy->mEnvironmentReferrerPolicy = mEnvironmentReferrerPolicy;
  copy->mIntegrity = mIntegrity;
  copy->mMozErrors = mMozErrors;

  copy->mContentPolicyType = mContentPolicyTypeOverridden
                                 ? mContentPolicyType
                                 : nsIContentPolicy::TYPE_FETCH;
  copy->mInternalPriority = mInternalPriority;
  copy->mMode = mMode;
  copy->mCredentialsMode = mCredentialsMode;
  copy->mCacheMode = mCacheMode;
  copy->mRedirectMode = mRedirectMode;
  copy->mPriorityMode = mPriorityMode;
  copy->mHasStreamBody = mHasStreamBody;
  copy->mContentPolicyTypeOverridden = mContentPolicyTypeOverridden;

  copy->mPreferredAlternativeDataType = mPreferredAlternativeDataType;
  copy->mSkipWasmCaching = mSkipWasmCaching;
  copy->mEmbedderPolicy = mEmbedderPolicy;
  copy->mAssociatedBrowsingContextID = mAssociatedBrowsingContextID;
  return copy;
}

SafeRefPtr<InternalRequest> InternalRequest::Clone() {
  auto clone = MakeSafeRefPtr<InternalRequest>(*this, ConstructorGuard{});

  if (!mBodyStream) {
    return clone;
  }

  nsCOMPtr<nsIInputStream> clonedBody;
  nsCOMPtr<nsIInputStream> replacementBody;

  nsresult rv = NS_CloneInputStream(mBodyStream, getter_AddRefs(clonedBody),
                                    getter_AddRefs(replacementBody));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }

  clone->mBodyStream.swap(clonedBody);
  if (replacementBody) {
    mBodyStream.swap(replacementBody);
  }
  return clone;
}
InternalRequest::InternalRequest(NotNull<nsIURI*> aURL,
                                 const nsACString& aFragment)
    : mMethod("GET"),
      mHeaders(new InternalHeaders(HeadersGuardEnum::None)),
      mBodyLength(InternalResponse::UNKNOWN_BODY_SIZE),
      mContentPolicyType(nsIContentPolicy::TYPE_FETCH),
      mReferrer(nsLiteralCString(kFETCH_CLIENT_REFERRER_STR)),
      mReferrerPolicy(ReferrerPolicy::_empty),
      mEnvironmentReferrerPolicy(ReferrerPolicy::_empty),
      mMode(RequestMode::No_cors),
      mCredentialsMode(RequestCredentials::Omit),
      mCacheMode(RequestCache::Default),
      mRedirectMode(RequestRedirect::Follow),
      mPriorityMode(RequestPriority::Auto) {
  AddURL(aURL, aFragment);
}

InternalRequest::InternalRequest(const InternalRequest& aOther,
                                 ConstructorGuard)
    : mMethod(aOther.mMethod),
      mURLList(aOther.mURLList.Clone()),
      mHeaders(new InternalHeaders(*aOther.mHeaders)),
      mTriggeringPrincipalOverride(aOther.mTriggeringPrincipalOverride),
      mNeverTaint(aOther.mNeverTaint),
      mBodyLength(InternalResponse::UNKNOWN_BODY_SIZE),
      mContentPolicyType(aOther.mContentPolicyType),
      mInternalPriority(aOther.mInternalPriority),
      mReferrer(aOther.mReferrer),
      mReferrerPolicy(aOther.mReferrerPolicy),
      mAssociatedBrowsingContextID(aOther.mAssociatedBrowsingContextID),
      mEnvironmentReferrerPolicy(aOther.mEnvironmentReferrerPolicy),
      mMode(aOther.mMode),
      mCredentialsMode(aOther.mCredentialsMode),
      mResponseTainting(aOther.mResponseTainting),
      mCacheMode(aOther.mCacheMode),
      mRedirectMode(aOther.mRedirectMode),
      mPriorityMode(aOther.mPriorityMode),
      mIntegrity(aOther.mIntegrity),
      mKeepalive(aOther.mKeepalive),
      mMozErrors(aOther.mMozErrors),
      mHasStreamBody(aOther.mHasStreamBody),
      mFragment(aOther.mFragment),
      mSkipServiceWorker(aOther.mSkipServiceWorker),
      mSkipWasmCaching(aOther.mSkipWasmCaching),
      mSynchronous(aOther.mSynchronous),
      mUnsafeRequest(aOther.mUnsafeRequest),
      mUseURLCredentials(aOther.mUseURLCredentials),
      mContentPolicyTypeOverridden(aOther.mContentPolicyTypeOverridden),
      mEmbedderPolicy(aOther.mEmbedderPolicy),
      mInterceptionTriggeringPrincipal(aOther.mInterceptionTriggeringPrincipal),
      mInterceptionContentPolicyType(aOther.mInterceptionContentPolicyType),
      mInterceptionRedirectChain(aOther.mInterceptionRedirectChain),
      mInterceptionFromThirdParty(aOther.mInterceptionFromThirdParty) {
  // NOTE: does not copy body stream... use the fallible Clone() for that
}

InternalRequest::InternalRequest(const IPCInternalRequest& aIPCRequest)
    : mMethod(aIPCRequest.method()),
      mURLList(aIPCRequest.urlList().Clone()),
      mHeaders(new InternalHeaders(aIPCRequest.headers(),
                                   aIPCRequest.headersGuard())),
      mBodyLength(aIPCRequest.bodySize()),
      mPreferredAlternativeDataType(aIPCRequest.preferredAlternativeDataType()),
      mContentPolicyType(aIPCRequest.contentPolicyType()),
      mInternalPriority(aIPCRequest.internalPriority()),
      mReferrer(aIPCRequest.referrer()),
      mReferrerPolicy(aIPCRequest.referrerPolicy()),
      mEnvironmentReferrerPolicy(aIPCRequest.environmentReferrerPolicy()),
      mMode(aIPCRequest.requestMode()),
      mCredentialsMode(aIPCRequest.requestCredentials()),
      mCacheMode(aIPCRequest.cacheMode()),
      mRedirectMode(aIPCRequest.requestRedirect()),
      mPriorityMode(aIPCRequest.requestPriority()),
      mIntegrity(aIPCRequest.integrity()),
      mKeepalive(aIPCRequest.keepalive()),
      mHasStreamBody(aIPCRequest.hasStreamBody()),
      mFragment(aIPCRequest.fragment()),
      mEmbedderPolicy(aIPCRequest.embedderPolicy()),
      mInterceptionTriggeringPrincipal(
          aIPCRequest.interceptionTriggeringPrincipal()),
      mInterceptionContentPolicyType(
          aIPCRequest.interceptionContentPolicyType()),
      mInterceptionRedirectChain(aIPCRequest.interceptionRedirectChain()),
      mInterceptionFromThirdParty(aIPCRequest.interceptionFromThirdParty()) {
  if (aIPCRequest.principalInfo()) {
    mPrincipalInfo = std::make_unique<mozilla::ipc::PrincipalInfo>(
        aIPCRequest.principalInfo().ref());
  }

  const Maybe<BodyStreamVariant>& body = aIPCRequest.body();

  if (body) {
    if (body->type() == BodyStreamVariant::TParentToChildStream) {
      const auto& stream = body->get_ParentToChildStream();
      if (stream.type() == ParentToChildStream::TIPCStream) {
        mBodyStream = DeserializeIPCStream(stream.get_IPCStream());
      } else {
        mBodyStream = stream.get_RemoteLazyInputStream();
      }
    }
    if (body->type() == BodyStreamVariant::TChildToParentStream) {
      mBodyStream =
          DeserializeIPCStream(body->get_ChildToParentStream().stream());
    }
  }
}

void InternalRequest::ToIPCInternalRequest(
    IPCInternalRequest* aIPCRequest, mozilla::ipc::PBackgroundChild* aManager) {
  aIPCRequest->method() = mMethod;
  aIPCRequest->urlList() = mURLList.Clone();
  mHeaders->ToIPC(aIPCRequest->headers(), aIPCRequest->headersGuard());
  aIPCRequest->bodySize() = mBodyLength;
  aIPCRequest->preferredAlternativeDataType() = mPreferredAlternativeDataType;
  aIPCRequest->contentPolicyType() = mContentPolicyType;
  aIPCRequest->internalPriority() = mInternalPriority;
  aIPCRequest->referrer() = mReferrer;
  aIPCRequest->referrerPolicy() = mReferrerPolicy;
  aIPCRequest->environmentReferrerPolicy() = mEnvironmentReferrerPolicy;
  aIPCRequest->requestMode() = mMode;
  aIPCRequest->requestCredentials() = mCredentialsMode;
  aIPCRequest->cacheMode() = mCacheMode;
  aIPCRequest->requestRedirect() = mRedirectMode;
  aIPCRequest->requestPriority() = mPriorityMode;
  aIPCRequest->integrity() = mIntegrity;
  aIPCRequest->keepalive() = mKeepalive;
  aIPCRequest->hasStreamBody() = mHasStreamBody;
  aIPCRequest->fragment() = mFragment;
  aIPCRequest->embedderPolicy() = mEmbedderPolicy;

  if (mPrincipalInfo) {
    aIPCRequest->principalInfo() = Some(*mPrincipalInfo);
  }

  if (mInterceptionTriggeringPrincipal) {
    aIPCRequest->interceptionTriggeringPrincipal() =
        mInterceptionTriggeringPrincipal;
    aIPCRequest->interceptionContentPolicyType() =
        mInterceptionContentPolicyType;
    if (!mInterceptionRedirectChain.IsEmpty()) {
      aIPCRequest->interceptionRedirectChain().Assign(
          mInterceptionRedirectChain);
    }
    aIPCRequest->interceptionFromThirdParty() = mInterceptionFromThirdParty;
  }

  if (mBodyStream) {
    nsCOMPtr<nsIInputStream> body = mBodyStream;
    aIPCRequest->body().emplace(ChildToParentStream());
    DebugOnly<bool> ok = mozilla::ipc::SerializeIPCStream(
        body.forget(), aIPCRequest->body()->get_ChildToParentStream().stream(),
        /* aAllowLazy */ false);
    MOZ_ASSERT(ok);
  }
}

InternalRequest::~InternalRequest() = default;

void InternalRequest::SetContentPolicyType(
    nsContentPolicyType aContentPolicyType) {
  mContentPolicyType = aContentPolicyType;
}

void InternalRequest::OverrideContentPolicyType(
    nsContentPolicyType aContentPolicyType) {
  SetContentPolicyType(aContentPolicyType);
  mContentPolicyTypeOverridden = true;
}

void InternalRequest::SetInterceptionContentPolicyType(
    nsContentPolicyType aContentPolicyType) {
  mInterceptionContentPolicyType = aContentPolicyType;
}

/* static */
/* static */
RequestDestination InternalRequest::MapContentPolicyTypeToRequestDestination(
    nsContentPolicyType aContentPolicyType) {
  switch (aContentPolicyType) {
    case nsIContentPolicy::TYPE_OTHER:
      return RequestDestination::_empty;
    case nsIContentPolicy::TYPE_INTERNAL_SCRIPT:
    case nsIContentPolicy::TYPE_INTERNAL_SCRIPT_PRELOAD:
    case nsIContentPolicy::TYPE_INTERNAL_MODULE:
    case nsIContentPolicy::TYPE_INTERNAL_MODULE_PRELOAD:
    case nsIContentPolicy::TYPE_INTERNAL_SERVICE_WORKER:
    case nsIContentPolicy::TYPE_INTERNAL_WORKER_IMPORT_SCRIPTS:
    case nsIContentPolicy::TYPE_INTERNAL_CHROMEUTILS_COMPILED_SCRIPT:
    case nsIContentPolicy::TYPE_INTERNAL_FRAME_MESSAGEMANAGER_SCRIPT:
    case nsIContentPolicy::TYPE_SCRIPT:
      return RequestDestination::Script;
    case nsIContentPolicy::TYPE_INTERNAL_WORKER:
    case nsIContentPolicy::TYPE_INTERNAL_WORKER_STATIC_MODULE:
      return RequestDestination::Worker;
    case nsIContentPolicy::TYPE_INTERNAL_SHARED_WORKER:
      return RequestDestination::Sharedworker;
    case nsIContentPolicy::TYPE_IMAGESET:
    case nsIContentPolicy::TYPE_INTERNAL_IMAGE:
    case nsIContentPolicy::TYPE_INTERNAL_IMAGE_PRELOAD:
    case nsIContentPolicy::TYPE_INTERNAL_IMAGE_FAVICON:
    case nsIContentPolicy::TYPE_INTERNAL_IMAGE_NOTIFICATION:
    case nsIContentPolicy::TYPE_IMAGE:
      return RequestDestination::Image;
    case nsIContentPolicy::TYPE_STYLESHEET:
    case nsIContentPolicy::TYPE_INTERNAL_STYLESHEET:
    case nsIContentPolicy::TYPE_INTERNAL_STYLESHEET_PRELOAD:
      return RequestDestination::Style;
    case nsIContentPolicy::TYPE_OBJECT:
    case nsIContentPolicy::TYPE_INTERNAL_OBJECT:
      return RequestDestination::Object;
    case nsIContentPolicy::TYPE_INTERNAL_EMBED:
      return RequestDestination::Embed;
    case nsIContentPolicy::TYPE_DOCUMENT:
      return RequestDestination::Document;
    case nsIContentPolicy::TYPE_SUBDOCUMENT:
    case nsIContentPolicy::TYPE_INTERNAL_IFRAME:
      return RequestDestination::Iframe;
    case nsIContentPolicy::TYPE_INTERNAL_FRAME:
      return RequestDestination::Frame;
    case nsIContentPolicy::TYPE_PING:
      return RequestDestination::_empty;
    case nsIContentPolicy::TYPE_XMLHTTPREQUEST:
    case nsIContentPolicy::TYPE_INTERNAL_XMLHTTPREQUEST_ASYNC:
    case nsIContentPolicy::TYPE_INTERNAL_XMLHTTPREQUEST_SYNC:
      return RequestDestination::_empty;
    case nsIContentPolicy::TYPE_INTERNAL_EVENTSOURCE:
      return RequestDestination::_empty;
    case nsIContentPolicy::TYPE_DTD:
    case nsIContentPolicy::TYPE_INTERNAL_DTD:
    case nsIContentPolicy::TYPE_INTERNAL_FORCE_ALLOWED_DTD:
      return RequestDestination::_empty;
    case nsIContentPolicy::TYPE_FONT:
    case nsIContentPolicy::TYPE_INTERNAL_FONT_PRELOAD:
    case nsIContentPolicy::TYPE_UA_FONT:
      return RequestDestination::Font;
    case nsIContentPolicy::TYPE_MEDIA:
      return RequestDestination::_empty;
    case nsIContentPolicy::TYPE_INTERNAL_AUDIO:
      return RequestDestination::Audio;
    case nsIContentPolicy::TYPE_INTERNAL_VIDEO:
      return RequestDestination::Video;
    case nsIContentPolicy::TYPE_INTERNAL_TRACK:
      return RequestDestination::Track;
    case nsIContentPolicy::TYPE_WEBSOCKET:
      return RequestDestination::_empty;
    case nsIContentPolicy::TYPE_CSP_REPORT:
      return RequestDestination::Report;
    case nsIContentPolicy::TYPE_XSLT:
      return RequestDestination::Xslt;
    case nsIContentPolicy::TYPE_BEACON:
      return RequestDestination::_empty;
    case nsIContentPolicy::TYPE_FETCH:
    case nsIContentPolicy::TYPE_INTERNAL_FETCH_PRELOAD:
      return RequestDestination::_empty;
    case nsIContentPolicy::TYPE_WEB_MANIFEST:
      return RequestDestination::Manifest;
    case nsIContentPolicy::TYPE_SAVEAS_DOWNLOAD:
      return RequestDestination::_empty;
    case nsIContentPolicy::TYPE_SPECULATIVE:
      return RequestDestination::_empty;
    case nsIContentPolicy::TYPE_INTERNAL_AUDIOWORKLET:
      return RequestDestination::Audioworklet;
    case nsIContentPolicy::TYPE_INTERNAL_PAINTWORKLET:
      return RequestDestination::Paintworklet;
    case nsIContentPolicy::TYPE_PROXIED_WEBRTC_MEDIA:
      return RequestDestination::_empty;
    case nsIContentPolicy::TYPE_WEB_IDENTITY:
      return RequestDestination::_empty;
    case nsIContentPolicy::TYPE_WEB_TRANSPORT:
      return RequestDestination::_empty;
    case nsIContentPolicy::TYPE_INTERNAL_SPECULATION_RULES:
      return RequestDestination::_empty;
    case nsIContentPolicy::TYPE_INTERNAL_EXTERNAL_RESOURCE:
      return RequestDestination::Image;
    case nsIContentPolicy::TYPE_JSON:
    case nsIContentPolicy::TYPE_INTERNAL_JSON_PRELOAD:
      return RequestDestination::Json;
    case nsIContentPolicy::TYPE_TEXT:
    case nsIContentPolicy::TYPE_INTERNAL_TEXT_PRELOAD:
      return RequestDestination::Text;
    case nsIContentPolicy::TYPE_INVALID:
      break;
      // Do not add default: so that compilers can catch the missing case.
  }

  MOZ_ASSERT(false, "Unhandled nsContentPolicyType value");
  return RequestDestination::_empty;
}

/* static */
RequestDestination InternalRequest::MapContentPolicyTypeToRequestDestination(
    ExtContentPolicyType aContentPolicyType) {
  switch (aContentPolicyType) {
    case ExtContentPolicyType::TYPE_INVALID:
    case ExtContentPolicyType::TYPE_OTHER:
      return RequestDestination::_empty;
    case ExtContentPolicyType::TYPE_SCRIPT:
      return RequestDestination::Script;
    case ExtContentPolicyType::TYPE_IMAGE:
      return RequestDestination::Image;
    case ExtContentPolicyType::TYPE_STYLESHEET:
      return RequestDestination::Style;
    case ExtContentPolicyType::TYPE_OBJECT:
      return RequestDestination::Object;
    case ExtContentPolicyType::TYPE_DOCUMENT:
      return RequestDestination::Document;
    case ExtContentPolicyType::TYPE_SUBDOCUMENT:
      return RequestDestination::Iframe;
    case ExtContentPolicyType::TYPE_PING:
    case ExtContentPolicyType::TYPE_XMLHTTPREQUEST:
    case ExtContentPolicyType::TYPE_DTD:
      return RequestDestination::_empty;
    case ExtContentPolicyType::TYPE_FONT:
      return RequestDestination::Font;
    case ExtContentPolicyType::TYPE_MEDIA:
    case ExtContentPolicyType::TYPE_WEBSOCKET:
      return RequestDestination::_empty;
    case ExtContentPolicyType::TYPE_CSP_REPORT:
      return RequestDestination::Report;
    case ExtContentPolicyType::TYPE_XSLT:
      return RequestDestination::Xslt;
    case ExtContentPolicyType::TYPE_BEACON:
    case ExtContentPolicyType::TYPE_FETCH:
      return RequestDestination::_empty;
    case ExtContentPolicyType::TYPE_IMAGESET:
      return RequestDestination::Image;
    case ExtContentPolicyType::TYPE_WEB_MANIFEST:
      return RequestDestination::Manifest;
    case ExtContentPolicyType::TYPE_SAVEAS_DOWNLOAD:
    case ExtContentPolicyType::TYPE_SPECULATIVE:
      return RequestDestination::_empty;
    case ExtContentPolicyType::TYPE_UA_FONT:
      return RequestDestination::Font;
    case ExtContentPolicyType::TYPE_PROXIED_WEBRTC_MEDIA:
    case ExtContentPolicyType::TYPE_WEB_IDENTITY:
    case ExtContentPolicyType::TYPE_WEB_TRANSPORT:
      return RequestDestination::_empty;
    case ExtContentPolicyType::TYPE_JSON:
      return RequestDestination::Json;
    case ExtContentPolicyType::TYPE_TEXT:
      return RequestDestination::Text;
      // Do not add default: so that compilers can catch the missing case.
  }

  MOZ_ASSERT(false, "Unhandled ExContentPolicyType value");
  return RequestDestination::_empty;
}

// static
bool InternalRequest::IsNavigationContentPolicy(
    nsContentPolicyType aContentPolicyType) {
  // https://fetch.spec.whatwg.org/#navigation-request-context
  //
  // A navigation request context is one of "form", "frame", "hyperlink",
  // "iframe", "internal" (as long as context frame type is not "none"),
  // "location", "metarefresh", and "prerender".
  //
  // Note, all of these request types are effectively initiated by nsDocShell.
  return aContentPolicyType == nsIContentPolicy::TYPE_DOCUMENT ||
         aContentPolicyType == nsIContentPolicy::TYPE_SUBDOCUMENT ||
         aContentPolicyType == nsIContentPolicy::TYPE_INTERNAL_FRAME ||
         aContentPolicyType == nsIContentPolicy::TYPE_INTERNAL_IFRAME;
}

// static
bool InternalRequest::IsWorkerContentPolicy(
    nsContentPolicyType aContentPolicyType) {
  // https://fetch.spec.whatwg.org/#worker-request-context
  //
  // A worker request context is one of "serviceworker", "sharedworker", and
  // "worker".
  //
  // Note, service workers are not included here because currently there is
  // no way to generate a Request with a "serviceworker" RequestDestination.
  // ServiceWorker scripts cannot be intercepted.
  return aContentPolicyType == nsIContentPolicy::TYPE_INTERNAL_WORKER ||
         aContentPolicyType == nsIContentPolicy::TYPE_INTERNAL_SHARED_WORKER;
}

bool InternalRequest::IsNavigationRequest() const {
  return IsNavigationContentPolicy(mContentPolicyType);
}

bool InternalRequest::IsWorkerRequest() const {
  return IsWorkerContentPolicy(mContentPolicyType);
}

bool InternalRequest::IsClientRequest() const {
  return IsNavigationRequest() || IsWorkerRequest();
}

// static
RequestMode InternalRequest::MapChannelToRequestMode(nsIChannel* aChannel) {
  MOZ_ASSERT(aChannel);

  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();

  nsContentPolicyType contentPolicy = loadInfo->InternalContentPolicyType();
  if (IsNavigationContentPolicy(contentPolicy)) {
    return RequestMode::Navigate;
  }

  // TODO: remove the worker override once securityMode is fully implemented
  // (bug 1189945)
  if (IsWorkerContentPolicy(contentPolicy)) {
    return RequestMode::Same_origin;
  }

  uint32_t securityMode = loadInfo->GetSecurityMode();

  switch (securityMode) {
    case nsILoadInfo::SEC_REQUIRE_SAME_ORIGIN_INHERITS_SEC_CONTEXT:
    case nsILoadInfo::SEC_REQUIRE_SAME_ORIGIN_DATA_IS_BLOCKED:
      return RequestMode::Same_origin;
    case nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_INHERITS_SEC_CONTEXT:
    case nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL:
      return RequestMode::No_cors;
    case nsILoadInfo::SEC_REQUIRE_CORS_INHERITS_SEC_CONTEXT:
      // TODO: Check additional flag force-preflight after bug 1199693 (bug
      // 1189945)
      return RequestMode::Cors;
    default:
      MOZ_ASSERT_UNREACHABLE("Unexpected security mode!");
      return RequestMode::Same_origin;
  }
}

// static
RequestCredentials InternalRequest::MapChannelToRequestCredentials(
    nsIChannel* aChannel) {
  MOZ_ASSERT(aChannel);

  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();

  uint32_t cookiePolicy = loadInfo->GetCookiePolicy();

  if (cookiePolicy == nsILoadInfo::SEC_COOKIES_INCLUDE) {
    return RequestCredentials::Include;
  } else if (cookiePolicy == nsILoadInfo::SEC_COOKIES_OMIT) {
    return RequestCredentials::Omit;
  } else if (cookiePolicy == nsILoadInfo::SEC_COOKIES_SAME_ORIGIN) {
    return RequestCredentials::Same_origin;
  }

  MOZ_ASSERT_UNREACHABLE("Unexpected cookie policy!");
  return RequestCredentials::Same_origin;
}

void InternalRequest::MaybeSkipCacheIfPerformingRevalidation() {
  if (mCacheMode == RequestCache::Default &&
      mHeaders->HasRevalidationHeaders()) {
    mCacheMode = RequestCache::No_store;
  }
}

void InternalRequest::SetPrincipalInfo(
    UniquePtr<mozilla::ipc::PrincipalInfo> aPrincipalInfo) {
  mPrincipalInfo = std::move(aPrincipalInfo);
}
}  // namespace mozilla::dom
