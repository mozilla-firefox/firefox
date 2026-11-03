/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SpeechModelDownloadPermissionRequest.h"

#include "mozilla/PermissionDelegateHandler.h"
#include "mozilla/Preferences.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "nsContentPermissionHelper.h"
#include "nsPrintfCString.h"

namespace mozilla::dom {

// The parent-process permission doorhanger for a model download. Reuses the
// existing "speech-recognition-model-download" permission type and its
// PermissionUI.sys.mjs prompt. On Allow it resolves the authorization with
// true (the actual download is performed by HWInferenceParent); on Cancel with
// false.
class SpeechModelDownloadPermissionRequest final
    : public ContentPermissionRequestBaseNoWindow {
 public:
  NS_DECL_ISUPPORTS

  SpeechModelDownloadPermissionRequest(nsIPrincipal* aPrincipal,
                                       nsIPrincipal* aTopLevelPrincipal,
                                       Element* aElement,
                                       const SpeechModelIdentifier& aModel,
                                       const nsAString& aProgressToken,
                                       std::function<void(bool)>&& aResolver)
      : ContentPermissionRequestBaseNoWindow(
            aPrincipal, aTopLevelPrincipal,
            "speech-recognition-model-download"_ns,
            // The prompt does not set requiresUserInput, so this is never
            // consulted. Computing it from a parent-side WindowGlobalParent
            // would assert IsInProcess() and crash, so simply report false.
            false, false),
        mElement(aElement),
        mModel(aModel),
        mProgressToken(aProgressToken),
        mResolver(std::move(aResolver)) {}

  NS_IMETHOD GetElement(mozilla::dom::Element** aElement) override;
  void GetPermissionOptions(nsTArray<nsString>& aOptions) const override;
  NS_IMETHOD Cancel() override;
  NS_IMETHOD Allow(JS::Handle<JS::Value> choices) override;

  // Settles the pending install(). Safe to call more than once; only the first
  // call runs the resolver. Not the virtual Cancel(), so it can be called from
  // non-MOZ_CAN_RUN_SCRIPT contexts (e.g. a failed AskPermission).
  void Resolve(bool aSuccess) {
    if (mResolver) {
      auto resolver = std::move(mResolver);
      mResolver = nullptr;
      resolver(aSuccess);
    }
  }

 private:
  ~SpeechModelDownloadPermissionRequest() { Resolve(false); }

  RefPtr<Element> mElement;
  SpeechModelIdentifier mModel;
  nsString mProgressToken;
  std::function<void(bool)> mResolver;
};

NS_IMPL_ISUPPORTS(SpeechModelDownloadPermissionRequest,
                  nsIContentPermissionRequest)

void SpeechModelDownloadPermissionRequest::GetPermissionOptions(
    nsTArray<nsString>& aOptions) const {
  aOptions.AppendElement(
      NS_ConvertUTF8toUTF16(nsPrintfCString("%u", mModel.mSizeMB)));
  aOptions.AppendElement(mProgressToken);
}

NS_IMETHODIMP
SpeechModelDownloadPermissionRequest::GetElement(Element** aElement) {
  NS_IF_ADDREF(*aElement = mElement);
  return NS_OK;
}

NS_IMETHODIMP
SpeechModelDownloadPermissionRequest::Cancel() {
  Resolve(false);
  return NS_OK;
}

NS_IMETHODIMP
SpeechModelDownloadPermissionRequest::Allow(JS::Handle<JS::Value> aChoices) {
  Resolve(true);
  return NS_OK;
}

void ShowSpeechModelDownloadConsent(const SpeechModelIdentifier& aModel,
                                    CanonicalBrowsingContext* aBrowsingContext,
                                    const nsString& aProgressToken,
                                    std::function<void(bool)>&& aResolver) {
  // Testing shortcut: decide from media.navigator.permission.disabled without
  // showing UI.
  if (Preferences::GetBool(
          "media.webspeech.recognition.model-download.prompt.testing", false)) {
    aResolver(
        Preferences::GetBool("media.navigator.permission.disabled", false));
    return;
  }

  WindowGlobalParent* wgp = aBrowsingContext->GetCurrentWindowGlobal();
  CanonicalBrowsingContext* top = aBrowsingContext->Top();
  WindowGlobalParent* topWgp = top->GetCurrentWindowGlobal();
  RefPtr<Element> element = top->GetEmbedderElement();
  if (!wgp || !element) {
    aResolver(false);
    return;
  }
  nsCOMPtr<nsIPrincipal> principal = wgp->DocumentPrincipal();
  nsCOMPtr<nsIPrincipal> topPrincipal =
      topWgp ? topWgp->DocumentPrincipal() : principal.get();
  if (!principal) {
    aResolver(false);
    return;
  }

  auto request = MakeRefPtr<SpeechModelDownloadPermissionRequest>(
      principal, topPrincipal, element, aModel, aProgressToken,
      std::move(aResolver));
  if (NS_FAILED(nsContentPermissionUtils::AskPermission(request, nullptr))) {
    request->Resolve(false);
  }
}

}  // namespace mozilla::dom
