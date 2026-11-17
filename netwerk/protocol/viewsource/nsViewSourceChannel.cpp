/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsViewSourceChannel.h"

#include "mozilla/NullPrincipal.h"
#include "nsContentSecurityManager.h"
#include "nsContentUtils.h"
#include "nsHttpChannel.h"
#include "nsIExternalProtocolHandler.h"
#include "nsIHttpHeaderVisitor.h"
#include "nsIIOService.h"
#include "nsIInputStreamChannel.h"
#include "nsINestedURI.h"
#include "nsIReferrerInfo.h"
#include "nsMimeTypes.h"
#include "nsNetUtil.h"
#include "nsServiceManagerUtils.h"

NS_IMPL_ADDREF(nsViewSourceChannel)
NS_IMPL_RELEASE(nsViewSourceChannel)
/*
  This QI uses NS_INTERFACE_MAP_ENTRY_CONDITIONAL to check for
  non-nullness of mHttpChannel, mCachingChannel, and mUploadChannel.
*/
NS_INTERFACE_MAP_BEGIN(nsViewSourceChannel)
  NS_INTERFACE_MAP_ENTRY(nsIViewSourceChannel)
  NS_INTERFACE_MAP_ENTRY(nsIStreamListener)
  NS_INTERFACE_MAP_ENTRY(nsIRequestObserver)
  NS_INTERFACE_MAP_ENTRY(nsIInterfaceRequestor)
  NS_INTERFACE_MAP_ENTRY(nsIChannelEventSink)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIHttpChannel, mHttpChannel)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIIdentChannel, mHttpChannel)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIHttpChannelInternal,
                                     mHttpChannelInternal)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsICachingChannel, mCachingChannel)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsICacheInfoChannel, mCacheInfoChannel)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIUploadChannel, mUploadChannel)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIFormPOSTActionChannel, mPostChannel)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIChildChannel, mChildChannel)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsIRequest, nsIViewSourceChannel)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsIChannel, nsIViewSourceChannel)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIViewSourceChannel)
NS_INTERFACE_MAP_END

static nsresult WillUseExternalProtocolHandler(nsIIOService* aIOService,
                                               const char* aScheme) {
  nsCOMPtr<nsIProtocolHandler> handler;
  nsresult rv =
      aIOService->GetProtocolHandler(aScheme, getter_AddRefs(handler));
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsCOMPtr<nsIExternalProtocolHandler> externalHandler =
      do_QueryInterface(handler);
  // We should not allow view-source to open any external app.
  if (externalHandler) {
    NS_WARNING(nsPrintfCString("blocking view-source:%s:", aScheme).get());
    return NS_ERROR_MALFORMED_URI;
  }

  return NS_OK;
}

nsresult nsViewSourceChannel::Init(nsIURI* uri, nsILoadInfo* aLoadInfo) {
  mOriginalURI = uri;

  nsAutoCString path;
  nsresult rv = uri->GetPathQueryRef(path);
  if (NS_FAILED(rv)) return rv;

  nsCOMPtr<nsIIOService> pService(do_GetIOService(&rv));
  if (NS_FAILED(rv)) return rv;

  nsAutoCString scheme;
  rv = pService->ExtractScheme(path, scheme);
  if (NS_FAILED(rv)) return rv;

  // prevent viewing source of javascript URIs (see bug 204779)
  if (scheme.EqualsLiteral("javascript")) {
    NS_WARNING("blocking view-source:javascript:");
    return NS_ERROR_INVALID_ARG;
  }

  rv = WillUseExternalProtocolHandler(pService, scheme.get());
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsCOMPtr<nsIURI> newChannelURI;
  rv = pService->NewURI(path, nullptr, nullptr, getter_AddRefs(newChannelURI));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = pService->NewChannelFromURIWithLoadInfo(newChannelURI, aLoadInfo,
                                               getter_AddRefs(mChannel));
  NS_ENSURE_SUCCESS(rv, rv);

  mIsSrcdocChannel = false;

  mChannel->SetOriginalURI(mOriginalURI);
  UpdateChannelInterfaces();

  return NS_OK;
}

nsresult nsViewSourceChannel::InitSrcdoc(nsIURI* aURI, nsIURI* aBaseURI,
                                         const nsAString& aSrcdoc,
                                         nsILoadInfo* aLoadInfo) {
  nsresult rv;

  MOZ_ASSERT(aURI->SchemeIs("view-source"));
  nsCOMPtr<nsINestedURI> nestedURI(do_QueryInterface(aURI));
  NS_ENSURE_TRUE(nestedURI, NS_ERROR_INVALID_ARG);
  nsCOMPtr<nsIURI> innerURI;
  rv = nestedURI->GetInnerURI(getter_AddRefs(innerURI));
  NS_ENSURE_SUCCESS(rv, rv);
  MOZ_RELEASE_ASSERT(NS_IsAboutSrcdoc(innerURI));

  nsCOMPtr<nsIURI> inStreamURI;
  // Need to strip view-source: from the URI.  Hardcoded to
  // about:srcdoc as this is the only permissible URI for srcdoc
  // loads
  rv = NS_NewURI(getter_AddRefs(inStreamURI), u"about:srcdoc"_ns);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = NS_NewInputStreamChannelInternal(getter_AddRefs(mChannel), inStreamURI,
                                        aSrcdoc, "text/html"_ns, aLoadInfo,
                                        true);

  NS_ENSURE_SUCCESS(rv, rv);
  mOriginalURI = aURI;
  mIsSrcdocChannel = true;

  mChannel->SetOriginalURI(mOriginalURI);
  UpdateChannelInterfaces();

  rv = UpdateLoadInfoResultPrincipalURI();
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIInputStreamChannel> isc = do_QueryInterface(mChannel);
  MOZ_ASSERT(isc);
  isc->SetBaseURI(aBaseURI);
  return NS_OK;
}

