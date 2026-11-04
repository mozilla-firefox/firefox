/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ConnectionAllowlists.h"

#include <functional>
#include <utility>

#include "mozilla/Logging.h"
#include "mozilla/StaticPrefs_security.h"
#include "mozilla/dom/ConnectionAllowlistViolationReportBody.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/ReportingUtils.h"
#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "mozilla/net/SFV.h"
#include "mozilla/net/URLPatternGlue.h"
#include "nsGlobalWindowInner.h"
#include "nsIGlobalObject.h"
#include "nsILoadInfo.h"
#include "nsIURIMutator.h"
#include "nsNetUtil.h"
#include "nsScriptSecurityManager.h"
#include "nsString.h"

using namespace mozilla;

static LazyLogModule sConnectionAllowlistsLog("ConnectionAllowlists");
#define LOG(fmt, ...) \
  MOZ_LOG_FMT(sConnectionAllowlistsLog, LogLevel::Debug, fmt, ##__VA_ARGS__)

namespace mozilla::dom {

void ConnectionAllowlists::Allowlist::AppendPattern(
    const nsACString& aSerializedPattern) {
  UrlPatternGlue pattern = nullptr;
  UrlPatternOptions options{};
  if (!urlpattern_parse_pattern_from_string(&aSerializedPattern, nullptr,
                                            options, &pattern)) {
    LOG("Failed to parse URLPattern: {}", aSerializedPattern);
    return;
  }

  mPatterns.AppendElement(UrlPattern(std::move(pattern)));
  mSerializedPatterns.AppendElement(aSerializedPattern);
}

// https://w3c.github.io/connection-allowlists/#abstract-opdef-parse-a-connection-allowlist-header
/* static */
Maybe<ConnectionAllowlists::Allowlist>
ConnectionAllowlists::ParseConnectionAllowlistHeader(const nsACString& aHeader,
                                                     Disposition aDisposition) {
  if (aHeader.IsEmpty()) {
    return Nothing();
  }

  // 1. If list's size is 0, return null.
  auto list = net::SFV::ParseList(aHeader);
  if (!list.IsValid() || list.Length() == 0) {
    LOG("Failed to parse header as a structured field list.");
    return Nothing();
  }

  // 2. If list[0] is not an inner list, return null.
  auto innerList = list.GetInnerListAt(0);
  if (!innerList.IsValid()) {
    LOG("First list member is not an inner list.");
    return Nothing();
  }

  // 3. Let allowlist be a Connection Allowlist whose disposition is
  // disposition.
  ConnectionAllowlists::Allowlist allowlist;
  allowlist.mDisposition = aDisposition;

  // 4. For each item in list[0]:
  size_t length = innerList.Length();
  for (size_t i = 0; i < length; i++) {
    auto item = innerList.GetItemAt(i);
    if (!item.IsValid()) {
      continue;
    }

    // 4.1. Let serialized pattern be null.
    Maybe<nsAutoCString> serializedPattern;

    nsAutoCString token;
    nsAutoCString string;
    if (NS_SUCCEEDED(item.GetValue<net::SFV::Token>(token))) {
      // 4.2. If item is the token `response-origin`:
      // 4.2.1. Set serialized pattern to the ASCII serialization of
      // response-url's origin.
      //
      // Unlike the spec we remember the token instead of resolving it here,
      // see the comment on mMatchesResponseOrigin.
      if (token.EqualsLiteral("response-origin")) {
        allowlist.mMatchesResponseOrigin = true;
      }
    } else if (NS_SUCCEEDED(item.GetValue<net::SFV::SFVString>(string))) {
      // 4.3. If item is a string, set serialized pattern to item.
      serializedPattern.emplace(string);
    }

    // 4.4. If serialized pattern is null, continue.
    if (serializedPattern.isNothing()) {
      continue;
    }

    // 4.5. Let URL pattern be the result of executing build a URL pattern from
    // an HTTP structured field value given serialized pattern with null as the
    // base URL. If this step throws an error, continue.
    //
    // 4.6. Append URL pattern to allowlist's allowlist.
    allowlist.AppendPattern(*serializedPattern);
  }

  // 5. For each key → value in list[0]'s parameters:
  nsAutoCString paramValue;

  // 5.1. If key is `report-to` and value is a token, set allowlist's reporting
  // endpoint to value.
  if (NS_SUCCEEDED(
          innerList.GetParam<net::SFV::Token>("report-to"_ns, paramValue))) {
    allowlist.mReportingEndpoint = paramValue;
  }

  // 5.2. If key is `redirects` and value is a token: If value is `block`, set
  // redirects to block. Else, set redirects to allow.
  if (NS_SUCCEEDED(
          innerList.GetParam<net::SFV::Token>("redirects"_ns, paramValue))) {
    allowlist.mRedirects = paramValue.EqualsLiteral("block")
                               ? ConnectionAllowlists::Redirects::Block
                               : ConnectionAllowlists::Redirects::Allow;
  }

  // 5.3. If key is `webrtc` and value is a token: If value is `block`, set
  // webrtc to block. Else, set webrtc to allow.
  if (NS_SUCCEEDED(
          innerList.GetParam<net::SFV::Token>("webrtc"_ns, paramValue))) {
    allowlist.mWebRTC = paramValue.EqualsLiteral("block")
                            ? ConnectionAllowlists::WebRTC::Block
                            : ConnectionAllowlists::WebRTC::Allow;
  }

  // 6. Return allowlist.
  return Some(std::move(allowlist));
}

/* static */
nsresult ConnectionAllowlists::ParseHeaders(const nsACString& aHeader,
                                            const nsACString& aReportOnlyHeader,
                                            ConnectionAllowlists** aResult) {
  *aResult = nullptr;

  if (!StaticPrefs::security_connection_allowlists_enabled()) {
    return NS_OK;
  }

  Maybe<Allowlist> enforcement =
      ParseConnectionAllowlistHeader(aHeader, Disposition::Enforce);
  Maybe<Allowlist> reportOnly =
      ParseConnectionAllowlistHeader(aReportOnlyHeader, Disposition::Report);

  if (enforcement.isNothing() && reportOnly.isNothing()) {
    return NS_OK;
  }

  RefPtr<ConnectionAllowlists> allowlists = new ConnectionAllowlists();
  allowlists->mEnforcement = std::move(enforcement);
  allowlists->mReportOnly = std::move(reportOnly);
  allowlists.forget(aResult);
  return NS_OK;
}

void ConnectionAllowlists::SetResponseURI(nsIURI* aURI) {
  MOZ_ASSERT(!mFrozen);
  mResponseURI = aURI;
}

bool ConnectionAllowlists::ShouldLoad(nsIURI* aURI,
                                      nsILoadInfo* aLoadInfo) const {
  // TODO: We probably need to exempt some content like in SubjectToCSP.
  // TODO: Requests vs URL.

  nsCOMPtr<nsIURI> uri = aURI;

  // Per https://fetch.spec.whatwg.org/#concept-main-fetch, the
  // upgrade-insecure-requests and mixed content upgrades (steps 5 and 6) happen
  // before the Connection Allowlists check (step 7), so match the upgraded URL.
  // Gecko performs these upgrades later as a REDIRECT_STS_UPGRADE redirect,
  // which ConnectionAllowlistsService::ConsultForRedirect ignores.
  if (NS_WillUpgradeInsecureRequest(uri, aLoadInfo) &&
      NS_WARN_IF(
          NS_FAILED(NS_GetSecureUpgradedURI(aURI, getter_AddRefs(uri))))) {
    return false;
  }

  switch (aLoadInfo->GetExternalContentPolicyType()) {
    case ExtContentPolicyType::TYPE_DOCUMENT:
    case ExtContentPolicyType::TYPE_SUBDOCUMENT:
      // Per
      // https://html.spec.whatwg.org/multipage/browsing-the-web.html#navigate-to-a-javascript:-url,
      // documents loaded via javascript: URL don't trigger a real request, so
      // they also shouldn't be blockable by the Connection Allowlists.
      if (uri->SchemeIs("javascript")) {
        return true;
      }
      break;

    case ExtContentPolicyType::TYPE_WEBSOCKET:
      // Per
      // https://websockets.spec.whatwg.org/#concept-websocket-establish,
      // the request URL of a WebSocket has its ws: or wss: scheme replaced
      // with http: or https:, so match the allowlist against that URL.
      MOZ_ASSERT(uri->SchemeIs("ws") || uri->SchemeIs("wss"));
      if (NS_WARN_IF(NS_FAILED(
              NS_MutateURI(uri)
                  .SetScheme(uri->SchemeIs("wss") ? "https"_ns : "http"_ns)
                  .Finalize(uri)))) {
        return false;
      }
      break;

    case ExtContentPolicyType::TYPE_INVALID:
      MOZ_ASSERT_UNREACHABLE("TYPE_INVALID");
      break;

    default:
      break;
  }

  return !ShouldBlockURL(uri, aLoadInfo);
}

// https://w3c.github.io/connection-allowlists/#abstract-opdef-match-a-url-to-a-connection-allowlist
bool ConnectionAllowlists::MatchURL(nsIURI* aURI,
                                    const Allowlist& aAllowlist) const {
  // 1. If url is local, return success.
  // TODO: helper? add chrome:?
  if (aURI->SchemeIs("about") || aURI->SchemeIs("data") ||
      aURI->SchemeIs("blob")) {
    return true;
  }

  // If list included the `response-origin` token, then allow all same-origin
  // URLs.
  if (aAllowlist.mMatchesResponseOrigin && mResponseURI) {
    if (nsScriptSecurityManager::SecurityCompareURIs(mResponseURI, aURI)) {
      return true;
    }
  }

  // XXX This seems like something we should have abstracted.
  nsAutoCString spec;
  if (NS_WARN_IF(NS_FAILED(aURI->GetSpec(spec)))) {
    return false;
  }
  UrlPatternInput input = net::CreateUrlPatternInput(spec);

  // 2. For each pattern in connection allowlist’s allowlist:
  for (const auto& pattern : aAllowlist.mPatterns) {
    // 2.1. If URL pattern matching given pattern and url does not return null,
    // return success.
    if (net::UrlPatternTest(pattern.get(), input, Nothing())) {
      return true;
    }
  }

  // 3. Return failure.
  return false;
}

// https://w3c.github.io/connection-allowlists/#abstract-opdef-should-url-be-blocked-by-connection-allowlists
bool ConnectionAllowlists::ShouldBlockURL(nsIURI* aURI,
                                          nsILoadInfo* aLoadInfo) const {
  // 1. For each connection allowlist in connection allowlists:
  for (const Maybe<Allowlist>& allowlist :
       {std::cref(mEnforcement), std::cref(mReportOnly)}) {
    if (allowlist.isNothing()) {
      continue;
    }

    // 1.1. If url matches connection allowlist, continue.
    if (MatchURL(aURI, *allowlist)) {
      continue;
    }

    // 1.2. Report a violation given url, environment, and connection
    // allowlist.
    ReportBlockedRequest(aURI, aLoadInfo, *allowlist, /* aIsRedirect */ false);

    // 1.3. If connection allowlist's disposition is enforce, return blocked.
    if (allowlist->mDisposition == Disposition::Enforce) {
      LOG("Blocking URL: {}", aURI->GetSpecOrDefault());
      return true;
    }
  }

  // 2. Return allowed.
  return false;
}

// The redirect steps of
// https://w3c.github.io/connection-allowlists/#abstract-opdef-should-request-be-blocked-by-connection-allowlists
bool ConnectionAllowlists::ShouldBlockRedirect(nsIURI* aOriginalURI,
                                               nsILoadInfo* aLoadInfo) const {
  // 2. For each allowlist in allowlists:
  for (const Maybe<Allowlist>& allowlist :
       {std::cref(mEnforcement), std::cref(mReportOnly)}) {
    if (allowlist.isNothing()) {
      continue;
    }

    // 2.1. If request’s URL list’s size is greater than 1:
    // 2.1.1. If allowlist’s redirects is allow, continue.
    if (allowlist->mRedirects == Redirects::Allow) {
      continue;
    }

    // 2.1.2. Report a violation given request’s url, request’s client, and
    // allowlist.
    ReportBlockedRequest(aOriginalURI, aLoadInfo, *allowlist,
                         /* aIsRedirect */ true);

    // 2.1.3. If allowlist’s disposition is enforce, return blocked.
    if (allowlist->mDisposition == Disposition::Enforce) {
      LOG("Blocking redirect of: {}", aOriginalURI->GetSpecOrDefault());
      return true;
    }
  }

  // 3. Return allowed.
  return false;
}

// https://w3c.github.io/connection-allowlists/#abstract-opdef-should-webrtc-be-blocked-by-connection-allowlists
bool ConnectionAllowlists::ShouldBlockWebRTC(
    nsPIDOMWindowInner* aWindow) const {
  // 1. Let allowlists be environment’s policy container’s connection
  // allowlists.
  // 2. For each allowlist in allowlists:
  for (const Maybe<Allowlist>& allowlist :
       {std::cref(mEnforcement), std::cref(mReportOnly)}) {
    if (allowlist.isNothing()) {
      continue;
    }

    // 2.1. If allowlist’s webrtc is allow, continue.
    if (allowlist->mWebRTC == WebRTC::Allow) {
      continue;
    }

    // Report violations to the web console.
    Resource resource{nsCString("webrtc"_ns)};
    ReportToConsole(resource, aWindow->WindowID(), *allowlist);

    // 2.2. Report a violation given "webrtc", environment, and allowlist.
    ReportViolation(resource, aWindow, *allowlist);

    // 2.3. If allowlist’s disposition is enforce, return blocked.
    if (allowlist->mDisposition == Disposition::Enforce) {
      LOG("Blocking WebRTC");
      return true;
    }
  }

  // 3. Return allowed.
  return false;
}

/* static */
void ConnectionAllowlists::ReportBlockedRequest(nsIURI* aURI,
                                                nsILoadInfo* aLoadInfo,
                                                const Allowlist& aAllowlist,
                                                bool aIsRedirect) {
  // Report violations to the web console, except for preloads.
  Resource resource{aURI};
  if (!nsContentUtils::IsPreloadType(aLoadInfo->InternalContentPolicyType())) {
    ReportToConsole(resource, aLoadInfo->GetInnerWindowID(), aAllowlist,
                    aIsRedirect);
  }

  // TODO: This will fail when blocking in the parent process for e.g.
  // navigation.
  nsCOMPtr<nsPIDOMWindowInner> window =
      nsGlobalWindowInner::GetInnerWindowWithId(aLoadInfo->GetInnerWindowID());
  ReportViolation(resource, window, aAllowlist);
}

// https://w3c.github.io/connection-allowlists/#abstract-opdef-report-a-violation
/* static */
void ConnectionAllowlists::ReportViolation(const Resource& aResource,
                                           nsPIDOMWindowInner* aWindow,
                                           const Allowlist& aAllowlist) {
  // 1. If allowlist’s reporting endpoint is null, return.
  if (aAllowlist.mReportingEndpoint.IsEmpty()) {
    return;
  }

  nsCOMPtr<nsPIDOMWindowInner> window = aWindow;
  if (!window) {
    LOG("Not reporting a violation, no global for the environment.");
    return;
  }

  Document* doc = window->GetExtantDoc();
  if (NS_WARN_IF(!doc) || NS_WARN_IF(!doc->GetDocumentURI())) {
    return;
  }

  // 2. Let violation be a new ConnectionAllowlistViolationReport, initialized
  // as follows:
  //
  // url
  //   environment’s creation URL, stripped for use in reports.
  nsAutoCString url;
  ReportingUtils::StripURL(doc->GetDocumentURI(), url);

  // connection
  //   If resource URL is a URL, then resource URL, stripped for use in reports.
  //   Otherwise, resource URL.
  nsAutoCString connection;
  if (aResource.is<nsIURI*>()) {
    nsCOMPtr<nsIURI> uri = aResource.as<nsIURI*>();
    ReportingUtils::StripURL(uri, connection);
  } else {
    connection = aResource.as<nsCString>();
  }

  // allowlist
  //   A new list containing the result of serializing each pattern in
  //   allowlist’s allowlist
  //
  // disposition
  //   allowlist’s disposition.
  nsIGlobalObject* global = window->AsGlobal();
  RefPtr<ConnectionAllowlistViolationReportBody> violation =
      new ConnectionAllowlistViolationReportBody(
          global, url, connection, aAllowlist.mSerializedPatterns.Clone(),
          aAllowlist.mDisposition == Disposition::Enforce
              ? ConnectionAllowlistDisposition::Enforce
              : ConnectionAllowlistDisposition::Report);

  // 3. Generate and queue a report given environment as the context,
  // "connection-allowlist" as the type, allowlist’s reporting endpoint as the
  // destination, and violation as the data.
  ReportingUtils::Report(global, nsGkAtoms::connection_allowlist,
                         aAllowlist.mReportingEndpoint, url, violation);
}

/* static */
void ConnectionAllowlists::ReportToConsole(const Resource& aResource,
                                           uint64_t aInnerWindowID,
                                           const Allowlist& aAllowlist,
                                           bool aIsRedirect) {
  const bool enforce = aAllowlist.mDisposition == Disposition::Enforce;
  const char* messageKey;
  if (aIsRedirect) {
    messageKey = enforce ? "ConnectionAllowlistRedirectBlocked"
                         : "ConnectionAllowlistReportOnlyRedirectBlocked";
  } else {
    messageKey = enforce ? "ConnectionAllowlistBlocked"
                         : "ConnectionAllowlistReportOnlyBlocked";
  }
  AutoTArray<nsString, 1> params;
  nsString* param = params.AppendElement();
  if (aResource.is<nsIURI*>()) {
    CopyUTF8toUTF16(aResource.as<nsIURI*>()->GetSpecOrDefault(), *param);
  } else {
    CopyUTF8toUTF16(aResource.as<nsCString>(), *param);
  }
  nsAutoString localizedMsg;
  nsresult rv = nsContentUtils::FormatLocalizedString(
      PropertiesFile::SECURITY_PROPERTIES, messageKey, params, localizedMsg);
  NS_ENSURE_SUCCESS_VOID(rv);

  nsContentUtils::ReportToConsoleByWindowID(
      localizedMsg,
      enforce ? nsIScriptError::errorFlag : nsIScriptError::warningFlag,
      "Security"_ns, aInnerWindowID);
}

void ConnectionAllowlists::Allowlist::ToEntryArgs(
    mozilla::ipc::ConnectionAllowlistEntry& aEntry) const {
  aEntry.patterns() = mSerializedPatterns.Clone();
  aEntry.matchesResponseOrigin() = mMatchesResponseOrigin;
  aEntry.reportingEndpoint() = mReportingEndpoint;
  aEntry.allowRedirects() = mRedirects == Redirects::Allow;
  aEntry.allowWebRTC() = mWebRTC == WebRTC::Allow;
}

/* static */
ConnectionAllowlists::Allowlist ConnectionAllowlists::Allowlist::FromEntryArgs(
    const mozilla::ipc::ConnectionAllowlistEntry& aEntry,
    Disposition aDisposition) {
  Allowlist allowlist;
  allowlist.mDisposition = aDisposition;

  for (const nsCString& serializedPattern : aEntry.patterns()) {
    allowlist.AppendPattern(serializedPattern);
  }
  allowlist.mMatchesResponseOrigin = aEntry.matchesResponseOrigin();
  allowlist.mReportingEndpoint = aEntry.reportingEndpoint();
  allowlist.mRedirects =
      aEntry.allowRedirects() ? Redirects::Allow : Redirects::Block;
  allowlist.mWebRTC = aEntry.allowWebRTC() ? WebRTC::Allow : WebRTC::Block;

  return allowlist;
}

void ConnectionAllowlists::ToArgs(
    mozilla::ipc::ConnectionAllowlistsArgs& aArgs) const {
  aArgs.enforcement() = Nothing();
  aArgs.reportOnly() = Nothing();
  aArgs.responseURISpec().Truncate();

  if (mEnforcement) {
    mozilla::ipc::ConnectionAllowlistEntry entry;
    mEnforcement->ToEntryArgs(entry);
    aArgs.enforcement() = Some(std::move(entry));
  }

  if (mReportOnly) {
    mozilla::ipc::ConnectionAllowlistEntry entry;
    mReportOnly->ToEntryArgs(entry);
    aArgs.reportOnly() = Some(std::move(entry));
  }

  if (mResponseURI &&
      NS_WARN_IF(NS_FAILED(mResponseURI->GetSpec(aArgs.responseURISpec())))) {
    aArgs.responseURISpec().Truncate();
  }
}

/* static */
already_AddRefed<ConnectionAllowlists> ConnectionAllowlists::FromArgs(
    const mozilla::ipc::ConnectionAllowlistsArgs& aArgs) {
  if (aArgs.enforcement().isNothing() && aArgs.reportOnly().isNothing()) {
    return nullptr;
  }

  RefPtr<ConnectionAllowlists> allowlists = new ConnectionAllowlists();

  if (aArgs.enforcement().isSome()) {
    allowlists->mEnforcement.emplace(
        Allowlist::FromEntryArgs(*aArgs.enforcement(), Disposition::Enforce));
  }

  if (aArgs.reportOnly().isSome()) {
    allowlists->mReportOnly.emplace(
        Allowlist::FromEntryArgs(*aArgs.reportOnly(), Disposition::Report));
  }

  if (!aArgs.responseURISpec().IsEmpty()) {
    nsCOMPtr<nsIURI> responseURI;
    if (NS_SUCCEEDED(
            NS_NewURI(getter_AddRefs(responseURI), aArgs.responseURISpec()))) {
      allowlists->mResponseURI = std::move(responseURI);
    } else {
      LOG("Failed to parse responseURISpec: {}", aArgs.responseURISpec());
    }
  }

  return allowlists.forget();
}

}  // namespace mozilla::dom

#undef LOG
