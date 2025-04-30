/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsAttrValue.h"
#include "nsCharSeparatedTokenizer.h"
#include "nsContentUtils.h"
#include "nsCSPUtils.h"
#include "nsDebug.h"
#include "nsCSPParser.h"
#include "nsComponentManagerUtils.h"
#include "nsIConsoleService.h"
#include "nsIContentSecurityPolicy.h"
#include "nsIChannel.h"
#include "nsICryptoHash.h"
#include "nsIScriptError.h"
#include "nsIStringBundle.h"
#include "nsIURL.h"
#include "nsNetUtil.h"
#include "nsReadableUtils.h"
#include "nsSandboxFlags.h"
#include "nsServiceManagerUtils.h"
#include "nsWhitespaceTokenizer.h"

#include "mozilla/Assertions.h"
#include "mozilla/Components.h"
#include "mozilla/dom/CSPDictionariesBinding.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/SRIMetadata.h"
#include "mozilla/dom/TrustedTypesConstants.h"
#include "mozilla/StaticPrefs_security.h"

using namespace mozilla;
using mozilla::dom::SRIMetadata;

#define DEFAULT_PORT -1

static mozilla::LogModule* GetCspUtilsLog() {
  static mozilla::LazyLogModule gCspUtilsPRLog("CSPUtils");
  return gCspUtilsPRLog;
}

#define CSPUTILSLOG(args) \
  MOZ_LOG(GetCspUtilsLog(), mozilla::LogLevel::Debug, args)
#define CSPUTILSLOGENABLED() \
  MOZ_LOG_TEST(GetCspUtilsLog(), mozilla::LogLevel::Debug)

void CSP_PercentDecodeStr(const nsAString& aEncStr, nsAString& outDecStr) {
  outDecStr.Truncate();

  // helper function that should not be visible outside this methods scope
  struct local {
    static inline char16_t convertHexDig(char16_t aHexDig) {
      if (isNumberToken(aHexDig)) {
        return aHexDig - '0';
      }
      if (aHexDig >= 'A' && aHexDig <= 'F') {
        return aHexDig - 'A' + 10;
      }
      // must be a lower case character
      // (aHexDig >= 'a' && aHexDig <= 'f')
      return aHexDig - 'a' + 10;
    }
  };

  const char16_t *cur, *end, *hexDig1, *hexDig2;
  cur = aEncStr.BeginReading();
  end = aEncStr.EndReading();

  while (cur != end) {
    // if it's not a percent sign then there is
    // nothing to do for that character
    if (*cur != PERCENT_SIGN) {
      outDecStr.Append(*cur);
      cur++;
      continue;
    }

    // get the two hexDigs following the '%'-sign
    hexDig1 = cur + 1;
    hexDig2 = cur + 2;

    // if there are no hexdigs after the '%' then
    // there is nothing to do for us.
    if (hexDig1 == end || hexDig2 == end || !isValidHexDig(*hexDig1) ||
        !isValidHexDig(*hexDig2)) {
      outDecStr.Append(PERCENT_SIGN);
      cur++;
      continue;
    }

    // decode "% hexDig1 hexDig2" into a character.
    char16_t decChar =
        (local::convertHexDig(*hexDig1) << 4) + local::convertHexDig(*hexDig2);
    outDecStr.Append(decChar);

    // increment 'cur' to after the second hexDig
    cur = ++hexDig2;
  }
}

// The Content Security Policy should be inherited for
// local schemes like: "about", "blob", "data", or "filesystem".
// see: https://w3c.github.io/webappsec-csp/#initialize-document-csp
bool CSP_ShouldResponseInheritCSP(nsIChannel* aChannel) {
  if (!aChannel) {
    return false;
  }

  nsCOMPtr<nsIURI> uri;
  nsresult rv = aChannel->GetURI(getter_AddRefs(uri));
  NS_ENSURE_SUCCESS(rv, false);

  bool isAbout = uri->SchemeIs("about");
  if (isAbout) {
    nsAutoCString aboutSpec;
    rv = uri->GetSpec(aboutSpec);
    NS_ENSURE_SUCCESS(rv, false);
    // also allow about:blank#foo
    if (StringBeginsWith(aboutSpec, "about:blank"_ns) ||
        StringBeginsWith(aboutSpec, "about:srcdoc"_ns)) {
      return true;
    }
  }

  return uri->SchemeIs("blob") || uri->SchemeIs("data") ||
         uri->SchemeIs("filesystem") || uri->SchemeIs("javascript");
}

void CSP_ApplyMetaCSPToDoc(mozilla::dom::Document& aDoc,
                           const nsAString& aPolicyStr) {
  if (aDoc.IsLoadedAsData()) {
    return;
  }

  nsAutoString policyStr(
      nsContentUtils::TrimWhitespace<nsContentUtils::IsHTMLWhitespace>(
          aPolicyStr));

  if (policyStr.IsEmpty()) {
    return;
  }

  nsCOMPtr<nsIContentSecurityPolicy> csp = aDoc.GetCsp();
  if (!csp) {
    MOZ_ASSERT(false, "how come there is no CSP");
    return;
  }

  // CSPs delivered via a <meta> tag can not be report-only.
  bool reportOnly = false;

  if (nsIURI* uri = aDoc.GetDocumentURI(); CSP_IsBrowserXHTML(uri)) {
    // Make the <meta> policy in browser.xhtml toggleable.
    if (!StaticPrefs::security_browser_xhtml_csp_enabled()) {
      return;
    }

    // Make the policy report-only to be able to collect telemetry.
    if (StaticPrefs::security_browser_xhtml_csp_report_only()) {
      reportOnly = true;
    }
  }

  // Multiple CSPs (delivered through either header of meta tag) need to
  // be joined together, see:
  // https://w3c.github.io/webappsec/specs/content-security-policy/#delivery-html-meta-element
  nsresult rv = csp->AppendPolicy(policyStr, reportOnly,
                                  true);  // delivered through the meta tag
  NS_ENSURE_SUCCESS_VOID(rv);
  if (nsPIDOMWindowInner* inner = aDoc.GetInnerWindow()) {
    inner->SetCsp(csp);
  }
  aDoc.ApplySettingsFromCSP(false);
}

bool CSP_IsBrowserXHTML(nsIURI* aURI) {
  if (!aURI->SchemeIs("chrome")) {
    return false;
  }

  nsAutoCString spec;
  aURI->GetSpec(spec);
  return spec.EqualsLiteral("chrome://browser/content/browser.xhtml");
}

void CSP_GetLocalizedStr(const char* aName, const nsTArray<nsString>& aParams,
                         nsAString& outResult) {
  nsCOMPtr<nsIStringBundle> keyStringBundle;
  nsCOMPtr<nsIStringBundleService> stringBundleService =
      mozilla::components::StringBundle::Service();

  NS_ASSERTION(stringBundleService, "String bundle service must be present!");
  stringBundleService->CreateBundle(
      "chrome://global/locale/security/csp.properties",
      getter_AddRefs(keyStringBundle));

  NS_ASSERTION(keyStringBundle, "Key string bundle must be available!");

  if (!keyStringBundle) {
    return;
  }

  if (aParams.IsEmpty()) {
    keyStringBundle->GetStringFromName(aName, outResult);
  } else {
    keyStringBundle->FormatStringFromName(aName, aParams, outResult);
  }
}

void CSP_LogStrMessage(const nsAString& aMsg) {
  nsCOMPtr<nsIConsoleService> console(
      do_GetService("@mozilla.org/consoleservice;1"));

  if (!console) {
    return;
  }
  nsString msg(aMsg);
  console->LogStringMessage(msg.get());
}

void CSP_LogMessage(const nsAString& aMessage, const nsACString& aSourceName,
                    const nsAString& aSourceLine, uint32_t aLineNumber,
                    uint32_t aColumnNumber, uint32_t aFlags,
                    const nsACString& aCategory, uint64_t aInnerWindowID,
                    bool aFromPrivateWindow) {
  nsCOMPtr<nsIConsoleService> console(
      do_GetService(NS_CONSOLESERVICE_CONTRACTID));

  nsCOMPtr<nsIScriptError> error(do_CreateInstance(NS_SCRIPTERROR_CONTRACTID));

  if (!console || !error) {
    return;
  }

  // Prepending CSP to the outgoing console message
  nsString cspMsg;
  CSP_GetLocalizedStr("CSPMessagePrefix",
                      AutoTArray<nsString, 1>{nsString(aMessage)}, cspMsg);

  // Currently 'aSourceLine' is not logged to the console, because similar
  // information is already included within the source link of the message.
  // For inline violations however, the line and column number are 0 and
  // information contained within 'aSourceLine' can be really useful for devs.
  // E.g. 'aSourceLine' might be: 'onclick attribute on DIV element'.
  // In such cases we append 'aSourceLine' directly to the error message.
  if (!aSourceLine.IsEmpty() && aLineNumber == 0) {
    cspMsg.AppendLiteral(u"\nSource: ");
    cspMsg.Append(aSourceLine);
  }

  // Since we are leveraging csp errors as the category names which
  // we pass to devtools, we should prepend them with "CSP_" to
  // allow easy distincution in devtools code. e.g.
  // upgradeInsecureRequest -> CSP_upgradeInsecureRequest
  nsCString category("CSP_");
  category.Append(aCategory);

  nsresult rv;
  if (aInnerWindowID > 0) {
    rv =
        error->InitWithWindowID(cspMsg, aSourceName, aLineNumber, aColumnNumber,
                                aFlags, category, aInnerWindowID);
  } else {
    rv = error->Init(cspMsg, aSourceName, aLineNumber, aColumnNumber, aFlags,
                     category, aFromPrivateWindow,
                     true /* from chrome context */);
  }
  if (NS_FAILED(rv)) {
    return;
  }
  console->LogMessage(error);
}