void nsViewSourceChannel::UpdateChannelInterfaces() {
  mHttpChannel = do_QueryInterface(mChannel);
  mHttpChannelInternal = do_QueryInterface(mChannel);
  mCachingChannel = do_QueryInterface(mChannel);
  mCacheInfoChannel = do_QueryInterface(mChannel);
  mUploadChannel = do_QueryInterface(mChannel);
  mPostChannel = do_QueryInterface(mChannel);
  mChildChannel = do_QueryInterface(mChannel);
}

void nsViewSourceChannel::ReleaseListeners() {
  mListener = nullptr;
  mCallbacks = nullptr;
}

nsresult nsViewSourceChannel::UpdateLoadInfoResultPrincipalURI() {
  nsresult rv;

  MOZ_ASSERT(mChannel);

  nsCOMPtr<nsILoadInfo> channelLoadInfo = mChannel->LoadInfo();
  nsCOMPtr<nsIURI> channelResultPrincipalURI;
  rv = channelLoadInfo->GetResultPrincipalURI(
      getter_AddRefs(channelResultPrincipalURI));
  if (NS_FAILED(rv)) {
    return rv;
  }

  if (!channelResultPrincipalURI) {
    mChannel->GetOriginalURI(getter_AddRefs(channelResultPrincipalURI));
    return NS_OK;
  }

  if (!channelResultPrincipalURI) {
    return NS_ERROR_UNEXPECTED;
  }

  if (channelResultPrincipalURI->SchemeIs("view-source")) {
    // already view-source
    return NS_OK;
  }

  nsCOMPtr<nsIURI> updatedResultPrincipalURI;
  rv = BuildViewSourceURI(channelResultPrincipalURI,
                          getter_AddRefs(updatedResultPrincipalURI));
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = channelLoadInfo->SetResultPrincipalURI(updatedResultPrincipalURI);
  if (NS_FAILED(rv)) {
    return rv;
  }

  return NS_OK;
}

nsresult nsViewSourceChannel::BuildViewSourceURI(nsIURI* aURI,
                                                 nsIURI** aResult) {
  nsresult rv;

  // protect ourselves against broken channel implementations
  if (!aURI) {
    NS_ERROR("no URI to build view-source uri!");
    return NS_ERROR_UNEXPECTED;
  }

  nsAutoCString spec;
  rv = aURI->GetSpec(spec);
  if (NS_FAILED(rv)) {
    return rv;
  }

  return NS_NewURI(aResult, "view-source:"_ns + spec);
}

////////////////////////////////////////////////////////////////////////////////
// nsIRequest methods:

NS_IMETHODIMP
nsViewSourceChannel::GetName(nsACString& result) {
  nsCOMPtr<nsIURI> uri;
  GetURI(getter_AddRefs(uri));
  if (uri) {
    return uri->GetSpec(result);
  }
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsViewSourceChannel::GetTransferSize(uint64_t* aTransferSize) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsViewSourceChannel::GetRequestSize(uint64_t* aRequestSize) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsViewSourceChannel::GetDecodedBodySize(uint64_t* aDecodedBodySize) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsViewSourceChannel::GetEncodedBodySize(uint64_t* aEncodedBodySize) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsViewSourceChannel::IsPending(bool* result) {
  NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

  return mChannel->IsPending(result);
}

NS_IMETHODIMP
nsViewSourceChannel::GetStatus(nsresult* status) {
  NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

  return mChannel->GetStatus(status);
}

NS_IMETHODIMP nsViewSourceChannel::SetCanceledReason(
    const nsACString& aReason) {
  return nsIViewSourceChannel::SetCanceledReasonImpl(aReason);
}

NS_IMETHODIMP nsViewSourceChannel::GetCanceledReason(nsACString& aReason) {
  return nsIViewSourceChannel::GetCanceledReasonImpl(aReason);
}

NS_IMETHODIMP nsViewSourceChannel::CancelWithReason(nsresult aStatus,
                                                    const nsACString& aReason) {
  return nsIViewSourceChannel::CancelWithReasonImpl(aStatus, aReason);
}

NS_IMETHODIMP
nsViewSourceChannel::Cancel(nsresult status) {
  NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

  return mChannel->Cancel(status);
}

NS_IMETHODIMP
nsViewSourceChannel::GetCanceled(bool* aCanceled) {
  NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

  return mChannel->GetCanceled(aCanceled);
}

NS_IMETHODIMP
nsViewSourceChannel::Suspend(void) {
  NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

  return mChannel->Suspend();
}

NS_IMETHODIMP
nsViewSourceChannel::Resume(void) {
  NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

  return mChannel->Resume();
}

////////////////////////////////////////////////////////////////////////////////
// nsIChannel methods:

NS_IMETHODIMP
nsViewSourceChannel::GetOriginalURI(nsIURI** aURI) {
  *aURI = do_AddRef(mOriginalURI).take();
  return NS_OK;
}

NS_IMETHODIMP
nsViewSourceChannel::SetOriginalURI(nsIURI* aURI) {
  NS_ENSURE_ARG_POINTER(aURI);
  mOriginalURI = aURI;
  return NS_OK;
}

NS_IMETHODIMP
nsViewSourceChannel::GetURI(nsIURI** aURI) {
  NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

  nsCOMPtr<nsIURI> uri;
  nsresult rv = mChannel->GetURI(getter_AddRefs(uri));
  if (NS_FAILED(rv)) {
    return rv;
  }

  return BuildViewSourceURI(uri, aURI);
}

NS_IMETHODIMP
nsViewSourceChannel::Open(nsIInputStream** aStream) {
  NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);
  return Open(aStream);
}

