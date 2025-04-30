/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* loading of CSS style sheets using the network APIs */

#ifndef mozilla_css_Loader_h
#define mozilla_css_Loader_h

#include <tuple>
#include <utility>

#include "mozilla/Attributes.h"
#include "mozilla/CORSMode.h"
#include "mozilla/css/StylePreloadKind.h"
#include "mozilla/dom/LinkStyle.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/SharedSubResourceCache.h"
#include "mozilla/UniquePtr.h"
#include "nsCompatibility.h"
#include "nsCycleCollectionParticipant.h"
#include "nsStringFwd.h"
#include "nsTArray.h"
#include "nsTObserverArray.h"
#include "nsURIHashKey.h"
#include "nsRefPtrHashtable.h"

class nsICSSLoaderObserver;
class nsIConsoleReportCollector;
class nsIContent;
class nsIPrincipal;

namespace mozilla {

class PreloadHashKey;
class SharedStyleSheetCache;
class SheetLoadDataHashKey;
class StyleSheet;

namespace dom {
class DocGroup;
class Element;
enum class FetchPriority : uint8_t;
}  // namespace dom

// The load data for a <link> or @import style-sheet.
//
// This must contain all the state that affects CSS parsing.
class SheetLoadDataHashKey : public PLDHashEntryHdr {
 public:
  using KeyType = const SheetLoadDataHashKey&;
  using KeyTypePointer = const SheetLoadDataHashKey*;

  explicit SheetLoadDataHashKey(const SheetLoadDataHashKey* aKey)
      : mURI(aKey->mURI),
        mLoaderPrincipal(aKey->mLoaderPrincipal),
        mPartitionPrincipal(aKey->mPartitionPrincipal),
        mEncodingGuess(aKey->mEncodingGuess),
        mCORSMode(aKey->mCORSMode),
        mParsingMode(aKey->mParsingMode),
        mCompatMode(aKey->mCompatMode),
        mSRIMetadata(aKey->mSRIMetadata),
        mIsLinkRelPreloadOrEarlyHint(aKey->mIsLinkRelPreloadOrEarlyHint) {
    MOZ_COUNT_CTOR(SheetLoadDataHashKey);
  }

  SheetLoadDataHashKey(nsIURI* aURI, nsIPrincipal* aLoaderPrincipal,
                       nsIPrincipal* aPartitionPrincipal,
                       NotNull<const Encoding*> aEncodingGuess,
                       CORSMode aCORSMode, css::SheetParsingMode aParsingMode,
                       nsCompatibility aCompatMode,
                       const dom::SRIMetadata& aSRIMetadata,
                       css::StylePreloadKind aPreloadKind)
      : mURI(aURI),
        mLoaderPrincipal(aLoaderPrincipal),
        mPartitionPrincipal(aPartitionPrincipal),
        mEncodingGuess(aEncodingGuess),
        mCORSMode(aCORSMode),
        mParsingMode(aParsingMode),
        mCompatMode(aCompatMode),
        mSRIMetadata(aSRIMetadata),
        mIsLinkRelPreloadOrEarlyHint(
            css::IsLinkRelPreloadOrEarlyHint(aPreloadKind)) {
    MOZ_ASSERT(aURI);
    MOZ_ASSERT(aLoaderPrincipal);
    MOZ_COUNT_CTOR(SheetLoadDataHashKey);
  }

  SheetLoadDataHashKey(SheetLoadDataHashKey&& toMove)
      : mURI(std::move(toMove.mURI)),
        mLoaderPrincipal(std::move(toMove.mLoaderPrincipal)),
        mPartitionPrincipal(std::move(toMove.mPartitionPrincipal)),
        mEncodingGuess(std::move(toMove.mEncodingGuess)),
        mCORSMode(std::move(toMove.mCORSMode)),
        mParsingMode(std::move(toMove.mParsingMode)),
        mCompatMode(std::move(toMove.mCompatMode)),
        mSRIMetadata(std::move(toMove.mSRIMetadata)),
        mIsLinkRelPreloadOrEarlyHint(
            std::move(toMove.mIsLinkRelPreloadOrEarlyHint)) {
    MOZ_COUNT_CTOR(SheetLoadDataHashKey);
  }

