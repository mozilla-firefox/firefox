/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Fetch.h"
#include "mozilla/dom/IdentityCredential.h"
#include "mozilla/dom/IdentityNetworkHelpers.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/Promise-inl.h"
#include "mozilla/dom/Request.h"
#include "mozilla/dom/WindowGlobalChild.h"
#include "mozilla/Components.h"
#include "mozilla/CredentialChosenCallback.h"
#include "mozilla/ExpandedPrincipal.h"
#include "mozilla/IdentityCredentialRequestManager.h"
#include "mozilla/NullPrincipal.h"
#include "nsICredentialChooserService.h"
#include "nsIEffectiveTLDService.h"
#include "nsIGlobalObject.h"
#include "nsIIdentityCredentialPromptService.h"
#include "nsIIdentityCredentialStorageService.h"
#include "nsIPermissionManager.h"
#include "nsITimer.h"
#include "nsIXPConnect.h"
#include "nsNetUtil.h"
#include "nsString.h"
#include "nsStringStream.h"
#include "nsTArray.h"
#include "nsURLHelper.h"

#include <utility>

namespace mozilla::dom {

IdentityCredential::~IdentityCredential() = default;

JSObject* IdentityCredential::WrapObject(JSContext* aCx,
                                         JS::Handle<JSObject*> aGivenProto) {
  return IdentityCredential_Binding::Wrap(aCx, this, aGivenProto);
}

IdentityCredential::IdentityCredential(nsPIDOMWindowInner* aParent)
    : Credential(aParent) {
  if (aParent && aParent->GetBrowsingContext() &&
      aParent->GetBrowsingContext()->Top() &&
      aParent->GetBrowsingContext()->Top()->GetDocument()) {
    this->mIdentityProvider =
        aParent->GetBrowsingContext()->Top()->GetDocument()->GetPrincipal();
  }
}

IdentityCredential::IdentityCredential(nsPIDOMWindowInner* aParent,
                                       const IPCIdentityCredential& aOther)
    : Credential(aParent) {
  CopyValuesFrom(aOther);
}

void IdentityCredential::CopyValuesFrom(const IPCIdentityCredential& aOther) {
  this->SetId(aOther.id());
  this->SetType(u"identity"_ns);
  IdentityCredentialInit creationOptions;
  if (aOther.token().isSome()) {
    this->mToken = aOther.token().value();
    creationOptions.mToken.Construct(
        NS_ConvertUTF16toUTF8(aOther.token().value()));
  }
  if (aOther.effectiveQueryURL().isSome()) {
    creationOptions.mEffectiveQueryURL.Construct(
        aOther.effectiveQueryURL().value());
  }
  if (aOther.effectiveOrigins().Length() > 0) {
    creationOptions.mEffectiveOrigins.Construct(
        Sequence(aOther.effectiveOrigins().Clone()));
  }
  if (aOther.effectiveType().isSome()) {
    creationOptions.mEffectiveType.Construct(aOther.effectiveType().value());
  }
  creationOptions.mId = aOther.id();
  IdentityCredentialUserData userData;
  if (aOther.name().isSome()) {
    userData.mName = aOther.name()->Data();
  }
  if (aOther.iconURL().isSome()) {
    userData.mIconURL = aOther.iconURL()->Data();
  }
  if (aOther.infoExpiresAt().isSome()) {
    int64_t now = PR_Now() / PR_USEC_PER_MSEC;
    uint64_t difference = 0;
    if (static_cast<uint64_t>(now) < aOther.infoExpiresAt().value()) {
      difference = aOther.infoExpiresAt().value() - static_cast<uint64_t>(now);
    }
    userData.mExpiresAfter.Construct(difference);
  }
  if (aOther.name().isSome() || aOther.iconURL().isSome() ||
      aOther.infoExpiresAt().isSome()) {
    creationOptions.mUiHint.Construct(userData);
  }
  this->mCreationOptions = Some(creationOptions);
  this->mIdentityProvider = aOther.identityProvider();
}

IPCIdentityCredential IdentityCredential::MakeIPCIdentityCredential() const {
  IPCIdentityCredential result;
  result.identityProvider() = mIdentityProvider;
  this->GetId(result.id());
  if (this->mCreationOptions.isSome()) {
    if (this->mCreationOptions->mEffectiveQueryURL.WasPassed()) {
      result.effectiveQueryURL() =
          Some(this->mCreationOptions->mEffectiveQueryURL.Value());
    }
    if (this->mCreationOptions->mEffectiveOrigins.WasPassed()) {
      result.effectiveOrigins() =
          this->mCreationOptions->mEffectiveOrigins.Value();
    }
    if (this->mCreationOptions->mEffectiveType.WasPassed()) {
      result.effectiveType() =
          Some(this->mCreationOptions->mEffectiveType.Value());
    }
    if (this->mCreationOptions->mUiHint.WasPassed() &&
        !this->mCreationOptions->mUiHint.Value().mIconURL.IsEmpty()) {
      result.iconURL() = Some(this->mCreationOptions->mUiHint.Value().mIconURL);
    }
    if (this->mCreationOptions->mUiHint.WasPassed() &&
        !this->mCreationOptions->mUiHint.Value().mName.IsEmpty()) {
      result.name() = Some(this->mCreationOptions->mUiHint.Value().mName);
    }
    if (this->mCreationOptions->mUiHint.WasPassed() &&
        this->mCreationOptions->mUiHint.Value().mExpiresAfter.WasPassed()) {
      result.infoExpiresAt() =
          Some(PR_Now() / PR_USEC_PER_MSEC +
               this->mCreationOptions->mUiHint.Value().mExpiresAfter.Value());
    }
    if (this->mCreationOptions->mToken.WasPassed()) {
      result.token() =
          Some(NS_ConvertUTF8toUTF16(this->mCreationOptions->mToken.Value()));
    }
  }
  if (!this->mToken.IsEmpty()) {
    result.token() = Some(this->mToken);
  }

  return result;
}

// static
already_AddRefed<IdentityCredential> IdentityCredential::Constructor(
    const GlobalObject& aGlobal, const IdentityCredentialInit& aInit,
    ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  if (!global || !global->GetAsInnerWindow() || !global->PrincipalOrNull()) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }
  RefPtr<IdentityCredential> result =
      new IdentityCredential(global->GetAsInnerWindow());
  result->SetId(aInit.mId);
  result->SetType(u"identity"_ns);
  result->mCreationOptions.emplace(aInit);
  result->mIdentityProvider = global->PrincipalOrNull();
  if (aInit.mToken.WasPassed()) {
    result->mToken = NS_ConvertUTF8toUTF16(aInit.mToken.Value());
  }
  return result.forget();
}

void IdentityCredential::GetToken(nsAString& aToken) const {
  aToken.Assign(mToken);
}
void IdentityCredential::SetToken(const nsAString& aToken) {
  mToken.Assign(aToken);
  if (mCreationOptions.isSome()) {
    mCreationOptions->mToken.Construct(NS_ConvertUTF16toUTF8(aToken));
  }
}

void IdentityCredential::GetOrigin(nsACString& aOrigin,
                                   ErrorResult& aError) const {
  nsresult rv = mIdentityProvider->GetWebExposedOriginSerialization(aOrigin);
  if (NS_FAILED(rv)) {
    aOrigin.SetLength(0);
    aError.Throw(rv);
  }
}

// static
void IdentityCredential::GetCredential(nsPIDOMWindowInner* aParent,
                                       const CredentialRequestOptions& aOptions,
                                       bool aSameOriginWithAncestors,
                                       const RefPtr<Promise>& aPromise) {
  MOZ_ASSERT(XRE_IsContentProcess());
  MOZ_ASSERT(aParent);
  MOZ_ASSERT(aPromise);
  MOZ_ASSERT(aOptions.mIdentity.WasPassed());
  // Prevent origin confusion by requiring no cross domain iframes
  // in this one's ancestry
  if (!aSameOriginWithAncestors) {
    aPromise->MaybeRejectWithNotAllowedError("Same origin ancestors only.");
    return;
  }

  RefPtr<WindowGlobalChild> wgc = aParent->GetWindowGlobalChild();
  MOZ_ASSERT(wgc);
  RefPtr<nsPIDOMWindowInner> parent(aParent);
  wgc->SendGetIdentityCredential(aOptions.mIdentity.Value(),
                                 aOptions.mMediation)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [aPromise,
           parent](const WindowGlobalChild::GetIdentityCredentialPromise::
                       ResolveValueType& aResult) {
            Maybe<IPCIdentityCredential> maybeResult;
            nsresult rv;
            std::tie(maybeResult, rv) = aResult;
            if (NS_WARN_IF(NS_FAILED(rv))) {
              aPromise->MaybeRejectWithAbortError(
                  "Credential get aborted with internal error");
              return;
            }
            if (maybeResult.isNothing()) {
              aPromise->MaybeResolve(JS::NullHandleValue);
              return;
            }
            aPromise->MaybeResolve(
                new IdentityCredential(parent, maybeResult.value()));
          },
          [aPromise](const WindowGlobalChild::GetIdentityCredentialPromise::
                         RejectValueType& aResult) {
            aPromise->MaybeRejectWithAbortError(
                "Credential get aborted with internal error");
          });
}

