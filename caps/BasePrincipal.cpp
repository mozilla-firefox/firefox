/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/BasePrincipal.h"

#include "ContentPrincipalJSONHandler.h"
#include "ExpandedPrincipal.h"
#include "ExpandedPrincipalJSONHandler.h"
#include "NullPrincipalJSONHandler.h"
#include "PrincipalJSONHandler.h"
#include "SubsumedPrincipalJSONHandler.h"
#include "ThirdPartyUtil.h"
#include "js/JSON.h"
#include "mozilla/Components.h"
#include "mozilla/ContentPrincipal.h"
#include "mozilla/ExtensionPolicyService.h"
#include "mozilla/JSONStringWriteFuncs.h"
#include "mozilla/JSONWriter.h"
#include "mozilla/NullPrincipal.h"
#include "mozilla/PermissionManager.h"
#include "mozilla/StaticPrefs_permissions.h"
#include "mozilla/StorageAccess.h"
#include "mozilla/dom/BlobURLProtocolHandler.h"
#include "mozilla/dom/ChromeUtils.h"
#include "mozilla/dom/ReferrerInfo.h"
#include "mozilla/dom/StorageUtils.h"
#include "mozilla/dom/ToJSValue.h"
#include "mozilla/dom/nsMixedContentBlocker.h"
#include "nsAboutProtocolUtils.h"
#include "nsContentUtils.h"
#include "nsDocShell.h"
#include "nsIEffectiveTLDService.h"
#include "nsIOService.h"
#include "nsIURIFixup.h"
#include "nsIURIMutator.h"
#include "nsIURIWithSpecialOrigin.h"
#include "nsIURL.h"
#include "nsMixedContentBlocker.h"
#include "nsNetUtil.h"
#include "nsPIDOMWindow.h"
#include "nsScriptSecurityManager.h"
#include "nsSerializationHelper.h"
#include "nsServiceManagerUtils.h"
#include "prnetdb.h"

