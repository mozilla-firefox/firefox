/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AddonContentPolicy.h"

#include "mozilla/dom/nsCSPContext.h"
#include "nsCOMPtr.h"
#include "nsComponentManagerUtils.h"
#include "nsIContent.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/Components.h"
#include "mozilla/dom/Document.h"
#include "mozilla/intl/Localization.h"
#include "nsIEffectiveTLDService.h"
#include "nsIUUIDGenerator.h"
#include "nsIURI.h"
#include "nsNetCID.h"
#include "nsNetUtil.h"
#include "nsSandboxFlags.h"

using namespace mozilla;
using namespace mozilla::intl;

/* Enforces content policies for WebExtension scopes. Currently:
 *
 *  - Checks custom content security policies for sufficiently stringent
 *    script-src and other script-related directives.
 *  - We also used to validate object-src similarly to script-src, but that was
 *    dropped because NPAPI plugins are no longer supported (see bug 1766881).
 */

AddonContentPolicy::AddonContentPolicy() = default;

AddonContentPolicy::~AddonContentPolicy() = default;

NS_IMPL_ISUPPORTS(AddonContentPolicy, nsIAddonContentPolicy)

// CSP Validation:

static const char* allowedSchemes[] = {"blob", "filesystem", nullptr};

static const char* allowedHostSchemes[] = {"http", "https", "moz-extension",
                                           nullptr};

/**
 * Validates a CSP directive to ensure that it is sufficiently stringent.
 * In particular, ensures that:
 *
 *  - No remote sources are allowed other than from https: schemes
 *
 *  - No remote sources specify host wildcards for generic domains
 *    (*.blogspot.com, *.com, *)
 *
 *  - All remote sources and local extension sources specify a host
 *
 *  - No scheme sources are allowed other than blob:, filesystem:,
 *    moz-extension:, and https:
 *
 *  - No keyword sources are allowed other than 'none', 'self', 'unsafe-eval',
 *    and hash sources.
 *
 *  Manifest V3 limits CSP for extension_pages, the script-src and
 *  worker-src directives may only be the following:
 *    - self
 *    - none
 */
class CSPValidator final : public nsCSPSrcVisitor {
 public:
  CSPValidator(nsAString& aURL, CSPDirective aDirective,
               bool aDirectiveRequired = true, uint32_t aPermittedPolicy = 0)
      : mURL(aURL),
        mDirective(CSP_CSPDirectiveToString(aDirective)),
        mPermittedPolicy(aPermittedPolicy),
        mDirectiveRequired(aDirectiveRequired),
        mFoundSelf(false) {
    // Start with the default error message for a missing directive, since no
    // visitors will be called if the directive isn't present.
    mError.SetIsVoid(true);
  }

  // Visitors

  bool visitSchemeSrc(const nsCSPSchemeSrc& src) override {
    nsAutoString scheme;
    src.getScheme(scheme);

    if (SchemeInList(scheme, allowedHostSchemes)) {
      FormatError("csp-error-missing-host"_ns, "scheme"_ns, scheme);
      return false;
    }
    if (!SchemeInList(scheme, allowedSchemes)) {
      FormatError("csp-error-illegal-protocol"_ns, "scheme"_ns, scheme);
      return false;
    }
    return true;
  };

