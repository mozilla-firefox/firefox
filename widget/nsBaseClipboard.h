/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsBaseClipboard_h_
#define nsBaseClipboard_h_

#include "mozilla/Array.h"
#include "mozilla/Logging.h"
#include "mozilla/MoveOnlyFunction.h"
#include "mozilla/Result.h"
#include "mozilla/dom/PContent.h"
#include "nsCOMPtr.h"
#include "nsIClipboard.h"
#include "nsITransferable.h"

extern mozilla::LazyLogModule gWidgetClipboardLog;
#define MOZ_CLIPBOARD_LOG(...) \
  MOZ_LOG(gWidgetClipboardLog, mozilla::LogLevel::Debug, (__VA_ARGS__))
#define MOZ_CLIPBOARD_LOG_ENABLED() \
  MOZ_LOG_TEST(gWidgetClipboardLog, mozilla::LogLevel::Debug)

class nsITransferable;
class nsIClipboardOwner;
class nsIPrincipal;
class nsIWidget;

namespace mozilla::dom {
class WindowContext;
}  // namespace mozilla::dom

#define NS_BASECLIPBOARD_IID \
  {0x9b45fabf, 0x4072, 0x45b5, {0xaf, 0xb4, 0x8c, 0x68, 0x62, 0x07, 0x0e, 0xe9}}

/**
 * A base clipboard class for all platform, so that they can share the same
 * implementation.
 */
class nsBaseClipboard : public nsIClipboard {
 public:
  NS_INLINE_DECL_STATIC_IID(NS_BASECLIPBOARD_IID)

  explicit nsBaseClipboard(
      const mozilla::dom::ClipboardCapabilities& aClipboardCaps);

  // nsISupports
  NS_DECL_ISUPPORTS

  // nsIClipboard
  NS_IMETHOD SetData(
      nsITransferable* aTransferable, nsIClipboardOwner* aOwner,
      ClipboardType aWhichClipboard,
      mozilla::dom::WindowContext* aWindowContext) override final;
  NS_IMETHOD AsyncSetData(ClipboardType aWhichClipboard,
                          mozilla::dom::WindowContext* aSettingWindowContext,
                          nsIAsyncClipboardRequestCallback* aCallback,
                          nsIAsyncSetClipboardData** _retval) override final;
  NS_IMETHOD GetData(
      nsITransferable* aTransferable, ClipboardType aWhichClipboard,
      mozilla::dom::WindowContext* aWindowContext) override final;
  NS_IMETHOD GetDataIfSmallerThan(
      nsITransferable* aTransferable, uint64_t aThreshold,
      ClipboardType aWhichClipboard,
      mozilla::dom::WindowContext* aWindowContext, JSContext* aJSContext,
      mozilla::dom::Promise** aPromise) override final;
  // If the clipboard data could not be taken by threshold,
  // returns NS_ERROR_CLIPBOARD_TOO_BIG.
  NS_IMETHOD GetDataIfSmallerThanNative(
      nsITransferable* aTransferable, uint64_t aThreshold,
      ClipboardType aWhichClipboard,
      mozilla::dom::WindowContext* aWindowContext) override final;
  NS_IMETHOD GetDataSnapshot(
      const nsTArray<nsCString>& aFlavorList, ClipboardType aWhichClipboard,
      mozilla::dom::WindowContext* aRequestingWindowContext,
      nsIPrincipal* aRequestingPrincipal,
      nsIClipboardGetDataSnapshotCallback* aCallback) override final;
  NS_IMETHOD GetDataSnapshotSync(
      const nsTArray<nsCString>& aFlavorList, ClipboardType aWhichClipboard,
      mozilla::dom::WindowContext* aRequestingWindowContext,
      nsIClipboardDataSnapshot** _retval) override final;
  NS_IMETHOD EmptyClipboard(ClipboardType aWhichClipboard) override final;
  NS_IMETHOD HasDataMatchingFlavors(const nsTArray<nsCString>& aFlavorList,
                                    ClipboardType aWhichClipboard,
                                    bool* aOutResult) override final;
  NS_IMETHOD IsClipboardTypeSupported(ClipboardType aWhichClipboard,
                                      bool* aRetval) override final;

  void GetDataSnapshotInternal(
      const nsTArray<nsCString>& aFlavorList,
      nsIClipboard::ClipboardType aClipboardType,
      mozilla::dom::WindowContext* aRequestingWindowContext,
      nsIPrincipal* aRequestingPrincipal,
      nsIClipboardGetDataSnapshotCallback* aCallback);

  using SetDataCompletion = mozilla::MoveOnlyFunction<void(nsresult)>;

