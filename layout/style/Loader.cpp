/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* loading of CSS style sheets using the network APIs */

#include "mozilla/css/Loader.h"

#include "MainThreadUtils.h"
#include "mozilla/ArrayUtils.h"
#include "mozilla/css/ErrorReporter.h"
#include "mozilla/dom/DocGroup.h"
#include "mozilla/dom/FetchPriority.h"
#include "mozilla/dom/SRILogHelper.h"
#include "mozilla/IntegerPrintfMacros.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/LoadInfo.h"
#include "mozilla/Logging.h"
#include "mozilla/glean/LayoutMetrics.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/PreloadHashKey.h"
#include "mozilla/ResultExtensions.h"
#include "mozilla/URLPreloader.h"
#include "nsIPrincipal.h"
#include "nsISupportsPriority.h"
#include "nsITimedChannel.h"
#include "nsICachingChannel.h"
#include "nsSyncLoadService.h"
#include "nsContentSecurityManager.h"
#include "nsCOMPtr.h"
#include "nsString.h"
#include "nsIContent.h"
#include "nsIContentInlines.h"
#include "nsICookieJarSettings.h"
#include "mozilla/dom/Document.h"
#include "nsIURI.h"
#include "nsContentUtils.h"
#include "nsHttpChannel.h"
#include "nsIScriptSecurityManager.h"
#include "nsContentPolicyUtils.h"
#include "nsIHttpChannel.h"
#include "nsIHttpChannelInternal.h"
#include "nsIClassOfService.h"
#include "nsIScriptError.h"
#include "nsMimeTypes.h"
#include "nsICSSLoaderObserver.h"
#include "nsThreadUtils.h"
#include "nsINetworkPredictor.h"
#include "nsQueryActor.h"
#include "nsQueryObject.h"
#include "nsStringStream.h"
#include "mozilla/dom/MediaList.h"
#include "mozilla/dom/ShadowRoot.h"
#include "mozilla/dom/URL.h"
#include "mozilla/net/UrlClassifierFeatureFactory.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/ServoBindings.h"
#include "mozilla/StyleSheet.h"
#include "mozilla/StyleSheetInlines.h"
#include "mozilla/ConsoleReportCollector.h"
#include "mozilla/css/StreamLoader.h"
#include "mozilla/SharedStyleSheetCache.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/Try.h"
#include "ReferrerInfo.h"

#include "nsXULPrototypeCache.h"

#include "nsError.h"

#include "mozilla/dom/SRICheck.h"

#include "mozilla/Encoding.h"

using namespace mozilla::dom;
using namespace mozilla::net;

/**
 * OVERALL ARCHITECTURE
 *
 * The CSS Loader gets requests to load various sorts of style sheets:
 * inline style from <style> elements, linked style, @import-ed child
 * sheets, non-document sheets.  The loader handles the following tasks:
 * 1) Creation of the actual style sheet objects: CreateSheet()
 * 2) setting of the right media, title, enabled state, etc on the
 *    sheet: PrepareSheet()
 * 3) Insertion of the sheet in the proper cascade order:
 *    InsertSheetInTree() and InsertChildSheet()
 * 4) Load of the sheet: LoadSheet() including security checks
 * 5) Parsing of the sheet: ParseSheet()
 * 6) Cleanup: SheetComplete()
 *
 * The detailed documentation for these functions is found with the
 * function implementations.
 *
 * The following helper object is used:
 *    SheetLoadData -- a small class that is used to store all the
 *                     information needed for the loading of a sheet;
 *                     this class handles listening for the stream
 *                     loader completion and also handles charset
 *                     determination.
 */

extern mozilla::LazyLogModule sCssLoaderLog;
mozilla::LazyLogModule sCssLoaderLog("nsCSSLoader");

static mozilla::LazyLogModule gSriPRLog("SRI");

static bool IsPrivilegedURI(nsIURI* aURI) {
  return aURI->SchemeIs("chrome") || aURI->SchemeIs("resource");
}

#define LOG_ERROR(args) MOZ_LOG(sCssLoaderLog, mozilla::LogLevel::Error, args)
#define LOG_WARN(args) MOZ_LOG(sCssLoaderLog, mozilla::LogLevel::Warning, args)
#define LOG_DEBUG(args) MOZ_LOG(sCssLoaderLog, mozilla::LogLevel::Debug, args)
#define LOG(args) LOG_DEBUG(args)

#define LOG_ERROR_ENABLED() \
  MOZ_LOG_TEST(sCssLoaderLog, mozilla::LogLevel::Error)
#define LOG_WARN_ENABLED() \
  MOZ_LOG_TEST(sCssLoaderLog, mozilla::LogLevel::Warning)
#define LOG_DEBUG_ENABLED() \
  MOZ_LOG_TEST(sCssLoaderLog, mozilla::LogLevel::Debug)
#define LOG_ENABLED() LOG_DEBUG_ENABLED()

#define LOG_URI(format, uri)                      \
  PR_BEGIN_MACRO                                  \
  NS_ASSERTION(uri, "Logging null uri");          \
  if (LOG_ENABLED()) {                            \
    LOG((format, uri->GetSpecOrDefault().get())); \
  }                                               \
  PR_END_MACRO

// And some convenience strings...
static const char* const gStateStrings[] = {"NeedsParser", "Pending", "Loading",
                                            "Complete"};