  bool visitHostSrc(const nsCSPHostSrc& src) override {
    nsAutoString scheme, host;

    src.getScheme(scheme);
    src.getHost(host);

    if (scheme.LowerCaseEqualsLiteral("http")) {
      // Allow localhost on http
      if (mPermittedPolicy & nsIAddonContentPolicy::CSP_ALLOW_LOCALHOST &&
          HostIsLocal(host)) {
        return true;
      }
      FormatError("csp-error-illegal-protocol"_ns, "scheme"_ns, scheme);
      return false;
    }
    if (scheme.LowerCaseEqualsLiteral("https")) {
      if (mPermittedPolicy & nsIAddonContentPolicy::CSP_ALLOW_LOCALHOST &&
          HostIsLocal(host)) {
        return true;
      }
      if (!(mPermittedPolicy & nsIAddonContentPolicy::CSP_ALLOW_REMOTE)) {
        FormatError("csp-error-illegal-protocol"_ns, "scheme"_ns, scheme);
        return false;
      }
      if (!HostIsAllowed(host)) {
        FormatError("csp-error-illegal-host-wildcard"_ns, "scheme"_ns, scheme);
        return false;
      }
    } else if (scheme.LowerCaseEqualsLiteral("moz-extension")) {
      // The CSP parser silently converts 'self' keywords to the origin
      // URL, so we need to reconstruct the URL to see if it was present.
      if (!mFoundSelf) {
        nsAutoString url(u"moz-extension://");
        url.Append(host);

        mFoundSelf = url.Equals(mURL);
      }

      if (host.IsEmpty() || host.EqualsLiteral("*")) {
        FormatError("csp-error-missing-host"_ns, "scheme"_ns, scheme);
        return false;
      }
    } else if (!SchemeInList(scheme, allowedSchemes)) {
      FormatError("csp-error-illegal-protocol"_ns, "scheme"_ns, scheme);
      return false;
    }

    return true;
  };

  bool visitKeywordSrc(const nsCSPKeywordSrc& src) override {
    switch (src.getKeyword()) {
      case CSP_NONE:
      case CSP_SELF:
        return true;
      case CSP_WASM_UNSAFE_EVAL:
        if (mPermittedPolicy & nsIAddonContentPolicy::CSP_ALLOW_WASM) {
          return true;
        }
        // fall through to also check CSP_ALLOW_EVAL
        [[fallthrough]];
      case CSP_UNSAFE_EVAL:
        if (mPermittedPolicy & nsIAddonContentPolicy::CSP_ALLOW_EVAL) {
          return true;
        }
        // fall through and produce an illegal-keyword error.
        [[fallthrough]];
      default:
        FormatError(
            "csp-error-illegal-keyword"_ns, "keyword"_ns,
            nsDependentString(CSP_EnumToUTF16Keyword(src.getKeyword())));
        return false;
    }
  };

  bool visitNonceSrc(const nsCSPNonceSrc& src) override {
    FormatError("csp-error-illegal-keyword"_ns, "keyword"_ns, u"'nonce-*'"_ns);
    return false;
  };

  bool visitHashSrc(const nsCSPHashSrc& src) override { return true; };

  bool visitUrlHashSrc(const nsCSPUrlHashSrc& src) override {
    FormatError("csp-error-illegal-keyword"_ns, "keyword"_ns, u"'url-*'"_ns);
    return false;
  };

  // Accessors

  inline nsAString& GetError() {
    if (mError.IsVoid() && mDirectiveRequired) {
      FormatError("csp-error-missing-directive"_ns);
    }

    return mError;
  };

  inline bool FoundSelf() { return mFoundSelf; };

  // Formatters

  inline void FormatError(const nsACString& l10nId,
                          const nsACString& aKey = ""_ns,
                          const nsAString& aValue = u""_ns) {
    nsTArray<nsCString> resIds = {"toolkit/global/cspErrors.ftl"_ns};
    RefPtr<intl::Localization> l10n = intl::Localization::Create(resIds, true);

    AutoTArray<intl::FluentArg, 2> l10nArgs = {
        intl::FluentArg("directive"_ns, mDirective)};
    if (!aKey.IsEmpty()) {
      l10nArgs.AppendElement(intl::FluentArg(aKey, aValue));
    }

    nsAutoCString translation;
    IgnoredErrorResult rv;
    l10n->FormatValueSync(l10nId, l10nArgs, translation, rv);
    if (rv.Failed()) {
      mError.AssignLiteral("An unexpected error occurred");
    } else {
      mError = NS_ConvertUTF8toUTF16(translation);
    }
  };