  explicit SheetLoadDataHashKey(const css::SheetLoadData&);

  MOZ_COUNTED_DTOR(SheetLoadDataHashKey)

  const SheetLoadDataHashKey& GetKey() const { return *this; }
  const SheetLoadDataHashKey* GetKeyPointer() const { return this; }

  bool KeyEquals(const SheetLoadDataHashKey* aKey) const {
    return KeyEquals(*aKey);
  }

  bool KeyEquals(const SheetLoadDataHashKey&) const;

  static const SheetLoadDataHashKey* KeyToPointer(
      const SheetLoadDataHashKey& aKey) {
    return &aKey;
  }
  static PLDHashNumber HashKey(const SheetLoadDataHashKey* aKey) {
    return nsURIHashKey::HashKey(aKey->mURI);
  }

  nsIURI* URI() const { return mURI; }

  nsIPrincipal* LoaderPrincipal() const { return mLoaderPrincipal; }
  nsIPrincipal* PartitionPrincipal() const { return mPartitionPrincipal; }

  css::SheetParsingMode ParsingMode() const { return mParsingMode; }

  enum { ALLOW_MEMMOVE = true };

 protected:
  const nsCOMPtr<nsIURI> mURI;
  const nsCOMPtr<nsIPrincipal> mLoaderPrincipal;
  const nsCOMPtr<nsIPrincipal> mPartitionPrincipal;
  // The encoding guess is the encoding the sheet would get if the request
  // didn't have any encoding information like @charset or a Content-Encoding
  // header.
  const NotNull<const Encoding*> mEncodingGuess;
  const CORSMode mCORSMode;
  const css::SheetParsingMode mParsingMode;
  const nsCompatibility mCompatMode;
  dom::SRIMetadata mSRIMetadata;
  const bool mIsLinkRelPreloadOrEarlyHint;
};

namespace css {

class SheetLoadData;
class ImportRule;

/*********************
 * Style sheet reuse *
 *********************/

class MOZ_RAII LoaderReusableStyleSheets {
 public:
  LoaderReusableStyleSheets() = default;

  /**
   * Look for a reusable sheet (see AddReusableSheet) matching the
   * given URL.  If found, set aResult, remove the reused sheet from
   * the internal list, and return true.  If not found, return false;
   * in this case, aResult is not modified.
   *
   * @param aURL the url to match
   * @param aResult [out] the style sheet which can be reused
   */
  bool FindReusableStyleSheet(nsIURI* aURL, RefPtr<StyleSheet>& aResult);

  /**
   * Indicate that a certain style sheet is available for reuse if its
   * URI matches the URI of an @import.  Sheets should be added in the
   * opposite order in which they are intended to be reused.
   *
   * @param aSheet the sheet which can be reused
   */
  void AddReusableSheet(StyleSheet* aSheet) {
    mReusableSheets.AppendElement(aSheet);
  }

 private:
  LoaderReusableStyleSheets(const LoaderReusableStyleSheets&) = delete;
  LoaderReusableStyleSheets& operator=(const LoaderReusableStyleSheets&) =
      delete;

  // The sheets that can be reused.
  nsTArray<RefPtr<StyleSheet>> mReusableSheets;
};

class Loader final {
  using ReferrerPolicy = dom::ReferrerPolicy;

 public:
  using Completed = dom::LinkStyle::Completed;
  using HasAlternateRel = dom::LinkStyle::HasAlternateRel;
  using IsAlternate = dom::LinkStyle::IsAlternate;
  using IsInline = dom::LinkStyle::IsInline;
  using IsExplicitlyEnabled = dom::LinkStyle::IsExplicitlyEnabled;
  using MediaMatched = dom::LinkStyle::MediaMatched;
  using LoadSheetResult = dom::LinkStyle::Update;
  using SheetInfo = dom::LinkStyle::SheetInfo;

