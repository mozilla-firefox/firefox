/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "LockManagerParent.h"

#include "LockRequestParent.h"
#include "mozilla/PrincipalHashKey.h"
#include "mozilla/RefPtr.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/dom/locks/PLockManager.h"
#include "mozilla/media/MediaUtils.h"
#include "nsIDUtils.h"
#include "nsTHashMap.h"

namespace mozilla::dom::locks {

static StaticAutoPtr<nsTHashMap<PrincipalHashKey, WeakPtr<ManagedLocks>>>
    sManagedLocksMap;

using IPCResult = mozilla::ipc::IPCResult;

LockManagerParent::LockManagerParent(NotNull<nsIPrincipal*> aPrincipal,
                                     const Maybe<nsID>& aClientId)
    : mPrincipal(aPrincipal) {
  if (aClientId.isSome()) {
    mClientId = NSID_TrimBracketsUTF16(aClientId.value());
  } else {
    mClientId = EmptyString();
  }

  if (!sManagedLocksMap) {
    sManagedLocksMap =
        new nsTHashMap<PrincipalHashKey, WeakPtr<ManagedLocks>>();
  } else {
    mManagedLocks = sManagedLocksMap->Get(aPrincipal);
  }

  if (!mManagedLocks) {
    mManagedLocks = new ManagedLocks();
    sManagedLocksMap->LookupOrInsert(aPrincipal, mManagedLocks);
  }
}

void LockManagerParent::ActorDestroy(ActorDestroyReason aWhy) {
  if (!mManagedLocks) {
    return;
  }

  nsTArray<nsString> affectedResourceNames;

  mManagedLocks->mHeldLocks.RemoveElementsBy(
      [this, &affectedResourceNames](const RefPtr<LockRequestParent>& request) {
        bool equals = request->Manager() == this;
        if (equals) {
          affectedResourceNames.AppendElement(request->Data().name());
        }
        return equals;
      });

  for (auto& queue : mManagedLocks->mQueueMap) {
    queue.GetModifiableData()->RemoveElementsBy(
        [this, &name = queue.GetKey(),
         &affectedResourceNames](const RefPtr<LockRequestParent>& request) {
          bool equals = request->Manager() == this;
          if (equals) {
            affectedResourceNames.AppendElement(name);
          }
          return equals;
        });
  }

  for (const nsString& name : affectedResourceNames) {
    if (auto queue = mManagedLocks->mQueueMap.Lookup(name)) {
      ProcessRequestQueue(queue.Data());
    }
  }

  mManagedLocks = nullptr;
  // We just decreased the refcount and potentially deleted it, so check whether
  // the weak pointer still points to anything and remove the entry if not.
  if (!sManagedLocksMap->Get(mPrincipal)) {
    sManagedLocksMap->Remove(mPrincipal);
  }
}

void LockManagerParent::ProcessRequestQueue(
    nsTArray<RefPtr<LockRequestParent>>& aQueue) {
  while (aQueue.Length()) {
    RefPtr<LockRequestParent> first = aQueue[0];
    if (!IsGrantableRequest(first->Data())) {
      break;
    }
    aQueue.RemoveElementAt(0);
    mManagedLocks->mHeldLocks.AppendElement(first);
    (void)NS_WARN_IF(!first->SendResolve(first->Data().lockMode(), true));
  }
}

bool LockManagerParent::IsGrantableRequest(const IPCLockRequest& aRequest) {
  for (const auto& held : mManagedLocks->mHeldLocks) {
    if (held->Data().name() == aRequest.name()) {
      if (aRequest.lockMode() == LockMode::Exclusive) {
        return false;
      }
      MOZ_ASSERT(aRequest.lockMode() == LockMode::Shared);
      if (held->Data().lockMode() == LockMode::Exclusive) {
        return false;
      }
    }
  }
  return true;
}

IPCResult LockManagerParent::RecvQuery(QueryResolver&& aResolver) {
  LockManagerSnapshot snapshot;
  snapshot.mHeld.Construct();
  snapshot.mPending.Construct();
  for (const auto& queueMapEntry : mManagedLocks->mQueueMap) {
    for (const RefPtr<LockRequestParent>& request : queueMapEntry.GetData()) {
      LockInfo info;
      info.mMode.Construct(request->Data().lockMode());
      info.mName.Construct(request->Data().name());
      info.mClientId.Construct(
          mozilla::ipc::ActorCast<LockManagerParent>(request->Manager())
              ->mClientId);
      if (!snapshot.mPending.Value().AppendElement(info, mozilla::fallible)) {
        return IPC_FAIL(this, "Out of memory");
      };
    }
  }
  for (const RefPtr<LockRequestParent>& request : mManagedLocks->mHeldLocks) {
    LockInfo info;
    info.mMode.Construct(request->Data().lockMode());
    info.mName.Construct(request->Data().name());
    info.mClientId.Construct(
        mozilla::ipc::ActorCast<LockManagerParent>(request->Manager())
            ->mClientId);
    if (!snapshot.mHeld.Value().AppendElement(info, mozilla::fallible)) {
      return IPC_FAIL(this, "Out of memory");
    };
  }
  aResolver(snapshot);
  return IPC_OK();
};

already_AddRefed<PLockRequestParent> LockManagerParent::AllocPLockRequestParent(
    const IPCLockRequest& aRequest) {
  return MakeAndAddRef<LockRequestParent>(aRequest);
}

IPCResult LockManagerParent::RecvPLockRequestConstructor(
    PLockRequestParent* aActor, const IPCLockRequest& aRequest) {
  RefPtr<LockRequestParent> actor = static_cast<LockRequestParent*>(aActor);
  nsTArray<RefPtr<LockRequestParent>>& queue =
      mManagedLocks->mQueueMap.LookupOrInsert(aRequest.name());
  if (aRequest.steal()) {
    mManagedLocks->mHeldLocks.RemoveElementsBy(
        [&aRequest](const RefPtr<LockRequestParent>& aHeld) {
          if (aHeld->Data().name() == aRequest.name()) {
            (void)NS_WARN_IF(!PLockRequestParent::Send__delete__(aHeld, true));
            return true;
          }
          return false;
        });
    queue.InsertElementAt(0, actor);
  } else if (aRequest.ifAvailable() &&
             (!queue.IsEmpty() || !IsGrantableRequest(actor->Data()))) {
    (void)NS_WARN_IF(!aActor->SendResolve(aRequest.lockMode(), false));
    return IPC_OK();
  } else {
    queue.AppendElement(actor);
  }
  ProcessRequestQueue(queue);
  return IPC_OK();
}

}  // namespace mozilla::dom::locks