 private:
  // Validators
  bool HostIsLocal(nsAString& host) {
    return host.EqualsLiteral("localhost") || host.EqualsLiteral("127.0.0.1");
  }

  bool HostIsAllowed(nsAString& host) {
    if (host.First() == '*') {
      if (host.EqualsLiteral("*") || host[1] != '.') {
        return false;
      }

      host.Cut(0, 2);

      nsCOMPtr<nsIEffectiveTLDService> tldService =
          do_GetService(NS_EFFECTIVETLDSERVICE_CONTRACTID);

      if (!tldService) {
        return false;
      }

      NS_ConvertUTF16toUTF8 cHost(host);
      nsAutoCString publicSuffix;

      nsresult rv = tldService->GetPublicSuffixFromHost(cHost, publicSuffix);

      return NS_SUCCEEDED(rv) && !cHost.Equals(publicSuffix);
    }

    return true;
  };

  bool SchemeInList(nsAString& scheme, const char** schemes) {
    for (; *schemes; schemes++) {
      if (scheme.LowerCaseEqualsASCII(*schemes)) {
        return true;
      }
    }
    return false;
  };

  // Data members

  nsAutoString mURL;
  NS_ConvertASCIItoUTF16 mDirective;
  nsString mError;

  uint32_t mPermittedPolicy;
  bool mDirectiveRequired;
  bool mFoundSelf;
};

/**
 * Validates a custom content security policy string for use by an add-on.
 * In particular, ensures that:
 *
 *  - That script-src (or default-src) directive is present, and meets
 *    the policies required by the CSPValidator class
 *
 *  - The script-src directive includes the source 'self'
 */
NS_IMETHODIMP
AddonContentPolicy::ValidateAddonCSP(const nsAString& aPolicyString,
                                     uint32_t aPermittedPolicy,
                                     nsAString& aResult) {
  nsresult rv;

  // Validate against a randomly-generated extension origin.
  // There is no add-on-specific behavior in the CSP code, beyond the ability
  // for add-ons to specify a custom policy, but the parser requires a valid
  // origin in order to operate correctly.
  nsAutoString url(u"moz-extension://");
  {
    nsCOMPtr<nsIUUIDGenerator> uuidgen = components::UUIDGenerator::Service();
    NS_ENSURE_TRUE(uuidgen, NS_ERROR_FAILURE);

    nsID id;
    rv = uuidgen->GenerateUUIDInPlace(&id);
    NS_ENSURE_SUCCESS(rv, rv);

    char idString[NSID_LENGTH];
    id.ToProvidedString(idString);

    MOZ_RELEASE_ASSERT(idString[0] == '{' && idString[NSID_LENGTH - 2] == '}',
                       "UUID generator did not return a valid UUID");

    url.AppendASCII(idString + 1, NSID_LENGTH - 3);
  }

  RefPtr<BasePrincipal> principal =
      BasePrincipal::CreateContentPrincipal(NS_ConvertUTF16toUTF8(url));

  nsCOMPtr<nsIURI> selfURI;
  principal->GetURI(getter_AddRefs(selfURI));
  RefPtr<nsCSPContext> csp = new nsCSPContext();
  rv = csp->SetRequestContextWithPrincipal(principal, selfURI, ""_ns, 0);
  NS_ENSURE_SUCCESS(rv, rv);
  csp->AppendPolicy(aPolicyString, false, false);

  const nsCSPPolicy* policy = csp->GetPolicy(0);
  if (!policy) {
    CSPValidator validator(url, nsIContentSecurityPolicy::SCRIPT_SRC_DIRECTIVE,
                           true, aPermittedPolicy);
    aResult.Assign(validator.GetError());
    return NS_OK;
  }

  bool haveValidDefaultSrc = false;
  bool hasValidScriptSrc = false;
  {
    CSPDirective directive = nsIContentSecurityPolicy::DEFAULT_SRC_DIRECTIVE;
    CSPValidator validator(url, directive);

    haveValidDefaultSrc = policy->visitDirectiveSrcs(directive, &validator);
  }

  aResult.SetIsVoid(true);
  {
    CSPDirective directive = nsIContentSecurityPolicy::SCRIPT_SRC_DIRECTIVE;
    CSPValidator validator(url, directive, !haveValidDefaultSrc,
                           aPermittedPolicy);

    if (!policy->visitDirectiveSrcs(directive, &validator)) {
      aResult.Assign(validator.GetError());
    } else if (!validator.FoundSelf()) {
      validator.FormatError("csp-error-missing-source"_ns, "source"_ns,
                            u"'self'"_ns);
      aResult.Assign(validator.GetError());
    }
    hasValidScriptSrc = true;
  }

  if (aResult.IsVoid()) {
    CSPDirective directive = nsIContentSecurityPolicy::WORKER_SRC_DIRECTIVE;
    CSPValidator validator(url, directive,
                           !haveValidDefaultSrc && !hasValidScriptSrc,
                           aPermittedPolicy);

    if (!policy->visitDirectiveSrcs(directive, &validator)) {
      aResult.Assign(validator.GetError());
    }
  }

  return NS_OK;
}