namespace mozilla {

BasePrincipal::BasePrincipal(PrincipalKind aKind,
                             const nsACString& aOriginNoSuffix,
                             const OriginAttributes& aOriginAttributes)
    : mOriginNoSuffix(NS_Atomize(aOriginNoSuffix)),
      mOriginSuffix(aOriginAttributes.CreateSuffixAtom()),
      mOriginAttributes(aOriginAttributes),
      mKind(aKind),
      mHasExplicitDomain(false) {}

BasePrincipal::BasePrincipal(BasePrincipal* aOther,
                             const OriginAttributes& aOriginAttributes)
    : mOriginNoSuffix(aOther->mOriginNoSuffix),
      mOriginSuffix(aOriginAttributes.CreateSuffixAtom()),
      mOriginAttributes(aOriginAttributes),
      mKind(aOther->mKind),
      mHasExplicitDomain(aOther->mHasExplicitDomain.load()) {}

BasePrincipal::~BasePrincipal() = default;

NS_IMETHODIMP
BasePrincipal::GetOrigin(nsACString& aOrigin) {
  nsresult rv = GetOriginNoSuffix(aOrigin);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString suffix;
  rv = GetOriginSuffix(suffix);
  NS_ENSURE_SUCCESS(rv, rv);
  aOrigin.Append(suffix);
  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::GetWebExposedOriginSerialization(nsACString& aOrigin) {
  aOrigin.Truncate();
  nsCOMPtr<nsIURI> prinURI;
  nsresult rv = GetURI(getter_AddRefs(prinURI));
  if (NS_FAILED(rv) || !prinURI) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  return nsContentUtils::GetWebExposedOriginSerialization(prinURI, aOrigin);
}

NS_IMETHODIMP
BasePrincipal::GetHostPort(nsACString& aRes) {
  aRes.Truncate();
  nsCOMPtr<nsIURI> prinURI;
  nsresult rv = GetURI(getter_AddRefs(prinURI));
  if (NS_FAILED(rv) || !prinURI) {
    return NS_OK;
  }
  return prinURI->GetHostPort(aRes);
}

NS_IMETHODIMP
BasePrincipal::GetHost(nsACString& aRes) {
  aRes.Truncate();
  nsCOMPtr<nsIURI> prinURI;
  nsresult rv = GetURI(getter_AddRefs(prinURI));
  if (NS_FAILED(rv) || !prinURI) {
    return NS_OK;
  }
  return prinURI->GetHost(aRes);
}

NS_IMETHODIMP
BasePrincipal::GetOriginNoSuffix(nsACString& aOrigin) {
  mOriginNoSuffix->ToUTF8String(aOrigin);
  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::GetSiteOrigin(nsACString& aSiteOrigin) {
  nsresult rv = GetSiteOriginNoSuffix(aSiteOrigin);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString suffix;
  rv = GetOriginSuffix(suffix);
  NS_ENSURE_SUCCESS(rv, rv);
  aSiteOrigin.Append(suffix);
  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::GetSiteOriginNoSuffix(nsACString& aSiteOrigin) {
  return GetOriginNoSuffix(aSiteOrigin);
}

template <typename HandlerTypesT>
bool ContainerPrincipalJSONHandler<HandlerTypesT>::ProcessInnerResult(
    bool aResult) {
  if (!aResult) {
    NS_WARNING("Failed to parse inner object");
    mState = State::Error;
    return false;
  }
  return true;
}

template <typename HandlerTypesT>
bool ContainerPrincipalJSONHandler<HandlerTypesT>::startObject() {
  if (mInnerHandler.isSome()) {
    return CallOnInner([&](auto& aInner) { return aInner.startObject(); });
  }

  switch (mState) {
    case State::Init:
      mState = State::StartObject;
      break;
    case State::SystemPrincipal_Key:
      mState = State::SystemPrincipal_StartObject;
      break;
    default:
      NS_WARNING("Unexpected object value");
      mState = State::Error;
      return false;
  }

  return true;
}

template <typename HandlerTypesT>
bool ContainerPrincipalJSONHandler<HandlerTypesT>::propertyName(
    const JS::Latin1Char* name, size_t length) {
  if (mInnerHandler.isSome()) {
    return CallOnInner(
        [&](auto& aInner) { return aInner.propertyName(name, length); });
  }

  switch (mState) {
    case State::StartObject: {
      if (length != 1) {
        NS_WARNING(
            nsPrintfCString("Unexpected property name length: %zu", length)
                .get());
        mState = State::Error;
        return false;
      }

      char key = char(name[0]);
      switch (key) {
        case BasePrincipal::NullPrincipalKey:
          mState = State::NullPrincipal_Inner;
          mInnerHandler.emplace(VariantType<NullPrincipalJSONHandler>());
          break;
        case BasePrincipal::ContentPrincipalKey:
          mState = State::ContentPrincipal_Inner;
          mInnerHandler.emplace(VariantType<ContentPrincipalJSONHandler>());
          break;
        case BasePrincipal::SystemPrincipalKey:
          mState = State::SystemPrincipal_Key;
          break;
        default:
          if constexpr (CanContainExpandedPrincipal) {
            if (key == BasePrincipal::ExpandedPrincipalKey) {
              mState = State::ExpandedPrincipal_Inner;
              mInnerHandler.emplace(
                  VariantType<ExpandedPrincipalJSONHandler>());
              break;
            }
          }
          NS_WARNING(
              nsPrintfCString("Unexpected property name: '%c'", key).get());
          mState = State::Error;
          return false;
      }
      break;
    }
    default:
      NS_WARNING("Unexpected property name");
      mState = State::Error;
      return false;
  }

  return true;
}

template <typename HandlerTypesT>
bool ContainerPrincipalJSONHandler<HandlerTypesT>::endObject() {
  if (mInnerHandler.isSome()) {
    return CallOnInner([&](auto& aInner) {
      if (!aInner.endObject()) {
        return false;
      }
      if (aInner.HasAccepted()) {
        this->mPrincipal = aInner.mPrincipal.forget();
        MOZ_ASSERT(this->mPrincipal);
        mInnerHandler.reset();
      }
      return true;
    });
  }

  switch (mState) {
    case State::SystemPrincipal_StartObject:
      mState = State::SystemPrincipal_EndObject;
      break;
    case State::SystemPrincipal_EndObject:
      this->mPrincipal =
          BasePrincipal::Cast(nsContentUtils::GetSystemPrincipal());
      mState = State::EndObject;
      break;
    case State::NullPrincipal_Inner:
      mState = State::EndObject;
      break;
    case State::ContentPrincipal_Inner:
      mState = State::EndObject;
      break;
    default:
      if constexpr (CanContainExpandedPrincipal) {
        if (mState == State::ExpandedPrincipal_Inner) {
          mState = State::EndObject;
          break;
        }
      }
      NS_WARNING("Unexpected end of object");
      mState = State::Error;
      return false;
  }

  return true;
}

template <typename HandlerTypesT>
bool ContainerPrincipalJSONHandler<HandlerTypesT>::startArray() {
  if constexpr (CanContainExpandedPrincipal) {
    if (mInnerHandler.isSome()) {
      return CallOnInner([&](auto& aInner) { return aInner.startArray(); });
    }
  }

  NS_WARNING("Unexpected array value");
  mState = State::Error;
  return false;
}

template <typename HandlerTypesT>
bool ContainerPrincipalJSONHandler<HandlerTypesT>::endArray() {
  if constexpr (CanContainExpandedPrincipal) {
    if (mInnerHandler.isSome()) {
      return CallOnInner([&](auto& aInner) { return aInner.endArray(); });
    }
  }

  NS_WARNING("Unexpected array value");
  mState = State::Error;
  return false;
}

template <typename HandlerTypesT>
bool ContainerPrincipalJSONHandler<HandlerTypesT>::stringValue(
    const JS::Latin1Char* str, size_t length) {
  if (mInnerHandler.isSome()) {
    return CallOnInner(
        [&](auto& aInner) { return aInner.stringValue(str, length); });
  }

  NS_WARNING("Unexpected string value");
  mState = State::Error;
  return false;
}

template class ContainerPrincipalJSONHandler<PrincipalJSONHandlerTypes>;
template class ContainerPrincipalJSONHandler<SubsumedPrincipalJSONHandlerTypes>;

// Takes a JSON string and parses it turning it into a principal of the
// corresponding type
//
// Given a content principal:
//
//                               inner JSON object
//                                      |
//       ---------------------------------------------------------
//       |                                                       |
// {"1": {"0": "https://mozilla.com", "2": "^privateBrowsingId=1"}}
//   |     |             |             |            |
//   |     -----------------------------            |
//   |              |    |                          |
// PrincipalKind    |    |                          |
//                  |    ----------------------------
//           SerializableKeys           |
//                                    Value
//
already_AddRefed<BasePrincipal> BasePrincipal::FromJSON(
    const nsACString& aJSON) {
  PrincipalJSONHandler handler;

  if (!JS::ParseJSONWithHandler(
          reinterpret_cast<const JS::Latin1Char*>(aJSON.BeginReading()),
          aJSON.Length(), &handler)) {
    NS_WARNING(
        nsPrintfCString("Unable to parse: %s", PromiseFlatCString(aJSON).get())
            .get());
    MOZ_ASSERT(false,
               "Unable to parse string as JSON to deserialize as a principal");
    return nullptr;
  }

  return handler.Get();
}

// Returns a JSON representation of the principal.
// Calling BasePrincipal::FromJSON will deserialize the JSON into
// the corresponding principal type.
nsresult BasePrincipal::ToJSON(nsACString& aJSON) {
  MOZ_ASSERT(aJSON.IsEmpty(), "ToJSON only supports an empty result input");
  aJSON.Truncate();

  // NOTE: JSONWriter emits raw UTF-8 code units for non-ASCII range.
  JSONStringRefWriteFunc func(aJSON);
  JSONWriter writer(func, JSONWriter::CollectionStyle::SingleLineStyle);

  nsresult rv = ToJSON(writer);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult BasePrincipal::ToJSON(JSONWriter& aWriter) {
  static_assert(eKindMax < std::size(JSONEnumKeyStrings));

  aWriter.Start(JSONWriter::CollectionStyle::SingleLineStyle);

  nsresult rv = WriteJSONProperties(aWriter);
  NS_ENSURE_SUCCESS(rv, rv);

  aWriter.End();

  return NS_OK;
}

nsresult BasePrincipal::WriteJSONProperties(JSONWriter& aWriter) {
  aWriter.StartObjectProperty(JSONEnumKeyStrings[Kind()],
                              JSONWriter::CollectionStyle::SingleLineStyle);

  nsresult rv = WriteJSONInnerProperties(aWriter);
  NS_ENSURE_SUCCESS(rv, rv);

  aWriter.EndObject();

  return NS_OK;
}

nsresult BasePrincipal::WriteJSONInnerProperties(JSONWriter& /*aWriter*/) {
  return NS_OK;
}

bool BasePrincipal::FastSubsumesIgnoringFPD(
    nsIPrincipal* aOther, DocumentDomainConsideration aConsideration) {
  MOZ_ASSERT(aOther);

  if (Kind() == eContentPrincipal &&
      !dom::ChromeUtils::IsOriginAttributesEqualIgnoringFPD(
          mOriginAttributes, Cast(aOther)->mOriginAttributes)) {
    return false;
  }

  return SubsumesInternal(aOther, aConsideration);
}

bool BasePrincipal::Subsumes(nsIPrincipal* aOther,
                             DocumentDomainConsideration aConsideration) {
  MOZ_ASSERT(aOther);
  MOZ_ASSERT_IF(Kind() == eContentPrincipal, mOriginSuffix);

  // Expanded principals handle origin attributes for each of their
  // sub-principals individually, null principals do only simple checks for
  // pointer equality, and system principals are immune to origin attributes
  // checks, so only do this check for content principals.
  if (Kind() == eContentPrincipal &&
      mOriginSuffix != Cast(aOther)->mOriginSuffix) {
    return false;
  }

  return SubsumesInternal(aOther, aConsideration);
}

NS_IMETHODIMP
BasePrincipal::Equals(nsIPrincipal* aOther, bool* aResult) {
  NS_ENSURE_ARG_POINTER(aOther);

  *aResult = FastEquals(aOther);

  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::EqualsForPermission(nsIPrincipal* aOther, bool aExactHost,
                                   bool* aResult) {
  *aResult = false;
  NS_ENSURE_ARG_POINTER(aOther);
  NS_ENSURE_ARG_POINTER(aResult);

  auto* other = Cast(aOther);
  if (Kind() != other->Kind()) {
    // Principals of different kinds can't be equal.
    return NS_OK;
  }

  if (Kind() == eSystemPrincipal) {
    *aResult = this == other;
    return NS_OK;
  }

  if (Kind() == eNullPrincipal) {
    // We don't store permissions for NullPrincipals.
    return NS_OK;
  }

  MOZ_ASSERT(Kind() == eExpandedPrincipal || Kind() == eContentPrincipal);

  // Certain origin attributes should not be used to isolate permissions.
  // Create a stripped copy of both OA sets to compare.
  mozilla::OriginAttributes ourAttrs = mOriginAttributes;
  PermissionManager::MaybeStripOriginAttributes(false, ourAttrs);
  mozilla::OriginAttributes theirAttrs = aOther->OriginAttributesRef();
  PermissionManager::MaybeStripOriginAttributes(false, theirAttrs);

  if (ourAttrs != theirAttrs) {
    return NS_OK;
  }

  if (mOriginNoSuffix == other->mOriginNoSuffix) {
    *aResult = true;
    return NS_OK;
  }

  // If we are matching with an exact host, we're done now - the permissions
  // don't match otherwise, we need to start comparing subdomains!
  if (aExactHost) {
    return NS_OK;
  }

  nsCOMPtr<nsIURI> ourURI;
  nsresult rv = GetURI(getter_AddRefs(ourURI));
  NS_ENSURE_SUCCESS(rv, rv);
  // Some principal types may indicate success, but still return nullptr for
  // URI.
  NS_ENSURE_TRUE(ourURI, NS_ERROR_FAILURE);

  nsCOMPtr<nsIURI> otherURI;
  rv = other->GetURI(getter_AddRefs(otherURI));
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(otherURI, NS_ERROR_FAILURE);

  // Compare schemes
  nsAutoCString otherScheme;
  rv = otherURI->GetScheme(otherScheme);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString ourScheme;
  rv = ourURI->GetScheme(ourScheme);
  NS_ENSURE_SUCCESS(rv, rv);

  if (otherScheme != ourScheme) {
    return NS_OK;
  }

  // Compare ports
  int32_t otherPort;
  rv = otherURI->GetPort(&otherPort);
  NS_ENSURE_SUCCESS(rv, rv);

  int32_t ourPort;
  rv = ourURI->GetPort(&ourPort);
  NS_ENSURE_SUCCESS(rv, rv);

  if (otherPort != ourPort) {
    return NS_OK;
  }

  // Check if the host or any subdomain of their host matches.
  nsAutoCString otherHost;
  rv = otherURI->GetHost(otherHost);
  if (NS_FAILED(rv) || otherHost.IsEmpty()) {
    return NS_OK;
  }

  nsAutoCString ourHost;
  rv = ourURI->GetHost(ourHost);
  if (NS_FAILED(rv) || ourHost.IsEmpty()) {
    return NS_OK;
  }

  nsCOMPtr<nsIEffectiveTLDService> tldService =
      mozilla::components::EffectiveTLD::Service();
  if (!tldService) {
    NS_ERROR("Should have a tld service!");
    return NS_ERROR_FAILURE;
  }

  // This loop will not loop forever, as GetNextSubDomain will eventually fail
  // with NS_ERROR_INSUFFICIENT_DOMAIN_LEVELS.
  while (otherHost != ourHost && otherHost.Length() > ourHost.Length()) {
    rv = tldService->GetNextSubDomain(otherHost, otherHost);
    if (NS_FAILED(rv)) {
      if (rv == NS_ERROR_INSUFFICIENT_DOMAIN_LEVELS) {
        return NS_OK;
      }
      return rv;
    }
  }

  *aResult = otherHost == ourHost;
  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::EqualsConsideringDomain(nsIPrincipal* aOther, bool* aResult) {
  NS_ENSURE_ARG_POINTER(aOther);

  *aResult = FastEqualsConsideringDomain(aOther);

  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::EqualsURI(nsIURI* aOtherURI, bool* aResult) {
  *aResult = false;
  nsCOMPtr<nsIURI> prinURI;
  nsresult rv = GetURI(getter_AddRefs(prinURI));
  if (NS_FAILED(rv) || !prinURI) {
    return NS_OK;
  }
  return prinURI->EqualsExceptRef(aOtherURI, aResult);
}

NS_IMETHODIMP
BasePrincipal::Subsumes(nsIPrincipal* aOther, bool* aResult) {
  NS_ENSURE_ARG_POINTER(aOther);

  *aResult = FastSubsumes(aOther);

  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::SubsumesConsideringDomain(nsIPrincipal* aOther, bool* aResult) {
  NS_ENSURE_ARG_POINTER(aOther);

  *aResult = FastSubsumesConsideringDomain(aOther);

  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::SubsumesConsideringDomainIgnoringFPD(nsIPrincipal* aOther,
                                                    bool* aResult) {
  NS_ENSURE_ARG_POINTER(aOther);

  *aResult = FastSubsumesConsideringDomainIgnoringFPD(aOther);

  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::CheckMayLoad(nsIURI* aURI, bool aAllowIfInheritsPrincipal) {
  return CheckMayLoadHelper(aURI, aAllowIfInheritsPrincipal, false, 0);
}

NS_IMETHODIMP
BasePrincipal::CheckMayLoadWithReporting(nsIURI* aURI,
                                         bool aAllowIfInheritsPrincipal,
                                         uint64_t aInnerWindowID) {
  AssertIsOnMainThread();
  return CheckMayLoadHelper(aURI, aAllowIfInheritsPrincipal, true,
                            aInnerWindowID);
}

nsresult BasePrincipal::CheckMayLoadHelper(nsIURI* aURI,
                                           bool aAllowIfInheritsPrincipal,
                                           bool aReport,
                                           uint64_t aInnerWindowID) {
  NS_ENSURE_ARG_POINTER(aURI);
  MOZ_ASSERT(
      aReport || aInnerWindowID == 0,
      "Why do we have an inner window id if we're not supposed to report?");
  MOZ_ASSERT(!aReport || NS_IsMainThread(), "Must be on main thread to report");

  // Check the internal method first, which allows us to quickly approve loads
  // for the System Principal.
  if (MayLoadInternal(aURI)) {
    return NS_OK;
  }

  nsresult rv;
  if (aAllowIfInheritsPrincipal) {
    // If the caller specified to allow loads of URIs that inherit
    // our principal, allow the load if this URI inherits its principal.
    bool doesInheritSecurityContext;
    rv = NS_URIChainHasFlags(aURI,
                             nsIProtocolHandler::URI_INHERITS_SECURITY_CONTEXT,
                             &doesInheritSecurityContext);
    if (NS_SUCCEEDED(rv) && doesInheritSecurityContext) {
      return NS_OK;
    }
  }

  // Get the principal uri for the WebExtension access check or error.
  nsCOMPtr<nsIURI> prinURI;
  rv = GetURI(getter_AddRefs(prinURI));
  if (!(NS_SUCCEEDED(rv) && prinURI)) {
    return NS_ERROR_DOM_BAD_URI;
  }

  // If the URL being loaded corresponds to a WebExtension URL, ask the policy
  // if the path should be accessible.
  bool isWebExtensionResource;
  rv = NS_URIChainHasFlags(aURI,
                           nsIProtocolHandler::URI_IS_WEBEXTENSION_RESOURCE,
                           &isWebExtensionResource);
  if (NS_SUCCEEDED(rv) && isWebExtensionResource) {
    extensions::URLInfo urlInfo(aURI);
    if (RefPtr<extensions::WebExtensionPolicyCore> urlPolicyCore =
            ExtensionPolicyService::GetCoreByURL(urlInfo)) {
      extensions::URLInfo prinUrlInfo(prinURI);
      if (urlPolicyCore->SourceMayAccessPath(prinUrlInfo, urlInfo.FilePath())) {
        return NS_OK;
      }
    }
  }

  if (aReport) {
    // FIXME: Once bug 1900706 is complete, reporting can be updated to work
    // off-main-thread.
    nsScriptSecurityManager::ReportError("CheckSameOriginError", prinURI, aURI,
                                         mOriginAttributes.IsPrivateBrowsing(),
                                         aInnerWindowID);
  }

  return NS_ERROR_DOM_BAD_URI;
}

NS_IMETHODIMP
BasePrincipal::IsThirdPartyURI(nsIURI* aURI, bool* aRes) {
  if (IsSystemPrincipal() || (AddonPolicyCore() && AddonAllowsLoad(aURI))) {
    *aRes = false;
    return NS_OK;
  }

  *aRes = true;
  // If we do not have a URI its always 3rd party.
  nsCOMPtr<nsIURI> prinURI;
  nsresult rv = GetURI(getter_AddRefs(prinURI));
  if (NS_FAILED(rv) || !prinURI) {
    return NS_OK;
  }
  ThirdPartyUtil* thirdPartyUtil = ThirdPartyUtil::GetInstance();
  return thirdPartyUtil->IsThirdPartyURI(prinURI, aURI, aRes);
}

NS_IMETHODIMP
BasePrincipal::IsThirdPartyPrincipal(nsIPrincipal* aPrin, bool* aRes) {
  *aRes = true;
  nsCOMPtr<nsIURI> prinURI;
  nsresult rv = GetURI(getter_AddRefs(prinURI));
  if (NS_FAILED(rv) || !prinURI) {
    return NS_OK;
  }
  return aPrin->IsThirdPartyURI(prinURI, aRes);
}

NS_IMETHODIMP
BasePrincipal::IsThirdPartyChannel(nsIChannel* aChan, bool* aRes) {
  AssertIsOnMainThread();
  if (IsSystemPrincipal()) {
    // Nothing is 3rd party to the system principal.
    *aRes = false;
    return NS_OK;
  }

  nsCOMPtr<nsIURI> prinURI;
  GetURI(getter_AddRefs(prinURI));
  ThirdPartyUtil* thirdPartyUtil = ThirdPartyUtil::GetInstance();
  return thirdPartyUtil->IsThirdPartyChannel(aChan, prinURI, aRes);
}

NS_IMETHODIMP
BasePrincipal::IsSameOrigin(nsIURI* aURI, bool* aRes) {
  *aRes = false;
  nsCOMPtr<nsIURI> prinURI;
  nsresult rv = GetURI(getter_AddRefs(prinURI));
  if (NS_FAILED(rv) || !prinURI) {
    // Note that expanded and system principals return here, because they have
    // no URI.
    return NS_OK;
  }
  *aRes = nsScriptSecurityManager::SecurityCompareURIs(prinURI, aURI);
  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::IsL10nAllowed(nsIURI* aURI, bool* aRes) {
  AssertIsOnMainThread();  // URI_DANGEROUS_TO_LOAD is not threadsafe to query.
  *aRes = false;

  if (nsContentUtils::IsErrorPage(aURI)) {
    *aRes = true;
    return NS_OK;
  }

  // The system principal is always allowed.
  if (IsSystemPrincipal()) {
    *aRes = true;
    return NS_OK;
  }

  nsCOMPtr<nsIURI> uri;
  nsresult rv = GetURI(getter_AddRefs(uri));
  NS_ENSURE_SUCCESS(rv, NS_OK);

  if (!uri) {
    return NS_OK;
  }

  bool hasFlags;

  // Allow access to uris that cannot be loaded by web content.
  rv = NS_URIChainHasFlags(uri, nsIProtocolHandler::URI_DANGEROUS_TO_LOAD,
                           &hasFlags);
  NS_ENSURE_SUCCESS(rv, NS_OK);
  if (hasFlags) {
    *aRes = true;
    return NS_OK;
  }

  // UI resources also get access.
  rv = NS_URIChainHasFlags(uri, nsIProtocolHandler::URI_IS_UI_RESOURCE,
                           &hasFlags);
  NS_ENSURE_SUCCESS(rv, NS_OK);
  if (hasFlags) {
    *aRes = true;
    return NS_OK;
  }

  auto policy = AddonPolicyCore();
  *aRes = (policy && policy->IsPrivileged());
  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::GetPrefLightCacheKey(nsIURI* aURI, bool aWithCredentials,
                                    const OriginAttributes& aOriginAttributes,
                                    nsACString& _retval) {
  _retval.Truncate();
  constexpr auto space = " "_ns;

  nsCOMPtr<nsIURI> uri;
  nsresult rv = GetURI(getter_AddRefs(uri));
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString scheme, host, port;
  if (uri) {
    uri->GetScheme(scheme);
    uri->GetHost(host);
    port.AppendInt(NS_GetRealPort(uri));
  }

  if (aWithCredentials) {
    _retval.AssignLiteral("cred");
  } else {
    _retval.AssignLiteral("nocred");
  }

  nsAutoCString spec;
  rv = aURI->GetSpec(spec);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString originAttributesSuffix;
  aOriginAttributes.CreateSuffix(originAttributesSuffix);

  _retval.Append(space + scheme + space + host + space + port + space + spec +
                 space + originAttributesSuffix);

  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::HasFirstpartyStorageAccess(mozIDOMWindow* aCheckWindow,
                                          uint32_t* aRejectedReason,
                                          bool* aOutAllowed) {
  AssertIsOnMainThread();
  *aRejectedReason = 0;
  *aOutAllowed = false;

  if (IsSystemPrincipal()) {
    // System principal is always considered to have first-party storage access.
    *aOutAllowed = true;
    return NS_OK;
  }

  nsPIDOMWindowInner* win = nsPIDOMWindowInner::From(aCheckWindow);
  nsCOMPtr<nsIURI> uri;
  nsresult rv = GetURI(getter_AddRefs(uri));
  if (NS_FAILED(rv)) {
    return rv;
  }

  // The uri could be null if the principal is an expanded principal.
  if (!uri) {
    return NS_ERROR_UNEXPECTED;
  }

  *aOutAllowed = ShouldAllowAccessFor(win, uri, true, aRejectedReason);
  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::GetIsNullPrincipal(bool* aResult) {
  *aResult = Kind() == eNullPrincipal;
  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::GetIsContentPrincipal(bool* aResult) {
  *aResult = Kind() == eContentPrincipal;
  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::GetIsExpandedPrincipal(bool* aResult) {
  *aResult = Kind() == eExpandedPrincipal;
  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::GetAsciiSpec(nsACString& aSpec) {
  aSpec.Truncate();
  nsCOMPtr<nsIURI> prinURI;
  nsresult rv = GetURI(getter_AddRefs(prinURI));
  if (NS_FAILED(rv) || !prinURI) {
    return NS_OK;
  }
  return prinURI->GetAsciiSpec(aSpec);
}

NS_IMETHODIMP
BasePrincipal::GetSpec(nsACString& aSpec) {
  aSpec.Truncate();
  nsCOMPtr<nsIURI> prinURI;
  nsresult rv = GetURI(getter_AddRefs(prinURI));
  if (NS_FAILED(rv) || !prinURI) {
    return NS_OK;
  }
  return prinURI->GetSpec(aSpec);
}

NS_IMETHODIMP
BasePrincipal::GetAsciiHost(nsACString& aHost) {
  aHost.Truncate();
  nsCOMPtr<nsIURI> prinURI;
  nsresult rv = GetURI(getter_AddRefs(prinURI));
  if (NS_FAILED(rv) || !prinURI) {
    return NS_OK;
  }
  return prinURI->GetAsciiHost(aHost);
}

NS_IMETHODIMP
BasePrincipal::GetExposablePrePath(nsACString& aPrepath) {
  aPrepath.Truncate();
  nsCOMPtr<nsIURI> prinURI;
  nsresult rv = GetURI(getter_AddRefs(prinURI));
  if (NS_FAILED(rv) || !prinURI) {
    return NS_OK;
  }

  nsCOMPtr<nsIURI> exposableURI = net::nsIOService::CreateExposableURI(prinURI);
  return exposableURI->GetDisplayPrePath(aPrepath);
}

NS_IMETHODIMP
BasePrincipal::GetExposableSpec(nsACString& aSpec) {
  aSpec.Truncate();
  nsCOMPtr<nsIURI> prinURI;
  nsresult rv = GetURI(getter_AddRefs(prinURI));
  if (NS_FAILED(rv) || !prinURI) {
    return NS_OK;
  }
  nsCOMPtr<nsIURI> clone;
  rv = NS_MutateURI(prinURI)
           .SetQuery(""_ns)
           .SetRef(""_ns)
           .SetUserPass(""_ns)
           .Finalize(clone);
  NS_ENSURE_SUCCESS(rv, rv);
  return clone->GetAsciiSpec(aSpec);
}

NS_IMETHODIMP
BasePrincipal::GetPrePath(nsACString& aPath) {
  aPath.Truncate();
  nsCOMPtr<nsIURI> prinURI;
  nsresult rv = GetURI(getter_AddRefs(prinURI));
  if (NS_FAILED(rv) || !prinURI) {
    return NS_OK;
  }
  return prinURI->GetPrePath(aPath);
}

NS_IMETHODIMP
BasePrincipal::GetFilePath(nsACString& aPath) {
  aPath.Truncate();
  nsCOMPtr<nsIURI> prinURI;
  nsresult rv = GetURI(getter_AddRefs(prinURI));
  if (NS_FAILED(rv) || !prinURI) {
    return NS_OK;
  }
  return prinURI->GetFilePath(aPath);
}

NS_IMETHODIMP
BasePrincipal::GetIsSystemPrincipal(bool* aResult) {
  *aResult = IsSystemPrincipal();
  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::GetIsAddonPrincipal(bool* aResult) {
  *aResult = !!AddonPolicyCore();
  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::GetIsAddonOrExpandedAddonPrincipal(bool* aResult) {
  *aResult = AddonPolicyCore() || ContentScriptAddonPolicyCore();
  return NS_OK;
}

NS_IMETHODIMP BasePrincipal::GetIsOnion(bool* aIsOnion) {
  *aIsOnion = false;
  nsCOMPtr<nsIURI> prinURI;
  nsresult rv = GetURI(getter_AddRefs(prinURI));
  if (NS_FAILED(rv) || !prinURI) {
    return NS_OK;
  }

  nsAutoCString host;
  rv = prinURI->GetHost(host);
  if (NS_FAILED(rv)) {
    return NS_OK;
  }
  *aIsOnion = StringEndsWith(host, ".onion"_ns);
  return NS_OK;
}

NS_IMETHODIMP BasePrincipal::GetIsIpAddress(bool* aIsIpAddress) {
  *aIsIpAddress = false;

  nsCOMPtr<nsIURI> prinURI;
  nsresult rv = GetURI(getter_AddRefs(prinURI));
  if (NS_FAILED(rv) || !prinURI) {
    return NS_OK;
  }

  nsAutoCString host;
  rv = prinURI->GetHost(host);
  if (NS_FAILED(rv)) {
    return NS_OK;
  }

  PRNetAddr prAddr;
  memset(&prAddr, 0, sizeof(prAddr));

  if (PR_StringToNetAddr(host.get(), &prAddr) == PR_SUCCESS) {
    *aIsIpAddress = true;
  }

  return NS_OK;
}

NS_IMETHODIMP BasePrincipal::GetIsLocalIpAddress(bool* aIsIpAddress) {
  *aIsIpAddress = false;

  nsCOMPtr<nsIURI> prinURI;
  nsresult rv = GetURI(getter_AddRefs(prinURI));
  if (NS_FAILED(rv) || !prinURI) {
    return NS_OK;
  }

  nsCOMPtr<nsIIOService> ioService = do_GetIOService(&rv);
  if (NS_FAILED(rv) || !ioService) {
    return NS_OK;
  }
  rv = ioService->HostnameIsLocalIPAddress(prinURI, aIsIpAddress);
  if (NS_FAILED(rv)) {
    *aIsIpAddress = false;
  }
  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::GetScheme(nsACString& aScheme) {
  aScheme.Truncate();

  nsCOMPtr<nsIURI> prinURI;
  nsresult rv = GetURI(getter_AddRefs(prinURI));
  if (NS_FAILED(rv) || !prinURI) {
    return NS_OK;
  }

  return prinURI->GetScheme(aScheme);
}

NS_IMETHODIMP
BasePrincipal::SchemeIs(const char* aScheme, bool* aResult) {
  *aResult = false;
  nsCOMPtr<nsIURI> prinURI;
  nsresult rv = GetURI(getter_AddRefs(prinURI));
  if (NS_WARN_IF(NS_FAILED(rv)) || !prinURI) {
    return NS_OK;
  }
  *aResult = prinURI->SchemeIs(aScheme);
  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::IsURIInPrefList(const char* aPref, bool* aResult) {
  AssertIsOnMainThread();
  *aResult = false;

  if (Kind() != eContentPrincipal) {
    // We don't store prefs for non-content principals.
    return NS_OK;
  }

  nsCOMPtr<nsIURI> prinURI;
  nsresult rv = GetURI(getter_AddRefs(prinURI));
  if (NS_FAILED(rv) || !prinURI) {
    return NS_OK;
  }
  *aResult = nsContentUtils::IsURIInPrefList(prinURI, aPref);
  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::IsURIInList(const nsACString& aList, bool* aResult) {
  *aResult = false;
  nsCOMPtr<nsIURI> prinURI;

  nsresult rv = GetURI(getter_AddRefs(prinURI));
  if (NS_FAILED(rv) || !prinURI) {
    return NS_OK;
  }

  *aResult = nsContentUtils::IsURIInList(prinURI, nsCString(aList));
  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::IsContentAccessibleAboutURI(bool* aResult) {
  *aResult = false;

  nsCOMPtr<nsIURI> prinURI;
  nsresult rv = GetURI(getter_AddRefs(prinURI));
  if (NS_FAILED(rv) || !prinURI) {
    return NS_OK;
  }

  if (!prinURI->SchemeIs("about")) {
    return NS_OK;
  }

  *aResult = NS_IsContentAccessibleAboutURI(prinURI);
  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::GetIsOriginPotentiallyTrustworthy(bool* aResult) {
  AssertIsOnMainThread();
  *aResult = false;

  nsCOMPtr<nsIURI> uri;
  nsresult rv = GetURI(getter_AddRefs(uri));
  if (NS_FAILED(rv) || !uri) {
    return NS_OK;
  }

  *aResult = nsMixedContentBlocker::IsPotentiallyTrustworthyOrigin(uri);
  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::GetIsLoopbackHost(bool* aRes) {
  AssertIsOnMainThread();
  *aRes = false;
  nsAutoCString host;
  nsresult rv = GetHost(host);
  // Swallow potential failure as this method is infallible.
  if (NS_FAILED(rv)) {
    return NS_OK;
  }

  *aRes = nsMixedContentBlocker::IsPotentiallyTrustworthyLoopbackHost(host);
  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::GetIsSecureContextAllowlistedHost(bool* aRes) {
  AssertIsOnMainThread();
  *aRes = false;

  // The allowlist only applies to network schemes, matching
  // nsMixedContentBlocker::IsPotentiallyTrustworthyOrigin. An https principal
  // is trustworthy on its own merits and must not be treated as allowlisted.
  nsAutoCString scheme;
  if (NS_FAILED(GetScheme(scheme)) ||
      (!scheme.EqualsLiteral("http") && !scheme.EqualsLiteral("ws"))) {
    return NS_OK;
  }

  nsAutoCString host;
  // Swallow potential failure as this method is infallible.
  if (NS_FAILED(GetHost(host))) {
    return NS_OK;
  }

  *aRes = nsMixedContentBlocker::IsPotentiallyTrustworthyAllowlistedHost(host);
  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::GetAboutModuleFlags(uint32_t* flags) {
  AssertIsOnMainThread();
  *flags = 0;
  nsCOMPtr<nsIURI> prinURI;
  nsresult rv = GetURI(getter_AddRefs(prinURI));
  if (NS_FAILED(rv) || !prinURI) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  if (!prinURI->SchemeIs("about")) {
    return NS_OK;
  }

  nsCOMPtr<nsIAboutModule> aboutModule;
  rv = NS_GetAboutModule(prinURI, getter_AddRefs(aboutModule));
  if (NS_FAILED(rv) || !aboutModule) {
    return rv;
  }
  return aboutModule->GetURIFlags(prinURI, flags);
}

NS_IMETHODIMP
BasePrincipal::GetOriginAttributes(JSContext* aCx,
                                   JS::MutableHandle<JS::Value> aVal) {
  if (NS_WARN_IF(!ToJSValue(aCx, mOriginAttributes, aVal))) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::GetOriginSuffix(nsACString& aOriginAttributes) {
  MOZ_ASSERT(mOriginSuffix);
  mOriginSuffix->ToUTF8String(aOriginAttributes);
  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::GetUserContextId(uint32_t* aUserContextId) {
  *aUserContextId = UserContextId();
  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::GetPrivateBrowsingId(uint32_t* aPrivateBrowsingId) {
  *aPrivateBrowsingId = PrivateBrowsingId();
  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::GetIsInPrivateBrowsing(bool* aIsInPrivateBrowsing) {
  *aIsInPrivateBrowsing = mOriginAttributes.IsPrivateBrowsing();
  return NS_OK;
}

nsresult BasePrincipal::GetAddonPolicy(
    extensions::WebExtensionPolicy** aResult) {
  AssertIsOnMainThread();
  RefPtr<extensions::WebExtensionPolicy> policy(AddonPolicy());
  policy.forget(aResult);
  return NS_OK;
}

nsresult BasePrincipal::GetContentScriptAddonPolicy(
    extensions::WebExtensionPolicy** aResult) {
  RefPtr<extensions::WebExtensionPolicy> policy(ContentScriptAddonPolicy());
  policy.forget(aResult);
  return NS_OK;
}

extensions::WebExtensionPolicy* BasePrincipal::AddonPolicy() {
  AssertIsOnMainThread();
  RefPtr<extensions::WebExtensionPolicyCore> core = AddonPolicyCore();
  return core ? core->GetMainThreadPolicy() : nullptr;
}

RefPtr<extensions::WebExtensionPolicyCore> BasePrincipal::AddonPolicyCore() {
  if (Is<ContentPrincipal>()) {
    return As<ContentPrincipal>()->AddonPolicyCore();
  }
  return nullptr;
}

bool BasePrincipal::AddonHasPermission(const nsAtom* aPerm) {
  if (auto policy = AddonPolicyCore()) {
    return policy->HasPermission(aPerm);
  }
  return false;
}

nsIPrincipal* BasePrincipal::PrincipalToInherit(nsIURI* aRequestedURI) {
  if (Is<ExpandedPrincipal>()) {
    return As<ExpandedPrincipal>()->PrincipalToInherit(aRequestedURI);
  }
  return this;
}

bool BasePrincipal::OverridesCSP(nsIPrincipal* aDocumentPrincipal) {
  MOZ_ASSERT(aDocumentPrincipal);

  // Expanded principals override CSP if and only if they subsume the document
  // principal.
  if (mKind == eExpandedPrincipal) {
    return FastSubsumes(aDocumentPrincipal);
  }
  // Extension principals always override the CSP of non-extension principals.
  // This is primarily for the sake of their stylesheets, which are usually
  // loaded from channels and cannot have expanded principals.
  return (AddonPolicyCore() &&
          !BasePrincipal::Cast(aDocumentPrincipal)->AddonPolicyCore());
}

already_AddRefed<BasePrincipal> BasePrincipal::CreateContentPrincipal(
    nsIURI* aURI, const OriginAttributes& aAttrs, nsIURI* aInitialDomain) {
  MOZ_ASSERT(aURI);

  // Blob URLs don't derive the principal directly from the URL in the same way
  // as normal content principals, and may have a non-content principal.
  nsCOMPtr<nsIURI> innermost = NS_GetInnermostURI(aURI);
  if (innermost && innermost->SchemeIs(BLOBURI_SCHEME)) {
    MOZ_ASSERT(!aInitialDomain,
               "an initial domain for a blob URI makes no sense");
    nsCOMPtr<nsIPrincipal> blobPrincipal;
    if (!dom::BlobURLProtocolHandler::GetBlobURLPrincipal(
            innermost, aAttrs, getter_AddRefs(blobPrincipal))) {
      // This isn't a valid Blob URL, give up and return a null principal.
      return NullPrincipal::Create(aAttrs);
    }
    return blobPrincipal.forget().downcast<BasePrincipal>();
  }

  nsAutoCString originNoSuffix;
  nsresult rv =
      ContentPrincipal::GenerateOriginNoSuffixFromURI(aURI, originNoSuffix);
  if (NS_FAILED(rv)) {
    // If the generation of the origin fails, we still want to have a valid
    // principal. Better to return a null principal here.
    return NullPrincipal::Create(aAttrs);
  }

  return CreateContentPrincipal(aURI, aAttrs, originNoSuffix, aInitialDomain);
}

already_AddRefed<BasePrincipal> BasePrincipal::CreateContentPrincipal(
    nsIURI* aURI, const OriginAttributes& aAttrs,
    const nsACString& aOriginNoSuffix, nsIURI* aInitialDomain) {
  MOZ_ASSERT(aURI);
  MOZ_ASSERT(!aOriginNoSuffix.IsEmpty());

  // If the URI is supposed to inherit the security context of whoever loads it,
  // we shouldn't make a content principal for it.
  bool inheritsPrincipal;
  nsresult rv = NS_URIChainHasFlags(
      aURI, nsIProtocolHandler::URI_INHERITS_SECURITY_CONTEXT,
      &inheritsPrincipal);
  if (NS_FAILED(rv) || inheritsPrincipal) {
    return NullPrincipal::Create(aAttrs);
  }

  // Check whether the URI knows what its principal is supposed to be.
#if defined(MOZ_THUNDERBIRD) || defined(MOZ_SUITE)
  nsCOMPtr<nsIURIWithSpecialOrigin> uriWithSpecialOrigin =
      do_QueryInterface(aURI);
  if (uriWithSpecialOrigin) {
    nsCOMPtr<nsIURI> origin;
    rv = uriWithSpecialOrigin->GetOrigin(getter_AddRefs(origin));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return nullptr;
    }
    MOZ_ASSERT(origin);
    OriginAttributes attrs;
    RefPtr<BasePrincipal> principal =
        CreateContentPrincipal(origin, attrs, aInitialDomain);
    return principal.forget();
  }
#endif

  // Mint a content principal.
  RefPtr<ContentPrincipal> principal =
      new ContentPrincipal(aURI, aAttrs, aOriginNoSuffix, aInitialDomain);
  return principal.forget();
}

already_AddRefed<BasePrincipal> BasePrincipal::CreateContentPrincipal(
    const nsACString& aOrigin) {
  MOZ_ASSERT(!StringBeginsWith(aOrigin, "["_ns),
             "CreateContentPrincipal does not support System and Expanded "
             "principals");

  MOZ_ASSERT(
      !StringBeginsWith(aOrigin, nsLiteralCString(NS_NULLPRINCIPAL_SCHEME ":")),
      "CreateContentPrincipal does not support NullPrincipal");

  nsAutoCString originNoSuffix;
  OriginAttributes attrs;
  if (!attrs.PopulateFromOrigin(aOrigin, originNoSuffix)) {
    return nullptr;
  }

  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_NewURI(getter_AddRefs(uri), originNoSuffix);
  NS_ENSURE_SUCCESS(rv, nullptr);

  return BasePrincipal::CreateContentPrincipal(uri, attrs);
}

already_AddRefed<BasePrincipal> BasePrincipal::CloneForcingOriginAttributes(
    const OriginAttributes& aOriginAttributes) {
  if (NS_WARN_IF(!IsContentPrincipal())) {
    return nullptr;
  }

  nsAutoCString originNoSuffix;
  nsresult rv = GetOriginNoSuffix(originNoSuffix);
  NS_ENSURE_SUCCESS(rv, nullptr);

  nsCOMPtr<nsIURI> uri;
  MOZ_ALWAYS_SUCCEEDS(GetURI(getter_AddRefs(uri)));

  // XXX: This does not copy over the domain. Should it?
  RefPtr<ContentPrincipal> copy =
      new ContentPrincipal(uri, aOriginAttributes, originNoSuffix, nullptr);
  return copy.forget();
}

extensions::WebExtensionPolicy* BasePrincipal::ContentScriptAddonPolicy() {
  AssertIsOnMainThread();
  RefPtr<extensions::WebExtensionPolicyCore> core =
      ContentScriptAddonPolicyCore();
  return core ? core->GetMainThreadPolicy() : nullptr;
}

RefPtr<extensions::WebExtensionPolicyCore>
BasePrincipal::ContentScriptAddonPolicyCore() {
  if (!Is<ExpandedPrincipal>()) {
    return nullptr;
  }

  auto* expanded = As<ExpandedPrincipal>();
  for (const auto& prin : expanded->AllowList()) {
    if (RefPtr<extensions::WebExtensionPolicyCore> policy =
            BasePrincipal::Cast(prin)->AddonPolicyCore()) {
      return policy;
    }
  }

  return nullptr;
}

bool BasePrincipal::AddonAllowsLoad(nsIURI* aURI,
                                    bool aExplicit /* = false */) {
  if (Is<ExpandedPrincipal>()) {
    return As<ExpandedPrincipal>()->AddonAllowsLoad(aURI, aExplicit);
  }
  if (auto policy = AddonPolicyCore()) {
    return policy->CanAccessURI(
        aURI, extensions::ACCESS_OWN_EXTENSION |
                  (aExplicit ? extensions::ACCESS_REQUIRE_EXPLICIT_DOMAIN : 0));
  }
  return false;
}

NS_IMETHODIMP
BasePrincipal::GetLocalStorageQuotaKey(nsACString& aKey) {
  aKey.Truncate();

  nsCOMPtr<nsIURI> uri;
  nsresult rv = GetURI(getter_AddRefs(uri));
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(uri, NS_ERROR_UNEXPECTED);

  // The special handling of the file scheme should be consistent with
  // GetStorageOriginKey.

  nsAutoCString baseDomain;
  rv = uri->GetAsciiHost(baseDomain);
  NS_ENSURE_SUCCESS(rv, rv);

  if (baseDomain.IsEmpty() && uri->SchemeIs("file")) {
    nsCOMPtr<nsIURL> url = do_QueryInterface(uri, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = url->GetDirectory(baseDomain);
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    nsCOMPtr<nsIEffectiveTLDService> eTLDService =
        mozilla::components::EffectiveTLD::Service(&rv);
    NS_ENSURE_SUCCESS(rv, rv);

    nsAutoCString eTLDplusOne;
    rv = eTLDService->GetBaseDomain(uri, 0, eTLDplusOne);
    if (NS_SUCCEEDED(rv)) {
      baseDomain = std::move(eTLDplusOne);
    } else if (rv == NS_ERROR_HOST_IS_IP_ADDRESS ||
               rv == NS_ERROR_INSUFFICIENT_DOMAIN_LEVELS) {
      rv = NS_OK;
    }
    NS_ENSURE_SUCCESS(rv, rv);
  }

  OriginAttributesRef().CreateSuffix(aKey);

  nsAutoCString subdomainsDBKey;
  rv = dom::StorageUtils::CreateReversedDomain(baseDomain, subdomainsDBKey);
  NS_ENSURE_SUCCESS(rv, rv);

  aKey.Append(':');
  aKey.Append(subdomainsDBKey);

  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::GetNextSubDomainPrincipal(
    nsIPrincipal** aNextSubDomainPrincipal) {
  nsCOMPtr<nsIURI> uri;
  nsresult rv = GetURI(getter_AddRefs(uri));
  if (NS_FAILED(rv) || !uri) {
    return NS_OK;
  }

  nsAutoCString host;
  rv = uri->GetHost(host);
  if (NS_FAILED(rv) || host.IsEmpty()) {
    return NS_OK;
  }

  nsCString subDomain;
  nsCOMPtr<nsIEffectiveTLDService> eTLDService =
      mozilla::components::EffectiveTLD::Service();
  if (!eTLDService) {
    return NS_OK;
  }
  rv = eTLDService->GetNextSubDomain(host, subDomain);

  if (NS_FAILED(rv) || subDomain.IsEmpty()) {
    return NS_OK;
  }

  nsCOMPtr<nsIURI> subDomainURI;
  rv = NS_MutateURI(uri).SetHost(subDomain).Finalize(subDomainURI);
  if (NS_FAILED(rv) || !subDomainURI) {
    return NS_OK;
  }
  // Copy the attributes over
  mozilla::OriginAttributes attrs = OriginAttributesRef();

  if (!StaticPrefs::permissions_isolateBy_userContext()) {
    // Disable userContext for permissions.
    attrs.StripAttributes(mozilla::OriginAttributes::STRIP_USER_CONTEXT_ID);
  }
  RefPtr<nsIPrincipal> principal =
      mozilla::BasePrincipal::CreateContentPrincipal(subDomainURI, attrs);

  if (!principal) {
    return NS_OK;
  }
  principal.forget(aNextSubDomainPrincipal);
  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::GetStorageOriginKey(nsACString& aOriginKey) {
  aOriginKey.Truncate();

  nsCOMPtr<nsIURI> uri;
  nsresult rv = GetURI(getter_AddRefs(uri));
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(uri, NS_ERROR_UNEXPECTED);

  // The special handling of the file scheme should be consistent with
  // GetLocalStorageQuotaKey.

  nsAutoCString domainOrigin;
  rv = uri->GetAsciiHost(domainOrigin);
  NS_ENSURE_SUCCESS(rv, rv);

  if (domainOrigin.IsEmpty()) {
    // For the file:/// protocol use the exact directory as domain.
    if (uri->SchemeIs("file")) {
      nsCOMPtr<nsIURL> url = do_QueryInterface(uri, &rv);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = url->GetDirectory(domainOrigin);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  // Append reversed domain
  nsAutoCString reverseDomain;
  rv = dom::StorageUtils::CreateReversedDomain(domainOrigin, reverseDomain);
  NS_ENSURE_SUCCESS(rv, rv);

  aOriginKey.Append(reverseDomain);

  // Append scheme
  nsAutoCString scheme;
  rv = uri->GetScheme(scheme);
  NS_ENSURE_SUCCESS(rv, rv);

  aOriginKey.Append(':');
  aOriginKey.Append(scheme);

  // Append port if any
  int32_t port = NS_GetRealPort(uri);
  if (port != -1) {
    aOriginKey.Append(nsPrintfCString(":%d", port));
  }

  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::GetIsScriptAllowedByPolicy(bool* aIsScriptAllowedByPolicy) {
  AssertIsOnMainThread();
  *aIsScriptAllowedByPolicy = false;
  nsCOMPtr<nsIURI> prinURI;
  nsresult rv = GetURI(getter_AddRefs(prinURI));
  if (NS_FAILED(rv) || !prinURI) {
    return NS_OK;
  }
  nsIScriptSecurityManager* ssm = nsContentUtils::GetSecurityManager();
  if (!ssm) {
    return NS_ERROR_UNEXPECTED;
  }
  return ssm->PolicyAllowsScript(prinURI, aIsScriptAllowedByPolicy);
}

bool SiteIdentifier::Equals(const SiteIdentifier& aOther) const {
  MOZ_ASSERT(IsInitialized());
  MOZ_ASSERT(aOther.IsInitialized());
  return mPrincipal->FastEquals(aOther.mPrincipal);
}

NS_IMETHODIMP
BasePrincipal::CreateReferrerInfo(mozilla::dom::ReferrerPolicy aReferrerPolicy,
                                  nsIReferrerInfo** _retval) {
  nsCOMPtr<nsIURI> prinURI;
  RefPtr<dom::ReferrerInfo> info;
  nsresult rv = GetURI(getter_AddRefs(prinURI));
  if (NS_FAILED(rv) || !prinURI) {
    info = new dom::ReferrerInfo(nullptr);
    info.forget(_retval);
    return NS_OK;
  }
  info = new dom::ReferrerInfo(prinURI, aReferrerPolicy);
  info.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP
BasePrincipal::GetPrecursorPrincipal(nsIPrincipal** aPrecursor) {
  *aPrecursor = nullptr;
  return NS_OK;
}

NS_IMPL_ADDREF(BasePrincipal::Deserializer)
NS_IMPL_RELEASE(BasePrincipal::Deserializer)

NS_INTERFACE_MAP_BEGIN(BasePrincipal::Deserializer)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_INTERFACE_MAP_ENTRY(nsISerializable)
  if (mPrincipal) {
    return mPrincipal->QueryInterface(aIID, aInstancePtr);
  } else
NS_INTERFACE_MAP_END

NS_IMETHODIMP
BasePrincipal::Deserializer::Write(nsIObjectOutputStream* /*aStream*/) {
  // Read is used still for legacy principals
  MOZ_RELEASE_ASSERT(false, "Old style serialization is removed");
  return NS_OK;
}

/* static */
void BasePrincipal::WriteJSONProperty(JSONWriter& aWriter,
                                      const Span<const char>& aKey,
                                      const nsCString& aValue) {
  aWriter.StringProperty(aKey, aValue);
}

}  // namespace mozilla

uint32_t nsIPrincipal::GetHashValue() const {
  auto* bp = mozilla::BasePrincipal::Cast(this);
  return mozilla::HashGeneric(bp->GetOriginNoSuffixHash(),
                              bp->GetOriginSuffixHash());
}
