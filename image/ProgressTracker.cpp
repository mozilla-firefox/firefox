/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ProgressTracker.h"

#include "Image.h"
#include "ImageLogging.h"
#include "imgINotificationObserver.h"
#include "imgIRequest.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/Assertions.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/Services.h"
#include "nsIObserverService.h"
#include "nsNetUtil.h"

using mozilla::WeakPtr;

namespace mozilla {
namespace image {

static void CheckProgressConsistency(Progress /*aOldProgress*/,
                                     Progress aNewProgress, bool aIsMultipart) {
  // Check preconditions for every progress bit.

  // Error's do not get propagated from the tracker for each image part to the
  // tracker for the multipart image because we don't want one bad part to
  // prevent the remaining parts from showing. So we need to consider whether
  // this is a tracker for a multipart image for these assertions to work.

  if (aNewProgress & FLAG_SIZE_AVAILABLE) {
    // No preconditions.
  }
  if (aNewProgress & FLAG_DECODE_COMPLETE) {
    MOZ_ASSERT(aNewProgress & FLAG_SIZE_AVAILABLE);
    MOZ_ASSERT(aIsMultipart ||
               aNewProgress & (FLAG_FRAME_COMPLETE | FLAG_HAS_ERROR));
  }
  if (aNewProgress & FLAG_FRAME_COMPLETE) {
    MOZ_ASSERT(aNewProgress & FLAG_SIZE_AVAILABLE);
  }
  if (aNewProgress & FLAG_LOAD_COMPLETE) {
    MOZ_ASSERT(aIsMultipart ||
               aNewProgress & (FLAG_SIZE_AVAILABLE | FLAG_HAS_ERROR));
  }
  if (aNewProgress & FLAG_IS_ANIMATED) {
    // No preconditions; like FLAG_HAS_TRANSPARENCY, we should normally never
    // discover this *after* FLAG_SIZE_AVAILABLE, but unfortunately some corrupt
    // GIFs may fool us.
  }
  if (aNewProgress & FLAG_HAS_TRANSPARENCY) {
    // XXX We'd like to assert that transparency is only set during metadata
    // decode but we don't have any way to assert that until bug 1254892 is
    // fixed.
  }
  if (aNewProgress & FLAG_LAST_PART_COMPLETE) {
    MOZ_ASSERT(aNewProgress & FLAG_LOAD_COMPLETE);
  }
  if (aNewProgress & FLAG_HAS_ERROR) {
    // No preconditions.
  }
}

ProgressTracker::ProgressTracker()
    : mMutex("ProgressTracker::mMutex"),
      mImage(nullptr),
      mObservers(new ObserverTable),
      mProgress(NoProgress),
      mIsMultipart(false) {}

already_AddRefed<Image> ProgressTracker::GetImage() const {
  MutexAutoLock lock(mMutex);
  RefPtr<Image> image = mImage;
  return image.forget();
}

void ProgressTracker::SetImage(Image* aImage) {
  MutexAutoLock lock(mMutex);
  MOZ_ASSERT(aImage, "Setting null image");
  MOZ_ASSERT(!mImage, "Setting image when we already have one");
  mImage = aImage;
}

void ProgressTracker::ResetImage() {
  MutexAutoLock lock(mMutex);
  MOZ_ASSERT(mImage, "Resetting image when it's already null!");
  mImage = nullptr;
}

uint32_t ProgressTracker::GetImageStatus() const {
  uint32_t status = imgIRequest::STATUS_NONE;

  // Translate our current state to a set of imgIRequest::STATE_* flags.
  if (mProgress & FLAG_SIZE_AVAILABLE) {
    status |= imgIRequest::STATUS_SIZE_AVAILABLE;
  }
  if (mProgress & FLAG_DECODE_COMPLETE) {
    status |= imgIRequest::STATUS_DECODE_COMPLETE;
  }
  if (mProgress & FLAG_FRAME_COMPLETE) {
    status |= imgIRequest::STATUS_FRAME_COMPLETE;
  }
  if (mProgress & FLAG_LOAD_COMPLETE) {
    status |= imgIRequest::STATUS_LOAD_COMPLETE;
  }
  if (mProgress & FLAG_IS_ANIMATED) {
    status |= imgIRequest::STATUS_IS_ANIMATED;
  }
  if (mProgress & FLAG_HAS_TRANSPARENCY) {
    status |= imgIRequest::STATUS_HAS_TRANSPARENCY;
  }
  if (mProgress & FLAG_HAS_ERROR) {
    status |= imgIRequest::STATUS_ERROR;
  }

  return status;
}

// A helper class to allow us to call SyncNotify asynchronously.
class AsyncNotifyRunnable : public Runnable {
 public:
  AsyncNotifyRunnable(ProgressTracker* aTracker, IProgressObserver* aObserver)
      : Runnable("ProgressTracker::AsyncNotifyRunnable"), mTracker(aTracker) {
    MOZ_ASSERT(NS_IsMainThread(), "Should be created on the main thread");
    MOZ_ASSERT(aTracker, "aTracker should not be null");
    MOZ_ASSERT(aObserver, "aObserver should not be null");
    mObservers.AppendElement(aObserver);
  }

