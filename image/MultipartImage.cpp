/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MultipartImage.h"

#include "imgINotificationObserver.h"

namespace mozilla {

using gfx::IntSize;
using gfx::SourceSurface;

namespace image {

///////////////////////////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////////////////////////

class NextPartObserver : public IProgressObserver {
 public:
  MOZ_DECLARE_REFCOUNTED_TYPENAME(NextPartObserver)
  NS_INLINE_DECL_REFCOUNTING(NextPartObserver, override)

  explicit NextPartObserver(MultipartImage* aOwner) : mOwner(aOwner) {
    MOZ_ASSERT(mOwner);
  }

  void BeginObserving(Image* aImage) {
    MOZ_ASSERT(aImage);
    mImage = aImage;

    RefPtr<ProgressTracker> tracker = mImage->GetProgressTracker();
    tracker->AddObserver(this);
  }

  void BlockUntilDecodedAndFinishObserving() {
    // Use RequestDecodeForSize() to block until our image finishes decoding.
    // The size is ignored because we don't pass the FLAG_HIGH_QUALITY_SCALING
    // flag.
    mImage->RequestDecodeForSize(gfx::IntSize(0, 0),
                                 imgIContainer::FLAG_SYNC_DECODE);

    // RequestDecodeForSize() should've sent synchronous notifications that
    // would have caused us to call FinishObserving() (and null out mImage)
    // already. If for some reason it didn't, we should do so here.
    if (mImage) {
      FinishObserving();
    }
  }

  virtual void Notify(int32_t aType,
                      const nsIntRect* aRect = nullptr) override {
    if (!mImage) {
      // We've already finished observing the last image we were given.
      return;
    }

    if (aType == imgINotificationObserver::FRAME_COMPLETE) {
      FinishObserving();
    }
  }

  virtual void OnLoadComplete(bool aLastPart) override {
    if (!mImage) {
      // We've already finished observing the last image we were given.
      return;
    }

    // Retrieve the image's intrinsic size.
    int32_t width = 0;
    int32_t height = 0;
    mImage->GetWidth(&width);
    mImage->GetHeight(&height);

    // Request decoding at the intrinsic size.
    mImage->RequestDecodeForSize(IntSize(width, height),
                                 imgIContainer::DECODE_FLAGS_DEFAULT |
                                     imgIContainer::FLAG_HIGH_QUALITY_SCALING);

    // If there's already an error, we may never get a FRAME_COMPLETE
    // notification, so go ahead and notify our owner right away.
    RefPtr<ProgressTracker> tracker = mImage->GetProgressTracker();
    if (tracker->GetProgress() & FLAG_HAS_ERROR) {
      FinishObserving();
    }
  }

  // Other notifications are ignored.
  virtual void SetHasImage() override {}
  virtual bool NotificationsDeferred() const override { return false; }
  virtual void MarkPendingNotify() override {}
  virtual void ClearPendingNotify() override {}

 private:
  virtual ~NextPartObserver() {}

  void FinishObserving() {
    MOZ_ASSERT(mImage);

    RefPtr<ProgressTracker> tracker = mImage->GetProgressTracker();
    tracker->RemoveObserver(this);
    mImage = nullptr;

    mOwner->FinishTransition();
  }

  MultipartImage* mOwner;
  RefPtr<Image> mImage;
};

///////////////////////////////////////////////////////////////////////////////
// Implementation
///////////////////////////////////////////////////////////////////////////////

MultipartImage::MultipartImage(Image* aFirstPart)
    : ImageWrapper(aFirstPart), mPendingNotify(false) {
  mNextPartObserver = new NextPartObserver(this);
}

void MultipartImage::Init() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mTracker, "Should've called SetProgressTracker() by now");

  // Start observing the first part.
  RefPtr<ProgressTracker> firstPartTracker = InnerImage()->GetProgressTracker();
  firstPartTracker->AddObserver(this);
  InnerImage()->IncrementAnimationConsumers();
}

MultipartImage::~MultipartImage() {
  // Ask our ProgressTracker to drop its weak reference to us.
  mTracker->ResetImage();
}

NS_IMPL_ISUPPORTS_INHERITED0(MultipartImage, ImageWrapper)

void MultipartImage::BeginTransitionToPart(Image* aNextPart) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aNextPart);

  if (mNextPart) {
    // Let the decoder catch up so we don't drop frames.
    mNextPartObserver->BlockUntilDecodedAndFinishObserving();
    MOZ_ASSERT(!mNextPart);
  }

  mNextPart = aNextPart;

  // Start observing the next part; we'll complete the transition when
  // NextPartObserver calls FinishTransition.
  mNextPartObserver->BeginObserving(mNextPart);
  mNextPart->IncrementAnimationConsumers();
}

static Progress FilterProgress(Progress aProgress) {
  // Filter out onload blocking notifications, since we don't want to block
  // onload for multipart images.
  // Filter out errors, since we don't want errors in one part to error out
  // the whole stream.
  return aProgress & ~FLAG_HAS_ERROR;
}

