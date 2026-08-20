/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * container for information saved in session history when the document
 * is not
 */

#include "mozilla/PresState.h"
#include "mozilla/UniquePtr.h"
#include "nsILayoutHistoryState.h"
#include "nsTHashMap.h"
#include "nsWeakReference.h"

using namespace mozilla;

class nsLayoutHistoryState final : public nsILayoutHistoryState,
                                   public nsSupportsWeakReference {
 public:
  nsLayoutHistoryState() : mScrollPositionOnly(false) {}

  NS_DECL_ISUPPORTS
  NS_DECL_NSILAYOUTHISTORYSTATE

 private:
  ~nsLayoutHistoryState() = default;
  bool mScrollPositionOnly;

  nsTHashMap<nsCString, UniquePtr<PresState>> mStates;
};

already_AddRefed<nsILayoutHistoryState> NS_NewLayoutHistoryState() {
  return MakeAndAddRef<nsLayoutHistoryState>();
}

NS_IMPL_ISUPPORTS(nsLayoutHistoryState, nsILayoutHistoryState,
                  nsISupportsWeakReference)

NS_IMETHODIMP
nsLayoutHistoryState::GetHasStates(bool* aHasStates) {
  *aHasStates = HasStates();
  return NS_OK;
}

NS_IMETHODIMP
nsLayoutHistoryState::GetKeys(nsTArray<nsCString>& aKeys) {
  if (!HasStates()) {
    return NS_ERROR_FAILURE;
  }

  AppendToArray(aKeys, mStates.Keys());

  return NS_OK;
}

NS_IMETHODIMP
nsLayoutHistoryState::GetPresState(const nsACString& aKey, float* aScrollX,
                                   float* aScrollY,
                                   bool* aAllowScrollOriginDowngrade,
                                   float* aRes) {
  PresState* state = GetState(nsCString(aKey));

  if (!state) {
    return NS_ERROR_FAILURE;
  }

  *aScrollX = state->scrollState().x;
  *aScrollY = state->scrollState().y;
  *aAllowScrollOriginDowngrade = state->allowScrollOriginDowngrade();
  *aRes = state->resolution();

  return NS_OK;
}

NS_IMETHODIMP
nsLayoutHistoryState::AddNewPresState(const nsACString& aKey, float aScrollX,
                                      float aScrollY,
                                      bool aAllowScrollOriginDowngrade,
                                      float aRes) {
  UniquePtr<PresState> newState = NewPresState();
  newState->scrollState() = nsPoint(aScrollX, aScrollY);
  newState->allowScrollOriginDowngrade() = aAllowScrollOriginDowngrade;
  newState->resolution() = aRes;

  mStates.InsertOrUpdate(nsCString(aKey), std::move(newState));

  return NS_OK;
}

void nsLayoutHistoryState::AddState(const nsCString& aStateKey,
                                    UniquePtr<PresState> aState) {
  mStates.InsertOrUpdate(aStateKey, std::move(aState));
}

PresState* nsLayoutHistoryState::GetState(const nsCString& aKey) {
  auto statePtr = mStates.Lookup(aKey);
  if (!statePtr) {
    return nullptr;
  }
  PresState* state = statePtr->get();

  if (mScrollPositionOnly) {
    // Ensure any state that shouldn't be restored is removed
    state->contentData() = void_t();
    state->disabledSet() = false;
  }

  return state;
}

void nsLayoutHistoryState::RemoveState(const nsCString& aKey) {
  mStates.Remove(aKey);
}

UniquePtr<PresState> nsLayoutHistoryState::TakeState(const nsCString& aKey) {
  UniquePtr<PresState> state;
  if (auto entry = mStates.Extract(aKey)) {
    state = std::move(*entry);
  }

  if (state && mScrollPositionOnly) {
    // Ensure any state that shouldn't be restored is removed
    state->contentData() = void_t();
    state->disabledSet() = false;
  }

  return state;
}

bool nsLayoutHistoryState::HasStates() { return mStates.Count() != 0; }

void nsLayoutHistoryState::SetScrollPositionOnly(const bool aFlag) {
  mScrollPositionOnly = aFlag;
}

void nsLayoutHistoryState::ResetScrollState() {
  for (const auto& state : mStates.Values()) {
    if (state) {
      state->scrollState() = nsPoint(0, 0);
    }
  }
}

void nsLayoutHistoryState::GetContents(bool* aScrollPositionOnly,
                                       nsTArray<nsCString>& aKeys,
                                       nsTArray<mozilla::PresState>& aStates) {
  *aScrollPositionOnly = mScrollPositionOnly;
  aKeys.SetCapacity(mStates.Count());
  aStates.SetCapacity(mStates.Count());
  for (const auto& entry : mStates) {
    aKeys.AppendElement(entry.GetKey());
    aStates.AppendElement(*(entry.GetData().get()));
  }
}

void nsLayoutHistoryState::Reset() {
  mScrollPositionOnly = false;
  mStates.Clear();
}

namespace mozilla {
UniquePtr<PresState> NewPresState() {
  return MakeUnique<PresState>(
      /* contentData */ mozilla::void_t(),
      /* scrollState */ nsPoint(0, 0),
      /* allowScrollOriginDowngrade */ true,
      /* resolution */ 1.0,
      /* disabledSet */ false,
      /* disabled */ false,
      /* droppedDown */ false,
      /* horizontalOverflow */ false,
      /* verticalOverflow */ false);
}
}  // namespace mozilla