NS_IMETHODIMP
nsViewSourceChannel::AsyncOpen(nsIStreamListener* aListener) {
  // We can't ensure GetInitialSecurityCheckDone here

  NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

  mListener = aListener;

  nsresult rv = mChannel->AsyncOpen(this);
  if (NS_FAILED(rv)) {
    ReleaseListeners();
    return rv;
  }

  nsCOMPtr<nsILoadGroup> loadGroup;
  mChannel->GetLoadGroup(getter_AddRefs(loadGroup));
  if (loadGroup) {
    loadGroup->AddRequest(static_cast<nsIViewSourceChannel*>(this), nullptr);
  }

  // We do this here to make sure all notification callbacks changes have been
  // made first before we inject this view-source channel.
  mChannel->GetNotificationCallbacks(getter_AddRefs(mCallbacks));
  mChannel->SetNotificationCallbacks(this);

  MOZ_ASSERT(mCallbacks != this, "We have a cycle");

  mOpened = true;

  return NS_OK;
}

/*
 * Both the view source channel and mChannel are added to the
 * loadgroup.  There should never be more than one request in the
 * loadgroup that has LOAD_DOCUMENT_URI set.  The one that has this
 * flag set is the request whose URI is used to refetch the document,
 * so it better be the viewsource channel.
 *
 * Therefore, we need to make sure that
 * 1) The load flags on mChannel _never_ include LOAD_DOCUMENT_URI
 * 2) The load flags on |this| include LOAD_DOCUMENT_URI when it was
 *    set via SetLoadFlags (mIsDocument keeps track of this flag).
 */

NS_IMETHODIMP
nsViewSourceChannel::GetLoadFlags(uint32_t* aLoadFlags) {
  NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

  nsresult rv = mChannel->GetLoadFlags(aLoadFlags);
  if (NS_FAILED(rv)) return rv;

  // This should actually be just LOAD_DOCUMENT_URI but the win32 compiler
  // fails to deal due to amiguous inheritance.  nsIChannel::LOAD_DOCUMENT_URI
  // also fails; the Win32 compiler thinks that's supposed to be a method.
  if (mIsDocument) *aLoadFlags |= ::nsIChannel::LOAD_DOCUMENT_URI;

  return rv;
}

NS_IMETHODIMP
nsViewSourceChannel::SetLoadFlags(uint32_t aLoadFlags) {
  NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

  // "View source" always wants the currently cached content.
  // We also want to have _this_ channel, not mChannel to be the
  // 'document' channel in the loadgroup.

  // These should actually be just LOAD_FROM_CACHE and LOAD_DOCUMENT_URI but
  // the win32 compiler fails to deal due to amiguous inheritance.
  // nsIChannel::LOAD_DOCUMENT_URI/nsIRequest::LOAD_FROM_CACHE also fails; the
  // Win32 compiler thinks that's supposed to be a method.
  mIsDocument = (aLoadFlags & ::nsIChannel::LOAD_DOCUMENT_URI) != 0;

  nsresult rv =
      mChannel->SetLoadFlags((aLoadFlags | ::nsIRequest::LOAD_FROM_CACHE) &
                             ~::nsIChannel::LOAD_DOCUMENT_URI);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (mHttpChannel) {
    rv = mHttpChannel->SetIsMainDocumentChannel(
        aLoadFlags & ::nsIChannel::LOAD_DOCUMENT_URI);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
  }

  return NS_OK;
}

NS_IMETHODIMP
nsViewSourceChannel::GetTRRMode(nsIRequest::TRRMode* aTRRMode) {
  return nsIViewSourceChannel::GetTRRModeImpl(aTRRMode);
}

NS_IMETHODIMP
nsViewSourceChannel::SetTRRMode(nsIRequest::TRRMode aTRRMode) {
  return nsIViewSourceChannel::SetTRRModeImpl(aTRRMode);
}

NS_IMETHODIMP
nsViewSourceChannel::GetContentType(nsACString& aContentType) {
  NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

  aContentType.Truncate();

  if (mContentType.IsEmpty()) {
    // Get the current content type
    nsresult rv;
    nsAutoCString contentType;
    rv = mChannel->GetContentType(contentType);
    if (NS_FAILED(rv)) return rv;

    // If we don't know our type, just say so.  The unknown
    // content decoder will then kick in automatically, and it
    // will call our SetOriginalContentType method instead of our
    // SetContentType method to set the type it determines.
    if (!contentType.EqualsLiteral(UNKNOWN_CONTENT_TYPE) &&
        !contentType.IsEmpty()) {
      contentType = VIEWSOURCE_CONTENT_TYPE;
    }

    mContentType = std::move(contentType);
  }

  aContentType = mContentType;
  return NS_OK;
}

