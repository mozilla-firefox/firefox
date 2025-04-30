/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=4 sw=2 sts=2 et cin: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "DecoderDoctorDiagnostics.h"
#include "HttpLog.h"

#include "nsNetUtil.h"

#include "mozilla/Atomics.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/Components.h"
#include "mozilla/Encoding.h"
#include "mozilla/LoadContext.h"
#include "mozilla/LoadInfo.h"
#include "mozilla/Monitor.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/StaticPrefs_privacy.h"
#include "mozilla/StoragePrincipalHelper.h"
#include "mozilla/TaskQueue.h"
#include "nsAboutProtocolUtils.h"
#include "nsBufferedStreams.h"
#include "nsCategoryCache.h"
#include "nsComponentManagerUtils.h"
#include "nsContentUtils.h"
#include "nsEscape.h"
#include "nsFileStreams.h"
#include "nsHashKeys.h"
#include "nsHttp.h"
#include "nsMimeTypes.h"
#include "nsIAuthPrompt.h"
#include "nsIAuthPrompt2.h"
#include "nsIAuthPromptAdapterFactory.h"
#include "nsIBufferedStreams.h"
#include "nsBufferedStreams.h"
#include "nsIChannelEventSink.h"
#include "nsIContentSniffer.h"
#include "mozilla/dom/Document.h"
#include "nsIDownloader.h"
#include "nsIFileProtocolHandler.h"
#include "nsIFileStreams.h"
#include "nsIFileURL.h"
#include "nsIIDNService.h"
#include "nsIInputStreamChannel.h"
#include "nsIInputStreamPump.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsILoadContext.h"
#include "nsIMIMEHeaderParam.h"
#include "nsINode.h"
#include "nsIObjectLoadingContent.h"
#include "nsPersistentProperties.h"
#include "nsIPrivateBrowsingChannel.h"
#include "nsIPropertyBag2.h"
#include "nsIProtocolProxyService.h"
#include "mozilla/net/RedirectChannelRegistrar.h"
#include "nsRequestObserverProxy.h"
#include "nsISensitiveInfoHiddenURI.h"
#include "nsISimpleStreamListener.h"
#include "nsISocketProvider.h"
#include "nsIStandardURL.h"
#include "nsIStreamLoader.h"
#include "nsIIncrementalStreamLoader.h"
#include "nsStringStream.h"
#include "nsSyncStreamListener.h"
#include "nsITextToSubURI.h"
#include "nsIURIWithSpecialOrigin.h"
#include "nsIViewSourceChannel.h"
#include "nsInterfaceRequestorAgg.h"
#include "nsINestedURI.h"
#include "mozilla/dom/nsCSPUtils.h"
#include "mozilla/dom/nsHTTPSOnlyUtils.h"
#include "mozilla/dom/nsMixedContentBlocker.h"
#include "mozilla/dom/BlobURLProtocolHandler.h"
#include "mozilla/net/HttpBaseChannel.h"
#include "nsIScriptError.h"
#include "nsISiteSecurityService.h"
#include "nsHttpHandler.h"
#include "nsNSSComponent.h"
#include "nsIRedirectHistoryEntry.h"
#include "nsICertStorage.h"
#include "nsICertOverrideService.h"
#include "nsQueryObject.h"
#include "mozIThirdPartyUtil.h"
#include "../mime/nsMIMEHeaderParamImpl.h"
#include "nsStandardURL.h"
#include "DefaultURI.h"
#include "nsChromeProtocolHandler.h"
#include "nsJSProtocolHandler.h"
#include "nsDataHandler.h"
#include "mozilla/dom/BlobURLProtocolHandler.h"
#include "nsStreamUtils.h"
#include "nsSocketTransportService2.h"
#include "nsViewSourceHandler.h"
#include "nsJARURI.h"
#ifndef XP_IOS
#  include "nsIconURI.h"
#endif
#include "nsAboutProtocolHandler.h"
#include "nsResProtocolHandler.h"
#include "mozilla/net/CookieJarSettings.h"
#include "mozilla/net/MozSrcProtocolHandler.h"
#include "mozilla/net/ExtensionProtocolHandler.h"
#include "mozilla/net/PageThumbProtocolHandler.h"
#include "mozilla/net/SFVService.h"
#include <limits>
#include "nsICookieService.h"
#include "nsIXPConnect.h"
#include "nsParserConstants.h"
#include "nsCRT.h"
#include "nsServiceManagerUtils.h"
#include "mozilla/dom/MediaList.h"
#include "MediaContainerType.h"
#include "DecoderTraits.h"
#include "imgLoader.h"

#if defined(MOZ_THUNDERBIRD) || defined(MOZ_SUITE)
#  include "nsNewMailnewsURI.h"
#endif

using namespace mozilla;
using namespace mozilla::net;
using mozilla::dom::BlobURLProtocolHandler;
using mozilla::dom::ClientInfo;
using mozilla::dom::PerformanceStorage;
using mozilla::dom::ServiceWorkerDescriptor;

#define MAX_RECURSION_COUNT 50

already_AddRefed<nsIIOService> do_GetIOService(nsresult* error /* = 0 */) {
  nsCOMPtr<nsIIOService> io;
  io = mozilla::components::IO::Service();
  if (error) *error = io ? NS_OK : NS_ERROR_FAILURE;
  return io.forget();
}

nsresult NS_NewLocalFileInputStream(nsIInputStream** result, nsIFile* file,
                                    int32_t ioFlags /* = -1 */,
                                    int32_t perm /* = -1 */,
                                    int32_t behaviorFlags /* = 0 */) {
  nsresult rv;
  nsCOMPtr<nsIFileInputStream> in =
      do_CreateInstance(NS_LOCALFILEINPUTSTREAM_CONTRACTID, &rv);
  if (NS_SUCCEEDED(rv)) {
    rv = in->Init(file, ioFlags, perm, behaviorFlags);
    if (NS_SUCCEEDED(rv)) in.forget(result);
  }
  return rv;
}

Result<nsCOMPtr<nsIInputStream>, nsresult> NS_NewLocalFileInputStream(
    nsIFile* file, int32_t ioFlags /* = -1 */, int32_t perm /* = -1 */,
    int32_t behaviorFlags /* = 0 */) {
  nsCOMPtr<nsIInputStream> stream;
  const nsresult rv = NS_NewLocalFileInputStream(getter_AddRefs(stream), file,
                                                 ioFlags, perm, behaviorFlags);
  if (NS_SUCCEEDED(rv)) {
    return stream;
  }
  return Err(rv);
}

nsresult NS_NewLocalFileOutputStream(nsIOutputStream** result, nsIFile* file,
                                     int32_t ioFlags /* = -1 */,
                                     int32_t perm /* = -1 */,
                                     int32_t behaviorFlags /* = 0 */) {
  nsresult rv;
  nsCOMPtr<nsIFileOutputStream> out =
      do_CreateInstance(NS_LOCALFILEOUTPUTSTREAM_CONTRACTID, &rv);
  if (NS_SUCCEEDED(rv)) {
    rv = out->Init(file, ioFlags, perm, behaviorFlags);
    if (NS_SUCCEEDED(rv)) out.forget(result);
  }
  return rv;
}

Result<nsCOMPtr<nsIOutputStream>, nsresult> NS_NewLocalFileOutputStream(
    nsIFile* file, int32_t ioFlags /* = -1 */, int32_t perm /* = -1 */,
    int32_t behaviorFlags /* = 0 */) {
  nsCOMPtr<nsIOutputStream> stream;
  const nsresult rv = NS_NewLocalFileOutputStream(getter_AddRefs(stream), file,
                                                  ioFlags, perm, behaviorFlags);
  if (NS_SUCCEEDED(rv)) {
    return stream;
  }
  return Err(rv);
}

nsresult NS_NewLocalFileOutputStream(nsIOutputStream** result,
                                     const mozilla::ipc::FileDescriptor& fd) {
  nsCOMPtr<nsIFileOutputStream> out;
  nsFileOutputStream::Create(NS_GET_IID(nsIFileOutputStream),
                             getter_AddRefs(out));

  nsresult rv =
      static_cast<nsFileOutputStream*>(out.get())->InitWithFileDescriptor(fd);
  if (NS_FAILED(rv)) {
    return rv;
  }

  out.forget(result);
  return NS_OK;
}

nsresult net_EnsureIOService(nsIIOService** ios, nsCOMPtr<nsIIOService>& grip) {
  nsresult rv = NS_OK;
  if (!*ios) {
    grip = do_GetIOService(&rv);
    *ios = grip;
  }
  return rv;
}

nsresult NS_NewFileURI(
    nsIURI** result, nsIFile* spec,
    nsIIOService*
        ioService /* = nullptr */)  // pass in nsIIOService to optimize callers
{
  nsresult rv;
  nsCOMPtr<nsIIOService> grip;
  rv = net_EnsureIOService(&ioService, grip);
  if (ioService) rv = ioService->NewFileURI(spec, result);
  return rv;
}

nsresult NS_GetURIWithNewRef(nsIURI* aInput, const nsACString& aRef,
                             nsIURI** aOutput) {
  MOZ_DIAGNOSTIC_ASSERT(aRef.IsEmpty() || aRef[0] == '#');

  if (NS_WARN_IF(!aInput || !aOutput)) {
    return NS_ERROR_INVALID_ARG;
  }

  bool hasRef;
  nsresult rv = aInput->GetHasRef(&hasRef);

  nsAutoCString ref;
  if (NS_SUCCEEDED(rv)) {
    rv = aInput->GetRef(ref);
  }

  // If the ref is already equal to the new ref, we do not need to do anything.
  // Also, if the GetRef failed (it could return NS_ERROR_NOT_IMPLEMENTED)
  // we can assume SetRef would fail as well, so returning the original
  // URI is OK.
  //
  // Note that aRef contains the hash, but ref doesn't, so need to account for
  // that in the equality check.
  if (NS_FAILED(rv) || (!hasRef && aRef.IsEmpty()) ||
      (!aRef.IsEmpty() && hasRef &&
       Substring(aRef.Data() + 1, aRef.Length() - 1) == ref)) {
    nsCOMPtr<nsIURI> uri = aInput;
    uri.forget(aOutput);
    return NS_OK;
  }

  return NS_MutateURI(aInput).SetRef(aRef).Finalize(aOutput);
}

nsresult NS_GetURIWithoutRef(nsIURI* aInput, nsIURI** aOutput) {
  return NS_GetURIWithNewRef(aInput, ""_ns, aOutput);
}