nsresult IdentityCredential::CanSilentlyCollect(nsIPrincipal* aPrincipal,
                                                nsIPrincipal* aIDPPrincipal,
                                                bool* aResult) {
  NS_ENSURE_ARG_POINTER(aPrincipal);
  NS_ENSURE_ARG_POINTER(aIDPPrincipal);
  nsCString origin;
  nsresult rv = aIDPPrincipal->GetOrigin(origin);
  NS_ENSURE_SUCCESS(rv, rv);

  uint32_t permit = nsIPermissionManager::UNKNOWN_ACTION;
  nsCOMPtr<nsIPermissionManager> permissionManager =
      components::PermissionManager::Service();
  if (!permissionManager) {
    return NS_ERROR_SERVICE_NOT_AVAILABLE;
  }

  rv = permissionManager->TestPermissionFromPrincipal(
      aPrincipal, "credential-allow-silent-access^"_ns + origin, &permit);
  NS_ENSURE_SUCCESS(rv, rv);
  *aResult = (permit == nsIPermissionManager::ALLOW_ACTION);
  if (!*aResult) {
    return NS_OK;
  }
  rv = permissionManager->TestPermissionFromPrincipal(
      aPrincipal, "credential-allow-silent-access"_ns, &permit);
  NS_ENSURE_SUCCESS(rv, rv);
  *aResult = permit == nsIPermissionManager::ALLOW_ACTION;
  return NS_OK;
}

// static
RefPtr<IdentityCredential::GetIPCIdentityCredentialPromise>
IdentityCredential::GetCredentialInMainProcess(
    nsIPrincipal* aPrincipal, CanonicalBrowsingContext* aBrowsingContext,
    const IdentityCredentialRequestOptions& aOptions,
    const CredentialMediationRequirement& aMediationRequirement) {
  RefPtr<nsIPrincipal> principal = aPrincipal;
  RefPtr<CanonicalBrowsingContext> cbc = aBrowsingContext;
  RefPtr<IdentityCredential::GetIPCIdentityCredentialPromise::Private> result =
      new IdentityCredential::GetIPCIdentityCredentialPromise::Private(
          __func__);
  if (StaticPrefs::
          dom_security_credentialmanagement_identity_lightweight_enabled()) {
    // First try to collect credentials from local storage
    CollectFromCredentialStoreInMainProcess(aPrincipal, aBrowsingContext,
                                            aOptions)
        ->Then(
            GetCurrentSerialEventTarget(), __func__,
            [aOptions, aMediationRequirement, cbc, principal,
             result](const nsTArray<IPCIdentityCredential>& aResult) {
              // If collected one credential and the request permit it,
              // see if we can silently resolve
              if (aResult.Length() == 1 &&
                  (aMediationRequirement !=
                       CredentialMediationRequirement::Required &&
                   aMediationRequirement !=
                       CredentialMediationRequirement::Conditional)) {
                const IPCIdentityCredential& silentCandidate =
                    aResult.ElementAt(0);
                bool permitted;
                nsresult rv = CanSilentlyCollect(
                    principal, silentCandidate.identityProvider(), &permitted);
                if (NS_SUCCEEDED(rv) && permitted) {
                  result->Resolve(silentCandidate, __func__);
                  return;
                }
              }

              // The only way to get a credential from here is not silent,
              // so we must bail out here.
              if (aMediationRequirement ==
                  CredentialMediationRequirement::Silent) {
                result->Reject(NS_OK, __func__);
                return;
              }

              // If we have no collectable credentials, discover a remote
              // credential
              if (aResult.Length() == 0) {
                DiscoverFromExternalSourceInMainProcess(principal, cbc,
                                                        aOptions)
                    ->Then(
                        GetCurrentSerialEventTarget(), __func__,
                        [result](const IPCIdentityCredential& credential) {
                          result->Resolve(credential, __func__);
                        },
                        [result](nsresult rv) {
                          // This can be an NS_OK if discovery didn't fail,
                          // but we didn't get a result.
                          result->Reject(rv, __func__);
                        });
                return;
              }

              // Show the credential chooser, and when the callback fires,
              // forward the result onto the `result` local variable that
              // we are returning from this function.
              RefPtr<CredentialChosenCallback> callback =
                  new CredentialChosenCallback(aResult, result);
              nsresult rv = ShowCredentialChooser(cbc, aResult, callback);
              // If showing the chooser failed, we reject here since the
              // callback won't fire.
              if (NS_FAILED(rv)) {
                result->Reject(rv, __func__);
              }
            },
            [result](nsresult aErr) { result->Reject(aErr, __func__); });
  } else {
    // If we don't have lightweight credentials enabled, just fire discovery
    // off.
    DiscoverFromExternalSourceInMainProcess(principal, cbc, aOptions)
        ->Then(
            GetCurrentSerialEventTarget(), __func__,
            [result](const IPCIdentityCredential& credential) {
              result->Resolve(credential, __func__);
            },
            [result](nsresult rv) { result->Reject(rv, __func__); });
  }
  return result.forget();
}

// static
RefPtr<GenericPromise> IdentityCredential::AllowedToCollectCredential(
    nsIPrincipal* aPrincipal, CanonicalBrowsingContext* aBrowsingContext,
    const IdentityCredentialRequestOptions& aOptions,
    IPCIdentityCredential aCredential) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(aPrincipal);
  for (const nsCString& origin : aCredential.effectiveOrigins()) {
    nsCOMPtr<nsIURI> allowURI;
    nsresult rv = NS_NewURI(getter_AddRefs(allowURI), origin);
    if (NS_SUCCEEDED(rv)) {
      if (aPrincipal->IsSameOrigin(allowURI)) {
        return GenericPromise::CreateAndResolve(true, __func__);
      }
    }
  }
  if (aCredential.effectiveType().isSome() && aOptions.mProviders.WasPassed()) {
    for (const auto& provider : aOptions.mProviders.Value()) {
      if (provider.mEffectiveType.WasPassed() &&
          provider.mEffectiveType.Value() ==
              aCredential.effectiveType().value()) {
        return GenericPromise::CreateAndResolve(true, __func__);
      }
    }
  }
  if (aCredential.effectiveQueryURL().isSome()) {
    // Make the url to test, returning the default resolved to false promise if
    // it fails
    nsCOMPtr<nsIURI> dynamicURI;
    nsresult rv = NS_NewURI(getter_AddRefs(dynamicURI),
                            aCredential.effectiveQueryURL().value());
    if (NS_SUCCEEDED(rv) && aOptions.mProviders.WasPassed()) {
      // at this point we need to run through the providers passed as an
      // argument to "navigator.credentials.get" and see if any meet the
      // conditions to effectively query for aCredential based on an HTTP+CORS
      // endpoint. This gives the IDP control over what origins can use each
      // credential they store.
      for (const auto& provider : aOptions.mProviders.Value()) {
        // We only issue requests if the provider provided by the RP has the
        // same URL as the credential that was stored by the IDP.
        if (!provider.mEffectiveQueryURL.WasPassed() ||
            !provider.mEffectiveQueryURL.Value().Equals(
                aCredential.effectiveQueryURL().value())) {
          continue;
        }

        // We are going to do some work here, so build the promise to return
        RefPtr<GenericPromise::Private> resultPromise =
            new GenericPromise::Private(__func__);
        // Create the JS global for use by the fetch, tied to the RP principal
        nsIXPConnect* xpc = nsContentUtils::XPConnect();
        MOZ_ASSERT(xpc, "This should never be null!");
        nsCOMPtr<nsIGlobalObject> global;
        AutoJSAPI jsapi;
        jsapi.Init();
        JSContext* cx = jsapi.cx();
        JS::Rooted<JSObject*> sandbox(cx);
        rv = xpc->CreateSandbox(cx, aPrincipal, sandbox.address());
        if (NS_WARN_IF(NS_FAILED(rv))) {
          resultPromise->Reject(rv, __func__);
          return resultPromise;
        }
        MOZ_ASSERT(JS_IsGlobalObject(sandbox));
        global = xpc::NativeGlobal(sandbox);
        if (NS_WARN_IF(!global)) {
          resultPromise->Reject(NS_ERROR_FAILURE, __func__);
          return resultPromise;
        }
        // Construct the HTTP+CORS request to be made.
        constexpr auto fragment = ""_ns;
        auto internalRequest = MakeSafeRefPtr<InternalRequest>(
            provider.mEffectiveQueryURL.Value(), fragment);
        internalRequest->SetCredentialsMode(RequestCredentials::Omit);
        internalRequest->SetMode(RequestMode::Cors);
        internalRequest->OverrideContentPolicyType(
            nsContentPolicyType::TYPE_WEB_IDENTITY);
        internalRequest->SetHeaders(
            new InternalHeaders(HeadersGuardEnum::Request));
        RefPtr<Request> request =
            new Request(global, std::move(internalRequest), nullptr);
        RequestOrUTF8String fetchInput;
        fetchInput.SetAsRequest() = request;
        RootedDictionary<RequestInit> requestInit(RootingCx());
        ErrorResult error;
        // Issue the fetch and define the callbacks
        RefPtr<Promise> fetchPromise =
            FetchRequest(request->GetParentObject(), fetchInput, requestInit,
                         CallerType::System, error);
        if (NS_WARN_IF(error.Failed())) {
          resultPromise->Reject(error.StealNSResult(), __func__);
          return resultPromise;
        }
        fetchPromise->AddCallbacksWithCycleCollectedArgs(
            [resultPromise](JSContext* aCx, JS::Handle<JS::Value> aValue,
                            ErrorResult&) {
              // Get the Response object from the argument to the callback
              if (NS_WARN_IF(!aValue.isObject())) {
                resultPromise->Reject(NS_ERROR_FAILURE, __func__);
                return;
              }
              JS::Rooted<JSObject*> obj(aCx, &aValue.toObject());
              MOZ_ASSERT(obj);
              Response* response = nullptr;
              if (NS_WARN_IF(
                      NS_FAILED(UNWRAP_OBJECT(Response, &obj, response)))) {
                resultPromise->Reject(NS_ERROR_FAILURE, __func__);
                return;
              }
              // Resolve whether or not the request was a success
              resultPromise->Resolve(response->Ok(), __func__);
            },
            [resultPromise](JSContext*, JS::Handle<JS::Value> aValue,
                            ErrorResult&) {
              resultPromise->Reject(
                  Promise::TryExtractNSResultFromRejectionValue(aValue),
                  __func__);
            });
        return resultPromise;
      }
    }
  }

  return GenericPromise::CreateAndResolve(false, __func__);
}