  Loader();
  // aDocGroup is used for dispatching SheetLoadData in PostLoadEvent(). It
  // can be null if you want to use this constructor, and there's no
  // document when the Loader is constructed.
  explicit Loader(dom::DocGroup*);
  explicit Loader(dom::Document*);

 private:
  // Private destructor, to discourage deletion outside of Release():
  ~Loader();

 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(Loader)
  NS_DECL_CYCLE_COLLECTION_NATIVE_CLASS(Loader)

  void DropDocumentReference();  // notification that doc is going away

  void DeregisterFromSheetCache();
  void RegisterInSheetCache();

  void SetCompatibilityMode(nsCompatibility aCompatMode) {
    mDocumentCompatMode = aCompatMode;
  }

  using StylePreloadKind = css::StylePreloadKind;

  bool HasLoaded(const SheetLoadDataHashKey& aKey) const {
    return mLoadsPerformed.Contains(aKey);
  }

  void WillStartPendingLoad() {
    MOZ_DIAGNOSTIC_ASSERT(mPendingLoadCount, "Where did this load come from?");
    mPendingLoadCount--;
  }

  nsCompatibility CompatMode(StylePreloadKind aPreloadKind) const {
    if (css::ShouldAssumeStandardsMode(aPreloadKind)) {
      return eCompatibility_FullStandards;
    }
    return mDocumentCompatMode;
  }

  // TODO(emilio): Is the complexity of this method and carrying the titles
  // around worth it? The alternate sheets will load anyhow eventually...
  void DocumentStyleSheetSetChanged();

  // XXXbz sort out what the deal is with events!  When should they fire?

  /**
   * Load an inline style sheet.  If a successful result is returned and
   * result.WillNotify() is true, then aObserver is guaranteed to be notified
   * asynchronously once the sheet is marked complete.  If an error is
   * returned, or if result.WillNotify() is false, aObserver will not be
   * notified.  In addition to parsing the sheet, this method will insert it
   * into the stylesheet list of this CSSLoader's document.
   * @param aObserver the observer to notify when the load completes.
   *        May be null.
   * @param aBuffer the stylesheet data
   */
  Result<LoadSheetResult, nsresult> LoadInlineStyle(
      const SheetInfo&, const nsAString& aBuffer,
      nsICSSLoaderObserver* aObserver);

  /**
   * Load a linked (document) stylesheet.  If a successful result is returned,
   * aObserver is guaranteed to be notified asynchronously once the sheet is
   * loaded and marked complete, i.e., result.WillNotify() will always return
   * true.  If an error is returned, aObserver will not be notified.  In
   * addition to loading the sheet, this method will insert it into the
   * stylesheet list of this CSSLoader's document.
   * @param aObserver the observer to notify when the load completes.
   *                  May be null.
   */
  Result<LoadSheetResult, nsresult> LoadStyleLink(
      const SheetInfo&, nsICSSLoaderObserver* aObserver);

  /**
   * Load a child (@import-ed) style sheet.  In addition to loading the sheet,
   * this method will insert it into the child sheet list of aParentSheet.  If
   * there is no sheet currently being parsed and the child sheet is not
   * complete when this method returns, then when the child sheet becomes
   * complete aParentSheet will be QIed to nsICSSLoaderObserver and
   * asynchronously notified, just like for LoadStyleLink.  Note that if the
   * child sheet is already complete when this method returns, no
   * nsICSSLoaderObserver notification will be sent.
   *
   * @param aParentSheet the parent of this child sheet
   * @param aParentData the SheetLoadData corresponding to the load of the
   *                    parent sheet. May be null for @import rules inserted via
   *                    CSSOM.
   * @param aURL the URL of the child sheet
   * @param aMedia the already-parsed media list for the child sheet
   * @param aSavedSheets any saved style sheets which could be reused
   *              for this load
   */
  nsresult LoadChildSheet(StyleSheet& aParentSheet, SheetLoadData* aParentData,
                          nsIURI* aURL, dom::MediaList* aMedia,
                          LoaderReusableStyleSheets* aSavedSheets);