nsresult NS_NewChannelInternal(
    nsIChannel** outChannel, nsIURI* aUri, nsILoadInfo* aLoadInfo,
    PerformanceStorage* aPerformanceStorage /* = nullptr */,
    nsILoadGroup* aLoadGroup /* = nullptr */,
    nsIInterfaceRequestor* aCallbacks /* = nullptr */,
    nsLoadFlags aLoadFlags /* = nsIRequest::LOAD_NORMAL */,
    nsIIOService* aIoService /* = nullptr */) {
  // NS_NewChannelInternal is mostly called for channel redirects. We should
  // allow the creation of a channel even if the original channel did not have a
  // loadinfo attached.
  NS_ENSURE_ARG_POINTER(outChannel);

  nsCOMPtr<nsIIOService> grip;
  nsresult rv = net_EnsureIOService(&aIoService, grip);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIChannel> channel;
  rv = aIoService->NewChannelFromURIWithLoadInfo(aUri, aLoadInfo,
                                                 getter_AddRefs(channel));
  NS_ENSURE_SUCCESS(rv, rv);

  if (aLoadGroup) {
    rv = channel->SetLoadGroup(aLoadGroup);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (aCallbacks) {
    rv = channel->SetNotificationCallbacks(aCallbacks);
    NS_ENSURE_SUCCESS(rv, rv);
  }

#ifdef DEBUG
  nsLoadFlags channelLoadFlags = 0;
  channel->GetLoadFlags(&channelLoadFlags);
  // Will be removed when we remove LOAD_REPLACE altogether
  // This check is trying to catch protocol handlers that still
  // try to set the LOAD_REPLACE flag.
  MOZ_DIAGNOSTIC_ASSERT(!(channelLoadFlags & nsIChannel::LOAD_REPLACE));
#endif

  if (aLoadFlags != nsIRequest::LOAD_NORMAL) {
    rv = channel->SetLoadFlags(aLoadFlags);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (aPerformanceStorage) {
    nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();
    loadInfo->SetPerformanceStorage(aPerformanceStorage);
  }

  channel.forget(outChannel);
  return NS_OK;
}

namespace {

void AssertLoadingPrincipalAndClientInfoMatch(
    nsIPrincipal* aLoadingPrincipal, const ClientInfo& aLoadingClientInfo,
    nsContentPolicyType aType) {
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  // Verify that the provided loading ClientInfo matches the loading
  // principal.  Unfortunately we can't just use nsIPrincipal::Equals() here
  // because of some corner cases:
  //
  //  1. Worker debugger scripts want to use a system loading principal for
  //     worker scripts with a content principal.  We exempt these from this
  //     check.
  //  2. Null principals currently require exact object identity for
  //     nsIPrincipal::Equals() to return true.  This doesn't work here because
  //     ClientInfo::GetPrincipal() uses PrincipalInfoToPrincipal() to allocate
  //     a new object.  To work around this we compare the principal origin
  //     string itself.  If bug 1431771 is fixed then we could switch to
  //     Equals().

  // Allow worker debugger to load with a system principal.
  if (aLoadingPrincipal->IsSystemPrincipal() &&
      (aType == nsIContentPolicy::TYPE_INTERNAL_WORKER ||
       aType == nsIContentPolicy::TYPE_INTERNAL_SHARED_WORKER ||
       aType == nsIContentPolicy::TYPE_INTERNAL_SERVICE_WORKER ||
       aType == nsIContentPolicy::TYPE_INTERNAL_WORKER_IMPORT_SCRIPTS ||
       aType == nsIContentPolicy::TYPE_INTERNAL_WORKER_STATIC_MODULE)) {
    return;
  }

  // Perform a fast comparison for most principal checks.
  auto clientPrincipalOrErr(aLoadingClientInfo.GetPrincipal());
  if (clientPrincipalOrErr.isOk()) {
    nsCOMPtr<nsIPrincipal> clientPrincipal = clientPrincipalOrErr.unwrap();
    if (aLoadingPrincipal->Equals(clientPrincipal)) {
      return;
    }
    // Fall back to a slower origin equality test to support null principals.
    nsAutoCString loadingOriginNoSuffix;
    MOZ_ALWAYS_SUCCEEDS(
        aLoadingPrincipal->GetOriginNoSuffix(loadingOriginNoSuffix));

    nsAutoCString clientOriginNoSuffix;
    MOZ_ALWAYS_SUCCEEDS(
        clientPrincipal->GetOriginNoSuffix(clientOriginNoSuffix));

    // The client principal will have the partitionKey set if it's in a third
    // party context, but the loading principal won't. So, we ignore he
    // partitionKey when doing the verification here.
    MOZ_DIAGNOSTIC_ASSERT(loadingOriginNoSuffix == clientOriginNoSuffix);
    MOZ_DIAGNOSTIC_ASSERT(
        aLoadingPrincipal->OriginAttributesRef().EqualsIgnoringPartitionKey(
            clientPrincipal->OriginAttributesRef()));
  }
#endif
}

}  // namespace

nsresult NS_NewChannel(nsIChannel** outChannel, nsIURI* aUri,
                       nsIPrincipal* aLoadingPrincipal,
                       nsSecurityFlags aSecurityFlags,
                       nsContentPolicyType aContentPolicyType,
                       nsICookieJarSettings* aCookieJarSettings /* = nullptr */,
                       PerformanceStorage* aPerformanceStorage /* = nullptr */,
                       nsILoadGroup* aLoadGroup /* = nullptr */,
                       nsIInterfaceRequestor* aCallbacks /* = nullptr */,
                       nsLoadFlags aLoadFlags /* = nsIRequest::LOAD_NORMAL */,
                       nsIIOService* aIoService /* = nullptr */,
                       uint32_t aSandboxFlags /* = 0 */) {
  return NS_NewChannelInternal(
      outChannel, aUri,
      nullptr,  // aLoadingNode,
      aLoadingPrincipal,
      nullptr,  // aTriggeringPrincipal
      Maybe<ClientInfo>(), Maybe<ServiceWorkerDescriptor>(), aSecurityFlags,
      aContentPolicyType, aCookieJarSettings, aPerformanceStorage, aLoadGroup,
      aCallbacks, aLoadFlags, aIoService, aSandboxFlags);
}

nsresult NS_NewChannel(nsIChannel** outChannel, nsIURI* aUri,
                       nsIPrincipal* aLoadingPrincipal,
                       const ClientInfo& aLoadingClientInfo,
                       const Maybe<ServiceWorkerDescriptor>& aController,
                       nsSecurityFlags aSecurityFlags,
                       nsContentPolicyType aContentPolicyType,
                       nsICookieJarSettings* aCookieJarSettings /* = nullptr */,
                       PerformanceStorage* aPerformanceStorage /* = nullptr */,
                       nsILoadGroup* aLoadGroup /* = nullptr */,
                       nsIInterfaceRequestor* aCallbacks /* = nullptr */,
                       nsLoadFlags aLoadFlags /* = nsIRequest::LOAD_NORMAL */,
                       nsIIOService* aIoService /* = nullptr */,
                       uint32_t aSandboxFlags /* = 0 */) {
  AssertLoadingPrincipalAndClientInfoMatch(
      aLoadingPrincipal, aLoadingClientInfo, aContentPolicyType);

  Maybe<ClientInfo> loadingClientInfo;
  loadingClientInfo.emplace(aLoadingClientInfo);

  return NS_NewChannelInternal(outChannel, aUri,
                               nullptr,  // aLoadingNode,
                               aLoadingPrincipal,
                               nullptr,  // aTriggeringPrincipal
                               loadingClientInfo, aController, aSecurityFlags,
                               aContentPolicyType, aCookieJarSettings,
                               aPerformanceStorage, aLoadGroup, aCallbacks,
                               aLoadFlags, aIoService, aSandboxFlags);
}

nsresult NS_NewChannelInternal(
    nsIChannel** outChannel, nsIURI* aUri, nsINode* aLoadingNode,
    nsIPrincipal* aLoadingPrincipal, nsIPrincipal* aTriggeringPrincipal,
    const Maybe<ClientInfo>& aLoadingClientInfo,
    const Maybe<ServiceWorkerDescriptor>& aController,
    nsSecurityFlags aSecurityFlags, nsContentPolicyType aContentPolicyType,
    nsICookieJarSettings* aCookieJarSettings /* = nullptr */,
    PerformanceStorage* aPerformanceStorage /* = nullptr */,
    nsILoadGroup* aLoadGroup /* = nullptr */,
    nsIInterfaceRequestor* aCallbacks /* = nullptr */,
    nsLoadFlags aLoadFlags /* = nsIRequest::LOAD_NORMAL */,
    nsIIOService* aIoService /* = nullptr */,
    uint32_t aSandboxFlags /* = 0 */) {
  NS_ENSURE_ARG_POINTER(outChannel);

  nsCOMPtr<nsIIOService> grip;
  nsresult rv = net_EnsureIOService(&aIoService, grip);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIChannel> channel;
  rv = aIoService->NewChannelFromURIWithClientAndController(
      aUri, aLoadingNode, aLoadingPrincipal, aTriggeringPrincipal,
      aLoadingClientInfo, aController, aSecurityFlags, aContentPolicyType,
      aSandboxFlags, getter_AddRefs(channel));
  if (NS_FAILED(rv)) {
    return rv;
  }

  if (aLoadGroup) {
    rv = channel->SetLoadGroup(aLoadGroup);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (aCallbacks) {
    rv = channel->SetNotificationCallbacks(aCallbacks);
    NS_ENSURE_SUCCESS(rv, rv);
  }

#ifdef DEBUG
  nsLoadFlags channelLoadFlags = 0;
  channel->GetLoadFlags(&channelLoadFlags);
  // Will be removed when we remove LOAD_REPLACE altogether
  // This check is trying to catch protocol handlers that still
  // try to set the LOAD_REPLACE flag.
  MOZ_DIAGNOSTIC_ASSERT(!(channelLoadFlags & nsIChannel::LOAD_REPLACE));
#endif

  if (aLoadFlags != nsIRequest::LOAD_NORMAL) {
    rv = channel->SetLoadFlags(aLoadFlags);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (aPerformanceStorage || aCookieJarSettings) {
    nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();

    if (aPerformanceStorage) {
      loadInfo->SetPerformanceStorage(aPerformanceStorage);
    }

    if (aCookieJarSettings) {
      loadInfo->SetCookieJarSettings(aCookieJarSettings);
    }
  }

  channel.forget(outChannel);
  return NS_OK;
}

nsresult /*NS_NewChannelWithNodeAndTriggeringPrincipal */
NS_NewChannelWithTriggeringPrincipal(
    nsIChannel** outChannel, nsIURI* aUri, nsINode* aLoadingNode,
    nsIPrincipal* aTriggeringPrincipal, nsSecurityFlags aSecurityFlags,
    nsContentPolicyType aContentPolicyType,
    PerformanceStorage* aPerformanceStorage /* = nullptr */,
    nsILoadGroup* aLoadGroup /* = nullptr */,
    nsIInterfaceRequestor* aCallbacks /* = nullptr */,
    nsLoadFlags aLoadFlags /* = nsIRequest::LOAD_NORMAL */,
    nsIIOService* aIoService /* = nullptr */) {
  MOZ_ASSERT(aLoadingNode);
  NS_ASSERTION(aTriggeringPrincipal,
               "Can not create channel without a triggering Principal!");

  nsCOMPtr<nsICookieJarSettings> cookieJarSettings;

  // Special treatment for resources injected by add-ons.
  if (aTriggeringPrincipal &&
      StaticPrefs::privacy_antitracking_isolateContentScriptResources() &&
      nsContentUtils::IsExpandedPrincipal(aTriggeringPrincipal)) {
    bool shouldResistFingerprinting =
        nsContentUtils::ShouldResistFingerprinting_dangerous(
            aLoadingNode->NodePrincipal(),
            "CookieJarSettings can't exist yet, we're creating it",
            RFPTarget::IsAlwaysEnabledForPrecompute);
    cookieJarSettings = CookieJarSettings::Create(
        nsICookieService::BEHAVIOR_REJECT,
        StoragePrincipalHelper::PartitionKeyForExpandedPrincipal(
            aTriggeringPrincipal),
        OriginAttributes::IsFirstPartyEnabled(), false,
        shouldResistFingerprinting);
  } else {
    // Let's inherit the cookie behavior and permission from the parent
    // document.
    cookieJarSettings = aLoadingNode->OwnerDoc()->CookieJarSettings();
  }

  return NS_NewChannelInternal(
      outChannel, aUri, aLoadingNode, aLoadingNode->NodePrincipal(),
      aTriggeringPrincipal, Maybe<ClientInfo>(),
      Maybe<ServiceWorkerDescriptor>(), aSecurityFlags, aContentPolicyType,
      cookieJarSettings, aPerformanceStorage, aLoadGroup, aCallbacks,
      aLoadFlags, aIoService);
}

// See NS_NewChannelInternal for usage and argument description
nsresult NS_NewChannelWithTriggeringPrincipal(
    nsIChannel** outChannel, nsIURI* aUri, nsIPrincipal* aLoadingPrincipal,
    nsIPrincipal* aTriggeringPrincipal, nsSecurityFlags aSecurityFlags,
    nsContentPolicyType aContentPolicyType,
    nsICookieJarSettings* aCookieJarSettings /* = nullptr */,
    PerformanceStorage* aPerformanceStorage /* = nullptr */,
    nsILoadGroup* aLoadGroup /* = nullptr */,
    nsIInterfaceRequestor* aCallbacks /* = nullptr */,
    nsLoadFlags aLoadFlags /* = nsIRequest::LOAD_NORMAL */,
    nsIIOService* aIoService /* = nullptr */) {
  NS_ASSERTION(aLoadingPrincipal,
               "Can not create channel without a loading Principal!");
  return NS_NewChannelInternal(
      outChannel, aUri,
      nullptr,  // aLoadingNode
      aLoadingPrincipal, aTriggeringPrincipal, Maybe<ClientInfo>(),
      Maybe<ServiceWorkerDescriptor>(), aSecurityFlags, aContentPolicyType,
      aCookieJarSettings, aPerformanceStorage, aLoadGroup, aCallbacks,
      aLoadFlags, aIoService);
}

// See NS_NewChannelInternal for usage and argument description
nsresult NS_NewChannelWithTriggeringPrincipal(
    nsIChannel** outChannel, nsIURI* aUri, nsIPrincipal* aLoadingPrincipal,
    nsIPrincipal* aTriggeringPrincipal, const ClientInfo& aLoadingClientInfo,
    const Maybe<ServiceWorkerDescriptor>& aController,
    nsSecurityFlags aSecurityFlags, nsContentPolicyType aContentPolicyType,
    nsICookieJarSettings* aCookieJarSettings /* = nullptr */,
    PerformanceStorage* aPerformanceStorage /* = nullptr */,
    nsILoadGroup* aLoadGroup /* = nullptr */,
    nsIInterfaceRequestor* aCallbacks /* = nullptr */,
    nsLoadFlags aLoadFlags /* = nsIRequest::LOAD_NORMAL */,
    nsIIOService* aIoService /* = nullptr */) {
  AssertLoadingPrincipalAndClientInfoMatch(
      aLoadingPrincipal, aLoadingClientInfo, aContentPolicyType);

  Maybe<ClientInfo> loadingClientInfo;
  loadingClientInfo.emplace(aLoadingClientInfo);

  return NS_NewChannelInternal(
      outChannel, aUri,
      nullptr,  // aLoadingNode
      aLoadingPrincipal, aTriggeringPrincipal, loadingClientInfo, aController,
      aSecurityFlags, aContentPolicyType, aCookieJarSettings,
      aPerformanceStorage, aLoadGroup, aCallbacks, aLoadFlags, aIoService);
}

nsresult NS_NewChannel(nsIChannel** outChannel, nsIURI* aUri,
                       nsINode* aLoadingNode, nsSecurityFlags aSecurityFlags,
                       nsContentPolicyType aContentPolicyType,
                       PerformanceStorage* aPerformanceStorage /* = nullptr */,
                       nsILoadGroup* aLoadGroup /* = nullptr */,
                       nsIInterfaceRequestor* aCallbacks /* = nullptr */,
                       nsLoadFlags aLoadFlags /* = nsIRequest::LOAD_NORMAL */,
                       nsIIOService* aIoService /* = nullptr */,
                       uint32_t aSandboxFlags /* = 0 */) {
  NS_ASSERTION(aLoadingNode, "Can not create channel without a loading Node!");
  return NS_NewChannelInternal(
      outChannel, aUri, aLoadingNode, aLoadingNode->NodePrincipal(),
      nullptr,  // aTriggeringPrincipal
      Maybe<ClientInfo>(), Maybe<ServiceWorkerDescriptor>(), aSecurityFlags,
      aContentPolicyType, aLoadingNode->OwnerDoc()->CookieJarSettings(),
      aPerformanceStorage, aLoadGroup, aCallbacks, aLoadFlags, aIoService,
      aSandboxFlags);
}

nsresult NS_GetIsDocumentChannel(nsIChannel* aChannel, bool* aIsDocument) {
  // Check if this channel is going to be used to create a document. If it has
  // LOAD_DOCUMENT_URI set it is trivially creating a document. If
  // LOAD_HTML_OBJECT_DATA is set it may or may not be used to create a
  // document, depending on its MIME type.

  if (!aChannel || !aIsDocument) {
    return NS_ERROR_NULL_POINTER;
  }
  *aIsDocument = false;
  nsLoadFlags loadFlags;
  nsresult rv = aChannel->GetLoadFlags(&loadFlags);
  if (NS_FAILED(rv)) {
    return rv;
  }
  if (loadFlags & nsIChannel::LOAD_DOCUMENT_URI) {
    *aIsDocument = true;
    return NS_OK;
  }
  if (!(loadFlags & nsIRequest::LOAD_HTML_OBJECT_DATA)) {
    *aIsDocument = false;
    return NS_OK;
  }
  nsAutoCString mimeType;
  rv = aChannel->GetContentType(mimeType);
  if (NS_FAILED(rv)) {
    return rv;
  }
  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  if (nsContentUtils::HtmlObjectContentTypeForMIMEType(
          mimeType, loadInfo->GetSandboxFlags()) ==
      nsIObjectLoadingContent::TYPE_DOCUMENT) {
    *aIsDocument = true;
    return NS_OK;
  }
  *aIsDocument = false;
  return NS_OK;
}

nsresult NS_MakeAbsoluteURI(nsACString& result, const nsACString& spec,
                            nsIURI* baseURI) {
  nsresult rv;
  if (!baseURI) {
    NS_WARNING("It doesn't make sense to not supply a base URI");
    result = spec;
    rv = NS_OK;
  } else if (spec.IsEmpty()) {
    rv = baseURI->GetSpec(result);
  } else {
    rv = baseURI->Resolve(spec, result);
  }
  return rv;
}

nsresult NS_MakeAbsoluteURI(char** result, const char* spec, nsIURI* baseURI) {
  nsresult rv;
  nsAutoCString resultBuf;
  rv = NS_MakeAbsoluteURI(resultBuf, nsDependentCString(spec), baseURI);
  if (NS_SUCCEEDED(rv)) {
    *result = ToNewCString(resultBuf, mozilla::fallible);
    if (!*result) rv = NS_ERROR_OUT_OF_MEMORY;
  }
  return rv;
}

nsresult NS_MakeAbsoluteURI(nsAString& result, const nsAString& spec,
                            nsIURI* baseURI) {
  nsresult rv;
  if (!baseURI) {
    NS_WARNING("It doesn't make sense to not supply a base URI");
    result = spec;
    rv = NS_OK;
  } else {
    nsAutoCString resultBuf;
    if (spec.IsEmpty()) {
      rv = baseURI->GetSpec(resultBuf);
    } else {
      rv = baseURI->Resolve(NS_ConvertUTF16toUTF8(spec), resultBuf);
    }
    if (NS_SUCCEEDED(rv)) CopyUTF8toUTF16(resultBuf, result);
  }
  return rv;
}

int32_t NS_GetDefaultPort(const char* scheme,
                          nsIIOService* ioService /* = nullptr */) {
  nsresult rv;

  // Getting the default port through the protocol handler previously had a lot
  // of XPCOM overhead involved.  We optimize the protocols that matter for Web
  // pages (HTTP and HTTPS) by hardcoding their default ports here.
  //
  // XXX: This might not be necessary for performance anymore.
  if (strncmp(scheme, "http", 4) == 0) {
    if (scheme[4] == 's' && scheme[5] == '\0') {
      return 443;
    }
    if (scheme[4] == '\0') {
      return 80;
    }
  }

  nsCOMPtr<nsIIOService> grip;
  net_EnsureIOService(&ioService, grip);
  if (!ioService) return -1;

  int32_t port;
  rv = ioService->GetDefaultPort(scheme, &port);
  return NS_SUCCEEDED(rv) ? port : -1;
}

int32_t NS_GetRealPort(nsIURI* aURI) {
  int32_t port;
  nsresult rv = aURI->GetPort(&port);
  if (NS_FAILED(rv)) return -1;

  if (port != -1) return port;  // explicitly specified

  // Otherwise, we have to get the default port from the protocol handler

  // Need the scheme first
  nsAutoCString scheme;
  rv = aURI->GetScheme(scheme);
  if (NS_FAILED(rv)) return -1;

  return NS_GetDefaultPort(scheme.get());
}

nsresult NS_NewInputStreamChannelInternal(
    nsIChannel** outChannel, nsIURI* aUri,
    already_AddRefed<nsIInputStream> aStream, const nsACString& aContentType,
    const nsACString& aContentCharset, nsILoadInfo* aLoadInfo) {
  nsresult rv;
  nsCOMPtr<nsIInputStreamChannel> isc =
      do_CreateInstance(NS_INPUTSTREAMCHANNEL_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = isc->SetURI(aUri);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIInputStream> stream = std::move(aStream);
  rv = isc->SetContentStream(stream);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIChannel> channel = do_QueryInterface(isc, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!aContentType.IsEmpty()) {
    rv = channel->SetContentType(aContentType);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (!aContentCharset.IsEmpty()) {
    rv = channel->SetContentCharset(aContentCharset);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  MOZ_ASSERT(aLoadInfo, "need a loadinfo to create a inputstreamchannel");
  channel->SetLoadInfo(aLoadInfo);

  // If we're sandboxed, make sure to clear any owner the channel
  // might already have.
  if (aLoadInfo && aLoadInfo->GetLoadingSandboxed()) {
    channel->SetOwner(nullptr);
  }

  channel.forget(outChannel);
  return NS_OK;
}

nsresult NS_NewInputStreamChannelInternal(
    nsIChannel** outChannel, nsIURI* aUri,
    already_AddRefed<nsIInputStream> aStream, const nsACString& aContentType,
    const nsACString& aContentCharset, nsINode* aLoadingNode,
    nsIPrincipal* aLoadingPrincipal, nsIPrincipal* aTriggeringPrincipal,
    nsSecurityFlags aSecurityFlags, nsContentPolicyType aContentPolicyType) {
  nsCOMPtr<nsILoadInfo> loadInfo = new mozilla::net::LoadInfo(
      aLoadingPrincipal, aTriggeringPrincipal, aLoadingNode, aSecurityFlags,
      aContentPolicyType);
  if (!loadInfo) {
    return NS_ERROR_UNEXPECTED;
  }

  nsCOMPtr<nsIInputStream> stream = std::move(aStream);

  return NS_NewInputStreamChannelInternal(outChannel, aUri, stream.forget(),
                                          aContentType, aContentCharset,
                                          loadInfo);
}

nsresult NS_NewInputStreamChannel(
    nsIChannel** outChannel, nsIURI* aUri,
    already_AddRefed<nsIInputStream> aStream, nsIPrincipal* aLoadingPrincipal,
    nsSecurityFlags aSecurityFlags, nsContentPolicyType aContentPolicyType,
    const nsACString& aContentType /* = ""_ns */,
    const nsACString& aContentCharset /* = ""_ns */) {
  nsCOMPtr<nsIInputStream> stream = aStream;
  return NS_NewInputStreamChannelInternal(outChannel, aUri, stream.forget(),
                                          aContentType, aContentCharset,
                                          nullptr,  // aLoadingNode
                                          aLoadingPrincipal,
                                          nullptr,  // aTriggeringPrincipal
                                          aSecurityFlags, aContentPolicyType);
}

nsresult NS_NewInputStreamChannelInternal(nsIChannel** outChannel, nsIURI* aUri,
                                          const nsAString& aData,
                                          const nsACString& aContentType,
                                          nsILoadInfo* aLoadInfo,
                                          bool aIsSrcdocChannel /* = false */) {
  nsresult rv;
  nsCOMPtr<nsIStringInputStream> stream;
  stream = do_CreateInstance(NS_STRINGINPUTSTREAM_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  uint32_t len;
  char* utf8Bytes = ToNewUTF8String(aData, &len);
  rv = stream->AdoptData(utf8Bytes, len);

  nsCOMPtr<nsIChannel> channel;
  rv = NS_NewInputStreamChannelInternal(getter_AddRefs(channel), aUri,
                                        stream.forget(), aContentType,
                                        "UTF-8"_ns, aLoadInfo);

  NS_ENSURE_SUCCESS(rv, rv);

  if (aIsSrcdocChannel) {
    nsCOMPtr<nsIInputStreamChannel> inStrmChan = do_QueryInterface(channel);
    NS_ENSURE_TRUE(inStrmChan, NS_ERROR_FAILURE);
    inStrmChan->SetSrcdocData(aData);
  }
  channel.forget(outChannel);
  return NS_OK;
}

nsresult NS_NewInputStreamChannelInternal(
    nsIChannel** outChannel, nsIURI* aUri, const nsAString& aData,
    const nsACString& aContentType, nsINode* aLoadingNode,
    nsIPrincipal* aLoadingPrincipal, nsIPrincipal* aTriggeringPrincipal,
    nsSecurityFlags aSecurityFlags, nsContentPolicyType aContentPolicyType,
    bool aIsSrcdocChannel /* = false */) {
  nsCOMPtr<nsILoadInfo> loadInfo = new mozilla::net::LoadInfo(
      aLoadingPrincipal, aTriggeringPrincipal, aLoadingNode, aSecurityFlags,
      aContentPolicyType);
  return NS_NewInputStreamChannelInternal(outChannel, aUri, aData, aContentType,
                                          loadInfo, aIsSrcdocChannel);
}

nsresult NS_NewInputStreamChannel(nsIChannel** outChannel, nsIURI* aUri,
                                  const nsAString& aData,
                                  const nsACString& aContentType,
                                  nsIPrincipal* aLoadingPrincipal,
                                  nsSecurityFlags aSecurityFlags,
                                  nsContentPolicyType aContentPolicyType,
                                  bool aIsSrcdocChannel /* = false */) {
  return NS_NewInputStreamChannelInternal(outChannel, aUri, aData, aContentType,
                                          nullptr,  // aLoadingNode
                                          aLoadingPrincipal,
                                          nullptr,  // aTriggeringPrincipal
                                          aSecurityFlags, aContentPolicyType,
                                          aIsSrcdocChannel);
}

nsresult NS_NewInputStreamPump(
    nsIInputStreamPump** aResult, already_AddRefed<nsIInputStream> aStream,
    uint32_t aSegsize /* = 0 */, uint32_t aSegcount /* = 0 */,
    bool aCloseWhenDone /* = false */,
    nsISerialEventTarget* aMainThreadTarget /* = nullptr */) {
  nsCOMPtr<nsIInputStream> stream = std::move(aStream);

  nsresult rv;
  nsCOMPtr<nsIInputStreamPump> pump =
      do_CreateInstance(NS_INPUTSTREAMPUMP_CONTRACTID, &rv);
  if (NS_SUCCEEDED(rv)) {
    rv = pump->Init(stream, aSegsize, aSegcount, aCloseWhenDone,
                    aMainThreadTarget);
    if (NS_SUCCEEDED(rv)) {
      *aResult = nullptr;
      pump.swap(*aResult);
    }
  }
  return rv;
}

nsresult NS_NewLoadGroup(nsILoadGroup** result, nsIRequestObserver* obs) {
  nsresult rv;
  nsCOMPtr<nsILoadGroup> group =
      do_CreateInstance(NS_LOADGROUP_CONTRACTID, &rv);
  if (NS_SUCCEEDED(rv)) {
    rv = group->SetGroupObserver(obs);
    if (NS_SUCCEEDED(rv)) {
      *result = nullptr;
      group.swap(*result);
    }
  }
  return rv;
}

bool NS_IsReasonableHTTPHeaderValue(const nsACString& aValue) {
  return mozilla::net::nsHttp::IsReasonableHeaderValue(aValue);
}

bool NS_IsValidHTTPToken(const nsACString& aToken) {
  return mozilla::net::nsHttp::IsValidToken(aToken);
}

void NS_TrimHTTPWhitespace(const nsACString& aSource, nsACString& aDest) {
  mozilla::net::nsHttp::TrimHTTPWhitespace(aSource, aDest);
}

nsresult NS_NewLoadGroup(nsILoadGroup** aResult, nsIPrincipal* aPrincipal) {
  using mozilla::LoadContext;
  nsresult rv;

  nsCOMPtr<nsILoadGroup> group =
      do_CreateInstance(NS_LOADGROUP_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  RefPtr<LoadContext> loadContext = new LoadContext(aPrincipal);
  rv = group->SetNotificationCallbacks(loadContext);
  NS_ENSURE_SUCCESS(rv, rv);

  group.forget(aResult);
  return rv;
}

bool NS_LoadGroupMatchesPrincipal(nsILoadGroup* aLoadGroup,
                                  nsIPrincipal* aPrincipal) {
  if (!aPrincipal) {
    return false;
  }

  // If this is a null principal then the load group doesn't really matter.
  // The principal will not be allowed to perform any actions that actually
  // use the load group.  Unconditionally treat null principals as a match.
  if (aPrincipal->GetIsNullPrincipal()) {
    return true;
  }

  if (!aLoadGroup) {
    return false;
  }

  nsCOMPtr<nsILoadContext> loadContext;
  NS_QueryNotificationCallbacks(nullptr, aLoadGroup, NS_GET_IID(nsILoadContext),
                                getter_AddRefs(loadContext));
  NS_ENSURE_TRUE(loadContext, false);

  return true;
}

nsresult NS_NewDownloader(nsIStreamListener** result,
                          nsIDownloadObserver* observer,
                          nsIFile* downloadLocation /* = nullptr */) {
  nsresult rv;
  nsCOMPtr<nsIDownloader> downloader =
      do_CreateInstance(NS_DOWNLOADER_CONTRACTID, &rv);
  if (NS_SUCCEEDED(rv)) {
    rv = downloader->Init(observer, downloadLocation);
    if (NS_SUCCEEDED(rv)) {
      downloader.forget(result);
    }
  }
  return rv;
}

nsresult NS_NewIncrementalStreamLoader(
    nsIIncrementalStreamLoader** result,
    nsIIncrementalStreamLoaderObserver* observer) {
  nsresult rv;
  nsCOMPtr<nsIIncrementalStreamLoader> loader =
      do_CreateInstance(NS_INCREMENTALSTREAMLOADER_CONTRACTID, &rv);
  if (NS_SUCCEEDED(rv)) {
    rv = loader->Init(observer);
    if (NS_SUCCEEDED(rv)) {
      *result = nullptr;
      loader.swap(*result);
    }
  }
  return rv;
}

nsresult NS_NewStreamLoader(
    nsIStreamLoader** result, nsIStreamLoaderObserver* observer,
    nsIRequestObserver* requestObserver /* = nullptr */) {
  nsresult rv;
  nsCOMPtr<nsIStreamLoader> loader =
      do_CreateInstance(NS_STREAMLOADER_CONTRACTID, &rv);
  if (NS_SUCCEEDED(rv)) {
    rv = loader->Init(observer, requestObserver);
    if (NS_SUCCEEDED(rv)) {
      *result = nullptr;
      loader.swap(*result);
    }
  }
  return rv;
}

nsresult NS_NewStreamLoaderInternal(
    nsIStreamLoader** outStream, nsIURI* aUri,
    nsIStreamLoaderObserver* aObserver, nsINode* aLoadingNode,
    nsIPrincipal* aLoadingPrincipal, nsSecurityFlags aSecurityFlags,
    nsContentPolicyType aContentPolicyType,
    nsILoadGroup* aLoadGroup /* = nullptr */,
    nsIInterfaceRequestor* aCallbacks /* = nullptr */,
    nsLoadFlags aLoadFlags /* = nsIRequest::LOAD_NORMAL */) {
  nsCOMPtr<nsIChannel> channel;
  nsresult rv = NS_NewChannelInternal(
      getter_AddRefs(channel), aUri, aLoadingNode, aLoadingPrincipal,
      nullptr,  // aTriggeringPrincipal
      Maybe<ClientInfo>(), Maybe<ServiceWorkerDescriptor>(), aSecurityFlags,
      aContentPolicyType,
      nullptr,  // nsICookieJarSettings
      nullptr,  // PerformanceStorage
      aLoadGroup, aCallbacks, aLoadFlags);

  NS_ENSURE_SUCCESS(rv, rv);
  rv = NS_NewStreamLoader(outStream, aObserver);
  NS_ENSURE_SUCCESS(rv, rv);
  return channel->AsyncOpen(*outStream);
}

nsresult NS_NewStreamLoader(
    nsIStreamLoader** outStream, nsIURI* aUri,
    nsIStreamLoaderObserver* aObserver, nsINode* aLoadingNode,
    nsSecurityFlags aSecurityFlags, nsContentPolicyType aContentPolicyType,
    nsILoadGroup* aLoadGroup /* = nullptr */,
    nsIInterfaceRequestor* aCallbacks /* = nullptr */,
    nsLoadFlags aLoadFlags /* = nsIRequest::LOAD_NORMAL */) {
  NS_ASSERTION(aLoadingNode,
               "Can not create stream loader without a loading Node!");
  return NS_NewStreamLoaderInternal(
      outStream, aUri, aObserver, aLoadingNode, aLoadingNode->NodePrincipal(),
      aSecurityFlags, aContentPolicyType, aLoadGroup, aCallbacks, aLoadFlags);
}

nsresult NS_NewStreamLoader(
    nsIStreamLoader** outStream, nsIURI* aUri,
    nsIStreamLoaderObserver* aObserver, nsIPrincipal* aLoadingPrincipal,
    nsSecurityFlags aSecurityFlags, nsContentPolicyType aContentPolicyType,
    nsILoadGroup* aLoadGroup /* = nullptr */,
    nsIInterfaceRequestor* aCallbacks /* = nullptr */,
    nsLoadFlags aLoadFlags /* = nsIRequest::LOAD_NORMAL */) {
  return NS_NewStreamLoaderInternal(outStream, aUri, aObserver,
                                    nullptr,  // aLoadingNode
                                    aLoadingPrincipal, aSecurityFlags,
                                    aContentPolicyType, aLoadGroup, aCallbacks,
                                    aLoadFlags);
}

nsresult NS_NewSyncStreamListener(nsIStreamListener** result,
                                  nsIInputStream** stream) {
  nsCOMPtr<nsISyncStreamListener> listener = new nsSyncStreamListener();
  nsresult rv = listener->GetInputStream(stream);
  if (NS_SUCCEEDED(rv)) {
    listener.forget(result);
  }
  return rv;
}

nsresult NS_ImplementChannelOpen(nsIChannel* channel, nsIInputStream** result) {
  nsCOMPtr<nsIStreamListener> listener;
  nsCOMPtr<nsIInputStream> stream;
  nsresult rv = NS_NewSyncStreamListener(getter_AddRefs(listener),
                                         getter_AddRefs(stream));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = channel->AsyncOpen(listener);
  NS_ENSURE_SUCCESS(rv, rv);

  uint64_t n;
  // block until the initial response is received or an error occurs.
  rv = stream->Available(&n);
  NS_ENSURE_SUCCESS(rv, rv);

  *result = nullptr;
  stream.swap(*result);

  return NS_OK;
}

nsresult NS_NewRequestObserverProxy(nsIRequestObserver** result,
                                    nsIRequestObserver* observer,
                                    nsISupports* context) {
  nsCOMPtr<nsIRequestObserverProxy> proxy = new nsRequestObserverProxy();
  nsresult rv = proxy->Init(observer, context);
  if (NS_SUCCEEDED(rv)) {
    proxy.forget(result);
  }
  return rv;
}

nsresult NS_NewSimpleStreamListener(
    nsIStreamListener** result, nsIOutputStream* sink,
    nsIRequestObserver* observer /* = nullptr */) {
  nsresult rv;
  nsCOMPtr<nsISimpleStreamListener> listener =
      do_CreateInstance(NS_SIMPLESTREAMLISTENER_CONTRACTID, &rv);
  if (NS_SUCCEEDED(rv)) {
    rv = listener->Init(sink, observer);
    if (NS_SUCCEEDED(rv)) {
      listener.forget(result);
    }
  }
  return rv;
}

nsresult NS_CheckPortSafety(int32_t port, const char* scheme,
                            nsIIOService* ioService /* = nullptr */) {
  nsresult rv;
  nsCOMPtr<nsIIOService> grip;
  rv = net_EnsureIOService(&ioService, grip);
  if (ioService) {
    bool allow;
    rv = ioService->AllowPort(port, scheme, &allow);
    if (NS_SUCCEEDED(rv) && !allow) {
      NS_WARNING("port blocked");
      rv = NS_ERROR_PORT_ACCESS_NOT_ALLOWED;
    }
  }
  return rv;
}

nsresult NS_CheckPortSafety(nsIURI* uri) {
  int32_t port;
  nsresult rv = uri->GetPort(&port);
  if (NS_FAILED(rv) || port == -1) {  // port undefined or default-valued
    return NS_OK;
  }
  nsAutoCString scheme;
  uri->GetScheme(scheme);
  return NS_CheckPortSafety(port, scheme.get());
}

nsresult NS_NewProxyInfo(const nsACString& type, const nsACString& host,
                         int32_t port, uint32_t flags, nsIProxyInfo** result) {
  nsresult rv;
  nsCOMPtr<nsIProtocolProxyService> pps;
  pps = mozilla::components::ProtocolProxy::Service(&rv);
  if (NS_SUCCEEDED(rv)) {
    rv = pps->NewProxyInfo(type, host, port, ""_ns, ""_ns, flags, UINT32_MAX,
                           nullptr, result);
  }
  return rv;
}

nsresult NS_GetFileProtocolHandler(nsIFileProtocolHandler** result,
                                   nsIIOService* ioService /* = nullptr */) {
  nsresult rv;
  nsCOMPtr<nsIIOService> grip;
  rv = net_EnsureIOService(&ioService, grip);
  if (ioService) {
    nsCOMPtr<nsIProtocolHandler> handler;
    rv = ioService->GetProtocolHandler("file", getter_AddRefs(handler));
    if (NS_SUCCEEDED(rv)) rv = CallQueryInterface(handler, result);
  }
  return rv;
}

nsresult NS_GetFileFromURLSpec(const nsACString& inURL, nsIFile** result,
                               nsIIOService* ioService /* = nullptr */) {
  nsresult rv;
  nsCOMPtr<nsIFileProtocolHandler> fileHandler;
  rv = NS_GetFileProtocolHandler(getter_AddRefs(fileHandler), ioService);
  if (NS_SUCCEEDED(rv)) rv = fileHandler->GetFileFromURLSpec(inURL, result);
  return rv;
}

nsresult NS_GetURLSpecFromFile(nsIFile* file, nsACString& url,
                               nsIIOService* ioService /* = nullptr */) {
  nsresult rv;
  nsCOMPtr<nsIFileProtocolHandler> fileHandler;
  rv = NS_GetFileProtocolHandler(getter_AddRefs(fileHandler), ioService);
  if (NS_SUCCEEDED(rv)) rv = fileHandler->GetURLSpecFromFile(file, url);
  return rv;
}

nsresult NS_GetURLSpecFromActualFile(nsIFile* file, nsACString& url,
                                     nsIIOService* ioService /* = nullptr */) {
  nsresult rv;
  nsCOMPtr<nsIFileProtocolHandler> fileHandler;
  rv = NS_GetFileProtocolHandler(getter_AddRefs(fileHandler), ioService);
  if (NS_SUCCEEDED(rv)) rv = fileHandler->GetURLSpecFromActualFile(file, url);
  return rv;
}

nsresult NS_GetURLSpecFromDir(nsIFile* file, nsACString& url,
                              nsIIOService* ioService /* = nullptr */) {
  nsresult rv;
  nsCOMPtr<nsIFileProtocolHandler> fileHandler;
  rv = NS_GetFileProtocolHandler(getter_AddRefs(fileHandler), ioService);
  if (NS_SUCCEEDED(rv)) rv = fileHandler->GetURLSpecFromDir(file, url);
  return rv;
}

void NS_GetReferrerFromChannel(nsIChannel* channel, nsIURI** referrer) {
  *referrer = nullptr;

  if (nsCOMPtr<nsIPropertyBag2> props = do_QueryInterface(channel)) {
    // We have to check for a property on a property bag because the
    // referrer may be empty for security reasons (for example, when loading
    // an http page with an https referrer).
    nsresult rv;
    nsCOMPtr<nsIURI> uri(
        do_GetProperty(props, u"docshell.internalReferrer"_ns, &rv));
    if (NS_SUCCEEDED(rv)) {
      uri.forget(referrer);
      return;
    }
  }

  // if that didn't work, we can still try to get the referrer from the
  // nsIHttpChannel (if we can QI to it)
  nsCOMPtr<nsIHttpChannel> chan(do_QueryInterface(channel));
  if (!chan) {
    return;
  }

  nsCOMPtr<nsIReferrerInfo> referrerInfo = chan->GetReferrerInfo();
  if (!referrerInfo) {
    return;
  }

  referrerInfo->GetOriginalReferrer(referrer);
}

already_AddRefed<nsINetUtil> do_GetNetUtil(nsresult* error /* = 0 */) {
  nsCOMPtr<nsIIOService> io;
  nsCOMPtr<nsINetUtil> util;

  io = mozilla::components::IO::Service();
  if (io) util = do_QueryInterface(io);

  if (error) *error = !!util ? NS_OK : NS_ERROR_FAILURE;
  return util.forget();
}

nsresult NS_ParseRequestContentType(const nsACString& rawContentType,
                                    nsCString& contentType,
                                    nsCString& contentCharset) {
  // contentCharset is left untouched if not present in rawContentType
  nsresult rv;
  nsCOMPtr<nsINetUtil> util = do_GetNetUtil(&rv);
  NS_ENSURE_SUCCESS(rv, rv);
  nsCString charset;
  bool hadCharset;
  rv = util->ParseRequestContentType(rawContentType, charset, &hadCharset,
                                     contentType);
  if (NS_SUCCEEDED(rv) && hadCharset) contentCharset = charset;
  return rv;
}

nsresult NS_ParseResponseContentType(const nsACString& rawContentType,
                                     nsCString& contentType,
                                     nsCString& contentCharset) {
  // contentCharset is left untouched if not present in rawContentType
  nsresult rv;
  nsCOMPtr<nsINetUtil> util = do_GetNetUtil(&rv);
  NS_ENSURE_SUCCESS(rv, rv);
  nsCString charset;
  bool hadCharset;
  rv = util->ParseResponseContentType(rawContentType, charset, &hadCharset,
                                      contentType);
  if (NS_SUCCEEDED(rv) && hadCharset) contentCharset = charset;
  return rv;
}

nsresult NS_ExtractCharsetFromContentType(const nsACString& rawContentType,
                                          nsCString& contentCharset,
                                          bool* hadCharset,
                                          int32_t* charsetStart,
                                          int32_t* charsetEnd) {
  // contentCharset is left untouched if not present in rawContentType
  nsresult rv;
  nsCOMPtr<nsINetUtil> util = do_GetNetUtil(&rv);
  NS_ENSURE_SUCCESS(rv, rv);

  return util->ExtractCharsetFromContentType(
      rawContentType, contentCharset, charsetStart, charsetEnd, hadCharset);
}

nsresult NS_NewAtomicFileOutputStream(nsIOutputStream** result, nsIFile* file,
                                      int32_t ioFlags /* = -1 */,
                                      int32_t perm /* = -1 */,
                                      int32_t behaviorFlags /* = 0 */) {
  nsresult rv;
  nsCOMPtr<nsIFileOutputStream> out =
      do_CreateInstance(NS_ATOMICLOCALFILEOUTPUTSTREAM_CONTRACTID, &rv);
  if (NS_SUCCEEDED(rv)) {
    rv = out->Init(file, ioFlags, perm, behaviorFlags);
    if (NS_SUCCEEDED(rv)) out.forget(result);
  }
  return rv;
}

nsresult NS_NewSafeLocalFileOutputStream(nsIOutputStream** result,
                                         nsIFile* file,
                                         int32_t ioFlags /* = -1 */,
                                         int32_t perm /* = -1 */,
                                         int32_t behaviorFlags /* = 0 */) {
  nsresult rv;
  nsCOMPtr<nsIFileOutputStream> out =
      do_CreateInstance(NS_SAFELOCALFILEOUTPUTSTREAM_CONTRACTID, &rv);
  if (NS_SUCCEEDED(rv)) {
    rv = out->Init(file, ioFlags, perm, behaviorFlags);
    if (NS_SUCCEEDED(rv)) out.forget(result);
  }
  return rv;
}

nsresult NS_NewLocalFileRandomAccessStream(nsIRandomAccessStream** result,
                                           nsIFile* file,
                                           int32_t ioFlags /* = -1 */,
                                           int32_t perm /* = -1 */,
                                           int32_t behaviorFlags /* = 0 */) {
  nsCOMPtr<nsIFileRandomAccessStream> stream = new nsFileRandomAccessStream();
  nsresult rv = stream->Init(file, ioFlags, perm, behaviorFlags);
  if (NS_SUCCEEDED(rv)) {
    stream.forget(result);
  }
  return rv;
}

mozilla::Result<nsCOMPtr<nsIRandomAccessStream>, nsresult>
NS_NewLocalFileRandomAccessStream(nsIFile* file, int32_t ioFlags /* = -1 */,
                                  int32_t perm /* = -1 */,
                                  int32_t behaviorFlags /* = 0 */) {
  nsCOMPtr<nsIRandomAccessStream> stream;
  const nsresult rv = NS_NewLocalFileRandomAccessStream(
      getter_AddRefs(stream), file, ioFlags, perm, behaviorFlags);
  if (NS_SUCCEEDED(rv)) {
    return stream;
  }
  return Err(rv);
}

nsresult NS_NewBufferedOutputStream(
    nsIOutputStream** aResult, already_AddRefed<nsIOutputStream> aOutputStream,
    uint32_t aBufferSize) {
  nsCOMPtr<nsIOutputStream> outputStream = std::move(aOutputStream);

  nsresult rv;
  nsCOMPtr<nsIBufferedOutputStream> out =
      do_CreateInstance(NS_BUFFEREDOUTPUTSTREAM_CONTRACTID, &rv);
  if (NS_SUCCEEDED(rv)) {
    rv = out->Init(outputStream, aBufferSize);
    if (NS_SUCCEEDED(rv)) {
      out.forget(aResult);
    }
  }
  return rv;
}

[[nodiscard]] nsresult NS_NewBufferedInputStream(
    nsIInputStream** aResult, already_AddRefed<nsIInputStream> aInputStream,
    uint32_t aBufferSize) {
  nsCOMPtr<nsIInputStream> inputStream = std::move(aInputStream);

  nsCOMPtr<nsIBufferedInputStream> in;
  nsresult rv = nsBufferedInputStream::Create(
      NS_GET_IID(nsIBufferedInputStream), getter_AddRefs(in));
  if (NS_SUCCEEDED(rv)) {
    rv = in->Init(inputStream, aBufferSize);
    if (NS_SUCCEEDED(rv)) {
      *aResult = static_cast<nsBufferedInputStream*>(in.get())
                     ->GetInputStream()
                     .take();
    }
  }
  return rv;
}

Result<nsCOMPtr<nsIInputStream>, nsresult> NS_NewBufferedInputStream(
    already_AddRefed<nsIInputStream> aInputStream, uint32_t aBufferSize) {
  nsCOMPtr<nsIInputStream> stream;
  const nsresult rv = NS_NewBufferedInputStream(
      getter_AddRefs(stream), std::move(aInputStream), aBufferSize);
  if (NS_SUCCEEDED(rv)) {
    return stream;
  }
  return Err(rv);
}

namespace {

#define BUFFER_SIZE 8192

class BufferWriter final : public nsIInputStreamCallback {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  BufferWriter(nsIInputStream* aInputStream, void* aBuffer, int64_t aCount)
      : mMonitor("BufferWriter.mMonitor"),
        mInputStream(aInputStream),
        mBuffer(aBuffer),
        mCount(aCount),
        mWrittenData(0),
        mBufferType(aBuffer ? eExternal : eInternal),
        mBufferSize(0) {
    MOZ_ASSERT(aInputStream);
    MOZ_ASSERT(aCount == -1 || aCount > 0);
    MOZ_ASSERT_IF(mBuffer, aCount > 0);
  }

  nsresult Write() {
    NS_ASSERT_OWNINGTHREAD(BufferWriter);

    // Let's make the inputStream buffered if it's not.
    if (!NS_InputStreamIsBuffered(mInputStream)) {
      nsCOMPtr<nsIInputStream> bufferedStream;
      nsresult rv = NS_NewBufferedInputStream(
          getter_AddRefs(bufferedStream), mInputStream.forget(), BUFFER_SIZE);
      NS_ENSURE_SUCCESS(rv, rv);

      mInputStream = bufferedStream;
    }

    mAsyncInputStream = do_QueryInterface(mInputStream);

    if (!mAsyncInputStream) {
      return WriteSync();
    }

    // Let's use mAsyncInputStream only.
    mInputStream = nullptr;

    return WriteAsync();
  }

  uint64_t WrittenData() const {
    NS_ASSERT_OWNINGTHREAD(BufferWriter);
    return mWrittenData;
  }

  void* StealBuffer() {
    NS_ASSERT_OWNINGTHREAD(BufferWriter);
    MOZ_ASSERT(mBufferType == eInternal);

    void* buffer = mBuffer;

    mBuffer = nullptr;
    mBufferSize = 0;

    return buffer;
  }

 private:
  ~BufferWriter() {
    if (mBuffer && mBufferType == eInternal) {
      free(mBuffer);
    }

    if (mTaskQueue) {
      mTaskQueue->BeginShutdown();
    }
  }

  nsresult WriteSync() {
    NS_ASSERT_OWNINGTHREAD(BufferWriter);

    uint64_t length = (uint64_t)mCount;

    if (mCount == -1) {
      nsresult rv = mInputStream->Available(&length);
      NS_ENSURE_SUCCESS(rv, rv);

      if (length == 0) {
        // nothing to read.
        return NS_OK;
      }
    }

    if (mBufferType == eInternal) {
      mBuffer = malloc(length);
      if (NS_WARN_IF(!mBuffer)) {
        return NS_ERROR_OUT_OF_MEMORY;
      }
    }

    uint32_t writtenData;
    nsresult rv = mInputStream->ReadSegments(NS_CopySegmentToBuffer, mBuffer,
                                             length, &writtenData);
    NS_ENSURE_SUCCESS(rv, rv);

    mWrittenData = writtenData;
    return NS_OK;
  }

  nsresult WriteAsync() {
    NS_ASSERT_OWNINGTHREAD(BufferWriter);

    if (mCount > 0 && mBufferType == eInternal) {
      mBuffer = malloc(mCount);
      if (NS_WARN_IF(!mBuffer)) {
        return NS_ERROR_OUT_OF_MEMORY;
      }
    }

    while (true) {
      if (mCount == -1 && !MaybeExpandBufferSize()) {
        return NS_ERROR_OUT_OF_MEMORY;
      }

      uint64_t offset = mWrittenData;
      uint64_t length = mCount == -1 ? BUFFER_SIZE : mCount;

      // Let's try to read data directly.
      uint32_t writtenData;
      nsresult rv = mAsyncInputStream->ReadSegments(
          NS_CopySegmentToBuffer, static_cast<char*>(mBuffer) + offset, length,
          &writtenData);

      // Operation completed. Nothing more to read.
      if (NS_SUCCEEDED(rv) && writtenData == 0) {
        return NS_OK;
      }

      // If we succeeded, let's try to read again.
      if (NS_SUCCEEDED(rv)) {
        mWrittenData += writtenData;
        if (mCount != -1) {
          MOZ_ASSERT(mCount >= writtenData);
          mCount -= writtenData;

          // Is this the end of the reading?
          if (mCount == 0) {
            return NS_OK;
          }
        }

        continue;
      }

      // Async wait...
      if (rv == NS_BASE_STREAM_WOULD_BLOCK) {
        rv = MaybeCreateTaskQueue();
        if (NS_WARN_IF(NS_FAILED(rv))) {
          return rv;
        }

        MonitorAutoLock lock(mMonitor);

        rv = mAsyncInputStream->AsyncWait(this, 0, length, mTaskQueue);
        if (NS_WARN_IF(NS_FAILED(rv))) {
          return rv;
        }

        lock.Wait();
        continue;
      }

      // Otherwise, let's propagate the error.
      return rv;
    }

    MOZ_ASSERT_UNREACHABLE("We should not be here");
    return NS_ERROR_FAILURE;
  }

  nsresult MaybeCreateTaskQueue() {
    NS_ASSERT_OWNINGTHREAD(BufferWriter);

    if (!mTaskQueue) {
      nsCOMPtr<nsIEventTarget> target;
      target = mozilla::components::StreamTransport::Service();
      if (!target) {
        return NS_ERROR_FAILURE;
      }

      mTaskQueue = TaskQueue::Create(target.forget(), "nsNetUtil:BufferWriter");
    }

    return NS_OK;
  }

  NS_IMETHOD
  OnInputStreamReady(nsIAsyncInputStream* aStream) override {
    MOZ_ASSERT(!NS_IsMainThread());

    // We have something to read. Let's unlock the main-thread.
    MonitorAutoLock lock(mMonitor);
    lock.Notify();
    return NS_OK;
  }

  bool MaybeExpandBufferSize() {
    NS_ASSERT_OWNINGTHREAD(BufferWriter);

    MOZ_ASSERT(mCount == -1);

    if (mBufferSize >= mWrittenData + BUFFER_SIZE) {
      // The buffer is big enough.
      return true;
    }

    CheckedUint32 bufferSize =
        std::max<uint32_t>(static_cast<uint32_t>(mWrittenData), BUFFER_SIZE);
    while (bufferSize.isValid() &&
           bufferSize.value() < mWrittenData + BUFFER_SIZE) {
      bufferSize *= 2;
    }

    if (!bufferSize.isValid()) {
      return false;
    }

    void* buffer = realloc(mBuffer, bufferSize.value());
    if (!buffer) {
      return false;
    }

    mBuffer = buffer;
    mBufferSize = bufferSize.value();
    return true;
  }

  // All the members of this class are touched on the owning thread only. The
  // monitor is only used to communicate when there is more data to read.
  Monitor mMonitor MOZ_UNANNOTATED;

  nsCOMPtr<nsIInputStream> mInputStream;
  nsCOMPtr<nsIAsyncInputStream> mAsyncInputStream;

  RefPtr<TaskQueue> mTaskQueue;

  void* mBuffer;
  int64_t mCount;
  uint64_t mWrittenData;

  enum {
    // The buffer is allocated internally and this object must release it
    // in the DTOR if not stolen. The buffer can be reallocated.
    eInternal,

    // The buffer is not owned by this object and it cannot be reallocated.
    eExternal,
  } mBufferType;

  // The following set if needed for the async read.
  uint64_t mBufferSize;
};

NS_IMPL_ISUPPORTS(BufferWriter, nsIInputStreamCallback)

}  // anonymous namespace

nsresult NS_ReadInputStreamToBuffer(nsIInputStream* aInputStream, void** aDest,
                                    int64_t aCount, uint64_t* aWritten) {
  MOZ_ASSERT(aInputStream);
  MOZ_ASSERT(aCount >= -1);

  uint64_t dummyWritten;
  if (!aWritten) {
    aWritten = &dummyWritten;
  }

  if (aCount == 0) {
    *aWritten = 0;
    return NS_OK;
  }

  // This will take care of allocating and reallocating aDest.
  RefPtr<BufferWriter> writer = new BufferWriter(aInputStream, *aDest, aCount);

  nsresult rv = writer->Write();
  NS_ENSURE_SUCCESS(rv, rv);

  *aWritten = writer->WrittenData();

  if (!*aDest) {
    *aDest = writer->StealBuffer();
  }

  return NS_OK;
}

nsresult NS_ReadInputStreamToString(nsIInputStream* aInputStream,
                                    nsACString& aDest, int64_t aCount,
                                    uint64_t* aWritten) {
  uint64_t dummyWritten;
  if (!aWritten) {
    aWritten = &dummyWritten;
  }

  // Nothing to do if aCount is 0.
  if (aCount == 0) {
    aDest.Truncate();
    *aWritten = 0;
    return NS_OK;
  }

  // If we have the size, we can pre-allocate the buffer.
  if (aCount > 0) {
    if (NS_WARN_IF(aCount >= INT32_MAX) ||
        NS_WARN_IF(!aDest.SetLength(aCount, mozilla::fallible))) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    void* dest = aDest.BeginWriting();
    nsresult rv =
        NS_ReadInputStreamToBuffer(aInputStream, &dest, aCount, aWritten);
    NS_ENSURE_SUCCESS(rv, rv);

    if ((uint64_t)aCount > *aWritten) {
      aDest.Truncate(*aWritten);
    }

    return NS_OK;
  }

  // If the size is unknown, BufferWriter will allocate the buffer.
  void* dest = nullptr;
  nsresult rv =
      NS_ReadInputStreamToBuffer(aInputStream, &dest, aCount, aWritten);
  MOZ_ASSERT_IF(NS_FAILED(rv), dest == nullptr);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!dest) {
    MOZ_ASSERT(*aWritten == 0);
    aDest.Truncate();
    return NS_OK;
  }

  aDest.Adopt(reinterpret_cast<char*>(dest), *aWritten);
  return NS_OK;
}

nsresult NS_NewURI(nsIURI** result, const nsACString& spec,
                   NotNull<const Encoding*> encoding,
                   nsIURI* baseURI /* = nullptr */) {
  nsAutoCString charset;
  encoding->Name(charset);
  return NS_NewURI(result, spec, charset.get(), baseURI);
}

nsresult NS_NewURI(nsIURI** result, const nsAString& aSpec,
                   const char* charset /* = nullptr */,
                   nsIURI* baseURI /* = nullptr */) {
  nsAutoCString spec;
  if (!AppendUTF16toUTF8(aSpec, spec, mozilla::fallible)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  return NS_NewURI(result, spec, charset, baseURI);
}

nsresult NS_NewURI(nsIURI** result, const nsAString& aSpec,
                   NotNull<const Encoding*> encoding,
                   nsIURI* baseURI /* = nullptr */) {
  nsAutoCString spec;
  if (!AppendUTF16toUTF8(aSpec, spec, mozilla::fallible)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  return NS_NewURI(result, spec, encoding, baseURI);
}

nsresult NS_NewURI(nsIURI** result, const char* spec,
                   nsIURI* baseURI /* = nullptr */) {
  return NS_NewURI(result, nsDependentCString(spec), nullptr, baseURI);
}

static nsresult NewStandardURI(const nsACString& aSpec, const char* aCharset,
                               nsIURI* aBaseURI, int32_t aDefaultPort,
                               nsIURI** aURI) {
  return NS_MutateURI(new nsStandardURL::Mutator())
      .Apply(&nsIStandardURLMutator::Init, nsIStandardURL::URLTYPE_AUTHORITY,
             aDefaultPort, aSpec, aCharset, aBaseURI, nullptr)
      .Finalize(aURI);
}

nsresult NS_GetSpecWithNSURLEncoding(nsACString& aResult,
                                     const nsACString& aSpec) {
  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_NewURIWithNSURLEncoding(getter_AddRefs(uri), aSpec);
  NS_ENSURE_SUCCESS(rv, rv);
  return uri->GetAsciiSpec(aResult);
}

nsresult NS_NewURIWithNSURLEncoding(nsIURI** aResult, const nsACString& aSpec) {
  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_NewURI(getter_AddRefs(uri), aSpec);
  NS_ENSURE_SUCCESS(rv, rv);

  // Escape the ref portion of the URL. NSURL is more strict about which
  // characters in the URL must be % encoded. For example, an unescaped '#'
  // to indicate the beginning of the ref component is accepted by NSURL, but
  // '#' characters in the ref must be escaped. Also adds encoding for other
  // characters not accepted by NSURL in the ref such as '{', '|', '}', and '^'.
  // The ref returned from GetRef() does not include the leading '#'.
  nsAutoCString ref, escapedRef;
  if (NS_SUCCEEDED(uri->GetRef(ref)) && !ref.IsEmpty()) {
    if (!NS_Escape(ref, escapedRef, url_NSURLRef)) {
      return NS_ERROR_INVALID_ARG;
    }
    rv = NS_MutateURI(uri).SetRef(escapedRef).Finalize(uri);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  uri.forget(aResult);
  return NS_OK;
}

extern MOZ_THREAD_LOCAL(uint32_t) gTlsURLRecursionCount;

template <typename T>
class TlsAutoIncrement {
 public:
  explicit TlsAutoIncrement(T& var) : mVar(var) {
    mValue = mVar.get();
    mVar.set(mValue + 1);
  }
  ~TlsAutoIncrement() {
    typename T::Type value = mVar.get();
    MOZ_ASSERT(value == mValue + 1);
    mVar.set(value - 1);
  }

  typename T::Type value() { return mValue; }

 private:
  typename T::Type mValue;
  T& mVar;
};

nsresult NS_NewURI(nsIURI** aURI, const nsACString& aSpec,
                   const char* aCharset /* = nullptr */,
                   nsIURI* aBaseURI /* = nullptr */) {
  // we don't expect any other processes than: socket, content or parent
  // to be able to create a URL
  MOZ_ASSERT(XRE_IsSocketProcess() || XRE_IsContentProcess() ||
             XRE_IsParentProcess());
  TlsAutoIncrement<decltype(gTlsURLRecursionCount)> inc(gTlsURLRecursionCount);
  if (inc.value() >= MAX_RECURSION_COUNT) {
    return NS_ERROR_MALFORMED_URI;
  }

  nsCOMPtr<nsIIOService> ioService = do_GetIOService();
  if (!ioService) {
    // Individual protocol handlers unfortunately rely on the ioservice, let's
    // return an error here instead of causing unpredictable crashes later.
    return NS_ERROR_NOT_AVAILABLE;
  }

  if (StaticPrefs::network_url_max_length() &&
      aSpec.Length() > StaticPrefs::network_url_max_length()) {
    return NS_ERROR_MALFORMED_URI;
  }

  nsAutoCString scheme;
  nsresult rv = net_ExtractURLScheme(aSpec, scheme);
  if (NS_FAILED(rv)) {
    // then aSpec is relative
    if (!aBaseURI) {
      return NS_ERROR_MALFORMED_URI;
    }

    if (!aSpec.IsEmpty() && aSpec[0] == '#') {
      // Looks like a reference instead of a fully-specified URI.
      // --> initialize |uri| as a clone of |aBaseURI|, with ref appended.
      return NS_GetURIWithNewRef(aBaseURI, aSpec, aURI);
    }

    rv = aBaseURI->GetScheme(scheme);
    if (NS_FAILED(rv)) return rv;
  }

  // If encoding is not UTF-8 and url is not special or url’s scheme is "ws" or
  // "wss" then set encoding to UTF-8.
  if (aCharset && !scheme.IsEmpty() &&
      (scheme.EqualsLiteral("ws") || scheme.EqualsLiteral("wss") ||
       !SchemeIsSpecial(scheme))) {
    aCharset = "UTF-8";
  }

  if (scheme.EqualsLiteral("http") || scheme.EqualsLiteral("ws")) {
    return NewStandardURI(aSpec, aCharset, aBaseURI, NS_HTTP_DEFAULT_PORT,
                          aURI);
  }
  if (scheme.EqualsLiteral("https") || scheme.EqualsLiteral("wss")) {
    return NewStandardURI(aSpec, aCharset, aBaseURI, NS_HTTPS_DEFAULT_PORT,
                          aURI);
  }
  if (scheme.EqualsLiteral("ftp")) {
    return NewStandardURI(aSpec, aCharset, aBaseURI, 21, aURI);
  }

  if (scheme.EqualsLiteral("file")) {
    return NS_MutateURI(new nsStandardURL::Mutator())
        .Apply(&nsIFileURLMutator::MarkFileURL)
        .Apply(&nsIStandardURLMutator::Init,
               nsIStandardURL::URLTYPE_NO_AUTHORITY, -1, aSpec, aCharset,
               aBaseURI, nullptr)
        .Finalize(aURI);
  }

  if (scheme.EqualsLiteral("data")) {
    return nsDataHandler::CreateNewURI(aSpec, aCharset, aBaseURI, aURI);
  }

  if (scheme.EqualsLiteral("moz-safe-about") ||
      scheme.EqualsLiteral("page-icon") || scheme.EqualsLiteral("moz") ||
      scheme.EqualsLiteral("cached-favicon")) {
    return NS_MutateURI(new nsSimpleURI::Mutator())
        .SetSpec(aSpec)
        .Finalize(aURI);
  }

  if (scheme.EqualsLiteral("chrome")) {
    return nsChromeProtocolHandler::CreateNewURI(aSpec, aCharset, aBaseURI,
                                                 aURI);
  }

  if (scheme.EqualsLiteral("javascript")) {
    return nsJSProtocolHandler::CreateNewURI(aSpec, aCharset, aBaseURI, aURI);
  }

  if (scheme.EqualsLiteral("blob")) {
    return BlobURLProtocolHandler::CreateNewURI(aSpec, aCharset, aBaseURI,
                                                aURI);
  }

  if (scheme.EqualsLiteral("view-source")) {
    return nsViewSourceHandler::CreateNewURI(aSpec, aCharset, aBaseURI, aURI);
  }

  if (scheme.EqualsLiteral("resource")) {
    RefPtr<nsResProtocolHandler> handler = nsResProtocolHandler::GetSingleton();
    if (!handler) {
      return NS_ERROR_NOT_AVAILABLE;
    }
    return handler->NewURI(aSpec, aCharset, aBaseURI, aURI);
  }

  if (scheme.EqualsLiteral("moz-src")) {
    RefPtr<MozSrcProtocolHandler> handler =
        MozSrcProtocolHandler::GetSingleton();
    if (!handler) {
      return NS_ERROR_NOT_AVAILABLE;
    }
    return handler->NewURI(aSpec, aCharset, aBaseURI, aURI);
  }

  if (scheme.EqualsLiteral("indexeddb") || scheme.EqualsLiteral("uuid")) {
    return NS_MutateURI(new nsStandardURL::Mutator())
        .Apply(&nsIStandardURLMutator::Init, nsIStandardURL::URLTYPE_AUTHORITY,
               0, aSpec, aCharset, aBaseURI, nullptr)
        .Finalize(aURI);
  }

  if (scheme.EqualsLiteral("moz-extension")) {
    RefPtr<mozilla::net::ExtensionProtocolHandler> handler =
        mozilla::net::ExtensionProtocolHandler::GetSingleton();
    if (!handler) {
      return NS_ERROR_NOT_AVAILABLE;
    }
    return handler->NewURI(aSpec, aCharset, aBaseURI, aURI);
  }

  if (scheme.EqualsLiteral("moz-page-thumb")) {
    // The moz-page-thumb service runs JS to resolve a URI to a
    // storage location, so this should only ever run on the main
    // thread.
    if (!NS_IsMainThread()) {
      return NS_ERROR_NOT_AVAILABLE;
    }

    RefPtr<mozilla::net::PageThumbProtocolHandler> handler =
        mozilla::net::PageThumbProtocolHandler::GetSingleton();
    if (!handler) {
      return NS_ERROR_NOT_AVAILABLE;
    }
    return handler->NewURI(aSpec, aCharset, aBaseURI, aURI);
  }

  if (scheme.EqualsLiteral("about")) {
    return nsAboutProtocolHandler::CreateNewURI(aSpec, aCharset, aBaseURI,
                                                aURI);
  }

  if (scheme.EqualsLiteral("jar")) {
    return NS_MutateURI(new nsJARURI::Mutator())
        .Apply(&nsIJARURIMutator::SetSpecBaseCharset, aSpec, aBaseURI, aCharset)
        .Finalize(aURI);
  }

#ifndef XP_IOS
  if (scheme.EqualsLiteral("moz-icon")) {
    return NS_MutateURI(new nsMozIconURI::Mutator())
        .SetSpec(aSpec)
        .Finalize(aURI);
  }
#endif

#ifdef MOZ_WIDGET_GTK
  if (scheme.EqualsLiteral("smb") || scheme.EqualsLiteral("sftp")) {
    return NS_MutateURI(new nsStandardURL::Mutator())
        .Apply(&nsIStandardURLMutator::Init, nsIStandardURL::URLTYPE_STANDARD,
               -1, aSpec, aCharset, aBaseURI, nullptr)
        .Finalize(aURI);
  }
#endif

  if (scheme.EqualsLiteral("android")) {
    return NS_MutateURI(NS_STANDARDURLMUTATOR_CONTRACTID)
        .Apply(&nsIStandardURLMutator::Init, nsIStandardURL::URLTYPE_STANDARD,
               -1, aSpec, aCharset, aBaseURI, nullptr)
        .Finalize(aURI);
  }

  // web-extensions can add custom protocol implementations with standard URLs
  // that have notion of hostname, authority and relative URLs. Below we
  // manually check agains set of known protocols schemes until more general
  // solution is in place (See Bug 1569733)
  if (!StaticPrefs::network_url_useDefaultURI()) {
    if (scheme.EqualsLiteral("ssh")) {
      return NewStandardURI(aSpec, aCharset, aBaseURI, 22, aURI);
    }

    if (scheme.EqualsLiteral("dweb") || scheme.EqualsLiteral("dat") ||
        scheme.EqualsLiteral("ipfs") || scheme.EqualsLiteral("ipns") ||
        scheme.EqualsLiteral("ssb") || scheme.EqualsLiteral("wtp")) {
      return NewStandardURI(aSpec, aCharset, aBaseURI, -1, aURI);
    }
  }

#if defined(MOZ_THUNDERBIRD) || defined(MOZ_SUITE)
  rv = NS_NewMailnewsURI(aURI, aSpec, aCharset, aBaseURI);
  if (rv != NS_ERROR_UNKNOWN_PROTOCOL) {
    return rv;
  }
#endif

  auto mustUseSimpleURI = [](const nsCString& scheme) -> bool {
    if (!StaticPrefs::network_url_simple_uri_unknown_schemes_enabled()) {
      return false;
    }

    bool res = false;
    RefPtr<nsIIOService> ios = do_GetIOService();
    MOZ_ALWAYS_SUCCEEDS(ios->IsSimpleURIUnknownScheme(scheme, &res));
    return res;
  };

  if (aBaseURI) {
    nsAutoCString newSpec;
    rv = aBaseURI->Resolve(aSpec, newSpec);
    NS_ENSURE_SUCCESS(rv, rv);

    nsAutoCString newScheme;
    rv = net_ExtractURLScheme(newSpec, newScheme);
    if (NS_SUCCEEDED(rv)) {
      // The scheme shouldn't really change at this point.
      MOZ_DIAGNOSTIC_ASSERT(newScheme == scheme);
    }

    if (StaticPrefs::network_url_useDefaultURI()) {
      if (mustUseSimpleURI(scheme)) {
        return NS_MutateURI(new nsSimpleURI::Mutator())
            .SetSpec(newSpec)
            .Finalize(aURI);
      }

      return NS_MutateURI(new DefaultURI::Mutator())
          .SetSpec(newSpec)
          .Finalize(aURI);
    }

    return NS_MutateURI(new nsSimpleURI::Mutator())
        .SetSpec(newSpec)
        .Finalize(aURI);
  }

  if (StaticPrefs::network_url_useDefaultURI()) {
    if (mustUseSimpleURI(scheme)) {
      return NS_MutateURI(new nsSimpleURI::Mutator())
          .SetSpec(aSpec)
          .Finalize(aURI);
    }
    return NS_MutateURI(new DefaultURI::Mutator())
        .SetSpec(aSpec)
        .Finalize(aURI);
  }

  // Falls back to external protocol handler.
  return NS_MutateURI(new nsSimpleURI::Mutator()).SetSpec(aSpec).Finalize(aURI);
}

nsresult NS_GetSanitizedURIStringFromURI(nsIURI* aUri,
                                         nsACString& aSanitizedSpec) {
  aSanitizedSpec.Truncate();

  nsCOMPtr<nsISensitiveInfoHiddenURI> safeUri = do_QueryInterface(aUri);
  nsAutoCString cSpec;
  nsresult rv;
  if (safeUri) {
    rv = safeUri->GetSensitiveInfoHiddenSpec(cSpec);
  } else {
    rv = aUri->GetSpec(cSpec);
  }

  if (NS_SUCCEEDED(rv)) {
    aSanitizedSpec.Assign(cSpec);
  }
  return rv;
}

nsresult NS_LoadPersistentPropertiesFromURISpec(
    nsIPersistentProperties** outResult, const nsACString& aSpec) {
  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_NewURI(getter_AddRefs(uri), aSpec);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIChannel> channel;
  rv = NS_NewChannel(getter_AddRefs(channel), uri,
                     nsContentUtils::GetSystemPrincipal(),
                     nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
                     nsIContentPolicy::TYPE_OTHER);
  NS_ENSURE_SUCCESS(rv, rv);
  nsCOMPtr<nsIInputStream> in;
  rv = channel->Open(getter_AddRefs(in));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIPersistentProperties> properties = new nsPersistentProperties();
  rv = properties->Load(in);
  NS_ENSURE_SUCCESS(rv, rv);

  properties.swap(*outResult);
  return NS_OK;
}

bool NS_UsePrivateBrowsing(nsIChannel* channel) {
  OriginAttributes attrs;
  bool result = StoragePrincipalHelper::GetOriginAttributes(
      channel, attrs, StoragePrincipalHelper::eRegularPrincipal);
  NS_ENSURE_TRUE(result, result);
  return attrs.IsPrivateBrowsing();
}

bool NS_HasBeenCrossOrigin(nsIChannel* aChannel, bool aReport) {
  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  // TYPE_DOCUMENT loads have a null LoadingPrincipal and can not be cross
  // origin.
  if (!loadInfo->GetLoadingPrincipal()) {
    return false;
  }

  // Always treat tainted channels as cross-origin.
  if (loadInfo->GetTainting() != LoadTainting::Basic) {
    return true;
  }

  nsCOMPtr<nsIPrincipal> loadingPrincipal = loadInfo->GetLoadingPrincipal();
  uint32_t mode = loadInfo->GetSecurityMode();
  bool dataInherits =
      mode == nsILoadInfo::SEC_REQUIRE_SAME_ORIGIN_INHERITS_SEC_CONTEXT ||
      mode == nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_INHERITS_SEC_CONTEXT ||
      mode == nsILoadInfo::SEC_REQUIRE_CORS_INHERITS_SEC_CONTEXT;

  bool aboutBlankInherits = dataInherits && loadInfo->GetAboutBlankInherits();

  uint64_t innerWindowID = loadInfo->GetInnerWindowID();

  for (nsIRedirectHistoryEntry* redirectHistoryEntry :
       loadInfo->RedirectChain()) {
    nsCOMPtr<nsIPrincipal> principal;
    redirectHistoryEntry->GetPrincipal(getter_AddRefs(principal));
    if (!principal) {
      return true;
    }

    nsCOMPtr<nsIURI> uri;
    auto* basePrin = BasePrincipal::Cast(principal);
    basePrin->GetURI(getter_AddRefs(uri));
    if (!uri) {
      return true;
    }

    if (aboutBlankInherits && NS_IsAboutBlank(uri)) {
      continue;
    }

    nsresult res;
    if (aReport) {
      res = loadingPrincipal->CheckMayLoadWithReporting(uri, dataInherits,
                                                        innerWindowID);
    } else {
      res = loadingPrincipal->CheckMayLoad(uri, dataInherits);
    }
    if (NS_FAILED(res)) {
      return true;
    }
  }

  nsCOMPtr<nsIURI> uri;
  NS_GetFinalChannelURI(aChannel, getter_AddRefs(uri));
  if (!uri) {
    return true;
  }

  if (aboutBlankInherits && NS_IsAboutBlank(uri)) {
    return false;
  }

  nsresult res;
  if (aReport) {
    res = loadingPrincipal->CheckMayLoadWithReporting(uri, dataInherits,
                                                      innerWindowID);
  } else {
    res = loadingPrincipal->CheckMayLoad(uri, dataInherits);
  }

  return NS_FAILED(res);
}

bool NS_IsSafeMethodNav(nsIChannel* aChannel) {
  RefPtr<HttpBaseChannel> baseChan = do_QueryObject(aChannel);
  if (!baseChan) {
    return false;
  }
  nsHttpRequestHead* requestHead = baseChan->GetRequestHead();
  if (!requestHead) {
    return false;
  }
  return requestHead->IsSafeMethod();
}

void NS_WrapAuthPrompt(nsIAuthPrompt* aAuthPrompt,
                       nsIAuthPrompt2** aAuthPrompt2) {
  nsCOMPtr<nsIAuthPromptAdapterFactory> factory;
  factory = mozilla::components::AuthPromptAdapter::Service();
  if (!factory) return;

  NS_WARNING("Using deprecated nsIAuthPrompt");
  factory->CreateAdapter(aAuthPrompt, aAuthPrompt2);
}

void NS_QueryAuthPrompt2(nsIInterfaceRequestor* aCallbacks,
                         nsIAuthPrompt2** aAuthPrompt) {
  CallGetInterface(aCallbacks, aAuthPrompt);
  if (*aAuthPrompt) return;

  // Maybe only nsIAuthPrompt is provided and we have to wrap it.
  nsCOMPtr<nsIAuthPrompt> prompt(do_GetInterface(aCallbacks));
  if (!prompt) return;

  NS_WrapAuthPrompt(prompt, aAuthPrompt);
}

void NS_QueryAuthPrompt2(nsIChannel* aChannel, nsIAuthPrompt2** aAuthPrompt) {
  *aAuthPrompt = nullptr;

  // We want to use any auth prompt we can find on the channel's callbacks,
  // and if that fails use the loadgroup's prompt (if any)
  // Therefore, we can't just use NS_QueryNotificationCallbacks, because
  // that would prefer a loadgroup's nsIAuthPrompt2 over a channel's
  // nsIAuthPrompt.
  nsCOMPtr<nsIInterfaceRequestor> callbacks;
  aChannel->GetNotificationCallbacks(getter_AddRefs(callbacks));
  if (callbacks) {
    NS_QueryAuthPrompt2(callbacks, aAuthPrompt);
    if (*aAuthPrompt) return;
  }

  nsCOMPtr<nsILoadGroup> group;
  aChannel->GetLoadGroup(getter_AddRefs(group));
  if (!group) return;

  group->GetNotificationCallbacks(getter_AddRefs(callbacks));
  if (!callbacks) return;
  NS_QueryAuthPrompt2(callbacks, aAuthPrompt);
}

nsresult NS_NewNotificationCallbacksAggregation(
    nsIInterfaceRequestor* callbacks, nsILoadGroup* loadGroup,
    nsIEventTarget* target, nsIInterfaceRequestor** result) {
  nsCOMPtr<nsIInterfaceRequestor> cbs;
  if (loadGroup) loadGroup->GetNotificationCallbacks(getter_AddRefs(cbs));
  return NS_NewInterfaceRequestorAggregation(callbacks, cbs, target, result);
}

nsresult NS_NewNotificationCallbacksAggregation(
    nsIInterfaceRequestor* callbacks, nsILoadGroup* loadGroup,
    nsIInterfaceRequestor** result) {
  return NS_NewNotificationCallbacksAggregation(callbacks, loadGroup, nullptr,
                                                result);
}

nsresult NS_DoImplGetInnermostURI(nsINestedURI* nestedURI, nsIURI** result) {
  MOZ_ASSERT(nestedURI, "Must have a nested URI!");
  MOZ_ASSERT(!*result, "Must have null *result");

  nsCOMPtr<nsIURI> inner;
  nsresult rv = nestedURI->GetInnerURI(getter_AddRefs(inner));
  NS_ENSURE_SUCCESS(rv, rv);

  // We may need to loop here until we reach the innermost
  // URI.
  nsCOMPtr<nsINestedURI> nestedInner(do_QueryInterface(inner));
  while (nestedInner) {
    rv = nestedInner->GetInnerURI(getter_AddRefs(inner));
    NS_ENSURE_SUCCESS(rv, rv);
    nestedInner = do_QueryInterface(inner);
  }

  // Found the innermost one if we reach here.
  inner.swap(*result);

  return rv;
}

nsresult NS_ImplGetInnermostURI(nsINestedURI* nestedURI, nsIURI** result) {
  // Make it safe to use swap()
  *result = nullptr;

  return NS_DoImplGetInnermostURI(nestedURI, result);
}

already_AddRefed<nsIURI> NS_GetInnermostURI(nsIURI* aURI) {
  MOZ_ASSERT(aURI, "Must have URI");

  nsCOMPtr<nsIURI> uri = aURI;

  nsCOMPtr<nsINestedURI> nestedURI(do_QueryInterface(uri));
  if (!nestedURI) {
    return uri.forget();
  }

  nsresult rv = nestedURI->GetInnermostURI(getter_AddRefs(uri));
  if (NS_FAILED(rv)) {
    return nullptr;
  }

  return uri.forget();
}

nsresult NS_GetFinalChannelURI(nsIChannel* channel, nsIURI** uri) {
  *uri = nullptr;

  nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();
  nsCOMPtr<nsIURI> resultPrincipalURI;
  loadInfo->GetResultPrincipalURI(getter_AddRefs(resultPrincipalURI));
  if (resultPrincipalURI) {
    resultPrincipalURI.forget(uri);
    return NS_OK;
  }
  return channel->GetOriginalURI(uri);
}

nsresult NS_URIChainHasFlags(nsIURI* uri, uint32_t flags, bool* result) {
  nsresult rv;
  nsCOMPtr<nsINetUtil> util = do_GetNetUtil(&rv);
  NS_ENSURE_SUCCESS(rv, rv);

  return util->URIChainHasFlags(uri, flags, result);
}

uint32_t NS_SecurityHashURI(nsIURI* aURI) {
  nsCOMPtr<nsIURI> baseURI = NS_GetInnermostURI(aURI);

  nsAutoCString scheme;
  uint32_t schemeHash = 0;
  if (NS_SUCCEEDED(baseURI->GetScheme(scheme))) {
    schemeHash = mozilla::HashString(scheme);
  }

  // TODO figure out how to hash file:// URIs
  if (scheme.EqualsLiteral("file")) return schemeHash;  // sad face

#if IS_ORIGIN_IS_FULL_SPEC_DEFINED
  bool hasFlag;
  if (NS_FAILED(NS_URIChainHasFlags(
          baseURI, nsIProtocolHandler::ORIGIN_IS_FULL_SPEC, &hasFlag)) ||
      hasFlag) {
    nsAutoCString spec;
    uint32_t specHash;
    nsresult res = baseURI->GetSpec(spec);
    if (NS_SUCCEEDED(res))
      specHash = mozilla::HashString(spec);
    else
      specHash = static_cast<uint32_t>(res);
    return specHash;
  }
#endif

  nsAutoCString host;
  uint32_t hostHash = 0;
  if (NS_SUCCEEDED(baseURI->GetAsciiHost(host))) {
    hostHash = mozilla::HashString(host);
  }

  return mozilla::AddToHash(schemeHash, hostHash, NS_GetRealPort(baseURI));
}

bool NS_SecurityCompareURIs(nsIURI* aSourceURI, nsIURI* aTargetURI,
                            bool aStrictFileOriginPolicy) {
  nsresult rv;

  // Note that this is not an Equals() test on purpose -- for URIs that don't
  // support host/port, we want equality to basically be object identity, for
  // security purposes.  Otherwise, for example, two javascript: URIs that
  // are otherwise unrelated could end up "same origin", which would be
  // unfortunate.
  if (aSourceURI && aSourceURI == aTargetURI) {
    return true;
  }

  if (!aTargetURI || !aSourceURI) {
    return false;
  }

  // If either URI is a nested URI, get the base URI
  nsCOMPtr<nsIURI> sourceBaseURI = NS_GetInnermostURI(aSourceURI);
  nsCOMPtr<nsIURI> targetBaseURI = NS_GetInnermostURI(aTargetURI);

#if defined(MOZ_THUNDERBIRD) || defined(MOZ_SUITE)
  // Check if either URI has a special origin.
  nsCOMPtr<nsIURI> origin;
  nsCOMPtr<nsIURIWithSpecialOrigin> uriWithSpecialOrigin =
      do_QueryInterface(sourceBaseURI);
  if (uriWithSpecialOrigin) {
    rv = uriWithSpecialOrigin->GetOrigin(getter_AddRefs(origin));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return false;
    }
    MOZ_ASSERT(origin);
    sourceBaseURI = origin;
  }
  uriWithSpecialOrigin = do_QueryInterface(targetBaseURI);
  if (uriWithSpecialOrigin) {
    rv = uriWithSpecialOrigin->GetOrigin(getter_AddRefs(origin));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return false;
    }
    MOZ_ASSERT(origin);
    targetBaseURI = origin;
  }
#endif

  nsCOMPtr<nsIPrincipal> sourceBlobPrincipal;
  if (BlobURLProtocolHandler::GetBlobURLPrincipal(
          sourceBaseURI, getter_AddRefs(sourceBlobPrincipal))) {
    nsCOMPtr<nsIURI> sourceBlobOwnerURI;
    auto* basePrin = BasePrincipal::Cast(sourceBlobPrincipal);
    rv = basePrin->GetURI(getter_AddRefs(sourceBlobOwnerURI));
    if (NS_SUCCEEDED(rv)) {
      sourceBaseURI = sourceBlobOwnerURI;
    }
  }

  nsCOMPtr<nsIPrincipal> targetBlobPrincipal;
  if (BlobURLProtocolHandler::GetBlobURLPrincipal(
          targetBaseURI, getter_AddRefs(targetBlobPrincipal))) {
    nsCOMPtr<nsIURI> targetBlobOwnerURI;
    auto* basePrin = BasePrincipal::Cast(targetBlobPrincipal);
    rv = basePrin->GetURI(getter_AddRefs(targetBlobOwnerURI));
    if (NS_SUCCEEDED(rv)) {
      targetBaseURI = targetBlobOwnerURI;
    }
  }

  if (!sourceBaseURI || !targetBaseURI) return false;

  // Compare schemes
  nsAutoCString targetScheme;
  bool sameScheme = false;
  if (NS_FAILED(targetBaseURI->GetScheme(targetScheme)) ||
      NS_FAILED(sourceBaseURI->SchemeIs(targetScheme.get(), &sameScheme)) ||
      !sameScheme) {
    // Not same-origin if schemes differ
    return false;
  }

  // For file scheme, reject unless the files are identical.
  if (targetScheme.EqualsLiteral("file")) {
    // in traditional unsafe behavior all files are the same origin
    if (!aStrictFileOriginPolicy) return true;

    nsCOMPtr<nsIFileURL> sourceFileURL(do_QueryInterface(sourceBaseURI));
    nsCOMPtr<nsIFileURL> targetFileURL(do_QueryInterface(targetBaseURI));

    if (!sourceFileURL || !targetFileURL) return false;

    nsCOMPtr<nsIFile> sourceFile, targetFile;

    sourceFileURL->GetFile(getter_AddRefs(sourceFile));
    targetFileURL->GetFile(getter_AddRefs(targetFile));

    if (!sourceFile || !targetFile) return false;

    // Otherwise they had better match
    bool filesAreEqual = false;
    rv = sourceFile->Equals(targetFile, &filesAreEqual);
    return NS_SUCCEEDED(rv) && filesAreEqual;
  }

#if IS_ORIGIN_IS_FULL_SPEC_DEFINED
  bool hasFlag;
  if (NS_FAILED(NS_URIChainHasFlags(
          targetBaseURI, nsIProtocolHandler::ORIGIN_IS_FULL_SPEC, &hasFlag)) ||
      hasFlag) {
    // URIs with this flag have the whole spec as a distinct trust
    // domain; use the whole spec for comparison
    nsAutoCString targetSpec;
    nsAutoCString sourceSpec;
    return (NS_SUCCEEDED(targetBaseURI->GetSpec(targetSpec)) &&
            NS_SUCCEEDED(sourceBaseURI->GetSpec(sourceSpec)) &&
            targetSpec.Equals(sourceSpec));
  }
#endif

  // Compare hosts
  nsAutoCString targetHost;
  nsAutoCString sourceHost;
  if (NS_FAILED(targetBaseURI->GetAsciiHost(targetHost)) ||
      NS_FAILED(sourceBaseURI->GetAsciiHost(sourceHost))) {
    return false;
  }

  nsCOMPtr<nsIStandardURL> targetURL(do_QueryInterface(targetBaseURI));
  nsCOMPtr<nsIStandardURL> sourceURL(do_QueryInterface(sourceBaseURI));
  if (!targetURL || !sourceURL) {
    return false;
  }

  if (!targetHost.Equals(sourceHost, nsCaseInsensitiveCStringComparator)) {
    return false;
  }

  return NS_GetRealPort(targetBaseURI) == NS_GetRealPort(sourceBaseURI);
}

bool NS_URIIsLocalFile(nsIURI* aURI) {
  nsCOMPtr<nsINetUtil> util = do_GetNetUtil();

  bool isFile;
  return util &&
         NS_SUCCEEDED(util->ProtocolHasFlags(
             aURI, nsIProtocolHandler::URI_IS_LOCAL_FILE, &isFile)) &&
         isFile;
}

bool NS_IsInternalSameURIRedirect(nsIChannel* aOldChannel,
                                  nsIChannel* aNewChannel, uint32_t aFlags) {
  if (!(aFlags & nsIChannelEventSink::REDIRECT_INTERNAL)) {
    return false;
  }

  nsCOMPtr<nsIURI> oldURI, newURI;
  aOldChannel->GetURI(getter_AddRefs(oldURI));
  aNewChannel->GetURI(getter_AddRefs(newURI));

  if (!oldURI || !newURI) {
    return false;
  }

  bool res;
  return NS_SUCCEEDED(oldURI->Equals(newURI, &res)) && res;
}

bool NS_IsHSTSUpgradeRedirect(nsIChannel* aOldChannel, nsIChannel* aNewChannel,
                              uint32_t aFlags) {
  if (!(aFlags & nsIChannelEventSink::REDIRECT_STS_UPGRADE)) {
    return false;
  }

  nsCOMPtr<nsIURI> oldURI, newURI;
  aOldChannel->GetURI(getter_AddRefs(oldURI));
  aNewChannel->GetURI(getter_AddRefs(newURI));

  if (!oldURI || !newURI) {
    return false;
  }

  if (!oldURI->SchemeIs("http")) {
    return false;
  }

  nsCOMPtr<nsIURI> upgradedURI;
  nsresult rv = NS_GetSecureUpgradedURI(oldURI, getter_AddRefs(upgradedURI));
  if (NS_FAILED(rv)) {
    return false;
  }

  bool res;
  return NS_SUCCEEDED(upgradedURI->Equals(newURI, &res)) && res;
}

bool NS_ShouldRemoveAuthHeaderOnRedirect(nsIChannel* aOldChannel,
                                         nsIChannel* aNewChannel,
                                         uint32_t aFlags) {
  // we need to strip Authentication headers for external cross-origin redirects
  // Howerver, we should NOT strip auth headers for
  // - internal redirects/HSTS upgrades
  // - same origin redirects
  // Ref: https://fetch.spec.whatwg.org/#http-redirect-fetch
  if ((aFlags & (nsIChannelEventSink::REDIRECT_STS_UPGRADE |
                 nsIChannelEventSink::REDIRECT_INTERNAL))) {
    // this is an internal redirect do not strip auth header
    return false;
  }
  nsCOMPtr<nsIURI> oldUri;
  MOZ_ALWAYS_SUCCEEDS(
      NS_GetFinalChannelURI(aOldChannel, getter_AddRefs(oldUri)));

  nsCOMPtr<nsIURI> newUri;
  MOZ_ALWAYS_SUCCEEDS(
      NS_GetFinalChannelURI(aNewChannel, getter_AddRefs(newUri)));

  nsresult rv = nsContentUtils::GetSecurityManager()->CheckSameOriginURI(
      newUri, oldUri, false, false);

  return NS_FAILED(rv);
}

nsresult NS_LinkRedirectChannels(uint64_t channelId,
                                 nsIParentChannel* parentChannel,
                                 nsIChannel** _result) {
  nsCOMPtr<nsIRedirectChannelRegistrar> registrar =
      RedirectChannelRegistrar::GetOrCreate();
  MOZ_ASSERT(registrar);

  return registrar->LinkChannels(channelId, parentChannel, _result);
}

nsILoadInfo::CrossOriginEmbedderPolicy
NS_GetCrossOriginEmbedderPolicyFromHeader(
    const nsACString& aHeader, bool aIsOriginTrialCoepCredentiallessEnabled) {
  nsCOMPtr<nsISFVService> sfv = GetSFVService();

  nsCOMPtr<nsISFVItem> item;
  nsresult rv = sfv->ParseItem(aHeader, getter_AddRefs(item));
  if (NS_FAILED(rv)) {
    return nsILoadInfo::EMBEDDER_POLICY_NULL;
  }

  nsCOMPtr<nsISFVBareItem> value;
  rv = item->GetValue(getter_AddRefs(value));
  if (NS_FAILED(rv)) {
    return nsILoadInfo::EMBEDDER_POLICY_NULL;
  }

  nsCOMPtr<nsISFVToken> token = do_QueryInterface(value);
  if (!token) {
    return nsILoadInfo::EMBEDDER_POLICY_NULL;
  }

  nsAutoCString embedderPolicy;
  rv = token->GetValue(embedderPolicy);
  if (NS_FAILED(rv)) {
    return nsILoadInfo::EMBEDDER_POLICY_NULL;
  }

  if (embedderPolicy.EqualsLiteral("require-corp")) {
    return nsILoadInfo::EMBEDDER_POLICY_REQUIRE_CORP;
  } else if (embedderPolicy.EqualsLiteral("credentialless") &&
             IsCoepCredentiallessEnabled(
                 aIsOriginTrialCoepCredentiallessEnabled)) {
    return nsILoadInfo::EMBEDDER_POLICY_CREDENTIALLESS;
  }

  return nsILoadInfo::EMBEDDER_POLICY_NULL;
}

bool NS_GetForceLoadAtTopFromHeader(const nsACString& aHeader) {
  nsCOMPtr<nsISFVService> sfv = mozilla::net::GetSFVService();

  nsCOMPtr<nsISFVDictionary> dict;
  if (NS_FAILED(sfv->ParseDictionary(aHeader, getter_AddRefs(dict)))) {
    return false;
  }
  nsCOMPtr<nsISFVItemOrInnerList> iil;
  if (NS_FAILED(dict->Get("force-load-at-top"_ns, getter_AddRefs(iil)))) {
    return false;
  }

  nsCOMPtr<nsISFVItem> item(do_QueryInterface(iil));
  if (!item) {
    return false;
  }

  nsCOMPtr<nsISFVBareItem> bareItem;
  if (NS_FAILED(item->GetValue(getter_AddRefs(bareItem)))) {
    return false;
  }

  int32_t type;
  if (NS_FAILED(bareItem->GetType(&type))) {
    return false;
  }

  nsCOMPtr<nsISFVBool> boolItem(do_QueryInterface(bareItem));
  if (!boolItem) {
    return false;
  }

  bool b;
  if (NS_FAILED(boolItem->GetValue(&b))) {
    return false;
  }

  return b;
}

/** Given the first (disposition) token from a Content-Disposition header,
 * tell whether it indicates the content is inline or attachment
 * @param aDispToken the disposition token from the content-disposition header
 */
uint32_t NS_GetContentDispositionFromToken(const nsAString& aDispToken) {
  // RFC 2183, section 2.8 says that an unknown disposition
  // value should be treated as "attachment"
  // If all of these tests eval to false, then we have a content-disposition of
  // "attachment" or unknown
  if (aDispToken.IsEmpty() || aDispToken.LowerCaseEqualsLiteral("inline") ||
      // Broken sites just send
      // Content-Disposition: filename="file"
      // without a disposition token... screen those out.
      StringHead(aDispToken, 8).LowerCaseEqualsLiteral("filename")) {
    return nsIChannel::DISPOSITION_INLINE;
  }

  return nsIChannel::DISPOSITION_ATTACHMENT;
}

uint32_t NS_GetContentDispositionFromHeader(const nsACString& aHeader,
                                            nsIChannel* aChan /* = nullptr */) {
  nsresult rv;
  nsCOMPtr<nsIMIMEHeaderParam> mimehdrpar;
  mimehdrpar = mozilla::components::MimeHeaderParam::Service(&rv);
  if (NS_FAILED(rv)) return nsIChannel::DISPOSITION_ATTACHMENT;

  nsAutoString dispToken;
  rv = mimehdrpar->GetParameterHTTP(aHeader, "", ""_ns, true, nullptr,
                                    dispToken);

  if (NS_FAILED(rv)) {
    // special case (see bug 272541): empty disposition type handled as "inline"
    if (rv == NS_ERROR_FIRST_HEADER_FIELD_COMPONENT_EMPTY) {
      return nsIChannel::DISPOSITION_INLINE;
    }
    return nsIChannel::DISPOSITION_ATTACHMENT;
  }

  return NS_GetContentDispositionFromToken(dispToken);
}

nsresult NS_GetFilenameFromDisposition(nsAString& aFilename,
                                       const nsACString& aDisposition) {
  aFilename.Truncate();

  nsresult rv;
  nsCOMPtr<nsIMIMEHeaderParam> mimehdrpar;
  mimehdrpar = mozilla::components::MimeHeaderParam::Service(&rv);
  if (NS_FAILED(rv)) return rv;

  // Get the value of 'filename' parameter
  rv = mimehdrpar->GetParameterHTTP(aDisposition, "filename", ""_ns, true,
                                    nullptr, aFilename);

  if (NS_FAILED(rv)) {
    aFilename.Truncate();
    return rv;
  }

  if (aFilename.IsEmpty()) return NS_ERROR_NOT_AVAILABLE;

  // Filename may still be percent-encoded. Fix:
  if (aFilename.FindChar('%') != -1) {
    nsCOMPtr<nsITextToSubURI> textToSubURI;
    textToSubURI = mozilla::components::TextToSubURI::Service(&rv);
    if (NS_SUCCEEDED(rv)) {
      nsAutoString unescaped;
      textToSubURI->UnEscapeURIForUI(NS_ConvertUTF16toUTF8(aFilename),
                                     /* dontEscape = */ true, unescaped);
      aFilename.Assign(unescaped);
    }
  }

  return NS_OK;
}

void net_EnsurePSMInit() {
  if (XRE_IsSocketProcess()) {
    EnsureNSSInitializedChromeOrContent();
    return;
  }

  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());

  DebugOnly<bool> rv = EnsureNSSInitializedChromeOrContent();
  MOZ_ASSERT(rv);
}

bool NS_IsAboutBlank(nsIURI* uri) {
  // GetSpec can be expensive for some URIs, so check the scheme first.
  if (!uri->SchemeIs("about")) {
    return false;
  }

  nsAutoCString spec;
  if (NS_FAILED(uri->GetSpec(spec))) {
    return false;
  }

  return spec.EqualsLiteral("about:blank");
}

bool NS_IsAboutBlankAllowQueryAndFragment(nsIURI* uri) {
  // GetSpec can be expensive for some URIs, so check the scheme first.
  if (!uri->SchemeIs("about")) {
    return false;
  }

  nsAutoCString name;
  if (NS_FAILED(NS_GetAboutModuleName(uri, name))) {
    return false;
  }

  return name.EqualsLiteral("blank");
}

bool NS_IsAboutSrcdoc(nsIURI* uri) {
  // GetSpec can be expensive for some URIs, so check the scheme first.
  if (!uri->SchemeIs("about")) {
    return false;
  }

  nsAutoCString spec;
  if (NS_FAILED(uri->GetSpec(spec))) {
    return false;
  }

  return spec.EqualsLiteral("about:srcdoc");
}

nsresult NS_GenerateHostPort(const nsCString& host, int32_t port,
                             nsACString& hostLine) {
  if (strchr(host.get(), ':')) {
    // host is an IPv6 address literal and must be encapsulated in []'s
    hostLine.Assign('[');
    // scope id is not needed for Host header.
    int scopeIdPos = host.FindChar('%');
    if (scopeIdPos == -1) {
      hostLine.Append(host);
    } else if (scopeIdPos > 0) {
      hostLine.Append(Substring(host, 0, scopeIdPos));
    } else {
      return NS_ERROR_MALFORMED_URI;
    }
    hostLine.Append(']');
  } else {
    hostLine.Assign(host);
  }
  if (port != -1) {
    hostLine.Append(':');
    hostLine.AppendInt(port);
  }
  return NS_OK;
}

void NS_SniffContent(const char* aSnifferType, nsIRequest* aRequest,
                     const uint8_t* aData, uint32_t aLength,
                     nsACString& aSniffedType) {
  using ContentSnifferCache = nsCategoryCache<nsIContentSniffer>;
  extern ContentSnifferCache* gNetSniffers;
  extern ContentSnifferCache* gDataSniffers;
  extern ContentSnifferCache* gORBSniffers;
  extern ContentSnifferCache* gNetAndORBSniffers;
  ContentSnifferCache* cache = nullptr;
  if (!strcmp(aSnifferType, NS_CONTENT_SNIFFER_CATEGORY)) {
    if (!gNetSniffers) {
      gNetSniffers = new ContentSnifferCache(NS_CONTENT_SNIFFER_CATEGORY);
    }
    cache = gNetSniffers;
  } else if (!strcmp(aSnifferType, NS_DATA_SNIFFER_CATEGORY)) {
    if (!gDataSniffers) {
      gDataSniffers = new ContentSnifferCache(NS_DATA_SNIFFER_CATEGORY);
    }
    cache = gDataSniffers;
  } else if (!strcmp(aSnifferType, NS_ORB_SNIFFER_CATEGORY)) {
    if (!gORBSniffers) {
      gORBSniffers = new ContentSnifferCache(NS_ORB_SNIFFER_CATEGORY);
    }
    cache = gORBSniffers;
  } else if (!strcmp(aSnifferType, NS_CONTENT_AND_ORB_SNIFFER_CATEGORY)) {
    if (!gNetAndORBSniffers) {
      gNetAndORBSniffers =
          new ContentSnifferCache(NS_CONTENT_AND_ORB_SNIFFER_CATEGORY);
    }
    cache = gNetAndORBSniffers;
  } else {
    // Invalid content sniffer type was requested
    MOZ_ASSERT(false);
    return;
  }

  // In case XCTO nosniff was present, we could just skip sniffing here
  nsCOMPtr<nsIChannel> channel = do_QueryInterface(aRequest);
  if (channel) {
    nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();
    if (loadInfo->GetSkipContentSniffing()) {
      /* Bug 1571742
       * We cannot skip snffing if the current MIME-Type might be a JSON.
       * The JSON-Viewer relies on its own sniffer to determine, if it can
       * render the page, so we need to make an exception if the Server provides
       * a application/ mime, as it might be json.
       */
      nsAutoCString currentContentType;
      channel->GetContentType(currentContentType);
      if (!StringBeginsWith(currentContentType, "application/"_ns)) {
        return;
      }
    }
  }
  nsCOMArray<nsIContentSniffer> sniffers;
  cache->GetEntries(sniffers);
  for (int32_t i = 0; i < sniffers.Count(); ++i) {
    nsresult rv = sniffers[i]->GetMIMETypeFromContent(aRequest, aData, aLength,
                                                      aSniffedType);
    if (NS_SUCCEEDED(rv) && !aSniffedType.IsEmpty()) {
      return;
    }
  }

  aSniffedType.Truncate();
}

bool NS_IsSrcdocChannel(nsIChannel* aChannel) {
  bool isSrcdoc;
  nsCOMPtr<nsIInputStreamChannel> isr = do_QueryInterface(aChannel);
  if (isr) {
    isr->GetIsSrcdocChannel(&isSrcdoc);
    return isSrcdoc;
  }
  nsCOMPtr<nsIViewSourceChannel> vsc = do_QueryInterface(aChannel);
  if (vsc) {
    nsresult rv = vsc->GetIsSrcdocChannel(&isSrcdoc);
    if (NS_SUCCEEDED(rv)) {
      return isSrcdoc;
    }
  }
  return false;
}

// helper function for NS_ShouldSecureUpgrade for checking HSTS
bool handleResultFunc(bool aAllowSTS, bool aIsStsHost) {
  if (aIsStsHost) {
    LOG(("nsHttpChannel::Connect() STS permissions found\n"));
    if (aAllowSTS) {
      return true;
    }
  }
  return false;
};
// That function is a helper function of NS_ShouldSecureUpgrade to check if
// CSP upgrade-insecure-requests, Mixed content auto upgrading or HTTPs-Only/-
// First should upgrade the given request.
static bool ShouldSecureUpgradeNoHSTS(nsIURI* aURI, nsILoadInfo* aLoadInfo) {
  // 2. CSP upgrade-insecure-requests
  if (aLoadInfo->GetUpgradeInsecureRequests()) {
    // let's log a message to the console that we are upgrading a request
    nsAutoCString scheme;
    aURI->GetScheme(scheme);
    // append the additional 's' for security to the scheme :-)
    scheme.AppendLiteral("s");
    NS_ConvertUTF8toUTF16 reportSpec(aURI->GetSpecOrDefault());
    NS_ConvertUTF8toUTF16 reportScheme(scheme);
    AutoTArray<nsString, 2> params = {reportSpec, reportScheme};
    uint64_t innerWindowId = aLoadInfo->GetInnerWindowID();
    CSP_LogLocalizedStr("upgradeInsecureRequest", params,
                        ""_ns,   // aSourceFile
                        u""_ns,  // aScriptSample
                        0,       // aLineNumber
                        1,       // aColumnNumber
                        nsIScriptError::warningFlag,
                        "upgradeInsecureRequest"_ns, innerWindowId,
                        aLoadInfo->GetOriginAttributes().IsPrivateBrowsing());
    aLoadInfo->SetHttpsUpgradeTelemetry(nsILoadInfo::CSP_UIR);
    return true;
  }
  // 3. Mixed content auto upgrading
  if (aLoadInfo->GetBrowserUpgradeInsecureRequests()) {
    // let's log a message to the console that we are upgrading a request
    nsAutoCString scheme;
    aURI->GetScheme(scheme);
    // append the additional 's' for security to the scheme :-)
    scheme.AppendLiteral("s");
    NS_ConvertUTF8toUTF16 reportSpec(aURI->GetSpecOrDefault());
    NS_ConvertUTF8toUTF16 reportScheme(scheme);
    AutoTArray<nsString, 2> params = {reportSpec, reportScheme};

    nsAutoString localizedMsg;
    nsContentUtils::FormatLocalizedString(nsContentUtils::eSECURITY_PROPERTIES,
                                          "MixedContentAutoUpgrade", params,
                                          localizedMsg);

    // Prepending ixed Content to the outgoing console message
    nsString message;
    message.AppendLiteral(u"Mixed Content: ");
    message.Append(localizedMsg);

    uint64_t innerWindowId = aLoadInfo->GetInnerWindowID();
    nsContentUtils::ReportToConsoleByWindowID(
        message, nsIScriptError::warningFlag, "Mixed Content Message"_ns,
        innerWindowId, SourceLocation(aURI));

    // Set this flag so we know we'll upgrade because of
    // 'security.mixed_content.upgrade_display_content'.
    aLoadInfo->SetBrowserDidUpgradeInsecureRequests(true);
    return true;
  }

  // 4. Https-Only
  if (nsHTTPSOnlyUtils::ShouldUpgradeRequest(aURI, aLoadInfo)) {
    aLoadInfo->SetHttpsUpgradeTelemetry(nsILoadInfo::HTTPS_ONLY_UPGRADE);
    return true;
  }
  // 4.a Https-First
  if (nsHTTPSOnlyUtils::ShouldUpgradeHttpsFirstRequest(aURI, aLoadInfo)) {
    if (aLoadInfo->GetSchemelessInput() ==
        nsILoadInfo::SchemelessInputTypeSchemeless) {
      aLoadInfo->SetHttpsUpgradeTelemetry(
          nsILoadInfo::HTTPS_FIRST_SCHEMELESS_UPGRADE);
    } else {
      aLoadInfo->SetHttpsUpgradeTelemetry(nsILoadInfo::HTTPS_FIRST_UPGRADE);
    }
    return true;
  }
  return false;
}

// Check if channel should be upgraded. check in the following order:
// 1. HSTS
// 2. CSP upgrade-insecure-requests
// 3. Mixed content auto upgrading
// 4. Https-Only / first
// (5. Https RR - will be checked in nsHttpChannel)
nsresult NS_ShouldSecureUpgrade(
    nsIURI* aURI, nsILoadInfo* aLoadInfo, nsIPrincipal* aChannelResultPrincipal,
    bool aAllowSTS, const OriginAttributes& aOriginAttributes,
    bool& aShouldUpgrade, std::function<void(bool, nsresult)>&& aResultCallback,
    bool& aWillCallback) {
  MOZ_ASSERT(XRE_IsParentProcess());
  if (!XRE_IsParentProcess()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  aWillCallback = false;
  aShouldUpgrade = false;

  // Even if we're in private browsing mode, we still enforce existing STS
  // data (it is read-only).
  // if the connection is not using SSL and either the exact host matches or
  // a superdomain wants to force HTTPS, do it.
  bool isHttps = aURI->SchemeIs("https");

  // If request is https, then there is nothing to do here.
  if (isHttps) {
    aLoadInfo->SetHttpsUpgradeTelemetry(nsILoadInfo::ALREADY_HTTPS);
    aShouldUpgrade = false;
    return NS_OK;
  }
  // If it is a mixed content trustworthy loopback, then we shouldn't upgrade
  // it.
  if (nsMixedContentBlocker::IsPotentiallyTrustworthyLoopbackURL(aURI)) {
    aShouldUpgrade = false;
    return NS_OK;
  }
  // If no loadInfo exist there is nothing to upgrade here.
  if (!aLoadInfo) {
    aShouldUpgrade = false;
    return NS_OK;
  }
  // The loadInfo indicates no HTTPS upgrade.
  bool skipHTTPSUpgrade = false;
  Unused << aLoadInfo->GetSkipHTTPSUpgrade(&skipHTTPSUpgrade);
  if (skipHTTPSUpgrade) {
    aLoadInfo->SetHttpsUpgradeTelemetry(nsILoadInfo::SKIP_HTTPS_UPGRADE);
    aShouldUpgrade = false;
    return NS_OK;
  }
  MOZ_ASSERT(!aURI->SchemeIs("https"));

  // enforce Strict-Transport-Security
  nsISiteSecurityService* sss = gHttpHandler->GetSSService();
  NS_ENSURE_TRUE(sss, NS_ERROR_OUT_OF_MEMORY);

  bool isStsHost = false;
  // Calling |IsSecureURI| before the storage is ready to read will
  // block the main thread. Once the storage is ready, we can call it
  // from main thread.
  static Atomic<bool, Relaxed> storageReady(false);
  if (!storageReady && gSocketTransportService && aResultCallback) {
    nsCOMPtr<nsILoadInfo> loadInfo = aLoadInfo;
    nsCOMPtr<nsIURI> uri = aURI;
    auto callbackWrapper = [resultCallback{std::move(aResultCallback)}, uri,
                            loadInfo](bool aShouldUpgrade, nsresult aStatus) {
      MOZ_ASSERT(NS_IsMainThread());

      // 1. HSTS upgrade
      if (aShouldUpgrade || NS_FAILED(aStatus)) {
        resultCallback(aShouldUpgrade, aStatus);
        return;
      }
      // Check if we need to upgrade because of other reasons.
      // 2. CSP upgrade-insecure-requests
      // 3. Mixed content auto upgrading
      // 4. Https-Only / first
      bool shouldUpgrade = ShouldSecureUpgradeNoHSTS(uri, loadInfo);
      resultCallback(shouldUpgrade, aStatus);
    };
    nsCOMPtr<nsISiteSecurityService> service = sss;
    nsresult rv = gSocketTransportService->Dispatch(
        NS_NewRunnableFunction(
            "net::NS_ShouldSecureUpgrade",
            [service{std::move(service)}, uri{std::move(uri)},
             originAttributes(aOriginAttributes),
             handleResultFunc{std::move(handleResultFunc)},
             callbackWrapper{std::move(callbackWrapper)},
             allowSTS{std::move(aAllowSTS)}]() mutable {
              bool isStsHost = false;
              nsresult rv =
                  service->IsSecureURI(uri, originAttributes, &isStsHost);

              // Successfully get the result from |IsSecureURI| implies that
              // the storage is ready to read.
              storageReady = NS_SUCCEEDED(rv);
              bool shouldUpgrade = handleResultFunc(allowSTS, isStsHost);
              // Check if request should be upgraded.
              NS_DispatchToMainThread(NS_NewRunnableFunction(
                  "net::NS_ShouldSecureUpgrade::ResultCallback",
                  [rv, shouldUpgrade,
                   callbackWrapper{std::move(callbackWrapper)}]() {
                    callbackWrapper(shouldUpgrade, rv);
                  }));
            }),
        NS_DISPATCH_NORMAL);
    aWillCallback = NS_SUCCEEDED(rv);
    return rv;
  }

  nsresult rv = sss->IsSecureURI(aURI, aOriginAttributes, &isStsHost);

  // if the SSS check fails, it's likely because this load is on a
  // malformed URI or something else in the setup is wrong, so any error
  // should be reported.
  NS_ENSURE_SUCCESS(rv, rv);

  aShouldUpgrade = handleResultFunc(aAllowSTS, isStsHost);
  // we can't pass the loadinfo to handleResultFunc since it's not threadsafe
  // hence we set the http telemetry information on the loadinfo here.
  if (aShouldUpgrade) {
    aLoadInfo->SetHttpsUpgradeTelemetry(nsILoadInfo::HSTS);
  }
  if (!aShouldUpgrade) {
    // Check for CSP upgrade-insecure-requests, Mixed content auto upgrading
    // and Https-Only / -First.
    aShouldUpgrade = ShouldSecureUpgradeNoHSTS(aURI, aLoadInfo);
  }
  return rv;
}

nsresult NS_GetSecureUpgradedURI(nsIURI* aURI, nsIURI** aUpgradedURI) {
  NS_MutateURI mutator(aURI);
  mutator.SetScheme("https"_ns);  // Change the scheme to HTTPS:

  // Change the default port to 443:
  nsCOMPtr<nsIStandardURL> stdURL = do_QueryInterface(aURI);
  if (stdURL) {
    mutator.Apply(&nsIStandardURLMutator::SetDefaultPort, 443, nullptr);
  } else {
    // If we don't have a nsStandardURL, fall back to using GetPort/SetPort.
    // XXXdholbert Is this function even called with a non-nsStandardURL arg,
    // in practice?
    NS_WARNING("Calling NS_GetSecureUpgradedURI for non nsStandardURL");
    int32_t oldPort = -1;
    nsresult rv = aURI->GetPort(&oldPort);
    if (NS_FAILED(rv)) return rv;

    // Keep any nonstandard ports so only the scheme is changed.
    // For example:
    //  http://foo.com:80 -> https://foo.com:443
    //  http://foo.com:81 -> https://foo.com:81

    if (oldPort == 80 || oldPort == -1) {
      mutator.SetPort(-1);
    } else {
      mutator.SetPort(oldPort);
    }
  }

  return mutator.Finalize(aUpgradedURI);
}

nsresult NS_CompareLoadInfoAndLoadContext(nsIChannel* aChannel) {
  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();

  nsCOMPtr<nsILoadContext> loadContext;
  NS_QueryNotificationCallbacks(aChannel, loadContext);
  if (!loadContext) {
    return NS_OK;
  }

  // We try to skip about:newtab.
  // about:newtab will use SystemPrincipal to download thumbnails through
  // https:// and blob URLs.
  bool isAboutPage = false;
  nsINode* node = loadInfo->LoadingNode();
  if (node) {
    nsIURI* uri = node->OwnerDoc()->GetDocumentURI();
    isAboutPage = uri->SchemeIs("about");
  }

  if (isAboutPage) {
    return NS_OK;
  }

  // We skip the favicon loading here. The favicon loading might be
  // triggered by the XUL image. For that case, the loadContext will have
  // default originAttributes since the XUL image uses SystemPrincipal, but
  // the loadInfo will use originAttributes from the content. Thus, the
  // originAttributes between loadInfo and loadContext will be different.
  // That's why we have to skip the comparison for the favicon loading.
  if (loadInfo->GetLoadingPrincipal() &&
      loadInfo->GetLoadingPrincipal()->IsSystemPrincipal() &&
      loadInfo->InternalContentPolicyType() ==
          nsIContentPolicy::TYPE_INTERNAL_IMAGE_FAVICON) {
    return NS_OK;
  }

  OriginAttributes originAttrsLoadInfo = loadInfo->GetOriginAttributes();
  OriginAttributes originAttrsLoadContext;
  loadContext->GetOriginAttributes(originAttrsLoadContext);

  LOG(
      ("NS_CompareLoadInfoAndLoadContext - loadInfo: %d, %d; "
       "loadContext: %d, %d. [channel=%p]",
       originAttrsLoadInfo.mUserContextId,
       originAttrsLoadInfo.mPrivateBrowsingId,
       originAttrsLoadContext.mUserContextId,
       originAttrsLoadContext.mPrivateBrowsingId, aChannel));

  MOZ_ASSERT(originAttrsLoadInfo.mUserContextId ==
                 originAttrsLoadContext.mUserContextId,
             "The value of mUserContextId in the loadContext and in the "
             "loadInfo are not the same!");

  MOZ_ASSERT(originAttrsLoadInfo.mPrivateBrowsingId ==
                 originAttrsLoadContext.mPrivateBrowsingId,
             "The value of mPrivateBrowsingId in the loadContext and in the "
             "loadInfo are not the same!");

  return NS_OK;
}

nsresult NS_SetRequestBlockingReason(nsIChannel* channel, uint32_t reason) {
  NS_ENSURE_ARG(channel);

  nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();
  return NS_SetRequestBlockingReason(loadInfo, reason);
}

nsresult NS_SetRequestBlockingReason(nsILoadInfo* loadInfo, uint32_t reason) {
  NS_ENSURE_ARG(loadInfo);

  return loadInfo->SetRequestBlockingReason(reason);
}

nsresult NS_SetRequestBlockingReasonIfNull(nsILoadInfo* loadInfo,
                                           uint32_t reason) {
  NS_ENSURE_ARG(loadInfo);

  uint32_t existingReason;
  if (NS_SUCCEEDED(loadInfo->GetRequestBlockingReason(&existingReason)) &&
      existingReason != nsILoadInfo::BLOCKING_REASON_NONE) {
    return NS_OK;
  }

  return loadInfo->SetRequestBlockingReason(reason);
}

bool NS_IsOffline() {
  bool offline = true;
  bool connectivity = true;
  nsCOMPtr<nsIIOService> ios = do_GetIOService();
  if (ios) {
    ios->GetOffline(&offline);
    ios->GetConnectivity(&connectivity);
  }
  return offline || !connectivity;
}

/**
 * This function returns true if this channel should be classified by
 * the URL Classifier, false otherwise. There are two types of classification:
 *   1. SafeBrowsing
 *   2. Enhanced Tracking Protection (ETP)
 *
 * The idea of the algorithm to determine if a channel should be
 * classified is based on:
 * 1. Channels created by non-privileged code should be classified for
 *    ETP. For SafeBrowsing, it depends on the pref
 *    "browser.safebrowsing.only_top_level" to decide if it should be
 *    classified.
 * 2. Top-level document’s channels, if loaded by privileged code
 *    (system principal), should be classified for both types.
 * 3. Any other channel, created by privileged code, is considered safe.
 *
 * A bad/hacked/corrupted safebrowsing database, plus a mistakenly
 * classified critical channel (this may result from a bug in the exemption
 * rules or incorrect information being passed into) can cause serious
 * problems. For example, if the updater channel is classified and blocked
 * by the Safe Browsing, Firefox can't update itself, and there is no way to
 * recover from that.
 *
 * So two safeguards are added to ensure critical channels are never
 * automatically classified either because there is a bug in the algorithm
 * or the data in loadinfo is wrong.
 * 1. beConservative, this is set by ServiceRequest and we treat
 *    channel created for ServiceRequest as critical channels.
 * 2. nsIChannel::LOAD_BYPASS_URL_CLASSIFIER, channel's opener can use this
 *    flag to enforce bypassing the URL classifier check.
 */
bool NS_ShouldClassifyChannel(nsIChannel* aChannel, ClassifyType aType) {
  nsLoadFlags loadFlags;
  Unused << aChannel->GetLoadFlags(&loadFlags);
  //  If our load flags dictate that we must let this channel through without
  //  URL classification, obey that here without performing more checks.
  if (loadFlags & nsIChannel::LOAD_BYPASS_URL_CLASSIFIER) {
    return false;
  }

  nsCOMPtr<nsIHttpChannelInternal> httpChannel(do_QueryInterface(aChannel));
  if (httpChannel) {
    bool beConservative;
    nsresult rv = httpChannel->GetBeConservative(&beConservative);

    // beConservative flag, set by ServiceRequest to ensure channels that
    // fetch update use conservative TLS setting, are used here to identify
    // channels are critical to bypass classification. for channels don't
    // support beConservative, continue to apply the exemption rules.
    if (NS_SUCCEEDED(rv) && beConservative) {
      return false;
    }
  }

  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  ExtContentPolicyType type = loadInfo->GetExternalContentPolicyType();

  // Skip classifying channel for safe browsing unless it is a top-level.
  if (aType == ClassifyType::SafeBrowsing &&
      (StaticPrefs::browser_safebrowsing_only_top_level() &&
       ExtContentPolicy::TYPE_DOCUMENT != type)) {
    return false;
  }

  // Skip classifying channel triggered by system unless it is a top-level
  // load.
  return !(loadInfo->TriggeringPrincipal()->IsSystemPrincipal() &&
           ExtContentPolicy::TYPE_DOCUMENT != type);
}

namespace mozilla {
namespace net {

bool InScriptableRange(int64_t val) {
  return (val <= kJS_MAX_SAFE_INTEGER) && (val >= kJS_MIN_SAFE_INTEGER);
}

bool InScriptableRange(uint64_t val) { return val <= kJS_MAX_SAFE_UINTEGER; }

nsresult GetParameterHTTP(const nsACString& aHeaderVal, const char* aParamName,
                          nsAString& aResult) {
  return nsMIMEHeaderParamImpl::GetParameterHTTP(aHeaderVal, aParamName,
                                                 aResult);
}

bool ChannelIsPost(nsIChannel* aChannel) {
  if (nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aChannel)) {
    nsAutoCString method;
    Unused << httpChannel->GetRequestMethod(method);
    return method.EqualsLiteral("POST");
  }
  return false;
}

bool SchemeIsHttpOrHttps(nsIURI* aURI) {
  MOZ_ASSERT(aURI);
  return aURI->SchemeIs("http") || aURI->SchemeIs("https");
}

bool SchemeIsSpecial(const nsACString& aScheme) {
  // See https://url.spec.whatwg.org/#special-scheme
  return aScheme.EqualsIgnoreCase("ftp") || aScheme.EqualsIgnoreCase("file") ||
         aScheme.EqualsIgnoreCase("http") ||
         aScheme.EqualsIgnoreCase("https") || aScheme.EqualsIgnoreCase("ws") ||
         aScheme.EqualsIgnoreCase("wss");
}

bool IsSchemeChangePermitted(nsIURI* aOldURI, const nsACString& newScheme) {
  // See step 2.1 in https://url.spec.whatwg.org/#special-scheme
  // Note: The spec text uses "buffer" instead of newScheme, and "url"
  MOZ_ASSERT(aOldURI);

  nsAutoCString tmp;
  nsresult rv = aOldURI->GetScheme(tmp);
  // If url's scheme is a special scheme and buffer is not a
  // special scheme, then return.
  // If url's scheme is not a special scheme and buffer is a
  // special scheme, then return.
  if (NS_FAILED(rv) || SchemeIsSpecial(tmp) != SchemeIsSpecial(newScheme)) {
    return false;
  }

  // If url's scheme is "file" and its host is an empty host, then return.
  if (aOldURI->SchemeIs("file")) {
    rv = aOldURI->GetHost(tmp);
    if (NS_FAILED(rv) || tmp.IsEmpty()) {
      return false;
    }
  }

  // URL Spec: If url includes credentials or has a non-null port, and
  // buffer is "file", then return.
  if (newScheme.EqualsIgnoreCase("file")) {
    bool hasUserPass;
    if (NS_FAILED(aOldURI->GetHasUserPass(&hasUserPass)) || hasUserPass) {
      return false;
    }
    int32_t port;
    rv = aOldURI->GetPort(&port);
    if (NS_FAILED(rv) || port != -1) {
      return false;
    }
  }

  return true;
}

already_AddRefed<nsIURI> TryChangeProtocol(nsIURI* aURI,
                                           const nsACString& aProtocol) {
  MOZ_ASSERT(aURI);

  nsACString::const_iterator start;
  aProtocol.BeginReading(start);

  nsACString::const_iterator end;
  aProtocol.EndReading(end);

  nsACString::const_iterator iter(start);
  FindCharInReadable(':', iter, end);

  // Changing the protocol of a URL, changes the "nature" of the URI
  // implementation. In order to do this properly, we have to serialize the
  // existing URL and reparse it in a new object.
  nsCOMPtr<nsIURI> clone;
  nsresult rv =
      NS_MutateURI(aURI).SetScheme(Substring(start, iter)).Finalize(clone);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }

  nsAutoCString newScheme;
  rv = clone->GetScheme(newScheme);
  if (NS_FAILED(rv) || !net::IsSchemeChangePermitted(aURI, newScheme)) {
    nsAutoCString url;
    Unused << clone->GetSpec(url);
    AutoTArray<nsString, 2> params;
    params.AppendElement(NS_ConvertUTF8toUTF16(url));
    params.AppendElement(NS_ConvertUTF8toUTF16(newScheme));
    nsContentUtils::ReportToConsole(
        nsIScriptError::warningFlag, "Strict Url Protocol Setter"_ns, nullptr,
        nsContentUtils::eNECKO_PROPERTIES, "StrictUrlProtocolSetter", params);
    return nullptr;
  }

  nsAutoCString href;
  rv = clone->GetSpec(href);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }

  RefPtr<nsIURI> uri;
  rv = NS_NewURI(getter_AddRefs(uri), href);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }
  return uri.forget();
}

// Decode a parameter value using the encoding defined in RFC 5987 (in place)
//
//   charset  "'" [ language ] "'" value-chars
//
// returns true when decoding happened successfully (otherwise leaves
// passed value alone)
static bool Decode5987Format(nsAString& aEncoded) {
  nsresult rv;
  nsCOMPtr<nsIMIMEHeaderParam> mimehdrpar;
  mimehdrpar = mozilla::components::MimeHeaderParam::Service(&rv);
  if (NS_FAILED(rv)) return false;

  nsAutoCString asciiValue;

  const char16_t* encstart = aEncoded.BeginReading();
  const char16_t* encend = aEncoded.EndReading();

  // create a plain ASCII string, aborting if we can't do that
  // converted form is always shorter than input
  while (encstart != encend) {
    if (*encstart > 0 && *encstart < 128) {
      asciiValue.Append((char)*encstart);
    } else {
      return false;
    }
    encstart++;
  }

  nsAutoString decoded;
  nsAutoCString language;

  rv = mimehdrpar->DecodeRFC5987Param(asciiValue, language, decoded);
  if (NS_FAILED(rv)) return false;

  aEncoded = decoded;
  return true;
}

LinkHeader::LinkHeader() { mCrossOrigin.SetIsVoid(true); }

void LinkHeader::Reset() {
  mHref.Truncate();
  mRel.Truncate();
  mTitle.Truncate();
  mNonce.Truncate();
  mIntegrity.Truncate();
  mSrcset.Truncate();
  mSizes.Truncate();
  mType.Truncate();
  mMedia.Truncate();
  mAnchor.Truncate();
  mCrossOrigin.Truncate();
  mReferrerPolicy.Truncate();
  mAs.Truncate();
  mCrossOrigin.SetIsVoid(true);
  mFetchPriority.Truncate();
}

nsresult LinkHeader::NewResolveHref(nsIURI** aOutURI, nsIURI* aBaseURI) const {
  if (mAnchor.IsEmpty()) {
    // use the base uri
    return NS_NewURI(aOutURI, mHref, nullptr, aBaseURI);
  }

  // compute the anchored URI
  nsCOMPtr<nsIURI> anchoredURI;
  nsresult rv =
      NS_NewURI(getter_AddRefs(anchoredURI), mAnchor, nullptr, aBaseURI);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_NewURI(aOutURI, mHref, nullptr, anchoredURI);
}

bool LinkHeader::operator==(const LinkHeader& rhs) const {
  return mHref == rhs.mHref && mRel == rhs.mRel && mTitle == rhs.mTitle &&
         mNonce == rhs.mNonce && mIntegrity == rhs.mIntegrity &&
         mSrcset == rhs.mSrcset && mSizes == rhs.mSizes && mType == rhs.mType &&
         mMedia == rhs.mMedia && mAnchor == rhs.mAnchor &&
         mCrossOrigin == rhs.mCrossOrigin &&
         mReferrerPolicy == rhs.mReferrerPolicy && mAs == rhs.mAs &&
         mFetchPriority == rhs.mFetchPriority;
}

constexpr auto kTitleStar = "title*"_ns;

nsTArray<LinkHeader> ParseLinkHeader(const nsAString& aLinkData) {
  nsTArray<LinkHeader> linkHeaders;

  // keep track where we are within the header field
  bool seenParameters = false;

  // parse link content and add to array
  LinkHeader header;
  nsAutoString titleStar;

  // copy to work buffer
  nsAutoString stringList(aLinkData);

  // put an extra null at the end
  stringList.Append(kNullCh);

  char16_t* start = stringList.BeginWriting();

  while (*start != kNullCh) {
    // parse link content and call process style link

    // skip leading space
    while ((*start != kNullCh) && nsCRT::IsAsciiSpace(*start)) {
      ++start;
    }

    char16_t* end = start;
    char16_t* last = end - 1;

    bool wasQuotedString = false;

    // look for semicolon or comma
    while (*end != kNullCh && *end != kSemicolon && *end != kComma) {
      char16_t ch = *end;

      if (ch == kQuote || ch == kLessThan) {
        // quoted string

        char16_t quote = ch;
        if (quote == kLessThan) {
          quote = kGreaterThan;
        }

        wasQuotedString = (ch == kQuote);

        char16_t* closeQuote = (end + 1);

        // seek closing quote
        while (*closeQuote != kNullCh && quote != *closeQuote) {
          // in quoted-string, "\" is an escape character
          if (wasQuotedString && *closeQuote == kBackSlash &&
              *(closeQuote + 1) != kNullCh) {
            ++closeQuote;
          }

          ++closeQuote;
        }

        if (quote == *closeQuote) {
          // found closer

          // skip to close quote
          end = closeQuote;

          last = end - 1;

          ch = *(end + 1);

          if (ch != kNullCh && ch != kSemicolon && ch != kComma) {
            // end string here
            *(++end) = kNullCh;

            ch = *(end + 1);

            // keep going until semi or comma
            while (ch != kNullCh && ch != kSemicolon && ch != kComma) {
              ++end;

              ch = *(end + 1);
            }
          }
        }
      }

      ++end;
      ++last;
    }

    char16_t endCh = *end;

    // end string here
    *end = kNullCh;

    if (start < end) {
      if ((*start == kLessThan) && (*last == kGreaterThan)) {
        *last = kNullCh;

        // first instance of <...> wins
        // also, do not allow hrefs after the first param was seen
        if (header.mHref.IsEmpty() && !seenParameters) {
          header.mHref = (start + 1);
          header.mHref.StripWhitespace();
        }
      } else {
        char16_t* equals = start;
        seenParameters = true;

        while ((*equals != kNullCh) && (*equals != kEqual)) {
          equals++;
        }

        const bool hadEquals = *equals != kNullCh;
        *equals = kNullCh;
        nsAutoString attr(start);
        attr.StripWhitespace();

        char16_t* value = hadEquals ? ++equals : equals;
        while (nsCRT::IsAsciiSpace(*value)) {
          value++;
        }

        if ((*value == kQuote) && (*value == *last)) {
          *last = kNullCh;
          value++;
        }

        if (wasQuotedString) {
          // unescape in-place
          char16_t* unescaped = value;
          char16_t* src = value;

          while (*src != kNullCh) {
            if (*src == kBackSlash && *(src + 1) != kNullCh) {
              src++;
            }
            *unescaped++ = *src++;
          }

          *unescaped = kNullCh;
        }

        if (attr.LowerCaseEqualsASCII(kTitleStar.get())) {
          if (titleStar.IsEmpty() && !wasQuotedString) {
            // RFC 5987 encoding; uses token format only, so skip if we get
            // here with a quoted-string
            nsAutoString tmp;
            tmp = value;
            if (Decode5987Format(tmp)) {
              titleStar = tmp;
              titleStar.CompressWhitespace();
            } else {
              // header value did not parse, throw it away
              titleStar.Truncate();
            }
          }
        } else {
          header.MaybeUpdateAttribute(attr, value);
        }
      }
    }

    if (endCh == kComma) {
      // hit a comma, process what we've got so far

      header.mHref.Trim(" \t\n\r\f");  // trim HTML5 whitespace
      if (!header.mHref.IsEmpty() && !header.mRel.IsEmpty()) {
        if (!titleStar.IsEmpty()) {
          // prefer RFC 5987 variant over non-I18zed version
          header.mTitle = titleStar;
        }
        linkHeaders.AppendElement(header);
      }

      titleStar.Truncate();
      header.Reset();

      seenParameters = false;
    }

    start = ++end;
  }

  header.mHref.Trim(" \t\n\r\f");  // trim HTML5 whitespace
  if (!header.mHref.IsEmpty() && !header.mRel.IsEmpty()) {
    if (!titleStar.IsEmpty()) {
      // prefer RFC 5987 variant over non-I18zed version
      header.mTitle = titleStar;
    }
    linkHeaders.AppendElement(header);
  }

  return linkHeaders;
}

void LinkHeader::MaybeUpdateAttribute(const nsAString& aAttribute,
                                      const char16_t* aValue) {
  MOZ_ASSERT(!aAttribute.LowerCaseEqualsASCII(kTitleStar.get()));

  if (aAttribute.LowerCaseEqualsLiteral("rel")) {
    if (mRel.IsEmpty()) {
      mRel = aValue;
      mRel.CompressWhitespace();
    }
  } else if (aAttribute.LowerCaseEqualsLiteral("title")) {
    if (mTitle.IsEmpty()) {
      mTitle = aValue;
      mTitle.CompressWhitespace();
    }
  } else if (aAttribute.LowerCaseEqualsLiteral("type")) {
    if (mType.IsEmpty()) {
      mType = aValue;
      mType.StripWhitespace();
    }
  } else if (aAttribute.LowerCaseEqualsLiteral("media")) {
    if (mMedia.IsEmpty()) {
      mMedia = aValue;

      // The HTML5 spec is formulated in terms of the CSS3 spec,
      // which specifies that media queries are case insensitive.
      nsContentUtils::ASCIIToLower(mMedia);
    }
  } else if (aAttribute.LowerCaseEqualsLiteral("anchor")) {
    if (mAnchor.IsEmpty()) {
      mAnchor = aValue;
      mAnchor.StripWhitespace();
    }
  } else if (aAttribute.LowerCaseEqualsLiteral("crossorigin")) {
    if (mCrossOrigin.IsVoid()) {
      mCrossOrigin.SetIsVoid(false);
      mCrossOrigin = aValue;
      mCrossOrigin.StripWhitespace();
    }
  } else if (aAttribute.LowerCaseEqualsLiteral("as")) {
    if (mAs.IsEmpty()) {
      mAs = aValue;
      mAs.CompressWhitespace();
    }
  } else if (aAttribute.LowerCaseEqualsLiteral("referrerpolicy")) {
    // https://html.spec.whatwg.org/multipage/urls-and-fetching.html#referrer-policy-attribute
    // Specs says referrer policy attribute is an enumerated attribute,
    // case insensitive and includes the empty string
    // We will parse the aValue with AttributeReferrerPolicyFromString
    // later, which will handle parsing it as an enumerated attribute.
    if (mReferrerPolicy.IsEmpty()) {
      mReferrerPolicy = aValue;
    }

  } else if (aAttribute.LowerCaseEqualsLiteral("nonce")) {
    if (mNonce.IsEmpty()) {
      mNonce = aValue;
    }
  } else if (aAttribute.LowerCaseEqualsLiteral("integrity")) {
    if (mIntegrity.IsEmpty()) {
      mIntegrity = aValue;
    }
  } else if (aAttribute.LowerCaseEqualsLiteral("imagesrcset")) {
    if (mSrcset.IsEmpty()) {
      mSrcset = aValue;
    }
  } else if (aAttribute.LowerCaseEqualsLiteral("imagesizes")) {
    if (mSizes.IsEmpty()) {
      mSizes = aValue;
    }
  } else if (aAttribute.LowerCaseEqualsLiteral("fetchpriority")) {
    if (mFetchPriority.IsEmpty()) {
      LOG(("Update fetchPriority to \"%s\"",
           NS_ConvertUTF16toUTF8(aValue).get()));
      mFetchPriority = aValue;
    }
  }
}

// We will use official mime-types from:
// https://www.iana.org/assignments/media-types/media-types.xhtml#font
// We do not support old deprecated mime-types for preload feature.
// (We currectly do not support font/collection)
static uint32_t StyleLinkElementFontMimeTypesNum = 5;
static const char* StyleLinkElementFontMimeTypes[] = {
    "font/otf", "font/sfnt", "font/ttf", "font/woff", "font/woff2"};

bool IsFontMimeType(const nsAString& aType) {
  if (aType.IsEmpty()) {
    return true;
  }
  for (uint32_t i = 0; i < StyleLinkElementFontMimeTypesNum; i++) {
    if (aType.EqualsASCII(StyleLinkElementFontMimeTypes[i])) {
      return true;
    }
  }
  return false;
}

static const nsAttrValue::EnumTable kAsAttributeTable[] = {
    {"", DESTINATION_INVALID},
    {"audio", DESTINATION_AUDIO},
    {"font", DESTINATION_FONT},
    {"image", DESTINATION_IMAGE},
    {"script", DESTINATION_SCRIPT},
    {"style", DESTINATION_STYLE},
    {"track", DESTINATION_TRACK},
    {"video", DESTINATION_VIDEO},
    {"fetch", DESTINATION_FETCH},
    {"json", DESTINATION_JSON},
    {nullptr, 0}};

void ParseAsValue(const nsAString& aValue, nsAttrValue& aResult) {
  DebugOnly<bool> success =
      aResult.ParseEnumValue(aValue, kAsAttributeTable, false,
                             // default value is a empty string
                             // if aValue is not a value we
                             // understand
                             &kAsAttributeTable[0]);
  MOZ_ASSERT(success);
}

nsContentPolicyType AsValueToContentPolicy(const nsAttrValue& aValue) {
  switch (aValue.GetEnumValue()) {
    case DESTINATION_INVALID:
      return nsIContentPolicy::TYPE_INVALID;
    case DESTINATION_AUDIO:
      return nsIContentPolicy::TYPE_INTERNAL_AUDIO;
    case DESTINATION_TRACK:
      return nsIContentPolicy::TYPE_INTERNAL_TRACK;
    case DESTINATION_VIDEO:
      return nsIContentPolicy::TYPE_INTERNAL_VIDEO;
    case DESTINATION_FONT:
      return nsIContentPolicy::TYPE_FONT;
    case DESTINATION_IMAGE:
      return nsIContentPolicy::TYPE_IMAGE;
    case DESTINATION_SCRIPT:
      return nsIContentPolicy::TYPE_SCRIPT;
    case DESTINATION_STYLE:
      return nsIContentPolicy::TYPE_STYLESHEET;
    case DESTINATION_FETCH:
      return nsIContentPolicy::TYPE_INTERNAL_FETCH_PRELOAD;
    case DESTINATION_JSON:
      return nsIContentPolicy::TYPE_JSON;
  }
  return nsIContentPolicy::TYPE_INVALID;
}

// TODO: implement this using nsAttrValue's destination enums when support for
// the new destinations is added; see this diff for a possible start:
// https://phabricator.services.mozilla.com/D172368?vs=705114&id=708720
bool IsScriptLikeOrInvalid(const nsAString& aAs) {
  return !(
      aAs.LowerCaseEqualsASCII("fetch") || aAs.LowerCaseEqualsASCII("audio") ||
      aAs.LowerCaseEqualsASCII("document") ||
      aAs.LowerCaseEqualsASCII("embed") || aAs.LowerCaseEqualsASCII("font") ||
      aAs.LowerCaseEqualsASCII("frame") || aAs.LowerCaseEqualsASCII("iframe") ||
      aAs.LowerCaseEqualsASCII("image") ||
      aAs.LowerCaseEqualsASCII("manifest") ||
      aAs.LowerCaseEqualsASCII("object") ||
      aAs.LowerCaseEqualsASCII("report") || aAs.LowerCaseEqualsASCII("style") ||
      aAs.LowerCaseEqualsASCII("track") || aAs.LowerCaseEqualsASCII("video") ||
      aAs.LowerCaseEqualsASCII("webidentity") ||
      aAs.LowerCaseEqualsASCII("xslt") || aAs.LowerCaseEqualsASCII("json"));
}

bool CheckPreloadAttrs(const nsAttrValue& aAs, const nsAString& aType,
                       const nsAString& aMedia,
                       mozilla::dom::Document* aDocument) {
  nsContentPolicyType policyType = AsValueToContentPolicy(aAs);
  if (policyType == nsIContentPolicy::TYPE_INVALID) {
    return false;
  }

  // Check if media attribute is valid.
  if (!aMedia.IsEmpty()) {
    RefPtr<mozilla::dom::MediaList> mediaList =
        mozilla::dom::MediaList::Create(NS_ConvertUTF16toUTF8(aMedia));
    if (!mediaList->Matches(*aDocument)) {
      return false;
    }
  }

  if (aType.IsEmpty()) {
    return true;
  }

  if (policyType == nsIContentPolicy::TYPE_INTERNAL_FETCH_PRELOAD) {
    return true;
  }

  nsAutoString type(aType);
  ToLowerCase(type);
  if (policyType == nsIContentPolicy::TYPE_MEDIA) {
    if (aAs.GetEnumValue() == DESTINATION_TRACK) {
      return type.EqualsASCII("text/vtt");
    }
    Maybe<MediaContainerType> mimeType = MakeMediaContainerType(aType);
    if (!mimeType) {
      return false;
    }
    DecoderDoctorDiagnostics diagnostics;
    CanPlayStatus status =
        DecoderTraits::CanHandleContainerType(*mimeType, &diagnostics);
    // Preload if this return CANPLAY_YES and CANPLAY_MAYBE.
    return status != CANPLAY_NO;
  }
  if (policyType == nsIContentPolicy::TYPE_FONT) {
    return IsFontMimeType(type);
  }
  if (policyType == nsIContentPolicy::TYPE_IMAGE) {
    return imgLoader::SupportImageWithMimeType(
        NS_ConvertUTF16toUTF8(type), AcceptedMimeTypes::IMAGES_AND_DOCUMENTS);
  }
  if (policyType == nsIContentPolicy::TYPE_SCRIPT) {
    return nsContentUtils::IsJavascriptMIMEType(type);
  }
  if (policyType == nsIContentPolicy::TYPE_STYLESHEET) {
    return type.EqualsASCII("text/css");
  }
  if (policyType == nsIContentPolicy::TYPE_JSON) {
    return nsContentUtils::IsJsonMimeType(type);
  }
  return false;
}

void WarnIgnoredPreload(const mozilla::dom::Document& aDoc, nsIURI& aURI) {
  AutoTArray<nsString, 1> params;
  {
    nsCString uri = nsContentUtils::TruncatedURLForDisplay(&aURI);
    AppendUTF8toUTF16(uri, *params.AppendElement());
  }
  nsContentUtils::ReportToConsole(nsIScriptError::warningFlag, "DOM"_ns, &aDoc,
                                  nsContentUtils::eDOM_PROPERTIES,
                                  "PreloadIgnoredInvalidAttr", params);
}

nsresult HasRootDomain(const nsACString& aInput, const nsACString& aHost,
                       bool* aResult) {
  if (NS_WARN_IF(!aResult)) {
    return NS_ERROR_FAILURE;
  }

  *aResult = false;

  // If the strings are the same, we obviously have a match.
  if (aInput == aHost) {
    *aResult = true;
    return NS_OK;
  }

  // If aHost is not found, we know we do not have it as a root domain.
  int32_t index = nsAutoCString(aInput).Find(aHost);
  if (index == kNotFound) {
    return NS_OK;
  }

  // Otherwise, we have aHost as our root domain iff the index of aHost is
  // aHost.length subtracted from our length and (since we do not have an
  // exact match) the character before the index is a dot or slash.
  *aResult = index > 0 && (uint32_t)index == aInput.Length() - aHost.Length() &&
             (aInput[index - 1] == '.' || aInput[index - 1] == '/');
  return NS_OK;
}

void CheckForBrokenChromeURL(nsILoadInfo* aLoadInfo, nsIURI* aURI) {
  if (!aURI) {
    return;
  }
  nsAutoCString scheme;
  aURI->GetScheme(scheme);
  if (!scheme.EqualsLiteral("chrome") && !scheme.EqualsLiteral("resource")) {
    return;
  }
  nsAutoCString host;
  aURI->GetHost(host);
  // Ignore test hits.
  if (host.EqualsLiteral("mochitests") || host.EqualsLiteral("reftest")) {
    return;
  }

  nsAutoCString filePath;
  aURI->GetFilePath(filePath);
  // Fluent likes checking for files everywhere and expects failure.
  if (StringEndsWith(filePath, ".ftl"_ns)) {
    return;
  }

  // Ignore fetches/xhrs, as they are frequently used in a way where
  // non-existence is OK (ie with fallbacks). This risks false negatives (ie
  // files that *should* be there but aren't) - which we accept for now.
  ExtContentPolicy policy = aLoadInfo
                                ? aLoadInfo->GetExternalContentPolicyType()
                                : ExtContentPolicy::TYPE_OTHER;
  if (policy == ExtContentPolicy::TYPE_FETCH ||
      policy == ExtContentPolicy::TYPE_XMLHTTPREQUEST) {
    return;
  }

  nsCString spec;
  aURI->GetSpec(spec);

#ifdef ANDROID
  // Various toolkit files use this and are shipped on android, but
  // info-pages.css and aboutLicense.css are not - bug 1808987
  if (StringEndsWith(spec, "info-pages.css"_ns) ||
      StringEndsWith(spec, "aboutLicense.css"_ns) ||
      // Error page CSS is also missing: bug 1810039
      StringEndsWith(spec, "aboutNetError.css"_ns) ||
      StringEndsWith(spec, "aboutHttpsOnlyError.css"_ns) ||
      StringEndsWith(spec, "error-pages.css"_ns) ||
      // popup.css is used in a single mochitest: bug 1810577
      StringEndsWith(spec, "/popup.css"_ns) ||
      // Used by an extension installation test - bug 1809650
      StringBeginsWith(spec, "resource://android/assets/web_extensions/"_ns)) {
    return;
  }
#endif

  // DTD files from gre may not exist when requested by tests.
  if (StringBeginsWith(spec, "resource://gre/res/dtd/"_ns)) {
    return;
  }

  // The background task machinery allows the caller to specify a JSM on the
  // command line, which is then looked up in both app-specific and toolkit-wide
  // locations.
  if (spec.Find("backgroundtasks") != kNotFound) {
    return;
  }

  if (xpc::IsInAutomation()) {
#ifdef DEBUG
    if (NS_IsMainThread()) {
      nsCOMPtr<nsIXPConnect> xpc = nsIXPConnect::XPConnect();
      Unused << xpc->DebugDumpJSStack(false, false, false);
    }
#endif
    MOZ_CRASH_UNSAFE_PRINTF("Missing chrome or resource URLs: %s", spec.get());
  } else {
    printf_stderr("Missing chrome or resource URL: %s\n", spec.get());
  }
}

bool IsCoepCredentiallessEnabled(bool aIsOriginTrialCoepCredentiallessEnabled) {
  return StaticPrefs::
             browser_tabs_remote_coep_credentialless_DoNotUseDirectly() ||
         aIsOriginTrialCoepCredentiallessEnabled;
}

nsresult AddExtraHeaders(nsIHttpChannel* aHttpChannel,
                         const nsACString& aExtraHeaders,
                         bool aMerge /* = true */) {
  nsresult rv;
  nsAutoCString oneHeader;
  nsAutoCString headerName;
  nsAutoCString headerValue;
  int32_t crlf = 0;
  int32_t colon = 0;
  const char* kWhitespace = "\b\t\r\n ";
  nsAutoCString extraHeaders(aExtraHeaders);
  while (true) {
    crlf = extraHeaders.Find("\r\n");
    if (crlf == -1) break;
    extraHeaders.Mid(oneHeader, 0, crlf);
    extraHeaders.Cut(0, crlf + 2);
    colon = oneHeader.Find(":");
    if (colon == -1) break;  // Should have a colon.
    oneHeader.Left(headerName, colon);
    colon++;
    oneHeader.Mid(headerValue, colon, oneHeader.Length() - colon);
    headerName.Trim(kWhitespace);
    headerValue.Trim(kWhitespace);
    // Add the header (merging if required).
    rv = aHttpChannel->SetRequestHeader(headerName, headerValue, aMerge);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

}  // namespace net
}  // namespace mozilla