namespace mozilla {

SheetLoadDataHashKey::SheetLoadDataHashKey(const css::SheetLoadData& aLoadData)
    : mURI(aLoadData.mURI),
      mLoaderPrincipal(aLoadData.mLoader->LoaderPrincipal()),
      mPartitionPrincipal(aLoadData.mLoader->PartitionedPrincipal()),
      mEncodingGuess(aLoadData.mGuessedEncoding),
      mCORSMode(aLoadData.mSheet->GetCORSMode()),
      mParsingMode(aLoadData.mSheet->ParsingMode()),
      mCompatMode(aLoadData.mCompatMode),
      mIsLinkRelPreloadOrEarlyHint(aLoadData.IsLinkRelPreloadOrEarlyHint()) {
  MOZ_COUNT_CTOR(SheetLoadDataHashKey);
  MOZ_ASSERT(mURI);
  MOZ_ASSERT(mLoaderPrincipal);
  MOZ_ASSERT(mPartitionPrincipal);
  aLoadData.mSheet->GetIntegrity(mSRIMetadata);
}

bool SheetLoadDataHashKey::KeyEquals(const SheetLoadDataHashKey& aKey) const {
  {
    bool eq;
    if (NS_FAILED(mURI->Equals(aKey.mURI, &eq)) || !eq) {
      return false;
    }
  }

  LOG_URI("KeyEquals(%s)\n", mURI);

  if (mParsingMode != aKey.mParsingMode) {
    LOG((" > Parsing mode mismatch\n"));
    return false;
  }

  // Chrome URIs ignore everything else.
  if (IsPrivilegedURI(mURI)) {
    return true;
  }

  if (!mPartitionPrincipal->Equals(aKey.mPartitionPrincipal)) {
    LOG((" > Partition principal mismatch\n"));
    return false;
  }

  if (mCORSMode != aKey.mCORSMode) {
    LOG((" > CORS mismatch\n"));
    return false;
  }

  if (mCompatMode != aKey.mCompatMode) {
    LOG((" > Quirks mismatch\n"));
    return false;
  }

  // If encoding differs, then don't reuse the cache.
  //
  // TODO(emilio): When the encoding is determined from the request (either
  // BOM or Content-Length or @charset), we could do a bit better,
  // theoretically.
  if (mEncodingGuess != aKey.mEncodingGuess) {
    LOG((" > Encoding guess mismatch\n"));
    return false;
  }

  // Consuming stylesheet tags must never coalesce to <link preload> initiated
  // speculative loads with a weaker SRI hash or its different value.  This
  // check makes sure that regular loads will never find such a weaker preload
  // and rather start a new, independent load with new, stronger SRI checker
  // set up, so that integrity is ensured.
  if (mIsLinkRelPreloadOrEarlyHint != aKey.mIsLinkRelPreloadOrEarlyHint) {
    const auto& linkPreloadMetadata =
        mIsLinkRelPreloadOrEarlyHint ? mSRIMetadata : aKey.mSRIMetadata;
    const auto& consumerPreloadMetadata =
        mIsLinkRelPreloadOrEarlyHint ? aKey.mSRIMetadata : mSRIMetadata;
    if (!consumerPreloadMetadata.CanTrustBeDelegatedTo(linkPreloadMetadata)) {
      LOG((" > Preload SRI metadata mismatch\n"));
      return false;
    }
  }

  return true;
}

namespace css {

static NotNull<const Encoding*> GetFallbackEncoding(
    Loader& aLoader, nsINode* aOwningNode,
    const Encoding* aPreloadOrParentDataEncoding) {
  const Encoding* encoding;
  // Now try the charset on the <link> or processing instruction
  // that loaded us
  if (aOwningNode) {
    nsAutoString label16;
    LinkStyle::FromNode(*aOwningNode)->GetCharset(label16);
    encoding = Encoding::ForLabel(label16);
    if (encoding) {
      return WrapNotNull(encoding);
    }
  }

  // Try preload or parent sheet encoding.
  if (aPreloadOrParentDataEncoding) {
    return WrapNotNull(aPreloadOrParentDataEncoding);
  }

  if (auto* doc = aLoader.GetDocument()) {
    // Use the document charset.
    return doc->GetDocumentCharacterSet();
  }

  return UTF_8_ENCODING;
}

/********************************
 * SheetLoadData implementation *
 ********************************/
NS_IMPL_ISUPPORTS(SheetLoadData, nsISupports)

SheetLoadData::SheetLoadData(
    css::Loader* aLoader, const nsAString& aTitle, nsIURI* aURI,
    StyleSheet* aSheet, SyncLoad aSyncLoad, nsINode* aOwningNode,
    IsAlternate aIsAlternate, MediaMatched aMediaMatches,
    StylePreloadKind aPreloadKind, nsICSSLoaderObserver* aObserver,
    nsIPrincipal* aTriggeringPrincipal, nsIReferrerInfo* aReferrerInfo,
    const nsAString& aNonce, FetchPriority aFetchPriority,
    already_AddRefed<SubResourceNetworkMetadataHolder>&& aNetworkMetadata)
    : mLoader(aLoader),
      mTitle(aTitle),
      mEncoding(nullptr),
      mURI(aURI),
      mSheet(aSheet),
      mPendingChildren(0),
      mSyncLoad(aSyncLoad == SyncLoad::Yes),
      mIsNonDocumentSheet(false),
      mIsChildSheet(aSheet->GetParentSheet()),
      mIsBeingParsed(false),
      mIsLoading(false),
      mIsCancelled(false),
      mMustNotify(false),
      mHadOwnerNode(!!aOwningNode),
      mWasAlternate(aIsAlternate == IsAlternate::Yes),
      mMediaMatched(aMediaMatches == MediaMatched::Yes),
      mUseSystemPrincipal(false),
      mSheetAlreadyComplete(false),
      mIsCrossOriginNoCORS(false),
      mBlockResourceTiming(false),
      mLoadFailed(false),
      mShouldEmulateNotificationsForCachedLoad(false),
      mPreloadKind(aPreloadKind),
      mObserver(aObserver),
      mTriggeringPrincipal(aTriggeringPrincipal),
      mReferrerInfo(aReferrerInfo),
      mNonce(aNonce),
      mFetchPriority{aFetchPriority},
      mGuessedEncoding(GetFallbackEncoding(*aLoader, aOwningNode, nullptr)),
      mCompatMode(aLoader->CompatMode(aPreloadKind)),
      mRecordErrors(
          aLoader && aLoader->GetDocument() &&
          css::ErrorReporter::ShouldReportErrors(*aLoader->GetDocument())),
      mNetworkMetadata(std::move(aNetworkMetadata)) {
  MOZ_ASSERT(!aOwningNode || dom::LinkStyle::FromNode(*aOwningNode),
             "Must implement LinkStyle");
  MOZ_ASSERT(mTriggeringPrincipal);
  MOZ_ASSERT(mLoader, "Must have a loader!");
}

SheetLoadData::SheetLoadData(
    css::Loader* aLoader, nsIURI* aURI, StyleSheet* aSheet,
    SheetLoadData* aParentData, nsICSSLoaderObserver* aObserver,
    nsIPrincipal* aTriggeringPrincipal, nsIReferrerInfo* aReferrerInfo,
    already_AddRefed<SubResourceNetworkMetadataHolder>&& aNetworkMetadata)
    : mLoader(aLoader),
      mEncoding(nullptr),
      mURI(aURI),
      mSheet(aSheet),
      mParentData(aParentData),
      mPendingChildren(0),
      mSyncLoad(aParentData && aParentData->mSyncLoad),
      mIsNonDocumentSheet(aParentData && aParentData->mIsNonDocumentSheet),
      mIsChildSheet(aSheet->GetParentSheet()),
      mIsBeingParsed(false),
      mIsLoading(false),
      mIsCancelled(false),
      mMustNotify(false),
      mHadOwnerNode(false),
      mWasAlternate(false),
      mMediaMatched(true),
      mUseSystemPrincipal(aParentData && aParentData->mUseSystemPrincipal),
      mSheetAlreadyComplete(false),
      mIsCrossOriginNoCORS(false),
      mBlockResourceTiming(false),
      mLoadFailed(false),
      mShouldEmulateNotificationsForCachedLoad(false),
      mPreloadKind(StylePreloadKind::None),
      mObserver(aObserver),
      mTriggeringPrincipal(aTriggeringPrincipal),
      mReferrerInfo(aReferrerInfo),
      mNonce(u""_ns),
      mFetchPriority(FetchPriority::Auto),
      mGuessedEncoding(GetFallbackEncoding(
          *aLoader, nullptr, aParentData ? aParentData->mEncoding : nullptr)),
      mCompatMode(aLoader->CompatMode(mPreloadKind)),
      mRecordErrors(
          aLoader && aLoader->GetDocument() &&
          css::ErrorReporter::ShouldReportErrors(*aLoader->GetDocument())),
      mNetworkMetadata(std::move(aNetworkMetadata)) {
  MOZ_ASSERT(mLoader, "Must have a loader!");
  MOZ_ASSERT(mTriggeringPrincipal);
  MOZ_ASSERT(!mUseSystemPrincipal || mSyncLoad,
             "Shouldn't use system principal for async loads");
  MOZ_ASSERT_IF(aParentData, mIsChildSheet);
}

SheetLoadData::SheetLoadData(
    css::Loader* aLoader, nsIURI* aURI, StyleSheet* aSheet, SyncLoad aSyncLoad,
    UseSystemPrincipal aUseSystemPrincipal, StylePreloadKind aPreloadKind,
    const Encoding* aPreloadEncoding, nsICSSLoaderObserver* aObserver,
    nsIPrincipal* aTriggeringPrincipal, nsIReferrerInfo* aReferrerInfo,
    const nsAString& aNonce, FetchPriority aFetchPriority,
    already_AddRefed<SubResourceNetworkMetadataHolder>&& aNetworkMetadata)
    : mLoader(aLoader),
      mEncoding(nullptr),
      mURI(aURI),
      mSheet(aSheet),
      mPendingChildren(0),
      mSyncLoad(aSyncLoad == SyncLoad::Yes),
      mIsNonDocumentSheet(true),
      mIsChildSheet(false),
      mIsBeingParsed(false),
      mIsLoading(false),
      mIsCancelled(false),
      mMustNotify(false),
      mHadOwnerNode(false),
      mWasAlternate(false),
      mMediaMatched(true),
      mUseSystemPrincipal(aUseSystemPrincipal == UseSystemPrincipal::Yes),
      mSheetAlreadyComplete(false),
      mIsCrossOriginNoCORS(false),
      mBlockResourceTiming(false),
      mLoadFailed(false),
      mShouldEmulateNotificationsForCachedLoad(false),
      mPreloadKind(aPreloadKind),
      mObserver(aObserver),
      mTriggeringPrincipal(aTriggeringPrincipal),
      mReferrerInfo(aReferrerInfo),
      mNonce(aNonce),
      mFetchPriority(aFetchPriority),
      mGuessedEncoding(
          GetFallbackEncoding(*aLoader, nullptr, aPreloadEncoding)),
      mCompatMode(aLoader->CompatMode(aPreloadKind)),
      mRecordErrors(
          aLoader && aLoader->GetDocument() &&
          css::ErrorReporter::ShouldReportErrors(*aLoader->GetDocument())),
      mNetworkMetadata(std::move(aNetworkMetadata)) {
  MOZ_ASSERT(mTriggeringPrincipal);
  MOZ_ASSERT(mLoader, "Must have a loader!");
  MOZ_ASSERT(!mUseSystemPrincipal || mSyncLoad,
             "Shouldn't use system principal for async loads");
  MOZ_ASSERT(!aSheet->GetParentSheet(), "Shouldn't be used for child loads");
}

SheetLoadData::~SheetLoadData() {
  MOZ_RELEASE_ASSERT(mSheetCompleteCalled || mIntentionallyDropped,
                     "Should always call SheetComplete, except when "
                     "dropping the load");
}

void SheetLoadData::StartLoading() {
  MOZ_ASSERT(!mIsLoading, "Already loading? How?");
  mIsLoading = true;
  mLoadStart = TimeStamp::Now();
}

void SheetLoadData::SetLoadCompleted() {
  MOZ_ASSERT(mIsLoading, "Not loading?");
  MOZ_ASSERT(!mLoadStart.IsNull());
  mIsLoading = false;
  // Belts and suspenders just in case.
  if (MOZ_LIKELY(!mLoadStart.IsNull())) {
    TimeDuration rawDuration = TimeStamp::Now() - mLoadStart;
    glean::performance_pageload::async_sheet_load.AccumulateRawDuration(
        rawDuration);
    // GLAM EXPERIMENT
    // This metric is temporary, disabled by default, and will be enabled only
    // for the purpose of experimenting with client-side sampling of data for
    // GLAM use. See Bug 1947604 for more information.
    glean::glam_experiment::async_sheet_load.AccumulateRawDuration(rawDuration);
  }
}

void SheetLoadData::OnCoalescedTo(const SheetLoadData& aExistingLoad) {
  if (&aExistingLoad.Loader() != &Loader()) {
    mShouldEmulateNotificationsForCachedLoad = true;
  }
  mLoadStart = TimeStamp::Now();
}

RefPtr<StyleSheet> SheetLoadData::ValueForCache() const {
  // We need to clone the sheet on insertion to the cache because otherwise the
  // stylesheets can keep full windows alive via either their JS wrapper, or via
  // StyleSheet::mRelevantGlobal.
  //
  // If this ever changes, then you also need to fix up the memory reporting in
  // both SizeOfIncludingThis and nsXULPrototypeCache::CollectMemoryReports.
  return mSheet->Clone(nullptr, nullptr);
}

void SheetLoadData::PrioritizeAsPreload(nsIChannel* aChannel) {
  if (nsCOMPtr<nsISupportsPriority> sp = do_QueryInterface(aChannel)) {
    sp->AdjustPriority(nsISupportsPriority::PRIORITY_HIGHEST);
  }
}

void SheetLoadData::StartPendingLoad() {
  mLoader->LoadSheet(*this, Loader::SheetState::NeedsParser, 0,
                     Loader::PendingLoad::Yes);
}

already_AddRefed<AsyncEventDispatcher>
SheetLoadData::PrepareLoadEventIfNeeded() {
  nsCOMPtr<nsINode> node = mSheet->GetOwnerNode();
  if (!node) {
    return nullptr;
  }
  MOZ_ASSERT(!RootLoadData().IsLinkRelPreloadOrEarlyHint(),
             "rel=preload handled elsewhere");
  RefPtr<AsyncEventDispatcher> dispatcher;
  if (BlocksLoadEvent()) {
    dispatcher = new LoadBlockingAsyncEventDispatcher(
        node, mLoadFailed ? u"error"_ns : u"load"_ns, CanBubble::eNo,
        ChromeOnlyDispatch::eNo);
  } else {
    // Fire the load event on the link, but don't block the document load.
    dispatcher =
        new AsyncEventDispatcher(node, mLoadFailed ? u"error"_ns : u"load"_ns,
                                 CanBubble::eNo, ChromeOnlyDispatch::eNo);
  }
  return dispatcher.forget();
}

nsINode* SheetLoadData::GetRequestingNode() const {
  if (nsINode* node = mSheet->GetOwnerNodeOfOutermostSheet()) {
    return node;
  }
  return mLoader->GetDocument();
}

/*********************
 * Style sheet reuse *
 *********************/

bool LoaderReusableStyleSheets::FindReusableStyleSheet(
    nsIURI* aURL, RefPtr<StyleSheet>& aResult) {
  MOZ_ASSERT(aURL);
  for (size_t i = mReusableSheets.Length(); i > 0; --i) {
    size_t index = i - 1;
    bool sameURI;
    MOZ_ASSERT(mReusableSheets[index]->GetOriginalURI());
    nsresult rv =
        aURL->Equals(mReusableSheets[index]->GetOriginalURI(), &sameURI);
    if (!NS_FAILED(rv) && sameURI) {
      aResult = mReusableSheets[index];
      mReusableSheets.RemoveElementAt(index);
      return true;
    }
  }
  return false;
}
/*************************
 * Loader Implementation *
 *************************/

Loader::Loader()
    : mDocument(nullptr),
      mDocumentCompatMode(eCompatibility_FullStandards),
      mReporter(new ConsoleReportCollector()) {}

Loader::Loader(DocGroup* aDocGroup) : Loader() { mDocGroup = aDocGroup; }

Loader::Loader(Document* aDocument) : Loader() {
  MOZ_ASSERT(aDocument, "We should get a valid document from the caller!");
  mDocument = aDocument;
  mIsDocumentAssociated = true;
  mDocumentCompatMode = aDocument->GetCompatibilityMode();
  mSheets = SharedStyleSheetCache::Get();
  RegisterInSheetCache();
}

// Note: no real need to revoke our stylesheet loaded events -- they hold strong
// references to us, so if we're going away that means they're all done.
Loader::~Loader() = default;

void Loader::RegisterInSheetCache() {
  MOZ_ASSERT(mDocument);
  MOZ_ASSERT(mSheets);

  mSheets->RegisterLoader(*this);
}

void Loader::DeregisterFromSheetCache() {
  MOZ_ASSERT(mDocument);
  MOZ_ASSERT(mSheets);

  mSheets->CancelLoadsForLoader(*this);
  mSheets->UnregisterLoader(*this);
}

void Loader::DropDocumentReference() {
  // Flush out pending datas just so we don't leak by accident.
  if (mSheets) {
    DeregisterFromSheetCache();
  }
  mDocument = nullptr;
}

void Loader::DocumentStyleSheetSetChanged() {
  MOZ_ASSERT(mDocument);

  // start any pending alternates that aren't alternates anymore
  mSheets->StartPendingLoadsForLoader(*this, [&](const SheetLoadData& aData) {
    return IsAlternateSheet(aData.mTitle, true) != IsAlternate::Yes;
  });
}

static const char kCharsetSym[] = "@charset \"";

static bool GetCharsetFromData(const char* aStyleSheetData,
                               uint32_t aDataLength, nsACString& aCharset) {
  aCharset.Truncate();
  if (aDataLength <= sizeof(kCharsetSym) - 1) {
    return false;
  }

  if (strncmp(aStyleSheetData, kCharsetSym, sizeof(kCharsetSym) - 1)) {
    return false;
  }

  for (uint32_t i = sizeof(kCharsetSym) - 1; i < aDataLength; ++i) {
    char c = aStyleSheetData[i];
    if (c == '"') {
      ++i;
      if (i < aDataLength && aStyleSheetData[i] == ';') {
        return true;
      }
      // fail
      break;
    }
    aCharset.Append(c);
  }

  // Did not see end quote or semicolon
  aCharset.Truncate();
  return false;
}

NotNull<const Encoding*> SheetLoadData::DetermineNonBOMEncoding(
    const nsACString& aSegment, nsIChannel* aChannel) const {
  // 1024 bytes is specified in https://drafts.csswg.org/css-syntax/
  constexpr size_t kSniffingBufferSize = 1024;
  nsAutoCString label;
  // Check HTTP
  if (aChannel && NS_SUCCEEDED(aChannel->GetContentCharset(label))) {
    if (const auto* encoding = Encoding::ForLabel(label)) {
      return WrapNotNull(encoding);
    }
  }

  // Check @charset
  auto sniffingLength = std::min(aSegment.Length(), kSniffingBufferSize);
  if (GetCharsetFromData(aSegment.BeginReading(), sniffingLength, label)) {
    if (const auto* encoding = Encoding::ForLabel(label)) {
      if (encoding == UTF_16BE_ENCODING || encoding == UTF_16LE_ENCODING) {
        return UTF_8_ENCODING;
      }
      return WrapNotNull(encoding);
    }
  }
  return mGuessedEncoding;
}

static nsresult VerifySheetIntegrity(const SRIMetadata& aMetadata,
                                     nsIChannel* aChannel,
                                     LoadTainting aTainting,
                                     const nsACString& aFirst,
                                     const nsACString& aSecond,
                                     nsIConsoleReportCollector* aReporter) {
  NS_ENSURE_ARG_POINTER(aReporter);
  MOZ_LOG(SRILogHelper::GetSriLog(), LogLevel::Debug,
          ("VerifySheetIntegrity (unichar stream)"));

  SRICheckDataVerifier verifier(aMetadata, aChannel, aReporter);
  MOZ_TRY(verifier.Update(aFirst));
  MOZ_TRY(verifier.Update(aSecond));
  return verifier.Verify(aMetadata, aChannel, aTainting, aReporter);
}

static bool AllLoadsCanceled(const SheetLoadData& aData) {
  const SheetLoadData* data = &aData;
  do {
    if (!data->IsCancelled()) {
      return false;
    }
  } while ((data = data->mNext));
  return true;
}

void SheetLoadData::OnStartRequest(nsIRequest* aRequest) {
  MOZ_ASSERT(NS_IsMainThread());
  NotifyStart(aRequest);

  SetMinimumExpirationTime(
      nsContentUtils::GetSubresourceCacheExpirationTime(aRequest, mURI));

  // We need to block resolution of parse promise until we receive OnStopRequest
  // on Main thread. This is necessary because parse promise resolution fires
  // OnLoad event must not be dispatched until OnStopRequest in main thread is
  // processed, for stuff like performance resource entries.
  mSheet->BlockParsePromise();

  nsCOMPtr<nsIChannel> channel = do_QueryInterface(aRequest);
  if (!channel) {
    return;
  }

  nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();
  mTainting = loadInfo->GetTainting();

  nsCOMPtr<nsIURI> originalURI;
  channel->GetOriginalURI(getter_AddRefs(originalURI));
  MOZ_DIAGNOSTIC_ASSERT(originalURI,
                        "Someone just violated the nsIRequest contract");
  nsCOMPtr<nsIURI> finalURI;
  NS_GetFinalChannelURI(channel, getter_AddRefs(finalURI));
  MOZ_DIAGNOSTIC_ASSERT(finalURI,
                        "Someone just violated the nsIRequest contract");
  mSheet->SetURIs(finalURI, originalURI, finalURI);

  nsCOMPtr<nsIPrincipal> principal;
  nsIScriptSecurityManager* secMan = nsContentUtils::GetSecurityManager();
  if (mUseSystemPrincipal) {
    secMan->GetSystemPrincipal(getter_AddRefs(principal));
  } else {
    secMan->GetChannelResultPrincipal(channel, getter_AddRefs(principal));
  }
  MOZ_DIAGNOSTIC_ASSERT(principal);
  mSheet->SetPrincipal(principal);

  nsCOMPtr<nsIReferrerInfo> referrerInfo =
      ReferrerInfo::CreateForExternalCSSResources(
          mSheet, nsContentUtils::GetReferrerPolicyFromChannel(channel));
  mSheet->SetReferrerInfo(referrerInfo);

  if (nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(channel)) {
    nsCString sourceMapURL;
    if (nsContentUtils::GetSourceMapURL(httpChannel, sourceMapURL)) {
      mSheet->SetSourceMapURL(std::move(sourceMapURL));
    }
  }

  mIsCrossOriginNoCORS = mSheet->GetCORSMode() == CORS_NONE &&
                         !mTriggeringPrincipal->Subsumes(mSheet->Principal());
}

/*
 * Here we need to check that the load did not give us an http error
 * page and check the mimetype on the channel to make sure we're not
 * loading non-text/css data in standards mode.
 */
nsresult SheetLoadData::VerifySheetReadyToParse(nsresult aStatus,
                                                const nsACString& aBytes1,
                                                const nsACString& aBytes2,
                                                nsIChannel* aChannel) {
  LOG(("SheetLoadData::VerifySheetReadyToParse"));
  NS_ASSERTION((!NS_IsMainThread() || !mLoader->mSyncCallback),
               "Synchronous callback from necko");
  MOZ_DIAGNOSTIC_ASSERT_IF(mRecordErrors, NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(aChannel);

  if (AllLoadsCanceled(*this)) {
    return NS_BINDING_ABORTED;
  }

  if (NS_FAILED(aStatus)) {
    return aStatus;
  }

  // If it's an HTTP channel, we want to make sure this is not an
  // error document we got.
  if (nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aChannel)) {
    bool requestSucceeded;
    nsresult result = httpChannel->GetRequestSucceeded(&requestSucceeded);
    if (NS_SUCCEEDED(result) && !requestSucceeded) {
      return NS_ERROR_NOT_AVAILABLE;
    }
  }

  nsAutoCString contentType;
  aChannel->GetContentType(contentType);

  // In standards mode, a style sheet must have one of these MIME
  // types to be processed at all.  In quirks mode, we accept any
  // MIME type, but only if the style sheet is same-origin with the
  // requesting document or parent sheet.  See bug 524223.

  const bool validType = contentType.EqualsLiteral("text/css") ||
                         contentType.EqualsLiteral(UNKNOWN_CONTENT_TYPE) ||
                         contentType.IsEmpty();

  if (!validType) {
    const bool sameOrigin = mTriggeringPrincipal->Subsumes(mSheet->Principal());
    const auto flag = sameOrigin && mCompatMode == eCompatibility_NavQuirks
                          ? nsIScriptError::warningFlag
                          : nsIScriptError::errorFlag;
    const auto errorMessage = flag == nsIScriptError::errorFlag
                                  ? "MimeNotCss"_ns
                                  : "MimeNotCssWarn"_ns;
    NS_ConvertUTF8toUTF16 sheetUri(mSheet->GetSheetURI()->GetSpecOrDefault());
    NS_ConvertUTF8toUTF16 contentType16(contentType);

    nsAutoCString referrerSpec;
    if (nsCOMPtr<nsIURI> referrer = ReferrerInfo()->GetOriginalReferrer()) {
      referrer->GetSpec(referrerSpec);
    }
    mLoader->mReporter->AddConsoleReport(
        flag, "CSS Loader"_ns, nsContentUtils::eCSS_PROPERTIES, referrerSpec, 0,
        0, errorMessage, {sheetUri, contentType16});
    if (flag == nsIScriptError::errorFlag) {
      LOG_WARN(
          ("  Ignoring sheet with improper MIME type %s", contentType.get()));
      return NS_ERROR_NOT_AVAILABLE;
    }
  }

  SRIMetadata sriMetadata;
  mSheet->GetIntegrity(sriMetadata);
  if (!sriMetadata.IsEmpty()) {
    nsresult rv = VerifySheetIntegrity(sriMetadata, aChannel, mTainting,
                                       aBytes1, aBytes2, mLoader->mReporter);
    if (NS_FAILED(rv)) {
      LOG(("  Load was blocked by SRI"));
      MOZ_LOG(gSriPRLog, LogLevel::Debug,
              ("css::Loader::OnStreamComplete, bad metadata"));
      return NS_ERROR_SRI_CORRUPT;
    }
  }
  return NS_OK_PARSE_SHEET;
}

Loader::IsAlternate Loader::IsAlternateSheet(const nsAString& aTitle,
                                             bool aHasAlternateRel) {
  // A sheet is alternate if it has a nonempty title that doesn't match the
  // currently selected style set.  But if there _is_ no currently selected
  // style set, the sheet wasn't marked as an alternate explicitly, and aTitle
  // is nonempty, we should select the style set corresponding to aTitle, since
  // that's a preferred sheet.
  if (aTitle.IsEmpty()) {
    return IsAlternate::No;
  }

  if (mDocument) {
    const nsString& currentSheetSet = mDocument->GetCurrentStyleSheetSet();
    if (!aHasAlternateRel && currentSheetSet.IsEmpty()) {
      // There's no preferred set yet, and we now have a sheet with a title.
      // Make that be the preferred set.
      // FIXME(emilio): This is kinda wild, can we do it somewhere else?
      mDocument->SetPreferredStyleSheetSet(aTitle);
      // We're definitely not an alternate. Also, beware that at this point
      // currentSheetSet may dangle.
      return IsAlternate::No;
    }

    if (aTitle.Equals(currentSheetSet)) {
      return IsAlternate::No;
    }
  }

  return IsAlternate::Yes;
}

static nsContentPolicyType ComputeContentPolicyType(
    StylePreloadKind aPreloadKind) {
  return aPreloadKind == StylePreloadKind::None
             ? nsIContentPolicy::TYPE_INTERNAL_STYLESHEET
             : nsIContentPolicy::TYPE_INTERNAL_STYLESHEET_PRELOAD;
}

nsresult Loader::CheckContentPolicy(nsIPrincipal* aLoadingPrincipal,
                                    nsIPrincipal* aTriggeringPrincipal,
                                    nsIURI* aTargetURI,
                                    nsINode* aRequestingNode,
                                    const nsAString& aNonce,
                                    StylePreloadKind aPreloadKind) {
  // When performing a system load don't consult content policies.
  if (!mDocument) {
    return NS_OK;
  }

  nsContentPolicyType contentPolicyType =
      ComputeContentPolicyType(aPreloadKind);

  nsCOMPtr<nsILoadInfo> secCheckLoadInfo = new net::LoadInfo(
      aLoadingPrincipal, aTriggeringPrincipal, aRequestingNode,
      nsILoadInfo::SEC_ONLY_FOR_EXPLICIT_CONTENTSEC_CHECK, contentPolicyType);
  secCheckLoadInfo->SetCspNonce(aNonce);

  int16_t shouldLoad = nsIContentPolicy::ACCEPT;
  nsresult rv =
      NS_CheckContentLoadPolicy(aTargetURI, secCheckLoadInfo, &shouldLoad,
                                nsContentUtils::GetContentPolicy());
  if (NS_FAILED(rv) || NS_CP_REJECTED(shouldLoad)) {
    // Asynchronously notify observers (e.g devtools) of CSP failure.
    nsContentUtils::AddScriptRunner(NS_NewRunnableFunction(
        "Loader::NotifyOnFailedCheckPolicy",
        [targetURI = RefPtr<nsIURI>(aTargetURI),
         requestingNode = RefPtr<nsINode>(aRequestingNode),
         contentPolicyType]() {
          nsCOMPtr<nsIChannel> channel;
          NS_NewChannel(
              getter_AddRefs(channel), targetURI, requestingNode,
              nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_INHERITS_SEC_CONTEXT,
              contentPolicyType);
          NS_SetRequestBlockingReason(
              channel, nsILoadInfo::BLOCKING_REASON_CONTENT_POLICY_GENERAL);
          nsCOMPtr<nsIObserverService> obsService =
              services::GetObserverService();
          if (obsService) {
            obsService->NotifyObservers(
                channel, "http-on-failed-opening-request", nullptr);
          }
        }));
    return NS_ERROR_CONTENT_BLOCKED;
  }
  return NS_OK;
}

static void RecordUseCountersIfNeeded(Document* aDoc,
                                      const StyleSheet& aSheet) {
  if (!aDoc) {
    return;
  }
  const StyleUseCounters* docCounters = aDoc->GetStyleUseCounters();
  if (!docCounters) {
    return;
  }
  if (aSheet.URLData()->ChromeRulesEnabled()) {
    return;
  }
  const auto* sheetCounters = aSheet.GetStyleUseCounters();
  if (!sheetCounters) {
    return;
  }
  Servo_UseCounters_Merge(docCounters, sheetCounters);
}

bool Loader::MaybePutIntoLoadsPerformed(SheetLoadData& aLoadData) {
  if (!aLoadData.mURI) {
    // Inline style sheet is not tracked.
    return false;
  }

  return mLoadsPerformed.EnsureInserted(SheetLoadDataHashKey(aLoadData));
}

/**
 * CreateSheet() creates a StyleSheet object for the given URI.
 *
 * We check for an existing style sheet object for that uri in various caches
 * and clone it if we find it.  Cloned sheets will have the title/media/enabled
 * state of the sheet they are clones off; make sure to call PrepareSheet() on
 * the result of CreateSheet().
 */
std::tuple<RefPtr<StyleSheet>, Loader::SheetState,
           RefPtr<SubResourceNetworkMetadataHolder>>
Loader::CreateSheet(nsIURI* aURI, nsIContent* aLinkingContent,
                    nsIPrincipal* aTriggeringPrincipal,
                    css::SheetParsingMode aParsingMode, CORSMode aCORSMode,
                    const Encoding* aPreloadOrParentDataEncoding,
                    const nsAString& aIntegrity, bool aSyncLoad,
                    StylePreloadKind aPreloadKind) {
  MOZ_ASSERT(aURI, "This path is not taken for inline stylesheets");
  LOG(("css::Loader::CreateSheet(%s)", aURI->GetSpecOrDefault().get()));

  SRIMetadata sriMetadata;
  if (!aIntegrity.IsEmpty()) {
    MOZ_LOG(gSriPRLog, LogLevel::Debug,
            ("css::Loader::CreateSheet, integrity=%s",
             NS_ConvertUTF16toUTF8(aIntegrity).get()));
    nsAutoCString sourceUri;
    if (mDocument && mDocument->GetDocumentURI()) {
      mDocument->GetDocumentURI()->GetAsciiSpec(sourceUri);
    }
    SRICheck::IntegrityMetadata(aIntegrity, sourceUri, mReporter, &sriMetadata);
  }

  if (mSheets) {
    SheetLoadDataHashKey key(aURI, LoaderPrincipal(), PartitionedPrincipal(),
                             GetFallbackEncoding(*this, aLinkingContent,
                                                 aPreloadOrParentDataEncoding),
                             aCORSMode, aParsingMode, CompatMode(aPreloadKind),
                             sriMetadata, aPreloadKind);
    auto cacheResult = mSheets->Lookup(*this, key, aSyncLoad);
    if (cacheResult.mState != CachedSubResourceState::Miss) {
      SheetState sheetState = SheetState::Complete;
      RefPtr<StyleSheet> sheet;
      RefPtr<SubResourceNetworkMetadataHolder> networkMetadata;
      if (cacheResult.mCompleteValue) {
        sheet = cacheResult.mCompleteValue->Clone(nullptr, nullptr);
        networkMetadata = cacheResult.mNetworkMetadata;
        mDocument->SetDidHitCompleteSheetCache();
      } else {
        MOZ_ASSERT(cacheResult.mLoadingOrPendingValue);
        sheet = cacheResult.mLoadingOrPendingValue->ValueForCache();
        sheetState = cacheResult.mState == CachedSubResourceState::Loading
                         ? SheetState::Loading
                         : SheetState::Pending;
      }
      LOG(("  Hit cache with state: %s", gStateStrings[size_t(sheetState)]));
      return {std::move(sheet), sheetState, std::move(networkMetadata)};
    }
  }

  nsIURI* sheetURI = aURI;
  nsIURI* baseURI = aURI;
  nsIURI* originalURI = aURI;

  auto sheet = MakeRefPtr<StyleSheet>(aParsingMode, aCORSMode, sriMetadata);
  sheet->SetURIs(sheetURI, originalURI, baseURI);
  nsCOMPtr<nsIReferrerInfo> referrerInfo =
      ReferrerInfo::CreateForExternalCSSResources(sheet);
  sheet->SetReferrerInfo(referrerInfo);
  LOG(("  Needs parser"));
  return {std::move(sheet), SheetState::NeedsParser, nullptr};
}

static Loader::MediaMatched MediaListMatches(const MediaList* aMediaList,
                                             const Document* aDocument) {
  if (!aMediaList || !aDocument) {
    return Loader::MediaMatched::Yes;
  }

  if (aMediaList->Matches(*aDocument)) {
    return Loader::MediaMatched::Yes;
  }

  return Loader::MediaMatched::No;
}

/**
 * PrepareSheet() handles setting the media and title on the sheet, as
 * well as setting the enabled state based on the title and whether
 * the sheet had "alternate" in its rel.
 */
Loader::MediaMatched Loader::PrepareSheet(
    StyleSheet& aSheet, const nsAString& aTitle, const nsAString& aMediaString,
    MediaList* aMediaList, IsAlternate aIsAlternate,
    IsExplicitlyEnabled aIsExplicitlyEnabled) {
  RefPtr<MediaList> mediaList(aMediaList);

  if (!aMediaString.IsEmpty()) {
    NS_ASSERTION(!aMediaList,
                 "must not provide both aMediaString and aMediaList");
    mediaList = MediaList::Create(NS_ConvertUTF16toUTF8(aMediaString));
  }

  aSheet.SetMedia(do_AddRef(mediaList));

  aSheet.SetTitle(aTitle);
  aSheet.SetEnabled(aIsAlternate == IsAlternate::No ||
                    aIsExplicitlyEnabled == IsExplicitlyEnabled::Yes);
  return MediaListMatches(mediaList, mDocument);
}

/**
 * InsertSheetInTree handles ordering of sheets in the document or shadow root.
 *
 * Here we have two types of sheets -- those with linking elements and
 * those without.  The latter are loaded by Link: headers, and are only added to
 * the document.
 *
 * The following constraints are observed:
 * 1) Any sheet with a linking element comes after all sheets without
 *    linking elements
 * 2) Sheets without linking elements are inserted in the order in
 *    which the inserting requests come in, since all of these are
 *    inserted during header data processing in the content sink
 * 3) Sheets with linking elements are ordered based on document order
 *    as determined by CompareDocumentPosition.
 */
void Loader::InsertSheetInTree(StyleSheet& aSheet) {
  LOG(("css::Loader::InsertSheetInTree"));
  MOZ_ASSERT(mDocument, "Must have a document to insert into");

  // If our owning node is null, we come from a link header.
  nsINode* owningNode = aSheet.GetOwnerNode();
  MOZ_ASSERT_IF(owningNode, owningNode->OwnerDoc() == mDocument);
  DocumentOrShadowRoot* target =
      owningNode ? owningNode->GetContainingDocumentOrShadowRoot() : mDocument;
  MOZ_ASSERT(target, "Where should we insert it?");

  size_t insertionPoint = target->FindSheetInsertionPointInTree(aSheet);
  if (auto* shadow = ShadowRoot::FromNode(target->AsNode())) {
    shadow->InsertSheetAt(insertionPoint, aSheet);
  } else {
    MOZ_ASSERT(&target->AsNode() == mDocument);
    mDocument->InsertSheetAt(insertionPoint, aSheet);
  }

  LOG(("  Inserting into target (doc: %d) at position %zu",
       target->AsNode().IsDocument(), insertionPoint));
}

/**
 * InsertChildSheet handles ordering of @import-ed sheet in their
 * parent sheets.  Here we want to just insert based on order of the
 * @import rules that imported the sheets.  In theory we can't just
 * append to the end because the CSSOM can insert @import rules.  In
 * practice, we get the call to load the child sheet before the CSSOM
 * has finished inserting the @import rule, so we have no idea where
 * to put it anyway.  So just append for now.  (In the future if we
 * want to insert the sheet at the correct position, we'll need to
 * restore CSSStyleSheet::InsertStyleSheetAt, which was removed in
 * bug 1220506.)
 */
void Loader::InsertChildSheet(StyleSheet& aSheet, StyleSheet& aParentSheet) {
  LOG(("css::Loader::InsertChildSheet"));

  // child sheets should always start out enabled, even if they got
  // cloned off of top-level sheets which were disabled
  aSheet.SetEnabled(true);
  aParentSheet.AppendStyleSheet(aSheet);

  LOG(("  Inserting into parent sheet"));
}

nsresult Loader::NewStyleSheetChannel(SheetLoadData& aLoadData,
                                      CORSMode aCorsMode,
                                      UsePreload aUsePreload,
                                      UseLoadGroup aUseLoadGroup,
                                      nsIChannel** aOutChannel) {
  nsCOMPtr<nsILoadGroup> loadGroup;
  nsCOMPtr<nsICookieJarSettings> cookieJarSettings;
  if (aUseLoadGroup == UseLoadGroup::Yes && mDocument) {
    loadGroup = mDocument->GetDocumentLoadGroup();
    // load for a document with no loadgrup indicates that something is
    // completely bogus, let's bail out early.
    if (!loadGroup) {
      LOG_ERROR(("  Failed to query loadGroup from document"));
      return NS_ERROR_UNEXPECTED;
    }

    cookieJarSettings = mDocument->CookieJarSettings();
  }

  nsSecurityFlags securityFlags =
      nsContentSecurityManager::ComputeSecurityFlags(
          aCorsMode, nsContentSecurityManager::CORSSecurityMapping::
                         CORS_NONE_MAPS_TO_INHERITED_CONTEXT);
  securityFlags |= nsILoadInfo::SEC_ALLOW_CHROME;

  nsContentPolicyType contentPolicyType =
      ComputeContentPolicyType(aLoadData.mPreloadKind);

  nsINode* requestingNode = aLoadData.GetRequestingNode();

  nsIPrincipal* triggeringPrincipal = aLoadData.mTriggeringPrincipal;

  // Note that we are calling NS_NewChannelWithTriggeringPrincipal() with both
  // a node and a principal.
  // This is because of a case where the node is the document being styled and
  // the principal is the stylesheet (perhaps from a different origin) that is
  // applying the styles.
  if (requestingNode) {
    return NS_NewChannelWithTriggeringPrincipal(
        aOutChannel, aLoadData.mURI, requestingNode, triggeringPrincipal,
        securityFlags, contentPolicyType,
        /* aPerformanceStorage = */ nullptr, loadGroup);
  }

  MOZ_ASSERT(triggeringPrincipal->Equals(LoaderPrincipal()));

  if (aUsePreload == UsePreload::Yes) {
    auto result = URLPreloader::ReadURI(aLoadData.mURI);
    if (result.isOk()) {
      nsCOMPtr<nsIInputStream> stream;
      MOZ_TRY(
          NS_NewCStringInputStream(getter_AddRefs(stream), result.unwrap()));

      return NS_NewInputStreamChannel(aOutChannel, aLoadData.mURI,
                                      stream.forget(), triggeringPrincipal,
                                      securityFlags, contentPolicyType);
    }
  }

  return NS_NewChannel(aOutChannel, aLoadData.mURI, triggeringPrincipal,
                       securityFlags, contentPolicyType, cookieJarSettings,
                       /* aPerformanceStorage = */ nullptr, loadGroup);
}

nsresult Loader::LoadSheetSyncInternal(SheetLoadData& aLoadData,
                                       SheetState aSheetState) {
  LOG(("  Synchronous load"));
  MOZ_ASSERT(!aLoadData.mObserver, "Observer for a sync load?");
  MOZ_ASSERT(aSheetState == SheetState::NeedsParser,
             "Sync loads can't reuse existing async loads");

  // Create a StreamLoader instance to which we will feed
  // the data from the sync load.  Do this before creating the
  // channel to make error recovery simpler.
  auto streamLoader = MakeRefPtr<StreamLoader>(aLoadData);

  if (mDocument) {
    net::PredictorLearn(aLoadData.mURI, mDocument->GetDocumentURI(),
                        nsINetworkPredictor::LEARN_LOAD_SUBRESOURCE, mDocument);
  }

  // Synchronous loads should only be used internally. Therefore no CORS
  // policy is needed.
  nsCOMPtr<nsIChannel> channel;
  nsresult rv =
      NewStyleSheetChannel(aLoadData, CORSMode::CORS_NONE, UsePreload::Yes,
                           UseLoadGroup::No, getter_AddRefs(channel));
  if (NS_FAILED(rv)) {
    LOG_ERROR(("  Failed to create channel"));
    streamLoader->ChannelOpenFailed(rv);
    SheetComplete(aLoadData, rv);
    return rv;
  }

  nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();
  loadInfo->SetCspNonce(aLoadData.Nonce());

#ifdef DEBUG
  {
    nsCOMPtr<nsIInterfaceRequestor> prevCallback;
    channel->GetNotificationCallbacks(getter_AddRefs(prevCallback));
    MOZ_ASSERT(!prevCallback);
  }
#endif
  channel->SetNotificationCallbacks(streamLoader);

  nsCOMPtr<nsIInputStream> stream;
  rv = channel->Open(getter_AddRefs(stream));

  if (NS_FAILED(rv)) {
    LOG_ERROR(("  Failed to open URI synchronously"));
    streamLoader->ChannelOpenFailed(rv);
    channel->SetNotificationCallbacks(nullptr);
    SheetComplete(aLoadData, rv);
    return rv;
  }

  // Force UA sheets to be UTF-8.
  // XXX this is only necessary because the default in
  // SheetLoadData::OnDetermineCharset is wrong (bug 521039).
  channel->SetContentCharset("UTF-8"_ns);

  // Manually feed the streamloader the contents of the stream.
  // This will call back into OnStreamComplete
  // and thence to ParseSheet.  Regardless of whether this fails,
  // SheetComplete has been called.
  return nsSyncLoadService::PushSyncStreamToListener(stream.forget(),
                                                     streamLoader, channel);
}

bool Loader::MaybeDeferLoad(SheetLoadData& aLoadData, SheetState aSheetState,
                            PendingLoad aPendingLoad,
                            const SheetLoadDataHashKey& aKey) {
  MOZ_ASSERT(mSheets);

  // If we have at least one other load ongoing, then we can defer it until
  // all non-pending loads are done.
  if (aSheetState == SheetState::NeedsParser &&
      aPendingLoad == PendingLoad::No && aLoadData.ShouldDefer() &&
      mOngoingLoadCount > mPendingLoadCount + 1) {
    LOG(("  Deferring sheet load"));
    ++mPendingLoadCount;
    mSheets->DeferLoad(aKey, aLoadData);
    return true;
  }
  return false;
}

bool Loader::MaybeCoalesceLoadAndNotifyOpen(SheetLoadData& aLoadData,
                                            SheetState aSheetState,
                                            const SheetLoadDataHashKey& aKey,
                                            const PreloadHashKey& aPreloadKey) {
  bool coalescedLoad = false;
  auto cacheState = [&aSheetState] {
    switch (aSheetState) {
      case SheetState::Complete:
        return CachedSubResourceState::Complete;
      case SheetState::Pending:
        return CachedSubResourceState::Pending;
      case SheetState::Loading:
        return CachedSubResourceState::Loading;
      case SheetState::NeedsParser:
        return CachedSubResourceState::Miss;
    }
    MOZ_ASSERT_UNREACHABLE("wat");
    return CachedSubResourceState::Miss;
  }();

  if ((coalescedLoad = mSheets->CoalesceLoad(aKey, aLoadData, cacheState))) {
    if (aSheetState == SheetState::Pending) {
      ++mPendingLoadCount;
    } else {
      // TODO: why not just `IsPreload()`?
      aLoadData.NotifyOpen(aPreloadKey, mDocument,
                           aLoadData.IsLinkRelPreloadOrEarlyHint());
    }
  }
  return coalescedLoad;
}

/**
 * LoadSheet handles the actual load of a sheet.  If the load is
 * supposed to be synchronous it just opens a channel synchronously
 * using the given uri, wraps the resulting stream in a converter
 * stream and calls ParseSheet.  Otherwise it tries to look for an
 * existing load for this URI and piggyback on it.  Failing all that,
 * a new load is kicked off asynchronously.
 */
nsresult Loader::LoadSheet(SheetLoadData& aLoadData, SheetState aSheetState,
                           uint64_t aEarlyHintPreloaderId,
                           PendingLoad aPendingLoad) {
  LOG(("css::Loader::LoadSheet"));
  MOZ_ASSERT(aLoadData.mURI, "Need a URI to load");
  MOZ_ASSERT(aLoadData.mSheet, "Need a sheet to load into");
  MOZ_ASSERT(aSheetState != SheetState::Complete, "Why bother?");
  MOZ_ASSERT(!aLoadData.mUseSystemPrincipal || aLoadData.mSyncLoad,
             "Shouldn't use system principal for async loads");

  LOG_URI("  Load from: '%s'", aLoadData.mURI);

  // If we're firing a pending load, this load is already accounted for the
  // first time it went through this function.
  if (aPendingLoad == PendingLoad::No) {
    if (aLoadData.BlocksLoadEvent()) {
      IncrementOngoingLoadCountAndMaybeBlockOnload();
    }

    // We technically never defer non-top-level sheets, so this condition could
    // be outside the branch, but conceptually it should be here.
    if (aLoadData.mParentData) {
      ++aLoadData.mParentData->mPendingChildren;
    }
  }

  if (!mDocument && !aLoadData.mIsNonDocumentSheet) {
    // No point starting the load; just release all the data and such.
    LOG_WARN(("  No document and not non-document sheet; pre-dropping load"));
    SheetComplete(aLoadData, NS_BINDING_ABORTED);
    return NS_BINDING_ABORTED;
  }

  if (aLoadData.mSyncLoad) {
    return LoadSheetSyncInternal(aLoadData, aSheetState);
  }

  SheetLoadDataHashKey key(aLoadData);

  auto preloadKey = PreloadHashKey::CreateAsStyle(aLoadData);
  if (mSheets) {
    if (MaybeDeferLoad(aLoadData, aSheetState, aPendingLoad, key)) {
      return NS_OK;
    }

    if (MaybeCoalesceLoadAndNotifyOpen(aLoadData, aSheetState, key,
                                       preloadKey)) {
      // All done here; once the load completes we'll be marked complete
      // automatically.
      return NS_OK;
    }
  }

  aLoadData.NotifyOpen(preloadKey, mDocument,
                       aLoadData.IsLinkRelPreloadOrEarlyHint());

  return LoadSheetAsyncInternal(aLoadData, aEarlyHintPreloaderId, key);
}

void Loader::AdjustPriority(const SheetLoadData& aLoadData,
                            nsIChannel* aChannel) {
  if (!aLoadData.ShouldDefer() && aLoadData.IsLinkRelPreloadOrEarlyHint()) {
    SheetLoadData::PrioritizeAsPreload(aChannel);
  }

  if (!StaticPrefs::network_fetchpriority_enabled()) {
    return;
  }

  nsCOMPtr<nsISupportsPriority> sp = do_QueryInterface(aChannel);

  if (!sp) {
    return;
  }

  // Adjusting priorites is specified as implementation-defined.
  // See corresponding preferences in StaticPrefList.yaml for more context.
  const int32_t supportsPriorityDelta = [&]() {
    if (aLoadData.ShouldDefer()) {
      return FETCH_PRIORITY_ADJUSTMENT_FOR(deferred_style,
                                           aLoadData.mFetchPriority);
    }
    if (aLoadData.IsLinkRelPreloadOrEarlyHint()) {
      return FETCH_PRIORITY_ADJUSTMENT_FOR(link_preload_style,
                                           aLoadData.mFetchPriority);
    }
    return FETCH_PRIORITY_ADJUSTMENT_FOR(non_deferred_style,
                                         aLoadData.mFetchPriority);
  }();

  sp->AdjustPriority(supportsPriorityDelta);
#ifdef DEBUG
  int32_t adjustedPriority;
  sp->GetPriority(&adjustedPriority);
  LogPriorityMapping(sCssLoaderLog, aLoadData.mFetchPriority, adjustedPriority);
#endif

  if (nsCOMPtr<nsIClassOfService> cos = do_QueryInterface(aChannel)) {
    cos->SetFetchPriorityDOM(aLoadData.mFetchPriority);
  }
}

nsresult Loader::LoadSheetAsyncInternal(SheetLoadData& aLoadData,
                                        uint64_t aEarlyHintPreloaderId,
                                        const SheetLoadDataHashKey& aKey) {
  SRIMetadata sriMetadata;
  aLoadData.mSheet->GetIntegrity(sriMetadata);

#ifdef DEBUG
  AutoRestore<bool> syncCallbackGuard(mSyncCallback);
  mSyncCallback = true;
#endif

  nsCOMPtr<nsIChannel> channel;
  nsresult rv = NewStyleSheetChannel(aLoadData, aLoadData.mSheet->GetCORSMode(),
                                     UsePreload::No, UseLoadGroup::Yes,
                                     getter_AddRefs(channel));
  if (NS_FAILED(rv)) {
    LOG_ERROR(("  Failed to create channel"));
    SheetComplete(aLoadData, rv);
    return rv;
  }

  nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();
  loadInfo->SetCspNonce(aLoadData.Nonce());

  if (!aLoadData.ShouldDefer()) {
    if (nsCOMPtr<nsIClassOfService> cos = do_QueryInterface(channel)) {
      cos->AddClassFlags(nsIClassOfService::Leader);
    }

    if (!aLoadData.BlocksLoadEvent()) {
      SheetLoadData::AddLoadBackgroundFlag(channel);
    }
  }

  AdjustPriority(aLoadData, channel);

  if (nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(channel)) {
    if (nsCOMPtr<nsIReferrerInfo> referrerInfo = aLoadData.ReferrerInfo()) {
      rv = httpChannel->SetReferrerInfo(referrerInfo);
      Unused << NS_WARN_IF(NS_FAILED(rv));
    }

    nsCOMPtr<nsIHttpChannelInternal> internalChannel =
        do_QueryInterface(httpChannel);
    if (internalChannel) {
      rv = internalChannel->SetIntegrityMetadata(
          sriMetadata.GetIntegrityString());
      NS_ENSURE_SUCCESS(rv, rv);
    }

    // Set the initiator type
    if (nsCOMPtr<nsITimedChannel> timedChannel =
            do_QueryInterface(httpChannel)) {
      timedChannel->SetInitiatorType(aLoadData.InitiatorTypeString());

      if (aLoadData.mParentData) {
        // This is a child sheet load.
        //
        // The resource timing of the sub-resources that a document loads
        // should normally be reported to the document.  One exception is any
        // sub-resources of any cross-origin resources that are loaded.  We
        // don't mind reporting timing data for a direct child cross-origin
        // resource since the resource that linked to it (and hence potentially
        // anything in that parent origin) is aware that the cross-origin
        // resources is to be loaded.  However, we do not want to report
        // timings for any sub-resources that a cross-origin resource may load
        // since that obviously leaks information about what the cross-origin
        // resource loads, which is bad.
        //
        // In addition to checking whether we're an immediate child resource of
        // a cross-origin resource (by checking if mIsCrossOriginNoCORS is set
        // to true on our parent), we also check our parent to see whether it
        // itself is a sub-resource of a cross-origin resource by checking
        // mBlockResourceTiming.  If that is set then we too are such a
        // sub-resource and so we set the flag on ourself too to propagate it
        // on down.
        if (aLoadData.mParentData->mIsCrossOriginNoCORS ||
            aLoadData.mParentData->mBlockResourceTiming) {
          // Set a flag so any other stylesheet triggered by this one will
          // not be reported
          aLoadData.mBlockResourceTiming = true;

          // Mark the channel so PerformanceMainThread::AddEntry will not
          // report the resource.
          timedChannel->SetReportResourceTiming(false);
        }
      }
    }
  }

  // Now tell the channel we expect text/css data back....  We do
  // this before opening it, so it's only treated as a hint.
  channel->SetContentType("text/css"_ns);

  // We don't have to hold on to the stream loader.  The ownership
  // model is: Necko owns the stream loader, which owns the load data,
  // which owns us
  auto streamLoader = MakeRefPtr<StreamLoader>(aLoadData);
  if (mDocument) {
    net::PredictorLearn(aLoadData.mURI, mDocument->GetDocumentURI(),
                        nsINetworkPredictor::LEARN_LOAD_SUBRESOURCE, mDocument);
  }

#ifdef DEBUG
  {
    nsCOMPtr<nsIInterfaceRequestor> prevCallback;
    channel->GetNotificationCallbacks(getter_AddRefs(prevCallback));
    MOZ_ASSERT(!prevCallback);
  }
#endif
  channel->SetNotificationCallbacks(streamLoader);

  if (aEarlyHintPreloaderId) {
    nsCOMPtr<nsIHttpChannelInternal> channelInternal =
        do_QueryInterface(channel);
    NS_ENSURE_TRUE(channelInternal != nullptr, NS_ERROR_FAILURE);

    rv = channelInternal->SetEarlyHintPreloaderId(aEarlyHintPreloaderId);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  rv = channel->AsyncOpen(streamLoader);
  if (NS_FAILED(rv)) {
    LOG_ERROR(("  Failed to create stream loader"));
    streamLoader->ChannelOpenFailed(rv);
    channel->SetNotificationCallbacks(nullptr);
    // NOTE: NotifyStop will be done in SheetComplete -> NotifyObservers.
    aLoadData.NotifyStart(channel);
    SheetComplete(aLoadData, rv);
    return rv;
  }

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  if (nsCOMPtr<nsIHttpChannelInternal> hci = do_QueryInterface(channel)) {
    hci->DoDiagnosticAssertWhenOnStopNotCalledOnDestroy();
  }
#endif

  if (mSheets) {
    mSheets->LoadStarted(aKey, aLoadData);
  }
  return NS_OK;
}

/**
 * ParseSheet handles parsing the data stream.
 */
Loader::Completed Loader::ParseSheet(
    const nsACString& aBytes, const RefPtr<SheetLoadDataHolder>& aLoadData,
    AllowAsyncParse aAllowAsync) {
  LOG(("css::Loader::ParseSheet"));
  SheetLoadData* loadData = aLoadData->get();
  MOZ_ASSERT(loadData);

  if (loadData->mURI) {
    LOG_URI("  Load succeeded for URI: '%s', parsing", loadData->mURI);
  }
  AUTO_PROFILER_LABEL_CATEGORY_PAIR_RELEVANT_FOR_JS(LAYOUT_CSSParsing);

  ++mParsedSheetCount;

  loadData->mIsBeingParsed = true;

  StyleSheet* sheet = loadData->mSheet;
  MOZ_ASSERT(sheet);

  // Some cases, like inline style and UA stylesheets, need to be parsed
  // synchronously. The former may trigger child loads, the latter must not.
  if (loadData->mSyncLoad || aAllowAsync == AllowAsyncParse::No) {
    sheet->ParseSheetSync(this, aBytes, loadData);
    loadData->mIsBeingParsed = false;

    bool noPendingChildren = loadData->mPendingChildren == 0;
    MOZ_ASSERT_IF(loadData->mSyncLoad, noPendingChildren);
    if (noPendingChildren) {
      SheetComplete(*loadData, NS_OK);
      return Completed::Yes;
    }
    return Completed::No;
  }

  // This parse does not need to be synchronous. \o/
  //
  // Note that load is already blocked from
  // IncrementOngoingLoadCountAndMaybeBlockOnload(), and will be unblocked from
  // SheetFinishedParsingAsync which will end up in NotifyObservers as needed.
  sheet->ParseSheet(*this, aBytes, aLoadData)
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [loadData = aLoadData](bool aDummy) {
            MOZ_ASSERT(NS_IsMainThread());
            loadData->get()->SheetFinishedParsingAsync();
          },
          [] { MOZ_CRASH("rejected parse promise"); });
  return Completed::No;
}

void Loader::AddPerformanceEntryForCachedSheet(SheetLoadData& aLoadData) {
  MOZ_ASSERT(aLoadData.mURI);

  if (!aLoadData.mNetworkMetadata) {
    return;
  }
  if (!mDocument) {
    return;
  }

  nsAutoCString name;
  aLoadData.mURI->GetSpec(name);
  NS_ConvertUTF8toUTF16 entryName(name);

  auto end = TimeStamp::Now();
  auto start = aLoadData.mLoadStart;
  if (start.IsNull()) {
    start = end;
  }

  SharedSubResourceCacheUtils::AddPerformanceEntryForCache(
      entryName, aLoadData.InitiatorTypeString(), aLoadData.mNetworkMetadata,
      start, end, mDocument);
}

void Loader::NotifyObservers(SheetLoadData& aData, nsresult aStatus) {
  RecordUseCountersIfNeeded(mDocument, *aData.mSheet);
  if (MaybePutIntoLoadsPerformed(aData) &&
      aData.mShouldEmulateNotificationsForCachedLoad) {
    NotifyObserversForCachedSheet(aData);
    AddPerformanceEntryForCachedSheet(aData);
  }

  RefPtr loadDispatcher = aData.PrepareLoadEventIfNeeded();
  if (aData.mURI) {
    aData.NotifyStop(aStatus);
    // NOTE(emilio): This needs to happen before notifying observers, as
    // FontFaceSet for example checks for pending sheet loads from the
    // StyleSheetLoaded callback.
    if (aData.BlocksLoadEvent()) {
      DecrementOngoingLoadCountAndMaybeUnblockOnload();
      if (mPendingLoadCount && mPendingLoadCount == mOngoingLoadCount) {
        LOG(("  No more loading sheets; starting deferred loads"));
        StartDeferredLoads();
      }
    }
  }
  if (!aData.mTitle.IsEmpty() && NS_SUCCEEDED(aStatus)) {
    nsContentUtils::AddScriptRunner(NS_NewRunnableFunction(
        "Loader::NotifyObservers - Create PageStyle actor",
        [doc = RefPtr{mDocument}] {
          // Force creating the page style actor, if available.
          // This will no-op if no actor with this name is registered (outside
          // of desktop Firefox).
          nsCOMPtr<nsISupports> pageStyleActor =
              do_QueryActor("PageStyle", doc);
          Unused << pageStyleActor;
        }));
  }
  if (aData.mMustNotify) {
    if (nsCOMPtr<nsICSSLoaderObserver> observer = std::move(aData.mObserver)) {
      LOG(("  Notifying observer %p for data %p.  deferred: %d", observer.get(),
           &aData, aData.ShouldDefer()));
      observer->StyleSheetLoaded(aData.mSheet, aData.ShouldDefer(), aStatus);
    }

    for (nsCOMPtr<nsICSSLoaderObserver> obs : mObservers.ForwardRange()) {
      LOG(("  Notifying global observer %p for data %p.  deferred: %d",
           obs.get(), &aData, aData.ShouldDefer()));
      obs->StyleSheetLoaded(aData.mSheet, aData.ShouldDefer(), aStatus);
    }

    if (loadDispatcher) {
      loadDispatcher->RunDOMEventWhenSafe();
    }
  } else if (loadDispatcher) {
    loadDispatcher->PostDOMEvent();
  }
}

/**
 * SheetComplete is the do-it-all cleanup function.  It removes the
 * load data from the "loading" hashtable, adds the sheet to the
 * "completed" hashtable, massages the XUL cache, handles siblings of
 * the load data (other loads for the same URI), handles unblocking
 * blocked parent loads as needed, and most importantly calls
 * NS_RELEASE on the load data to destroy the whole mess.
 */
void Loader::SheetComplete(SheetLoadData& aLoadData, nsresult aStatus) {
  LOG(("css::Loader::SheetComplete, status: 0x%" PRIx32,
       static_cast<uint32_t>(aStatus)));
  if (aLoadData.mURI) {
    mReporter->FlushConsoleReports(mDocument);
  }
  SharedStyleSheetCache::LoadCompleted(mSheets.get(), aLoadData, aStatus);
}

// static
void Loader::MarkLoadTreeFailed(SheetLoadData& aLoadData,
                                Loader* aOnlyForLoader) {
  if (aLoadData.mURI) {
    LOG_URI("  Load failed: '%s'", aLoadData.mURI);
  }

  SheetLoadData* data = &aLoadData;
  do {
    if (!aOnlyForLoader || aOnlyForLoader == data->mLoader) {
      data->mLoadFailed = true;
      data->mSheet->MaybeRejectReplacePromise();
    }

    if (data->mParentData) {
      MarkLoadTreeFailed(*data->mParentData, aOnlyForLoader);
    }

    data = data->mNext;
  } while (data);
}

RefPtr<StyleSheet> Loader::LookupInlineSheetInCache(
    const nsAString& aBuffer, nsIPrincipal* aSheetPrincipal) {
  MOZ_ASSERT(mSheets, "Document associated loader should have sheet cache");
  auto result = mSheets->LookupInline(LoaderPrincipal(), aBuffer);
  if (!result) {
    return nullptr;
  }

  StyleSheet* sheet = result.Data();
  MOZ_ASSERT(!sheet->HasModifiedRules(),
             "How did we end up with a dirty sheet?");

  if (NS_WARN_IF(!sheet->Principal()->Equals(aSheetPrincipal))) {
    // If the sheet is going to have different access rights, don't return it
    // from the cache. XXX can this happen now that we eagerly clone?
    return nullptr;
  }
  return sheet->Clone(nullptr, nullptr);
}

void Loader::MaybeNotifyPreloadUsed(SheetLoadData& aData) {
  if (!mDocument) {
    return;
  }

  auto key = PreloadHashKey::CreateAsStyle(aData);
  RefPtr<PreloaderBase> preload = mDocument->Preloads().LookupPreload(key);
  if (!preload) {
    return;
  }

  preload->NotifyUsage(mDocument);
}

Result<Loader::LoadSheetResult, nsresult> Loader::LoadInlineStyle(
    const SheetInfo& aInfo, const nsAString& aBuffer,
    nsICSSLoaderObserver* aObserver) {
  LOG(("css::Loader::LoadInlineStyle"));
  MOZ_ASSERT(aInfo.mContent);

  if (!mEnabled) {
    LOG_WARN(("  Not enabled"));
    return Err(NS_ERROR_NOT_AVAILABLE);
  }

  if (!mDocument) {
    return Err(NS_ERROR_NOT_INITIALIZED);
  }

  MOZ_ASSERT(LinkStyle::FromNodeOrNull(aInfo.mContent),
             "Element is not a style linking element!");

  // Since we're not planning to load a URI, no need to hand a principal to the
  // load data or to CreateSheet().

  // Check IsAlternateSheet now, since it can mutate our document.
  auto isAlternate = IsAlternateSheet(aInfo.mTitle, aInfo.mHasAlternateRel);
  LOG(("  Sheet is alternate: %d", static_cast<int>(isAlternate)));

  // Use the document's base URL so that @import in the inline sheet picks up
  // the right base.
  nsIURI* baseURI = aInfo.mContent->GetBaseURI();
  nsIURI* sheetURI = aInfo.mContent->OwnerDoc()->GetDocumentURI();
  nsIURI* originalURI = nullptr;

  MOZ_ASSERT(aInfo.mIntegrity.IsEmpty());
  nsIPrincipal* loadingPrincipal = LoaderPrincipal();
  nsIPrincipal* principal = aInfo.mTriggeringPrincipal
                                ? aInfo.mTriggeringPrincipal.get()
                                : loadingPrincipal;
  nsIPrincipal* sheetPrincipal = [&] {
    // The triggering principal may be an expanded principal, which is safe to
    // use for URL security checks, but not as the loader principal for a
    // stylesheet. So treat this as principal inheritance, and downgrade if
    // necessary.
    //
    // FIXME(emilio): Why doing this for inline sheets but not for links?
    if (aInfo.mTriggeringPrincipal) {
      return BasePrincipal::Cast(aInfo.mTriggeringPrincipal)
          ->PrincipalToInherit();
    }
    return LoaderPrincipal();
  }();

  RefPtr<StyleSheet> sheet = LookupInlineSheetInCache(aBuffer, sheetPrincipal);
  const bool isSheetFromCache = !!sheet;
  if (!isSheetFromCache) {
    sheet = MakeRefPtr<StyleSheet>(eAuthorSheetFeatures, aInfo.mCORSMode,
                                   SRIMetadata{});
    sheet->SetURIs(sheetURI, originalURI, baseURI);
    nsIReferrerInfo* referrerInfo =
        aInfo.mContent->OwnerDoc()->ReferrerInfoForInternalCSSAndSVGResources();
    sheet->SetReferrerInfo(referrerInfo);
    // We never actually load this, so just set its principal directly.
    sheet->SetPrincipal(sheetPrincipal);
  }

  auto matched = PrepareSheet(*sheet, aInfo.mTitle, aInfo.mMedia, nullptr,
                              isAlternate, aInfo.mIsExplicitlyEnabled);
  if (auto* linkStyle = LinkStyle::FromNode(*aInfo.mContent)) {
    linkStyle->SetStyleSheet(sheet);
  }
  MOZ_ASSERT(sheet->IsComplete() == isSheetFromCache);

  Completed completed;
  auto data = MakeRefPtr<SheetLoadData>(
      this, aInfo.mTitle, /* aURI = */ nullptr, sheet, SyncLoad::No,
      aInfo.mContent, isAlternate, matched, StylePreloadKind::None, aObserver,
      principal, aInfo.mReferrerInfo, aInfo.mNonce, aInfo.mFetchPriority,
      nullptr);
  MOZ_ASSERT(data->GetRequestingNode() == aInfo.mContent);
  if (isSheetFromCache) {
    MOZ_ASSERT(sheet->IsComplete());
    MOZ_ASSERT(sheet->GetOwnerNode() == aInfo.mContent);
    completed = Completed::Yes;
    InsertSheetInTree(*sheet);
    NotifyOfCachedLoad(std::move(data));
  } else {
    // Parse completion releases the load data.
    //
    // Note that we need to parse synchronously, since the web expects that the
    // effects of inline stylesheets are visible immediately (aside from
    // @imports).
    NS_ConvertUTF16toUTF8 utf8(aBuffer);
    RefPtr<SheetLoadDataHolder> holder(
        new nsMainThreadPtrHolder<css::SheetLoadData>(__func__, data.get(),
                                                      true));
    completed = ParseSheet(utf8, holder, AllowAsyncParse::No);
    if (completed == Completed::Yes) {
      mSheets->InsertInline(LoaderPrincipal(), aBuffer, data->ValueForCache());
    } else {
      data->mMustNotify = true;
    }
  }

  return LoadSheetResult{completed, isAlternate, matched};
}

nsLiteralString SheetLoadData::InitiatorTypeString() {
  MOZ_ASSERT(mURI, "Inline sheet doesn't have the initiator type string");

  if (mPreloadKind == StylePreloadKind::FromEarlyHintsHeader) {
    return u"early-hints"_ns;
  }

  if (mParentData) {
    // @import rule.
    return u"css"_ns;
  }

  // <link>.
  return u"link"_ns;
}

Result<Loader::LoadSheetResult, nsresult> Loader::LoadStyleLink(
    const SheetInfo& aInfo, nsICSSLoaderObserver* aObserver) {
  MOZ_ASSERT(aInfo.mURI, "Must have URL to load");
  LOG(("css::Loader::LoadStyleLink"));
  LOG_URI("  Link uri: '%s'", aInfo.mURI);
  LOG(("  Link title: '%s'", NS_ConvertUTF16toUTF8(aInfo.mTitle).get()));
  LOG(("  Link media: '%s'", NS_ConvertUTF16toUTF8(aInfo.mMedia).get()));
  LOG(("  Link alternate rel: %d", aInfo.mHasAlternateRel));

  if (!mEnabled) {
    LOG_WARN(("  Not enabled"));
    return Err(NS_ERROR_NOT_AVAILABLE);
  }

  if (!mDocument) {
    return Err(NS_ERROR_NOT_INITIALIZED);
  }

  MOZ_ASSERT_IF(aInfo.mContent,
                aInfo.mContent->NodePrincipal() == mDocument->NodePrincipal());
  nsIPrincipal* loadingPrincipal = LoaderPrincipal();
  nsIPrincipal* principal = aInfo.mTriggeringPrincipal
                                ? aInfo.mTriggeringPrincipal.get()
                                : loadingPrincipal;

  nsINode* requestingNode =
      aInfo.mContent ? static_cast<nsINode*>(aInfo.mContent) : mDocument;
  const bool syncLoad = [&] {
    if (!aInfo.mContent) {
      return false;
    }
    const bool privilegedShadowTree =
        aInfo.mContent->IsInShadowTree() &&
        (aInfo.mContent->ChromeOnlyAccess() ||
         aInfo.mContent->OwnerDoc()->ChromeRulesEnabled());
    if (!privilegedShadowTree) {
      return false;
    }
    if (!IsPrivilegedURI(aInfo.mURI)) {
      return false;
    }
    // We're loading a chrome/resource URI in a chrome doc shadow tree or UA
    // widget. Load synchronously to avoid FOUC.
    return true;
  }();
  LOG(("  Link sync load: '%s'", syncLoad ? "true" : "false"));
  MOZ_ASSERT_IF(syncLoad, !aObserver);

  nsresult rv =
      CheckContentPolicy(loadingPrincipal, principal, aInfo.mURI,
                         requestingNode, aInfo.mNonce, StylePreloadKind::None);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    // Don't fire the error event if our document is loaded as data.  We're
    // supposed to not even try to do loads in that case... Unfortunately, we
    // implement that via nsDataDocumentContentPolicy, which doesn't have a good
    // way to communicate back to us that _it_ is the thing that blocked the
    // load.
    if (aInfo.mContent && !mDocument->IsLoadedAsData()) {
      // Fire an async error event on it.
      RefPtr<AsyncEventDispatcher> loadBlockingAsyncDispatcher =
          new LoadBlockingAsyncEventDispatcher(aInfo.mContent, u"error"_ns,
                                               CanBubble::eNo,
                                               ChromeOnlyDispatch::eNo);
      loadBlockingAsyncDispatcher->PostDOMEvent();
    }
    return Err(rv);
  }

