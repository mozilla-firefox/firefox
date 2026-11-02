/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/ChildSHistory.h"
#include "mozilla/dom/ChildSHistoryBinding.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/ContentFrameMessageManager.h"
#include "mozilla/StaticPrefs_browser.h"
#include "nsIXULRuntime.h"
#include "nsComponentManagerUtils.h"
#include "nsSHistory.h"
#include "nsDocShell.h"
#include "nsXULAppAPI.h"

extern mozilla::LazyLogModule gSHLog;

namespace mozilla {
namespace dom {

ChildSHistory::PendingAsyncHistoryNavigation::PendingAsyncHistoryNavigation(
    ChildSHistory* aHistory, int32_t aOffset, bool aRequireUserInteraction,
    bool aUserActivation)
    : Runnable("PendingAsyncHistoryNavigation"),
      mHistory(aHistory),
      mRequireUserInteraction(aRequireUserInteraction),
      mUserActivation(aUserActivation),
      mOffset(aOffset) {}
ChildSHistory::PendingAsyncHistoryNavigation::PendingAsyncHistoryNavigation(
    ChildSHistory* aHistory, const nsID& aKey,
    BrowsingContext* aBrowsingContext, bool aRequireUserInteraction,
    bool aUserActivation, bool aCheckForCancelation,
    std::function<void(nsresult)>&& aResolver)
    : Runnable("PendingAsyncHistoryNavigation"),
      mHistory(aHistory),
      mRequireUserInteraction(aRequireUserInteraction),
      mUserActivation(aUserActivation),
      mCheckForCancelation(aCheckForCancelation),
      mOffset(std::numeric_limits<int32_t>::max()),
      mKey(Some(aKey)),
      mBrowsingContext(aBrowsingContext),
      mResolver(Some(aResolver)) {}

nsresult ChildSHistory::PendingAsyncHistoryNavigation::Run() {
  if (isInList()) {
    remove();
    if (mKey) {
      RefPtr browsingContext = mBrowsingContext;
      mHistory->GotoKey(*mKey, browsingContext, mUserActivation,
                        mCheckForCancelation, *mResolver);
    } else {
      mHistory->Go(mOffset, mRequireUserInteraction, mUserActivation,
                   IgnoreErrors());
    }
  }
  return NS_OK;
}

ChildSHistory::ChildSHistory(BrowsingContext* aBrowsingContext)
    : mBrowsingContext(aBrowsingContext) {}

ChildSHistory::~ChildSHistory() {
  if (mHistory) {
    static_cast<nsSHistory*>(mHistory.get())->SetBrowsingContext(nullptr);
  }
}

void ChildSHistory::SetBrowsingContext(BrowsingContext* aBrowsingContext) {
  mBrowsingContext = aBrowsingContext;
}

int32_t ChildSHistory::Count() {
  int32_t length = mLength;
  for (const auto& change : mPendingSHistoryChanges) {
    length += change.mLengthDelta;
  }
  return length;
}

int32_t ChildSHistory::Index() {
  int32_t index = mIndex;
  for (const auto& change : mPendingSHistoryChanges) {
    index += change.mIndexDelta;
  }
  return index;
}

nsID ChildSHistory::AddPendingHistoryChange() {
  int32_t indexDelta = 1;
  int32_t lengthDelta = (Index() + indexDelta) - (Count() - 1);
  return AddPendingHistoryChange(indexDelta, lengthDelta);
}

nsID ChildSHistory::AddPendingHistoryChange(int32_t aIndexDelta,
                                            int32_t aLengthDelta) {
  nsID changeID = nsID::GenerateUUID();
  PendingSHistoryChange change = {changeID, aIndexDelta, aLengthDelta};
  mPendingSHistoryChanges.AppendElement(change);
  return changeID;
}

void ChildSHistory::SetIndexAndLength(uint32_t aIndex, uint32_t aLength,
                                      const nsID& aChangeID) {
  mIndex = aIndex;
  mLength = aLength;
  mPendingSHistoryChanges.RemoveElementsBy(
      [aChangeID](const PendingSHistoryChange& aChange) {
        return aChange.mChangeID == aChangeID;
      });
}

void ChildSHistory::Reload(uint32_t aReloadFlags, ErrorResult& aRv) {
  if (XRE_IsParentProcess()) {
    nsCOMPtr<nsISHistory> shistory =
        mBrowsingContext->Canonical()->GetSessionHistory();
    if (shistory) {
      aRv = shistory->Reload(aReloadFlags);
    }
  } else {
    ContentChild::GetSingleton()->SendHistoryReload(mBrowsingContext,
                                                    aReloadFlags);
  }
}

bool ChildSHistory::CanGo(int32_t aOffset, bool aRequireUserInteraction) {
  CheckedInt<int32_t> index = Index();
  index += aOffset;
  if (!index.isValid()) {
    return false;
  }

  if (!mHistory || aOffset >= 0) {
    return index.value() < Count() && index.value() >= 0;
  }

  if (!aRequireUserInteraction) {
    return index.value() >= 0;
  }

  bool canGoBack;
  mHistory->CanGoBackFromEntryAtIndex(Index(), &canGoBack);
  return canGoBack;
}

void ChildSHistory::Go(int32_t aOffset, bool aRequireUserInteraction,
                       bool aUserActivation, ErrorResult& aRv) {
  CheckedInt<int32_t> index = Index();
  MOZ_LOG(
      gSHLog, LogLevel::Debug,
      ("ChildSHistory::Go(%d), current index = %d", aOffset, index.value()));
  if (aRequireUserInteraction && aOffset != -1 && aOffset != 1) {
    NS_ERROR(
        "aRequireUserInteraction may only be used with an offset of -1 or 1");
    aRv.Throw(NS_ERROR_INVALID_ARG);
    return;
  }

  while (true) {
    index += aOffset;
    if (!index.isValid()) {
      aRv.Throw(NS_ERROR_FAILURE);
      return;
    }

    // Check for user interaction if desired, except for the first and last
    // history entries. We compare with >= to account for the case where
    // aOffset >= Count().
    if (!StaticPrefs::browser_navigation_requireUserInteraction() ||
        !aRequireUserInteraction || index.value() >= Count() - 1 ||
        index.value() <= 0) {
      break;
    }
    if (mHistory && mHistory->HasUserInteractionAtIndex(index.value())) {
      break;
    }
  }

  GotoIndex(index.value(), aOffset, aRequireUserInteraction, aUserActivation);
}

void ChildSHistory::AsyncGo(int32_t aOffset, bool aRequireUserInteraction,
                            bool aUserActivation) {
  CheckedInt<int32_t> index = Index();
  MOZ_LOG(gSHLog, LogLevel::Debug,
          ("ChildSHistory::AsyncGo(%d), current index = %d", aOffset,
           index.value()));

  RefPtr asyncNav = MakeRefPtr<PendingAsyncHistoryNavigation>(
      this, aOffset, aRequireUserInteraction, aUserActivation);
  mPendingNavigations.insertBack(asyncNav);
  NS_DispatchToCurrentThread(asyncNav.forget());
}

void ChildSHistory::AsyncGo(const nsID& aKey, BrowsingContext* aNavigable,
                            bool aRequireUserInteraction, bool aUserActivation,
                            bool aCheckForCancelation,
                            std::function<void(nsresult)>&& aResolver) {
  CheckedInt<int32_t> index = Index();
  MOZ_LOG_FMT(gSHLog, LogLevel::Debug,
              "ChildSHistory::AsyncGo({}), current index = {}",
              aKey.ToString().get(), index.value());

  RefPtr asyncNav = MakeRefPtr<PendingAsyncHistoryNavigation>(
      this, aKey, aNavigable, aRequireUserInteraction, aUserActivation,
      aCheckForCancelation, std::move(aResolver));
  mPendingNavigations.insertBack(asyncNav);
  NS_DispatchToCurrentThread(asyncNav.forget());
}

void ChildSHistory::GotoIndex(int32_t aIndex, int32_t aOffset,
                              bool aRequireUserInteraction,
                              bool aUserActivation) {
  MOZ_LOG(gSHLog, LogLevel::Debug,
          ("ChildSHistory::GotoIndex(%d, %d), epoch %" PRIu64, aIndex, aOffset,
           mHistoryEpoch));
  if (!mPendingEpoch) {
    mPendingEpoch = true;
    RefPtr<ChildSHistory> self(this);
    NS_DispatchToCurrentThread(
        NS_NewRunnableFunction("UpdateEpochRunnable", [self] {
          self->mHistoryEpoch++;
          self->mPendingEpoch = false;
        }));
  }

  nsCOMPtr<nsISHistory> shistory = mHistory;
  RefPtr<BrowsingContext> bc = mBrowsingContext;
  bc->HistoryGo(
      aOffset, mHistoryEpoch, aRequireUserInteraction, aUserActivation,
      [shistory](Maybe<int32_t>&& aRequestedIndex) {
        // FIXME Should probably only do this for non-fission.
        if (aRequestedIndex.isSome() && shistory) {
          shistory->InternalSetRequestedIndex(aRequestedIndex.value());
        }
      });
}

void ChildSHistory::GotoKey(const nsID& aKey, BrowsingContext* aNavigable,
                            bool aUserActivation, bool aCheckForCancelation,
                            const std::function<void(nsresult)>& aResolver) {
  if (!mPendingEpoch) {
    mPendingEpoch = true;
    RefPtr<ChildSHistory> self(this);
    NS_DispatchToCurrentThread(
        NS_NewRunnableFunction("UpdateEpochRunnable", [self] {
          self->mHistoryEpoch++;
          self->mPendingEpoch = false;
        }));
  }

  nsCOMPtr<nsISHistory> shistory = mHistory;
  aNavigable->NavigationTraverse(
      aKey, mHistoryEpoch, aUserActivation,
      /* aCheckForCancelation */ aCheckForCancelation,
      [shistory, aResolver](nsresult aResult) { aResolver(aResult); });
}

void ChildSHistory::RemovePendingHistoryNavigations() {
  // Per the spec, this generally shouldn't remove all navigations - it
  // depends if they're in the same document family or not.  We don't do
  // that.  Also with SessionHistoryInParent, this can only abort AsyncGo's
  // that have not yet been sent to the parent - see discussion of point
  // 2.2 in comments in nsDocShell::UpdateURLAndHistory()
  MOZ_LOG(gSHLog, LogLevel::Debug,
          ("ChildSHistory::RemovePendingHistoryNavigations: %zu",
           mPendingNavigations.length()));
  mPendingNavigations.clear();
}

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ChildSHistory)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(ChildSHistory)
NS_IMPL_CYCLE_COLLECTING_RELEASE(ChildSHistory)

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(ChildSHistory)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(ChildSHistory)
  if (tmp->mHistory) {
    static_cast<nsSHistory*>(tmp->mHistory.get())->SetBrowsingContext(nullptr);
  }
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mBrowsingContext, mHistory)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(ChildSHistory)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mBrowsingContext, mHistory)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

JSObject* ChildSHistory::WrapObject(JSContext* cx,
                                    JS::Handle<JSObject*> aGivenProto) {
  return ChildSHistory_Binding::Wrap(cx, this, aGivenProto);
}

nsISupports* ChildSHistory::GetParentObject() const {
  return xpc::NativeGlobal(xpc::PrivilegedJunkScope());
}

}  // namespace dom
}  // namespace mozilla
