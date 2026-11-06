/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BoundStorageKeyChild.h"

#include "mozilla/dom/cache/CacheChild.h"
#include "mozilla/dom/cache/CacheOpChild.h"
#include "mozilla/dom/cache/CacheStorage.h"
#include "mozilla/dom/cache/CacheWorkerRef.h"
#include "mozilla/dom/cache/PCacheStorageChild.h"

namespace mozilla::dom::cache {

BoundStorageKeyChild::BoundStorageKeyChild(
    BoundStorageKeyChildListener* aListener)
    : mListener(aListener), mDelayedDestroy(false) {
  MOZ_COUNT_CTOR(BoundStorageKeyChild);
  MOZ_DIAGNOSTIC_ASSERT(mListener);
}

BoundStorageKeyChild::~BoundStorageKeyChild() {
  MOZ_COUNT_DTOR(BoundStorageKeyChild);
  NS_ASSERT_OWNINGTHREAD(BoundStorageKeyChild);
  MOZ_DIAGNOSTIC_ASSERT(!mListener);
}

void BoundStorageKeyChild::ClearListener() {
  NS_ASSERT_OWNINGTHREAD(BoundStorageKeyChild);
  MOZ_DIAGNOSTIC_ASSERT(mListener);
  mListener = nullptr;
}

void BoundStorageKeyChild::StartDestroyFromListener() {
  NS_ASSERT_OWNINGTHREAD(BoundStorageKeyChild);

  // The listener is being destroyed now. Managed actors (CacheStorageChild,
  // CacheChild, CacheStreamControlChild) tear down asynchronously and none of
  // them keeps the listener alive, so StartDestroy() usually takes the delayed
  // path; sever the listener here or DestroyInternal() would later call into
  // freed memory.
  BoundStorageKeyChildListener* listener = mListener;
  if (listener) {
    listener->OnActorDestroy(this);
    // listener should call ClearListener() in OnActorDestroy()
    MOZ_DIAGNOSTIC_ASSERT(!mListener);
  }

  StartDestroy();
}

void BoundStorageKeyChild::DestroyInternal() {
  BoundStorageKeyChildListener* listener = mListener;

  // Theoretically we can get double called if the right race happens.  Handle
  // that by just ignoring the second StartDestroy() call.
  if (!listener) {
    return;
  }

  listener->OnActorDestroy(this);

  // listener should call ClearListener() in OnActorDestroy()
  MOZ_DIAGNOSTIC_ASSERT(!mListener);
}

void BoundStorageKeyChild::StartDestroy() {
  // StartDestroy() can get called from either child actor or the
  // CacheWorkerRef.
  NS_ASSERT_OWNINGTHREAD(BoundStorageKeyChild);

  if (NumChildActors() != 0) {
    mDelayedDestroy = true;
    return;
  }
  DestroyInternal();
}

void BoundStorageKeyChild::NoteDeletedActor() {
  // Check to see if DestroyInternal was delayed because of active CacheOpChilds
  // when StartDestroy was called from WorkerRef notification. If the last
  // CacheOpChild is getting destructed; it's the time for us to SendTearDown to
  // the other side.
  if (mDelayedDestroy && NumChildActors() == 0) {
    DestroyInternal();
  }
}

void BoundStorageKeyChild::ActorDestroy(ActorDestroyReason aReason) {
  NS_ASSERT_OWNINGTHREAD(BoundStorageKeyChild);
  BoundStorageKeyChildListener* listener = mListener;
  if (listener) {
    listener->OnActorDestroy(this);
    // listener should call ClearListener() in OnActorDestroy()
    MOZ_DIAGNOSTIC_ASSERT(!mListener);
  }
}

}  // namespace mozilla::dom::cache