void MultipartImage::FinishTransition() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mNextPart, "Should have a next part here");

  RefPtr<ProgressTracker> newCurrentPartTracker =
      mNextPart->GetProgressTracker();
  if (newCurrentPartTracker->GetProgress() & FLAG_HAS_ERROR) {
    // This frame has an error; drop it.
    mNextPart = nullptr;

    // We still need to notify, though.
    mTracker->ResetForNewRequest();
    RefPtr<ProgressTracker> currentPartTracker =
        InnerImage()->GetProgressTracker();
    mTracker->SyncNotifyProgress(
        FilterProgress(currentPartTracker->GetProgress()));

    return;
  }

  // Stop observing the current part.
  {
    RefPtr<ProgressTracker> currentPartTracker =
        InnerImage()->GetProgressTracker();
    currentPartTracker->RemoveObserver(this);
  }

  // Make the next part become the current part.
  mTracker->ResetForNewRequest();
  SetInnerImage(mNextPart);
  mNextPart = nullptr;
  newCurrentPartTracker->AddObserver(this);

  // Finally, send all the notifications for the new current part and send a
  // FRAME_UPDATE notification so that observers know to redraw.
  mTracker->SyncNotifyProgress(
      FilterProgress(newCurrentPartTracker->GetProgress()),
      GetMaxSizedIntRect());
}

already_AddRefed<imgIContainer> MultipartImage::Unwrap() {
  // Although we wrap another image, we don't allow callers to unwrap as. As far
  // as external code is concerned, MultipartImage is atomic.
  nsCOMPtr<imgIContainer> image = this;
  return image.forget();
}

already_AddRefed<ProgressTracker> MultipartImage::GetProgressTracker() {
  MOZ_ASSERT(mTracker);
  RefPtr<ProgressTracker> tracker = mTracker;
  return tracker.forget();
}

void MultipartImage::SetProgressTracker(ProgressTracker* aTracker) {
  MOZ_ASSERT(aTracker);
  MOZ_ASSERT(!mTracker);
  mTracker = aTracker;
}

nsresult MultipartImage::OnImageDataAvailable(nsIRequest* aRequest,
                                              nsIInputStream* aInStr,
                                              uint64_t aSourceOffset,
                                              uint32_t aCount) {
  // Note that this method is special in that we forward it to the next part if
  // one exists, and *not* the current part.

  // We may trigger notifications that will free the part, so keep it alive.
  RefPtr<Image> part = mNextPart ? mNextPart.get() : InnerImage();
  part->OnImageDataAvailable(aRequest, aInStr, aSourceOffset, aCount);

  return NS_OK;
}

nsresult MultipartImage::OnImageDataComplete(nsIRequest* aRequest,
                                             nsresult aStatus, bool aLastPart) {
  // Note that this method is special in that we forward it to the next part if
  // one exists, and *not* the current part.

  // We may trigger notifications that will free the part, so keep it alive.
  RefPtr<Image> part = mNextPart ? mNextPart.get() : InnerImage();
  part->OnImageDataComplete(aRequest, aStatus, aLastPart);

  return NS_OK;
}

void MultipartImage::Notify(int32_t aType,
                            const nsIntRect* aRect /* = nullptr*/) {
  if (aType == imgINotificationObserver::SIZE_AVAILABLE) {
    mTracker->SyncNotifyProgress(FLAG_SIZE_AVAILABLE);
  } else if (aType == imgINotificationObserver::FRAME_UPDATE) {
    mTracker->SyncNotifyProgress(NoProgress, *aRect);
  } else if (aType == imgINotificationObserver::FRAME_COMPLETE) {
    mTracker->SyncNotifyProgress(FLAG_FRAME_COMPLETE);
  } else if (aType == imgINotificationObserver::LOAD_COMPLETE) {
    mTracker->SyncNotifyProgress(FLAG_LOAD_COMPLETE);
  } else if (aType == imgINotificationObserver::DECODE_COMPLETE) {
    mTracker->SyncNotifyProgress(FLAG_DECODE_COMPLETE);
  } else if (aType == imgINotificationObserver::DISCARD) {
    mTracker->OnDiscard();
  } else if (aType == imgINotificationObserver::UNLOCKED_DRAW) {
    mTracker->OnUnlockedDraw();
  } else if (aType == imgINotificationObserver::IS_ANIMATED) {
    mTracker->SyncNotifyProgress(FLAG_IS_ANIMATED);
  } else if (aType == imgINotificationObserver::HAS_TRANSPARENCY) {
    mTracker->SyncNotifyProgress(FLAG_HAS_TRANSPARENCY);
  } else {
    MOZ_ASSERT_UNREACHABLE("Notification list should be exhaustive");
  }
}

void MultipartImage::OnLoadComplete(bool aLastPart) {
  Progress progress = FLAG_LOAD_COMPLETE;
  if (aLastPart) {
    progress |= FLAG_LAST_PART_COMPLETE;
  }
  mTracker->SyncNotifyProgress(progress);
}

void MultipartImage::SetHasImage() { mTracker->OnImageAvailable(); }

bool MultipartImage::NotificationsDeferred() const { return mPendingNotify; }

void MultipartImage::MarkPendingNotify() { mPendingNotify = true; }

void MultipartImage::ClearPendingNotify() { mPendingNotify = false; }

}  // namespace image
}  // namespace mozilla