CSPDirective CSP_StringToCSPDirective(const nsAString& aDir) {
  nsString lowerDir = PromiseFlatString(aDir);
  ToLowerCase(lowerDir);

  uint32_t numDirs = (sizeof(CSPStrDirectives) / sizeof(CSPStrDirectives[0]));

  for (uint32_t i = 1; i < numDirs; i++) {
    if (lowerDir.EqualsASCII(CSPStrDirectives[i])) {
      return static_cast<CSPDirective>(i);
    }
  }
  return nsIContentSecurityPolicy::NO_DIRECTIVE;
}

/**
 * Combines CSP_LogMessage and CSP_GetLocalizedStr into one call.
 */
void CSP_LogLocalizedStr(const char* aName, const nsTArray<nsString>& aParams,
                         const nsACString& aSourceName,
                         const nsAString& aSourceLine, uint32_t aLineNumber,
                         uint32_t aColumnNumber, uint32_t aFlags,
                         const nsACString& aCategory, uint64_t aInnerWindowID,
                         bool aFromPrivateWindow) {
  nsAutoString logMsg;
  CSP_GetLocalizedStr(aName, aParams, logMsg);
  CSP_LogMessage(logMsg, aSourceName, aSourceLine, aLineNumber, aColumnNumber,
                 aFlags, aCategory, aInnerWindowID, aFromPrivateWindow);
}

/* ===== Helpers ============================ */
// This implements
// https://w3c.github.io/webappsec-csp/#effective-directive-for-a-request.
// However the spec doesn't currently cover all request destinations, which
// we roughly represent using nsContentPolicyType.
CSPDirective CSP_ContentTypeToDirective(nsContentPolicyType aType) {
  switch (aType) {
    case nsIContentPolicy::TYPE_IMAGE:
    case nsIContentPolicy::TYPE_IMAGESET:
    case nsIContentPolicy::TYPE_INTERNAL_IMAGE:
    case nsIContentPolicy::TYPE_INTERNAL_IMAGE_PRELOAD:
    case nsIContentPolicy::TYPE_INTERNAL_IMAGE_FAVICON:
    case nsIContentPolicy::TYPE_INTERNAL_EXTERNAL_RESOURCE:
      return nsIContentSecurityPolicy::IMG_SRC_DIRECTIVE;

    // BLock XSLT as script, see bug 910139
    case nsIContentPolicy::TYPE_XSLT:
    case nsIContentPolicy::TYPE_SCRIPT:
    case nsIContentPolicy::TYPE_INTERNAL_SCRIPT:
    case nsIContentPolicy::TYPE_INTERNAL_SCRIPT_PRELOAD:
    case nsIContentPolicy::TYPE_INTERNAL_MODULE:
    case nsIContentPolicy::TYPE_INTERNAL_MODULE_PRELOAD:
    case nsIContentPolicy::TYPE_INTERNAL_WORKER_IMPORT_SCRIPTS:
    case nsIContentPolicy::TYPE_INTERNAL_AUDIOWORKLET:
    case nsIContentPolicy::TYPE_INTERNAL_PAINTWORKLET:
    case nsIContentPolicy::TYPE_INTERNAL_CHROMEUTILS_COMPILED_SCRIPT:
    case nsIContentPolicy::TYPE_INTERNAL_FRAME_MESSAGEMANAGER_SCRIPT:
      // (https://github.com/w3c/webappsec-csp/issues/554)
      // Some of these types are not explicitly defined in the spec.
      //
      // Chrome seems to use script-src-elem for worklet!
      return nsIContentSecurityPolicy::SCRIPT_SRC_ELEM_DIRECTIVE;

    case nsIContentPolicy::TYPE_STYLESHEET:
    case nsIContentPolicy::TYPE_INTERNAL_STYLESHEET:
    case nsIContentPolicy::TYPE_INTERNAL_STYLESHEET_PRELOAD:
      return nsIContentSecurityPolicy::STYLE_SRC_ELEM_DIRECTIVE;

    case nsIContentPolicy::TYPE_FONT:
    case nsIContentPolicy::TYPE_INTERNAL_FONT_PRELOAD:
      return nsIContentSecurityPolicy::FONT_SRC_DIRECTIVE;

    case nsIContentPolicy::TYPE_MEDIA:
    case nsIContentPolicy::TYPE_INTERNAL_AUDIO:
    case nsIContentPolicy::TYPE_INTERNAL_VIDEO:
    case nsIContentPolicy::TYPE_INTERNAL_TRACK:
      return nsIContentSecurityPolicy::MEDIA_SRC_DIRECTIVE;

    case nsIContentPolicy::TYPE_WEB_MANIFEST:
      return nsIContentSecurityPolicy::WEB_MANIFEST_SRC_DIRECTIVE;

    case nsIContentPolicy::TYPE_INTERNAL_WORKER:
    case nsIContentPolicy::TYPE_INTERNAL_WORKER_STATIC_MODULE:
    case nsIContentPolicy::TYPE_INTERNAL_SHARED_WORKER:
    case nsIContentPolicy::TYPE_INTERNAL_SERVICE_WORKER:
      return nsIContentSecurityPolicy::WORKER_SRC_DIRECTIVE;

    case nsIContentPolicy::TYPE_SUBDOCUMENT:
    case nsIContentPolicy::TYPE_INTERNAL_FRAME:
    case nsIContentPolicy::TYPE_INTERNAL_IFRAME:
      return nsIContentSecurityPolicy::FRAME_SRC_DIRECTIVE;

    case nsIContentPolicy::TYPE_WEBSOCKET:
    case nsIContentPolicy::TYPE_XMLHTTPREQUEST:
    case nsIContentPolicy::TYPE_BEACON:
    case nsIContentPolicy::TYPE_PING:
    case nsIContentPolicy::TYPE_FETCH:
    case nsIContentPolicy::TYPE_INTERNAL_XMLHTTPREQUEST_ASYNC:
    case nsIContentPolicy::TYPE_INTERNAL_XMLHTTPREQUEST_SYNC:
    case nsIContentPolicy::TYPE_INTERNAL_EVENTSOURCE:
    case nsIContentPolicy::TYPE_INTERNAL_FETCH_PRELOAD:
    case nsIContentPolicy::TYPE_WEB_IDENTITY:
    case nsIContentPolicy::TYPE_WEB_TRANSPORT:
    case nsIContentPolicy::TYPE_JSON:
    case nsIContentPolicy::TYPE_INTERNAL_JSON_PRELOAD:
      return nsIContentSecurityPolicy::CONNECT_SRC_DIRECTIVE;

    case nsIContentPolicy::TYPE_OBJECT:
    case nsIContentPolicy::TYPE_OBJECT_SUBREQUEST:
    case nsIContentPolicy::TYPE_INTERNAL_EMBED:
    case nsIContentPolicy::TYPE_INTERNAL_OBJECT:
      return nsIContentSecurityPolicy::OBJECT_SRC_DIRECTIVE;

    case nsIContentPolicy::TYPE_DTD:
    case nsIContentPolicy::TYPE_OTHER:
    case nsIContentPolicy::TYPE_SPECULATIVE:
    case nsIContentPolicy::TYPE_INTERNAL_DTD:
    case nsIContentPolicy::TYPE_INTERNAL_FORCE_ALLOWED_DTD:
      return nsIContentSecurityPolicy::DEFAULT_SRC_DIRECTIVE;

    // CSP does not apply to webrtc connections
    case nsIContentPolicy::TYPE_PROXIED_WEBRTC_MEDIA:
    // csp shold not block top level loads, e.g. in case
    // of a redirect.
    case nsIContentPolicy::TYPE_DOCUMENT:
    // CSP can not block csp reports
    case nsIContentPolicy::TYPE_CSP_REPORT:
      return nsIContentSecurityPolicy::NO_DIRECTIVE;

    case nsIContentPolicy::TYPE_SAVEAS_DOWNLOAD:
    case nsIContentPolicy::TYPE_UA_FONT:
      return nsIContentSecurityPolicy::NO_DIRECTIVE;

    // Fall through to error for all other directives
    case nsIContentPolicy::TYPE_INVALID:
    case nsIContentPolicy::TYPE_END:
      MOZ_ASSERT(false, "Can not map nsContentPolicyType to CSPDirective");
      // Do not add default: so that compilers can catch the missing case.
  }
  return nsIContentSecurityPolicy::DEFAULT_SRC_DIRECTIVE;
}

already_AddRefed<nsIContentSecurityPolicy> CSP_CreateFromHeader(const nsAString& aHeaderValue, nsIURI* aSelfURI,
                              nsIPrincipal* aLoadingPrincipal,
                              ErrorResult& aRv) {
  RefPtr<nsCSPContext> csp = new nsCSPContext();
  // Hard code some default values until we have a use case where we can provide
  // something else.
  // When inheriting from this CSP, these values will be overwritten anyway.
  aRv = csp->SetRequestContextWithPrincipal(aLoadingPrincipal, aSelfURI,
                                                    /* aReferrer */ ""_ns,
                                                    /* aInnerWindowId */ 0);
  if (aRv.Failed()) {
    return nullptr;
  }

  aRv = CSP_AppendCSPFromHeader(csp, aHeaderValue, /* aReportOnly */ false);
  if (aRv.Failed()) {
    return nullptr;
  }

  return csp.forget();
}

nsCSPHostSrc* CSP_CreateHostSrcFromSelfURI(nsIURI* aSelfURI) {
  // Create the host first
  nsCString host;
  aSelfURI->GetAsciiHost(host);
  nsCSPHostSrc* hostsrc = new nsCSPHostSrc(NS_ConvertUTF8toUTF16(host));
  hostsrc->setGeneratedFromSelfKeyword();

  // Add the scheme.
  nsCString scheme;
  aSelfURI->GetScheme(scheme);
  hostsrc->setScheme(NS_ConvertUTF8toUTF16(scheme));

  // An empty host (e.g. for data:) indicates it's effectively a unique origin.
  // Please note that we still need to set the scheme on hostsrc (see above),
  // because it's used for reporting.
  if (host.EqualsLiteral("")) {
    hostsrc->setIsUniqueOrigin();
    // no need to query the port in that case.
    return hostsrc;
  }

  int32_t port;
  aSelfURI->GetPort(&port);
  // Only add port if it's not default port.
  if (port > 0) {
    nsAutoString portStr;
    portStr.AppendInt(port);
    hostsrc->setPort(portStr);
  }
  return hostsrc;
}

