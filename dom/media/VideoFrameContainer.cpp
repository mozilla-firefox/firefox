/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "VideoFrameContainer.h"

#include "mozilla/Logging.h"

#ifdef MOZ_WIDGET_ANDROID
#  include "GLImages.h"  // for SurfaceTextureImage
#endif
#include "MediaDecoderOwner.h"
#include "mozilla/AbstractThread.h"
#include "mozilla/gfx/BuildConstants.h"
#include "mozilla/gfx/gfxVars.h"

using namespace mozilla::layers;

mozilla::LazyLogModule gVideoFrameContainer("VideoFrameContainer");

namespace mozilla {
#define NS_DispatchToMainThread(...) CompileError_UseAbstractMainThreadInstead

VideoFrameContainer::VideoFrameContainer(
    MediaDecoderOwner* aOwner, already_AddRefed<ImageContainer> aContainer)
    : mOwner(aOwner),
      mImageContainer(aContainer),
      mMutex("nsVideoFrameContainer"),
      mFrameID(0),
      mLastPrincipalHandle(PRINCIPAL_HANDLE_NONE),
      mPendingPrincipalHandle(PRINCIPAL_HANDLE_NONE),
      mFrameIDForPendingPrincipalHandle(0),
      mMainThread(aOwner->AbstractMainThread()),
      mSupportsOnly8BitImage(kIsAndroid &&
                             !gfx::gfxVars::AllowGLNorm16Textures()) {
  NS_ASSERTION(aOwner, "aOwner must not be null");
  NS_ASSERTION(mImageContainer, "aContainer must not be null");
}

VideoFrameContainer::~VideoFrameContainer() = default;

PrincipalHandle VideoFrameContainer::GetLastPrincipalHandle() {
  MutexAutoLock lock(mMutex);
  return GetLastPrincipalHandleLocked();
}

PrincipalHandle VideoFrameContainer::GetLastPrincipalHandleLocked() {
  return mLastPrincipalHandle;
}

void VideoFrameContainer::UpdatePrincipalHandleForFrameID(
    const PrincipalHandle& aPrincipalHandle,
    const ImageContainer::FrameID& aFrameID) {
  MutexAutoLock lock(mMutex);
  UpdatePrincipalHandleForFrameIDLocked(aPrincipalHandle, aFrameID);
}

void VideoFrameContainer::UpdatePrincipalHandleForFrameIDLocked(
    const PrincipalHandle& aPrincipalHandle,
    const ImageContainer::FrameID& aFrameID) {
  if (mPendingPrincipalHandle == aPrincipalHandle) {
    return;
  }
  mPendingPrincipalHandle = aPrincipalHandle;
  mFrameIDForPendingPrincipalHandle = aFrameID;
}

#ifdef MOZ_WIDGET_ANDROID
static void NotifySetCurrent(Image* aImage) {
  if (aImage) {
    MOZ_LOG_FMT(gVideoFrameContainer, LogLevel::Debug,
                "NotifySetCurrent, serial={}", aImage->GetSerial());
    aImage->OnSetCurrent();
  }
}
#endif

void VideoFrameContainer::SetCurrentFrame(
    const gfx::IntSize& aIntrinsicSize, Image* aImage,
    const TimeStamp& aTargetTime, const media::TimeUnit& aProcessingDuration,
    const media::TimeUnit& aMediaTime) {
  MOZ_LOG_FMT(
      gVideoFrameContainer, LogLevel::Debug,
      "SetCurrentFrame, processing duration={}us,pts={}",
      aProcessingDuration.IsValid() ? aProcessingDuration.ToMicroseconds() : -1,
      aMediaTime.ToString());
#ifdef MOZ_WIDGET_ANDROID
  NotifySetCurrent(aImage);
#endif
  AutoTArray<ImageContainer::NonOwningImage, 1> imageList;
  if (aImage) {
    imageList.AppendElement(ImageContainer::NonOwningImage(
        aImage, aTargetTime, ++mFrameID, 0, aProcessingDuration, aMediaTime));
  }
  MutexAutoLock lock(mMutex);
  SetCurrentFramesLocked(aIntrinsicSize, imageList);
}

void VideoFrameContainer::SetCurrentFrames(
    const gfx::IntSize& aIntrinsicSize,
    const nsTArray<ImageContainer::NonOwningImage>& aImages) {
  MOZ_LOG_FMT(gVideoFrameContainer, LogLevel::Debug,
              "SetCurrentFrames ({} images)", aImages.Length());
#ifdef MOZ_WIDGET_ANDROID
  // When there are multiple frames, only the last one is effective
  // (see bug 1299068 comment 4). Here I just count on VideoSink and VideoOutput
  // to send one frame at a time and warn if not.
  (void)NS_WARN_IF(aImages.Length() > 1);
  for (auto& image : aImages) {
    NotifySetCurrent(image.mImage);
  }
#endif
  MutexAutoLock lock(mMutex);
  SetCurrentFramesLocked(aIntrinsicSize, aImages);
}

#ifdef DEBUG
static bool Is8BitImage(const ImageContainer::NonOwningImage& aFrame) {
  return aFrame.mImage->GetColorDepth() == gfx::ColorDepth::COLOR_8;
}
#endif

void VideoFrameContainer::SetCurrentFramesLocked(
    const gfx::IntSize& aIntrinsicSize,
    const nsTArray<ImageContainer::NonOwningImage>& aImages) {
  MOZ_LOG_FMT(gVideoFrameContainer, LogLevel::Debug,
              "SetCurrentFramesLocked ({} images", aImages.Length());
  mMutex.AssertCurrentThreadOwns();

  MOZ_ASSERT(!SupportsOnly8BitImage() ||
                 std::all_of(aImages.begin(), aImages.end(), Is8BitImage),
             "Images should be 8-bit");

  Maybe<gfx::IntSize> newIntrinsicSize;
  if (auto size = Some(aIntrinsicSize); size != mIntrinsicSize) {
    mIntrinsicSize = size;
    newIntrinsicSize = size;
  }

  // Unlike intrinsic size, for a realtime (e.g. WebRTC) source rotation has
  // no independent out-of-band source: it can only come from an image.
  // (Playback differs -- there, rotation comes from container metadata via
  // VideoInfo, independently of any image.) An empty image set here (e.g. a
  // track pausing) carries no rotation information of its own, so it must
  // not be treated as "rotation is now 0" -- that would force a spurious
  // reflow/resize on the video element for frames simply stopping, not for
  // an actual orientation change.
  Maybe<VideoRotation> newRotation;
  if (!aImages.IsEmpty()) {
    Maybe<VideoRotation> rotation = aImages[0].mRotation;
    // Compare effective rotation values, and not the raw Maybes directly --
    // Nothing() and Some(kDegree_0) are equivalent, so switching between
    // them should not count as a change.
    if (rotation.valueOr(VideoRotation::kDegree_0) !=
        mRotation.valueOr(VideoRotation::kDegree_0)) {
      newRotation = Some(rotation.valueOr(VideoRotation::kDegree_0));
    }
    mRotation = rotation;
  }

  if (newIntrinsicSize || newRotation) {
    mMainThread->Dispatch(NS_NewRunnableFunction(
        "IntrinsicSizeOrRotationChanged",
        [this, self = RefPtr(this), newIntrinsicSize, newRotation]() {
          if (newIntrinsicSize) {
            mMainThreadState.mNewIntrinsicSize = newIntrinsicSize;
          }
          if (newRotation) {
            mMainThreadState.mNewRotation = newRotation;
          }
        }));
  }

  gfx::IntSize oldFrameSize = mImageContainer->GetCurrentSize();

  // When using the OMX decoder, destruction of the current image can indirectly
  //  block on main thread I/O. If we let this happen while holding onto
  //  |mImageContainer|'s lock, then when the main thread then tries to
  //  composite it can then block on |mImageContainer|'s lock, causing a
  //  deadlock. We use this hack to defer the destruction of the current image
  //  until it is safe.
  nsTArray<ImageContainer::OwningImage> oldImages;
  mImageContainer->GetCurrentImages(&oldImages);

  PrincipalHandle principalHandle = PRINCIPAL_HANDLE_NONE;
  if (mPendingPrincipalHandle != PRINCIPAL_HANDLE_NONE &&
      (aImages.IsEmpty() ||
       aImages[0].mFrameID >= mFrameIDForPendingPrincipalHandle)) {
    // There are no FrameIDs prior to `mFrameIDForPendingPrincipalHandle`
    // in the new set of images.
    // This means that the old principal handle has been flushed out and we
    // can notify our video element about this change.
    principalHandle = mPendingPrincipalHandle;
    mLastPrincipalHandle = mPendingPrincipalHandle;
    mPendingPrincipalHandle = PRINCIPAL_HANDLE_NONE;
    mFrameIDForPendingPrincipalHandle = 0;
  }

  if (aImages.IsEmpty()) {
    mImageContainer->ClearImagesInHost(layers::ClearImagesType::All);
  } else {
    mImageContainer->SetCurrentImages(aImages);
  }
  gfx::IntSize newFrameSize = mImageContainer->GetCurrentSize();
  bool imageSizeChanged =
      (oldFrameSize != newFrameSize) || newRotation.isSome();

  if (principalHandle != PRINCIPAL_HANDLE_NONE || imageSizeChanged) {
    RefPtr<VideoFrameContainer> self = this;
    mMainThread->Dispatch(NS_NewRunnableFunction(
        "PrincipalHandleOrImageSizeChanged",
        [this, self, principalHandle, imageSizeChanged]() {
          mMainThreadState.mImageSizeChanged = imageSizeChanged;
          if (mOwner && principalHandle != PRINCIPAL_HANDLE_NONE) {
            mOwner->PrincipalHandleChangedForVideoFrameContainer(
                this, principalHandle);
          }
        }));
  }
}

void VideoFrameContainer::ClearFutureFrames(TimeStamp aNow) {
  MutexAutoLock lock(mMutex);

  MOZ_LOG_FMT(gVideoFrameContainer, LogLevel::Debug, "ClearFutureFrame");
  // See comment in SetCurrentFrame for the reasoning behind
  // using a kungFuDeathGrip here.
  AutoTArray<ImageContainer::OwningImage, 10> kungFuDeathGrip;
  mImageContainer->GetCurrentImages(&kungFuDeathGrip);

  if (!kungFuDeathGrip.IsEmpty()) {
    AutoTArray<ImageContainer::NonOwningImage, 1> currentFrame;
    const ImageContainer::OwningImage* img = &kungFuDeathGrip[0];
    // Find the current image in case there are several.
    for (const auto& image : kungFuDeathGrip) {
      if (image.mTimeStamp > aNow) {
        break;
      }
      img = &image;
    }
    currentFrame.AppendElement(ImageContainer::NonOwningImage(
        img->mImage, img->mTimeStamp, img->mFrameID, img->mProducerID,
        img->mProcessingDuration, img->mMediaTime, img->mWebrtcCaptureTime,
        img->mWebrtcReceiveTime, img->mRtpTimestamp, img->mRotation));
    mImageContainer->SetCurrentImages(currentFrame);
  }
}

void VideoFrameContainer::ClearCachedResources() {
  MutexAutoLock lock(mMutex);
  mImageContainer->ClearCachedResources();
}

void VideoFrameContainer::ClearImagesInHost(layers::ClearImagesType aType) {
  MutexAutoLock lock(mMutex);
  mImageContainer->ClearImagesInHost(aType);
}

ImageContainer* VideoFrameContainer::GetImageContainer() {
  // Note - you'll need the lock to manipulate this.  The pointer is not
  // modified from multiple threads, just the data pointed to by it.
  return mImageContainer;
}

double VideoFrameContainer::GetFrameDelay() {
  MutexAutoLock lock(mMutex);
  return mImageContainer->GetPaintDelay().ToSeconds();
}

void VideoFrameContainer::InvalidateWithFlags(uint32_t aFlags) {
  NS_ASSERTION(NS_IsMainThread(), "Must call on main thread");

  if (!mOwner) {
    // Owner has been destroyed
    return;
  }

  MediaDecoderOwner::ImageSizeChanged imageSizeChanged{
      mMainThreadState.mImageSizeChanged};
  mMainThreadState.mImageSizeChanged = false;

  auto newIntrinsicSize = std::move(mMainThreadState.mNewIntrinsicSize);
  auto newRotation = std::move(mMainThreadState.mNewRotation);

  MediaDecoderOwner::ForceInvalidate forceInvalidate{
      (aFlags & INVALIDATE_FORCE) != 0};

  mOwner->Invalidate(imageSizeChanged, newIntrinsicSize, newRotation,
                     forceInvalidate);
}

}  // namespace mozilla

#undef NS_DispatchToMainThread
