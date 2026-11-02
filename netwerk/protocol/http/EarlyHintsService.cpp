/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "EarlyHintsService.h"

#include "EarlyHintPreconnect.h"
#include "EarlyHintPreloader.h"
#include "mozilla/PreloadHashKey.h"
#include "mozilla/StoragePrincipalHelper.h"
#include "mozilla/dom/LinkStyle.h"
#include "nsContentUtils.h"
#include "nsIChannel.h"
#include "nsICookieJarSettings.h"
#include "nsILoadInfo.h"
#include "nsIPrincipal.h"
#include "nsNetUtil.h"
#include "nsString.h"

namespace mozilla::net {

EarlyHintsService::EarlyHintsService()
    : mOngoingEarlyHints(new OngoingEarlyHints()) {}

// implementing the destructor in the .cpp file to allow EarlyHintsService.h
// not to include EarlyHintPreloader.h, decoupling the two files and hopefully
// allow faster compile times
EarlyHintsService::~EarlyHintsService() = default;

void EarlyHintsService::EarlyHint(
    const nsACString& aLinkHeader, nsIURI* aBaseURI, nsIChannel* aChannel,
    const nsACString& aReferrerPolicy, const nsACString& aCSPHeader,
    const nsACString& aConnectionAllowlistHeader,
    dom::CanonicalBrowsingContext* aLoadingBrowsingContext) {
  mEarlyHintsCount++;
  if (mFirstEarlyHint.isNothing()) {
    mFirstEarlyHint.emplace(TimeStamp::NowLoRes());
  } else {
    // Only allow one early hint response with link headers. See
    // https://html.spec.whatwg.org/multipage/semantics.html#early-hints
    // > Note: Only the first early hint response served during the navigation
    // > is handled, and it is discarded if it is succeeded by a cross-origin
    // > redirect.
    return;
  }

  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  // We only follow Early Hints sent on the main document. Make sure that we got
  // the main document channel here.
  if (loadInfo->GetExternalContentPolicyType() !=
      ExtContentPolicy::TYPE_DOCUMENT) {
    MOZ_ASSERT(false, "Early Hint on non-document channel");
    return;
  }
  nsCOMPtr<nsIPrincipal> principal;
  // We want to set the top-level document as the triggeringPrincipal for the
  // load of the sub-resources (image, font, fetch, script, style, fetch and in
  // the future maybe more). We can't use the `triggeringPrincipal` of the main
  // document channel, because it is the `systemPrincipal` for user initiated
  // loads. Same for the `LoadInfo::FindPrincipalToInherit(aChannel)`.
  //
  // On 3xx redirects of the main document to cross site locations, all Early
  // Hint preloads get cancelled as specified in the whatwg spec:
  //
  //   Note: Only the first early hint response served during the navigation is
  //   handled, and it is discarded if it is succeeded by a cross-origin
  //   redirect. [1]
  //
  // Therefore the channel doesn't need to change the principal for any reason
  // and has the correct principal for the whole lifetime.
  //
  // [1]: https://html.spec.whatwg.org/multipage/semantics.html#early-hints
  nsresult rv = nsContentUtils::GetSecurityManager()->GetChannelResultPrincipal(
      aChannel, getter_AddRefs(principal));
  NS_ENSURE_SUCCESS_VOID(rv);

  nsCOMPtr<nsICookieJarSettings> cookieJarSettings;
  if (NS_FAILED(
          loadInfo->GetCookieJarSettings(getter_AddRefs(cookieJarSettings)))) {
    return;
  }

  // TODO: find out why LinkHeaderParser uses utf16 and check if it can be
  //       changed to utf8
  auto linkHeaders = ParseLinkHeader(NS_ConvertUTF8toUTF16(aLinkHeader));

  for (auto& linkHeader : linkHeaders) {
    if (linkHeader.mRel.LowerCaseEqualsLiteral("preconnect")) {
      mLinkType |= dom::LinkStyle::ePRECONNECT;
      OriginAttributes originAttributes;
      StoragePrincipalHelper::GetOriginAttributesForNetworkState(
          aChannel, originAttributes);
      EarlyHintPreconnect::MaybePreconnect(linkHeader, aBaseURI,
                                           std::move(originAttributes));
    } else if (linkHeader.mRel.LowerCaseEqualsLiteral("preload")) {
      mLinkType |= dom::LinkStyle::ePRELOAD;
      EarlyHintPreloader::MaybeCreateAndInsertPreload(
          mOngoingEarlyHints, linkHeader, aBaseURI, principal,
          cookieJarSettings, aReferrerPolicy, aCSPHeader,
          aConnectionAllowlistHeader, loadInfo->GetBrowsingContextID(),
          aLoadingBrowsingContext, false);
    } else if (linkHeader.mRel.LowerCaseEqualsLiteral("modulepreload")) {
      mLinkType |= dom::LinkStyle::eMODULE_PRELOAD;
      EarlyHintPreloader::MaybeCreateAndInsertPreload(
          mOngoingEarlyHints, linkHeader, aBaseURI, principal,
          cookieJarSettings, aReferrerPolicy, aCSPHeader,
          aConnectionAllowlistHeader, loadInfo->GetBrowsingContextID(),
          aLoadingBrowsingContext, true);
    } else if (linkHeader.mRel.LowerCaseEqualsLiteral(
                   "compression-dictionary")) {
      mLinkType |= dom::LinkStyle::eCOMPRESSION_DICTIONARY;
      EarlyHintPreloader::MaybeCreateAndInsertPreload(
          mOngoingEarlyHints, linkHeader, aBaseURI, principal,
          cookieJarSettings, aReferrerPolicy, aCSPHeader,
          aConnectionAllowlistHeader, loadInfo->GetBrowsingContextID(),
          aLoadingBrowsingContext, true);
    }
  }
}

void EarlyHintsService::Cancel(const nsACString& aReason) {
  Reset();
  mOngoingEarlyHints->CancelAll(aReason);
}

void EarlyHintsService::RegisterLinksAndGetConnectArgs(
    dom::ContentParentId aCpId, nsTArray<EarlyHintConnectArgs>& aOutLinks) {
  mOngoingEarlyHints->RegisterLinksAndGetConnectArgs(aCpId, aOutLinks);
}

void EarlyHintsService::Reset() {
  if (mEarlyHintsCount == 0) {
    return;
  }
  // Reset telemetry counters and timestamps.
  mEarlyHintsCount = 0;
  mFirstEarlyHint = Nothing();
}

}  // namespace mozilla::net
