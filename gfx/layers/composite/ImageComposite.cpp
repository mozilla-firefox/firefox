/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageComposite.h"

#include <inttypes.h>

#include "gfxPlatform.h"
#include "mozilla/ProfilerMarkers.h"
#include "nsPrintfCString.h"

namespace mozilla {

using namespace gfx;

namespace layers {

/* static */ const int32_t ImageComposite::BIAS_TIME_MS = 1;

ImageComposite::ImageComposite() = default;

ImageComposite::~ImageComposite() = default;

TimeStamp ImageComposite::GetBiasedTime(const TimeStamp& aInput) const {
  switch (mBias) {
    case ImageComposite::BIAS_NEGATIVE:
      return aInput - TimeDuration::FromMilliseconds(BIAS_TIME_MS);
    case ImageComposite::BIAS_POSITIVE:
      return aInput + TimeDuration::FromMilliseconds(BIAS_TIME_MS);
    default:
      return aInput;
  }
}

void ImageComposite::UpdateBias(size_t aImageIndex, bool aFrameChanged) {
  MOZ_ASSERT(aImageIndex < ImagesCount());

  TimeStamp compositionTime = GetCompositionTime();
  TimeStamp compositedImageTime = mImages[aImageIndex].mTimeStamp;
  TimeStamp nextImageTime = aImageIndex + 1 < ImagesCount()
                                ? mImages[aImageIndex + 1].mTimeStamp
                                : TimeStamp();

  if (profiler_thread_is_being_profiled_for_markers() && compositedImageTime &&
      nextImageTime) {
    TimeDuration offsetCurrent = compositedImageTime - compositionTime;
    TimeDuration offsetNext = nextImageTime - compositionTime;
    nsPrintfCString str("current %.2lfms, next %.2lfms",
                        offsetCurrent.ToMilliseconds(),
                        offsetNext.ToMilliseconds());
    PROFILER_MARKER_TEXT("Video frame offsets", GRAPHICS, {}, str);
  }

  if (compositedImageTime.IsNull()) {
    mBias = ImageComposite::BIAS_NONE;
    return;
  }
  TimeDuration threshold = TimeDuration::FromMicroseconds(1500);
  if (compositionTime - compositedImageTime < threshold &&
      compositionTime - compositedImageTime > -threshold) {
    // The chosen frame's time is very close to the composition time (probably
    // just before the current composition time, but due to previously set
    // negative bias, it could be just after the current composition time too).
    // If the inter-frame time is almost exactly equal to (a multiple of)
    // the inter-composition time, then we're in a dangerous situation because
    // jitter might cause frames to fall one side or the other of the
    // composition times, causing many frames to be skipped or duplicated.
    // Try to prevent that by adding a negative bias to the frame times during
    // the next composite; that should ensure the next frame's time is treated
    // as falling just before a composite time.
    mBias = ImageComposite::BIAS_NEGATIVE;
    return;
  }
  if (!nextImageTime.IsNull() && nextImageTime - compositionTime < threshold &&
      nextImageTime - compositionTime > -threshold) {
    // The next frame's time is very close to our composition time (probably
    // just after the current composition time, but due to previously set
    // positive bias, it could be just before the current composition time too).
    // We're in a dangerous situation because jitter might cause frames to
    // fall one side or the other of the composition times, causing many frames
    // to be skipped or duplicated.
    // Specifically, the next composite is at risk of picking the "next + 1"
    // frame rather than the "next" frame, which would cause the "next" frame to
    // be skipped. Try to prevent that by adding a positive bias to the frame
    // times during the next composite; if the inter-frame time is almost
    // exactly equal to the inter-composition time, that should ensure that the
    // next + 1 frame falls just *after* the next composition time, and the next
    // composite should then pick the next frame rather than the next + 1 frame.
    mBias = ImageComposite::BIAS_POSITIVE;
    return;
  }
  if (aFrameChanged) {
    // The current and next video frames are a sufficient distance from the
    // composition time and we can reliably pick the right frame without bias.
    // Reset the bias.
    // We only do this when the frame changed. Otherwise, when playing a 30fps
    // video on a 60fps display, we'd keep resetting the bias during the "middle
    // frames".
    mBias = ImageComposite::BIAS_NONE;
  }
}

int ImageComposite::ChooseImageIndex() {
  // ChooseImageIndex is called for all images in the layer when it is visible.
  // Change to this behaviour would break dropped frames counting calculation:
  // We rely on this assumption to determine if during successive runs an
  // image is returned that isn't the one following immediately the previous one
  if (mImages.IsEmpty()) {
    return -1;
  }

  TimeStamp compositionTime = GetCompositionTime();
  auto compositionOpportunityId = GetCompositionOpportunityId();
  if (compositionTime &&
      compositionOpportunityId != mLastChooseImageIndexComposition) {
    // We are inside a composition, in the first call to ChooseImageIndex during
    // this composition.
    // Find the newest frame whose biased timestamp is at or before
    // `compositionTime`.
    uint32_t imageIndex = 0;
    while (imageIndex + 1 < mImages.Length() &&
           mImages[imageIndex + 1].mTextureHost->IsValid() &&
           GetBiasedTime(mImages[imageIndex + 1].mTimeStamp) <=
               compositionTime) {
      ++imageIndex;
    }

    if (!mImages[imageIndex].mTextureHost->IsValid()) {
      // Still not ready to be shown.
      return -1;
    }

    bool wasVisibleAtPreviousComposition =
        compositionOpportunityId == mLastChooseImageIndexComposition.Next();

    bool frameChanged =
        UpdateCompositedFrame(imageIndex, wasVisibleAtPreviousComposition);
    UpdateBias(imageIndex, frameChanged);

    mLastChooseImageIndexComposition = compositionOpportunityId;

    return imageIndex;
  }

  // We've been called before during this composition, or we're not in a
  // composition. Just return the last image we picked (if it's one of the
  // current images).
  for (uint32_t i = 0; i < mImages.Length(); ++i) {
    if (mImages[i].mFrameID == mLastFrameID &&
        mImages[i].mProducerID == mLastProducerID) {
      return i;
    }
  }

  return 0;
}

const ImageComposite::TimedImage* ImageComposite::ChooseImage() {
  int index = ChooseImageIndex();
  return index >= 0 ? &mImages[index] : nullptr;
}

void ImageComposite::RemoveImagesWithTextureHost(TextureHost* aTexture) {
  for (int32_t i = mImages.Length() - 1; i >= 0; --i) {
    if (mImages[i].mTextureHost == aTexture) {
      mImages.RemoveElementAt(i);
    }
  }
}

void ImageComposite::ClearImages() { mImages.Clear(); }

void ImageComposite::SetImages(nsTArray<TimedImage>&& aNewImages) {
  if (!aNewImages.IsEmpty()) {
    DetectTimeStampJitter(&aNewImages[0]);

    // Frames older than the first frame in aNewImages that we haven't shown yet
    // will never be shown.
    CountSkippedFrames(&aNewImages[0]);

    if (profiler_thread_is_being_profiled_for_markers()) {
      int len = aNewImages.Length();
      const auto& first = aNewImages[0];
      const auto& last = aNewImages.LastElement();
      nsPrintfCString str("%d %s, frameID %" PRId32 " (prod %" PRId32
                          ") to frameID %" PRId32 " (prod %" PRId32 ")",
                          len, len == 1 ? "image" : "images", first.mFrameID,
                          first.mProducerID, last.mFrameID, last.mProducerID);
      PROFILER_MARKER_TEXT("ImageComposite::SetImages", GRAPHICS, {}, str);
    }
  }
  mImages = std::move(aNewImages);
}

// Returns whether the frame changed.
bool ImageComposite::UpdateCompositedFrame(
    int aImageIndex, bool aWasVisibleAtPreviousComposition) {
  MOZ_RELEASE_ASSERT(aImageIndex >= 0);
  MOZ_RELEASE_ASSERT(aImageIndex < static_cast<int>(mImages.Length()));
  const TimedImage& image = mImages[aImageIndex];

  auto compositionOpportunityId = GetCompositionOpportunityId();
  TimeStamp compositionTime = GetCompositionTime();
  MOZ_RELEASE_ASSERT(compositionTime,
                     "Should only be called during a composition");

  nsCString descr;
  if (profiler_thread_is_being_profiled_for_markers()) {
    nsCString relativeTimeString;
    if (image.mTimeStamp) {
      relativeTimeString.AppendPrintf(
          " [relative timestamp %.1lfms]",
          (image.mTimeStamp - compositionTime).ToMilliseconds());
    }
    int remainingImages = mImages.Length() - 1 - aImageIndex;
    static const char* kBiasStrings[] = {"NONE", "NEGATIVE", "POSITIVE"};
    descr.AppendPrintf(
        "frameID %" PRId32 " (producerID %" PRId32 ") [composite %" PRIu64
        "] [bias %s] [%d remaining %s]%s",
        image.mFrameID, image.mProducerID, compositionOpportunityId.mId,
        kBiasStrings[mBias], remainingImages,
        remainingImages == 1 ? "image" : "images", relativeTimeString.get());
    if (mLastProducerID != image.mProducerID) {
      descr.AppendPrintf(", previous producerID: %" PRId32, mLastProducerID);
    } else if (mLastFrameID != image.mFrameID) {
      descr.AppendPrintf(", previous frameID: %" PRId32, mLastFrameID);
    } else {
      descr.AppendLiteral(", no change");
    }
  }
  PROFILER_MARKER_TEXT("UpdateCompositedFrame", GRAPHICS, {}, descr);

  if (mLastFrameID == image.mFrameID && mLastProducerID == image.mProducerID) {
    // The frame didn't change.
    return false;
  }

  CountSkippedFrames(&image);

  int32_t dropped = mSkippedFramesSinceLastComposite;
  mSkippedFramesSinceLastComposite = 0;

  if (!aWasVisibleAtPreviousComposition) {
    // This video was not part of the on-screen scene during the previous
    // composition opportunity, for example it may have been scrolled off-screen
    // or in a background tab, or compositing might have been paused.
    // Ignore any skipped frames and don't count them as dropped.
    dropped = 0;
  }

  if (dropped > 0) {
    mDroppedFrames += dropped;
    if (profiler_thread_is_being_profiled_for_markers()) {
      const char* frameOrFrames = dropped == 1 ? "frame" : "frames";
      nsPrintfCString text("%" PRId32 " %s dropped: %" PRId32 " -> %" PRId32
                           " (producer %" PRId32 ")",
                           dropped, frameOrFrames, mLastFrameID, image.mFrameID,
                           mLastProducerID);
      PROFILER_MARKER_TEXT("Video frames dropped", GRAPHICS, {}, text);
    }
  }

  mLastFrameID = image.mFrameID;
  mLastProducerID = image.mProducerID;
  mLastFrameUpdateComposition = compositionOpportunityId;

  return true;
}

void ImageComposite::OnFinishRendering(int aImageIndex,
                                       const TimedImage* aImage,
                                       base::ProcessId aProcessId,
                                       const CompositableHandle& aHandle) {
  if (mLastFrameUpdateComposition != GetCompositionOpportunityId()) {
    // The frame did not change in this composition.
    return;
  }

  if (aHandle) {
    ImageCompositeNotificationInfo info;
    info.mImageBridgeProcessId = aProcessId;
    info.mNotification = ImageCompositeNotification(
        aHandle, aImage->mTimeStamp, GetCompositionTime(), mLastFrameID,
        mLastProducerID);
    AppendImageCompositeNotification(info);
  }
}

const ImageComposite::TimedImage* ImageComposite::GetImage(
    size_t aIndex) const {
  if (aIndex >= mImages.Length()) {
    return nullptr;
  }
  return &mImages[aIndex];
}

void ImageComposite::CountSkippedFrames(const TimedImage* aImage) {
  if (aImage->mProducerID != mLastProducerID) {
    // Switched producers.
    return;
  }

  if (mImages.IsEmpty() || aImage->mFrameID <= mLastFrameID + 1) {
    // No frames were skipped.
    return;
  }

  uint32_t targetFrameRate = gfxPlatform::TargetFrameRate();
  if (targetFrameRate == 0) {
    // Can't know whether we could have reasonably displayed all video frames.
    return;
  }

  double targetFrameDurationMS = 1000.0 / targetFrameRate;

  // Count how many images in mImages were skipped between mLastFrameID and
  // aImage.mFrameID. Only count frames for which we can estimate a duration by
  // looking at the next frame's timestamp, and only if the video frame rate is
  // no faster than the target frame rate.
  int32_t skipped = 0;
  for (size_t i = 0; i + 1 < mImages.Length(); i++) {
    const auto& img = mImages[i];
    if (img.mProducerID != aImage->mProducerID ||
        img.mFrameID <= mLastFrameID || img.mFrameID >= aImage->mFrameID) {
      continue;
    }

    // We skipped img! Estimate img's time duration.
    const auto& next = mImages[i + 1];
    if (next.mProducerID != aImage->mProducerID) {
      continue;
    }

    MOZ_ASSERT(next.mFrameID > img.mFrameID);
    TimeDuration duration = next.mTimeStamp - img.mTimeStamp;
    if (floor(duration.ToMilliseconds()) >= floor(targetFrameDurationMS)) {
      // Count the frame.
      skipped++;
    }
  }

  mSkippedFramesSinceLastComposite += skipped;
}

void ImageComposite::DetectTimeStampJitter(const TimedImage* aNewImage) {
  if (!profiler_thread_is_being_profiled_for_markers() ||
      aNewImage->mTimeStamp.IsNull()) {
    return;
  }

  // Find aNewImage in mImages and compute its timestamp delta, if found.
  // Ideally, a given video frame should never change its timestamp (jitter
  // should be zero). However, we re-adjust video frame timestamps based on the
  // audio clock. If the audio clock drifts compared to the system clock, or if
  // there are bugs or inaccuracies in the computation of these timestamps,
  // jitter will be non-zero.
  Maybe<TimeDuration> jitter;
  for (const auto& img : mImages) {
    if (img.mProducerID == aNewImage->mProducerID &&
        img.mFrameID == aNewImage->mFrameID) {
      if (!img.mTimeStamp.IsNull()) {
        jitter = Some(aNewImage->mTimeStamp - img.mTimeStamp);
      }
      break;
    }
  }
  if (jitter) {
    nsPrintfCString text("%.2lfms", jitter->ToMilliseconds());
    PROFILER_MARKER_TEXT("VideoFrameTimeStampJitter", GRAPHICS, {}, text);
  }
}

}  // namespace layers
}  // namespace mozilla