  // Check IsAlternateSheet now, since it can mutate our document and make
  // pending sheets go to the non-pending state.
  auto isAlternate = IsAlternateSheet(aInfo.mTitle, aInfo.mHasAlternateRel);
  auto [sheet, state, networkMetadata] = CreateSheet(
      aInfo, eAuthorSheetFeatures, syncLoad, StylePreloadKind::None);

  LOG(("  Sheet is alternate: %d", static_cast<int>(isAlternate)));

  auto matched = PrepareSheet(*sheet, aInfo.mTitle, aInfo.mMedia, nullptr,
                              isAlternate, aInfo.mIsExplicitlyEnabled);

  if (auto* linkStyle = LinkStyle::FromNodeOrNull(aInfo.mContent)) {
    linkStyle->SetStyleSheet(sheet);
  }
  MOZ_ASSERT(sheet->IsComplete() == (state == SheetState::Complete));

  // We may get here with no content for Link: headers for example.
  MOZ_ASSERT(!aInfo.mContent || LinkStyle::FromNode(*aInfo.mContent),
             "If there is any node, it should be a LinkStyle");
  auto data = MakeRefPtr<SheetLoadData>(
      this, aInfo.mTitle, aInfo.mURI, sheet, SyncLoad(syncLoad), aInfo.mContent,
      isAlternate, matched, StylePreloadKind::None, aObserver, principal,
      aInfo.mReferrerInfo, aInfo.mNonce, aInfo.mFetchPriority,
      networkMetadata.forget());