  // Same as SetData, but reports the final result through aCompletion so
  // callers can wait for a content analysis verdict without the main thread
  // spinning its event loop. Used by ClipboardContentAnalysisParent for
  // copies from content processes.
  void SetDataWithCompletion(nsITransferable* aTransferable,
                             nsIClipboardOwner* aOwner,
                             ClipboardType aWhichClipboard,
                             mozilla::dom::WindowContext* aWindowContext,
                             SetDataCompletion&& aCompletion);

  // Same as GetData, but leaves the paste content analysis check to the
  // caller. aRequestingWindowContext is only used to decide whether the
  // requester may read a copy kept in the clipboard cache (see
  // ClipboardCache::SetKeptCopy). This can go away once
  // ClipboardContentAnalysisParent uses GetDataSnapshot (bug 1908280).
  nsresult GetDataWithoutContentAnalysis(
      nsITransferable* aTransferable, ClipboardType aWhichClipboard,
      mozilla::dom::WindowContext* aRequestingWindowContext);

  using GetNativeDataCallback = mozilla::MoveOnlyFunction<void(
      mozilla::Result<nsCOMPtr<nsISupports>, nsresult>)>;
  using HasMatchingFlavorsCallback = mozilla::MoveOnlyFunction<void(
      mozilla::Result<nsTArray<nsCString>, nsresult>)>;
  using GetWebCustomFormatsCallback = mozilla::MoveOnlyFunction<void(
      mozilla::Result<nsTArray<nsCString>, nsresult>)>;

  // The inner window id of the window the cached data that
  // aRequestingWindowContext would read came from, if the cache is valid and
  // we know it.
  mozilla::Maybe<uint64_t> GetClipboardCacheInnerWindowId(
      ClipboardType aClipboardType,
      mozilla::dom::WindowContext* aRequestingWindowContext);
  virtual mozilla::Result<int32_t, nsresult> GetNativeClipboardSequenceNumber(
      ClipboardType aWhichClipboard) = 0;

  class ClipboardPopulatedDataSnapshot final : public nsIClipboardDataSnapshot {
   public:
    explicit ClipboardPopulatedDataSnapshot(nsITransferable* aTransferable);

    NS_DECL_ISUPPORTS
    NS_DECL_NSICLIPBOARDDATASNAPSHOT
   private:
    virtual ~ClipboardPopulatedDataSnapshot() = default;
    nsCOMPtr<nsITransferable> mTransferable;
    // List of available data types for clipboard content.
    nsTArray<nsCString> mFlavors;
  };

 protected:
  virtual ~nsBaseClipboard();

  // Implement the native clipboard behavior.
  NS_IMETHOD SetNativeClipboardData(nsITransferable* aTransferable,
                                    ClipboardType aWhichClipboard) = 0;
  virtual mozilla::Result<nsCOMPtr<nsISupports>, nsresult>
  GetNativeClipboardData(const nsACString& aFlavor,
                         ClipboardType aWhichClipboard,
                         uint64_t aThreshold = 0) = 0;
  virtual void AsyncGetNativeClipboardData(const nsACString& aFlavor,
                                           ClipboardType aWhichClipboard,
                                           GetNativeDataCallback&& aCallback);
  virtual nsresult EmptyNativeClipboardData(ClipboardType aWhichClipboard) = 0;
  virtual mozilla::Result<bool, nsresult> HasNativeClipboardDataMatchingFlavors(
      const nsTArray<nsCString>& aFlavorList,
      ClipboardType aWhichClipboard) = 0;
  virtual void AsyncHasNativeClipboardDataMatchingFlavors(
      const nsTArray<nsCString>& aFlavorList, ClipboardType aWhichClipboard,
      HasMatchingFlavorsCallback&& aCallback);

  nsTArray<nsCString> GetWebCustomFormatsFromClipboard(
      ClipboardType aWhichClipboard);

  void ClearClipboardCache(ClipboardType aClipboardType);

  /**
   *  This method is used to check if the passed in flavor is a valid flavor
   *  for nsIClipboard.
   *  @param aFlavor [in] the web custom format to be checked.
   *  @return        false, if aFlavor is a invalid, otherwise, true.
   *
   *                 mimeType is a valid flavor.
   *                 mimeType with parameters is a valid flavor.
   *                 any string not a web custom format is a valid flavor.
   *                 web custom format is a valid flavor.
   *                 web custom format with parameters is not valid flavor.
   *
   *  example:  "text/plain" -> true             // mimeType
   *            "text/plain;foo=1" -> true       // mimeType with parameters
   *            "web text/plain" -> true         // A valid web custom format
   *            "web " - false                   // Not a valid web custom
   *                                                format
   *            "web text/plain;foo=1" -> false  // A web custom format with
   *                                             // parameters is invalid
   *            "Not MimeType" -> true           // any string which is not web
   *                                             // custom format
   */
  static bool IsValidFlavor(const nsACString& aFlavor);

