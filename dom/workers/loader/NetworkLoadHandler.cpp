/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "NetworkLoadHandler.h"

#include <memory>

#include "CacheLoadHandler.h"  // CachePromiseHandler
#include "js/loader/ModuleLoadRequest.h"
#include "js/loader/ScriptLoadRequest.h"
#include "mozilla/Encoding.h"
#include "mozilla/StaticPrefs_javascript.h"
#include "mozilla/dom/BlobURLProtocolHandler.h"
#include "mozilla/dom/InternalResponse.h"
#include "mozilla/dom/PolicyContainer.h"
#include "mozilla/dom/Response.h"
#include "mozilla/dom/ScriptLoader.h"
#include "mozilla/dom/ServiceWorkerBinding.h"
#include "mozilla/dom/ServiceWorkerManager.h"
#include "mozilla/dom/WorkerScope.h"
#include "mozilla/dom/workerinternals/ScriptLoader.h"  // WorkerScriptLoader
#include "nsContentUtils.h"
#include "nsIChannel.h"
#include "nsIHttpChannel.h"
#include "nsIHttpChannelInternal.h"
#include "nsIPrincipal.h"
#include "nsIScriptError.h"
#include "nsNetUtil.h"

using mozilla::ipc::PrincipalInfo;

namespace mozilla {
namespace dom {

namespace workerinternals::loader {

NS_IMPL_ISUPPORTS(NetworkLoadHandler, nsIStreamLoaderObserver,
                  nsIRequestObserver)

NetworkLoadHandler::NetworkLoadHandler(WorkerScriptLoader* aLoader,
                                       ThreadSafeRequestHandle* aRequestHandle)
    : mLoader(aLoader),
      mWorkerRef(aLoader->mWorkerRef),
      mRequestHandle(aRequestHandle) {
  MOZ_ASSERT(mLoader);

  // Worker scripts are always decoded as UTF-8 per spec.
  mDecoder = std::make_unique<ScriptDecoder>(
      UTF_8_ENCODING, ScriptDecoder::BOMHandling::Remove);
}

NS_IMETHODIMP
NetworkLoadHandler::OnStreamComplete(nsIStreamLoader* aLoader,
                                     nsISupports* aContext, nsresult aStatus,
                                     uint32_t aStringLen,
                                     const uint8_t* aString) {
  AssertIsOnMainThread();

  // If the request has been handed over to the worker thread, it means that
  // the loader has shut down and we can exit early. This happens when
  // cancelling the channel failed and the load was finished without us; see
  // ScriptLoaderRunnable::CancelMainThread().
  if (mRequestHandle->ExecutionScheduled()) {
    return NS_OK;
  }
  nsresult rv = DataReceivedFromNetwork(aLoader, aStatus, aStringLen, aString);
  return mRequestHandle->OnStreamComplete(rv);
}

nsresult NetworkLoadHandler::DataReceivedFromNetwork(nsIStreamLoader* aLoader,
                                                     nsresult aStatus,
                                                     uint32_t aStringLen,
                                                     const uint8_t* aString) {
  AssertIsOnMainThread();
  MOZ_ASSERT(!mRequestHandle->ExecutionScheduled());

  if (aStringLen > GetWorkerScriptMaxSizeInBytes()) {
    Document* parentDoc = mWorkerRef->Private()->GetDocument();
    nsContentUtils::ReportToConsole(nsIScriptError::errorFlag, "DOM"_ns,
                                    parentDoc, PropertiesFile::DOM_PROPERTIES,
                                    "WorkerScriptTooLargeError");
    return NS_ERROR_DOM_ABORT_ERR;
  }

  WorkerLoadContext* loadContext = mRequestHandle->GetContext();

  if (!loadContext->mChannel) {
    return NS_BINDING_ABORTED;
  }

#ifdef NIGHTLY_BUILD
  if (StaticPrefs::javascript_options_experimental_wasm_esm_integration()) {
    if (mRequestHandle->GetRequest()->IsModuleRequest()) {
      // https://html.spec.whatwg.org/multipage/webappapis.html#fetch-a-single-module-script
      // Extract the content-type. If its essence is wasm, we'll attempt to
      // compile this module as a wasm module. (Steps 13.2, 13.6)
      nsAutoCString mimeType;
      if (NS_SUCCEEDED(loadContext->mChannel->GetContentType(mimeType))) {
        if (nsContentUtils::HasWasmMimeTypeEssence(
                NS_ConvertUTF8toUTF16(mimeType))) {
          mRequestHandle->GetRequest()
              ->AsModuleRequest()
              ->SetHasWasmMimeTypeEssence();
          loadContext->mRequest->SetWasmBytes();
          if (!loadContext->mRequest->WasmBytes().append(aString, aStringLen)) {
            return NS_ERROR_OUT_OF_MEMORY;
          }
        }
      }
    }
  }
#endif

  loadContext->mChannel = nullptr;

  if (NS_FAILED(aStatus)) {
    return aStatus;
  }

  if (mRequestHandle->IsCancelled()) {
    return mRequestHandle->GetCancelResult();
  }

  NS_ASSERTION(aString, "This should never be null!");

  nsCOMPtr<nsIRequest> request;
  nsresult rv = aLoader->GetRequest(getter_AddRefs(request));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIChannel> channel = do_QueryInterface(request);
  MOZ_ASSERT(channel);

  nsIScriptSecurityManager* ssm = nsContentUtils::GetSecurityManager();
  NS_ASSERTION(ssm, "Should never be null!");

  nsCOMPtr<nsIPrincipal> channelPrincipal;
  rv =
      ssm->GetChannelResultPrincipal(channel, getter_AddRefs(channelPrincipal));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  nsIPrincipal* principal = mWorkerRef->Private()->GetPrincipal();
  if (!principal) {
    WorkerPrivate* parentWorker = mWorkerRef->Private()->GetParent();
    MOZ_ASSERT(parentWorker, "Must have a parent!");
    principal = parentWorker->GetPrincipal();
  }

#ifdef DEBUG
  if (loadContext->IsTopLevel()) {
    nsCOMPtr<nsIPrincipal> loadingPrincipal =
        mWorkerRef->Private()->GetLoadingPrincipal();
    // if we are not in a ServiceWorker, and the principal is not null, then
    // the loading principal must subsume the worker principal if it is not a
    // nullPrincipal (sandbox).
    MOZ_ASSERT(!loadingPrincipal || loadingPrincipal->GetIsNullPrincipal() ||
               principal->GetIsNullPrincipal() ||
               loadingPrincipal->Subsumes(principal));
  }
#endif

  // We don't mute the main worker script because we've already done
  // same-origin checks on them so we should be able to see their errors.
  // Note that for data: url, where we allow it through the same-origin check
  // but then give it a different origin.
  loadContext->mMutedErrorFlag.emplace(!loadContext->IsTopLevel() &&
                                       !principal->Subsumes(channelPrincipal));

  // Make sure we're not seeing the result of a 404 or something by checking
  // the 'requestSucceeded' attribute on the http channel.
  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(request);
  PolicyContainer::Headers policyContainerHeaders;
  nsAutoCString tRPHeaderCValue;

  if (httpChannel) {
    bool requestSucceeded;
    rv = httpChannel->GetRequestSucceeded(&requestSucceeded);
    NS_ENSURE_SUCCESS(rv, rv);

    if (!requestSucceeded) {
      return NS_ERROR_NOT_AVAILABLE;
    }

    (void)httpChannel->GetResponseHeader(
        "content-security-policy"_ns,
        policyContainerHeaders.mContentSecurityPolicy);

    (void)httpChannel->GetResponseHeader(
        "content-security-policy-report-only"_ns,
        policyContainerHeaders.mContentSecurityPolicyReportOnly);

    (void)httpChannel->GetResponseHeader(
        "connection-allowlist"_ns, policyContainerHeaders.mConnectionAllowlist);

    (void)httpChannel->GetResponseHeader(
        "connection-allowlist-report-only"_ns,
        policyContainerHeaders.mConnectionAllowlistReportOnly);

    (void)httpChannel->GetResponseHeader("referrer-policy"_ns, tRPHeaderCValue);

    nsAutoCString sourceMapURL;
    if (nsContentUtils::GetSourceMapURL(httpChannel, sourceMapURL)) {
      loadContext->mRequest->SetSourceMapURL(
          NS_ConvertUTF8toUTF16(sourceMapURL));
    }
  }

  // May be null.
  Document* parentDoc = mWorkerRef->Private()->GetDocument();

  // We only decode source text, not wasm bytecode.
  if (!loadContext->mRequest->IsWasmBytes()) {
    // Set the Source type to "text" for decoding.
    loadContext->mRequest->SetTextSource(loadContext);

    // Use the regular ScriptDecoder Decoder for this grunt work! Should be just
    // fine because we're running on the main thread.
    rv = mDecoder->DecodeRawData(loadContext->mRequest, aString, aStringLen,
                                 /* aEndOfStream = */ true);
    NS_ENSURE_SUCCESS(rv, rv);

    if (!loadContext->mRequest->ScriptTextLength()) {
      nsContentUtils::ReportToConsole(nsIScriptError::warningFlag, "DOM"_ns,
                                      parentDoc, PropertiesFile::DOM_PROPERTIES,
                                      "EmptyWorkerSourceWarning");
    }
  }

  // For modules, we need to store the base URI on the module request object,
  // rather than on the worker private (as we do for classic scripts). This is
  // because module loading is shared across multiple components, with
  // ScriptLoadRequests being the common structure among them. This specific
  // use of the base url is used when resolving the module specifier for child
  // modules.
  nsCOMPtr<nsIURI> uri;
  rv = channel->GetOriginalURI(getter_AddRefs(uri));
  NS_ENSURE_SUCCESS(rv, rv);

  loadContext->mRequest->SetBaseURLFromChannelAndOriginalURI(channel, uri);

  // Figure out what we actually loaded.
  nsCOMPtr<nsIURI> finalURI;
  rv = NS_GetFinalChannelURI(channel, getter_AddRefs(finalURI));
  NS_ENSURE_SUCCESS(rv, rv);

  if (principal->IsSameOrigin(finalURI)) {
    nsCString filename;
    rv = finalURI->GetSpec(filename);
    NS_ENSURE_SUCCESS(rv, rv);

    if (!filename.IsEmpty()) {
      // This will help callers figure out what their script url resolved to
      // in case of errors, and is used for debugging.
      // The full URL shouldn't be exposed to the debugger if cross origin.
      // See Bug 1634872.
      loadContext->mRequest->mURL = filename;
    }
  }

  // Update the principal of the worker and its base URI if we just loaded the
  // worker's primary script.
  bool isDynamic = loadContext->mRequest->IsModuleRequest() &&
                   loadContext->mRequest->AsModuleRequest()->IsDynamicImport();
  if (loadContext->IsTopLevel() && !isDynamic) {
    // Take care of the base URI first.
    mWorkerRef->Private()->SetBaseURI(finalURI);

    if (httpChannel) {
      nsCString reportingEndpoints;
      if (NS_SUCCEEDED(httpChannel->GetResponseHeader("Reporting-Endpoints"_ns,
                                                      reportingEndpoints))) {
        mWorkerRef->Private()->SetReportingEndpointsHeader(reportingEndpoints);
      }
    }

    // Store the channel info if needed.
    mWorkerRef->Private()->InitChannelInfo(channel);

    // Our final channel principal should match the loading principal
    // in terms of the origin.  This used to be an assert, but it seems
    // there are some rare cases where this check can fail in practice.
    // Perhaps some browser script setting nsIChannel.owner, etc.
    NS_ENSURE_TRUE(mWorkerRef->Private()->FinalChannelPrincipalIsValid(channel),
                   NS_ERROR_FAILURE);

    // However, we must still override the principal since the nsIPrincipal
    // URL may be different due to same-origin redirects.  Unfortunately this
    // URL must exactly match the final worker script URL in order to
    // properly set the referrer header on fetch/xhr requests.  If bug 1340694
    // is ever fixed this can be removed.
    MOZ_TRY(mWorkerRef->Private()->SetPrincipalsAndPolicyContainerFromChannel(
        channel));

    // Set the Policy-Container from the headers if we don't inherit it from
    // the channel.
    if (!CSP_ShouldResponseInheritCSP(channel)) {
      MOZ_TRY(mWorkerRef->Private()->SetPolicyContainerFromHeaders(
          policyContainerHeaders));
    }

    mWorkerRef->Private()->UpdateReferrerInfoFromHeader(tRPHeaderCValue);

    WorkerPrivate* parent = mWorkerRef->Private()->GetParent();
    if (parent) {
      // XHR Params Allowed
      mWorkerRef->Private()->SetXHRParamsAllowed(parent->XHRParamsAllowed());
    }

    nsCOMPtr<nsILoadInfo> chanLoadInfo = channel->LoadInfo();
    if (chanLoadInfo) {
      mLoader->SetController(chanLoadInfo->GetController());
    }

    // If we are loading a blob URL we must inherit the controller
    // from the parent.  This is a bit odd as the blob URL may have
    // been created in a different context with a different controller.
    // For now, though, this is what the spec says.  See:
    //
    // https://github.com/w3c/ServiceWorker/issues/1261
    //
    if (IsBlobURI(mWorkerRef->Private()->GetBaseURI())) {
      MOZ_DIAGNOSTIC_ASSERT(mLoader->GetController().isNothing());
      mLoader->SetController(mWorkerRef->Private()->GetParentController());
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
NetworkLoadHandler::OnStartRequest(nsIRequest* aRequest) {
  nsresult rv = PrepareForRequest(aRequest);

  if (NS_WARN_IF(NS_FAILED(rv))) {
    aRequest->Cancel(rv);
  }

  return rv;
}

nsresult NetworkLoadHandler::PrepareForRequest(nsIRequest* aRequest) {
  AssertIsOnMainThread();

  // If one load info cancels or hits an error, it can race with the start
  // callback coming from another load info. The request may even have been
  // handed over to the worker thread already, in which case we must not touch
  // it any more and let the caller cancel this channel.
  if (mRequestHandle->ExecutionScheduled() || mRequestHandle->IsCancelled()) {
    return NS_ERROR_FAILURE;
  }

  WorkerLoadContext* loadContext = mRequestHandle->GetContext();

  nsCOMPtr<nsIChannel> channel = do_QueryInterface(aRequest);

  // Checking the MIME type is only required for ServiceWorkers'
  // importScripts, per step 10 of
  // https://w3c.github.io/ServiceWorker/#importscripts
  //
  // "Extract a MIME type from the response’s header list. If this MIME type
  // (ignoring parameters) is not a JavaScript MIME type, return a network
  // error."
  if (mWorkerRef->Private()->IsServiceWorker()) {
    nsAutoCString mimeType;
    channel->GetContentType(mimeType);

    auto mimeTypeUTF16 = NS_ConvertUTF8toUTF16(mimeType);
    if (!nsContentUtils::IsJavascriptMIMEType(mimeTypeUTF16)) {
      // JSON is only allowed for non-toplevel JSON module imports, not for
      // classic importScripts() or the top-level worker script.
      if (!((!loadContext->IsTopLevel() &&
             loadContext->mRequest->IsModuleRequest() &&
             loadContext->mRequest->AsModuleRequest()->mModuleType ==
                 JS::ModuleType::JSON &&
             nsContentUtils::IsJsonMimeType(mimeTypeUTF16))
#ifdef NIGHTLY_BUILD
            // Allow wasm modules.
            || (StaticPrefs::
                    javascript_options_experimental_wasm_esm_integration() &&
                nsContentUtils::HasWasmMimeTypeEssence(mimeTypeUTF16))
#endif
            // Allow non-toplevel text modules
            || (JS::Prefs::experimental_import_text() &&
                !loadContext->IsTopLevel() &&
                loadContext->mRequest->IsModuleRequest() &&
                loadContext->mRequest->AsModuleRequest()->mModuleType ==
                    JS::ModuleType::Text))) {
        const nsCString& scope = mWorkerRef->Private()
                                     ->GetServiceWorkerRegistrationDescriptor()
                                     .Scope();

        ServiceWorkerManager::LocalizeAndReportToAllClients(
            scope, "ServiceWorkerRegisterMimeTypeError2",
            nsTArray<nsString>{
                NS_ConvertUTF8toUTF16(scope), NS_ConvertUTF8toUTF16(mimeType),
                NS_ConvertUTF8toUTF16(loadContext->mRequest->mURL)});

        return NS_ERROR_DOM_NETWORK_ERR;
      }
    }
  }

  // We synthesize the result code, but its never exposed to content.
  SafeRefPtr<mozilla::dom::InternalResponse> ir =
      MakeSafeRefPtr<mozilla::dom::InternalResponse>(200, "OK"_ns);
  ir->SetBody(loadContext->mCacheReadStream,
              InternalResponse::UNKNOWN_BODY_SIZE);

  // Drop our reference to the stream now that we've passed it along, so it
  // doesn't hang around once the cache is done with it and keep data alive.
  loadContext->mCacheReadStream = nullptr;

  // Set the channel info of the channel on the response so that it's
  // saved in the cache.
  ir->InitChannelInfo(channel);

  // Save the principal of the channel since its URI encodes the script URI
  // rather than the ServiceWorkerRegistrationInfo URI.
  nsIScriptSecurityManager* ssm = nsContentUtils::GetSecurityManager();
  NS_ASSERTION(ssm, "Should never be null!");

  nsCOMPtr<nsIPrincipal> channelPrincipal;
  MOZ_TRY(ssm->GetChannelResultPrincipal(channel,
                                         getter_AddRefs(channelPrincipal)));

  auto principalInfo = std::make_unique<PrincipalInfo>();
  MOZ_TRY(PrincipalToPrincipalInfo(channelPrincipal, principalInfo.get()));

  ir->SetPrincipalInfo(std::move(principalInfo));
  ir->Headers()->FillResponseHeaders(channel);

  RefPtr<CacheCreator> cacheCreator = mRequestHandle->GetCacheCreator();
  if (NS_WARN_IF(!cacheCreator)) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<mozilla::dom::Response> response = new mozilla::dom::Response(
      cacheCreator->Global(), std::move(ir), nullptr);

  mozilla::dom::RequestOrUTF8String request;

  MOZ_ASSERT(!loadContext->mFullURL.IsEmpty());
  request.SetAsUTF8String() = loadContext->mFullURL;

  // This JSContext will not end up executing JS code because here there are
  // no ReadableStreams involved.
  AutoJSAPI jsapi;
  jsapi.Init();

  ErrorResult error;
  RefPtr<Promise> cachePromise =
      cacheCreator->Cache_()->Put(jsapi.cx(), request, *response, error);
  error.WouldReportJSException();
  if (NS_WARN_IF(error.Failed())) {
    return error.StealNSResult();
  }

  RefPtr<CachePromiseHandler> promiseHandler =
      new CachePromiseHandler(mLoader, mRequestHandle);
  cachePromise->AppendNativeHandler(promiseHandler);

  loadContext->mCachePromise.swap(cachePromise);
  loadContext->mCacheStatus = WorkerLoadContext::WritingToCache;

  return NS_OK;
}

}  // namespace workerinternals::loader

}  // namespace dom
}  // namespace mozilla