  NS_IMETHOD Run() override {
    MOZ_ASSERT(NS_IsMainThread(), "Should be running on the main thread");
    MOZ_ASSERT(mTracker, "mTracker should not be null");
    for (uint32_t i = 0; i < mObservers.Length(); ++i) {
      mObservers[i]->ClearPendingNotify();
      mTracker->SyncNotify(mObservers[i]);
    }

    mTracker->mRunnable = nullptr;
    return NS_OK;
  }

  void AddObserver(IProgressObserver* aObserver) {
    mObservers.AppendElement(aObserver);
  }

  void RemoveObserver(IProgressObserver* aObserver) {
    mObservers.RemoveElement(aObserver);
  }

 private:
  friend class ProgressTracker;

  RefPtr<ProgressTracker> mTracker;
  nsTArray<RefPtr<IProgressObserver>> mObservers;
};

ProgressTracker::RenderBlockingRunnable::RenderBlockingRunnable(
    already_AddRefed<AsyncNotifyRunnable> aEvent)
    : PrioritizableRunnable(std::move(aEvent),
                            nsIRunnablePriority::PRIORITY_RENDER_BLOCKING) {}

void ProgressTracker::RenderBlockingRunnable::AddObserver(
    IProgressObserver* aObserver) {
  static_cast<AsyncNotifyRunnable*>(mRunnable.get())->AddObserver(aObserver);
}

void ProgressTracker::RenderBlockingRunnable::RemoveObserver(
    IProgressObserver* aObserver) {
  static_cast<AsyncNotifyRunnable*>(mRunnable.get())->RemoveObserver(aObserver);
}

/* static */
already_AddRefed<ProgressTracker::RenderBlockingRunnable>
ProgressTracker::RenderBlockingRunnable::Create(
    already_AddRefed<AsyncNotifyRunnable> aEvent) {
  MOZ_ASSERT(NS_IsMainThread());
  RefPtr<ProgressTracker::RenderBlockingRunnable> event(
      new ProgressTracker::RenderBlockingRunnable(std::move(aEvent)));
  return event.forget();
}

void ProgressTracker::Notify(IProgressObserver* aObserver) {
  MOZ_ASSERT(NS_IsMainThread());

  if (aObserver->NotificationsDeferred()) {
    // There is a pending notification, or the observer isn't ready yet.
    return;
  }

  if (MOZ_LOG_TEST(gImgLog, LogLevel::Debug)) {
    RefPtr<Image> image = GetImage();
    LOG_FUNC_WITH_PARAM(gImgLog, "ProgressTracker::Notify async", "uri", image);
  }

  aObserver->MarkPendingNotify();

  // If we have an existing runnable that we can use, we just append this
  // observer to its list of observers to be notified. This ensures we don't
  // unnecessarily delay onload.
  if (mRunnable) {
    mRunnable->AddObserver(aObserver);
  } else if (!AppShutdown::IsInOrBeyond(ShutdownPhase::XPCOMShutdownThreads)) {
    // Avoid dispatch if we are late in shutdown.
    auto ev = MakeRefPtr<AsyncNotifyRunnable>(this, aObserver);
    mRunnable = ProgressTracker::RenderBlockingRunnable::Create(ev.forget());
    SchedulerGroup::Dispatch(do_AddRef(mRunnable));
  }
}

// A helper class to allow us to call SyncNotify asynchronously for a given,
// fixed, state.
class AsyncNotifyCurrentStateRunnable : public Runnable {
 public:
  AsyncNotifyCurrentStateRunnable(ProgressTracker* aProgressTracker,
                                  IProgressObserver* aObserver)
      : Runnable("image::AsyncNotifyCurrentStateRunnable"),
        mProgressTracker(aProgressTracker),
        mObserver(aObserver) {
    MOZ_ASSERT(NS_IsMainThread(), "Should be created on the main thread");
    MOZ_ASSERT(mProgressTracker, "mProgressTracker should not be null");
    MOZ_ASSERT(mObserver, "mObserver should not be null");
    mImage = mProgressTracker->GetImage();
  }