bool CSP_IsEmptyDirective(const nsAString& aValue, const nsAString& aDir) {
  return (aDir.Length() == 0 && aValue.Length() == 0);
}
bool CSP_IsDirective(const nsAString& aValue, CSPDirective aDir) {
  return aValue.LowerCaseEqualsASCII(CSP_CSPDirectiveToString(aDir));
}

bool CSP_IsKeyword(const nsAString& aValue, enum CSPKeyword aKey) {
  return aValue.LowerCaseEqualsASCII(CSP_EnumToUTF8Keyword(aKey));
}

bool CSP_IsQuotelessKeyword(const nsAString& aKey) {
  nsString lowerKey;
  ToLowerCase(aKey, lowerKey);

  nsAutoString keyword;
  for (uint32_t i = 0; i < CSP_LAST_KEYWORD_VALUE; i++) {
    // skipping the leading ' and trimming the trailing '
    keyword.AssignASCII(gCSPUTF8Keywords[i] + 1);
    keyword.Trim("'", false, true);
    if (lowerKey.Equals(keyword)) {
      return true;
    }
  }
  return false;
}

/*
 * Checks whether the current directive permits a specific
 * scheme. This function is called from nsCSPSchemeSrc() and
 * also nsCSPHostSrc.
 * @param aEnforcementScheme
 *        The scheme that this directive allows
 * @param aUri
 *        The uri of the subresource load.
 * @param aReportOnly
 *        Whether the enforced policy is report only or not.
 * @param aUpgradeInsecure
 *        Whether the policy makes use of the directive
 *        'upgrade-insecure-requests'.
 * @param aFromSelfURI
 *        Whether a scheme was generated from the keyword 'self'
 *        which then allows schemeless sources to match ws and wss.
 */

bool permitsScheme(const nsAString& aEnforcementScheme, nsIURI* aUri,
                   bool aReportOnly, bool aUpgradeInsecure, bool aFromSelfURI) {
  nsAutoCString scheme;
  nsresult rv = aUri->GetScheme(scheme);
  NS_ENSURE_SUCCESS(rv, false);

  // no scheme to enforce, let's allow the load (e.g. script-src *)
  if (aEnforcementScheme.IsEmpty()) {
    return true;
  }

  // if the scheme matches, all good - allow the load
  if (aEnforcementScheme.EqualsASCII(scheme.get())) {
    return true;
  }

  // allow scheme-less sources where the protected resource is http
  // and the load is https, see:
  // http://www.w3.org/TR/CSP2/#match-source-expression
  if (aEnforcementScheme.EqualsASCII("http")) {
    if (scheme.EqualsASCII("https")) {
      return true;
    }
    if ((scheme.EqualsASCII("ws") || scheme.EqualsASCII("wss")) &&
        aFromSelfURI) {
      return true;
    }
  }
  if (aEnforcementScheme.EqualsASCII("https")) {
    if (scheme.EqualsLiteral("wss") && aFromSelfURI) {
      return true;
    }
  }
  if (aEnforcementScheme.EqualsASCII("ws") && scheme.EqualsASCII("wss")) {
    return true;
  }

  // Allow the load when enforcing upgrade-insecure-requests with the
  // promise the request gets upgraded from http to https and ws to wss.
  // See nsHttpChannel::Connect() and also WebSocket.cpp. Please note,
  // the report only policies should not allow the load and report
  // the error back to the page.
  return (
      (aUpgradeInsecure && !aReportOnly) &&
      ((scheme.EqualsASCII("http") &&
        aEnforcementScheme.EqualsASCII("https")) ||
       (scheme.EqualsASCII("ws") && aEnforcementScheme.EqualsASCII("wss"))));
}

/*
 * A helper function for appending a CSP header to an existing CSP
 * policy.
 *
 * @param aCsp           the CSP policy
 * @param aHeaderValue   the header
 * @param aReportOnly    is this a report-only header?
 */

nsresult CSP_AppendCSPFromHeader(nsIContentSecurityPolicy* aCsp,
                                 const nsAString& aHeaderValue,
                                 bool aReportOnly) {
  NS_ENSURE_ARG(aCsp);

  // Need to tokenize the header value since multiple headers could be
  // concatenated into one comma-separated list of policies.
  // See RFC2616 section 4.2 (last paragraph)
  nsresult rv = NS_OK;
  for (const nsAString& policy :
       nsCharSeparatedTokenizer(aHeaderValue, ',').ToRange()) {
    rv = aCsp->AppendPolicy(policy, aReportOnly, false);
    NS_ENSURE_SUCCESS(rv, rv);
    {
      CSPUTILSLOG(("CSP refined with policy: \"%s\"",
                   NS_ConvertUTF16toUTF8(policy).get()));
    }
  }
  return NS_OK;
}

/* ===== nsCSPSrc ============================ */

nsCSPBaseSrc::nsCSPBaseSrc() {}

nsCSPBaseSrc::~nsCSPBaseSrc() = default;

// ::permits is only called for external load requests, therefore:
// nsCSPKeywordSrc and nsCSPHashSource fall back to this base class
// implementation which will never allow the load.
bool nsCSPBaseSrc::permits(nsIURI* aUri, bool aWasRedirected, bool aReportOnly,
                           bool aUpgradeInsecure) const {
  if (CSPUTILSLOGENABLED()) {
    CSPUTILSLOG(
        ("nsCSPBaseSrc::permits, aUri: %s", aUri->GetSpecOrDefault().get()));
  }
  return false;
}

// ::allows is only called for inlined loads, therefore:
// nsCSPSchemeSrc, nsCSPHostSrc fall back
// to this base class implementation which will never allow the load.
bool nsCSPBaseSrc::allows(enum CSPKeyword aKeyword,
                          const nsAString& aHashOrNonce) const {
  CSPUTILSLOG(("nsCSPBaseSrc::allows, aKeyWord: %s, a HashOrNonce: %s",
               aKeyword == CSP_HASH ? "hash" : CSP_EnumToUTF8Keyword(aKeyword),
               NS_ConvertUTF16toUTF8(aHashOrNonce).get()));
  return false;
}

/* ====== nsCSPSchemeSrc ===================== */

nsCSPSchemeSrc::nsCSPSchemeSrc(const nsAString& aScheme) : mScheme(aScheme) {
  ToLowerCase(mScheme);
}

nsCSPSchemeSrc::~nsCSPSchemeSrc() = default;

bool nsCSPSchemeSrc::permits(nsIURI* aUri, bool aWasRedirected,
                             bool aReportOnly, bool aUpgradeInsecure) const {
  if (CSPUTILSLOGENABLED()) {
    CSPUTILSLOG(
        ("nsCSPSchemeSrc::permits, aUri: %s", aUri->GetSpecOrDefault().get()));
  }
  MOZ_ASSERT((!mScheme.EqualsASCII("")), "scheme can not be the empty string");
  return permitsScheme(mScheme, aUri, aReportOnly, aUpgradeInsecure, false);
}

bool nsCSPSchemeSrc::visit(nsCSPSrcVisitor* aVisitor) const {
  return aVisitor->visitSchemeSrc(*this);
}

void nsCSPSchemeSrc::toString(nsAString& outStr) const {
  outStr.Append(mScheme);
  outStr.AppendLiteral(":");
}

/* ===== nsCSPHostSrc ======================== */

nsCSPHostSrc::nsCSPHostSrc(const nsAString& aHost)
    : mHost(aHost),
      mGeneratedFromSelfKeyword(false),
      mIsUniqueOrigin(false),
      mWithinFrameAncstorsDir(false) {
  ToLowerCase(mHost);
}

nsCSPHostSrc::~nsCSPHostSrc() = default;

/*
 * Checks whether the current directive permits a specific port.
 * @param aEnforcementScheme
 *        The scheme that this directive allows
 *        (used to query the default port for that scheme)
 * @param aEnforcementPort
 *        The port that this directive allows
 * @param aResourceURI
 *        The uri of the subresource load
 */
bool permitsPort(const nsAString& aEnforcementScheme,
                 const nsAString& aEnforcementPort, nsIURI* aResourceURI) {
  // If enforcement port is the wildcard, don't block the load.
  if (aEnforcementPort.EqualsASCII("*")) {
    return true;
  }

  int32_t resourcePort;
  nsresult rv = aResourceURI->GetPort(&resourcePort);
  if (NS_FAILED(rv) && aEnforcementPort.IsEmpty()) {
    // If we cannot get a Port (e.g. because of an Custom Protocol handler)
    // We need to check if a default port is associated with the Scheme
    if (aEnforcementScheme.IsEmpty()) {
      return false;
    }
    int defaultPortforScheme =
        NS_GetDefaultPort(NS_ConvertUTF16toUTF8(aEnforcementScheme).get());

    // If there is no default port associated with the Scheme (
    // defaultPortforScheme == -1) or it is an externally handled protocol (
    // defaultPortforScheme == 0 ) and the csp does not enforce a port - we can
    // allow not having a port
    return (defaultPortforScheme == -1 || defaultPortforScheme == -0);
  }
  // Avoid unnecessary string creation/manipulation and don't block the
  // load if the resource to be loaded uses the default port for that
  // scheme and there is no port to be enforced.
  // Note, this optimization relies on scheme checks within permitsScheme().
  if (resourcePort == DEFAULT_PORT && aEnforcementPort.IsEmpty()) {
    return true;
  }

  // By now we know at that either the resourcePort does not use the default
  // port or there is a port restriction to be enforced. A port value of -1
  // corresponds to the protocol's default port (eg. -1 implies port 80 for
  // http URIs), in such a case we have to query the default port of the
  // resource to be loaded.
  if (resourcePort == DEFAULT_PORT) {
    nsAutoCString resourceScheme;
    rv = aResourceURI->GetScheme(resourceScheme);
    NS_ENSURE_SUCCESS(rv, false);
    resourcePort = NS_GetDefaultPort(resourceScheme.get());
  }

  // If there is a port to be enforced and the ports match, then
  // don't block the load.
  nsString resourcePortStr;
  resourcePortStr.AppendInt(resourcePort);
  if (aEnforcementPort.Equals(resourcePortStr)) {
    return true;
  }

  // If there is no port to be enforced, query the default port for the load.
  nsString enforcementPort(aEnforcementPort);
  if (enforcementPort.IsEmpty()) {
    // For scheme less sources, our parser always generates a scheme
    // which is the scheme of the protected resource.
    MOZ_ASSERT(!aEnforcementScheme.IsEmpty(),
               "need a scheme to query default port");
    int32_t defaultEnforcementPort =
        NS_GetDefaultPort(NS_ConvertUTF16toUTF8(aEnforcementScheme).get());
    enforcementPort.Truncate();
    enforcementPort.AppendInt(defaultEnforcementPort);
  }

  // If default ports match, don't block the load
  if (enforcementPort.Equals(resourcePortStr)) {
    return true;
  }

  // Additional port matching where the regular URL matching algorithm
  // treats insecure ports as matching their secure variants.
  // default port for http is  :80
  // default port for https is :443
  if (enforcementPort.EqualsLiteral("80") &&
      resourcePortStr.EqualsLiteral("443")) {
    return true;
  }

  // ports do not match, block the load.
  return false;
}