 private:
  // A copy waiting on a content analysis verdict before it may touch the
  // clipboard.  A newer write replaces the entry, which makes the older
  // check's completion a no-op, so the write issued last always wins
  // no matter which verdict arrives first.
  class PendingCopy final {
   public:
    NS_INLINE_DECL_REFCOUNTING(PendingCopy)

    PendingCopy(nsITransferable* aTransferable, nsIClipboardOwner* aOwner,
                mozilla::dom::WindowContext* aWindowContext,
                SetDataCompletion&& aCompletion)
        : mTransferable(aTransferable),
          mOwner(aOwner),
          mWindowContext(aWindowContext),
          mCompletion(std::move(aCompletion)) {}

    // Moves the completion out before running it, so it can only fire once
    // however many times the copy is completed or cancelled.
    void Complete(nsresult aResult) {
      if (mCompletion) {
        SetDataCompletion completion = std::move(mCompletion);
        completion(aResult);
      }
    }

    // The principal of the copied data, or failing that of the copying
    // document.
    already_AddRefed<nsIPrincipal> GetSourcePrincipal() const;

    // Whether aRequestingWindow may read this copy while the clipboard cache
    // keeps it: only web content in the copying page and site may, and only
    // while content analysis is enabled.
    bool MayBeReadBy(mozilla::dom::WindowContext* aRequestingWindow) const;

    const nsCOMPtr<nsITransferable> mTransferable;
    const nsCOMPtr<nsIClipboardOwner> mOwner;
    const RefPtr<mozilla::dom::WindowContext> mWindowContext;

   private:
    ~PendingCopy() = default;
    SetDataCompletion mCompletion;
  };

  // aCheckContentAnalysis is false for the internal commit that runs once a
  // content analysis check has already allowed the copy, and for writing the
  // blocked-copy placeholder.
  nsresult SetDataImpl(nsITransferable* aTransferable,
                       nsIClipboardOwner* aOwner, ClipboardType aWhichClipboard,
                       mozilla::dom::WindowContext* aWindowContext,
                       bool aCheckContentAnalysis,
                       SetDataCompletion&& aCompletion = nullptr);

  // Whether this write has to wait for a content analysis copy verdict.
  bool NeedsCopyContentAnalysis(nsITransferable* aTransferable,
                                ClipboardType aWhichClipboard,
                                mozilla::dom::WindowContext* aWindowContext);

  // Called on the main thread when a deferred copy's verdict arrives.
  void OnCopyContentAnalysisResult(ClipboardType aWhichClipboard,
                                   PendingCopy* aPendingCopy, bool aAllowed);

  // Replaces the clipboard contents with a localized notice that the copy was
  // not permitted.
  void WriteCopyBlockedPlaceholder(ClipboardType aWhichClipboard);

  // Drops any deferred copy for this clipboard type, completing it with
  // aReason.  No-op if there isn't one.
  void CancelPendingCopy(ClipboardType aClipboardType, nsresult aReason);

  void RejectPendingAsyncSetDataRequestIfAny(ClipboardType aClipboardType);

  class AsyncSetClipboardData final : public nsIAsyncSetClipboardData {
   public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSIASYNCSETCLIPBOARDDATA

    AsyncSetClipboardData(nsIClipboard::ClipboardType aClipboardType,
                          nsBaseClipboard* aClipboard,
                          mozilla::dom::WindowContext* aRequestingWindowContext,
                          nsIAsyncClipboardRequestCallback* aCallback);

   private:
    virtual ~AsyncSetClipboardData() = default;
    bool IsValid() const {
      // If this request is no longer valid, the callback should be notified.
      MOZ_ASSERT_IF(!mClipboard, !mCallback);
      return !!mClipboard;
    }
    void MaybeNotifyCallback(nsresult aResult);

    // The clipboard type defined in nsIClipboard.
    nsIClipboard::ClipboardType mClipboardType;
    // It is safe to use a raw pointer as it will be nullified (by calling
    // NotifyCallback()) once nsBaseClipboard stops tracking us. This is
    // also used to indicate whether this request is valid.
    nsBaseClipboard* mClipboard;
    RefPtr<mozilla::dom::WindowContext> mWindowContext;
    // mCallback will be nullified once the callback is notified to ensure the
    // callback is only notified once.
    nsCOMPtr<nsIAsyncClipboardRequestCallback> mCallback;
  };