NS_IMETHODIMP
nsViewSourceChannel::SetContentType(const nsACString& aContentType) {
  // Our GetContentType() currently returns VIEWSOURCE_CONTENT_TYPE
  //
  // However, during the parsing phase the parser calls our
  // channel's GetContentType(). Returning the string above trips up
  // the parser. In order to avoid messy changes and not to have the
  // parser depend on nsIViewSourceChannel Vidur proposed the
  // following solution:
  //
  // The ViewSourceChannel initially returns a content type of
  // VIEWSOURCE_CONTENT_TYPE.  Based on this type decisions to
  // create a viewer for doing a view source are made.  After the
  // viewer is created, nsLayoutDLF::CreateInstance() calls this
  // SetContentType() with the original content type.  When it's
  // time for the parser to find out the content type it will call
  // our channel's GetContentType() and it will get the original
  // content type, such as, text/html and everything is kosher from
  // then on.

  if (!mOpened) {
    // We do not take hints
    return NS_ERROR_NOT_AVAILABLE;
  }

  mContentType = aContentType;
  return NS_OK;
}

NS_IMETHODIMP
nsViewSourceChannel::GetContentCharset(nsACString& aContentCharset) {
  NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

  return mChannel->GetContentCharset(aContentCharset);
}

NS_IMETHODIMP
nsViewSourceChannel::SetContentCharset(const nsACString& aContentCharset) {
  NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

  return mChannel->SetContentCharset(aContentCharset);
}