  /**
   * Called when we hit the internal memory cache with a complete stylesheet.
   */
  void DidHitCompleteSheetCache(const SheetLoadDataHashKey&,
                                const StyleUseCounters* aCounters);

  enum class UseSystemPrincipal { No, Yes };

  /**
   * Synchronously load and return the stylesheet at aURL.  Any child sheets
   * will also be loaded synchronously.  Note that synchronous loads over some
   * protocols may involve spinning up a new event loop, so use of this method
   * does NOT guarantee not receiving any events before the sheet loads.  This
   * method can be used to load sheets not associated with a document.
   *
   * @param aURL the URL of the sheet to load
   * @param aParsingMode the mode in which to parse the sheet
   *        (see comments at enum SheetParsingMode, above).
   * @param aUseSystemPrincipal if true, give the resulting sheet the system
   * principal no matter where it's being loaded from.
   *
   * NOTE: At the moment, this method assumes the sheet will be UTF-8, but
   * ideally it would allow arbitrary encodings.  Callers should NOT depend on
   * non-UTF8 sheets being treated as UTF-8 by this method.
   *
   * NOTE: A successful return from this method doesn't indicate anything about
   * whether the data could be parsed as CSS and doesn't indicate anything
   * about the status of child sheets of the returned sheet.
   */
  Result<RefPtr<StyleSheet>, nsresult> LoadSheetSync(
      nsIURI*, SheetParsingMode = eAuthorSheetFeatures,
      UseSystemPrincipal = UseSystemPrincipal::No);

  /**
   * Asynchronously load the stylesheet at aURL.  If a successful result is
   * returned, aObserver is guaranteed to be notified asynchronously once the
   * sheet is loaded and marked complete.  This method can be used to load
   * sheets not associated with a document.
   *
   * @param aURL the URL of the sheet to load
   * @param aParsingMode the mode in which to parse the sheet
   *        (see comments at enum SheetParsingMode, above).
   * @param aUseSystemPrincipal if true, give the resulting sheet the system
   * principal no matter where it's being loaded from.
   * @param aReferrerInfo referrer information of the sheet.
   * @param aObserver the observer to notify when the load completes.
   *                  Must not be null.
   * @param aEarlyHintPreloaderId to connect back to the early hint preload
   * channel. Null means no connect back should happen
   * @return the sheet to load. Note that the sheet may well not be loaded by
   * the time this method returns.
   *
   * NOTE: At the moment, this method assumes the sheet will be UTF-8, but
   * ideally it would allow arbitrary encodings.  Callers should NOT depend on
   * non-UTF8 sheets being treated as UTF-8 by this method.
   */
  Result<RefPtr<StyleSheet>, nsresult> LoadSheet(
      nsIURI* aURI, StylePreloadKind, const Encoding* aPreloadEncoding,
      nsIReferrerInfo* aReferrerInfo, nsICSSLoaderObserver* aObserver,
      uint64_t aEarlyHintPreloaderId, CORSMode aCORSMode,
      const nsAString& aNonce, const nsAString& aIntegrity,
      dom::FetchPriority aFetchPriority);

  /**
   * As above, but without caring for a couple things.
   * Only to be called by `PreloadedStyleSheet::PreloadAsync`.
   */
  Result<RefPtr<StyleSheet>, nsresult> LoadSheet(nsIURI*, SheetParsingMode,
                                                 UseSystemPrincipal,
                                                 nsICSSLoaderObserver*);

  /**
   * Stop loading all sheets.  All nsICSSLoaderObservers involved will be
   * notified with NS_BINDING_ABORTED as the status, possibly synchronously.
   */
  void Stop();

  /**
   * nsresult Loader::StopLoadingSheet(nsIURI* aURL), which notifies the
   * nsICSSLoaderObserver with NS_BINDING_ABORTED, was removed in Bug 556446.
   * It can be found in revision 2c44a32052ad.
   */