bool nsCSPHostSrc::permits(nsIURI* aUri, bool aWasRedirected, bool aReportOnly,
                           bool aUpgradeInsecure) const {
  if (CSPUTILSLOGENABLED()) {
    CSPUTILSLOG(
        ("nsCSPHostSrc::permits, aUri: %s", aUri->GetSpecOrDefault().get()));
  }

  if (mIsUniqueOrigin) {
    return false;
  }

  // we are following the enforcement rules from the spec, see:
  // http://www.w3.org/TR/CSP11/#match-source-expression

  // 4.3) scheme matching: Check if the scheme matches.
  if (!permitsScheme(mScheme, aUri, aReportOnly, aUpgradeInsecure,
                     mGeneratedFromSelfKeyword)) {
    return false;
  }

  // The host in nsCSpHostSrc should never be empty. In case we are enforcing
  // just a specific scheme, the parser should generate a nsCSPSchemeSource.
  NS_ASSERTION((!mHost.IsEmpty()), "host can not be the empty string");

  // Before we can check if the host matches, we have to
  // extract the host part from aUri.
  nsAutoCString uriHost;
  nsresult rv = aUri->GetAsciiHost(uriHost);
  NS_ENSURE_SUCCESS(rv, false);

  nsString decodedUriHost;
  CSP_PercentDecodeStr(NS_ConvertUTF8toUTF16(uriHost), decodedUriHost);

  // 2) host matching: Enforce a single *
  if (mHost.EqualsASCII("*")) {
    // The single ASTERISK character (*) does not match a URI's scheme of a type
    // designating a globally unique identifier (such as blob:, data:, or
    // filesystem:) At the moment firefox does not support filesystem; but for
    // future compatibility we support it in CSP according to the spec,
    // see: 4.2.2 Matching Source Expressions Note, that allowlisting any of
    // these schemes would call nsCSPSchemeSrc::permits().
    if (aUri->SchemeIs("blob") || aUri->SchemeIs("data") ||
        aUri->SchemeIs("filesystem")) {
      return false;
    }

    // If no scheme is present there also wont be a port and folder to check
    // which means we can return early
    if (mScheme.IsEmpty()) {
      return true;
    }
  }
  // 4.5) host matching: Check if the allowed host starts with a wilcard.
  else if (mHost.First() == '*') {
    NS_ASSERTION(
        mHost[1] == '.',
        "Second character needs to be '.' whenever host starts with '*'");

    // Eliminate leading "*", but keeping the FULL STOP (.) thereafter before
    // checking if the remaining characters match
    nsString wildCardHost = mHost;
    wildCardHost = Substring(wildCardHost, 1, wildCardHost.Length() - 1);
    if (!StringEndsWith(decodedUriHost, wildCardHost)) {
      return false;
    }
  }
  // 4.6) host matching: Check if hosts match.
  else if (!mHost.Equals(decodedUriHost)) {
    return false;
  }

  // Port matching: Check if the ports match.
  if (!permitsPort(mScheme, mPort, aUri)) {
    return false;
  }

  // 4.9) Path matching: If there is a path, we have to enforce
  // path-level matching, unless the channel got redirected, see:
  // http://www.w3.org/TR/CSP11/#source-list-paths-and-redirects
  if (!aWasRedirected && !mPath.IsEmpty()) {
    // converting aUri into nsIURL so we can strip query and ref
    // example.com/test#foo     -> example.com/test
    // example.com/test?val=foo -> example.com/test
    nsCOMPtr<nsIURL> url = do_QueryInterface(aUri);
    if (!url) {
      NS_ASSERTION(false, "can't QI into nsIURI");
      return false;
    }
    nsAutoCString uriPath;
    rv = url->GetFilePath(uriPath);
    NS_ENSURE_SUCCESS(rv, false);

    if (mWithinFrameAncstorsDir) {
      // no path matching for frame-ancestors to not leak any path information.
      return true;
    }

    nsString decodedUriPath;
    CSP_PercentDecodeStr(NS_ConvertUTF8toUTF16(uriPath), decodedUriPath);

    // check if the last character of mPath is '/'; if so
    // we just have to check loading resource is within
    // the allowed path.
    if (mPath.Last() == '/') {
      if (!StringBeginsWith(decodedUriPath, mPath)) {
        return false;
      }
    }
    // otherwise mPath refers to a specific file, and we have to
    // check if the loading resource matches the file.
    else {
      if (!mPath.Equals(decodedUriPath)) {
        return false;
      }
    }
  }

  // At the end: scheme, host, port and path match -> allow the load.
  return true;
}

bool nsCSPHostSrc::visit(nsCSPSrcVisitor* aVisitor) const {
  return aVisitor->visitHostSrc(*this);
}

void nsCSPHostSrc::toString(nsAString& outStr) const {
  if (mGeneratedFromSelfKeyword) {
    outStr.AppendLiteral("'self'");
    return;
  }

  // If mHost is a single "*", we append the wildcard and return.
  if (mHost.EqualsASCII("*") && mScheme.IsEmpty() && mPort.IsEmpty()) {
    outStr.Append(mHost);
    return;
  }

  // append scheme
  outStr.Append(mScheme);

  // append host
  outStr.AppendLiteral("://");
  outStr.Append(mHost);

  // append port
  if (!mPort.IsEmpty()) {
    outStr.AppendLiteral(":");
    outStr.Append(mPort);
  }

  // append path
  outStr.Append(mPath);
}

void nsCSPHostSrc::setScheme(const nsAString& aScheme) {
  mScheme = aScheme;
  ToLowerCase(mScheme);
}

void nsCSPHostSrc::setPort(const nsAString& aPort) { mPort = aPort; }

void nsCSPHostSrc::appendPath(const nsAString& aPath) { mPath.Append(aPath); }

/* ===== nsCSPKeywordSrc ===================== */

nsCSPKeywordSrc::nsCSPKeywordSrc(enum CSPKeyword aKeyword)
    : mKeyword(aKeyword) {
  NS_ASSERTION((aKeyword != CSP_SELF),
               "'self' should have been replaced in the parser");
}

nsCSPKeywordSrc::~nsCSPKeywordSrc() = default;

bool nsCSPKeywordSrc::allows(enum CSPKeyword aKeyword,
                             const nsAString& aHashOrNonce) const {
  CSPUTILSLOG(("nsCSPKeywordSrc::allows, aKeyWord: %s, aHashOrNonce: %s",
               CSP_EnumToUTF8Keyword(aKeyword),
               NS_ConvertUTF16toUTF8(aHashOrNonce).get()));
  return mKeyword == aKeyword;
}

bool nsCSPKeywordSrc::visit(nsCSPSrcVisitor* aVisitor) const {
  return aVisitor->visitKeywordSrc(*this);
}

void nsCSPKeywordSrc::toString(nsAString& outStr) const {
  outStr.Append(CSP_EnumToUTF16Keyword(mKeyword));
}

/* ===== nsCSPNonceSrc ==================== */

nsCSPNonceSrc::nsCSPNonceSrc(const nsAString& aNonce) : mNonce(aNonce) {}

nsCSPNonceSrc::~nsCSPNonceSrc() = default;

bool nsCSPNonceSrc::allows(enum CSPKeyword aKeyword,
                           const nsAString& aHashOrNonce) const {
  CSPUTILSLOG(("nsCSPNonceSrc::allows, aKeyWord: %s, a HashOrNonce: %s",
               CSP_EnumToUTF8Keyword(aKeyword),
               NS_ConvertUTF16toUTF8(aHashOrNonce).get()));

  if (aKeyword != CSP_NONCE) {
    return false;
  }
  // nonces can not be invalidated by strict-dynamic
  return mNonce.Equals(aHashOrNonce);
}

bool nsCSPNonceSrc::visit(nsCSPSrcVisitor* aVisitor) const {
  return aVisitor->visitNonceSrc(*this);
}

void nsCSPNonceSrc::toString(nsAString& outStr) const {
  outStr.Append(CSP_EnumToUTF16Keyword(CSP_NONCE));
  outStr.Append(mNonce);
  outStr.AppendLiteral("'");
}

/* ===== nsCSPHashSrc ===================== */

nsCSPHashSrc::nsCSPHashSrc(const nsAString& aAlgo, const nsAString& aHash)
    : mAlgorithm(aAlgo), mHash(aHash) {
  // Only the algo should be rewritten to lowercase, the hash must remain the
  // same.
  ToLowerCase(mAlgorithm);
  // Normalize the base64url encoding to base64 encoding:
  char16_t* cur = mHash.BeginWriting();
  char16_t* end = mHash.EndWriting();

  for (; cur < end; ++cur) {
    if (char16_t('-') == *cur) {
      *cur = char16_t('+');
    }
    if (char16_t('_') == *cur) {
      *cur = char16_t('/');
    }
  }
}

nsCSPHashSrc::~nsCSPHashSrc() = default;

