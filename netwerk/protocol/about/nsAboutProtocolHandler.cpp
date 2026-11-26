/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsAboutProtocolHandler.h"

#include "base/basictypes.h"
#include "mozilla/ipc/URIUtils.h"
#include "nsAboutProtocolUtils.h"
#include "nsContentUtils.h"
#include "nsError.h"
#include "nsIAboutModule.h"
#include "nsIChannel.h"
#include "nsIClassInfoImpl.h"
#include "nsIObjectInputStream.h"
#include "nsIObjectOutputStream.h"
#include "nsIScriptError.h"
#include "nsIURI.h"
#include "nsIWritablePropertyBag2.h"
#include "nsNetCID.h"
#include "nsNetUtil.h"
#include "nsString.h"

namespace mozilla {
namespace net {

static NS_DEFINE_CID(kNestedAboutURICID, NS_NESTEDABOUTURI_CID);

////////////////////////////////////////////////////////////////////////////////

NS_IMPL_ISUPPORTS(nsAboutProtocolHandler, nsIProtocolHandler,
                  nsIProtocolHandlerWithDynamicFlags, nsISupportsWeakReference)

////////////////////////////////////////////////////////////////////////////////
// nsIProtocolHandler methods:

NS_IMETHODIMP
nsAboutProtocolHandler::GetScheme(nsACString& result) {
  result.AssignLiteral("about");
  return NS_OK;
}

NS_IMETHODIMP
nsAboutProtocolHandler::GetFlagsForURI(nsIURI* aURI, uint32_t* aFlags) {
  // First use the default (which is "unsafe for content"):
  *aFlags = URI_NORELATIVE | URI_NOAUTH | URI_DANGEROUS_TO_LOAD |
            URI_SCHEME_NOT_SELF_LINKABLE;

  // Now try to see if this URI overrides the default:
  nsCOMPtr<nsIAboutModule> aboutMod;
  nsresult rv = NS_GetAboutModule(aURI, getter_AddRefs(aboutMod));
  if (NS_FAILED(rv)) {
    // Swallow this and just tell the consumer the default:
    return NS_OK;
  }
  uint32_t aboutModuleFlags = 0;
  rv = aboutMod->GetURIFlags(aURI, &aboutModuleFlags);
  // This should never happen, so pass back the error:
  NS_ENSURE_SUCCESS(rv, rv);

  // Secure (https) pages can load safe about pages without becoming
  // mixed content.
  if (aboutModuleFlags & nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT) {
    *aFlags |= URI_IS_POTENTIALLY_TRUSTWORTHY;
    // about: pages can only be loaded by unprivileged principals
    // if they are marked as LINKABLE
    if (aboutModuleFlags & nsIAboutModule::MAKE_LINKABLE) {
      // Replace URI_DANGEROUS_TO_LOAD with URI_LOADABLE_BY_ANYONE.
      *aFlags &= ~URI_DANGEROUS_TO_LOAD;
      *aFlags |= URI_LOADABLE_BY_ANYONE;
    }
  }
  return NS_OK;
}

// static
nsresult nsAboutProtocolHandler::CreateNewURI(const nsACString& aSpec,
                                              const char* aCharset,
                                              nsIURI* aBaseURI,
                                              nsIURI** aResult) {
  *aResult = nullptr;

  // Use a simple URI to parse out some stuff first
  nsCOMPtr<nsIURI> url;
  MOZ_TRY(
      NS_MutateURI(new nsSimpleURI::Mutator()).SetSpec(aSpec).Finalize(url));

  nsAutoCString name;
  MOZ_TRY(NS_GetAboutModuleName(url, name));

  // The about modules for these URL types have the
  // URI_SAFE_FOR_UNTRUSTED_CONTENT and MAKE_LINKABLE flags set.
  if (name.EqualsLiteral("blank") || name.EqualsLiteral("srcdoc")) {
    // We need to indicate that this baby is safe.  Use an inner URI that
    // no one but the security manager will see.  Make sure to preserve our
    // path, in case someone decides to hardcode checks for particular
    // about: URIs somewhere.
    nsAutoCString spec;
    MOZ_TRY(url->GetPathQueryRef(spec));

    spec.InsertLiteral("moz-safe-about:", 0);

    nsCOMPtr<nsIURI> inner;
    MOZ_TRY(NS_NewURI(getter_AddRefs(inner), spec));

    MOZ_TRY(NS_MutateURI(new nsNestedAboutURI::Mutator())
                .Apply(&nsINestedAboutURIMutator::InitWithBase, inner, aBaseURI)
                .SetSpec(aSpec)
                .Finalize(url));
  }

  url.swap(*aResult);
  return NS_OK;
}

NS_IMETHODIMP
nsAboutProtocolHandler::NewChannel(nsIURI* uri, nsILoadInfo* aLoadInfo,
                                   nsIChannel** result) {
  NS_ENSURE_ARG_POINTER(uri);

  // about:what you ask?
  nsCOMPtr<nsIAboutModule> aboutMod;
  nsresult rv = NS_GetAboutModule(uri, getter_AddRefs(aboutMod));

  nsAutoCString path;
  if (NS_SUCCEEDED(NS_GetAboutModuleName(uri, path)) &&
      path.EqualsLiteral("srcdoc")) {
    // about:srcdoc is meant to be unresolvable, yet is included in the
    // about lookup tables so that it can pass security checks when used in
    // a srcdoc iframe.  To ensure that it stays unresolvable, we pretend
    // that it doesn't exist.
    return NS_ERROR_MALFORMED_URI;
  }

  if (NS_FAILED(rv)) {
    if (rv == NS_ERROR_FACTORY_NOT_REGISTERED) {
      // This looks like an about: we don't know about.  Convert
      // this to an invalid URI error.
      return NS_ERROR_MALFORMED_URI;
    }

    return rv;
  }

  uint32_t flags = 0;
  if (NS_FAILED(aboutMod->GetURIFlags(uri, &flags))) {
    return NS_ERROR_FAILURE;
  }

  bool safeForUntrustedContent =
      (flags & nsIAboutModule::URI_SAFE_FOR_UNTRUSTED_CONTENT) != 0;

  MOZ_DIAGNOSTIC_ASSERT(
      safeForUntrustedContent ||
          (flags & (nsIAboutModule::URI_IS_ERROR_PAGE |
                    nsIAboutModule::URI_MUST_LOAD_IN_CHILD)) == 0,
      "Only unprivileged content should be loaded in child processes. (Did "
      "you forget to add URI_SAFE_FOR_UNTRUSTED_CONTENT to your about: "
      "page?)");

  // The standard return case:
  rv = aboutMod->NewChannel(uri, aLoadInfo, result);
  if (NS_FAILED(rv)) {
    if (rv == NS_ERROR_FACTORY_NOT_REGISTERED) {
      // This looks like an about: we don't know about.  Convert
      // this to an invalid URI error.
      return NS_ERROR_MALFORMED_URI;
    }

    return rv;
  }

  // Not all implementations of nsIAboutModule::NewChannel()
  // set the LoadInfo on the newly created channel yet, as
  // an interim solution we set the LoadInfo here if not
  // available on the channel. Bug 1087720
  nsCOMPtr<nsILoadInfo> loadInfo = (*result)->LoadInfo();
  if (aLoadInfo != loadInfo) {
    NS_ASSERTION(false,
                 "nsIAboutModule->newChannel(aURI, aLoadInfo) needs to "
                 "set LoadInfo");
    AutoTArray<nsString, 2> params = {
        u"nsIAboutModule->newChannel(aURI)"_ns,
        u"nsIAboutModule->newChannel(aURI, aLoadInfo)"_ns};
    nsContentUtils::ReportToConsole(
        nsIScriptError::warningFlag, "Security by Default"_ns,
        nullptr,  // aDocument
        PropertiesFile::NECKO_PROPERTIES, "APIDeprecationWarning", params);
    (*result)->SetLoadInfo(aLoadInfo);
  }

  // If this URI is safe for untrusted content, enforce that its
  // principal be based on the channel's originalURI by setting the
  // owner to null.
  // Note: this relies on aboutMod's newChannel implementation
  // having set the proper originalURI, which probably isn't ideal.
  if (safeForUntrustedContent) {
    (*result)->SetOwner(nullptr);
  }

  RefPtr<nsNestedAboutURI> aboutURI;
  if (NS_SUCCEEDED(
          uri->QueryInterface(kNestedAboutURICID, getter_AddRefs(aboutURI))) &&
      aboutURI->GetBaseURI()) {
    nsCOMPtr<nsIWritablePropertyBag2> writableBag = do_QueryInterface(*result);
    if (writableBag) {
      writableBag->SetPropertyAsInterface(u"baseURI"_ns,
                                          aboutURI->GetBaseURI());
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsAboutProtocolHandler::AllowPort(int32_t port, const char* scheme,
                                  bool* _retval) {
  // don't override anything.
  *_retval = false;
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// Safe about protocol handler impl

NS_IMPL_ISUPPORTS(nsSafeAboutProtocolHandler, nsIProtocolHandler,
                  nsISupportsWeakReference)

// nsIProtocolHandler methods:

NS_IMETHODIMP
nsSafeAboutProtocolHandler::GetScheme(nsACString& result) {
  result.AssignLiteral("moz-safe-about");
  return NS_OK;
}

NS_IMETHODIMP
nsSafeAboutProtocolHandler::NewChannel(nsIURI* uri, nsILoadInfo* aLoadInfo,
                                       nsIChannel** result) {
  *result = nullptr;
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
nsSafeAboutProtocolHandler::AllowPort(int32_t port, const char* scheme,
                                      bool* _retval) {
  // don't override anything.
  *_retval = false;
  return NS_OK;
}

////////////////////////////////////////////////////////////
// nsNestedAboutURI implementation

NS_IMPL_CLASSINFO(nsNestedAboutURI, nullptr, nsIClassInfo::THREADSAFE,
                  NS_NESTEDABOUTURI_CID);
// Empty CI getter. We only need nsIClassInfo for Serialization
NS_IMPL_CI_INTERFACE_GETTER0(nsNestedAboutURI)

NS_INTERFACE_MAP_BEGIN(nsNestedAboutURI)
  if (aIID.Equals(kNestedAboutURICID)) {
    foundInterface = static_cast<nsIURI*>(this);
  } else
    NS_IMPL_QUERY_CLASSINFO(nsNestedAboutURI)
NS_INTERFACE_MAP_END_INHERITING(nsSimpleNestedURI)

// nsISerializable

NS_IMETHODIMP
nsNestedAboutURI::Read(nsIObjectInputStream* aStream) {
  MOZ_ASSERT_UNREACHABLE("Use nsIURIMutator.read() instead");
  return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult nsNestedAboutURI::ReadPrivate(nsIObjectInputStream* aStream) {
  nsresult rv = nsSimpleNestedURI::ReadPrivate(aStream);
  if (NS_FAILED(rv)) return rv;

  bool haveBase;
  rv = aStream->ReadBoolean(&haveBase);
  if (NS_FAILED(rv)) return rv;

  if (haveBase) {
    nsCOMPtr<nsISupports> supports;
    rv = aStream->ReadObject(true, getter_AddRefs(supports));
    if (NS_FAILED(rv)) return rv;

    mBaseURI = do_QueryInterface(supports, &rv);
    if (NS_FAILED(rv)) return rv;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsNestedAboutURI::Write(nsIObjectOutputStream* aStream) {
  nsresult rv = nsSimpleNestedURI::Write(aStream);
  if (NS_FAILED(rv)) return rv;

  rv = aStream->WriteBoolean(mBaseURI != nullptr);
  if (NS_FAILED(rv)) return rv;

  if (mBaseURI) {
    // A previous iteration of this code wrote out mBaseURI as nsISupports
    // and then read it in as nsIURI, which is non-kosher when mBaseURI
    // implements more than just a single line of interfaces and the
    // canonical nsISupports* isn't the one a static_cast<> of mBaseURI
    // would produce.  For backwards compatibility with existing
    // serializations we continue to write mBaseURI as nsISupports but
    // switch to reading it as nsISupports, with a post-read QI to get to
    // nsIURI.
    rv = aStream->WriteCompoundObject(mBaseURI, NS_GET_IID(nsISupports), true);
    if (NS_FAILED(rv)) return rv;
  }

  return NS_OK;
}

void nsNestedAboutURI::Serialize(mozilla::ipc::URIParams& aParams) {
  using namespace mozilla::ipc;

  NestedAboutURIParams params;
  URIParams nestedParams;

  nsSimpleNestedURI::Serialize(nestedParams);
  params.nestedParams() = nestedParams;

  if (mBaseURI) {
    SerializeURI(mBaseURI, params.baseURI());
  }

  aParams = params;
}

bool nsNestedAboutURI::Deserialize(const mozilla::ipc::URIParams& aParams) {
  using namespace mozilla::ipc;

  if (aParams.type() != URIParams::TNestedAboutURIParams) {
    NS_ERROR("Received unknown parameters from the other process!");
    return false;
  }

  const NestedAboutURIParams& params = aParams.get_NestedAboutURIParams();
  if (!nsSimpleNestedURI::Deserialize(params.nestedParams())) {
    return false;
  }

  mBaseURI = nullptr;
  if (params.baseURI()) {
    mBaseURI = DeserializeURI(*params.baseURI());
  }
  return true;
}

bool nsNestedAboutURI::IsValidInnerURI(nsIURI* aInnerURI) {
  if (!Scheme().EqualsLiteral("about")) {
    return false;
  }

  if (!NS_IsContentAccessibleAboutURI(this)) {
    return false;
  }

  nsAutoCString expectedSpec;
  if (NS_FAILED(GetPathQueryRef(expectedSpec))) {
    return false;
  }
  expectedSpec.InsertLiteral("moz-safe-about:", 0);

  nsAutoCString innerSpec;
  if (NS_FAILED(aInnerURI->GetAsciiSpec(innerSpec))) {
    return false;
  }

  return innerSpec == expectedSpec;
}

// nsSimpleURI
/* virtual */ already_AddRefed<nsSimpleURI> nsNestedAboutURI::StartClone() {
  NS_ENSURE_TRUE(mInnerURI, nullptr);

  RefPtr<nsNestedAboutURI> url = new nsNestedAboutURI(mInnerURI, mBaseURI);

  return url.forget();
}

// Queries this list of interfaces. If none match, it queries mURI.
NS_IMPL_NSIURIMUTATOR_ISUPPORTS(nsNestedAboutURI::Mutator, nsIURISetters,
                                nsIURIMutator, nsISerializable,
                                nsINestedAboutURIMutator)

NS_IMETHODIMP
nsNestedAboutURI::Mutate(nsIURIMutator** aMutator) {
  RefPtr<nsNestedAboutURI::Mutator> mutator = new nsNestedAboutURI::Mutator();
  nsresult rv = mutator->InitFromURI(this);
  if (NS_FAILED(rv)) {
    return rv;
  }
  mutator.forget(aMutator);
  return NS_OK;
}

}  // namespace net
}  // namespace mozilla