// We don't forward these methods becacuse content-disposition isn't whitelisted
// (see GetResponseHeader/VisitResponseHeaders).
NS_IMETHODIMP
nsViewSourceChannel::GetContentDisposition(uint32_t* aContentDisposition) {
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
nsViewSourceChannel::SetContentDisposition(uint32_t aContentDisposition) {
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
nsViewSourceChannel::GetContentDispositionFilename(
    nsAString& aContentDispositionFilename) {
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
nsViewSourceChannel::SetContentDispositionFilename(
    const nsAString& aContentDispositionFilename) {
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
nsViewSourceChannel::GetContentDispositionHeader(
    nsACString& aContentDispositionHeader) {
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
nsViewSourceChannel::GetContentLength(int64_t* aContentLength) {
  NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

  return mChannel->GetContentLength(aContentLength);
}

NS_IMETHODIMP
nsViewSourceChannel::SetContentLength(int64_t aContentLength) {
  NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

  return mChannel->SetContentLength(aContentLength);
}

NS_IMETHODIMP
nsViewSourceChannel::GetLoadGroup(nsILoadGroup** aLoadGroup) {
  NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

  return mChannel->GetLoadGroup(aLoadGroup);
}

NS_IMETHODIMP
nsViewSourceChannel::SetLoadGroup(nsILoadGroup* aLoadGroup) {
  NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

  return mChannel->SetLoadGroup(aLoadGroup);
}

NS_IMETHODIMP
nsViewSourceChannel::GetOwner(nsISupports** aOwner) {
  NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

  return mChannel->GetOwner(aOwner);
}

NS_IMETHODIMP
nsViewSourceChannel::SetOwner(nsISupports* aOwner) {
  NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

  return mChannel->SetOwner(aOwner);
}

NS_IMETHODIMP
nsViewSourceChannel::GetLoadInfo(nsILoadInfo** aLoadInfo) {
  NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

  return mChannel->GetLoadInfo(aLoadInfo);
}

NS_IMETHODIMP
nsViewSourceChannel::SetLoadInfo(nsILoadInfo* aLoadInfo) {
  NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);
  MOZ_RELEASE_ASSERT(aLoadInfo, "loadinfo can't be null");
  return mChannel->SetLoadInfo(aLoadInfo);
}

NS_IMETHODIMP
nsViewSourceChannel::GetParentProcessChannelHandle(
    mozilla::dom::ParentProcessChannelHandle** aValue) {
  NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

  return mChannel->GetParentProcessChannelHandle(aValue);
}

NS_IMETHODIMP
nsViewSourceChannel::SetParentProcessChannelHandle(
    mozilla::dom::ParentProcessChannelHandle* aValue) {
  NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

  return mChannel->SetParentProcessChannelHandle(aValue);
}

NS_IMETHODIMP
nsViewSourceChannel::GetIsDocument(bool* aIsDocument) {
  NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

  return mChannel->GetIsDocument(aIsDocument);
}

NS_IMETHODIMP
nsViewSourceChannel::GetNotificationCallbacks(
    nsIInterfaceRequestor** aNotificationCallbacks) {
  NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

  return mChannel->GetNotificationCallbacks(aNotificationCallbacks);
}

NS_IMETHODIMP
nsViewSourceChannel::SetNotificationCallbacks(
    nsIInterfaceRequestor* aNotificationCallbacks) {
  NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

  return mChannel->SetNotificationCallbacks(aNotificationCallbacks);
}

NS_IMETHODIMP
nsViewSourceChannel::GetSecurityInfo(nsITransportSecurityInfo** aSecurityInfo) {
  NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

  return mChannel->GetSecurityInfo(aSecurityInfo);
}

// nsIViewSourceChannel methods
NS_IMETHODIMP
nsViewSourceChannel::GetOriginalContentType(nsACString& aContentType) {
  NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

  return mChannel->GetContentType(aContentType);
}

NS_IMETHODIMP
nsViewSourceChannel::SetOriginalContentType(const nsACString& aContentType) {
  NS_ENSURE_TRUE(mChannel, NS_ERROR_FAILURE);

  // clear our cached content-type value
  mContentType.Truncate();

  return mChannel->SetContentType(aContentType);
}

NS_IMETHODIMP
nsViewSourceChannel::GetIsSrcdocChannel(bool* aIsSrcdocChannel) {
  *aIsSrcdocChannel = mIsSrcdocChannel;
  return NS_OK;
}

NS_IMETHODIMP
nsViewSourceChannel::GetBaseURI(nsIURI** aBaseURI) {
  if (mIsSrcdocChannel) {
    nsCOMPtr<nsIInputStreamChannel> isc = do_QueryInterface(mChannel);
    if (isc) {
      return isc->GetBaseURI(aBaseURI);
    }
  }
  *aBaseURI = do_AddRef(mBaseURI).take();
  return NS_OK;
}

NS_IMETHODIMP
nsViewSourceChannel::SetBaseURI(nsIURI* aBaseURI) {
  mBaseURI = aBaseURI;
  return NS_OK;
}

nsIChannel* nsViewSourceChannel::GetInnerChannel() { return mChannel; }

NS_IMETHODIMP
nsViewSourceChannel::GetProtocolVersion(nsACString& aProtocolVersion) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

// nsIRequestObserver methods
NS_IMETHODIMP
nsViewSourceChannel::OnStartRequest(nsIRequest* aRequest) {
  NS_ENSURE_TRUE(mListener, NS_ERROR_FAILURE);
  // The channel may have gotten redirected... Time to update our info
  mChannel = do_QueryInterface(aRequest);
  UpdateChannelInterfaces();

  nsresult rv = UpdateLoadInfoResultPrincipalURI();
  if (NS_FAILED(rv)) {
    Cancel(rv);
  }

  nsCOMPtr<nsIStreamListener> listener = mListener;
  return listener->OnStartRequest(static_cast<nsIViewSourceChannel*>(this));
}

NS_IMETHODIMP
nsViewSourceChannel::OnStopRequest(nsIRequest* aRequest, nsresult aStatus) {
  NS_ENSURE_TRUE(mListener, NS_ERROR_FAILURE);
  if (mChannel) {
    nsCOMPtr<nsILoadGroup> loadGroup;
    mChannel->GetLoadGroup(getter_AddRefs(loadGroup));
    if (loadGroup) {
      loadGroup->RemoveRequest(static_cast<nsIViewSourceChannel*>(this),
                               nullptr, aStatus);
    }
  }

  nsCOMPtr<nsIStreamListener> listener = mListener;
  nsresult rv = listener->OnStopRequest(
      static_cast<nsIViewSourceChannel*>(this), aStatus);

  ReleaseListeners();

  return rv;
}

// nsIStreamListener methods
NS_IMETHODIMP
nsViewSourceChannel::OnDataAvailable(nsIRequest* aRequest,
                                     nsIInputStream* aInputStream,
                                     uint64_t aSourceOffset, uint32_t aLength) {
  NS_ENSURE_TRUE(mListener, NS_ERROR_FAILURE);
  nsCOMPtr<nsIStreamListener> listener = mListener;
  return listener->OnDataAvailable(static_cast<nsIViewSourceChannel*>(this),
                                   aInputStream, aSourceOffset, aLength);
}

// nsIHttpChannel methods

// We want to forward most of nsIHttpChannel over to mHttpChannel, but we want
// to override GetRequestHeader and VisitHeaders. The reason is that we don't
// want various headers like Link: and Refresh: applying to view-source.
NS_IMETHODIMP
nsViewSourceChannel::GetChannelId(uint64_t* aChannelId) {
  NS_ENSURE_ARG_POINTER(aChannelId);
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->GetChannelId(aChannelId);
}

NS_IMETHODIMP
nsViewSourceChannel::SetChannelId(uint64_t aChannelId) {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->SetChannelId(aChannelId);
}

NS_IMETHODIMP
nsViewSourceChannel::GetTopLevelContentWindowId(uint64_t* aWindowId) {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->GetTopLevelContentWindowId(aWindowId);
}

NS_IMETHODIMP
nsViewSourceChannel::SetTopLevelContentWindowId(uint64_t aWindowId) {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->SetTopLevelContentWindowId(aWindowId);
}

NS_IMETHODIMP
nsViewSourceChannel::GetBrowserId(uint64_t* aId) {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->GetBrowserId(aId);
}

NS_IMETHODIMP
nsViewSourceChannel::SetBrowserId(uint64_t aId) {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->SetBrowserId(aId);
}

NS_IMETHODIMP
nsViewSourceChannel::GetRequestMethod(nsACString& aRequestMethod) {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->GetRequestMethod(aRequestMethod);
}

NS_IMETHODIMP
nsViewSourceChannel::SetRequestMethod(const nsACString& aRequestMethod) {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->SetRequestMethod(aRequestMethod);
}

NS_IMETHODIMP
nsViewSourceChannel::GetReferrerInfo(nsIReferrerInfo** aReferrerInfo) {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->GetReferrerInfo(aReferrerInfo);
}

NS_IMETHODIMP
nsViewSourceChannel::SetReferrerInfo(nsIReferrerInfo* aReferrerInfo) {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->SetReferrerInfo(aReferrerInfo);
}

NS_IMETHODIMP nsViewSourceChannel::SetReferrerInfoWithoutClone(
    nsIReferrerInfo* aReferrerInfo) {
  return !mHttpChannel
             ? NS_ERROR_NULL_POINTER
             : mHttpChannel->SetReferrerInfoWithoutClone(aReferrerInfo);
}

NS_IMETHODIMP
nsViewSourceChannel::GetRequestHeader(const nsACString& aHeader,
                                      nsACString& aValue) {
  aValue.Truncate();
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->GetRequestHeader(aHeader, aValue);
}

NS_IMETHODIMP
nsViewSourceChannel::SetRequestHeader(const nsACString& aHeader,
                                      const nsACString& aValue, bool aMerge) {
  return !mHttpChannel
             ? NS_ERROR_NULL_POINTER
             : mHttpChannel->SetRequestHeader(aHeader, aValue, aMerge);
}

NS_IMETHODIMP
nsViewSourceChannel::SetNewReferrerInfo(
    const nsACString& aUrl, nsIReferrerInfo::ReferrerPolicyIDL aPolicy,
    bool aSendReferrer) {
  return !mHttpChannel
             ? NS_ERROR_NULL_POINTER
             : mHttpChannel->SetNewReferrerInfo(aUrl, aPolicy, aSendReferrer);
}

NS_IMETHODIMP
nsViewSourceChannel::SetEmptyRequestHeader(const nsACString& aHeader) {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->SetEmptyRequestHeader(aHeader);
}

NS_IMETHODIMP
nsViewSourceChannel::VisitRequestHeaders(nsIHttpHeaderVisitor* aVisitor) {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->VisitRequestHeaders(aVisitor);
}

NS_IMETHODIMP
nsViewSourceChannel::VisitNonDefaultRequestHeaders(
    nsIHttpHeaderVisitor* aVisitor) {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->VisitNonDefaultRequestHeaders(aVisitor);
}

NS_IMETHODIMP
nsViewSourceChannel::ShouldStripRequestBodyHeader(const nsACString& aMethod,
                                                  bool* aResult) {
  return !mHttpChannel
             ? NS_ERROR_NULL_POINTER
             : mHttpChannel->ShouldStripRequestBodyHeader(aMethod, aResult);
}

NS_IMETHODIMP
nsViewSourceChannel::GetAllowSTS(bool* aAllowSTS) {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->GetAllowSTS(aAllowSTS);
}

NS_IMETHODIMP
nsViewSourceChannel::SetAllowSTS(bool aAllowSTS) {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->SetAllowSTS(aAllowSTS);
}

NS_IMETHODIMP
nsViewSourceChannel::GetRedirectionLimit(uint32_t* aRedirectionLimit) {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->GetRedirectionLimit(aRedirectionLimit);
}

NS_IMETHODIMP
nsViewSourceChannel::SetRedirectionLimit(uint32_t aRedirectionLimit) {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->SetRedirectionLimit(aRedirectionLimit);
}

NS_IMETHODIMP
nsViewSourceChannel::GetResponseStatus(uint32_t* aResponseStatus) {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->GetResponseStatus(aResponseStatus);
}

NS_IMETHODIMP
nsViewSourceChannel::GetResponseStatusText(nsACString& aResponseStatusText) {
  return !mHttpChannel
             ? NS_ERROR_NULL_POINTER
             : mHttpChannel->GetResponseStatusText(aResponseStatusText);
}

NS_IMETHODIMP
nsViewSourceChannel::GetRequestSucceeded(bool* aRequestSucceeded) {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->GetRequestSucceeded(aRequestSucceeded);
}

NS_IMETHODIMP
nsViewSourceChannel::GetResponseHeader(const nsACString& aHeader,
                                       nsACString& aValue) {
  aValue.Truncate();
  if (!mHttpChannel) return NS_ERROR_NULL_POINTER;

  if (!aHeader.Equals("Content-Type"_ns, nsCaseInsensitiveCStringComparator) &&
      !aHeader.Equals("Content-Security-Policy"_ns,
                      nsCaseInsensitiveCStringComparator) &&
      !aHeader.Equals("Content-Security-Policy-Report-Only"_ns,
                      nsCaseInsensitiveCStringComparator) &&
      !aHeader.Equals("X-Frame-Options"_ns,
                      nsCaseInsensitiveCStringComparator)) {
    // We simulate the NS_ERROR_NOT_AVAILABLE error which is produced by
    // GetResponseHeader via nsHttpHeaderArray::GetHeader when the entry is
    // not present, such that it appears as though no headers except for the
    // whitelisted ones were set on this channel.
    return NS_ERROR_NOT_AVAILABLE;
  }

  return mHttpChannel->GetResponseHeader(aHeader, aValue);
}

NS_IMETHODIMP
nsViewSourceChannel::SetResponseHeader(const nsACString& header,
                                       const nsACString& value, bool merge) {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->SetResponseHeader(header, value, merge);
}

NS_IMETHODIMP
nsViewSourceChannel::VisitResponseHeaders(nsIHttpHeaderVisitor* aVisitor) {
  if (!mHttpChannel) return NS_ERROR_NULL_POINTER;

  constexpr auto contentTypeStr = "Content-Type"_ns;
  nsAutoCString contentType;
  nsresult rv = mHttpChannel->GetResponseHeader(contentTypeStr, contentType);
  if (NS_SUCCEEDED(rv)) {
    return aVisitor->VisitHeader(contentTypeStr, contentType);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsViewSourceChannel::GetOriginalResponseHeader(const nsACString& aHeader,
                                               nsIHttpHeaderVisitor* aVisitor) {
  nsAutoCString value;
  nsresult rv = GetResponseHeader(aHeader, value);
  if (NS_FAILED(rv)) {
    return rv;
  }
  return aVisitor->VisitHeader(aHeader, value);
}

NS_IMETHODIMP
nsViewSourceChannel::VisitOriginalResponseHeaders(
    nsIHttpHeaderVisitor* aVisitor) {
  return VisitResponseHeaders(aVisitor);
}

NS_IMETHODIMP
nsViewSourceChannel::IsNoStoreResponse(bool* _retval) {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->IsNoStoreResponse(_retval);
}

NS_IMETHODIMP
nsViewSourceChannel::IsNoCacheResponse(bool* _retval) {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->IsNoCacheResponse(_retval);
}

NS_IMETHODIMP
nsViewSourceChannel::RedirectTo(nsIURI* uri) {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER : mHttpChannel->RedirectTo(uri);
}

NS_IMETHODIMP
nsViewSourceChannel::UpgradeToSecure() {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->UpgradeToSecure();
}

NS_IMETHODIMP
nsViewSourceChannel::GetRequestObserversCalled(bool* aCalled) {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->GetRequestObserversCalled(aCalled);
}

NS_IMETHODIMP
nsViewSourceChannel::SetRequestObserversCalled(bool aCalled) {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->SetRequestObserversCalled(aCalled);
}

NS_IMETHODIMP
nsViewSourceChannel::GetRequestContextID(uint64_t* _retval) {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->GetRequestContextID(_retval);
}

NS_IMETHODIMP
nsViewSourceChannel::SetRequestContextID(uint64_t rcid) {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->SetRequestContextID(rcid);
}

NS_IMETHODIMP
nsViewSourceChannel::GetIsUserAgentHeaderOutdated(bool* aValue) {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->GetIsUserAgentHeaderOutdated(aValue);
}

NS_IMETHODIMP
nsViewSourceChannel::SetIsUserAgentHeaderOutdated(bool aValue) {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->SetIsUserAgentHeaderOutdated(aValue);
}

NS_IMETHODIMP
nsViewSourceChannel::GetIsMainDocumentChannel(bool* aValue) {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->GetIsMainDocumentChannel(aValue);
}

NS_IMETHODIMP
nsViewSourceChannel::SetIsMainDocumentChannel(bool aValue) {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->SetIsMainDocumentChannel(aValue);
}

NS_IMETHODIMP nsViewSourceChannel::SetClassicScriptHintCharset(
    const nsAString& aClassicScriptHintCharset) {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->SetClassicScriptHintCharset(
                             aClassicScriptHintCharset);
}

NS_IMETHODIMP nsViewSourceChannel::GetClassicScriptHintCharset(
    nsAString& aClassicScriptHintCharset) {
  return !mHttpChannel ? NS_ERROR_NULL_POINTER
                       : mHttpChannel->GetClassicScriptHintCharset(
                             aClassicScriptHintCharset);
}

NS_IMETHODIMP nsViewSourceChannel::SetDocumentCharacterSet(
    const nsAString& aDocumentCharacterSet) {
  return !mHttpChannel
             ? NS_ERROR_NULL_POINTER
             : mHttpChannel->SetDocumentCharacterSet(aDocumentCharacterSet);
}

NS_IMETHODIMP nsViewSourceChannel::GetDocumentCharacterSet(
    nsAString& aDocumentCharacterSet) {
  return !mHttpChannel
             ? NS_ERROR_NULL_POINTER
             : mHttpChannel->GetDocumentCharacterSet(aDocumentCharacterSet);
}
// Have to manually forward SetCorsPreflightParameters since it's [notxpcom]
void nsViewSourceChannel::SetCorsPreflightParameters(
    const nsTArray<nsCString>& aUnsafeHeaders,
    bool aShouldStripRequestBodyHeader, bool aShouldStripAuthHeader) {
  mHttpChannelInternal->SetCorsPreflightParameters(
      aUnsafeHeaders, aShouldStripRequestBodyHeader, aShouldStripAuthHeader);
}

void nsViewSourceChannel::SetAltDataForChild(bool aIsForChild) {
  mHttpChannelInternal->SetAltDataForChild(aIsForChild);
}

void nsViewSourceChannel::DisableAltDataCache() {
  mHttpChannelInternal->DisableAltDataCache();
}

NS_IMETHODIMP
nsViewSourceChannel::LogBlockedCORSRequest(const nsAString& aMessage,
                                           const nsACString& aCategory,
                                           bool aIsWarning) {
  if (!mHttpChannel) {
    NS_WARNING(
        "nsViewSourceChannel::LogBlockedCORSRequest mHttpChannel is null");
    return NS_ERROR_UNEXPECTED;
  }
  return mHttpChannel->LogBlockedCORSRequest(aMessage, aCategory, aIsWarning);
}

NS_IMETHODIMP
nsViewSourceChannel::LogMimeTypeMismatch(const nsACString& aMessageName,
                                         bool aWarning, const nsAString& aURL,
                                         const nsAString& aContentType) {
  if (!mHttpChannel) {
    NS_WARNING("nsViewSourceChannel::LogMimeTypeMismatch mHttpChannel is null");
    return NS_ERROR_UNEXPECTED;
  }
  return mHttpChannel->LogMimeTypeMismatch(aMessageName, aWarning, aURL,
                                           aContentType);
}

// FIXME: Should this forward to mHttpChannel? This was previously handled by a
// default empty implementation.
void nsViewSourceChannel::SetSource(
    mozilla::UniquePtr<mozilla::ProfileChunkedBuffer> aSource) {}

const nsTArray<mozilla::net::PreferredAlternativeDataTypeParams>&
nsViewSourceChannel::PreferredAlternativeDataTypes() {
  if (mCacheInfoChannel) {
    return mCacheInfoChannel->PreferredAlternativeDataTypes();
  }
  return mEmptyArray;
}

void nsViewSourceChannel::DoDiagnosticAssertWhenOnStopNotCalledOnDestroy() {
  if (mHttpChannelInternal) {
    mHttpChannelInternal->DoDiagnosticAssertWhenOnStopNotCalledOnDestroy();
  }
}

// FIXME: Should this forward to mHttpChannelInternal? This was previously
// handled by a default empty implementation.
void nsViewSourceChannel::SetConnectionInfo(
    mozilla::net::nsHttpConnectionInfo* aInfo) {}

// nsIChildChannel methods

NS_IMETHODIMP
nsViewSourceChannel::ConnectParent(uint32_t aRegistarId) {
  NS_ENSURE_TRUE(mChildChannel, NS_ERROR_NULL_POINTER);

  return mChildChannel->ConnectParent(aRegistarId);
}

NS_IMETHODIMP
nsViewSourceChannel::CompleteRedirectSetup(nsIStreamListener* aListener) {
  NS_ENSURE_TRUE(mChildChannel, NS_ERROR_NULL_POINTER);

  mListener = aListener;

  /*
   * We want to add ourselves to the loadgroup before opening
   * mChannel, since we want to make sure we're in the loadgroup
   * when mChannel finishes and fires OnStopRequest()
   */

  nsCOMPtr<nsILoadGroup> loadGroup;
  mChannel->GetLoadGroup(getter_AddRefs(loadGroup));
  if (loadGroup) {
    loadGroup->AddRequest(static_cast<nsIViewSourceChannel*>(this), nullptr);
  }

  nsresult rv = NS_OK;
  rv = mChildChannel->CompleteRedirectSetup(this);

  if (NS_FAILED(rv) && loadGroup) {
    loadGroup->RemoveRequest(static_cast<nsIViewSourceChannel*>(this), nullptr,
                             rv);
  }

  if (NS_SUCCEEDED(rv)) {
    mOpened = true;
  }

  return rv;
}

// nsIChannelEventSink

NS_IMETHODIMP
nsViewSourceChannel::AsyncOnChannelRedirect(
    nsIChannel* oldChannel, nsIChannel* newChannel, uint32_t flags,
    nsIAsyncVerifyRedirectCallback* callback) {
  nsresult rv;

  // We want consumers of the new inner channel be able to recognize it's
  // actually used for view-source.  Hence we modify its original URI here.
  // This will not affect security checks because those have already been
  // synchronously done before our event sink is called.  Note that result
  // principal URI is still modified at the OnStartRequest notification.
  nsCOMPtr<nsIURI> newChannelOrigURI;
  rv = newChannel->GetOriginalURI(getter_AddRefs(newChannelOrigURI));
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString scheme;
  rv = newChannelOrigURI->GetScheme(scheme);
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsCOMPtr<nsIIOService> ioService(do_GetIOService(&rv));
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = WillUseExternalProtocolHandler(ioService, scheme.get());
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsCOMPtr<nsIURI> newChannelUpdatedOrigURI;
  rv = BuildViewSourceURI(newChannelOrigURI,
                          getter_AddRefs(newChannelUpdatedOrigURI));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = newChannel->SetOriginalURI(newChannelUpdatedOrigURI);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIChannelEventSink> sink(do_QueryInterface(mCallbacks));
  if (sink) {
    return sink->AsyncOnChannelRedirect(oldChannel, newChannel, flags,
                                        callback);
  }

  callback->OnRedirectVerifyCallback(NS_OK);
  return NS_OK;
}

NS_IMETHODIMP
nsViewSourceChannel::GetDecompressDictionary(
    mozilla::net::DictionaryCacheEntry** aDictionary) {
  *aDictionary = nullptr;
  return NS_OK;
}

NS_IMETHODIMP
nsViewSourceChannel::SetDecompressDictionary(
    mozilla::net::DictionaryCacheEntry* aDictionary) {
  return NS_OK;
}

// nsIInterfaceRequestor

NS_IMETHODIMP
nsViewSourceChannel::GetInterface(const nsIID& aIID, void** aResult) {
  if (aIID.Equals(NS_GET_IID(nsIChannelEventSink))) {
    nsCOMPtr<nsIChannelEventSink> self(this);
    self.forget(aResult);
    return NS_OK;
  }

  if (mCallbacks) {
    return mCallbacks->GetInterface(aIID, aResult);
  }

  return NS_ERROR_NO_INTERFACE;
}