// static
RefPtr<IdentityCredential::GetIPCIdentityCredentialsPromise>
IdentityCredential::CollectFromCredentialStoreInMainProcess(
    nsIPrincipal* aPrincipal, CanonicalBrowsingContext* aBrowsingContext,
    const IdentityCredentialRequestOptions& aOptions) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(aPrincipal);
  nsresult rv;
  nsCOMPtr<nsIIdentityCredentialStorageService> icStorageService =
      mozilla::components::IdentityCredentialStorageService::Service(&rv);
  if (NS_WARN_IF(!icStorageService)) {
    return IdentityCredential::GetIPCIdentityCredentialsPromise::
        CreateAndReject(rv, __func__);
  }

  if (!aOptions.mProviders.WasPassed()) {
    return IdentityCredential::GetIPCIdentityCredentialsPromise::
        CreateAndResolve(CopyableTArray<mozilla::dom::IPCIdentityCredential>(),
                         __func__);
  }

  nsTArray<RefPtr<nsIPrincipal>> idpPrincipals;
  for (const auto& idpConfig : aOptions.mProviders.Value()) {
    if (idpConfig.mOrigin.WasPassed()) {
      RefPtr<nsIURI> idpURI;
      rv = NS_NewURI(getter_AddRefs(idpURI), idpConfig.mOrigin.Value());
      if (NS_FAILED(rv)) {
        continue;
      }
      RefPtr<nsIPrincipal> idpPrincipal = BasePrincipal::CreateContentPrincipal(
          idpURI, aPrincipal->OriginAttributesRef());
      idpPrincipals.AppendElement(idpPrincipal);
    } else if (idpConfig.mLoginURL.WasPassed()) {
      RefPtr<nsIURI> idpURI;
      rv = NS_NewURI(getter_AddRefs(idpURI), idpConfig.mLoginURL.Value());
      if (NS_FAILED(rv)) {
        continue;
      }
      RefPtr<nsIPrincipal> idpPrincipal = BasePrincipal::CreateContentPrincipal(
          idpURI, aPrincipal->OriginAttributesRef());
      idpPrincipals.AppendElement(idpPrincipal);
    }
  }

  CopyableTArray<mozilla::dom::IPCIdentityCredential> fromStore;
  rv = icStorageService->GetIdentityCredentials(idpPrincipals, fromStore);
  if (NS_FAILED(rv)) {
    return GetIPCIdentityCredentialsPromise::CreateAndReject(rv, __func__);
  }

  for (const auto& idpConfig : aOptions.mProviders.Value()) {
    if (idpConfig.mEffectiveType.WasPassed() &&
        idpConfig.mEffectiveType.Value() != "") {
      nsTArray<mozilla::dom::IPCIdentityCredential> typeMatches;
      rv = icStorageService->GetIdentityCredentialsOfType(
          idpConfig.mEffectiveType.Value(), typeMatches);
      if (NS_FAILED(rv)) {
        return GetIPCIdentityCredentialsPromise::CreateAndReject(rv, __func__);
      }
      fromStore.AppendElements(std::move(typeMatches));
    }
  }

  RefPtr<GetIPCIdentityCredentialsPromise::Private> resultPromise =
      new GetIPCIdentityCredentialsPromise::Private(__func__);
  nsTArray<RefPtr<GenericPromise>> promises;
  for (const IPCIdentityCredential& cred : fromStore) {
    promises.AppendElement(AllowedToCollectCredential(
        aPrincipal, aBrowsingContext, aOptions, cred));
  }
  GenericPromise::AllSettled(GetCurrentSerialEventTarget(), promises)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [resultPromise, fromStore](
              const GenericPromise::AllSettledPromiseType::ResolveValueType&
                  aResults) {
            if (aResults.Length() != fromStore.Length()) {
              resultPromise->Reject(NS_ERROR_FAILURE, __func__);
              return;
            }
            CopyableTArray<mozilla::dom::IPCIdentityCredential> result;
            for (size_t i = 0; i < aResults.Length(); i++) {
              if (aResults.ElementAt(i).IsResolve() &&
                  aResults.ElementAt(i).ResolveValue()) {
                result.AppendElement(fromStore.ElementAt(i));
              }
            }
            resultPromise->Resolve(result, __func__);
          },
          [resultPromise](
              const GenericPromise::AllSettledPromiseType::RejectValueType&
                  aResult) {
            resultPromise->Reject(NS_ERROR_FAILURE, __func__);
          });

  return resultPromise;
}

// Helper function to call the CredentialChooserService,
// fetching icons into a data URL. We could do this natively,
// but it is much easier to do in Javascript and this isn't
// performance critical.
RefPtr<MozPromise<nsCString, nsresult, true>> fetchIconURLHelper(
    nsPIDOMWindowInner* aParent, const nsCString& aSpec) {
  RefPtr<MozPromise<nsCString, nsresult, true>::Private> result =
      new MozPromise<nsCString, nsresult, true>::Private(__func__);
  nsresult rv;
  nsCOMPtr<nsICredentialChooserService> ccService =
      mozilla::components::CredentialChooserService::Service(&rv);
  if (NS_FAILED(rv) || !ccService) {
    result->Reject(rv, __func__);
    return result;
  }

  nsCOMPtr<nsIURI> iconURI;
  rv = NS_NewURI(getter_AddRefs(iconURI), aSpec);
  if (NS_FAILED(rv)) {
    result->Reject(rv, __func__);
    return result;
  }

  RefPtr<Promise> serviceResult;
  rv = ccService->FetchImageToDataURI(aParent, iconURI,
                                      getter_AddRefs(serviceResult));
  if (NS_FAILED(rv)) {
    result->Reject(rv, __func__);
    return result;
  }
  serviceResult->AddCallbacksWithCycleCollectedArgs(
      [result](JSContext* aCx, JS::Handle<JS::Value> aValue, ErrorResult&) {
        if (!aValue.get().isString()) {
          result->Reject(NS_ERROR_FAILURE, __func__);
          return;
        }
        nsAutoCString value;
        if (!AssignJSString(aCx, value, aValue.get().toString())) {
          result->Reject(NS_ERROR_FAILURE, __func__);
          return;
        }
        result->Resolve(value, __func__);
      },
      [result](JSContext* aCx, JS::Handle<JS::Value> aValue, ErrorResult&) {
        result->Reject(Promise::TryExtractNSResultFromRejectionValue(aValue),
                       __func__);
      });
  return result;
}

// static
RefPtr<GenericPromise> IdentityCredential::Store(
    nsPIDOMWindowInner* aParent, const IdentityCredential* aCredential,
    bool aSameOriginWithAncestors) {
  MOZ_ASSERT(XRE_IsContentProcess());
  MOZ_ASSERT(aParent);
  MOZ_ASSERT(aCredential);
  // Prevent origin confusion by requiring no cross domain iframes
  // in this one's ancestry
  if (!aSameOriginWithAncestors) {
    return GenericPromise::CreateAndReject(NS_ERROR_DOM_NOT_ALLOWED_ERR,
                                           __func__);
  }

  // Request the icon data while we are still in the content process so we can
  // use our window's JS global
  RefPtr<MozPromise<nsCString, nsresult, true>> iconFetch;
  if (aCredential->mCreationOptions.isSome() &&
      aCredential->mCreationOptions->mUiHint.WasPassed() &&
      !aCredential->mCreationOptions->mUiHint.Value().mIconURL.IsEmpty()) {
    iconFetch = fetchIconURLHelper(
        aParent, aCredential->mCreationOptions->mUiHint.Value().mIconURL);
  } else {
    iconFetch = MozPromise<nsCString, nsresult, true>::CreateAndReject(
        NS_ERROR_INVALID_ARG, __func__);
  }
  // First fetch the icon, then send the data we have to the main process
  IPCIdentityCredential sendCredential =
      aCredential->MakeIPCIdentityCredential();
  RefPtr<WindowGlobalChild> wgc = aParent->GetWindowGlobalChild();
  MOZ_ASSERT(wgc);
  return iconFetch
      ->Then(GetCurrentSerialEventTarget(), __func__,
             [sendCredential,
              wgc](MozPromise<nsCString, nsresult, true>::ResolveOrRejectValue&&
                       aValue) mutable {
               // If it was a resolution, then we can overwrite our icon data
               if (aValue.IsResolve()) {
                 sendCredential.iconURL() = Some(aValue.ResolveValue());
               }

               // Kick the request off to the main process and translate the
               // result to the expected type when we get a result.
               return wgc->SendStoreIdentityCredential(sendCredential);
             })
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [](const WindowGlobalChild::StoreIdentityCredentialPromise::
                 ResolveValueType& aResult) {
            return GenericPromise::CreateAndResolve(true, __func__);
          },
          [](const WindowGlobalChild::StoreIdentityCredentialPromise::
                 RejectValueType& aResult) {
            return GenericPromise::CreateAndReject(NS_ERROR_DOM_UNKNOWN_ERR,
                                                   __func__);
          });
}

