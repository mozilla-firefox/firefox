/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ScriptResponseHeaderProcessor.h"

#include "mozilla/StaticPrefs_javascript.h"
#include "mozilla/Try.h"
#include "mozilla/dom/WorkerRef.h"
#include "mozilla/dom/WorkerScope.h"

namespace mozilla {
namespace dom {

namespace workerinternals {

namespace loader {

NS_IMPL_ISUPPORTS(ScriptResponseHeaderProcessor, nsIRequestObserver);

nsresult ScriptResponseHeaderProcessor::ProcessCrossOriginEmbedderPolicyHeader(
    WorkerPrivate* aWorkerPrivate,
    nsILoadInfo::CrossOriginEmbedderPolicy aPolicy, bool aIsMainScript) {
  MOZ_ASSERT(aWorkerPrivate);

  if (aIsMainScript) {
    MOZ_TRY(aWorkerPrivate->SetEmbedderPolicy(aPolicy));
  } else {
    // NOTE: Spec doesn't mention non-main scripts must match COEP header with
    // the main script, but it must pass CORP checking.
    // see: wpt window-simple-success.https.html, the worker import script
    // test-incrementer.js without coep header.
    (void)NS_WARN_IF(!aWorkerPrivate->MatchEmbedderPolicy(aPolicy));
  }

  return NS_OK;
}

// Enforce strict MIME type checks for worker-imported scripts
// https://github.com/whatwg/html/pull/4001
nsresult ScriptResponseHeaderProcessor::EnsureExpectedModuleType(
    nsIRequest* aRequest) {
  if (mModuleType == Some(JS::ModuleType::Text)) {
    return NS_OK;
  }

  nsCOMPtr<nsIChannel> channel = do_QueryInterface(aRequest);
  MOZ_ASSERT(channel);
  nsAutoCString mimeType;
  channel->GetContentType(mimeType);
  NS_ConvertUTF8toUTF16 typeString(mimeType);

  if (mModuleType.isNothing()) {
    if (nsContentUtils::IsJavascriptMIMEType(typeString)) {
      return NS_OK;
    }
    return NS_ERROR_DOM_NETWORK_ERR;
  }

  if (*mModuleType == JS::ModuleType::JavaScriptOrWasm) {
    if (nsContentUtils::IsJavascriptMIMEType(typeString)) {
      return NS_OK;
    }
#ifdef NIGHTLY_BUILD
    if (StaticPrefs::javascript_options_experimental_wasm_esm_integration()) {
      if (nsContentUtils::HasWasmMimeTypeEssence(typeString)) {
        return NS_OK;
      }
    }
#endif
  }

  if (*mModuleType == JS::ModuleType::JSON &&
      nsContentUtils::IsJsonMimeType(typeString)) {
    return NS_OK;
  }

  return NS_ERROR_DOM_NETWORK_ERR;
}

nsresult ScriptResponseHeaderProcessor::ProcessCrossOriginEmbedderPolicyHeader(
    nsIRequest* aRequest) {
  nsCOMPtr<nsIHttpChannelInternal> httpChannel = do_QueryInterface(aRequest);

  MOZ_ASSERT(mWorkerRef);
  // NOTE: the spec doesn't say what to do with non-HTTP workers.
  // See: https://github.com/whatwg/html/issues/4916
  if (!httpChannel) {
    if (mIsMainScript) {
      mWorkerRef->Private()->InheritOwnerEmbedderPolicyOrNull(aRequest);
    }

    return NS_OK;
  }

  nsILoadInfo::CrossOriginEmbedderPolicy coep;
  MOZ_TRY(httpChannel->GetResponseEmbedderPolicy(
      mWorkerRef->Private()->Trials().IsEnabled(
          OriginTrial::CoepCredentialless),
      &coep));

  return ProcessCrossOriginEmbedderPolicyHeader(mWorkerRef->Private(), coep,
                                                mIsMainScript);
}

}  // namespace loader
}  // namespace workerinternals

}  // namespace dom
}  // namespace mozilla