  MOZ_ASSERT(data->GetRequestingNode() == requestingNode);

  MaybeNotifyPreloadUsed(*data);

  if (state == SheetState::Complete) {
    LOG(("  Sheet already complete: 0x%p", sheet.get()));
    MOZ_ASSERT(sheet->GetOwnerNode() == aInfo.mContent);
    InsertSheetInTree(*sheet);
    NotifyOfCachedLoad(std::move(data));
    return LoadSheetResult{Completed::Yes, isAlternate, matched};
  }

  // Now we need to actually load it.
  auto result = LoadSheetResult{Completed::No, isAlternate, matched};

  MOZ_ASSERT(result.ShouldBlock() == !data->ShouldDefer(),
             "These should better match!");

  // Load completion will free the data
  rv = LoadSheet(*data, state, 0);
  if (NS_FAILED(rv)) {
    return Err(rv);
  }

  if (!syncLoad) {
    data->mMustNotify = true;
  }
  return result;
}

static bool HaveAncestorDataWithURI(SheetLoadData& aData, nsIURI* aURI) {
  if (!aData.mURI) {
    // Inline style; this won't have any ancestors
    MOZ_ASSERT(!aData.mParentData, "How does inline style have a parent?");
    return false;
  }

  bool equal;
  if (NS_FAILED(aData.mURI->Equals(aURI, &equal)) || equal) {
    return true;
  }

  // Datas down the mNext chain have the same URI as aData, so we
  // don't have to compare to them.  But they might have different
  // parents, and we have to check all of those.
  SheetLoadData* data = &aData;
  do {
    if (data->mParentData &&
        HaveAncestorDataWithURI(*data->mParentData, aURI)) {
      return true;
    }

    data = data->mNext;
  } while (data);

  return false;
}

nsresult Loader::LoadChildSheet(StyleSheet& aParentSheet,
                                SheetLoadData* aParentData, nsIURI* aURL,
                                dom::MediaList* aMedia,
                                LoaderReusableStyleSheets* aReusableSheets) {
  LOG(("css::Loader::LoadChildSheet"));
  MOZ_ASSERT(aURL, "Must have a URI to load");

  if (!mEnabled) {
    LOG_WARN(("  Not enabled"));
    return NS_ERROR_NOT_AVAILABLE;
  }

  LOG_URI("  Child uri: '%s'", aURL);

  nsCOMPtr<nsINode> owningNode;

  nsINode* requestingNode = aParentSheet.GetOwnerNodeOfOutermostSheet();
  if (requestingNode) {
    MOZ_ASSERT(LoaderPrincipal() == requestingNode->NodePrincipal());
  } else {
    requestingNode = mDocument;
  }

  nsIPrincipal* principal = aParentSheet.Principal();
  nsresult rv =
      CheckContentPolicy(LoaderPrincipal(), principal, aURL, requestingNode,
                         /* aNonce = */ u""_ns, StylePreloadKind::None);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    if (aParentData) {
      MarkLoadTreeFailed(*aParentData);
    }
    return rv;
  }