// static
RefPtr<GenericPromise> IdentityCredential::StoreInMainProcess(
    nsIPrincipal* aPrincipal, const IPCIdentityCredential& aCredential) {
  if (!aCredential.identityProvider() ||
      !aCredential.identityProvider()->Equals(aPrincipal)) {
    return GenericPromise::CreateAndReject(nsresult::NS_ERROR_FAILURE,
                                           __func__);
  }
  nsresult error;
  nsCOMPtr<nsIIdentityCredentialStorageService> icStorageService =
      mozilla::components::IdentityCredentialStorageService::Service(&error);
  if (NS_WARN_IF(!icStorageService)) {
    return GenericPromise::CreateAndReject(error, __func__);
  }
  error = icStorageService->StoreIdentityCredential(aCredential);
  if (NS_FAILED(error)) {
    return GenericPromise::CreateAndReject(error, __func__);
  }

  IdentityCredentialRequestManager* icrm =
      IdentityCredentialRequestManager::GetInstance();
  if (!icrm) {
    return GenericPromise::CreateAndReject(NS_ERROR_NOT_AVAILABLE, __func__);
  }
  icrm->NotifyOfStoredCredential(aCredential.identityProvider(), aCredential);

  return GenericPromise::CreateAndReject(nsresult::NS_ERROR_FAILURE, __func__);
}

// static
RefPtr<IdentityCredential::GetIdentityCredentialPromise>
IdentityCredential::Create(nsPIDOMWindowInner* aParent,
                           const CredentialCreationOptions& aOptions,
                           bool aSameOriginWithAncestors) {
  MOZ_ASSERT(aOptions.mIdentity.WasPassed());
  MOZ_ASSERT(aParent);
  const IdentityCredentialInit& init = aOptions.mIdentity.Value();
  RefPtr<IdentityCredential> result = new IdentityCredential(aParent);
  result->SetId(init.mId);
  result->SetType(u"identity"_ns);
  result->mCreationOptions.emplace(init);
  if (init.mToken.WasPassed()) {
    result->mToken = NS_ConvertUTF8toUTF16(init.mToken.Value());
  }
  return GetIdentityCredentialPromise::CreateAndResolve(result.forget(),
                                                        __func__);
}

// Helper function to navigate to the identity provider's login page, based
// on the contents of the config for the identity provider provided in
// navigator.credentials.get. Returns the result of a outer window Open.
nsresult OpenIdentityProviderDialog(
    const RefPtr<WindowGlobalChild>& aWgc,
    const IdentityProviderConfig& aProviderConfig) {
  MOZ_ASSERT(aProviderConfig.mLoginURL.WasPassed());
  AutoJSAPI jsapi;
  MOZ_ASSERT(aWgc);
  if (!jsapi.Init(aWgc->GetWindowGlobal())) {
    return NS_ERROR_FAILURE;
  }
  MOZ_ASSERT(aWgc->WindowContext()->TopWindowContext());
  nsGlobalWindowOuter* outer = nsGlobalWindowOuter::GetOuterWindowWithId(
      aWgc->WindowContext()->TopWindowContext()->OuterWindowId());
  bool popup =
      aProviderConfig.mLoginTarget.WasPassed() &&
      aProviderConfig.mLoginTarget.Value() == IdentityLoginTargetType::Popup;
  RefPtr<BrowsingContext> newBC;
  if (popup) {
    return outer->OpenJS(aProviderConfig.mLoginURL.Value(), u"_blank"_ns,
                         u"popup"_ns, getter_AddRefs(newBC));
  }
  return outer->OpenJS(aProviderConfig.mLoginURL.Value(), u"_top"_ns, u""_ns,
                       getter_AddRefs(newBC));
}