  class ClipboardDataSnapshot final : public nsIClipboardDataSnapshot {
   public:
    ClipboardDataSnapshot(
        nsIClipboard::ClipboardType aClipboardType, int32_t aSequenceNumber,
        nsTArray<nsCString>&& aFlavors, bool aFromCache,
        nsBaseClipboard* aClipboard,
        mozilla::dom::WindowContext* aRequestingWindowContext);

    NS_DECL_ISUPPORTS
    NS_DECL_NSICLIPBOARDDATASNAPSHOT

   private:
    virtual ~ClipboardDataSnapshot() = default;
    bool IsValid();

    using GetDataInternalCallback = mozilla::MoveOnlyFunction<void(nsresult)>;
    void GetDataInternal(nsTArray<nsCString>&& aTypes,
                         nsTArray<nsCString>::index_type aIndex,
                         nsITransferable* aTransferable,
                         GetDataInternalCallback&& aCallback);

    // The clipboard type defined in nsIClipboard.
    const nsIClipboard::ClipboardType mClipboardType;
    // The sequence number associated with the clipboard content for this
    // request. If it doesn't match with the current sequence number in system
    // clipboard, this request targets stale data and is deemed invalid.
    const int32_t mSequenceNumber;
    // List of available data types for clipboard content.
    const nsTArray<nsCString> mFlavors;
    // Data should be read from cache.
    const bool mFromCache;
    // This is also used to indicate whether this request is still valid.
    RefPtr<nsBaseClipboard> mClipboard;
    // The requesting window, which is used for Content Analysis purposes.
    RefPtr<mozilla::dom::WindowContext> mRequestingWindowContext;
  };

  class ClipboardCache final {
   public:
    ~ClipboardCache() {
      // In order to notify the old clipboard owner.
      Clear();
    }

    /**
     * Clear the cached transferable and notify the original clipboard owner
     * that it has lost ownership.
     */
    void Clear();
    void Update(nsITransferable* aTransferable,
                nsIClipboardOwner* aClipboardOwner, int32_t aSequenceNumber,
                mozilla::Maybe<uint64_t> aInnerWindowId) {
      // Clear first to notify the old clipboard owner.
      Clear();
      mTransferable = aTransferable;
      mClipboardOwner = aClipboardOwner;
      mSequenceNumber = aSequenceNumber;
      mInnerWindowId = std::move(aInnerWindowId);
    }
    nsITransferable* GetTransferable() const { return mTransferable; }
    nsIClipboardOwner* GetClipboardOwner() const { return mClipboardOwner; }
    int32_t GetSequenceNumber() const { return mSequenceNumber; }
    mozilla::Maybe<uint64_t> GetInnerWindowId() const { return mInnerWindowId; }

    // Fills aTransferable from aCachedTransferable, which is either the
    // cached transferable or a kept copy's.
    static nsresult GetData(nsITransferable* aCachedTransferable,
                            nsITransferable* aTransferable);

    /**
     * Keep aPendingCopy, which has not (or not yet) been written to the native
     * clipboard, alongside the cached native clipboard data so that web
     * content in the copying page and site can still read it (see
     * PendingCopy::MayBeReadBy). It is keyed to aSequenceNumber, the sequence
     * number of whatever the native clipboard holds, and dropped once that
     * changes or the copying page goes away.
     *
     * Returns false, keeping nothing, if the copy's source can't be
     * identified.
     */
    bool SetKeptCopy(PendingCopy* aPendingCopy, int32_t aSequenceNumber);
    // Keys the kept copy to aSequenceNumber, which the native clipboard got
    // when we wrote something else to it (such as a notice saying why the
    // copy isn't there).
    void RekeyKeptCopy(int32_t aSequenceNumber);
    // Stops keeping the kept copy, if any, and returns it.
    RefPtr<PendingCopy> DropKeptCopy();
    PendingCopy* GetKeptCopy() const { return mKeptCopy; }
    int32_t GetKeptCopySequenceNumber() const {
      return mKeptCopySequenceNumber;
    }

   private:
    nsCOMPtr<nsITransferable> mTransferable;
    nsCOMPtr<nsIClipboardOwner> mClipboardOwner;
    int32_t mSequenceNumber = -1;
    mozilla::Maybe<uint64_t> mInnerWindowId;
    // A copy that has not (or not yet) been written to the native clipboard,
    // kept so that web content in the copying page and site can still read it
    // (see SetKeptCopy).
    RefPtr<PendingCopy> mKeptCopy;
    // The native clipboard sequence number mKeptCopy is keyed to. The kept
    // copy is dropped once the native clipboard's sequence number no longer
    // matches.
    int32_t mKeptCopySequenceNumber = -1;
  };