bool nsCSPHashSrc::allows(enum CSPKeyword aKeyword,
                          const nsAString& aHashOrNonce) const {
  CSPUTILSLOG(("nsCSPHashSrc::allows, aKeyWord: %s, a HashOrNonce: %s",
               CSP_EnumToUTF8Keyword(aKeyword),
               NS_ConvertUTF16toUTF8(aHashOrNonce).get()));

  if (aKeyword != CSP_HASH) {
    return false;
  }

  // hashes can not be invalidated by strict-dynamic

  // Convert aHashOrNonce to UTF-8
  NS_ConvertUTF16toUTF8 utf8_hash(aHashOrNonce);

  nsCOMPtr<nsICryptoHash> hasher;
  nsresult rv = NS_NewCryptoHash(NS_ConvertUTF16toUTF8(mAlgorithm),
                                 getter_AddRefs(hasher));
  NS_ENSURE_SUCCESS(rv, false);

  rv = hasher->Update((uint8_t*)utf8_hash.get(), utf8_hash.Length());
  NS_ENSURE_SUCCESS(rv, false);

  nsAutoCString hash;
  rv = hasher->Finish(true, hash);
  NS_ENSURE_SUCCESS(rv, false);

  return NS_ConvertUTF16toUTF8(mHash).Equals(hash);
}

bool nsCSPHashSrc::visit(nsCSPSrcVisitor* aVisitor) const {
  return aVisitor->visitHashSrc(*this);
}

void nsCSPHashSrc::toString(nsAString& outStr) const {
  outStr.AppendLiteral("'");
  outStr.Append(mAlgorithm);
  outStr.AppendLiteral("-");
  outStr.Append(mHash);
  outStr.AppendLiteral("'");
}

/* ===== nsCSPReportURI ===================== */

nsCSPReportURI::nsCSPReportURI(nsIURI* aURI) : mReportURI(aURI) {}

nsCSPReportURI::~nsCSPReportURI() = default;

bool nsCSPReportURI::visit(nsCSPSrcVisitor* aVisitor) const { return false; }

void nsCSPReportURI::toString(nsAString& outStr) const {
  nsAutoCString spec;
  nsresult rv = mReportURI->GetSpec(spec);
  if (NS_FAILED(rv)) {
    return;
  }
  outStr.AppendASCII(spec.get());
}

/* ===== nsCSPReportGroup ===================== */

nsCSPGroup::nsCSPGroup(const nsAString& aGroup) : mGroup(aGroup) {}

nsCSPGroup::~nsCSPGroup() = default;

bool nsCSPGroup::visit(nsCSPSrcVisitor* aVisitor) const { return false; }

void nsCSPGroup::toString(nsAString& aOutStr) const { aOutStr.Append(mGroup); }

/* ===== nsCSPSandboxFlags ===================== */

nsCSPSandboxFlags::nsCSPSandboxFlags(const nsAString& aFlags) : mFlags(aFlags) {
  ToLowerCase(mFlags);
}

nsCSPSandboxFlags::~nsCSPSandboxFlags() = default;

bool nsCSPSandboxFlags::visit(nsCSPSrcVisitor* aVisitor) const { return false; }

void nsCSPSandboxFlags::toString(nsAString& outStr) const {
  outStr.Append(mFlags);
}

/* ===== nsCSPRequireTrustedTypesForDirectiveValue ===================== */

nsCSPRequireTrustedTypesForDirectiveValue::
    nsCSPRequireTrustedTypesForDirectiveValue(const nsAString& aValue)
    : mValue{aValue} {}

bool nsCSPRequireTrustedTypesForDirectiveValue::visit(
    nsCSPSrcVisitor* aVisitor) const {
  MOZ_ASSERT_UNREACHABLE(
      "This method should only be called for other overloads of this method.");
  return false;
}

void nsCSPRequireTrustedTypesForDirectiveValue::toString(
    nsAString& aOutStr) const {
  aOutStr.Append(mValue);
}

/* =============== nsCSPTrustedTypesDirectivePolicyName =============== */

nsCSPTrustedTypesDirectivePolicyName::nsCSPTrustedTypesDirectivePolicyName(
    const nsAString& aName)
    : mName{aName} {}

bool nsCSPTrustedTypesDirectivePolicyName::visit(
    nsCSPSrcVisitor* aVisitor) const {
  MOZ_ASSERT_UNREACHABLE(
      "Should only be called for other overloads of this method.");
  return false;
}

void nsCSPTrustedTypesDirectivePolicyName::toString(nsAString& aOutStr) const {
  aOutStr.Append(mName);
}

/* =============== nsCSPTrustedTypesDirectiveInvalidToken =============== */

nsCSPTrustedTypesDirectiveInvalidToken::nsCSPTrustedTypesDirectiveInvalidToken(
    const nsAString& aInvalidToken)
    : mInvalidToken{aInvalidToken} {}

bool nsCSPTrustedTypesDirectiveInvalidToken::visit(
    nsCSPSrcVisitor* aVisitor) const {
  MOZ_ASSERT_UNREACHABLE(
      "Should only be called for other overloads of this method.");
  return false;
}

void nsCSPTrustedTypesDirectiveInvalidToken::toString(
    nsAString& aOutStr) const {
  aOutStr.Append(mInvalidToken);
}

/* ===== nsCSPDirective ====================== */

nsCSPDirective::nsCSPDirective(CSPDirective aDirective) {
  mDirective = aDirective;
}

nsCSPDirective::~nsCSPDirective() {
  for (uint32_t i = 0; i < mSrcs.Length(); i++) {
    delete mSrcs[i];
  }
}

// https://w3c.github.io/webappsec-csp/#match-nonce-to-source-list
static bool DoesNonceMatchSourceList(nsILoadInfo* aLoadInfo,
                                     const nsTArray<nsCSPBaseSrc*>& aSrcs) {
  // Step 1. Assert: source list is not null. (implicit)

  // Note: For code-reuse we do "request’s cryptographic nonce metadata" here
  // instead of the caller.
  nsAutoString nonce;
  MOZ_ALWAYS_SUCCEEDS(aLoadInfo->GetCspNonce(nonce));

  // Step 2. If nonce is the empty string, return "Does Not Match".
  if (nonce.IsEmpty()) {
    return false;
  }

  // Step 3. For each expression of source list:
  for (nsCSPBaseSrc* src : aSrcs) {
    // Step 3.1. If expression matches the nonce-source grammar, and nonce is
    // identical to expression’s base64-value part, return "Matches".
    if (src->isNonce()) {
      nsAutoString srcNonce;
      static_cast<nsCSPNonceSrc*>(src)->getNonce(srcNonce);
      if (srcNonce == nonce) {
        return true;
      }
    }
  }

  // Step 4. Return "Does Not Match".
  return false;
}

// https://www.w3.org/TR/SRI/#parse-metadata
// This function is similar to SRICheck::IntegrityMetadata, but also keeps
// SRI metadata with weaker hashes.
// CSP treats "no metadata" and empty results the same way.
static nsTArray<SRIMetadata> ParseSRIMetadata(const nsAString& aMetadata) {
  // Step 1. Let result be the empty set.
  // Step 2. Let empty be equal to true.
  nsTArray<SRIMetadata> result;

  NS_ConvertUTF16toUTF8 metadataList(aMetadata);
  nsAutoCString token;

  // Step 3. For each token returned by splitting metadata on spaces:
  nsCWhitespaceTokenizer tokenizer(metadataList);
  while (tokenizer.hasMoreTokens()) {
    token = tokenizer.nextToken();
    // Step 3.1. Set empty to false.
    // Step 3.3. Parse token per the grammar in integrity metadata.
    SRIMetadata metadata(token);
    // Step 3.2. If token is not a valid metadata, skip the remaining steps, and
    // proceed to the next token.
    if (metadata.IsMalformed()) {
      continue;
    }

    // Step 3.4. Let algorithm be the alg component of token.
    // Step 3.5. If algorithm is a hash function recognized by the user agent,
    // add the
    //  parsed token to result.
    if (metadata.IsAlgorithmSupported()) {
      result.AppendElement(metadata);
    }
  }

  // Step 4. Return no metadata if empty is true, otherwise return result.
  return result;
}