  nsCOMPtr<nsICSSLoaderObserver> observer;

  if (aParentData) {
    LOG(("  Have a parent load"));
    // Check for cycles
    if (HaveAncestorDataWithURI(*aParentData, aURL)) {
      // Houston, we have a loop, blow off this child and pretend this never
      // happened
      LOG_ERROR(("  @import cycle detected, dropping load"));
      return NS_OK;
    }

    NS_ASSERTION(aParentData->mSheet == &aParentSheet,
                 "Unexpected call to LoadChildSheet");
  } else {
    LOG(("  No parent load; must be CSSOM"));
    // No parent load data, so the sheet will need to be notified when
    // we finish, if it can be, if we do the load asynchronously.
    observer = &aParentSheet;
  }

  // Now that we know it's safe to load this (passes security check and not a
  // loop) do so.
  RefPtr<StyleSheet> sheet;
  RefPtr<SubResourceNetworkMetadataHolder> networkMetadata;
  SheetState state;
  bool isReusableSheet = false;
  if (aReusableSheets && aReusableSheets->FindReusableStyleSheet(aURL, sheet)) {
    state = SheetState::Complete;
    isReusableSheet = true;
  } else {
    // For now, use CORS_NONE for child sheets
    std::tie(sheet, state, networkMetadata) = CreateSheet(
        aURL, nullptr, principal, aParentSheet.ParsingMode(), CORS_NONE,
        aParentData ? aParentData->mEncoding : nullptr,
        u""_ns,  // integrity is only checked on main sheet
        aParentData && aParentData->mSyncLoad, StylePreloadKind::None);
    PrepareSheet(*sheet, u""_ns, u""_ns, aMedia, IsAlternate::No,
                 IsExplicitlyEnabled::No);
  }

