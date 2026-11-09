/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_IMAGECOMPOSITE_H
#define MOZILLA_GFX_IMAGECOMPOSITE_H

#include "CompositableHost.h"   // for CompositableTextureHostRef
#include "mozilla/TimeStamp.h"  // for TimeStamp
#include "mozilla/gfx/2D.h"
#include "nsTArray.h"

namespace mozilla {
namespace layers {

/**
 * Implements Image selection logic.
 */
class ImageComposite {
 public:
  static const int32_t BIAS_TIME_MS;

  explicit ImageComposite();
  virtual ~ImageComposite();

  int32_t GetFrameID() {
    const TimedImage* img = ChooseImage();
    return img ? img->mFrameID : -1;
  }

  int32_t GetProducerID() {
    const TimedImage* img = ChooseImage();
    return img ? img->mProducerID : -1;
  }

  int32_t GetLastFrameID() const { return mLastFrameID; }
  int32_t GetLastProducerID() const { return mLastProducerID; }
  uint32_t GetDroppedFramesAndReset() {
    uint32_t dropped = mDroppedFrames;
    mDroppedFrames = 0;
    return dropped;
  }

  enum Bias {
    // Don't apply bias to frame times
    BIAS_NONE,
    // Apply a negative bias to frame times to keep them before the vsync time
    BIAS_NEGATIVE,
    // Apply a positive bias to frame times to keep them after the vsync time
    BIAS_POSITIVE,
  };

 protected:
  virtual TimeStamp GetCompositionTime() const = 0;
  virtual CompositionOpportunityId GetCompositionOpportunityId() const = 0;
  virtual void AppendImageCompositeNotification(
      const ImageCompositeNotificationInfo& aInfo) const = 0;

  struct TimedImage {
    CompositableTextureHostRef mTextureHost;
    TimeStamp mTimeStamp;
    gfx::IntRect mPictureRect;
    int32_t mFrameID;
    int32_t mProducerID;
  };

  /**
   * ChooseImage is guaranteed to return the same TimedImage every time it's
   * called during the same composition, up to the end of Composite() ---
   * it depends only on mImages, mCompositor->GetCompositionTime(), and mBias.
   * mBias is updated at the end of Composite().
   */
  const TimedImage* ChooseImage();
  int ChooseImageIndex();
  const TimedImage* GetImage(size_t aIndex) const;
  size_t ImagesCount() const { return mImages.Length(); }
  const nsTArray<TimedImage>& Images() const { return mImages; }

  void RemoveImagesWithTextureHost(TextureHost* aTexture);
  void ClearImages();
  void SetImages(nsTArray<TimedImage>&& aNewImages);

 protected:
  // Send ImageComposite notifications and update the ChooseImage bias.
  void OnFinishRendering(int aImageIndex, const TimedImage* aImage,
                         base::ProcessId aProcessId,
                         const CompositableHandle& aHandle);

  int32_t mLastFrameID = -1;
  int32_t mLastProducerID = -1;

 private:
  nsTArray<TimedImage> mImages;
  TimeStamp GetBiasedTime(const TimeStamp& aInput) const;

  // Called when we know that frames older than aImage will never get another
  // chance to be displayed.
  // Counts frames in mImages that were skipped, and adds the skipped count to
  // mSkippedFramesSinceLastComposite.
  void CountSkippedFrames(const TimedImage* aImage);

  // Update mLastFrameID and mLastProducerID, and report dropped frames.
  // Returns whether the frame changed.
  bool UpdateCompositedFrame(int aImageIndex,
                             bool aWasVisibleAtPreviousComposition);

  void UpdateBias(size_t aImageIndex, bool aFrameUpdated);

  // Emit a profiler marker about video frame timestamp jitter.
  void DetectTimeStampJitter(const TimedImage* aNewImage);

  /**
   * Bias to apply to the next frame.
   */
  Bias mBias = BIAS_NONE;

  // Video frames that were in mImages but that will never reach the screen.
  // This count is reset in UpdateCompositedFrame.
  int32_t mSkippedFramesSinceLastComposite = 0;

  // The number of dropped frames since the last call to
  // GetDroppedFramesAndReset(). Not all skipped frames are considered "dropped"
  // - for example, videos that are scrolled out of view or videos in background
  // tabs are expected to skip frames, and those skipped frames are not counted
  // as dropped frames.
  uint32_t mDroppedFrames = 0;

  // The composition opportunity IDs of the last calls to ChooseImageIndex and
  // UpdateCompositedFrame, respectively.
  CompositionOpportunityId mLastChooseImageIndexComposition;
  CompositionOpportunityId mLastFrameUpdateComposition;
};

}  // namespace layers
}  // namespace mozilla

#endif  // MOZILLA_GFX_IMAGECOMPOSITE_H
