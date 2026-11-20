/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* loading of CSS style sheets using the network APIs */

#include "mozilla/css/Loader.h"

#include "mozilla/ArrayUtils.h"
#include "mozilla/dom/DocGroup.h"
#include "mozilla/dom/SRILogHelper.h"
#include "mozilla/IntegerPrintfMacros.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/LoadInfo.h"
#include "mozilla/Logging.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/PreloadHashKey.h"
#include "mozilla/ResultExtensions.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/URLPreloader.h"
#include "nsIChildChannel.h"
#include "nsIRunnable.h"
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
#include "nsNetUtil.h"
#include "nsContentUtils.h"
#include "nsIScriptSecurityManager.h"
#include "nsContentPolicyUtils.h"
#include "nsIHttpChannel.h"
#include "nsIHttpChannelInternal.h"
#include "nsIClassOfService.h"
#include "nsIScriptError.h"
#include "nsMimeTypes.h"
#include "nsICSSLoaderObserver.h"
#include "nsThreadUtils.h"
#include "nsGkAtoms.h"
#include "nsIThreadInternal.h"
#include "nsINetworkPredictor.h"
#include "nsQueryActor.h"
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
#include "mozilla/ServoUtils.h"
#include "mozilla/css/StreamLoader.h"
#include "mozilla/SharedStyleSheetCache.h"
#include "mozilla/StaticPrefs_dom.h"
#include "ReferrerInfo.h"

#include "nsXULPrototypeCache.h"

#include "nsError.h"

#include "mozilla/dom/SRICheck.h"

#include "mozilla/Encoding.h"

using namespace mozilla::dom;