  MOZ_ASSERT(sheet);
  InsertChildSheet(*sheet, aParentSheet);

  auto data = MakeRefPtr<SheetLoadData>(
      this, aURL, sheet, aParentData, observer, principal,
      aParentSheet.GetReferrerInfo(), networkMetadata.forget());
  MOZ_ASSERT(data->GetRequestingNode() == requestingNode);

  MaybeNotifyPreloadUsed(*data);

  if (state == SheetState::Complete) {
    LOG(("  Sheet already complete"));
    // We're completely done.  No need to notify, even, since the
    // @import rule addition/modification will trigger the right style
    // changes automatically.
    if (!isReusableSheet) {
      // Child sheets are not handled by NotifyObservers, and these need to be
      // performed here if the sheet comes from the SharedStyleSheetCache.
      RecordUseCountersIfNeeded(mDocument, *data->mSheet);
      if (MaybePutIntoLoadsPerformed(*data)) {
        NotifyObserversForCachedSheet(*data);
        AddPerformanceEntryForCachedSheet(*data);
      }
    }
    data->mIntentionallyDropped = true;
    return NS_OK;
  }

  bool syncLoad = data->mSyncLoad;

  // Load completion will release the data
  rv = LoadSheet(*data, state, 0);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!syncLoad) {
    data->mMustNotify = true;
  }
  return rv;
}