bool nsCSPDirective::permits(CSPDirective aDirective, nsILoadInfo* aLoadInfo,
                             nsIURI* aUri, bool aWasRedirected,
                             bool aReportOnly, bool aUpgradeInsecure) const {
  MOZ_ASSERT(equals(aDirective) || isDefaultDirective());

  if (CSPUTILSLOGENABLED()) {
    CSPUTILSLOG(("nsCSPDirective::permits, aUri: %s, aDirective: %s",
                 aUri->GetSpecOrDefault().get(),
                 CSP_CSPDirectiveToString(aDirective)));
  }

  if (aLoadInfo) {
    // https://w3c.github.io/webappsec-csp/#style-src-elem-pre-request
    if (aDirective == CSPDirective::STYLE_SRC_ELEM_DIRECTIVE) {
      // Step 3. If the result of executing §6.7.2.3 Does nonce match source
      // list? on request’s cryptographic nonce metadata and this directive’s
      // value is "Matches", return "Allowed".
      if (DoesNonceMatchSourceList(aLoadInfo, mSrcs)) {
        CSPUTILSLOG(("  Allowed by matching nonce (style)"));
        return true;
      }
    }

    // https://w3c.github.io/webappsec-csp/#script-pre-request
    // Step 1. If request’s destination is script-like:
    else if (aDirective == CSPDirective::SCRIPT_SRC_ELEM_DIRECTIVE ||
             aDirective == CSPDirective::WORKER_SRC_DIRECTIVE) {
      // Step 1.1. If the result of executing §6.7.2.3 Does nonce match source
      // list? on request’s cryptographic nonce metadata and this directive’s
      // value is "Matches", return "Allowed".
      if (DoesNonceMatchSourceList(aLoadInfo, mSrcs)) {
        CSPUTILSLOG(("  Allowed by matching nonce (script-like)"));
        return true;
      }

      // Step 1.2. Let integrity expressions be the set of source expressions in
      // directive’s value that match the hash-source grammar.
      nsTArray<nsCSPHashSrc*> integrityExpressions;
      bool hasStrictDynamicKeyword =
          false;  // Optimization to reduce number of iterations.
      for (uint32_t i = 0; i < mSrcs.Length(); i++) {
        if (mSrcs[i]->isHash()) {
          integrityExpressions.AppendElement(
              static_cast<nsCSPHashSrc*>(mSrcs[i]));
        } else if (mSrcs[i]->isKeyword(CSP_STRICT_DYNAMIC)) {
          hasStrictDynamicKeyword = true;
        }
      }

      // Step 1.3. If integrity expressions is not empty:
      if (!integrityExpressions.IsEmpty()) {
        // Step 1.3.1. Let integrity sources be the result of executing the
        // algorithm defined in [SRI 3.3.3 Parse metadata] on request’s
        // integrity metadata.
        nsAutoString integrityMetadata;
        aLoadInfo->GetIntegrityMetadata(integrityMetadata);

        nsTArray<SRIMetadata> integritySources =
            ParseSRIMetadata(integrityMetadata);
        MOZ_ASSERT(
            integritySources.IsEmpty() == integrityMetadata.IsEmpty(),
            "The integrity metadata should be only be empty, "
            "when the parsed string was completely empty, otherwise it should "
            "include at least one valid hash");

        // Step 1.3.2. If integrity sources is "no metadata" or an empty set,
        // skip the remaining substeps.
        if (!integritySources.IsEmpty()) {
          // Step 1.3.3. Let bypass due to integrity match be true.
          bool bypass = true;

          nsAutoCString sourceAlgorithmUTF8;
          nsAutoCString sourceHashUTF8;
          nsAutoString sourceAlgorithm;
          nsAutoString sourceHash;
          nsAutoString algorithm;
          nsAutoString hash;

          // Step 1.3.4. For each source of integrity sources:
          for (const SRIMetadata& source : integritySources) {
            source.GetAlgorithm(&sourceAlgorithmUTF8);
            sourceAlgorithm = NS_ConvertUTF8toUTF16(sourceAlgorithmUTF8);
            source.GetHash(0, &sourceHashUTF8);
            sourceHash = NS_ConvertUTF8toUTF16(sourceHashUTF8);

            // Step 1.3.4.1 If directive’s value does not contain a source
            // expression whose hash-algorithm is an ASCII case-insensitive
            // match for source’s hash-algorithm, and whose base64-value is
            // identical to source’s base64-value, then set bypass due to
            // integrity match to false.
            bool found = false;
            for (const nsCSPHashSrc* hashSrc : integrityExpressions) {
              hashSrc->getAlgorithm(algorithm);
              hashSrc->getHash(hash);

              // The nsCSPHashSrc constructor lowercases algorithm, so this
              // is case-insensitive.
              if (sourceAlgorithm == algorithm && sourceHash == hash) {
                found = true;
                break;
              }
            }

            if (!found) {
              bypass = false;
              break;
            }
          }

          // Step 1.3.5. If bypass due to integrity match is true, return
          // "Allowed".
          if (bypass) {
            CSPUTILSLOG(
                ("  Allowed by matching integrity metadata (script-like)"));
            return true;
          }
        }
      }

      // Step 1.4. If directive’s value contains a source expression that is an
      // ASCII case-insensitive match for the "'strict-dynamic'" keyword-source:

      // XXX I don't think we should apply strict-dynamic to XSLT.
      if (hasStrictDynamicKeyword && aLoadInfo->InternalContentPolicyType() !=
                                         nsIContentPolicy::TYPE_XSLT) {
        // Step 1.4.1  If the request’s parser metadata is "parser-inserted",
        // return "Blocked". Otherwise, return "Allowed".
        if (aLoadInfo->GetParserCreatedScript()) {
          CSPUTILSLOG(
              ("  Blocked by 'strict-dynamic' because parser-inserted"));
          return false;
        }

        CSPUTILSLOG(
            ("  Allowed by 'strict-dynamic' because not-parser-inserted"));
        return true;
      }
    }
  }

  for (uint32_t i = 0; i < mSrcs.Length(); i++) {
    if (mSrcs[i]->permits(aUri, aWasRedirected, aReportOnly,
                          aUpgradeInsecure)) {
      return true;
    }
  }
  return false;
}

bool nsCSPDirective::allows(enum CSPKeyword aKeyword,
                            const nsAString& aHashOrNonce) const {
  CSPUTILSLOG(("nsCSPDirective::allows, aKeyWord: %s, aHashOrNonce: %s",
               CSP_EnumToUTF8Keyword(aKeyword),
               NS_ConvertUTF16toUTF8(aHashOrNonce).get()));

  for (uint32_t i = 0; i < mSrcs.Length(); i++) {
    if (mSrcs[i]->allows(aKeyword, aHashOrNonce)) {
      return true;
    }
  }
  return false;
}

// https://w3c.github.io/webappsec-csp/#allow-all-inline
bool nsCSPDirective::allowsAllInlineBehavior(CSPDirective aDir) const {
  // Step 1. Let allow all inline be false.
  bool allowAll = false;

  // Step 2. For each expression of list:
  for (nsCSPBaseSrc* src : mSrcs) {
    // Step 2.1. If expression matches the nonce-source or hash-source grammar,
    // return "Does Not Allow".
    if (src->isNonce() || src->isHash()) {
      return false;
    }

    // Step 2.2. If type is "script", "script attribute" or "navigation" and
    // expression matches the keyword-source "'strict-dynamic'", return "Does
    // Not Allow".
    if ((aDir == nsIContentSecurityPolicy::SCRIPT_SRC_ELEM_DIRECTIVE ||
         aDir == nsIContentSecurityPolicy::SCRIPT_SRC_ATTR_DIRECTIVE) &&
        src->isKeyword(CSP_STRICT_DYNAMIC)) {
      return false;
    }

    // Step 2.3. If expression is an ASCII case-insensitive match for the
    // keyword-source "'unsafe-inline'", set allow all inline to true.
    if (src->isKeyword(CSP_UNSAFE_INLINE)) {
      allowAll = true;
    }
  }

  // Step 3. If allow all inline is true, return "Allows". Otherwise, return
  // "Does Not Allow".
  return allowAll;
}

static constexpr auto kWildcard = u"*"_ns;

bool nsCSPDirective::ShouldCreateViolationForNewTrustedTypesPolicy(
    const nsAString& aPolicyName,
    const nsTArray<nsString>& aCreatedPolicyNames) const {
  MOZ_ASSERT(mDirective == nsIContentSecurityPolicy::TRUSTED_TYPES_DIRECTIVE);

  if (mDirective == nsIContentSecurityPolicy::TRUSTED_TYPES_DIRECTIVE) {
    if (allows(CSP_NONE, EmptyString())) {
      // Step 2.4: if directive’s value only contains a tt-keyword which is a
      // match for a value 'none', set createViolation to true.
      // `nsCSPParser` ignores the 'none' keyword if other keywords or policy
      // names are present. Hence no additional checks required here.
      return true;
    }

    if (aCreatedPolicyNames.Contains(aPolicyName) &&
        !allows(CSP_ALLOW_DUPLICATES, EmptyString())) {
      // Step 2.5: if createdPolicyNames contains policyName and directive’s
      // value does not contain a tt-keyword which is a match for a value
      // 'allow-duplicates', set createViolation to true.
      return true;
    }

    if (!ContainsTrustedTypesDirectivePolicyName(aPolicyName) &&
        !ContainsTrustedTypesDirectivePolicyName(kWildcard)) {
      // Step 2.6: if directive’s value does not contain a tt-policy-name, which
      // value is policyName, and directive’s value does not contain a
      // tt-wildcard, set createViolation to true.
      return true;
    }
  }

  return false;
}

void nsCSPDirective::toString(nsAString& outStr) const {
  // Append directive name
  outStr.AppendASCII(CSP_CSPDirectiveToString(mDirective));

  MOZ_ASSERT(!mSrcs.IsEmpty());

  outStr.AppendLiteral(" ");

  // Append srcs
  StringJoinAppend(outStr, u" "_ns, mSrcs,
                   [](nsAString& dest, nsCSPBaseSrc* cspBaseSrc) {
                     cspBaseSrc->toString(dest);
                   });
}

