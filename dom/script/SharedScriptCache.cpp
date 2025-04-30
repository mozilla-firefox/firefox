/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedScriptCache.h"

#include "mozilla/Maybe.h"   // Maybe, Some, Nothing
#include "mozilla/Unused.h"  // Unused
#include "nsIPrefService.h"  // NS_PREFSERVICE_CONTRACTID
#include "nsIPrefBranch.h"   // nsIPrefBranch, NS_PREFBRANCH_PREFCHANGE_TOPIC_ID
#include "nsISupportsImpl.h"    // NS_IMPL_ISUPPORTS
#include "nsIMemoryReporter.h"  // nsIMemoryReporter, MOZ_DEFINE_MALLOC_SIZE_OF, RegisterWeakMemoryReporter, UnregisterWeakMemoryReporter, MOZ_COLLECT_REPORT, KIND_HEAP, UNITS_BYTES
#include "mozilla/dom/ContentParent.h"  // dom::ContentParent
#include "nsIPrincipal.h"               // nsIPrincipal
#include "nsStringFwd.h"                // nsACString
#include "ScriptLoader.h"               // ScriptLoader
#include "ScriptLoadHandler.h"          // ScriptLoadHandler

namespace mozilla::dom {

ScriptHashKey::ScriptHashKey(ScriptLoader* aLoader,
                             const JS::loader::ScriptLoadRequest* aRequest)
    : PLDHashEntryHdr(),
      mURI(aRequest->mURI),
      mLoaderPrincipal(aLoader->LoaderPrincipal()),
      mPartitionPrincipal(aLoader->PartitionedPrincipal()),
      mCORSMode(aRequest->CORSMode()),
      mSRIMetadata(aRequest->mIntegrity),
      mKind(aRequest->mKind),
      mNonce(aRequest->Nonce()),
      mIsLinkRelPreload(aRequest->GetScriptLoadContext()->IsPreload()) {
  if (mKind == JS::loader::ScriptKind::eClassic) {
    if (aRequest->GetScriptLoadContext()->HasScriptElement()) {
      aRequest->GetScriptLoadContext()->GetHintCharset(mHintCharset);
    }
  }

  MOZ_COUNT_CTOR(ScriptHashKey);
}

ScriptHashKey::ScriptHashKey(const ScriptLoadData& aLoadData)
    : ScriptHashKey(aLoadData.CacheKey()) {}

bool ScriptHashKey::KeyEquals(const ScriptHashKey& aKey) const {
  if (mKind != aKey.mKind) {
    return false;
  }

  {
    bool eq;
    if (NS_FAILED(mURI->Equals(aKey.mURI, &eq)) || !eq) {
      return false;
    }
  }

  if (!mPartitionPrincipal->Equals(aKey.mPartitionPrincipal)) {
    return false;
  }

  if (mCORSMode != aKey.mCORSMode) {
    return false;
  }

  if (mNonce != aKey.mNonce) {
    return false;
  }

  // NOTE: module always use UTF-8.
  if (mKind == JS::loader::ScriptKind::eClassic) {
    if (mHintCharset != aKey.mHintCharset) {
      return false;
    }
  }

  if (!mSRIMetadata.CanTrustBeDelegatedTo(aKey.mSRIMetadata) ||
      !aKey.mSRIMetadata.CanTrustBeDelegatedTo(mSRIMetadata)) {
    return false;
  }

  return true;
}

NS_IMPL_ISUPPORTS(ScriptLoadData, nsISupports)

ScriptLoadData::ScriptLoadData(ScriptLoader* aLoader,
                               JS::loader::ScriptLoadRequest* aRequest)
    : mExpirationTime(aRequest->ExpirationTime()),
      mLoader(aLoader),
      mKey(aLoader, aRequest),
      mLoadedScript(aRequest->getLoadedScript()),
      mNetworkMetadata(aRequest->mNetworkMetadata) {}

NS_IMPL_ISUPPORTS(SharedScriptCache, nsIMemoryReporter, nsIObserver)

MOZ_DEFINE_MALLOC_SIZE_OF(SharedScriptCacheMallocSizeOf)

SharedScriptCache::SharedScriptCache() = default;

void SharedScriptCache::Init() {
  RegisterWeakMemoryReporter(this);

  // URL classification (tracking protection etc) are handled inside
  // nsHttpChannel.
  // The cache reflects the policy for whether to block or not, and once
  // the policy is modified, we should discard the cache, to avoid running
  // a cached script which is supposed to be blocked.
  nsCOMPtr<nsIPrefBranch> prefs = do_GetService(NS_PREFSERVICE_CONTRACTID);
  if (prefs) {
    prefs->AddObserver("urlclassifier", this, false);
    prefs->AddObserver("privacy.trackingprotection.enabled", this, false);
  }
}

SharedScriptCache::~SharedScriptCache() { UnregisterWeakMemoryReporter(this); }

void SharedScriptCache::LoadCompleted(SharedScriptCache* aCache,
                                      ScriptLoadData& aData) {}

NS_IMETHODIMP
SharedScriptCache::CollectReports(nsIHandleReportCallback* aHandleReport,
                                  nsISupports* aData, bool aAnonymize) {
  MOZ_COLLECT_REPORT("explicit/js-non-window/cache", KIND_HEAP, UNITS_BYTES,
                     SharedScriptCacheMallocSizeOf(this) +
                         SizeOfExcludingThis(SharedScriptCacheMallocSizeOf),
                     "Memory used for SharedScriptCache to share script "
                     "across documents");
  return NS_OK;
}

NS_IMETHODIMP
SharedScriptCache::Observe(nsISupports* aSubject, const char* aTopic,
                           const char16_t* aData) {
  if (strcmp(aTopic, NS_PREFBRANCH_PREFCHANGE_TOPIC_ID) == 0) {
    SharedScriptCache::Clear();
  }

  return NS_OK;
}

void SharedScriptCache::Clear(const Maybe<bool>& aChrome,
                              const Maybe<nsCOMPtr<nsIPrincipal>>& aPrincipal,
                              const Maybe<nsCString>& aSchemelessSite,
                              const Maybe<OriginAttributesPattern>& aPattern,
                              const Maybe<nsCString>& aURL) {
  using ContentParent = dom::ContentParent;

  if (XRE_IsParentProcess()) {
    for (auto* cp : ContentParent::AllProcesses(ContentParent::eLive)) {
      Unused << cp->SendClearScriptCache(aChrome, aPrincipal, aSchemelessSite,
                                         aPattern, aURL);
    }
  }

  if (sSingleton) {
    sSingleton->ClearInProcess(aChrome, aPrincipal, aSchemelessSite, aPattern,
                               aURL);
  }
}

/* static */
void SharedScriptCache::PrepareForLastCC() {
  if (sSingleton) {
    sSingleton->mComplete.Clear();
    sSingleton->mPending.Clear();
    sSingleton->mLoading.Clear();
  }
}

}  // namespace mozilla::dom