Result<RefPtr<StyleSheet>, nsresult> Loader::LoadSheetSync(
    nsIURI* aURL, SheetParsingMode aParsingMode,
    UseSystemPrincipal aUseSystemPrincipal) {
  LOG(("css::Loader::LoadSheetSync"));
  nsCOMPtr<nsIReferrerInfo> referrerInfo = new ReferrerInfo(nullptr);
  return InternalLoadNonDocumentSheet(
      aURL, StylePreloadKind::None, aParsingMode, aUseSystemPrincipal, nullptr,
      referrerInfo, nullptr, CORS_NONE, u""_ns, u""_ns, 0, FetchPriority::Auto);
}

Result<RefPtr<StyleSheet>, nsresult> Loader::LoadSheet(
    nsIURI* aURI, SheetParsingMode aParsingMode,
    UseSystemPrincipal aUseSystemPrincipal, nsICSSLoaderObserver* aObserver) {
  nsCOMPtr<nsIReferrerInfo> referrerInfo = new ReferrerInfo(nullptr);
  return InternalLoadNonDocumentSheet(
      aURI, StylePreloadKind::None, aParsingMode, aUseSystemPrincipal, nullptr,
      referrerInfo, aObserver, CORS_NONE, u""_ns, u""_ns, 0,
      FetchPriority::Auto);
}