// static
nsresult IdentityCredential::ShowCredentialChooser(
    const RefPtr<CanonicalBrowsingContext>& aContext,
    const nsTArray<IPCIdentityCredential>& aCredentials,
    const RefPtr<nsICredentialChosenCallback>& aCallback) {
  nsresult rv;
  nsCOMPtr<nsICredentialChooserService> ccService =
      mozilla::components::CredentialChooserService::Service(&rv);
  if (NS_WARN_IF(!ccService)) {
    return rv;
  }

  // Build an AutoJSAPI out of the service so we can pass arguments in.
  nsCOMPtr<nsIXPConnectWrappedJS> wrapped = do_QueryInterface(ccService);
  AutoJSAPI jsapi;
  if (NS_WARN_IF(!jsapi.Init(wrapped->GetJSObjectGlobal()))) {
    return NS_ERROR_FAILURE;
  }

  // Build the options for the credential chooser service
  nsTArray<JS::Value> options;
  for (uint32_t index = 0; index < aCredentials.Length(); index++) {
    const IPCIdentityCredential& credential = aCredentials.ElementAt(index);
    JS::Rooted<JSObject*> option(jsapi.cx(), JS_NewPlainObject(jsapi.cx()));
    if (NS_WARN_IF(!option)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
    JS::Rooted<JS::Value> idValue(jsapi.cx());
    if (!xpc::NonVoidStringToJsval(jsapi.cx(), credential.id(), &idValue) ||
        !JS_DefineProperty(jsapi.cx(), option, "id", idValue, 0)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    JS::Rooted<JS::Value> typeValue(jsapi.cx());
    if (!xpc::NonVoidStringToJsval(jsapi.cx(), u"identity"_ns, &typeValue) ||
        !JS_DefineProperty(jsapi.cx(), option, "type", typeValue, 0)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    JS::Rooted<JS::Value> originValue(jsapi.cx());
    nsAutoCString origin;
    credential.identityProvider()->GetWebExposedOriginSerialization(origin);
    if (!xpc::NonVoidStringToJsval(jsapi.cx(), NS_ConvertUTF8toUTF16(origin),
                                   &originValue) ||
        !JS_DefineProperty(jsapi.cx(), option, "origin", originValue, 0)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    // We only put UI Hints on if we have a name and icon.
    if (credential.name().isSome() && credential.iconURL().isSome()) {
      JS::Rooted<JSObject*> uiHint(jsapi.cx(), JS_NewPlainObject(jsapi.cx()));
      if (credential.name().isSome()) {
        JS::Rooted<JS::Value> nameValue(jsapi.cx());
        if (!xpc::NonVoidStringToJsval(
                jsapi.cx(), NS_ConvertUTF8toUTF16(credential.name().value()),
                &nameValue) ||
            !JS_DefineProperty(jsapi.cx(), uiHint, "name", nameValue, 0)) {
          return NS_ERROR_OUT_OF_MEMORY;
        }
      }

      if (credential.iconURL().isSome()) {
        JS::Rooted<JS::Value> iconValue(jsapi.cx());
        if (!xpc::NonVoidStringToJsval(
                jsapi.cx(), NS_ConvertUTF8toUTF16(credential.iconURL().value()),
                &iconValue) ||
            !JS_DefineProperty(jsapi.cx(), uiHint, "iconURL", iconValue, 0)) {
          return NS_ERROR_OUT_OF_MEMORY;
        }
      }

      if (credential.infoExpiresAt().isSome()) {
        int64_t now = PR_Now() / PR_USEC_PER_MSEC;
        // Guarantee "now" isn't before 1970 so we can static cast it.
        if (now < 0) {
          return NS_ERROR_FAILURE;
        }
        // difference of 0 stands for any negative values as well.
        // The UI treats them the same, so no worries.
        uint64_t difference = 0;
        if (static_cast<uint64_t>(now) < credential.infoExpiresAt().value()) {
          difference =
              credential.infoExpiresAt().value() - static_cast<uint64_t>(now);
        }
        JS::Rooted<JS::Value> expireValue(jsapi.cx());
        if (!ToJSValue(jsapi.cx(), difference, &expireValue) ||
            !JS_DefineProperty(jsapi.cx(), uiHint, "expiresAfter", expireValue,
                               0)) {
          return NS_ERROR_OUT_OF_MEMORY;
        }
      }
      if (!JS_DefineProperty(jsapi.cx(), option, "uiHints", uiHint, 0)) {
        return NS_ERROR_OUT_OF_MEMORY;
      }
    }

    JS::Rooted<JS::Value> optionValue(jsapi.cx());
    optionValue.setObject(*option);
    options.AppendElement(optionValue);
  }

  return ccService->ShowCredentialChooser(aContext, options, aCallback);
}

// static
RefPtr<IdentityCredential::GetIPCIdentityCredentialPromise>
IdentityCredential::DiscoverLightweightFromExternalSourceInMainProcess(
    nsIPrincipal* aPrincipal, CanonicalBrowsingContext* aBrowsingContext,
    const IdentityCredentialRequestOptions& aOptions) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(aBrowsingContext);

  RefPtr<IdentityCredential::GetIPCIdentityCredentialPromise::Private>
      icrmResult =
          new IdentityCredential::GetIPCIdentityCredentialPromise::Private(
              __func__);

  nsCOMPtr<nsIPrincipal> principal(aPrincipal);
  RefPtr<CanonicalBrowsingContext> browsingContext(aBrowsingContext);

  // Add request to manager with result,
  IdentityCredentialRequestManager* icrm =
      IdentityCredentialRequestManager::GetInstance();
  if (!icrm) {
    return IdentityCredential::GetIPCIdentityCredentialPromise::CreateAndReject(
        NS_ERROR_NOT_AVAILABLE, __func__);
  }
  nsresult rv = icrm->StorePendingRequest(aPrincipal, aOptions, icrmResult,
                                          aBrowsingContext);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return IdentityCredential::GetIPCIdentityCredentialPromise::CreateAndReject(
        rv, __func__);
  }

  // If possible, tell the content process to perform the navigation that is
  // appropriate. We may be able to do this from the main process, but it is
  // safer to do it from the client process so that we know all appropriate
  // protections are in place.
  if (aBrowsingContext->GetCurrentWindowGlobal()) {
    if (aOptions.mProviders.WasPassed()) {
      IdentityProviderConfig provider(aOptions.mProviders.Value().ElementAt(0));
      IdentityLoginTargetType type = IdentityLoginTargetType::Redirect;
      if (provider.mLoginTarget.WasPassed()) {
        type = provider.mLoginTarget.Value();
      }
      if (provider.mLoginURL.WasPassed()) {
        Unused << aBrowsingContext->GetCurrentWindowGlobal()
                      ->SendNavigateForIdentityCredentialDiscovery(
                          provider.mLoginURL.Value(), type);
      }
    }
  }

  RefPtr<IdentityCredential::GetIPCIdentityCredentialPromise::Private>
      finalResult =
          new IdentityCredential::GetIPCIdentityCredentialPromise::Private(
              __func__);

  // Once an effective credential is stored, this promise resolves.
  icrmResult->Then(
      GetCurrentSerialEventTarget(), __func__,
      [browsingContext, finalResult](const IPCIdentityCredential& credential) {
        // Now we show a credential chooser in the relying party window
        // to get the user consent to use this account.
        // This will resolve the promise that we return from this function.
        nsTArray<IPCIdentityCredential> array;
        array.AppendElement(credential);
        RefPtr<CredentialChosenCallback> callback =
            new CredentialChosenCallback(array, finalResult);
        nsresult rv = ShowCredentialChooser(browsingContext, array, callback);
        if (NS_FAILED(rv)) {
          finalResult->Reject(rv, __func__);
        }
      },
      [finalResult](nsresult rv) { finalResult->Reject(rv, __func__); });
  return finalResult.forget();
}

// static
RefPtr<IdentityCredential::GetIPCIdentityCredentialPromise>
IdentityCredential::DiscoverFromExternalSourceInMainProcess(
    nsIPrincipal* aPrincipal, CanonicalBrowsingContext* aBrowsingContext,
    const IdentityCredentialRequestOptions& aOptions) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(aBrowsingContext);

  // Figure out what type of discovery we must do.
  RequestType requestType = DetermineRequestDiscoveryType(aOptions);

  // If it is lightweight and we have it enabled, perform that discovery.
  if (StaticPrefs::
          dom_security_credentialmanagement_identity_lightweight_enabled() &&
      requestType == LIGHTWEIGHT) {
    return DiscoverLightweightFromExternalSourceInMainProcess(
        aPrincipal, aBrowsingContext, aOptions);
  }

  // If we are not meant to discover anything, bail out with NS_OK.
  if (requestType == NONE) {
    return IdentityCredential::GetIPCIdentityCredentialPromise::CreateAndReject(
        NS_OK, __func__);
  }

  // Make sure we have providers.
  if (!aOptions.mProviders.WasPassed() ||
      aOptions.mProviders.Value().Length() < 1) {
    return IdentityCredential::GetIPCIdentityCredentialPromise::CreateAndReject(
        NS_ERROR_DOM_NOT_ALLOWED_ERR, __func__);
  }

  // The only other type of discovery is heavyweight. Make sure we can do that
  // before proceeding.
  if (!(StaticPrefs::
            dom_security_credentialmanagement_identity_heavyweight_enabled() &&
        requestType == HEAVYWEIGHT)) {
    return IdentityCredential::GetIPCIdentityCredentialPromise::CreateAndReject(
        NS_ERROR_NOT_AVAILABLE, __func__);
  }

  // Now doing heavyweight fedcm discovery
  RefPtr<IdentityCredential::GetIPCIdentityCredentialPromise::Private> result =
      new IdentityCredential::GetIPCIdentityCredentialPromise::Private(
          __func__);

  nsCOMPtr<nsIPrincipal> principal(aPrincipal);
  RefPtr<CanonicalBrowsingContext> browsingContext(aBrowsingContext);

  RefPtr<nsITimer> timeout;
  if (StaticPrefs::
          dom_security_credentialmanagement_identity_reject_delay_enabled()) {
    nsresult rv = NS_NewTimerWithCallback(
        getter_AddRefs(timeout),
        [=](auto) {
          result->Reject(NS_ERROR_DOM_NETWORK_ERR, __func__);
          IdentityCredential::CloseUserInterface(browsingContext);
        },
        StaticPrefs::
            dom_security_credentialmanagement_identity_reject_delay_duration_ms(),
        nsITimer::TYPE_ONE_SHOT, "IdentityCredentialTimeoutCallback");
    if (NS_WARN_IF(NS_FAILED(rv))) {
      result->Reject(NS_ERROR_FAILURE, __func__);
      return result.forget();
    }
  }

  // Construct an array of requests to fetch manifests for every provider.
  // We need this to show their branding information
  nsTArray<RefPtr<GetManifestPromise>> manifestPromises;
  for (const IdentityProviderConfig& provider : aOptions.mProviders.Value()) {
    RefPtr<GetManifestPromise> manifest =
        IdentityCredential::CheckRootManifest(aPrincipal, provider)
            ->Then(
                GetCurrentSerialEventTarget(), __func__,
                [provider, principal](bool valid) {
                  if (valid) {
                    return IdentityCredential::FetchInternalManifest(principal,
                                                                     provider);
                  }
                  return IdentityCredential::GetManifestPromise::
                      CreateAndReject(NS_ERROR_FAILURE, __func__);
                },
                [](nsresult error) {
                  return IdentityCredential::GetManifestPromise::
                      CreateAndReject(error, __func__);
                });
    manifestPromises.AppendElement(manifest);
  }

  // We use AllSettled here so that failures will be included- we use default
  // values there.
  GetManifestPromise::AllSettled(GetCurrentSerialEventTarget(),
                                 manifestPromises)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [browsingContext, aOptions](
              const GetManifestPromise::AllSettledPromiseType::ResolveValueType&
                  aResults) {
            // Convert the
            // GetManifestPromise::AllSettledPromiseType::ResolveValueType to a
            // Sequence<MozPromise>
            CopyableTArray<MozPromise<IdentityProviderAPIConfig, nsresult,
                                      true>::ResolveOrRejectValue>
                results = aResults;
            const Sequence<MozPromise<IdentityProviderAPIConfig, nsresult,
                                      true>::ResolveOrRejectValue>
                resultsSequence(std::move(results));
            // The user picks from the providers
            return PromptUserToSelectProvider(
                browsingContext, aOptions.mProviders.Value(), resultsSequence);
          },
          [](bool error) {
            return IdentityCredential::
                GetIdentityProviderConfigWithManifestPromise::CreateAndReject(
                    NS_ERROR_FAILURE, __func__);
          })
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [principal, browsingContext](
              const IdentityProviderConfigWithManifest& providerAndManifest) {
            IdentityProviderAPIConfig manifest;
            IdentityProviderConfig provider;
            std::tie(provider, manifest) = providerAndManifest;
            return IdentityCredential::
                CreateHeavyweightCredentialDuringDiscovery(
                    principal, browsingContext, provider, manifest);
          },
          [](nsresult error) {
            return IdentityCredential::GetIPCIdentityCredentialPromise::
                CreateAndReject(error, __func__);
          })
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [result, timeout = std::move(timeout)](
              const IdentityCredential::GetIPCIdentityCredentialPromise::
                  ResolveOrRejectValue&& value) {
            // Resolve the result
            result->ResolveOrReject(value, __func__);

            // Cancel the timer (if it is still pending) and
            // release the hold on the variables leaked into the timer.
            if (timeout &&
                StaticPrefs::
                    dom_security_credentialmanagement_identity_reject_delay_enabled()) {
              timeout->Cancel();
            }
          });

  return result;
}

