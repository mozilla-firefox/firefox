/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsSHistory_h
#define nsSHistory_h

#include "nsCOMPtr.h"
#include "nsDocShellLoadState.h"
#include "nsExpirationTracker.h"
#include "nsIDocumentViewer.h"
#include "nsISHistory.h"
#include "nsSHEntryShared.h"
#include "nsSimpleEnumerator.h"
#include "nsTObserverArray.h"
#include "nsWeakReference.h"

#include "mozilla/dom/SessionHistoryEntry.h"
#include "mozilla/dom/ipc/IdType.h"
#include "mozilla/LinkedList.h"
#include "mozilla/UniquePtr.h"

class nsIDocShell;
class nsDocShell;
class nsSHistoryObserver;

namespace mozilla {
namespace dom {
class EntryList;
class LoadSHEntryResult;
}  // namespace dom
}  // namespace mozilla

class nsSHistory : public mozilla::LinkedListElement<nsSHistory>,
                   public nsISHistory,
                   public nsSupportsWeakReference {
  using SessionHistoryEntry = mozilla::dom::SessionHistoryEntry;

 public:
  // The timer based history tracker is used to evict bfcache on expiration.
  class HistoryTracker final
      : public nsExpirationTracker<mozilla::dom::SHEntrySharedParentState, 3> {
   public:
    explicit HistoryTracker(nsSHistory* aSHistory, uint32_t aTimeout,
                            nsIEventTarget* aEventTarget)
        : nsExpirationTracker(1000 * aTimeout / 2, "HistoryTracker"_ns,
                              aEventTarget) {
      MOZ_ASSERT(aSHistory);
      mSHistory = aSHistory;
    }

   protected:
    virtual void NotifyExpired(
        mozilla::dom::SHEntrySharedParentState* aObj) override {
      RemoveObject(aObj);
      mSHistory->EvictExpiredDocumentViewerForEntry(aObj);
    }

   private:
    // HistoryTracker is owned by nsSHistory; it always outlives HistoryTracker
    // so it's safe to use raw pointer here.
    nsSHistory* mSHistory;
  };

  // Structure used in SetChildHistoryEntry
  struct SwapEntriesData {
    mozilla::dom::BrowsingContext*
        ignoreBC;  // constant; the browsing context to ignore
    SessionHistoryEntry* destTreeRoot;  // constant; the root of the dest tree
    SessionHistoryEntry*
        destTreeParent;  // constant; the node under destTreeRoot
                         // whose children will correspond to aEntry
  };

  explicit nsSHistory(mozilla::dom::BrowsingContext* aRootBC);
  NS_DECL_ISUPPORTS
  NS_DECL_NSISHISTORY

  // One time initialization method
  static nsresult Startup();
  static void Shutdown();
  static void UpdatePrefs();

  // Max number of total cached content viewers.  If the pref
  // browser.sessionhistory.max_total_viewers is negative, then
  // this value is calculated based on the total amount of memory.
  // Otherwise, it comes straight from the pref.
  static uint32_t GetMaxTotalViewers() { return sHistoryMaxTotalViewers; }

  // Get the root SHEntry from a given entry.
  static already_AddRefed<SessionHistoryEntry> GetRootSHEntry(
      SessionHistoryEntry* aEntry);

  // Callback prototype for WalkHistoryEntries.
  // `aEntry` is the child history entry, `aBC` is its corresponding browsing
  // context, `aChildIndex` is the child's index in its parent entry, and
  // `aData` is the opaque pointer passed to WalkHistoryEntries. Both structs
  // that are passed as `aData` to this function have a field
  // `aEntriesToUpdate`, which is an array of entries we need to update in
  // docshell, if the 'SH in parent' pref is on (which implies that this method
  // is executed in the parent)
  typedef nsresult (*WalkHistoryEntriesFunc)(SessionHistoryEntry* aEntry,
                                             mozilla::dom::BrowsingContext* aBC,
                                             int32_t aChildIndex, void* aData);

  // Clone a session history tree for subframe navigation.
  // The tree rooted at |aSrcEntry| will be cloned into |aDestEntry|, except
  // for the entry with id |aCloneID|, which will be replaced with
  // |aReplaceEntry|. |aSrcShell| is a (possibly null) docshell which
  // corresponds to |aSrcEntry| via its mLSHE or mOHE pointers, and will
  // have that pointer updated to point to the cloned history entry.
  // If aCloneChildren is true then the children of the entry with id
  // |aCloneID| will be cloned into |aReplaceEntry|.
  static nsresult CloneAndReplace(SessionHistoryEntry* aSrcEntry,
                                  mozilla::dom::BrowsingContext* aOwnerBC,
                                  uint32_t aCloneID,
                                  SessionHistoryEntry* aReplaceEntry,
                                  bool aCloneChildren,
                                  SessionHistoryEntry** aDestEntry);

  // Child-walking callback for CloneAndReplace
  static nsresult CloneAndReplaceChild(SessionHistoryEntry* aEntry,
                                       mozilla::dom::BrowsingContext* aOwnerBC,
                                       int32_t aChildIndex, void* aData);

  // Child-walking callback for SetHistoryEntry
  static nsresult SetChildHistoryEntry(SessionHistoryEntry* aEntry,
                                       mozilla::dom::BrowsingContext* aBC,
                                       int32_t aEntryIndex, void* aData);

  // For each child of aRootEntry, find the corresponding shell which is
  // a child of aBC, and call aCallback. The opaque pointer aData
  // is passed to the callback.
  static nsresult WalkHistoryEntries(SessionHistoryEntry* aRootEntry,
                                     mozilla::dom::BrowsingContext* aBC,
                                     WalkHistoryEntriesFunc aCallback,
                                     void* aData);

  // This function finds all entries that are contiguous and same-origin with
  // the aEntry. And call the aCallback on them, including the aEntry. This only
  // works for the root entries. It will do nothing for non-root entries.
  static void WalkContiguousEntries(
      SessionHistoryEntry* aEntry,
      const std::function<void(SessionHistoryEntry*)>& aCallback);
  // This function finds all entries that are contiguous and same-origin with
  // the aEntry and call the aCallback on them, including the aEntry, in their
  // history order. Will stop walking when `aCallback` returns false. Finds the
  // leftmost of entries that share navigation key. Works for all entries, not
  // just root entries.
  static void WalkContiguousEntriesInOrder(
      SessionHistoryEntry* aEntry,
      const std::function<bool(SessionHistoryEntry*)>& aCallback);
  // Same as above, but calls aCallback on aEntry, then the entries to the left
  // of aEntry in reverse order, then the entries to the right in normal order.
  // In case of sequences of adjacent contiguous same ID entries aCallback will
  // be called on the leftmost entry only.
  static void WalkClosestContiguousEntriesFrom(
      SessionHistoryEntry* aEntry,
      const std::function<bool(SessionHistoryEntry*)>& aCallback);

  nsTArray<RefPtr<SessionHistoryEntry>>& Entries() { return mEntries; }

  void NotifyOnHistoryReplaceEntry();

  void RemoveEntries(nsTArray<nsID>& aIDs, int32_t aStartIndex,
                     bool* aDidRemove);

  // The size of the window of SHEntries which can have alive viewers in the
  // bfcache around the currently active SHEntry.
  //
  // We try to keep viewers for SHEntries between index - VIEWER_WINDOW and
  // index + VIEWER_WINDOW alive.
  static const int32_t VIEWER_WINDOW = 3;

  struct LoadEntryResult {
    RefPtr<mozilla::dom::BrowsingContext> mBrowsingContext;
    RefPtr<nsDocShellLoadState> mLoadState;
  };

  MOZ_CAN_RUN_SCRIPT
  static void LoadURIs(
      const nsTArray<LoadEntryResult>& aLoadResults, bool aCheckForCancelation,
      const std::function<void(nsresult)>& aResolver = [](auto) {},
      mozilla::dom::BrowsingContext* aTraversable = nullptr);

  MOZ_CAN_RUN_SCRIPT
  static void LoadURIOrBFCache(const LoadEntryResult& aLoadEntry);

  // If this doesn't return an error then either aLoadResult is set to nothing,
  // in which case the caller should ignore the load, or it returns a valid
  // LoadEntryResult in aLoadResult which the caller should use to do the load.
  nsresult Reload(uint32_t aReloadFlags,
                  nsTArray<LoadEntryResult>& aLoadResults);
  nsresult ReloadCurrentEntry(nsTArray<LoadEntryResult>& aLoadResults);
  // Passing aSourceBrowsingContext should only be done by
  // CanonicalBrowsingContext::HistoryGo, since that corresponds to a call to
  // #apply-the-traverse-history-step
  nsresult GotoIndex(mozilla::dom::BrowsingContext* aSourceBrowsingContext,
                     int32_t aIndex, nsTArray<LoadEntryResult>& aLoadResults,
                     bool aSameEpoch, bool aLoadCurrentEntry,
                     bool aUserActivation);

  void WindowIndices(int32_t aIndex, int32_t* aOutStartIndex,
                     int32_t* aOutEndIndex);
  void NotifyListenersDocumentViewerEvicted(uint32_t aNumEvicted);

  int32_t Length() { return int32_t(mEntries.Length()); }
  int32_t Index() { return mIndex; }
  already_AddRefed<mozilla::dom::BrowsingContext> GetBrowsingContext() {
    return mozilla::dom::BrowsingContext::Get(mRootBC);
  }
  bool HasOngoingUpdate() { return mHasOngoingUpdate; }
  void SetHasOngoingUpdate(bool aVal) { mHasOngoingUpdate = aVal; }

  void SetBrowsingContext(mozilla::dom::BrowsingContext* aRootBC) {
    uint64_t newID = aRootBC ? aRootBC->Id() : 0;
    if (mRootBC != newID) {
      mRootBC = newID;
    }
  }

  int32_t GetTargetIndexForHistoryOperation() {
    // When performing a session history operation, such as replace or
    // navigation by key that can happen during ongoing history traversals; If
    // the requested index is valid, it indicates the loading was triggered by a
    // history load, and we should target the entry at requested index instead.
    return mRequestedIndex == -1 ? mIndex : mRequestedIndex;
  }

  void GetEpoch(uint64_t& aEpoch,
                mozilla::Maybe<mozilla::dom::ContentParentId>& aId) const {
    aEpoch = mEpoch;
    aId = mEpochParentId;
  }
  void SetEpoch(uint64_t aEpoch,
                mozilla::Maybe<mozilla::dom::ContentParentId> aId) {
    mEpoch = aEpoch;
    mEpochParentId = std::move(aId);
  }

  void LogHistory();

  bool ContainsEntry(SessionHistoryEntry* aEntry);

 protected:
  virtual ~nsSHistory();

  uint64_t mRootBC;

 private:
  friend class nsSHistoryObserver;

  enum class SearchDirection : int8_t { Left = -1, Right = 1 };

  // Returns aEntry's root entry's index or -1
  int32_t GetRootIndexOfEntry(mozilla::dom::SessionHistoryEntry* aEntry);

  // Finds the adjacent session history entry in search direction.
  // aRootIndex is aEntry's root entry's index and when an adjacent entry
  // is found, it is set to that entry's root entry's index. Effectively,
  // repeated calls move through mEntries strictly monotonically, so walks
  // terminate even if there are duplicate entries (bug 2042897).
  mozilla::dom::SessionHistoryEntry* FindAdjacentEntryFor(
      mozilla::dom::SessionHistoryEntry* aEntry,
      SearchDirection aSearchDirection, int32_t* aRootIndex);

  // Finds the adjacent session history entry in search direction that shares ID
  // with aEntry.
  // For aRootIndex, see FindAdjacentEntryFor.
  mozilla::dom::SessionHistoryEntry* FindClosestAdjacentContiguousEntryFor(
      mozilla::dom::SessionHistoryEntry* aEntry,
      SearchDirection aSearchDirection, int32_t* aRootIndex);

  // Finds the leftmost entry in a sequence of entries adjacent to aEntry that
  // shares ID with entry.
  // For aRootIndex, see FindAdjacentEntryFor.
  mozilla::dom::SessionHistoryEntry* FindLeftmostAdjacentContiguousEntryFor(
      mozilla::dom::SessionHistoryEntry* aEntry,
      SearchDirection aSearchDirection, int32_t* aRootIndex);

  bool ForEachDifferingEntry(
      SessionHistoryEntry* aPrevEntry, SessionHistoryEntry* aNextEntry,
      mozilla::dom::BrowsingContext* aParent,
      const std::function<void(SessionHistoryEntry*,
                               mozilla::dom::BrowsingContext*)>& aCallback);
  static void InitiateLoad(
      mozilla::dom::BrowsingContext* aSourceBrowsingContext,
      SessionHistoryEntry* aFrameEntry, mozilla::dom::BrowsingContext* aFrameBC,
      long aLoadType, nsTArray<LoadEntryResult>& aLoadResult,
      bool aLoadCurrentEntry, bool aUserActivation, int32_t aOffset,
      nsISHEntry* aPreviousEntryForActivation);

  nsresult LoadEntry(mozilla::dom::BrowsingContext* aSourceBrowsingContext,
                     int32_t aIndex, long aLoadType, uint32_t aHistCmd,
                     nsTArray<LoadEntryResult>& aLoadResults, bool aSameEpoch,
                     bool aLoadCurrentEntry, bool aUserActivation);

  // Find the history entry for a given bfcache entry. It only looks up between
  // the range where alive viewers may exist (i.e nsSHistory::VIEWER_WINDOW).
  nsresult FindEntryForBFCache(mozilla::dom::SHEntrySharedParentState* aEntry,
                               SessionHistoryEntry** aResult,
                               int32_t* aResultIndex);

  // Evict content viewers in this window which don't lie in the "safe" range
  // around aIndex.
  virtual void EvictOutOfRangeWindowDocumentViewers(int32_t aIndex);

 public:
  void EvictDocumentViewerForEntry(SessionHistoryEntry* aEntry);

 private:
  static void GloballyEvictDocumentViewers();
  static void GloballyEvictAllDocumentViewers();

  // Calculates a max number of total
  // content viewers to cache, based on amount of total memory
  static uint32_t CalcMaxTotalViewers();

  nsresult LoadNextPossibleEntry(
      mozilla::dom::BrowsingContext* aSourceBrowsingContext, int32_t aNewIndex,
      long aLoadType, uint32_t aHistCmd,
      nsTArray<LoadEntryResult>& aLoadResults, bool aLoadCurrentEntry,
      bool aUserActivation);

  // aIndex is the index of the entry which may be removed.
  // If aKeepNext is true, aIndex is compared to aIndex + 1,
  // otherwise comparison is done to aIndex - 1.
  bool RemoveDuplicate(int32_t aIndex, bool aKeepNext);

  // We need to update entries in docshell and browsing context.
  // If our docshell is located in parent or 'SH in parent' pref is off we can
  // update it directly, Otherwise, we have two choices. If the browsing context
  // that owns the docshell is in the same process as the process who called us
  // over IPC, then we save entries that need to be updated in a list, and once
  // we have returned from the IPC call, we update the docshell in the child
  // process. Otherwise, if the browsing context is in a different process, we
  // do a nested IPC call to that process to update the docshell in that
  // process.
  static void HandleEntriesToSwapInDocShell(mozilla::dom::BrowsingContext* aBC,
                                            SessionHistoryEntry* aOldEntry,
                                            SessionHistoryEntry* aNewEntry);

  void UpdateEntryLength(SessionHistoryEntry* aOldEntry,
                         SessionHistoryEntry* aNewEntry, bool aMove);

  MOZ_CAN_RUN_SCRIPT
  static bool MaybeCheckUnloadingIsCanceled(
      const nsTArray<nsSHistory::LoadEntryResult>& aLoadResults,
      mozilla::dom::BrowsingContext* aTraversable,
      std::function<void(nsTArray<nsSHistory::LoadEntryResult>&,
                         nsIDocumentViewer::PermitUnloadResult)>&& aResolver);

 protected:
  bool mHasOngoingUpdate;
  nsTArray<RefPtr<SessionHistoryEntry>> mEntries;  // entries are never null
 private:
  // Track all bfcache entries and evict on expiration.
  mozilla::UniquePtr<HistoryTracker> mHistoryTracker;

  int32_t mIndex;           // -1 means "no index"
  int32_t mRequestedIndex;  // -1 means "no requested index"

  // Session History listeners
  nsAutoTObserverArray<nsWeakPtr, 2> mListeners;

  nsID mRootDocShellID;

  // Max viewers allowed total, across all SHistory objects
  static int32_t sHistoryMaxTotalViewers;

  // The epoch (and id) tell us what navigations occured within the same
  // event-loop spin in the child.  We need to know this in order to
  // implement spec requirements for dropping pending navigations when we
  // do a history navigation, if it's not same-document.  Content processes
  // update the epoch via a runnable on each ::Go (including AsyncGo).
  uint64_t mEpoch = 0;
  mozilla::Maybe<mozilla::dom::ContentParentId> mEpochParentId;

  // Session history entries grouped by DocshellID, which are deduplicated by
  // SessionHistoryEntry ID.
  nsTHashMap<nsIDHashKey, mozilla::WeakPtr<mozilla::dom::EntryList>>
      mEntryLists;
};

// CallerWillNotifyHistoryIndexAndLengthChanges is used to prevent
// SHistoryChangeNotifier to send automatic index and length updates.
// When that is done, it is up to the caller to explicitly send those updates.
// This is needed in cases when the update is a reaction to some change in a
// child process and child process passes a changeId to the parent side.
class MOZ_STACK_CLASS CallerWillNotifyHistoryIndexAndLengthChanges {
 public:
  explicit CallerWillNotifyHistoryIndexAndLengthChanges(
      nsISHistory* aSHistory) {
    nsSHistory* shistory = static_cast<nsSHistory*>(aSHistory);
    if (shistory && !shistory->HasOngoingUpdate()) {
      shistory->SetHasOngoingUpdate(true);
      mSHistory = shistory;
    }
  }

  ~CallerWillNotifyHistoryIndexAndLengthChanges() {
    if (mSHistory) {
      mSHistory->SetHasOngoingUpdate(false);
    }
  }

  RefPtr<nsSHistory> mSHistory;
};

inline nsISupports* ToSupports(nsSHistory* aObj) {
  return static_cast<nsISHistory*>(aObj);
}

#endif /* nsSHistory */