Result<RefPtr<StyleSheet>, nsresult> Loader::LoadSheet(
    nsIURI* aURL, StylePreloadKind aPreloadKind,
    const Encoding* aPreloadEncoding, nsIReferrerInfo* aReferrerInfo,
    nsICSSLoaderObserver* aObserver, uint64_t aEarlyHintPreloaderId,
    CORSMode aCORSMode, const nsAString& aNonce, const nsAString& aIntegrity,
    FetchPriority aFetchPriority) {
  LOG(("css::Loader::LoadSheet(aURL, aObserver) api call"));
  return InternalLoadNonDocumentSheet(
      aURL, aPreloadKind, eAuthorSheetFeatures, UseSystemPrincipal::No,
      aPreloadEncoding, aReferrerInfo, aObserver, aCORSMode, aNonce, aIntegrity,
      aEarlyHintPreloaderId, aFetchPriority);
}

Result<RefPtr<StyleSheet>, nsresult> Loader::InternalLoadNonDocumentSheet(
    nsIURI* aURL, StylePreloadKind aPreloadKind, SheetParsingMode aParsingMode,
    UseSystemPrincipal aUseSystemPrincipal, const Encoding* aPreloadEncoding,
    nsIReferrerInfo* aReferrerInfo, nsICSSLoaderObserver* aObserver,
    CORSMode aCORSMode, const nsAString& aNonce, const nsAString& aIntegrity,
    uint64_t aEarlyHintPreloaderId, FetchPriority aFetchPriority) {
  MOZ_ASSERT(aURL, "Must have a URI to load");
  MOZ_ASSERT(aUseSystemPrincipal == UseSystemPrincipal::No || !aObserver,
             "Shouldn't load system-principal sheets async");
  MOZ_ASSERT(aReferrerInfo, "Must have referrerInfo");

  LOG_URI("  Non-document sheet uri: '%s'", aURL);

  if (!mEnabled) {
    LOG_WARN(("  Not enabled"));
    return Err(NS_ERROR_NOT_AVAILABLE);
  }

  nsIPrincipal* loadingPrincipal = LoaderPrincipal();
  nsIPrincipal* triggeringPrincipal = loadingPrincipal;
  nsresult rv = CheckContentPolicy(loadingPrincipal, triggeringPrincipal, aURL,
                                   mDocument, aNonce, aPreloadKind);
  if (NS_FAILED(rv)) {
    return Err(rv);
  }

  bool syncLoad = !aObserver;
  auto [sheet, state, networkMetadata] =
      CreateSheet(aURL, nullptr, triggeringPrincipal, aParsingMode, aCORSMode,
                  aPreloadEncoding, aIntegrity, syncLoad, aPreloadKind);

  PrepareSheet(*sheet, u""_ns, u""_ns, nullptr, IsAlternate::No,
               IsExplicitlyEnabled::No);

  auto data = MakeRefPtr<SheetLoadData>(
      this, aURL, sheet, SyncLoad(syncLoad), aUseSystemPrincipal, aPreloadKind,
      aPreloadEncoding, aObserver, triggeringPrincipal, aReferrerInfo, aNonce,
      aFetchPriority, networkMetadata.forget());
  MOZ_ASSERT(data->GetRequestingNode() == mDocument);
  if (state == SheetState::Complete) {
    LOG(("  Sheet already complete"));
    NotifyOfCachedLoad(std::move(data));
    return sheet;
  }

  rv = LoadSheet(*data, state, aEarlyHintPreloaderId);
  if (NS_FAILED(rv)) {
    return Err(rv);
  }
  if (aObserver) {
    data->mMustNotify = true;
  }
  return sheet;
}

void Loader::NotifyOfCachedLoad(RefPtr<SheetLoadData> aLoadData) {
  LOG(("css::Loader::PostLoadEvent"));
  MOZ_ASSERT(aLoadData->mSheet->IsComplete(),
             "Only expected to be used for cached sheets");
  // If we get to this code, the stylesheet loaded correctly at some point, so
  // we can just schedule a load event and don't need to touch the data's
  // mLoadFailed.
  // Note that we do this here and not from inside our SheetComplete so that we
  // don't end up running the load event more async than needed.
  MOZ_ASSERT(!aLoadData->mLoadFailed, "Why are we marked as failed?");
  aLoadData->mSheetAlreadyComplete = true;

  if (aLoadData->mURI) {
    aLoadData->mShouldEmulateNotificationsForCachedLoad = true;
  }

  // We need to check mURI to match
  // DecrementOngoingLoadCountAndMaybeUnblockOnload().
  if (aLoadData->mURI && aLoadData->BlocksLoadEvent()) {
    IncrementOngoingLoadCountAndMaybeBlockOnload();
  }
  SheetComplete(*aLoadData, NS_OK);
}

void Loader::NotifyObserversForCachedSheet(SheetLoadData& aLoadData) {
  nsCOMPtr<nsIObserverService> obsService = services::GetObserverService();

  if (!obsService->HasObservers("http-on-resource-cache-response")) {
    return;
  }

  nsCOMPtr<nsIChannel> channel;
  nsresult rv = NewStyleSheetChannel(aLoadData, aLoadData.mSheet->GetCORSMode(),
                                     UsePreload::No, UseLoadGroup::No,
                                     getter_AddRefs(channel));
  if (NS_FAILED(rv)) {
    return;
  }

  RefPtr<HttpBaseChannel> httpBaseChannel = do_QueryObject(channel);
  if (httpBaseChannel) {
    const net::nsHttpResponseHead* responseHead = nullptr;
    if (aLoadData.GetNetworkMetadata()) {
      responseHead = aLoadData.GetNetworkMetadata()->GetResponseHead();
    }
    httpBaseChannel->SetDummyChannelForCachedResource(responseHead);
  }

  channel->SetContentType("text/css"_ns);

  // TODO: Populate other fields (bug 1915626).

  obsService->NotifyObservers(channel, "http-on-resource-cache-response",
                              nullptr);
}

void Loader::Stop() {
  if (mSheets) {
    mSheets->CancelLoadsForLoader(*this);
  }
}

bool Loader::HasPendingLoads() { return mOngoingLoadCount; }

void Loader::AddObserver(nsICSSLoaderObserver* aObserver) {
  MOZ_ASSERT(aObserver, "Must have observer");
  mObservers.AppendElementUnlessExists(aObserver);
}

void Loader::RemoveObserver(nsICSSLoaderObserver* aObserver) {
  mObservers.RemoveElement(aObserver);
}

void Loader::StartDeferredLoads() {
  if (mSheets && mPendingLoadCount) {
    mSheets->StartPendingLoadsForLoader(
        *this, [](const SheetLoadData&) { return true; });
  }
}

NS_IMPL_CYCLE_COLLECTION_CLASS(Loader)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(Loader)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mSheets);
  for (nsCOMPtr<nsICSSLoaderObserver>& obs : tmp->mObservers.ForwardRange()) {
    ImplCycleCollectionTraverse(cb, obs, "mozilla::css::Loader.mObservers");
  }
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDocGroup)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(Loader)
  if (tmp->mSheets) {
    if (tmp->mDocument) {
      tmp->DeregisterFromSheetCache();
    }
    tmp->mSheets = nullptr;
  }
  tmp->mObservers.Clear();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDocGroup)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

size_t Loader::SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const {
  size_t n = aMallocSizeOf(this);

  n += mObservers.ShallowSizeOfExcludingThis(aMallocSizeOf);

  // Measurement of the following members may be added later if DMD finds it is
  // worthwhile:
  // The following members aren't measured:
  // - mDocument, because it's a weak backpointer

  return n;
}

nsIPrincipal* Loader::LoaderPrincipal() const {
  if (mDocument) {
    return mDocument->NodePrincipal();
  }
  // Loaders without a document do system loads.
  return nsContentUtils::GetSystemPrincipal();
}

nsIPrincipal* Loader::PartitionedPrincipal() const {
  if (mDocument && StaticPrefs::privacy_partition_network_state()) {
    return mDocument->PartitionedPrincipal();
  }
  return LoaderPrincipal();
}

bool Loader::ShouldBypassCache() const {
  return mDocument && nsContentUtils::ShouldBypassSubResourceCache(mDocument);
}

void Loader::BlockOnload() {
  if (mDocument) {
    mDocument->BlockOnload();
  }
}

void Loader::UnblockOnload(bool aFireSync) {
  if (mDocument) {
    mDocument->UnblockOnload(aFireSync);
  }
}

}  // namespace css
}  // namespace mozilla