// static
RefPtr<IdentityCredential::GetIPCIdentityCredentialPromise>
IdentityCredential::CreateHeavyweightCredentialDuringDiscovery(
    nsIPrincipal* aPrincipal, BrowsingContext* aBrowsingContext,
    const IdentityProviderConfig& aProvider,
    const IdentityProviderAPIConfig& aManifest) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(aBrowsingContext);

  nsCOMPtr<nsIPrincipal> argumentPrincipal = aPrincipal;
  RefPtr<BrowsingContext> browsingContext(aBrowsingContext);

  return IdentityCredential::FetchAccountList(argumentPrincipal, aProvider,
                                              aManifest)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [argumentPrincipal, browsingContext, aProvider](
              const std::tuple<IdentityProviderAPIConfig,
                               IdentityProviderAccountList>& promiseResult) {
            IdentityProviderAPIConfig currentManifest;
            IdentityProviderAccountList accountList;
            std::tie(currentManifest, accountList) = promiseResult;
            if (!accountList.mAccounts.WasPassed() ||
                accountList.mAccounts.Value().Length() == 0) {
              return IdentityCredential::GetAccountPromise::CreateAndReject(
                  NS_ERROR_FAILURE, __func__);
            }

            // Remove accounts without a matching login hint if one was provided
            // in the JS call
            if (aProvider.mLoginHint.WasPassed()) {
              const nsCString& loginHint = aProvider.mLoginHint.Value();
              accountList.mAccounts.Value().RemoveElementsBy(
                  [loginHint](const IdentityProviderAccount& account) {
                    if (!account.mLogin_hints.WasPassed() ||
                        account.mLogin_hints.Value().Length() == 0) {
                      return true;
                    }
                    if (account.mLogin_hints.Value().Contains(loginHint)) {
                      return false;
                    }
                    return true;
                  });
            }

            // Remove accounts without a matching domain hint if one was
            // provided in the JS call
            if (aProvider.mDomainHint.WasPassed()) {
              const nsCString& domainHint = aProvider.mDomainHint.Value();
              accountList.mAccounts.Value().RemoveElementsBy(
                  [domainHint](const IdentityProviderAccount& account) {
                    if (!account.mDomain_hints.WasPassed() ||
                        account.mDomain_hints.Value().Length() == 0) {
                      return true;
                    }
                    // The domain hint "any" matches any hint.
                    if (domainHint.Equals("any")) {
                      return false;
                    }
                    if (account.mDomain_hints.Value().Contains(domainHint)) {
                      return false;
                    }
                    return true;
                  });
            }

            // Remove accounts without a matching account hint if a label was
            // provided in the IDP config
            if (currentManifest.mAccount_label.WasPassed()) {
              const nsCString& accountHint =
                  currentManifest.mAccount_label.Value();
              accountList.mAccounts.Value().RemoveElementsBy(
                  [accountHint](const IdentityProviderAccount& account) {
                    if (!account.mLabel_hints.WasPassed() ||
                        account.mLabel_hints.Value().Length() == 0) {
                      return true;
                    }
                    if (account.mLabel_hints.Value().Contains(accountHint)) {
                      return false;
                    }
                    return true;
                  });
            }

            return PromptUserToSelectAccount(browsingContext, accountList,
                                             aProvider, currentManifest);
          },
          [](nsresult error) {
            return IdentityCredential::GetAccountPromise::CreateAndReject(
                error, __func__);
          })
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [argumentPrincipal, browsingContext, aProvider](
              const std::tuple<IdentityProviderAPIConfig,
                               IdentityProviderAccount>& promiseResult) {
            IdentityProviderAPIConfig currentManifest;
            IdentityProviderAccount account;
            std::tie(currentManifest, account) = promiseResult;
            return IdentityCredential::PromptUserWithPolicy(
                browsingContext, argumentPrincipal, account, currentManifest,
                aProvider);
          },
          [](nsresult error) {
            return IdentityCredential::GetAccountPromise::CreateAndReject(
                error, __func__);
          })
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [argumentPrincipal, aProvider](
              const std::tuple<IdentityProviderAPIConfig,
                               IdentityProviderAccount>& promiseResult) {
            IdentityProviderAPIConfig currentManifest;
            IdentityProviderAccount account;
            std::tie(currentManifest, account) = promiseResult;
            return IdentityCredential::FetchToken(argumentPrincipal, aProvider,
                                                  currentManifest, account);
          },
          [](nsresult error) {
            return IdentityCredential::GetTokenPromise::CreateAndReject(
                error, __func__);
          })
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [aProvider](
              const std::tuple<IdentityProviderToken, IdentityProviderAccount>&
                  promiseResult) {
            IdentityProviderToken token;
            IdentityProviderAccount account;
            std::tie(token, account) = promiseResult;
            IPCIdentityCredential credential;
            credential.token() = Some(token.mToken);
            credential.id() = account.mId;
            return IdentityCredential::GetIPCIdentityCredentialPromise::
                CreateAndResolve(credential, __func__);
          },
          [browsingContext](nsresult error) {
            CloseUserInterface(browsingContext);
            return IdentityCredential::GetIPCIdentityCredentialPromise::
                CreateAndReject(error, __func__);
          });
}

// static
RefPtr<IdentityCredential::ValidationPromise>
IdentityCredential::CheckRootManifest(nsIPrincipal* aPrincipal,
                                      const IdentityProviderConfig& aProvider) {
  MOZ_ASSERT(XRE_IsParentProcess());

  if (StaticPrefs::
          dom_security_credentialmanagement_identity_test_ignore_well_known()) {
    return IdentityCredential::ValidationPromise::CreateAndResolve(true,
                                                                   __func__);
  }

  // Build the URL
  nsCString configLocation = aProvider.mConfigURL.Value();
  nsCOMPtr<nsIURI> configURI;
  nsresult rv = NS_NewURI(getter_AddRefs(configURI), configLocation);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return IdentityCredential::ValidationPromise::CreateAndReject(rv, __func__);
  }
  RefPtr<nsIEffectiveTLDService> etld =
      mozilla::components::EffectiveTLD::Service();
  if (!etld) {
    return IdentityCredential::ValidationPromise::CreateAndReject(
        NS_ERROR_SERVICE_NOT_AVAILABLE, __func__);
  }
  nsCString manifestURIString;
  rv = etld->GetSite(configURI, manifestURIString);
  if (NS_FAILED(rv)) {
    return IdentityCredential::ValidationPromise::CreateAndReject(
        NS_ERROR_INVALID_ARG, __func__);
  }
  manifestURIString.AppendLiteral("/.well-known/web-identity");
  nsCOMPtr<nsIURI> manifestURI;
  rv = NS_NewURI(getter_AddRefs(manifestURI), manifestURIString, nullptr);
  if (NS_FAILED(rv)) {
    return IdentityCredential::ValidationPromise::CreateAndReject(
        NS_ERROR_INVALID_ARG, __func__);
  }

  // We actually don't need to do any of this well-known stuff if the
  // requesting principal is same-site to the manifest URI. There is no
  // privacy risk in that case, because the requests could be sent with
  // their unpartitioned cookies anyway.
  if (!aPrincipal->GetIsNullPrincipal()) {
    bool thirdParty = true;
    rv = aPrincipal->IsThirdPartyURI(manifestURI, &thirdParty);
    if (NS_SUCCEEDED(rv) && !thirdParty) {
      return IdentityCredential::ValidationPromise::CreateAndResolve(true,
                                                                     __func__);
    }
  }

  return IdentityNetworkHelpers::FetchWellKnownHelper(manifestURI, aPrincipal)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [aProvider](const IdentityProviderWellKnown& manifest) {
            // Make sure there is only one provider URL
            if (manifest.mProvider_urls.Length() != 1) {
              return IdentityCredential::ValidationPromise::CreateAndResolve(
                  false, __func__);
            }

            // Resolve whether or not that provider URL is the one we were
            // passed as an argument.
            bool correctURL =
                manifest.mProvider_urls[0] == aProvider.mConfigURL.Value();
            return IdentityCredential::ValidationPromise::CreateAndResolve(
                correctURL, __func__);
          },
          [](nsresult error) {
            return IdentityCredential::ValidationPromise::CreateAndReject(
                error, __func__);
          });
}

// static
RefPtr<IdentityCredential::GetManifestPromise>
IdentityCredential::FetchInternalManifest(
    nsIPrincipal* aPrincipal, const IdentityProviderConfig& aProvider) {
  MOZ_ASSERT(XRE_IsParentProcess());
  // Build the URL
  nsCString configLocation = aProvider.mConfigURL.Value();
  nsCOMPtr<nsIURI> manifestURI;
  nsresult rv = NS_NewURI(getter_AddRefs(manifestURI), configLocation, nullptr);
  if (NS_FAILED(rv)) {
    return IdentityCredential::GetManifestPromise::CreateAndReject(
        NS_ERROR_INVALID_ARG, __func__);
  }
  return IdentityNetworkHelpers::FetchConfigHelper(manifestURI, aPrincipal);
}

// static
RefPtr<IdentityCredential::GetAccountListPromise>
IdentityCredential::FetchAccountList(
    nsIPrincipal* aPrincipal, const IdentityProviderConfig& aProvider,
    const IdentityProviderAPIConfig& aManifest) {
  MOZ_ASSERT(XRE_IsParentProcess());
  // Build the URL
  nsCOMPtr<nsIURI> baseURI;
  nsresult rv =
      NS_NewURI(getter_AddRefs(baseURI), aProvider.mConfigURL.Value());
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return IdentityCredential::GetAccountListPromise::CreateAndReject(rv,
                                                                      __func__);
  }
  nsCOMPtr<nsIURI> idpURI;
  rv = NS_NewURI(getter_AddRefs(idpURI), aManifest.mAccounts_endpoint, nullptr,
                 baseURI);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return IdentityCredential::GetAccountListPromise::CreateAndReject(rv,
                                                                      __func__);
  }
  nsCOMPtr<nsIPrincipal> idpPrincipal = BasePrincipal::CreateContentPrincipal(
      idpURI, aPrincipal->OriginAttributesRef());

  return IdentityNetworkHelpers::FetchAccountsHelper(idpURI, idpPrincipal)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [aManifest](const IdentityProviderAccountList& accountList) {
            return IdentityCredential::GetAccountListPromise::CreateAndResolve(
                std::make_tuple(aManifest, accountList), __func__);
          },
          [](nsresult error) {
            return IdentityCredential::GetAccountListPromise::CreateAndReject(
                error, __func__);
          });
}

// static
RefPtr<IdentityCredential::GetTokenPromise> IdentityCredential::FetchToken(
    nsIPrincipal* aPrincipal, const IdentityProviderConfig& aProvider,
    const IdentityProviderAPIConfig& aManifest,
    const IdentityProviderAccount& aAccount) {
  MOZ_ASSERT(XRE_IsParentProcess());
  // Build the URL
  nsCOMPtr<nsIURI> baseURI;
  nsCString baseURIString = aProvider.mConfigURL.Value();
  nsresult rv = NS_NewURI(getter_AddRefs(baseURI), baseURIString);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return IdentityCredential::GetTokenPromise::CreateAndReject(rv, __func__);
  }
  nsCOMPtr<nsIURI> idpURI;
  nsCString tokenSpec = aManifest.mId_assertion_endpoint;
  rv = NS_NewURI(getter_AddRefs(idpURI), tokenSpec.get(), baseURI);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return IdentityCredential::GetTokenPromise::CreateAndReject(rv, __func__);
  }
  nsCString tokenLocation;
  rv = idpURI->GetSpec(tokenLocation);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return IdentityCredential::GetTokenPromise::CreateAndReject(rv, __func__);
  }

  // Create a new request
  URLParams bodyValue;
  bodyValue.Set("account_id"_ns, NS_ConvertUTF16toUTF8(aAccount.mId));
  bodyValue.Set("client_id"_ns, aProvider.mClientId.Value());
  if (aProvider.mNonce.WasPassed()) {
    bodyValue.Set("nonce"_ns, aProvider.mNonce.Value());
  }
  bodyValue.Set("disclosure_text_shown"_ns, "false"_ns);
  bodyValue.Set("is_auto_selected"_ns, "false"_ns);
  nsAutoCString bodyCString;
  bodyValue.Serialize(bodyCString, true);

  return IdentityNetworkHelpers::FetchTokenHelper(idpURI, bodyCString,
                                                  aPrincipal)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [aAccount](const IdentityProviderToken& token) {
            return IdentityCredential::GetTokenPromise::CreateAndResolve(
                std::make_tuple(token, aAccount), __func__);
          },
          [](nsresult error) {
            return IdentityCredential::GetTokenPromise::CreateAndReject(
                error, __func__);
          });
}