// 1024 bytes is specified in https://drafts.csswg.org/css-syntax/
#define SNIFFING_BUFFER_SIZE 1024

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
      mPrincipal(aLoadData.mTriggeringPrincipal),
      mLoaderPrincipal(aLoadData.mLoader->LoaderPrincipal()),
      mPartitionPrincipal(aLoadData.mLoader->PartitionedPrincipal()),
      mEncodingGuess(aLoadData.mGuessedEncoding),
      mCORSMode(aLoadData.mSheet->GetCORSMode()),
      mParsingMode(aLoadData.mSheet->ParsingMode()),
      mCompatMode(aLoadData.mCompatMode),
      mIsLinkRelPreload(aLoadData.IsLinkRelPreload()),
      mLoadedAsImage(aLoadData.mLoader->GetDocument() &&
                     aLoadData.mLoader->GetDocument()->IsBeingUsedAsImage()) {
  MOZ_COUNT_CTOR(SheetLoadDataHashKey);
  MOZ_ASSERT(mURI);
  MOZ_ASSERT(mPrincipal);
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

  if (!mPrincipal->Equals(aKey.mPrincipal)) {
    LOG((" > Principal mismatch\n"));
    return false;
  }

  // We only check for partition principal equality if any of the loads are
  // triggered by a document rather than e.g. an extension (which have different
  // origins than the loader principal).
  if (mPrincipal->Equals(mLoaderPrincipal) ||
      aKey.mPrincipal->Equals(aKey.mLoaderPrincipal)) {
    if (!mPartitionPrincipal->Equals(aKey.mPartitionPrincipal)) {
      LOG((" > Partition principal mismatch\n"));
      return false;
    }
  }

  if (mCORSMode != aKey.mCORSMode) {
    LOG((" > CORS mismatch\n"));
    return false;
  }

  if (mCompatMode != aKey.mCompatMode) {
    LOG((" > Quirks mismatch\n"));
    return false;
  }

  if (mLoadedAsImage != aKey.mLoadedAsImage) {
    LOG((" > Loaded as image mismatch\n"));
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
  if (mIsLinkRelPreload != aKey.mIsLinkRelPreload) {
    const auto& linkPreloadMetadata =
        mIsLinkRelPreload ? mSRIMetadata : aKey.mSRIMetadata;
    const auto& consumerPreloadMetadata =
        mIsLinkRelPreload ? aKey.mSRIMetadata : mSRIMetadata;

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
NS_IMPL_ISUPPORTS(SheetLoadData, nsIRunnable, nsIThreadObserver)

SheetLoadData::SheetLoadData(css::Loader* aLoader, const nsAString& aTitle,
                             nsIURI* aURI, StyleSheet* aSheet, bool aSyncLoad,
                             nsINode* aOwningNode, IsAlternate aIsAlternate,
                             MediaMatched aMediaMatches,
                             StylePreloadKind aPreloadKind,
                             nsICSSLoaderObserver* aObserver,
                             nsIPrincipal* aTriggeringPrincipal,
                             nsIReferrerInfo* aReferrerInfo)
    : mLoader(aLoader),
      mTitle(aTitle),
      mEncoding(nullptr),
      mURI(aURI),
      mLineNumber(1),
      mSheet(aSheet),
      mPendingChildren(0),
      mSyncLoad(aSyncLoad),
      mIsNonDocumentSheet(false),
      mIsChildSheet(aSheet->GetParentSheet()),
      mIsBeingParsed(false),
      mIsLoading(false),
      mIsCancelled(false),
      mMustNotify(false),
      mWasAlternate(aIsAlternate == IsAlternate::Yes),
      mMediaMatched(aMediaMatches == MediaMatched::Yes),
      mUseSystemPrincipal(false),
      mSheetAlreadyComplete(false),
      mIsCrossOriginNoCORS(false),
      mBlockResourceTiming(false),
      mLoadFailed(false),
      mPreloadKind(aPreloadKind),
      mOwningNodeBeforeLoadEvent(aOwningNode),
      mObserver(aObserver),
      mTriggeringPrincipal(aTriggeringPrincipal),
      mReferrerInfo(aReferrerInfo),
      mGuessedEncoding(GetFallbackEncoding(*aLoader, aOwningNode, nullptr)),
      mCompatMode(aLoader->CompatMode(aPreloadKind)) {
  MOZ_ASSERT(!mOwningNodeBeforeLoadEvent ||
                 dom::LinkStyle::FromNode(*mOwningNodeBeforeLoadEvent),
             "Must implement LinkStyle");
  MOZ_ASSERT(mTriggeringPrincipal);
  MOZ_ASSERT(mLoader, "Must have a loader!");
}

SheetLoadData::SheetLoadData(css::Loader* aLoader, nsIURI* aURI,
                             StyleSheet* aSheet, SheetLoadData* aParentData,
                             nsICSSLoaderObserver* aObserver,
                             nsIPrincipal* aTriggeringPrincipal,
                             nsIReferrerInfo* aReferrerInfo)
    : mLoader(aLoader),
      mEncoding(nullptr),
      mURI(aURI),
      mLineNumber(1),
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
      mWasAlternate(false),
      mMediaMatched(true),
      mUseSystemPrincipal(aParentData && aParentData->mUseSystemPrincipal),
      mSheetAlreadyComplete(false),
      mIsCrossOriginNoCORS(false),
      mBlockResourceTiming(false),
      mLoadFailed(false),
      mPreloadKind(StylePreloadKind::None),
      mObserver(aObserver),
      mTriggeringPrincipal(aTriggeringPrincipal),
      mReferrerInfo(aReferrerInfo),
      mGuessedEncoding(GetFallbackEncoding(
          *aLoader, nullptr, aParentData ? aParentData->mEncoding : nullptr)),
      mCompatMode(aLoader->CompatMode(mPreloadKind)) {
  MOZ_ASSERT(mLoader, "Must have a loader!");
  MOZ_ASSERT(mTriggeringPrincipal);
  MOZ_ASSERT(!mUseSystemPrincipal || mSyncLoad,
             "Shouldn't use system principal for async loads");
  MOZ_ASSERT_IF(aParentData, mIsChildSheet);
}

SheetLoadData::SheetLoadData(
    css::Loader* aLoader, nsIURI* aURI, StyleSheet* aSheet, bool aSyncLoad,
    UseSystemPrincipal aUseSystemPrincipal, StylePreloadKind aPreloadKind,
    const Encoding* aPreloadEncoding, nsICSSLoaderObserver* aObserver,
    nsIPrincipal* aTriggeringPrincipal, nsIReferrerInfo* aReferrerInfo)
    : mLoader(aLoader),
      mEncoding(nullptr),
      mURI(aURI),
      mLineNumber(1),
      mSheet(aSheet),
      mPendingChildren(0),
      mSyncLoad(aSyncLoad),
      mIsNonDocumentSheet(true),
      mIsChildSheet(false),
      mIsBeingParsed(false),
      mIsLoading(false),
      mIsCancelled(false),
      mMustNotify(false),
      mWasAlternate(false),
      mMediaMatched(true),
      mUseSystemPrincipal(aUseSystemPrincipal == UseSystemPrincipal::Yes),
      mSheetAlreadyComplete(false),
      mIsCrossOriginNoCORS(false),
      mBlockResourceTiming(false),
      mLoadFailed(false),
      mPreloadKind(aPreloadKind),
      mObserver(aObserver),
      mTriggeringPrincipal(aTriggeringPrincipal),
      mReferrerInfo(aReferrerInfo),
      mGuessedEncoding(
          GetFallbackEncoding(*aLoader, nullptr, aPreloadEncoding)),
      mCompatMode(aLoader->CompatMode(aPreloadKind)) {
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

NS_IMETHODIMP
SheetLoadData::Run() {
  mLoader->HandleLoadEvent(*this);
  return NS_OK;
}

NS_IMETHODIMP
SheetLoadData::OnDispatchedEvent() { return NS_OK; }

NS_IMETHODIMP
SheetLoadData::OnProcessNextEvent(nsIThreadInternal* aThread, bool aMayWait) {
  // XXXkhuey this is insane!
  // We want to fire our load even before or after event processing,
  // whichever comes first.
  FireLoadEvent(aThread);
  return NS_OK;
}

NS_IMETHODIMP
SheetLoadData::AfterProcessNextEvent(nsIThreadInternal* aThread,
                                     bool aEventWasProcessed) {
  // XXXkhuey this too!
  // We want to fire our load even before or after event processing,
  // whichever comes first.
  FireLoadEvent(aThread);
  return NS_OK;
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

void SheetLoadData::PrioritizeAsPreload() { PrioritizeAsPreload(Channel()); }

void SheetLoadData::FireLoadEvent(nsIThreadInternal* aThread) {
  // First remove ourselves as a thread observer.  But we need to keep
  // ourselves alive while doing that!
  RefPtr<SheetLoadData> kungFuDeathGrip(this);
  aThread->RemoveObserver(this);

  // Now fire the event.
  //
  // NOTE(emilio): A bit weird that we fire the event even if the node is no
  // longer in the tree, or the sheet that just loaded / errored is not the
  // current node.sheet, but...
  nsCOMPtr<nsINode> node = std::move(mOwningNodeBeforeLoadEvent);
  MOZ_ASSERT(node, "How did that happen???");

  nsContentUtils::DispatchTrustedEvent(node->OwnerDoc(), node,
                                       mLoadFailed ? u"error"_ns : u"load"_ns,
                                       CanBubble::eNo, Cancelable::eNo);

  MOZ_ASSERT(BlocksLoadEvent());
  mLoader->UnblockOnload(true);
}

void SheetLoadData::StartPendingLoad() {
  mLoader->LoadSheet(*this, Loader::SheetState::NeedsParser, 0,
                     Loader::PendingLoad::Yes);
}

void SheetLoadData::ScheduleLoadEventIfNeeded() {
  if (!mOwningNodeBeforeLoadEvent) {
    return;
  }

  MOZ_ASSERT(BlocksLoadEvent(), "The rel=preload load event happens elsewhere");

  nsCOMPtr<nsIThread> thread = do_GetCurrentThread();
  nsCOMPtr<nsIThreadInternal> internalThread = do_QueryInterface(thread);
  if (NS_SUCCEEDED(internalThread->AddObserver(this))) {
    mLoader->BlockOnload();
  }
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

Loader::~Loader() {
  // Note: no real need to revoke our stylesheet loaded events -- they
  // hold strong references to us, so if we're going away that means
  // they're all done.
}

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
  if (aDataLength <= sizeof(kCharsetSym) - 1) return false;

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
  const Encoding* encoding;
  nsAutoCString label;

  // Check HTTP
  if (aChannel && NS_SUCCEEDED(aChannel->GetContentCharset(label))) {
    encoding = Encoding::ForLabel(label);
    if (encoding) {
      return WrapNotNull(encoding);
    }
  }

  // Check @charset
  auto sniffingLength = aSegment.Length();
  if (sniffingLength > SNIFFING_BUFFER_SIZE) {
    sniffingLength = SNIFFING_BUFFER_SIZE;
  }
  if (GetCharsetFromData(aSegment.BeginReading(), sniffingLength, label)) {
    encoding = Encoding::ForLabel(label);
    if (encoding == UTF_16BE_ENCODING || encoding == UTF_16LE_ENCODING) {
      return UTF_8_ENCODING;
    }
    if (encoding) {
      return WrapNotNull(encoding);
    }
  }
  return mGuessedEncoding;
}

static nsresult VerifySheetIntegrity(const SRIMetadata& aMetadata,
                                     nsIChannel* aChannel,
                                     const nsACString& aFirst,
                                     const nsACString& aSecond,
                                     const nsACString& aSourceFileURI,
                                     nsIConsoleReportCollector* aReporter) {
  NS_ENSURE_ARG_POINTER(aReporter);

  if (MOZ_LOG_TEST(SRILogHelper::GetSriLog(), LogLevel::Debug)) {
    nsAutoCString requestURL;
    nsCOMPtr<nsIURI> originalURI;
    if (aChannel &&
        NS_SUCCEEDED(aChannel->GetOriginalURI(getter_AddRefs(originalURI))) &&
        originalURI) {
      originalURI->GetAsciiSpec(requestURL);
    }
    MOZ_LOG(SRILogHelper::GetSriLog(), LogLevel::Debug,
            ("VerifySheetIntegrity (unichar stream)"));
  }

  SRICheckDataVerifier verifier(aMetadata, aSourceFileURI, aReporter);
  nsresult rv =
      verifier.Update(aFirst.Length(), (const uint8_t*)aFirst.BeginReading());
  NS_ENSURE_SUCCESS(rv, rv);
  rv =
      verifier.Update(aSecond.Length(), (const uint8_t*)aSecond.BeginReading());
  NS_ENSURE_SUCCESS(rv, rv);

  return verifier.Verify(aMetadata, aChannel, aSourceFileURI, aReporter);
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

/*
 * Stream completion code shared by Stylo and the old style system.
 *
 * Here we need to check that the load did not give us an http error
 * page and check the mimetype on the channel to make sure we're not
 * loading non-text/css data in standards mode.
 */
nsresult SheetLoadData::VerifySheetReadyToParse(nsresult aStatus,
                                                const nsACString& aBytes1,
                                                const nsACString& aBytes2,
                                                nsIChannel* aChannel) {
  LOG(("SheetLoadData::VerifySheetReadyToParse"));
  NS_ASSERTION(!mLoader->mSyncCallback, "Synchronous callback from necko");

  if (AllLoadsCanceled(*this)) {
    LOG_WARN(("  All loads are canceled, dropping"));
    mLoader->SheetComplete(*this, NS_BINDING_ABORTED);
    return NS_OK;
  }

  if (NS_FAILED(aStatus)) {
    LOG_WARN(
        ("  Load failed: status 0x%" PRIx32, static_cast<uint32_t>(aStatus)));
    // Handle sheet not loading error because source was a tracking URL (or
    // fingerprinting, cryptomining, etc).
    // We make a note of this sheet node by including it in a dedicated
    // array of blocked tracking nodes under its parent document.
    //
    // Multiple sheet load instances might be tied to this request,
    // we annotate each one linked to a valid owning element (node).
    if (net::UrlClassifierFeatureFactory::IsClassifierBlockingErrorCode(
            aStatus)) {
      if (Document* doc = mLoader->GetDocument()) {
        for (SheetLoadData* data = this; data; data = data->mNext) {
          // mOwningNode may be null but AddBlockTrackingNode can cope
          doc->AddBlockedNodeByClassifier(
              nsIContent::FromNodeOrNull(data->mOwningNodeBeforeLoadEvent));
        }
      }
    }
    mLoader->SheetComplete(*this, aStatus);
    return NS_OK;
  }

  if (!aChannel) {
    mLoader->SheetComplete(*this, NS_OK);
    return NS_OK;
  }

  nsCOMPtr<nsIURI> originalURI;
  aChannel->GetOriginalURI(getter_AddRefs(originalURI));

  // If the channel's original URI is "chrome:", we want that, since
  // the observer code in nsXULPrototypeCache depends on chrome stylesheets
  // having a chrome URI.  (Whether or not chrome stylesheets come through
  // this codepath seems nondeterministic.)
  // Otherwise we want the potentially-HTTP-redirected URI.
  nsCOMPtr<nsIURI> channelURI;
  NS_GetFinalChannelURI(aChannel, getter_AddRefs(channelURI));

  if (!channelURI || !originalURI) {
    NS_ERROR("Someone just violated the nsIRequest contract");
    LOG_WARN(("  Channel without a URI.  Bad!"));
    mLoader->SheetComplete(*this, NS_ERROR_UNEXPECTED);
    return NS_OK;
  }

  nsCOMPtr<nsIPrincipal> principal;
  nsIScriptSecurityManager* secMan = nsContentUtils::GetSecurityManager();
  nsresult result = NS_ERROR_NOT_AVAILABLE;
  if (secMan) {  // Could be null if we already shut down
    if (mUseSystemPrincipal) {
      result = secMan->GetSystemPrincipal(getter_AddRefs(principal));
    } else {
      result = secMan->GetChannelResultPrincipal(aChannel,
                                                 getter_AddRefs(principal));
    }
  }

  if (NS_FAILED(result)) {
    LOG_WARN(("  Couldn't get principal"));
    mLoader->SheetComplete(*this, result);
    return NS_OK;
  }

  mSheet->SetPrincipal(principal);

  if (mSheet->GetCORSMode() == CORS_NONE &&
      !mTriggeringPrincipal->Subsumes(principal)) {
    mIsCrossOriginNoCORS = true;
  }

  // If it's an HTTP channel, we want to make sure this is not an
  // error document we got.
  if (nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aChannel)) {
    bool requestSucceeded;
    result = httpChannel->GetRequestSucceeded(&requestSucceeded);
    if (NS_SUCCEEDED(result) && !requestSucceeded) {
      LOG(("  Load returned an error page"));
      mLoader->SheetComplete(*this, NS_ERROR_NOT_AVAILABLE);
      return NS_OK;
    }

    nsAutoCString sourceMapURL;
    if (nsContentUtils::GetSourceMapURL(httpChannel, sourceMapURL)) {
      mSheet->SetSourceMapURL(NS_ConvertUTF8toUTF16(sourceMapURL));
    }
  }

  nsAutoCString contentType;
  aChannel->GetContentType(contentType);

  // In standards mode, a style sheet must have one of these MIME
  // types to be processed at all.  In quirks mode, we accept any
  // MIME type, but only if the style sheet is same-origin with the
  // requesting document or parent sheet.  See bug 524223.

  bool validType = contentType.EqualsLiteral("text/css") ||
                   contentType.EqualsLiteral(UNKNOWN_CONTENT_TYPE) ||
                   contentType.IsEmpty();

  if (!validType) {
    const char* errorMessage;
    uint32_t errorFlag;
    bool sameOrigin = true;

    bool subsumed;
    result = mTriggeringPrincipal->Subsumes(principal, &subsumed);
    if (NS_FAILED(result) || !subsumed) {
      sameOrigin = false;
    }

    if (sameOrigin && mCompatMode == eCompatibility_NavQuirks) {
      errorMessage = "MimeNotCssWarn";
      errorFlag = nsIScriptError::warningFlag;
    } else {
      errorMessage = "MimeNotCss";
      errorFlag = nsIScriptError::errorFlag;
    }

    AutoTArray<nsString, 2> strings;
    CopyUTF8toUTF16(channelURI->GetSpecOrDefault(), *strings.AppendElement());
    CopyASCIItoUTF16(contentType, *strings.AppendElement());

    nsCOMPtr<nsIURI> referrer = ReferrerInfo()->GetOriginalReferrer();
    nsContentUtils::ReportToConsole(
        errorFlag, "CSS Loader"_ns, mLoader->mDocument,
        nsContentUtils::eCSS_PROPERTIES, errorMessage, strings, referrer);

    if (errorFlag == nsIScriptError::errorFlag) {
      LOG_WARN(
          ("  Ignoring sheet with improper MIME type %s", contentType.get()));
      mLoader->SheetComplete(*this, NS_ERROR_NOT_AVAILABLE);
      return NS_OK;
    }
  }

  SRIMetadata sriMetadata;
  mSheet->GetIntegrity(sriMetadata);
  if (!sriMetadata.IsEmpty()) {
    nsAutoCString sourceUri;
    if (mLoader->mDocument && mLoader->mDocument->GetDocumentURI()) {
      mLoader->mDocument->GetDocumentURI()->GetAsciiSpec(sourceUri);
    }
    nsresult rv = VerifySheetIntegrity(sriMetadata, aChannel, aBytes1, aBytes2,
                                       sourceUri, mLoader->mReporter);

    nsCOMPtr<nsILoadGroup> loadGroup;
    aChannel->GetLoadGroup(getter_AddRefs(loadGroup));
    if (loadGroup) {
      mLoader->mReporter->FlushConsoleReports(loadGroup);
    } else {
      mLoader->mReporter->FlushConsoleReports(mLoader->mDocument);
    }

    if (NS_FAILED(rv)) {
      LOG(("  Load was blocked by SRI"));
      MOZ_LOG(gSriPRLog, LogLevel::Debug,
              ("css::Loader::OnStreamComplete, bad metadata"));
      mLoader->SheetComplete(*this, NS_ERROR_SRI_CORRUPT);
      return NS_OK;
    }
  }

  // Enough to set the URIs on mSheet, since any sibling datas we have share
  // the same mInner as mSheet and will thus get the same URI.
  mSheet->SetURIs(channelURI, originalURI, channelURI);

  ReferrerPolicy policy =
      nsContentUtils::GetReferrerPolicyFromChannel(aChannel);
  nsCOMPtr<nsIReferrerInfo> referrerInfo =
      ReferrerInfo::CreateForExternalCSSResources(mSheet, policy);

  mSheet->SetReferrerInfo(referrerInfo);
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
      aPreloadKind == StylePreloadKind::None
          ? nsIContentPolicy::TYPE_INTERNAL_STYLESHEET
          : nsIContentPolicy::TYPE_INTERNAL_STYLESHEET_PRELOAD;

  nsCOMPtr<nsILoadInfo> secCheckLoadInfo = new net::LoadInfo(
      aLoadingPrincipal, aTriggeringPrincipal, aRequestingNode,
      nsILoadInfo::SEC_ONLY_FOR_EXPLICIT_CONTENTSEC_CHECK, contentPolicyType);

  // snapshot the nonce at load start time for performing CSP checks
  if (contentPolicyType == nsIContentPolicy::TYPE_INTERNAL_STYLESHEET) {
    secCheckLoadInfo->SetCspNonce(aNonce);
    MOZ_ASSERT_IF(aPreloadKind != StylePreloadKind::None, aNonce.IsEmpty());
  }

  int16_t shouldLoad = nsIContentPolicy::ACCEPT;
  nsresult rv = NS_CheckContentLoadPolicy(aTargetURI, secCheckLoadInfo,
                                          "text/css"_ns, &shouldLoad,
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
                                      const StyleUseCounters* aCounters) {
  if (!aDoc || !aCounters) {
    return;
  }
  const StyleUseCounters* docCounters = aDoc->GetStyleUseCounters();
  if (!docCounters) {
    return;
  }
  Servo_UseCounters_Merge(docCounters, aCounters);
  aDoc->MaybeWarnAboutZoom();
}

/**
 * CreateSheet() creates a StyleSheet object for the given URI.
 *
 * We check for an existing style sheet object for that uri in various caches
 * and clone it if we find it.  Cloned sheets will have the title/media/enabled
 * state of the sheet they are clones off; make sure to call PrepareSheet() on
 * the result of CreateSheet().
 */
std::tuple<RefPtr<StyleSheet>, Loader::SheetState> Loader::CreateSheet(
    nsIURI* aURI, nsIContent* aLinkingContent,
    nsIPrincipal* aTriggeringPrincipal, css::SheetParsingMode aParsingMode,
    CORSMode aCORSMode, const Encoding* aPreloadOrParentDataEncoding,
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
    SheetLoadDataHashKey key(aURI, aTriggeringPrincipal, LoaderPrincipal(),
                             PartitionedPrincipal(),
                             GetFallbackEncoding(*this, aLinkingContent,
                                                 aPreloadOrParentDataEncoding),
                             aCORSMode, aParsingMode, CompatMode(aPreloadKind),
                             mDocument && mDocument->IsBeingUsedAsImage(),
                             aPreloadKind, sriMetadata);
    auto cacheResult = mSheets->Lookup(*this, key, aSyncLoad);
    if (cacheResult.mState != CachedSubResourceState::Miss) {
      SheetState sheetState = SheetState::Complete;
      RefPtr<StyleSheet> sheet;
      if (cacheResult.mCompleteValue) {
        sheet = cacheResult.mCompleteValue->Clone(nullptr, nullptr);
        mDocument->SetDidHitCompleteSheetCache();
        RecordUseCountersIfNeeded(mDocument, sheet->GetStyleUseCounters());
        mLoadsPerformed.PutEntry(key);
      } else {
        MOZ_ASSERT(cacheResult.mLoadingOrPendingValue);
        sheet = cacheResult.mLoadingOrPendingValue->ValueForCache();
        sheetState = cacheResult.mState == CachedSubResourceState::Loading
                         ? SheetState::Loading
                         : SheetState::Pending;
      }
      LOG(("  Hit cache with state: %s", gStateStrings[size_t(sheetState)]));
      return {std::move(sheet), sheetState};
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
  return {std::move(sheet), SheetState::NeedsParser};
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

  nsINode* owningNode = aSheet.GetOwnerNode();
  MOZ_ASSERT(!owningNode || owningNode->IsInUncomposedDoc() ||
                 owningNode->IsInShadowTree(),
             "Why would we insert it anywhere?");
  ShadowRoot* shadow = owningNode ? owningNode->GetContainingShadow() : nullptr;

  auto& target = shadow ? static_cast<DocumentOrShadowRoot&>(*shadow)
                        : static_cast<DocumentOrShadowRoot&>(*mDocument);

  // XXX Need to cancel pending sheet loads for this element, if any

  int32_t sheetCount = target.SheetCount();

  /*
   * Start the walk at the _end_ of the list, since in the typical
   * case we'll just want to append anyway.  We want to break out of
   * the loop when insertionPoint points to just before the index we
   * want to insert at.  In other words, when we leave the loop
   * insertionPoint is the index of the stylesheet that immediately
   * precedes the one we're inserting.
   */
  int32_t insertionPoint = sheetCount - 1;
  for (; insertionPoint >= 0; --insertionPoint) {
    nsINode* sheetOwner = target.SheetAt(insertionPoint)->GetOwnerNode();
    if (sheetOwner && !owningNode) {
      // Keep moving; all sheets with a sheetOwner come after all
      // sheets without a linkingNode
      continue;
    }

    if (!sheetOwner) {
      // Aha!  The current sheet has no sheet owner, so we want to insert after
      // it no matter whether we have a linking content or not.
      break;
    }

    MOZ_ASSERT(owningNode != sheetOwner, "Why do we still have our old sheet?");

    // Have to compare
    if (nsContentUtils::PositionIsBefore(sheetOwner, owningNode)) {
      // The current sheet comes before us, and it better be the first
      // such, because now we break
      break;
    }
  }

  ++insertionPoint;

  if (shadow) {
    shadow->InsertSheetAt(insertionPoint, aSheet);
  } else {
    mDocument->InsertSheetAt(insertionPoint, aSheet);
  }

  LOG(("  Inserting into target (doc: %d) at position %d",
       target.AsNode().IsDocument(), insertionPoint));
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
      IncrementOngoingLoadCount();
    }

    // We technically never defer non-top-level sheets, so this condition could
    // be outside the branch, but conceptually it should be here.
    if (aLoadData.mParentData) {
      ++aLoadData.mParentData->mPendingChildren;
    }
  }

  nsresult rv = NS_OK;

  if (!mDocument && !aLoadData.mIsNonDocumentSheet) {
    // No point starting the load; just release all the data and such.
    LOG_WARN(("  No document and not non-document sheet; pre-dropping load"));
    SheetComplete(aLoadData, NS_BINDING_ABORTED);
    return NS_BINDING_ABORTED;
  }

  SRIMetadata sriMetadata;
  aLoadData.mSheet->GetIntegrity(sriMetadata);

  nsINode* requestingNode = aLoadData.GetRequestingNode();

  if (aLoadData.mSyncLoad) {
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
                          nsINetworkPredictor::LEARN_LOAD_SUBRESOURCE,
                          mDocument);
    }

    // Synchronous loads should only be used internally. Therefore no CORS
    // policy is needed.
    nsSecurityFlags securityFlags =
        nsContentSecurityManager::ComputeSecurityFlags(
            CORSMode::CORS_NONE, nsContentSecurityManager::CORSSecurityMapping::
                                     CORS_NONE_MAPS_TO_INHERITED_CONTEXT);

    securityFlags |= nsILoadInfo::SEC_ALLOW_CHROME;

    nsContentPolicyType contentPolicyType =
        aLoadData.mPreloadKind == StylePreloadKind::None
            ? nsIContentPolicy::TYPE_INTERNAL_STYLESHEET
            : nsIContentPolicy::TYPE_INTERNAL_STYLESHEET_PRELOAD;

    // Just load it
    nsCOMPtr<nsIChannel> channel;
    // Note that we are calling NS_NewChannelWithTriggeringPrincipal() with both
    // a node and a principal.
    // This is because of a case where the node is the document being styled and
    // the principal is the stylesheet (perhaps from a different origin) that is
    // applying the styles.
    if (requestingNode) {
      rv = NS_NewChannelWithTriggeringPrincipal(
          getter_AddRefs(channel), aLoadData.mURI, requestingNode,
          aLoadData.mTriggeringPrincipal, securityFlags, contentPolicyType);
    } else {
      MOZ_ASSERT(aLoadData.mTriggeringPrincipal->Equals(LoaderPrincipal()));
      auto result = URLPreloader::ReadURI(aLoadData.mURI);
      if (result.isOk()) {
        nsCOMPtr<nsIInputStream> stream;
        MOZ_TRY(
            NS_NewCStringInputStream(getter_AddRefs(stream), result.unwrap()));

        rv = NS_NewInputStreamChannel(
            getter_AddRefs(channel), aLoadData.mURI, stream.forget(),
            aLoadData.mTriggeringPrincipal, securityFlags, contentPolicyType);
      } else {
        rv = NS_NewChannel(getter_AddRefs(channel), aLoadData.mURI,
                           aLoadData.mTriggeringPrincipal, securityFlags,
                           contentPolicyType);
      }
    }
    if (NS_FAILED(rv)) {
      LOG_ERROR(("  Failed to create channel"));
      streamLoader->ChannelOpenFailed(rv);
      SheetComplete(aLoadData, rv);
      return rv;
    }

    // snapshot the nonce at load start time for performing CSP checks
    if (contentPolicyType == nsIContentPolicy::TYPE_INTERNAL_STYLESHEET) {
      if (requestingNode) {
        // TODO(bug 1607009) move to SheetLoadData
        nsString* cspNonce = static_cast<nsString*>(
            requestingNode->GetProperty(nsGkAtoms::nonce));
        if (cspNonce) {
          nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();
          loadInfo->SetCspNonce(*cspNonce);
        }
      }
    }

    nsCOMPtr<nsIInputStream> stream;
    rv = channel->Open(getter_AddRefs(stream));

    if (NS_FAILED(rv)) {
      LOG_ERROR(("  Failed to open URI synchronously"));
      streamLoader->ChannelOpenFailed(rv);
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

  SheetLoadDataHashKey key(aLoadData);

  auto preloadKey = PreloadHashKey::CreateAsStyle(aLoadData);
  bool coalescedLoad = false;
  if (mSheets) {
    // If we have at least one other load ongoing, then we can defer it until
    // all non-pending loads are done.
    if (aSheetState == SheetState::NeedsParser &&
        aPendingLoad == PendingLoad::No && aLoadData.ShouldDefer() &&
        mOngoingLoadCount > mPendingLoadCount + 1) {
      LOG(("  Deferring sheet load"));
      ++mPendingLoadCount;
      mSheets->DeferLoad(key, aLoadData);
      return NS_OK;
    }

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

    if ((coalescedLoad = mSheets->CoalesceLoad(key, aLoadData, cacheState))) {
      if (aSheetState == SheetState::Pending) {
        ++mPendingLoadCount;
        return NS_OK;
      }
    }
  }

  aLoadData.NotifyOpen(preloadKey, mDocument, aLoadData.IsLinkRelPreload());
  if (coalescedLoad) {
    // All done here; once the load completes we'll be marked complete
    // automatically.
    return NS_OK;
  }

  nsCOMPtr<nsILoadGroup> loadGroup;
  nsCOMPtr<nsICookieJarSettings> cookieJarSettings;
  if (mDocument) {
    loadGroup = mDocument->GetDocumentLoadGroup();
    // load for a document with no loadgrup indicates that something is
    // completely bogus, let's bail out early.
    if (!loadGroup) {
      LOG_ERROR(("  Failed to query loadGroup from document"));
      SheetComplete(aLoadData, NS_ERROR_UNEXPECTED);
      return NS_ERROR_UNEXPECTED;
    }

    cookieJarSettings = mDocument->CookieJarSettings();
  }

#ifdef DEBUG
  AutoRestore<bool> syncCallbackGuard(mSyncCallback);
  mSyncCallback = true;
#endif

  nsSecurityFlags securityFlags =
      nsContentSecurityManager::ComputeSecurityFlags(
          aLoadData.mSheet->GetCORSMode(),
          nsContentSecurityManager::CORSSecurityMapping::
              CORS_NONE_MAPS_TO_INHERITED_CONTEXT);

  securityFlags |= nsILoadInfo::SEC_ALLOW_CHROME;

  nsContentPolicyType contentPolicyType =
      aLoadData.mPreloadKind == StylePreloadKind::None
          ? nsIContentPolicy::TYPE_INTERNAL_STYLESHEET
          : nsIContentPolicy::TYPE_INTERNAL_STYLESHEET_PRELOAD;

  nsCOMPtr<nsIChannel> channel;
  // Note we are calling NS_NewChannelWithTriggeringPrincipal here with a node
  // and a principal. This is because of a case where the node is the document
  // being styled and the principal is the stylesheet (perhaps from a different
  // origin)  that is applying the styles.
  if (requestingNode) {
    rv = NS_NewChannelWithTriggeringPrincipal(
        getter_AddRefs(channel), aLoadData.mURI, requestingNode,
        aLoadData.mTriggeringPrincipal, securityFlags, contentPolicyType,
        /* PerformanceStorage */ nullptr, loadGroup);
  } else {
    MOZ_ASSERT(aLoadData.mTriggeringPrincipal->Equals(LoaderPrincipal()));
    rv = NS_NewChannel(getter_AddRefs(channel), aLoadData.mURI,
                       aLoadData.mTriggeringPrincipal, securityFlags,
                       contentPolicyType, cookieJarSettings,
                       /* aPerformanceStorage */ nullptr, loadGroup);
  }

  if (NS_FAILED(rv)) {
    LOG_ERROR(("  Failed to create channel"));
    SheetComplete(aLoadData, rv);
    return rv;
  }

  // snapshot the nonce at load start time for performing CSP checks
  if (contentPolicyType == nsIContentPolicy::TYPE_INTERNAL_STYLESHEET) {
    if (requestingNode) {
      // TODO(bug 1607009) move to SheetLoadData
      nsString* cspNonce =
          static_cast<nsString*>(requestingNode->GetProperty(nsGkAtoms::nonce));
      if (cspNonce) {
        nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();
        loadInfo->SetCspNonce(*cspNonce);
      }
    }
  }

  if (!aLoadData.ShouldDefer()) {
    if (nsCOMPtr<nsIClassOfService> cos = do_QueryInterface(channel)) {
      cos->AddClassFlags(nsIClassOfService::Leader);
    }
    if (aLoadData.IsLinkRelPreload()) {
      SheetLoadData::PrioritizeAsPreload(channel);
      SheetLoadData::AddLoadBackgroundFlag(channel);
    }
  }

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
      if (aLoadData.mParentData) {
        timedChannel->SetInitiatorType(u"css"_ns);

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

      } else if (aEarlyHintPreloaderId) {
        timedChannel->SetInitiatorType(u"early-hints"_ns);
      } else {
        timedChannel->SetInitiatorType(u"link"_ns);
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
    mSheets->LoadStarted(key, aLoadData);
  }
  return NS_OK;
}

/**
 * ParseSheet handles parsing the data stream.
 */
Loader::Completed Loader::ParseSheet(const nsACString& aBytes,
                                     SheetLoadData& aLoadData,
                                     AllowAsyncParse aAllowAsync) {
  LOG(("css::Loader::ParseSheet"));
  if (aLoadData.mURI) {
    LOG_URI("  Load succeeded for URI: '%s', parsing", aLoadData.mURI);
  }
  AUTO_PROFILER_LABEL_CATEGORY_PAIR_RELEVANT_FOR_JS(LAYOUT_CSSParsing);

  ++mParsedSheetCount;

  aLoadData.mIsBeingParsed = true;

  StyleSheet* sheet = aLoadData.mSheet;
  MOZ_ASSERT(sheet);

  // Some cases, like inline style and UA stylesheets, need to be parsed
  // synchronously. The former may trigger child loads, the latter must not.
  if (aLoadData.mSyncLoad || aAllowAsync == AllowAsyncParse::No) {
    sheet->ParseSheetSync(this, aBytes, &aLoadData, aLoadData.mLineNumber);
    aLoadData.mIsBeingParsed = false;

    bool noPendingChildren = aLoadData.mPendingChildren == 0;
    MOZ_ASSERT_IF(aLoadData.mSyncLoad, noPendingChildren);
    if (noPendingChildren) {
      SheetComplete(aLoadData, NS_OK);
      return Completed::Yes;
    }
    return Completed::No;
  }

  // This parse does not need to be synchronous. \o/
  //
  // Note that load is already blocked from IncrementOngoingLoadCount(), and
  // will be unblocked from SheetFinishedParsingAsync which will end up in
  // NotifyObservers as needed.
  nsCOMPtr<nsISerialEventTarget> target = DispatchTarget();
  sheet->ParseSheet(*this, aBytes, aLoadData)
      ->Then(
          target, __func__,
          [loadData = RefPtr<SheetLoadData>(&aLoadData)](bool aDummy) {
            MOZ_ASSERT(NS_IsMainThread());
            loadData->SheetFinishedParsingAsync();
          },
          [] { MOZ_CRASH("rejected parse promise"); });
  return Completed::No;
}

void Loader::NotifyObservers(SheetLoadData& aData, nsresult aStatus) {
  RecordUseCountersIfNeeded(mDocument, aData.mSheet->GetStyleUseCounters());
  if (aData.mURI) {
    mLoadsPerformed.PutEntry(SheetLoadDataHashKey(aData));
    aData.NotifyStop(aStatus);
    // NOTE(emilio): This needs to happen before notifying observers, as
    // FontFaceSet for example checks for pending sheet loads from the
    // StyleSheetLoaded callback.
    if (aData.BlocksLoadEvent()) {
      DecrementOngoingLoadCount();
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
  }

  if (mPendingLoadCount && mPendingLoadCount == mOngoingLoadCount) {
    LOG(("  No more loading sheets; starting deferred loads"));
    StartDeferredLoads();
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

RefPtr<StyleSheet> Loader::LookupInlineSheetInCache(const nsAString& aBuffer) {
  auto result = mInlineSheets.Lookup(aBuffer);
  if (!result) {
    return nullptr;
  }
  if (result.Data()->HasModifiedRules()) {
    // Remove it now that we know that we're never going to use this stylesheet
    // again.
    result.Remove();
    return nullptr;
  }
  return result.Data()->Clone(nullptr, nullptr);
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
    const SheetInfo& aInfo, const nsAString& aBuffer, uint32_t aLineNumber,
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

  // We only cache sheets if in shadow trees, since regular document sheets are
  // likely to be unique.
  const bool isWorthCaching = aInfo.mContent->IsInShadowTree();
  RefPtr<StyleSheet> sheet;
  if (isWorthCaching) {
    sheet = LookupInlineSheetInCache(aBuffer);
  }
  const bool sheetFromCache = !!sheet;
  if (!sheet) {
    sheet = MakeRefPtr<StyleSheet>(eAuthorSheetFeatures, aInfo.mCORSMode,
                                   SRIMetadata{});
    sheet->SetURIs(sheetURI, originalURI, baseURI);
    nsIReferrerInfo* referrerInfo =
        aInfo.mContent->OwnerDoc()->ReferrerInfoForInternalCSSAndSVGResources();
    sheet->SetReferrerInfo(referrerInfo);

    nsIPrincipal* sheetPrincipal = principal;
    if (aInfo.mTriggeringPrincipal) {
      // The triggering principal may be an expanded principal, which is safe to
      // use for URL security checks, but not as the loader principal for a
      // stylesheet. So treat this as principal inheritance, and downgrade if
      // necessary.
      sheetPrincipal =
          BasePrincipal::Cast(aInfo.mTriggeringPrincipal)->PrincipalToInherit();
    }

    // We never actually load this, so just set its principal directly
    sheet->SetPrincipal(sheetPrincipal);
  }

  auto matched = PrepareSheet(*sheet, aInfo.mTitle, aInfo.mMedia, nullptr,
                              isAlternate, aInfo.mIsExplicitlyEnabled);

  if (auto* linkStyle = LinkStyle::FromNodeOrNull(aInfo.mContent)) {
    linkStyle->SetStyleSheet(sheet);
  }
  if (sheet->IsComplete()) {
    MOZ_ASSERT(sheet->GetOwnerNode() == aInfo.mContent);
    InsertSheetInTree(*sheet);
  }

  Completed completed;
  if (sheetFromCache) {
    MOZ_ASSERT(sheet->IsComplete());
    completed = Completed::Yes;
  } else {
    auto data = MakeRefPtr<SheetLoadData>(
        this, aInfo.mTitle, nullptr, sheet, false, aInfo.mContent, isAlternate,
        matched, StylePreloadKind::None, aObserver, principal,
        aInfo.mReferrerInfo);
    MOZ_ASSERT(data->GetRequestingNode() == aInfo.mContent);
    data->mLineNumber = aLineNumber;

    // Parse completion releases the load data.
    //
    // Note that we need to parse synchronously, since the web expects that the
    // effects of inline stylesheets are visible immediately (aside from
    // @imports).
    NS_ConvertUTF16toUTF8 utf8(aBuffer);
    completed = ParseSheet(utf8, *data, AllowAsyncParse::No);
    if (completed == Completed::Yes) {
      // TODO(emilio): Try to cache sheets with @import rules, maybe?
      if (isWorthCaching) {
        mInlineSheets.InsertOrUpdate(aBuffer, std::move(sheet));
      }
    } else {
      data->mMustNotify = true;
    }
  }

  return LoadSheetResult{completed, isAlternate, matched};
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
    const bool privilegedShadowTree = aInfo.mContent->IsInShadowTree() &&
                                      (aInfo.mContent->ChromeOnlyAccess() ||
                                       aInfo.mContent->IsInChromeDocument());
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
  auto [sheet, state] = CreateSheet(aInfo, eAuthorSheetFeatures, syncLoad,
                                    StylePreloadKind::None);

  LOG(("  Sheet is alternate: %d", static_cast<int>(isAlternate)));

  auto matched = PrepareSheet(*sheet, aInfo.mTitle, aInfo.mMedia, nullptr,
                              isAlternate, aInfo.mIsExplicitlyEnabled);

  if (auto* linkStyle = LinkStyle::FromNodeOrNull(aInfo.mContent)) {
    linkStyle->SetStyleSheet(sheet);
  }
  if (sheet->IsComplete()) {
    MOZ_ASSERT(sheet->GetOwnerNode() == aInfo.mContent);
    InsertSheetInTree(*sheet);
  }

  // We may get here with no content for Link: headers for example.
  MOZ_ASSERT(!aInfo.mContent || LinkStyle::FromNode(*aInfo.mContent),
             "If there is any node, it should be a LinkStyle");
  auto data = MakeRefPtr<SheetLoadData>(
      this, aInfo.mTitle, aInfo.mURI, sheet, syncLoad, aInfo.mContent,
      isAlternate, matched, StylePreloadKind::None, aObserver, principal,
      aInfo.mReferrerInfo);

  MOZ_ASSERT(data->GetRequestingNode() == requestingNode);

  MaybeNotifyPreloadUsed(*data);

  if (state == SheetState::Complete) {
    LOG(("  Sheet already complete: 0x%p", sheet.get()));
    if (aObserver || !mObservers.IsEmpty() || aInfo.mContent) {
      rv = PostLoadEvent(std::move(data));
      if (NS_FAILED(rv)) {
        return Err(rv);
      }
    } else {
      // We don't have to notify anyone of this load, as it was complete, so
      // drop it intentionally.
      data->mIntentionallyDropped = true;
    }

    // The load hasn't been completed yet, will be done in PostLoadEvent.
    return LoadSheetResult{Completed::No, isAlternate, matched};
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
                         u""_ns, StylePreloadKind::None);
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
  SheetState state;
  if (aReusableSheets && aReusableSheets->FindReusableStyleSheet(aURL, sheet)) {
    state = SheetState::Complete;
  } else {
    // For now, use CORS_NONE for child sheets
    std::tie(sheet, state) = CreateSheet(
        aURL, nullptr, principal, aParentSheet.ParsingMode(), CORS_NONE,
        aParentData ? aParentData->mEncoding : nullptr,
        u""_ns,  // integrity is only checked on main sheet
        aParentData && aParentData->mSyncLoad, StylePreloadKind::None);
    PrepareSheet(*sheet, u""_ns, u""_ns, aMedia, IsAlternate::No,
                 IsExplicitlyEnabled::No);
  }

  MOZ_ASSERT(sheet);
  InsertChildSheet(*sheet, aParentSheet);

  auto data =
      MakeRefPtr<SheetLoadData>(this, aURL, sheet, aParentData, observer,
                                principal, aParentSheet.GetReferrerInfo());
  MOZ_ASSERT(data->GetRequestingNode() == requestingNode);

  MaybeNotifyPreloadUsed(*data);

  if (state == SheetState::Complete) {
    LOG(("  Sheet already complete"));
    // We're completely done.  No need to notify, even, since the
    // @import rule addition/modification will trigger the right style
    // changes automatically.
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
      referrerInfo, nullptr, CORS_NONE, u""_ns, 0);
}

Result<RefPtr<StyleSheet>, nsresult> Loader::LoadSheet(
    nsIURI* aURI, SheetParsingMode aParsingMode,
    UseSystemPrincipal aUseSystemPrincipal, nsICSSLoaderObserver* aObserver) {
  nsCOMPtr<nsIReferrerInfo> referrerInfo = new ReferrerInfo(nullptr);
  return InternalLoadNonDocumentSheet(
      aURI, StylePreloadKind::None, aParsingMode, aUseSystemPrincipal, nullptr,
      referrerInfo, aObserver, CORS_NONE, u""_ns, 0);
}

Result<RefPtr<StyleSheet>, nsresult> Loader::LoadSheet(
    nsIURI* aURL, StylePreloadKind aPreloadKind,
    const Encoding* aPreloadEncoding, nsIReferrerInfo* aReferrerInfo,
    nsICSSLoaderObserver* aObserver, uint64_t aEarlyHintPreloaderId,
    CORSMode aCORSMode, const nsAString& aIntegrity) {
  LOG(("css::Loader::LoadSheet(aURL, aObserver) api call"));
  return InternalLoadNonDocumentSheet(aURL, aPreloadKind, eAuthorSheetFeatures,
                                      UseSystemPrincipal::No, aPreloadEncoding,
                                      aReferrerInfo, aObserver, aCORSMode,
                                      aIntegrity, aEarlyHintPreloaderId);
}

Result<RefPtr<StyleSheet>, nsresult> Loader::InternalLoadNonDocumentSheet(
    nsIURI* aURL, StylePreloadKind aPreloadKind, SheetParsingMode aParsingMode,
    UseSystemPrincipal aUseSystemPrincipal, const Encoding* aPreloadEncoding,
    nsIReferrerInfo* aReferrerInfo, nsICSSLoaderObserver* aObserver,
    CORSMode aCORSMode, const nsAString& aIntegrity,
    uint64_t aEarlyHintPreloaderId) {
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
                                   mDocument, u""_ns, aPreloadKind);
  if (NS_FAILED(rv)) {
    return Err(rv);
  }

  bool syncLoad = !aObserver;
  auto [sheet, state] =
      CreateSheet(aURL, nullptr, triggeringPrincipal, aParsingMode, aCORSMode,
                  aPreloadEncoding, aIntegrity, syncLoad, aPreloadKind);

  PrepareSheet(*sheet, u""_ns, u""_ns, nullptr, IsAlternate::No,
               IsExplicitlyEnabled::No);

  auto data = MakeRefPtr<SheetLoadData>(
      this, aURL, sheet, syncLoad, aUseSystemPrincipal, aPreloadKind,
      aPreloadEncoding, aObserver, triggeringPrincipal, aReferrerInfo);
  MOZ_ASSERT(data->GetRequestingNode() == mDocument);
  if (state == SheetState::Complete) {
    LOG(("  Sheet already complete"));
    if (aObserver || !mObservers.IsEmpty()) {
      rv = PostLoadEvent(std::move(data));
      if (NS_FAILED(rv)) {
        return Err(rv);
      }
    } else {
      // We don't have to notify anyone of this load, as it was complete, so
      // drop it intentionally.
      data->mIntentionallyDropped = true;
    }
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

nsresult Loader::PostLoadEvent(RefPtr<SheetLoadData> aLoadData) {
  LOG(("css::Loader::PostLoadEvent"));
  mPostedEvents.AppendElement(aLoadData);

  nsresult rv;
  RefPtr<SheetLoadData> runnable(aLoadData);
  if (mDocument) {
    rv = mDocument->Dispatch(TaskCategory::Other, runnable.forget());
  } else if (mDocGroup) {
    rv = mDocGroup->Dispatch(TaskCategory::Other, runnable.forget());
  } else {
    rv = SchedulerGroup::Dispatch(TaskCategory::Other, runnable.forget());
  }

  if (NS_FAILED(rv)) {
    NS_WARNING("failed to dispatch stylesheet load event");
    mPostedEvents.RemoveElement(aLoadData);
  } else {
    if (aLoadData->BlocksLoadEvent()) {
      IncrementOngoingLoadCount();
    }

    // We want to notify the observer for this data.
    aLoadData->mMustNotify = true;
    aLoadData->mSheetAlreadyComplete = true;

    // If we get to this code, aSheet loaded correctly at some point, so
    // we can just schedule a load event and don't need to touch the
    // data's mLoadFailed.  Note that we do this here and not from
    // inside our SheetComplete so that we don't end up running the load
    // event async.
    MOZ_ASSERT(!aLoadData->mLoadFailed, "Why are we marked as failed?");
    aLoadData->ScheduleLoadEventIfNeeded();
  }

  return rv;
}

void Loader::HandleLoadEvent(SheetLoadData& aEvent) {
  // XXXbz can't assert this yet.... May not have an observer because
  // we're unblocking the parser
  // NS_ASSERTION(aEvent->mObserver, "Must have observer");
  NS_ASSERTION(aEvent.mSheet, "Must have sheet");

  mPostedEvents.RemoveElement(&aEvent);
  SheetComplete(aEvent, NS_OK);
}

void Loader::Stop() {
  if (mSheets) {
    mSheets->CancelLoadsForLoader(*this);
  }

  auto arr = std::move(mPostedEvents);
  for (auto& data : arr) {
    data->Cancel();
  }
}

bool Loader::HasPendingNonDeferredLoads() const {
  MOZ_ASSERT(mOngoingLoadCount >= mPendingLoadCount);
  return mOngoingLoadCount - mPendingLoadCount > 0;
}

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
  for (const auto& data : tmp->mInlineSheets.Values()) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "Inline sheet cache in Loader");
    cb.NoteXPCOMChild(data);
  }
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
  tmp->mInlineSheets.Clear();
  tmp->mObservers.Clear();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDocGroup)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

size_t Loader::SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const {
  size_t n = aMallocSizeOf(this);

  n += mObservers.ShallowSizeOfExcludingThis(aMallocSizeOf);

  n += mInlineSheets.ShallowSizeOfExcludingThis(aMallocSizeOf);
  for (const auto& entry : mInlineSheets) {
    n += entry.GetKey().SizeOfExcludingThisIfUnshared(aMallocSizeOf);
    // If the sheet has a parent, then its parent will report it so we don't
    // have to worry about it here.
    const StyleSheet* sheet = entry.GetWeak();
    MOZ_ASSERT(!sheet->GetParentSheet(),
               "How did an @import rule end up here?");
    if (!sheet->GetOwnerNode()) {
      n += sheet->SizeOfIncludingThis(aMallocSizeOf);
    }
  }

  // Measurement of the following members may be added later if DMD finds it is
  // worthwhile:
  // - mPostedEvents: transient, and should be small
  //
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
  if (!mDocument) {
    return false;
  }
  RefPtr<nsILoadGroup> lg = mDocument->GetDocumentLoadGroup();
  if (!lg) {
    return false;
  }
  nsLoadFlags flags;
  if (NS_FAILED(lg->GetLoadFlags(&flags))) {
    return false;
  }
  return flags & (nsIRequest::LOAD_BYPASS_CACHE |
                  nsICachingChannel::LOAD_BYPASS_LOCAL_CACHE);
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

already_AddRefed<nsISerialEventTarget> Loader::DispatchTarget() {
  nsCOMPtr<nsISerialEventTarget> target;
  if (mDocument) {
    // If you change this, you may want to change StyleSheet::Replace
    target = mDocument->EventTargetFor(TaskCategory::Other);
  } else if (mDocGroup) {
    target = mDocGroup->EventTargetFor(TaskCategory::Other);
  } else {
    target = GetMainThreadSerialEventTarget();
  }

  return target.forget();
}

}  // namespace css
}  // namespace mozilla