void nsCSPDirective::toDomCSPStruct(mozilla::dom::CSP& outCSP) const {
  mozilla::dom::Sequence<nsString> srcs;
  nsString src;
  if (NS_WARN_IF(!srcs.SetCapacity(mSrcs.Length(), mozilla::fallible))) {
    MOZ_ASSERT(false,
               "Not enough memory for 'sources' sequence in "
               "nsCSPDirective::toDomCSPStruct().");
    return;
  }
  for (uint32_t i = 0; i < mSrcs.Length(); i++) {
    src.Truncate();
    mSrcs[i]->toString(src);
    if (!srcs.AppendElement(src, mozilla::fallible)) {
      MOZ_ASSERT(false,
                 "Failed to append to 'sources' sequence in "
                 "nsCSPDirective::toDomCSPStruct().");
    }
  }

  switch (mDirective) {
    case nsIContentSecurityPolicy::DEFAULT_SRC_DIRECTIVE:
      outCSP.mDefault_src.Construct();
      outCSP.mDefault_src.Value() = std::move(srcs);
      return;

    case nsIContentSecurityPolicy::SCRIPT_SRC_DIRECTIVE:
      outCSP.mScript_src.Construct();
      outCSP.mScript_src.Value() = std::move(srcs);
      return;

    case nsIContentSecurityPolicy::OBJECT_SRC_DIRECTIVE:
      outCSP.mObject_src.Construct();
      outCSP.mObject_src.Value() = std::move(srcs);
      return;

    case nsIContentSecurityPolicy::STYLE_SRC_DIRECTIVE:
      outCSP.mStyle_src.Construct();
      outCSP.mStyle_src.Value() = std::move(srcs);
      return;

    case nsIContentSecurityPolicy::IMG_SRC_DIRECTIVE:
      outCSP.mImg_src.Construct();
      outCSP.mImg_src.Value() = std::move(srcs);
      return;

    case nsIContentSecurityPolicy::MEDIA_SRC_DIRECTIVE:
      outCSP.mMedia_src.Construct();
      outCSP.mMedia_src.Value() = std::move(srcs);
      return;

    case nsIContentSecurityPolicy::FRAME_SRC_DIRECTIVE:
      outCSP.mFrame_src.Construct();
      outCSP.mFrame_src.Value() = std::move(srcs);
      return;

    case nsIContentSecurityPolicy::FONT_SRC_DIRECTIVE:
      outCSP.mFont_src.Construct();
      outCSP.mFont_src.Value() = std::move(srcs);
      return;

    case nsIContentSecurityPolicy::CONNECT_SRC_DIRECTIVE:
      outCSP.mConnect_src.Construct();
      outCSP.mConnect_src.Value() = std::move(srcs);
      return;

    case nsIContentSecurityPolicy::REPORT_URI_DIRECTIVE:
      outCSP.mReport_uri.Construct();
      outCSP.mReport_uri.Value() = std::move(srcs);
      return;

    case nsIContentSecurityPolicy::FRAME_ANCESTORS_DIRECTIVE:
      outCSP.mFrame_ancestors.Construct();
      outCSP.mFrame_ancestors.Value() = std::move(srcs);
      return;

    case nsIContentSecurityPolicy::WEB_MANIFEST_SRC_DIRECTIVE:
      outCSP.mManifest_src.Construct();
      outCSP.mManifest_src.Value() = std::move(srcs);
      return;
      // not supporting REFLECTED_XSS_DIRECTIVE

    case nsIContentSecurityPolicy::BASE_URI_DIRECTIVE:
      outCSP.mBase_uri.Construct();
      outCSP.mBase_uri.Value() = std::move(srcs);
      return;

    case nsIContentSecurityPolicy::FORM_ACTION_DIRECTIVE:
      outCSP.mForm_action.Construct();
      outCSP.mForm_action.Value() = std::move(srcs);
      return;

    case nsIContentSecurityPolicy::BLOCK_ALL_MIXED_CONTENT:
      outCSP.mBlock_all_mixed_content.Construct();
      // does not have any srcs
      return;

    case nsIContentSecurityPolicy::UPGRADE_IF_INSECURE_DIRECTIVE:
      outCSP.mUpgrade_insecure_requests.Construct();
      // does not have any srcs
      return;

    case nsIContentSecurityPolicy::CHILD_SRC_DIRECTIVE:
      outCSP.mChild_src.Construct();
      outCSP.mChild_src.Value() = std::move(srcs);
      return;

    case nsIContentSecurityPolicy::SANDBOX_DIRECTIVE:
      outCSP.mSandbox.Construct();
      outCSP.mSandbox.Value() = std::move(srcs);
      return;

    case nsIContentSecurityPolicy::WORKER_SRC_DIRECTIVE:
      outCSP.mWorker_src.Construct();
      outCSP.mWorker_src.Value() = std::move(srcs);
      return;

    case nsIContentSecurityPolicy::SCRIPT_SRC_ELEM_DIRECTIVE:
      outCSP.mScript_src_elem.Construct();
      outCSP.mScript_src_elem.Value() = std::move(srcs);
      return;

    case nsIContentSecurityPolicy::SCRIPT_SRC_ATTR_DIRECTIVE:
      outCSP.mScript_src_attr.Construct();
      outCSP.mScript_src_attr.Value() = std::move(srcs);
      return;

    case nsIContentSecurityPolicy::REQUIRE_TRUSTED_TYPES_FOR_DIRECTIVE:
      outCSP.mRequire_trusted_types_for.Construct();

      // Here, the srcs represent the sink group
      // (https://w3c.github.io/trusted-types/dist/spec/#integration-with-content-security-policy).
      outCSP.mRequire_trusted_types_for.Value() = std::move(srcs);
      return;

    case nsIContentSecurityPolicy::TRUSTED_TYPES_DIRECTIVE:
      outCSP.mTrusted_types.Construct();
      // Here, "srcs" represents tt-expressions
      // (https://w3c.github.io/trusted-types/dist/spec/#trusted-types-csp-directive).
      outCSP.mTrusted_types.Value() = std::move(srcs);
      return;

    case nsIContentSecurityPolicy::REPORT_TO_DIRECTIVE:
      outCSP.mReport_to.Construct();
      outCSP.mReport_to.Value() = std::move(srcs);
      return;

    default:
      NS_ASSERTION(false, "cannot find directive to convert CSP to JSON");
  }
}

bool nsCSPDirective::isDefaultDirective() const {
  return mDirective == nsIContentSecurityPolicy::DEFAULT_SRC_DIRECTIVE;
}

void nsCSPDirective::getReportURIs(nsTArray<nsString>& outReportURIs) const {
  NS_ASSERTION((mDirective == nsIContentSecurityPolicy::REPORT_URI_DIRECTIVE),
               "not a report-uri directive");

  // append uris
  nsString tmpReportURI;
  for (uint32_t i = 0; i < mSrcs.Length(); i++) {
    tmpReportURI.Truncate();
    mSrcs[i]->toString(tmpReportURI);
    outReportURIs.AppendElement(tmpReportURI);
  }
}

void nsCSPDirective::getReportGroup(nsAString& outReportGroup) const {
  NS_ASSERTION((mDirective == nsIContentSecurityPolicy::REPORT_TO_DIRECTIVE),
               "not a report-to directive");

  MOZ_ASSERT(mSrcs.Length() <= 1);
  mSrcs[0]->toString(outReportGroup);
}

bool nsCSPDirective::visitSrcs(nsCSPSrcVisitor* aVisitor) const {
  for (uint32_t i = 0; i < mSrcs.Length(); i++) {
    if (!mSrcs[i]->visit(aVisitor)) {
      return false;
    }
  }
  return true;
}

bool nsCSPDirective::equals(CSPDirective aDirective) const {
  return (mDirective == aDirective);
}

void nsCSPDirective::getDirName(nsAString& outStr) const {
  outStr.AppendASCII(CSP_CSPDirectiveToString(mDirective));
}

bool nsCSPDirective::hasReportSampleKeyword() const {
  for (nsCSPBaseSrc* src : mSrcs) {
    if (src->isReportSample()) {
      return true;
    }
  }

  return false;
}

bool nsCSPDirective::ContainsTrustedTypesDirectivePolicyName(
    const nsAString& aPolicyName) const {
  MOZ_ASSERT(mDirective == nsIContentSecurityPolicy::TRUSTED_TYPES_DIRECTIVE);

  if (mDirective == nsIContentSecurityPolicy::TRUSTED_TYPES_DIRECTIVE) {
    for (const auto* src : mSrcs) {
      if (src->isTrustedTypesDirectivePolicyName()) {
        const auto& name =
            static_cast<const nsCSPTrustedTypesDirectivePolicyName*>(src)
                ->GetName();
        if (name.Equals(aPolicyName)) {
          return true;
        }
      }
    }
  }

  return false;
}

/* =============== nsCSPChildSrcDirective ============= */

nsCSPChildSrcDirective::nsCSPChildSrcDirective(CSPDirective aDirective)
    : nsCSPDirective(aDirective),
      mRestrictFrames(false),
      mRestrictWorkers(false) {}

nsCSPChildSrcDirective::~nsCSPChildSrcDirective() = default;

bool nsCSPChildSrcDirective::equals(CSPDirective aDirective) const {
  if (aDirective == nsIContentSecurityPolicy::FRAME_SRC_DIRECTIVE) {
    return mRestrictFrames;
  }
  if (aDirective == nsIContentSecurityPolicy::WORKER_SRC_DIRECTIVE) {
    return mRestrictWorkers;
  }
  return (mDirective == aDirective);
}

/* =============== nsCSPScriptSrcDirective ============= */

nsCSPScriptSrcDirective::nsCSPScriptSrcDirective(CSPDirective aDirective)
    : nsCSPDirective(aDirective) {}

nsCSPScriptSrcDirective::~nsCSPScriptSrcDirective() = default;

bool nsCSPScriptSrcDirective::equals(CSPDirective aDirective) const {
  if (aDirective == nsIContentSecurityPolicy::WORKER_SRC_DIRECTIVE) {
    return mRestrictWorkers;
  }
  if (aDirective == nsIContentSecurityPolicy::SCRIPT_SRC_ELEM_DIRECTIVE) {
    return mRestrictScriptElem;
  }
  if (aDirective == nsIContentSecurityPolicy::SCRIPT_SRC_ATTR_DIRECTIVE) {
    return mRestrictScriptAttr;
  }
  return mDirective == aDirective;
}

/* =============== nsCSPStyleSrcDirective ============= */

nsCSPStyleSrcDirective::nsCSPStyleSrcDirective(CSPDirective aDirective)
    : nsCSPDirective(aDirective) {}

nsCSPStyleSrcDirective::~nsCSPStyleSrcDirective() = default;

bool nsCSPStyleSrcDirective::equals(CSPDirective aDirective) const {
  if (aDirective == nsIContentSecurityPolicy::STYLE_SRC_ELEM_DIRECTIVE) {
    return mRestrictStyleElem;
  }
  if (aDirective == nsIContentSecurityPolicy::STYLE_SRC_ATTR_DIRECTIVE) {
    return mRestrictStyleAttr;
  }
  return mDirective == aDirective;
}

/* =============== nsBlockAllMixedContentDirective ============= */

nsBlockAllMixedContentDirective::nsBlockAllMixedContentDirective(
    CSPDirective aDirective)
    : nsCSPDirective(aDirective) {}

nsBlockAllMixedContentDirective::~nsBlockAllMixedContentDirective() = default;

void nsBlockAllMixedContentDirective::toString(nsAString& outStr) const {
  outStr.AppendASCII(CSP_CSPDirectiveToString(
      nsIContentSecurityPolicy::BLOCK_ALL_MIXED_CONTENT));
}

void nsBlockAllMixedContentDirective::getDirName(nsAString& outStr) const {
  outStr.AppendASCII(CSP_CSPDirectiveToString(
      nsIContentSecurityPolicy::BLOCK_ALL_MIXED_CONTENT));
}

