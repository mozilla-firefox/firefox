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

/**
 * A base clipboard class for all platform, so that they can share the same
 * implementation.
 */
class nsBaseClipboard : public nsIClipboard {
 public:
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

  using GetNativeDataCallback = mozilla::MoveOnlyFunction<void(
      mozilla::Result<nsCOMPtr<nsISupports>, nsresult>)>;
  using HasMatchingFlavorsCallback = mozilla::MoveOnlyFunction<void(
      mozilla::Result<nsTArray<nsCString>, nsresult>)>;
  using GetWebCustomFormatsCallback = mozilla::MoveOnlyFunction<void(
      mozilla::Result<nsTArray<nsCString>, nsresult>)>;

  mozilla::Maybe<uint64_t> GetClipboardCacheInnerWindowId(
      ClipboardType aClipboardType);
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
    nsresult GetData(nsITransferable* aTransferable) const;

   private:
    nsCOMPtr<nsITransferable> mTransferable;
    nsCOMPtr<nsIClipboardOwner> mClipboardOwner;
    int32_t mSequenceNumber = -1;
    mozilla::Maybe<uint64_t> mInnerWindowId;
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

  // Return whether aRequestingPrincipal is allowed to see the web custom
  // formats currently on the clipboard, see
  // clipboard.readCrossOriginCustomFormats.enabled and
  // clipboard.readCustomFormatsFromClipboard.enabled.
  bool CanReadWebCustomFormats(ClipboardType aClipboardType,
                               nsIPrincipal* aRequestingPrincipal);

  mozilla::Result<nsTArray<nsCString>, nsresult> GetFlavorsFromClipboardCache(
      ClipboardType aClipboardType);
  nsresult GetDataFromClipboardCache(nsITransferable* aTransferable,
                                     ClipboardType aClipboardType);
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