/**
 * Validates a custom content security policy string for use by sandboxed add-on
 * pages. The CSP must not contain allow-same-origin.
 */
NS_IMETHODIMP
AddonContentPolicy::ValidateAddonSandboxCSP(const nsAString& aPolicyString,
                                            nsAString& aResult) {
  nsresult rv;

  // Validate against a randomly-generated extension origin.
  // There is no add-on-specific behavior in the CSP code, beyond the ability
  // for add-ons to specify a custom policy, but the parser requires a valid
  // origin in order to operate correctly.
  nsAutoString url(u"moz-extension://");
  {
    nsCOMPtr<nsIUUIDGenerator> uuidgen = components::UUIDGenerator::Service();
    NS_ENSURE_TRUE(uuidgen, NS_ERROR_FAILURE);

    nsID id;
    rv = uuidgen->GenerateUUIDInPlace(&id);
    NS_ENSURE_SUCCESS(rv, rv);

    char idString[NSID_LENGTH];
    id.ToProvidedString(idString);

    MOZ_RELEASE_ASSERT(idString[0] == '{' && idString[NSID_LENGTH - 2] == '}',
                       "UUID generator did not return a valid UUID");

    url.AppendASCII(idString + 1, NSID_LENGTH - 3);
  }

  RefPtr<BasePrincipal> principal =
      BasePrincipal::CreateContentPrincipal(NS_ConvertUTF16toUTF8(url));

  nsCOMPtr<nsIURI> selfURI;
  principal->GetURI(getter_AddRefs(selfURI));
  RefPtr<nsCSPContext> csp = new nsCSPContext();
  rv = csp->SetRequestContextWithPrincipal(principal, selfURI, ""_ns, 0);
  NS_ENSURE_SUCCESS(rv, rv);
  csp->AppendPolicy(aPolicyString, false, false);

  uint32_t sandboxFlags = 0;
  rv = csp->GetCSPSandboxFlags(&sandboxFlags);
  NS_ENSURE_SUCCESS(rv, rv);
  // ensure sandbox without allow-same-origin
  if (sandboxFlags & SANDBOXED_ORIGIN) {
    aResult.SetIsVoid(true);
  } else {
    CSPDirective directive = nsIContentSecurityPolicy::SANDBOX_DIRECTIVE;
    CSPValidator validator(url, directive, true,
                           nsIAddonContentPolicy::CSP_ALLOW_ANY);
    // provide more detailed error when allow-same-origin is set
    if (sandboxFlags != SANDBOXED_NONE) {
      validator.FormatError("csp-error-illegal-keyword"_ns, "keyword"_ns,
                            u"'allow-same-origin'"_ns);
    } else {
      MOZ_ASSERT(!validator.GetError().IsVoid(),
                 "CSPValidator should have raised an error.");
    }
    aResult.Assign(validator.GetError());
  }
  return NS_OK;
}
