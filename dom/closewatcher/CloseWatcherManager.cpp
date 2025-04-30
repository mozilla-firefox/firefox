/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CloseWatcherManager.h"
#include "CloseWatcher.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION(CloseWatcherManager, mGroups)
NS_IMPL_CYCLE_COLLECTING_ADDREF(CloseWatcherManager)
NS_IMPL_CYCLE_COLLECTING_RELEASE(CloseWatcherManager)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(CloseWatcherManager)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

// https://html.spec.whatwg.org/multipage/interaction.html#notify-the-close-watcher-manager-about-user-activation
void CloseWatcherManager::NotifyUserInteraction() {
  if (mNextUserInteractionAllowsNewGroup) {
    mAllowedNumberOfGroups += 1;
    mNextUserInteractionAllowsNewGroup = false;
  }
}

bool CloseWatcherManager::CanGrow() const {
  return mGroups.Length() < mAllowedNumberOfGroups;
}

// https://html.spec.whatwg.org/multipage/interaction.html#process-close-watchers
MOZ_CAN_RUN_SCRIPT bool CloseWatcherManager::ProcessCloseRequest() {
  bool processedACloseWatcher = false;
  if (mGroups.IsEmpty()) {
    return processedACloseWatcher;
  }
  auto i = mGroups.Length() - 1;
  auto group = mGroups.ElementAt(i).Clone();
  for (RefPtr<CloseWatcher> watcher : group.BackwardRange()) {
    processedACloseWatcher = true;
    // TODO:(keithamus): https://github.com/whatwg/html/issues/10240 ?
    if (!watcher->RequestToClose(true)) {
      break;
    }
  }
  if (mAllowedNumberOfGroups > 1) {
    mAllowedNumberOfGroups -= 1;
  }
  return processedACloseWatcher;
}

// https://html.spec.whatwg.org/multipage/interaction.html#establish-a-close-watcher
// step 4-6
void CloseWatcherManager::Add(CloseWatcher& aWatcher) {
  if (CanGrow()) {
    mGroups.AppendElement()->AppendElement(&aWatcher);
  } else {
    MOZ_ASSERT(!mGroups.IsEmpty(),
               "CloseWatcherManager groups must be at least 1");
    auto i = mGroups.Length() - 1;
    MOZ_ASSERT(!mGroups.ElementAt(i).Contains(&aWatcher));
    mGroups.ElementAt(i).AppendElement(&aWatcher);
  }
  mNextUserInteractionAllowsNewGroup = true;
}

// https://html.spec.whatwg.org/multipage/interaction.html#close-watcher-destroy
void CloseWatcherManager::Remove(CloseWatcher& aWatcher) {
  CloseWatcherArray::ForwardIterator iter(mGroups);
  while (iter.HasMore()) {
    auto& group = iter.GetNext();
    group.RemoveElement(&aWatcher);
    if (group.IsEmpty()) {
      iter.Remove();
    }
  }
  mGroups.Compact();
}

bool CloseWatcherManager::Contains(const CloseWatcher& aWatcher) const {
  for (const auto& group : mGroups.BackwardRange()) {
    if (group.Contains(&aWatcher)) {
      return true;
    }
  }
  return false;
}

}  // namespace mozilla::dom