  NS_IMETHOD Run() override {
    MOZ_ASSERT(NS_IsMainThread(), "Should be running on the main thread");
    mObserver->ClearPendingNotify();

    mProgressTracker->SyncNotify(mObserver);
    return NS_OK;
  }

 private:
  RefPtr<ProgressTracker> mProgressTracker;
  RefPtr<IProgressObserver> mObserver;

  // We have to hold on to a reference to the tracker's image, just in case
  // it goes away while we're in the event queue.
  RefPtr<Image> mImage;
};

void ProgressTracker::NotifyCurrentState(IProgressObserver* aObserver) {
  MOZ_ASSERT(NS_IsMainThread());

  if (aObserver->NotificationsDeferred()) {
    // There is a pending notification, or the observer isn't ready yet.
    return;
  }

  if (MOZ_LOG_TEST(gImgLog, LogLevel::Debug)) {
    RefPtr<Image> image = GetImage();
    LOG_FUNC_WITH_PARAM(gImgLog, "ProgressTracker::NotifyCurrentState", "uri",
                        image);
  }

  aObserver->MarkPendingNotify();

  // Avoid dispatch if we are late in shutdown.
  if (!AppShutdown::IsInOrBeyond(ShutdownPhase::XPCOMShutdownThreads)) {
    nsCOMPtr<nsIRunnable> ev =
        new AsyncNotifyCurrentStateRunnable(this, aObserver);
    SchedulerGroup::Dispatch(ev.forget());
  }
}

/**
 * ImageObserverNotifier is a helper type that abstracts over the difference
 * between sending notifications to all of the observers in an ObserverTable,
 * and sending them to a single observer. This allows the same notification code
 * to be used for both cases.
 */
template <typename T>
struct ImageObserverNotifier;

template <>
struct MOZ_STACK_CLASS ImageObserverNotifier<const ObserverTable*> {
  explicit ImageObserverNotifier(const ObserverTable* aObservers,
                                 bool aIgnoreDeferral = false)
      : mObservers(aObservers), mIgnoreDeferral(aIgnoreDeferral) {}

  template <typename Lambda>
  void operator()(Lambda aFunc) {
    for (const auto& weakObserver : mObservers->Values()) {
      RefPtr<IProgressObserver> observer = weakObserver.get();
      if (observer && (mIgnoreDeferral || !observer->NotificationsDeferred())) {
        aFunc(observer);
      }
    }
  }