// static
already_AddRefed<Promise> IdentityCredential::Disconnect(
    const GlobalObject& aGlobal,
    const IdentityCredentialDisconnectOptions& aOptions, ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  if (!global) {
    aRv.ThrowNotAllowedError("Must be called on an appropriate global object.");
    return nullptr;
  }
  nsPIDOMWindowInner* window = global->GetAsInnerWindow();
  if (!window) {
    aRv.ThrowNotAllowedError("Must be called on a window.");
    return nullptr;
  }
  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (NS_WARN_IF(aRv.Failed() || !promise)) {
    return nullptr;
  }
  RefPtr<WindowGlobalChild> wgc = window->GetWindowGlobalChild();
  MOZ_ASSERT(wgc);
  wgc->SendDisconnectIdentityCredential(aOptions)->Then(
      GetCurrentSerialEventTarget(), __func__,
      [promise](nsresult aResult) {
        if (aResult == NS_ERROR_DOM_MALFORMED_URI) {
          promise->MaybeRejectWithInvalidStateError(
              "Error parsing the provided URI");
        } else if (NS_FAILED(aResult)) {
          promise->MaybeRejectWithNetworkError(
              "Error sending disconnect request");
        } else {
          promise->MaybeResolveWithUndefined();
        }
      },
      [promise](mozilla::ipc::ResponseRejectReason aError) {
        promise->MaybeRejectWithUnknownError("Unknown failure");
      });
  return promise.forget();
}

// static
RefPtr<MozPromise<bool, nsresult, true>>
IdentityCredential::DisconnectInMainProcess(
    nsIPrincipal* aDocumentPrincipal,
    const IdentityCredentialDisconnectOptions& aOptions) {
  MOZ_ASSERT(XRE_IsParentProcess());
  nsresult rv;
  nsCOMPtr<nsIIdentityCredentialStorageService> icStorageService =
      mozilla::components::IdentityCredentialStorageService::Service(&rv);
  if (NS_WARN_IF(!icStorageService)) {
    return MozPromise<bool, nsresult, true>::CreateAndReject(rv, __func__);
  }

  RefPtr<MozPromise<bool, nsresult, true>::Private> resultPromise =
      new MozPromise<bool, nsresult, true>::Private(__func__);

  RefPtr<nsIURI> configURI;
  rv = NS_NewURI(getter_AddRefs(configURI), aOptions.mConfigURL.Value());
  if (NS_FAILED(rv)) {
    resultPromise->Reject(NS_ERROR_DOM_MALFORMED_URI, __func__);
    return resultPromise;
  }

  nsCOMPtr<nsIPrincipal> principal(aDocumentPrincipal);
  nsCOMPtr<nsIPrincipal> idpPrincipal = BasePrincipal::CreateContentPrincipal(
      configURI, principal->OriginAttributesRef());

  IdentityCredential::CheckRootManifest(aDocumentPrincipal, aOptions)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [aOptions, principal](bool valid) {
            if (valid) {
              return IdentityCredential::FetchInternalManifest(principal,
                                                               aOptions);
            }
            return IdentityCredential::GetManifestPromise::CreateAndReject(
                NS_ERROR_FAILURE, __func__);
          },
          [](nsresult error) {
            return IdentityCredential::GetManifestPromise::CreateAndReject(
                error, __func__);
          })
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [resultPromise, aOptions, icStorageService, configURI, idpPrincipal,
           principal](const IdentityProviderAPIConfig& aConfig) {
            if (!aConfig.mDisconnect_endpoint.WasPassed()) {
              resultPromise->Reject(NS_ERROR_DOM_NETWORK_ERR, __func__);
              return MozPromise<DisconnectedAccount, nsresult,
                                true>::CreateAndReject(NS_OK, __func__);
            }
            RefPtr<nsIURI> disconnectURI;
            nsCString disconnectArgument = aConfig.mDisconnect_endpoint.Value();
            nsresult rv = NS_NewURI(getter_AddRefs(disconnectURI),
                                    disconnectArgument, nullptr, configURI);
            if (NS_FAILED(rv)) {
              resultPromise->Reject(NS_ERROR_DOM_NETWORK_ERR, __func__);
              return MozPromise<DisconnectedAccount, nsresult,
                                true>::CreateAndReject(NS_OK, __func__);
            }

            bool connected = false;
            rv = icStorageService->Connected(principal, idpPrincipal,
                                             &connected);
            if (NS_WARN_IF(NS_FAILED(rv)) || !connected) {
              resultPromise->Reject(NS_ERROR_DOM_NETWORK_ERR, __func__);
              return MozPromise<DisconnectedAccount, nsresult,
                                true>::CreateAndReject(NS_OK, __func__);
            }

            // Create a new request
            URLParams bodyValue;
            bodyValue.Set("client_id"_ns, aOptions.mClientId.Value());
            bodyValue.Set("account_hint"_ns, aOptions.mAccountHint);
            nsAutoCString bodyCString;
            bodyValue.Serialize(bodyCString, true);
            return IdentityNetworkHelpers::FetchDisconnectHelper(
                disconnectURI, bodyCString, principal);
          },
          [resultPromise](nsresult aError) {
            resultPromise->Reject(aError, __func__);
            // We reject with NS_OK, so that we don't disconnect accounts in the
            // reject callback here.
            return MozPromise<DisconnectedAccount, nsresult,
                              true>::CreateAndReject(NS_OK, __func__);
          })
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [icStorageService, principal, idpPrincipal,
           resultPromise](const DisconnectedAccount& token) {
            bool registered = false, notUsed = false;
            nsresult rv = icStorageService->GetState(principal, idpPrincipal,
                                                     token.mAccount_id,
                                                     &registered, &notUsed);
            if (NS_WARN_IF(NS_FAILED(rv))) {
              resultPromise->Reject(NS_ERROR_UNEXPECTED, __func__);
              return;
            }
            if (registered) {
              nsresult rv = icStorageService->Delete(principal, idpPrincipal,
                                                     token.mAccount_id);
              if (NS_WARN_IF(NS_FAILED(rv))) {
                resultPromise->Reject(NS_ERROR_UNEXPECTED, __func__);
                return;
              }
              resultPromise->Resolve(true, __func__);
            } else {
              nsresult rv =
                  icStorageService->Disconnect(principal, idpPrincipal);
              if (NS_WARN_IF(NS_FAILED(rv))) {
                resultPromise->Reject(NS_ERROR_UNEXPECTED, __func__);
                return;
              }
              resultPromise->Resolve(true, __func__);
            }
            return;
          },
          [icStorageService, principal, idpPrincipal,
           resultPromise](nsresult error) {
            // Bail out if we already rejected the result above.
            if (error == NS_OK) {
              return;
            }

            // If we issued the request and it failed, fall back
            // to clearing all.
            nsresult rv = icStorageService->Disconnect(principal, idpPrincipal);
            if (NS_WARN_IF(NS_FAILED(rv))) {
              resultPromise->Reject(NS_ERROR_UNEXPECTED, __func__);
              return;
            }
            resultPromise->Resolve(true, __func__);
            return;
          });

  return resultPromise;
}