  /**
   * Whether the loader is enabled or not.
   * When disabled, processing of new styles is disabled and an attempt
   * to do so will fail with a return code of
   * NS_ERROR_NOT_AVAILABLE. Note that this DOES NOT disable
   * currently loading styles or already processed styles.
   */
  bool GetEnabled() { return mEnabled; }
  void SetEnabled(bool aEnabled) { mEnabled = aEnabled; }

  uint32_t ParsedSheetCount() const { return mParsedSheetCount; }

  /**
   * Get the document we live for. May return null.
   */
  dom::Document* GetDocument() const { return mDocument; }

  bool IsDocumentAssociated() const { return mIsDocumentAssociated; }

  /**
   * Return true if this loader has pending loads (ones that would send
   * notifications to an nsICSSLoaderObserver attached to this loader).
   * If called from inside nsICSSLoaderObserver::StyleSheetLoaded, this will
   * return false if and only if that is the last StyleSheetLoaded
   * notification the CSSLoader knows it's going to send.  In other words, if
   * two sheets load at once (via load coalescing, e.g.), HasPendingLoads()
   * will return true during notification for the first one, and false
   * during notification for the second one.
   */
  bool HasPendingLoads();

  /**
   * Add an observer to this loader.  The observer will be notified
   * for all loads that would have notified their own observers (even
   * if those loads don't have observers attached to them).
   * Load-specific observers will be notified before generic
   * observers.  The loader holds a reference to the observer.
   *
   * aObserver must not be null.
   */
  void AddObserver(nsICSSLoaderObserver* aObserver);

  /**
   * Remove an observer added via AddObserver.
   */
  void RemoveObserver(nsICSSLoaderObserver* aObserver);

  // These interfaces are public only for the benefit of static functions
  // within nsCSSLoader.cpp.

  // IsAlternateSheet can change our currently selected style set if none is
  // selected and aHasAlternateRel is false.
  IsAlternate IsAlternateSheet(const nsAString& aTitle, bool aHasAlternateRel);

  // Measure our size.
  size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const;

  enum class SheetState : uint8_t {
    NeedsParser = 0,
    Pending,
    Loading,
    Complete
  };

  // The loader principal is the document's node principal, if this loader is
  // owned by a document, or the system principal otherwise.
  nsIPrincipal* LoaderPrincipal() const;

  // The partitioned principal is the document's partitioned principal, if this
  // loader is owned by a document, or the system principal otherwise.
  nsIPrincipal* PartitionedPrincipal() const;

  bool ShouldBypassCache() const;

  enum class PendingLoad { No, Yes };

 private:
  friend class mozilla::SharedStyleSheetCache;
  friend class SheetLoadData;
  friend class StreamLoader;

  enum class UsePreload : bool { No, Yes };
  enum class UseLoadGroup : bool { No, Yes };

  nsresult NewStyleSheetChannel(SheetLoadData& aLoadData, CORSMode aCorsMode,
                                UsePreload aUsePreload,
                                UseLoadGroup aUseLoadGroup,
                                nsIChannel** aOutChannel);

  // Only to be called by `LoadSheet`.
  [[nodiscard]] bool MaybeDeferLoad(SheetLoadData& aLoadData,
                                    SheetState aSheetState,
                                    PendingLoad aPendingLoad,
                                    const SheetLoadDataHashKey& aKey);

  // Only to be called by `LoadSheet`.
  bool MaybeCoalesceLoadAndNotifyOpen(SheetLoadData& aLoadData,
                                      SheetState aSheetState,
                                      const SheetLoadDataHashKey& aKey,
                                      const PreloadHashKey& aPreloadKey);

  // Only to be called by `LoadSheet`.
  [[nodiscard]] nsresult LoadSheetSyncInternal(SheetLoadData& aLoadData,
                                               SheetState aSheetState);

  void AdjustPriority(const SheetLoadData& aLoadData, nsIChannel* aChannel);