 private:
  const ObserverTable* mObservers;
  const bool mIgnoreDeferral;
};

template <>
struct MOZ_STACK_CLASS ImageObserverNotifier<IProgressObserver*> {
  explicit ImageObserverNotifier(IProgressObserver* aObserver)
      : mObserver(aObserver) {}

  template <typename Lambda>
  void operator()(Lambda aFunc) {
    if (mObserver && !mObserver->NotificationsDeferred()) {
      aFunc(mObserver);
    }
  }

 private:
  IProgressObserver* mObserver;
};

template <typename T>
void SyncNotifyInternal(const T& aObservers, bool aHasImage, Progress aProgress,
                        const nsIntRect& aDirtyRect) {
  MOZ_ASSERT(NS_IsMainThread());

  typedef imgINotificationObserver I;
  ImageObserverNotifier<T> notify(aObservers);

  // Iterate over the observers only once, delivering every notification implied
  // by aProgress to each observer before moving on to the next one. This keeps
  // the observer (and its listener) hot in cache for the whole batch and
  // resolves each observer's (weak) reference a single time, rather than
  // re-walking the observer list once per progress flag.
  notify([&](IProgressObserver* aObs) {
    if (aProgress & FLAG_SIZE_AVAILABLE) {
      aObs->Notify(I::SIZE_AVAILABLE);
    }

    if (aHasImage) {
      // OnFrameUpdate
      // If there's any content in this frame at all (always true for vector
      // images, true for raster images that have decoded at least one frame)
      // then send OnFrameUpdate.
      if (!aDirtyRect.IsEmpty()) {
        aObs->Notify(I::FRAME_UPDATE, &aDirtyRect);
      }

      if (aProgress & FLAG_FRAME_COMPLETE) {
        aObs->Notify(I::FRAME_COMPLETE);
      }

      if (aProgress & FLAG_HAS_TRANSPARENCY) {
        aObs->Notify(I::HAS_TRANSPARENCY);
      }

      if (aProgress & FLAG_IS_ANIMATED) {
        aObs->Notify(I::IS_ANIMATED);
      }
    }

    if (aProgress & FLAG_DECODE_COMPLETE) {
      MOZ_ASSERT(aHasImage, "Stopped decoding without ever having an image?");
      aObs->Notify(I::DECODE_COMPLETE);
    }

    if (aProgress & FLAG_LOAD_COMPLETE) {
      aObs->OnLoadComplete(aProgress & FLAG_LAST_PART_COMPLETE);
    }
  });
}

void ProgressTracker::SyncNotifyProgress(Progress aProgress,
                                         const nsIntRect& aInvalidRect
                                         /* = nsIntRect() */) {
  MOZ_ASSERT(NS_IsMainThread(), "Use mObservers on main thread only");

  Progress progress = Difference(aProgress);
  CheckProgressConsistency(mProgress, mProgress | progress, mIsMultipart);

  // Apply the changes.
  mProgress |= progress;

  // Send notifications.
  mObservers.Read([&](const ObserverTable* aTable) {
    SyncNotifyInternal(aTable, HasImage(), progress, aInvalidRect);
  });

  if (progress & FLAG_HAS_ERROR) {
    FireFailureNotification();
  }
}

void ProgressTracker::SyncNotify(IProgressObserver* aObserver) {
  MOZ_ASSERT(NS_IsMainThread());

  RefPtr<Image> image = GetImage();
  LOG_SCOPE_WITH_PARAM(gImgLog, "ProgressTracker::SyncNotify", "uri", image);

  nsIntRect rect;
  if (image) {
    int32_t width, height;
    if (NS_FAILED(image->GetWidth(&width)) ||
        NS_FAILED(image->GetHeight(&height))) {
      // Either the image has no intrinsic size, or it has an error.
      rect = GetMaxSizedIntRect();
    } else {
      rect.SizeTo(width, height);
    }
  }

  SyncNotifyInternal(aObserver, !!image, mProgress, rect);
}

void ProgressTracker::EmulateRequestFinished(IProgressObserver* aObserver) {
  MOZ_ASSERT(NS_IsMainThread(),
             "SyncNotifyState and mObservers are not threadsafe");
  RefPtr<IProgressObserver> kungFuDeathGrip(aObserver);

  if (!(mProgress & FLAG_LOAD_COMPLETE)) {
    aObserver->OnLoadComplete(true);
  }
}

void ProgressTracker::AddObserver(IProgressObserver* aObserver) {
  MOZ_ASSERT(NS_IsMainThread());
  RefPtr<IProgressObserver> observer = aObserver;
  mObservers.Write([=](ObserverTable* aTable) {
    MOZ_ASSERT(!aTable->Contains(observer),
               "Adding duplicate entry for image observer");

    WeakPtr<IProgressObserver> weakPtr = observer.get();
    aTable->InsertOrUpdate(observer, weakPtr);
  });
}

bool ProgressTracker::RemoveObserver(IProgressObserver* aObserver,
                                     bool aEmulateRequestFinished) {
  MOZ_ASSERT(NS_IsMainThread());
  RefPtr<IProgressObserver> observer = aObserver;

  // Remove the observer from the list.
  bool removed = mObservers.Write(
      [observer](ObserverTable* aTable) { return aTable->Remove(observer); });

  // Observers can get confused if they don't get all the proper teardown
  // notifications. Part ways on good terms.
  if (removed && aEmulateRequestFinished &&
      !aObserver->NotificationsDeferred()) {
    EmulateRequestFinished(aObserver);
  }

  // Make sure we don't give callbacks to an observer that isn't interested in
  // them any more.
  if (aObserver->NotificationsDeferred() && mRunnable) {
    mRunnable->RemoveObserver(aObserver);
    aObserver->ClearPendingNotify();
  }

  return removed;
}

uint32_t ProgressTracker::ObserverCount() const {
  MOZ_ASSERT(NS_IsMainThread());
  return mObservers.Read(
      [](const ObserverTable* aTable) { return aTable->Count(); });
}

void ProgressTracker::OnUnlockedDraw() {
  MOZ_ASSERT(NS_IsMainThread());
  mObservers.Read([](const ObserverTable* aTable) {
    ImageObserverNotifier<const ObserverTable*> notify(aTable);
    notify([](IProgressObserver* aObs) {
      aObs->Notify(imgINotificationObserver::UNLOCKED_DRAW);
    });
  });
}

void ProgressTracker::ResetForNewRequest() {
  MOZ_ASSERT(NS_IsMainThread());
  mProgress = NoProgress;
}

void ProgressTracker::OnDiscard() {
  MOZ_ASSERT(NS_IsMainThread());
  mObservers.Read([](const ObserverTable* aTable) {
    ImageObserverNotifier<const ObserverTable*> notify(aTable);
    notify([](IProgressObserver* aObs) {
      aObs->Notify(imgINotificationObserver::DISCARD);
    });
  });
}

void ProgressTracker::OnImageAvailable() {
  MOZ_ASSERT(NS_IsMainThread());
  // Notify any imgRequestProxys that are observing us that we have an Image.
  mObservers.Read([](const ObserverTable* aTable) {
    ImageObserverNotifier<const ObserverTable*> notify(
        aTable, /* aIgnoreDeferral = */ true);
    notify([](IProgressObserver* aObs) { aObs->SetHasImage(); });
  });
}

void ProgressTracker::FireFailureNotification() {
  MOZ_ASSERT(NS_IsMainThread());

  // Some kind of problem has happened with image decoding.
  // Report the URI to net:failed-to-process-uri-conent observers.
  RefPtr<Image> image = GetImage();
  if (image) {
    // Should be on main thread, so ok to create a new nsIURI.
    nsCOMPtr<nsIURI> uri = image->GetURI();
    if (uri) {
      nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
      if (os) {
        os->NotifyObservers(uri, "net:failed-to-process-uri-content", nullptr);
      }
    }
  }
}

}  // namespace image
}  // namespace mozilla
