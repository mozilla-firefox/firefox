/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * ChildSHistory represents a view of session history from a child process. It
 * exposes getters for some cached history state, and mutators which are
 * implemented by communicating with the actual history storage.
 *
 * NOTE: Currently session history is in transition, meaning that we're still
 * using the legacy nsSHistory class internally. The API exposed from this class
 * should be only the API which we expect to expose when this transition is
 * complete, and special cases will need to call through the LegacySHistory()
 * getters.
 */

#ifndef mozilla_dom_ChildSHistory_h
#define mozilla_dom_ChildSHistory_h

#include "nsCOMPtr.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "nsWrapperCache.h"
#include "nsThreadUtils.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/LinkedList.h"
#include "nsID.h"

class nsISHEntry;
class nsISHistory;

namespace mozilla::dom {

class BrowsingContext;

class ChildSHistory : public nsISupports, public nsWrapperCache {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(ChildSHistory)
  nsISupports* GetParentObject() const;
  JSObject* WrapObject(JSContext* cx,
                       JS::Handle<JSObject*> aGivenProto) override;

  explicit ChildSHistory(BrowsingContext* aBrowsingContext);

  void SetBrowsingContext(BrowsingContext* aBrowsingContext);

  int32_t Count();
  int32_t Index();

  /** Reload the current entry in the session history. */
  MOZ_CAN_RUN_SCRIPT
  void Reload(uint32_t aReloadFlags, ErrorResult& aRv);

  /**
   * The CanGo and Go methods are called with an offset from the current index.
   * Positive numbers go forward in history, while negative numbers go
   * backwards.
   * aRequireUserInteraction is used in order to enable the back-button
   * intervention. This causes an additional check that there must be a previous
   * entry that has been user-interacted. This check is unnecessary when going
   * forwards as the latest entry is always available, whether it has been
   * interacted with or not. This feature is gated by the
   * browser.navigation.requireUserInteraction pref.
   */
  bool CanGo(int32_t aOffset, bool aRequireUserInteraction);
  MOZ_CAN_RUN_SCRIPT
  void Go(int32_t aOffset, bool aRequireUserInteraction, bool aUserActivation,
          ErrorResult& aRv);
  void AsyncGo(int32_t aOffset, bool aRequireUserInteraction,
               bool aUserActivation);
  void AsyncGo(const nsID& aKey, BrowsingContext* aNavigable,
               bool aRequireUserInteraction, bool aUserActivation,
               bool aCheckForCancelation,
               std::function<void(nsresult)>&& aResolver);

  // aIndex is the new index, and aOffset is the offset between new and current.
  MOZ_CAN_RUN_SCRIPT
  void GotoIndex(int32_t aIndex, int32_t aOffset, bool aRequireUserInteraction,
                 bool aUserActivation);
  MOZ_CAN_RUN_SCRIPT
  void GotoKey(const nsID& aKey, BrowsingContext* aNavigable,
               bool aUserActivation, bool aCheckForCancelation,
               const std::function<void(nsresult)>& aResolver);

  void RemovePendingHistoryNavigations();

  void SetIndexAndLength(uint32_t aIndex, uint32_t aLength,
                         const nsID& aChangeId);
  nsID AddPendingHistoryChange();
  nsID AddPendingHistoryChange(int32_t aIndexDelta, int32_t aLengthDelta);

 private:
  virtual ~ChildSHistory();

  class PendingAsyncHistoryNavigation
      : public Runnable,
        public mozilla::LinkedListElement<PendingAsyncHistoryNavigation> {
   public:
    PendingAsyncHistoryNavigation(ChildSHistory* aHistory, int32_t aOffset,
                                  bool aRequireUserInteraction,
                                  bool aUserActivation);

    PendingAsyncHistoryNavigation(ChildSHistory* aHistory, const nsID& aKey,
                                  BrowsingContext* aBrowsingContext,
                                  bool aRequireUserInteraction,
                                  bool aUserActivation,
                                  bool aCheckForCancelation,
                                  std::function<void(nsresult)>&& aResolver);

    MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHOD Run() override;

   private:
    const RefPtr<ChildSHistory> mHistory;
    bool mRequireUserInteraction;
    bool mUserActivation;
    bool mCheckForCancelation;
    int32_t mOffset;
    Maybe<nsID> mKey;
    RefPtr<BrowsingContext> mBrowsingContext;
    Maybe<std::function<void(nsresult)>> mResolver;
  };

  RefPtr<BrowsingContext> mBrowsingContext;
  nsCOMPtr<nsISHistory> mHistory;
  // Can be removed once history-in-parent is the only way
  mozilla::LinkedList<PendingAsyncHistoryNavigation> mPendingNavigations;
  int32_t mIndex = -1;
  int32_t mLength = 0;

  struct PendingSHistoryChange {
    nsID mChangeID;
    int32_t mIndexDelta;
    int32_t mLengthDelta;
  };
  AutoTArray<PendingSHistoryChange, 2> mPendingSHistoryChanges;

  // Needs to start 1 above default epoch in parent
  uint64_t mHistoryEpoch = 1;
  bool mPendingEpoch = false;
};

}  // namespace mozilla::dom

#endif /* mozilla_dom_ChildSHistory_h */