  // Only to be called by `LoadSheet`.
  [[nodiscard]] nsresult LoadSheetAsyncInternal(
      SheetLoadData& aLoadData, uint64_t aEarlyHintPreloaderId,
      const SheetLoadDataHashKey& aKey);

  // Helpers to conditionally block onload if mDocument is non-null.
  void IncrementOngoingLoadCountAndMaybeBlockOnload() {
    if (!mOngoingLoadCount++) {
      BlockOnload();
    }
  }

  void DecrementOngoingLoadCountAndMaybeUnblockOnload() {
    MOZ_DIAGNOSTIC_ASSERT(mOngoingLoadCount);
    MOZ_DIAGNOSTIC_ASSERT(mOngoingLoadCount > mPendingLoadCount);
    if (!--mOngoingLoadCount) {
      UnblockOnload(false);
    }
  }

  void BlockOnload();
  void UnblockOnload(bool aFireSync);

  nsresult CheckContentPolicy(nsIPrincipal* aLoadingPrincipal,
                              nsIPrincipal* aTriggeringPrincipal,
                              nsIURI* aTargetURI, nsINode* aRequestingNode,
                              const nsAString& aNonce, StylePreloadKind);

  bool MaybePutIntoLoadsPerformed(SheetLoadData& aLoadData);

 private:
  std::tuple<RefPtr<StyleSheet>, SheetState,
             RefPtr<SubResourceNetworkMetadataHolder>>
  CreateSheet(const SheetInfo& aInfo, css::SheetParsingMode aParsingMode,
              bool aSyncLoad, css::StylePreloadKind aPreloadKind) {
    nsIPrincipal* triggeringPrincipal = aInfo.mTriggeringPrincipal
                                            ? aInfo.mTriggeringPrincipal.get()
                                            : LoaderPrincipal();
    return CreateSheet(aInfo.mURI, aInfo.mContent, triggeringPrincipal,
                       aParsingMode, aInfo.mCORSMode,
                       /* aPreloadOrParentDataEncoding = */ nullptr,
                       aInfo.mIntegrity, aSyncLoad, aPreloadKind);
  }

  // For inline style, the aURI param is null, but the aLinkingContent
  // must be non-null then.  The loader principal must never be null
  // if aURI is not null.
  std::tuple<RefPtr<StyleSheet>, SheetState,
             RefPtr<SubResourceNetworkMetadataHolder>>
  CreateSheet(nsIURI* aURI, nsIContent* aLinkingContent,
              nsIPrincipal* aTriggeringPrincipal, css::SheetParsingMode,
              CORSMode, const Encoding* aPreloadOrParentDataEncoding,
              const nsAString& aIntegrity, bool aSyncLoad, StylePreloadKind);

  // Pass in either a media string or the MediaList from the CSSParser.  Don't
  // pass both.
  //
  // This method will set the sheet's enabled state based on IsAlternate and co.
  MediaMatched PrepareSheet(StyleSheet&, const nsAString& aTitle,
                            const nsAString& aMediaString, dom::MediaList*,
                            IsAlternate, IsExplicitlyEnabled);

  // Inserts a style sheet in a document or a ShadowRoot.
  void InsertSheetInTree(StyleSheet& aSheet);
  // Inserts a style sheet into a parent style sheet.
  void InsertChildSheet(StyleSheet& aSheet, StyleSheet& aParentSheet);

  Result<RefPtr<StyleSheet>, nsresult> InternalLoadNonDocumentSheet(
      nsIURI* aURL, StylePreloadKind, SheetParsingMode aParsingMode,
      UseSystemPrincipal, const Encoding* aPreloadEncoding,
      nsIReferrerInfo* aReferrerInfo, nsICSSLoaderObserver* aObserver,
      CORSMode aCORSMode, const nsAString& aNonce, const nsAString& aIntegrity,
      uint64_t aEarlyHintPreloaderId, dom::FetchPriority aFetchPriority);

