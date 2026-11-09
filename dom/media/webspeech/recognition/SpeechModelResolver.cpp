/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SpeechModelResolver.h"

#include <functional>

#include "SpeechModelDownloadPermissionRequest.h"
#include "SpeechRecognitionModelMapping.h"
#include "js/Conversions.h"
#include "mozilla/Logging.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/Promise-inl.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "nsIMLModelHub.h"
#include "nsServiceManagerUtils.h"

namespace mozilla::dom {

static LazyLogModule gSpeechModelDownloadLog("SpeechModelDownload");
#define LOGD(...) \
  MOZ_LOG_FMT(gSpeechModelDownloadLog, LogLevel::Debug, __VA_ARGS__)
#define LOGE(...) \
  MOZ_LOG_FMT(gSpeechModelDownloadLog, LogLevel::Error, __VA_ARGS__)

NS_IMPL_ISUPPORTS(SpeechModelResolver, nsIMLModelResolver)

NS_IMETHODIMP
SpeechModelResolver::Resolve(const nsACString& aId, nsACString& aEngine,
                             nsACString& aModel, nsACString& aRevision,
                             nsACString& aFilename) {
  SpeechModelIdentifier identifier;
  if (!ResolveSpeechModelId(aId, identifier)) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  aEngine.Assign(kSpeechRecognitionEngineId);
  aModel.Assign(identifier.mModelName);
  aRevision.Assign(identifier.mRevision);
  aFilename.Assign(identifier.mFileName);
  return NS_OK;
}

// Runs aCallback with whether aModel is already downloaded to the local cache.
// Must run on the parent main thread. Never rejects (false on any failure).
// The testing mock is not consulted here: it lives in HWInferenceParent, which
// short-circuits the already-installed case before this is reached.
static void CheckInstalled(const SpeechModelIdentifier& aModel,
                           std::function<void(bool)>&& aCallback) {
  nsCOMPtr<nsIMLModelHub> hub = do_GetService("@mozilla.org/ml-modelhub;1");
  if (!hub) {
    LOGE("CheckInstalled - failed to get ModelHub service");
    aCallback(false);
    return;
  }

  RefPtr<Promise> promise;
  nsresult rv = hub->IsModelInstalled(
      kSpeechRecognitionEngineId, aModel.mModelName, aModel.mRevision,
      aModel.mFileName, getter_AddRefs(promise));
  if (NS_FAILED(rv) || !promise) {
    LOGE("CheckInstalled - IsModelInstalled call failed rv={:x}",
         static_cast<uint32_t>(rv));
    aCallback(false);
    return;
  }

  promise->AddCallbacksWithCycleCollectedArgs(
      [aCallback](JSContext*, JS::Handle<JS::Value> aValue, ErrorResult&) {
        aCallback(JS::ToBoolean(aValue));
      },
      [aCallback](JSContext*, JS::Handle<JS::Value>, ErrorResult&) {
        aCallback(false);
      });
}

NS_IMETHODIMP
SpeechModelResolver::AuthorizeDownload(
    const nsACString& aModel, const nsACString& aRevision,
    const nsACString& aFilename, WindowGlobalParent* aWindow,
    const nsAString& aProgressToken,
    nsIMLModelDownloadAuthorizationCallback* aCallback) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aCallback);

  // Speech recognition is only ever driven by content, so a request without a
  // document to prompt for has nobody to ask.
  if (!aWindow) {
    LOGE("AuthorizeDownload - no requesting window");
    aCallback->Resolve(false);
    return NS_OK;
  }

  SpeechModelIdentifier model{nsCString(aModel), nsCString(aFilename),
                              nsCString(aRevision),
                              SpeechModelSizeMB(aModel, aRevision, aFilename)};
  nsString progressToken(aProgressToken);
  LOGD("AuthorizeDownload - model={} sizeMB={}", model.ToString().get(),
       model.mSizeMB);

  RefPtr<CanonicalBrowsingContext> bc = aWindow->GetBrowsingContext();
  nsCOMPtr<nsIMLModelDownloadAuthorizationCallback> callback = aCallback;
  std::function<void(bool)> resolve = [callback](bool aAllow) {
    callback->Resolve(aAllow);
  };

  // Under the testing mock, HWInferenceParent already short-circuited the
  // already-installed case, so go straight to the consent decision instead of
  // querying the on-disk cache.
  if (StaticPrefs::browser_ml_modelHub_testing()) {
    ShowSpeechModelDownloadConsent(model, bc, progressToken,
                                   std::move(resolve));
    return NS_OK;
  }

  // Nothing to download means nothing to consent to.
  CheckInstalled(
      model,
      [model, bc = std::move(bc), progressToken = std::move(progressToken),
       resolve = std::move(resolve)](bool aInstalled) mutable {
        if (aInstalled) {
          resolve(true);
          return;
        }
        ShowSpeechModelDownloadConsent(model, bc, progressToken,
                                       std::move(resolve));
      });
  return NS_OK;
}

#undef LOGD
#undef LOGE

}  // namespace mozilla::dom