/* =============== nsUpgradeInsecureDirective ============= */

nsUpgradeInsecureDirective::nsUpgradeInsecureDirective(CSPDirective aDirective)
    : nsCSPDirective(aDirective) {}

nsUpgradeInsecureDirective::~nsUpgradeInsecureDirective() = default;

void nsUpgradeInsecureDirective::toString(nsAString& outStr) const {
  outStr.AppendASCII(CSP_CSPDirectiveToString(
      nsIContentSecurityPolicy::UPGRADE_IF_INSECURE_DIRECTIVE));
}

void nsUpgradeInsecureDirective::getDirName(nsAString& outStr) const {
  outStr.AppendASCII(CSP_CSPDirectiveToString(
      nsIContentSecurityPolicy::UPGRADE_IF_INSECURE_DIRECTIVE));
}

/* ===== nsCSPPolicy ========================= */

nsCSPPolicy::nsCSPPolicy()
    : mUpgradeInsecDir(nullptr),
      mReportOnly(false),
      mDeliveredViaMetaTag(false) {
  CSPUTILSLOG(("nsCSPPolicy::nsCSPPolicy"));
}

nsCSPPolicy::~nsCSPPolicy() {
  CSPUTILSLOG(("nsCSPPolicy::~nsCSPPolicy"));

  for (uint32_t i = 0; i < mDirectives.Length(); i++) {
    delete mDirectives[i];
  }
}

bool nsCSPPolicy::permits(CSPDirective aDir, nsILoadInfo* aLoadInfo,
                          nsIURI* aUri, bool aWasRedirected, bool aSpecific,
                          nsAString& outViolatedDirective,
                          nsAString& outViolatedDirectiveString) const {
  if (CSPUTILSLOGENABLED()) {
    CSPUTILSLOG(("nsCSPPolicy::permits, aUri: %s, aDir: %s, aSpecific: %s",
                 aUri->GetSpecOrDefault().get(), CSP_CSPDirectiveToString(aDir),
                 aSpecific ? "true" : "false"));
  }

  NS_ASSERTION(aUri, "permits needs an uri to perform the check!");
  outViolatedDirective.Truncate();
  outViolatedDirectiveString.Truncate();

  nsCSPDirective* defaultDir = nullptr;

  // Try to find a relevant directive
  // These directive arrays are short (1-5 elements), not worth using a
  // hashtable.
  for (uint32_t i = 0; i < mDirectives.Length(); i++) {
    if (mDirectives[i]->equals(aDir)) {
      if (!mDirectives[i]->permits(aDir, aLoadInfo, aUri, aWasRedirected,
                                   mReportOnly, mUpgradeInsecDir)) {
        mDirectives[i]->getDirName(outViolatedDirective);
        mDirectives[i]->toString(outViolatedDirectiveString);
        return false;
      }
      return true;
    }
    if (mDirectives[i]->isDefaultDirective()) {
      defaultDir = mDirectives[i];
    }
  }

  // If the above loop runs through, we haven't found a matching directive.
  // Avoid relooping, just store the result of default-src while looping.
  if (!aSpecific && defaultDir) {
    if (!defaultDir->permits(aDir, aLoadInfo, aUri, aWasRedirected, mReportOnly,
                             mUpgradeInsecDir)) {
      defaultDir->getDirName(outViolatedDirective);
      defaultDir->toString(outViolatedDirectiveString);
      return false;
    }
    return true;
  }

  // Nothing restricts this, so we're allowing the load
  // See bug 764937
  return true;
}

bool nsCSPPolicy::allows(CSPDirective aDirective, enum CSPKeyword aKeyword,
                         const nsAString& aHashOrNonce) const {
  CSPUTILSLOG(("nsCSPPolicy::allows, aKeyWord: %s, a HashOrNonce: %s",
               CSP_EnumToUTF8Keyword(aKeyword),
               NS_ConvertUTF16toUTF8(aHashOrNonce).get()));

  if (nsCSPDirective* directive = matchingOrDefaultDirective(aDirective)) {
    return directive->allows(aKeyword, aHashOrNonce);
  }

  // No matching directive or default directive as fallback found, thus
  // allowing the load; see Bug 885433
  // a) inline scripts (also unsafe eval) should only be blocked
  //    if there is a [script-src] or [default-src]
  // b) inline styles should only be blocked
  //    if there is a [style-src] or [default-src]
  return true;
}

nsCSPDirective* nsCSPPolicy::matchingOrDefaultDirective(
    CSPDirective aDirective) const {
  nsCSPDirective* defaultDir = nullptr;

  // Try to find a matching directive
  for (uint32_t i = 0; i < mDirectives.Length(); i++) {
    if (mDirectives[i]->isDefaultDirective()) {
      defaultDir = mDirectives[i];
      continue;
    }
    if (mDirectives[i]->equals(aDirective)) {
      return mDirectives[i];
    }
  }

  return defaultDir;
}

void nsCSPPolicy::toString(nsAString& outStr) const {
  StringJoinAppend(outStr, u"; "_ns, mDirectives,
                   [](nsAString& dest, nsCSPDirective* cspDirective) {
                     cspDirective->toString(dest);
                   });
}

void nsCSPPolicy::toDomCSPStruct(mozilla::dom::CSP& outCSP) const {
  outCSP.mReport_only = mReportOnly;

  for (uint32_t i = 0; i < mDirectives.Length(); ++i) {
    mDirectives[i]->toDomCSPStruct(outCSP);
  }
}

bool nsCSPPolicy::hasDirective(CSPDirective aDir) const {
  for (uint32_t i = 0; i < mDirectives.Length(); i++) {
    if (mDirectives[i]->equals(aDir)) {
      return true;
    }
  }
  return false;
}

bool nsCSPPolicy::allowsAllInlineBehavior(CSPDirective aDir) const {
  nsCSPDirective* directive = matchingOrDefaultDirective(aDir);
  if (!directive) {
    // No matching or default directive found thus allow the all inline
    // scripts or styles. (See nsCSPPolicy::allows)
    return true;
  }

  return directive->allowsAllInlineBehavior(aDir);
}

bool nsCSPPolicy::ShouldCreateViolationForNewTrustedTypesPolicy(
    const nsAString& aPolicyName,
    const nsTArray<nsString>& aCreatedPolicyNames) const {
  for (const auto* directive : mDirectives) {
    if (directive->equals(nsIContentSecurityPolicy::TRUSTED_TYPES_DIRECTIVE)) {
      return directive->ShouldCreateViolationForNewTrustedTypesPolicy(
          aPolicyName, aCreatedPolicyNames);
    }
  }

  return false;
}

bool nsCSPPolicy::AreTrustedTypesForSinkGroupRequired(
    const nsAString& aSinkGroup) const {
  MOZ_ASSERT(aSinkGroup == dom::kTrustedTypesOnlySinkGroup);
  return mHasRequireTrustedTypesForDirective;
}

/*
 * Use this function only after ::allows() returned 'false' or if ensured by
 * other means that the directive is violated. First and foremost it's used to
 * get the violated directive before sending reports. The parameter
 * aDirectiveName is the equivalent of 'outViolatedDirective' for the
 * ::permits() function family.
 */
void nsCSPPolicy::getViolatedDirectiveInformation(
    CSPDirective aDirective, nsAString& aDirectiveName,
    nsAString& aDirectiveNameAndValue, bool* aReportSample) const {
  *aReportSample = false;
  nsCSPDirective* directive = matchingOrDefaultDirective(aDirective);
  if (!directive) {
    MOZ_ASSERT_UNREACHABLE("Can not query violated directive");
    aDirectiveName.Truncate();
    aDirectiveNameAndValue.Truncate();
    return;
  }

  directive->getDirName(aDirectiveName);
  directive->toString(aDirectiveNameAndValue);
  *aReportSample = directive->hasReportSampleKeyword();
}

/*
 * Helper function that returns the underlying bit representation of sandbox
 * flags. The function returns SANDBOXED_NONE if there are no sandbox
 * directives.
 */
uint32_t nsCSPPolicy::getSandboxFlags() const {
  for (uint32_t i = 0; i < mDirectives.Length(); i++) {
    if (mDirectives[i]->equals(nsIContentSecurityPolicy::SANDBOX_DIRECTIVE)) {
      nsAutoString flags;
      mDirectives[i]->toString(flags);

      if (flags.IsEmpty()) {
        return SANDBOX_ALL_FLAGS;
      }

      nsAttrValue attr;
      attr.ParseAtomArray(flags);

      return nsContentUtils::ParseSandboxAttributeToFlags(&attr);
    }
  }

  return SANDBOXED_NONE;
}

void nsCSPPolicy::getReportURIs(nsTArray<nsString>& outReportURIs) const {
  for (uint32_t i = 0; i < mDirectives.Length(); i++) {
    if (mDirectives[i]->equals(
            nsIContentSecurityPolicy::REPORT_URI_DIRECTIVE)) {
      mDirectives[i]->getReportURIs(outReportURIs);
      return;
    }
  }
}

void nsCSPPolicy::getReportGroup(nsAString& outReportGroup) const {
  for (uint32_t i = 0; i < mDirectives.Length(); i++) {
    if (mDirectives[i]->equals(nsIContentSecurityPolicy::REPORT_TO_DIRECTIVE)) {
      mDirectives[i]->getReportGroup(outReportGroup);
      return;
    }
  }
}

void nsCSPPolicy::getDirectiveNames(nsTArray<nsString>& outDirectives) const {
  for (uint32_t i = 0; i < mDirectives.Length(); i++) {
    nsAutoString name;
    mDirectives[i]->getDirName(name);
    outDirectives.AppendElement(name);
  }
}

bool nsCSPPolicy::visitDirectiveSrcs(CSPDirective aDir,
                                     nsCSPSrcVisitor* aVisitor) const {
  for (uint32_t i = 0; i < mDirectives.Length(); i++) {
    if (mDirectives[i]->equals(aDir)) {
      return mDirectives[i]->visitSrcs(aVisitor);
    }
  }
  return false;
}