  RefPtr<StyleSheet> LookupInlineSheetInCache(const nsAString&, nsIPrincipal*);

  // Synchronously notify of a cached load data.
  void NotifyOfCachedLoad(RefPtr<SheetLoadData>);

  // Notify observers of a cached stylesheet being.
  void NotifyObserversForCachedSheet(SheetLoadData&);

  // Add the Performance API's Resource Timing entry for the cached load data.
  void AddPerformanceEntryForCachedSheet(SheetLoadData&);

  // Start the loads of all the sheets in mPendingDatas
  void StartDeferredLoads();

  // Note: LoadSheet is responsible for setting the sheet to complete on
  // failure.
  nsresult LoadSheet(SheetLoadData&, SheetState, uint64_t aEarlyHintPreloaderId,
                     PendingLoad = PendingLoad::No);

  enum class AllowAsyncParse {
    Yes,
    No,
  };

  // Parse the stylesheet in the load data.
  //
  // Returns whether the parse finished. It may not finish e.g. if the sheet had
  // an @import.
  //
  // If this function returns Completed::Yes, then ParseSheet also called
  // SheetComplete on aLoadData.
  Completed ParseSheet(const nsACString&, const RefPtr<SheetLoadDataHolder>&,
                       AllowAsyncParse);

  // The load of the sheet in the load data is done, one way or another.
  // Do final cleanup.
  void SheetComplete(SheetLoadData&, nsresult);

  // Notify observers on an individual data. This is different from
  // SheetComplete for loads that are shared.
  void NotifyObservers(SheetLoadData&, nsresult);

  // Mark the given SheetLoadData, as well as any of its siblings, parents, etc
  // transitively, as failed.  The idea is to mark as failed any load that was
  // directly or indirectly @importing the sheet this SheetLoadData represents.
  //
  // if aOnlyForLoader is non-null, then only loads for a given loader will be
  // marked as failing. This is useful to only cancel loads associated to a
  // given loader, in case they were marked as canceled.
  static void MarkLoadTreeFailed(SheetLoadData&,
                                 Loader* aOnlyForLoader = nullptr);

  // A shorthand to mark a possible link preload as used to supress "unused"
  // warning in the console.
  void MaybeNotifyPreloadUsed(SheetLoadData&);

  // A set with all the different loads we've done in a given document, for the
  // purpose of not posting duplicate performance entries for them.
  nsTHashtable<const SheetLoadDataHashKey> mLoadsPerformed;

  RefPtr<SharedStyleSheetCache> mSheets;

  // Our array of "global" observers
  nsTObserverArray<nsCOMPtr<nsICSSLoaderObserver>> mObservers;

  // This reference is nulled by the Document in it's destructor through
  // DropDocumentReference().
  dom::Document* MOZ_NON_OWNING_REF mDocument;  // the document we live for

  // For dispatching events via DocGroup::Dispatch() when mDocument is nullptr.
  RefPtr<dom::DocGroup> mDocGroup;

  nsCompatibility mDocumentCompatMode;

  const nsCOMPtr<nsIConsoleReportCollector> mReporter;

  // Number of datas for asynchronous sheet loads still waiting to be notified.
  // This includes pending stylesheets whose load hasn't started yet but which
  // we need to, but not inline or constructable stylesheets, though the
  // constructable stylesheets bit may change, see bug 1642227.
  uint32_t mOngoingLoadCount = 0;

  // The number of sheets that have been deferred / are in a pending state.
  uint32_t mPendingLoadCount = 0;

  // The number of stylesheets that we have parsed, for testing purposes.
  Atomic<uint32_t, MemoryOrdering::Relaxed> mParsedSheetCount{0};

  bool mEnabled = true;

  // Whether we had a document at the point of creation.
  bool mIsDocumentAssociated = false;

#ifdef DEBUG
  // Whether we're in a necko callback atm.
  bool mSyncCallback = false;
#endif
};

}  // namespace css
}  // namespace mozilla

#endif /* mozilla_css_Loader_h */