  // What a requester reads from the clipboard cache.
  struct CachedDataForReading {
    nsCOMPtr<nsITransferable> mTransferable;
    int32_t mSequenceNumber = -1;
    // A kept copy is not on the native clipboard, so the native clipboard
    // must not fill in flavors it lacks.
    bool mIsKeptCopy = false;
  };

  void MaybeRetryGetAvailableFlavors(
      const nsTArray<nsCString>& aFlavorList,
      nsIClipboard::ClipboardType aWhichClipboard,
      nsIClipboardGetDataSnapshotCallback* aCallback, int32_t aRetryCount,
      mozilla::dom::WindowContext* aRequestingWindowContext,
      nsIPrincipal* aRequestingPrincipal);

  // Return clipboard cache if the cached data is valid, otherwise clear the
  // cached data and returns null.
  ClipboardCache* GetClipboardCacheIfValid(ClipboardType aClipboardType);

  // Return the copy kept in the clipboard cache if it is still valid,
  // otherwise drop it and return null.
  PendingCopy* GetKeptCopyIfValid(ClipboardType aClipboardType);

  // Return the copy kept in the clipboard cache if it is valid and
  // aRequestingWindowContext may read it, otherwise null.
  PendingCopy* GetKeptCopyForReading(
      ClipboardType aClipboardType,
      mozilla::dom::WindowContext* aRequestingWindowContext);

  // What aRequestingWindowContext reads from the clipboard cache: the copy
  // kept for its page if there is one, otherwise the data we put on the
  // native clipboard if widget.clipboard.use-cached-data.enabled is on.
  mozilla::Maybe<CachedDataForReading> GetClipboardCacheForReading(
      ClipboardType aClipboardType,
      mozilla::dom::WindowContext* aRequestingWindowContext);

  // Return whether aRequestingPrincipal is allowed to see the web custom
  // formats currently on the clipboard, see
  // clipboard.readCrossOriginCustomFormats.enabled and
  // clipboard.readCustomFormatsFromClipboard.enabled.
  bool CanReadWebCustomFormats(ClipboardType aClipboardType,
                               nsIPrincipal* aRequestingPrincipal);

  mozilla::Result<nsTArray<nsCString>, nsresult> GetFlavorsFromClipboardCache(
      ClipboardType aClipboardType);

  nsresult GetDataImpl(nsITransferable* aTransferable, uint64_t aThreshold,
                       ClipboardType aWhichClipboard,
                       mozilla::dom::WindowContext* aWindowContext,
                       bool aCheckContentAnalysis);

  // Whether any string data in aTransferable is larger than aThreshold bytes.
  static bool TransferableExceedsThreshold(nsITransferable* aTransferable,
                                           uint64_t aThreshold);
  void RequestUserConfirmation(ClipboardType aClipboardType,
                               const nsTArray<nsCString>& aFlavorList,
                               mozilla::dom::WindowContext* aWindowContext,
                               nsIPrincipal* aRequestingPrincipal,
                               nsIClipboardGetDataSnapshotCallback* aCallback);

  already_AddRefed<nsIClipboardDataSnapshot>
  MaybeCreateGetRequestFromClipboardCache(
      const nsTArray<nsCString>& aFlavorList, ClipboardType aClipboardType,
      mozilla::dom::WindowContext* aRequestingWindowContext,
      nsIPrincipal* aRequestingPrincipal);

  // Clean up data in transferable for posting to clipboard or dragging.  This
  // guarantees that text data does not include NUL characters.
  static nsresult SanitizeForClipboard(nsITransferable* aTransferable);

  // Track the pending request for each clipboard type separately. And only need
  // to track the latest request for each clipboard type as the prior pending
  // request will be canceled when a new request is made.
  mozilla::Array<RefPtr<AsyncSetClipboardData>,
                 nsIClipboard::kClipboardTypeCount>
      mPendingWriteRequests;

  mozilla::Array<mozilla::UniquePtr<ClipboardCache>,
                 nsIClipboard::kClipboardTypeCount>
      mCaches;

  // Copies awaiting a content analysis verdict. Only the
  // global clipboard is ever analyzed.
  RefPtr<PendingCopy> mPendingCopy;
  const mozilla::dom::ClipboardCapabilities mClipboardCaps;
  bool mIgnoreEmptyNotification = false;

  // True when SetNativeClipboardData or EmptyNativeClipboardData is running.
  bool mMutatingNativeClipboard = false;
};

#endif  // nsBaseClipboard_h_