// static
RefPtr<IdentityCredential::GetIdentityProviderConfigWithManifestPromise>
IdentityCredential::PromptUserToSelectProvider(
    BrowsingContext* aBrowsingContext,
    const Sequence<IdentityProviderConfig>& aProviders,
    const Sequence<GetManifestPromise::ResolveOrRejectValue>& aManifests) {
  MOZ_ASSERT(aBrowsingContext);
  RefPtr<
      IdentityCredential::GetIdentityProviderConfigWithManifestPromise::Private>
      resultPromise = new IdentityCredential::
          GetIdentityProviderConfigWithManifestPromise::Private(__func__);

  if (NS_WARN_IF(!aBrowsingContext)) {
    resultPromise->Reject(NS_ERROR_FAILURE, __func__);
    return resultPromise;
  }

  nsresult error;
  nsCOMPtr<nsIIdentityCredentialPromptService> icPromptService =
      mozilla::components::IdentityCredentialPromptService::Service(&error);
  if (NS_WARN_IF(!icPromptService)) {
    resultPromise->Reject(error, __func__);
    return resultPromise;
  }

  nsCOMPtr<nsIXPConnectWrappedJS> wrapped = do_QueryInterface(icPromptService);
  AutoJSAPI jsapi;
  if (NS_WARN_IF(!jsapi.Init(wrapped->GetJSObjectGlobal()))) {
    resultPromise->Reject(NS_ERROR_FAILURE, __func__);
    return resultPromise;
  }

  JS::Rooted<JS::Value> providersJS(jsapi.cx());
  bool success = ToJSValue(jsapi.cx(), aProviders, &providersJS);
  if (NS_WARN_IF(!success)) {
    resultPromise->Reject(NS_ERROR_FAILURE, __func__);
    return resultPromise;
  }

  // Convert each settled MozPromise into a Nullable<ResolveValue>
  Sequence<Nullable<IdentityProviderAPIConfig>> manifests;
  for (GetManifestPromise::ResolveOrRejectValue manifest : aManifests) {
    if (manifest.IsResolve()) {
      if (NS_WARN_IF(
              !manifests.AppendElement(manifest.ResolveValue(), fallible))) {
        resultPromise->Reject(NS_ERROR_FAILURE, __func__);
        return resultPromise;
      }
    } else {
      if (NS_WARN_IF(!manifests.AppendElement(
              Nullable<IdentityProviderAPIConfig>(), fallible))) {
        resultPromise->Reject(NS_ERROR_FAILURE, __func__);
        return resultPromise;
      }
    }
  }
  JS::Rooted<JS::Value> manifestsJS(jsapi.cx());
  success = ToJSValue(jsapi.cx(), manifests, &manifestsJS);
  if (NS_WARN_IF(!success)) {
    resultPromise->Reject(NS_ERROR_FAILURE, __func__);
    return resultPromise;
  }

  RefPtr<Promise> showPromptPromise;
  icPromptService->ShowProviderPrompt(aBrowsingContext, providersJS,
                                      manifestsJS,
                                      getter_AddRefs(showPromptPromise));

  showPromptPromise->AddCallbacksWithCycleCollectedArgs(
      [aProviders, aManifests, resultPromise](
          JSContext*, JS::Handle<JS::Value> aValue, ErrorResult&) {
        int32_t result = aValue.toInt32();
        if (result < 0 || (uint32_t)result > aProviders.Length() ||
            (uint32_t)result > aManifests.Length()) {
          resultPromise->Reject(NS_ERROR_FAILURE, __func__);
          return;
        }
        const IdentityProviderConfig& resolvedProvider =
            aProviders.ElementAt(result);
        if (!aManifests.ElementAt(result).IsResolve()) {
          resultPromise->Reject(NS_ERROR_FAILURE, __func__);
          return;
        }
        const IdentityProviderAPIConfig& resolvedManifest =
            aManifests.ElementAt(result).ResolveValue();
        resultPromise->Resolve(
            std::make_tuple(resolvedProvider, resolvedManifest), __func__);
      },
      [resultPromise](JSContext*, JS::Handle<JS::Value> aValue, ErrorResult&) {
        resultPromise->Reject(
            Promise::TryExtractNSResultFromRejectionValue(aValue), __func__);
      });
  // Working around https://gcc.gnu.org/bugzilla/show_bug.cgi?id=85883
  showPromptPromise->AppendNativeHandler(
      new MozPromiseRejectOnDestruction{resultPromise, __func__});

  return resultPromise;
}

// static
RefPtr<IdentityCredential::GetAccountPromise>
IdentityCredential::PromptUserToSelectAccount(
    BrowsingContext* aBrowsingContext,
    const IdentityProviderAccountList& aAccounts,
    const IdentityProviderConfig& aProvider,
    const IdentityProviderAPIConfig& aManifest) {
  MOZ_ASSERT(aBrowsingContext);
  RefPtr<IdentityCredential::GetAccountPromise::Private> resultPromise =
      new IdentityCredential::GetAccountPromise::Private(__func__);

  if (NS_WARN_IF(!aBrowsingContext)) {
    resultPromise->Reject(NS_ERROR_FAILURE, __func__);
    return resultPromise;
  }

  nsresult error;
  nsCOMPtr<nsIIdentityCredentialPromptService> icPromptService =
      mozilla::components::IdentityCredentialPromptService::Service(&error);
  if (NS_WARN_IF(!icPromptService)) {
    resultPromise->Reject(error, __func__);
    return resultPromise;
  }

  nsCOMPtr<nsIXPConnectWrappedJS> wrapped = do_QueryInterface(icPromptService);
  AutoJSAPI jsapi;
  if (NS_WARN_IF(!jsapi.Init(wrapped->GetJSObjectGlobal()))) {
    resultPromise->Reject(NS_ERROR_FAILURE, __func__);
    return resultPromise;
  }

  JS::Rooted<JS::Value> accountsJS(jsapi.cx());
  bool success = ToJSValue(jsapi.cx(), aAccounts, &accountsJS);
  if (NS_WARN_IF(!success)) {
    resultPromise->Reject(NS_ERROR_FAILURE, __func__);
    return resultPromise;
  }

  JS::Rooted<JS::Value> providerJS(jsapi.cx());
  success = ToJSValue(jsapi.cx(), aProvider, &providerJS);
  if (NS_WARN_IF(!success)) {
    resultPromise->Reject(NS_ERROR_FAILURE, __func__);
    return resultPromise;
  }

  JS::Rooted<JS::Value> manifestJS(jsapi.cx());
  success = ToJSValue(jsapi.cx(), aManifest, &manifestJS);
  if (NS_WARN_IF(!success)) {
    resultPromise->Reject(NS_ERROR_FAILURE, __func__);
    return resultPromise;
  }

  RefPtr<Promise> showPromptPromise;
  icPromptService->ShowAccountListPrompt(aBrowsingContext, accountsJS,
                                         providerJS, manifestJS,
                                         getter_AddRefs(showPromptPromise));

  showPromptPromise->AddCallbacksWithCycleCollectedArgs(
      [aAccounts, resultPromise, aManifest](
          JSContext*, JS::Handle<JS::Value> aValue, ErrorResult&) {
        int32_t result = aValue.toInt32();
        if (!aAccounts.mAccounts.WasPassed() || result < 0 ||
            (uint32_t)result > aAccounts.mAccounts.Value().Length()) {
          resultPromise->Reject(NS_ERROR_FAILURE, __func__);
          return;
        }
        const IdentityProviderAccount& resolved =
            aAccounts.mAccounts.Value().ElementAt(result);
        resultPromise->Resolve(std::make_tuple(aManifest, resolved), __func__);
      },
      [resultPromise](JSContext*, JS::Handle<JS::Value> aValue, ErrorResult&) {
        resultPromise->Reject(
            Promise::TryExtractNSResultFromRejectionValue(aValue), __func__);
      });
  // Working around https://gcc.gnu.org/bugzilla/show_bug.cgi?id=85883
  showPromptPromise->AppendNativeHandler(
      new MozPromiseRejectOnDestruction{resultPromise, __func__});

  return resultPromise;
}

// static
RefPtr<IdentityCredential::GetAccountPromise>
IdentityCredential::PromptUserWithPolicy(
    BrowsingContext* aBrowsingContext, nsIPrincipal* aPrincipal,
    const IdentityProviderAccount& aAccount,
    const IdentityProviderAPIConfig& aManifest,
    const IdentityProviderConfig& aProvider) {
  MOZ_ASSERT(aBrowsingContext);
  MOZ_ASSERT(aPrincipal);

  nsresult error;
  nsCOMPtr<nsIIdentityCredentialStorageService> icStorageService =
      mozilla::components::IdentityCredentialStorageService::Service(&error);
  if (NS_WARN_IF(!icStorageService)) {
    return IdentityCredential::GetAccountPromise::CreateAndReject(error,
                                                                  __func__);
  }

  // Check the storage bit
  nsCString configLocation = aProvider.mConfigURL.Value();
  nsCOMPtr<nsIURI> idpURI;
  error = NS_NewURI(getter_AddRefs(idpURI), configLocation);
  if (NS_WARN_IF(NS_FAILED(error))) {
    return IdentityCredential::GetAccountPromise::CreateAndReject(error,
                                                                  __func__);
  }
  bool registered = false;
  bool allowLogout = false;
  nsCOMPtr<nsIPrincipal> idpPrincipal = BasePrincipal::CreateContentPrincipal(
      idpURI, aPrincipal->OriginAttributesRef());
  error = icStorageService->GetState(aPrincipal, idpPrincipal,
                                     NS_ConvertUTF16toUTF8(aAccount.mId),
                                     &registered, &allowLogout);
  if (NS_WARN_IF(NS_FAILED(error))) {
    return IdentityCredential::GetAccountPromise::CreateAndReject(error,
                                                                  __func__);
  }

  // Mark as logged in and return
  icStorageService->SetState(aPrincipal, idpPrincipal,
                             NS_ConvertUTF16toUTF8(aAccount.mId), true, true);
  return IdentityCredential::GetAccountPromise::CreateAndResolve(
      std::make_tuple(aManifest, aAccount), __func__);
}

// static
void IdentityCredential::CloseUserInterface(BrowsingContext* aBrowsingContext) {
  nsresult error;
  nsCOMPtr<nsIIdentityCredentialPromptService> icPromptService =
      mozilla::components::IdentityCredentialPromptService::Service(&error);
  if (NS_WARN_IF(!icPromptService)) {
    return;
  }
  icPromptService->Close(aBrowsingContext);
}

// static
IdentityCredential::RequestType
IdentityCredential::DetermineRequestDiscoveryType(
    const IdentityCredentialRequestOptions& aOptions) {
  if (!aOptions.mProviders.WasPassed()) {
    return NONE;
  }
  for (const auto& provider : aOptions.mProviders.Value()) {
    if (provider.mConfigURL.WasPassed() && provider.mLoginURL.WasPassed()) {
      return INVALID;
    }
    if (provider.mConfigURL.WasPassed()) {
      return HEAVYWEIGHT;
    }
    if (provider.mLoginURL.WasPassed()) {
      if (aOptions.mProviders.Value().Length() > 1) {
        return INVALID;
      }
      return LIGHTWEIGHT;
    }
  }
  return NONE;
}

}  // namespace mozilla::dom
