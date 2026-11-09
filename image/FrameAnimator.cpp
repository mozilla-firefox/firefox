/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FrameAnimator.h"

#include <utility>

#include "LookupResult.h"
#include "RasterImage.h"
#include "imgIContainer.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/StaticPrefs_image.h"

namespace mozilla {

using namespace gfx;

namespace image {

///////////////////////////////////////////////////////////////////////////////
// AnimationState implementation.
///////////////////////////////////////////////////////////////////////////////

const gfx::IntRect AnimationState::UpdateState(
    RasterImage* aImage, const gfx::IntSize& aSize,
    bool aAllowInvalidation /* = true */) {
  LookupResult result = SurfaceCache::Lookup(
      ImageKey(aImage),
      RasterSurfaceKey(aSize, DefaultSurfaceFlags(), PlaybackType::eAnimated),
      /* aMarkUsed = */ false);

  return UpdateStateInternal(result, aSize, aAllowInvalidation);
}

const gfx::IntRect AnimationState::UpdateStateInternal(
    LookupResult& aResult, const gfx::IntSize& aSize,
    bool aAllowInvalidation /* = true */) {
  // Update mDiscarded and mIsCurrentlyDecoded.
  if (aResult.Type() == MatchType::NOT_FOUND) {
    // no frames, we've either been discarded, or never been decoded before.
    mDiscarded = mHasBeenDecoded;
    mIsCurrentlyDecoded = false;
  } else if (aResult.Type() == MatchType::PENDING) {
    // no frames yet, but a decoder is or will be working on it.
    mDiscarded = false;
    mIsCurrentlyDecoded = false;
    mHasRequestedDecode = true;
  } else {
    MOZ_ASSERT(aResult.Type() == MatchType::EXACT);
    mDiscarded = false;
    mHasRequestedDecode = true;

    // If we can seek to the current animation frame we consider it decoded.
    // Animated images are never fully decoded unless very short.
    // Note that we use GetFrame instead of Seek here. The difference is that
    // Seek eventually calls AnimationFrameBuffer::Get with aForDisplay == true,
    // whereas GetFrame calls AnimationFrameBuffer::Get with aForDisplay ==
    // false. The aForDisplay can change whether those functions succeed or not
    // (only for the first frame). Since this is not for display we want to pass
    // aForDisplay == false, but also for consistency with
    // RequestRefresh/AdvanceFrame, because we want our state to be in sync with
    // those functions. The user of Seek (GetCompositedFrame) doesn't need to be
    // in sync with our state.
    RefPtr<imgFrame> currentFrame =
        bool(aResult.Surface())
            ? aResult.Surface().GetFrame(mCurrentAnimationFrameIndex)
            : nullptr;
    mIsCurrentlyDecoded = !!currentFrame;
  }

  gfx::IntRect ret;

  if (aAllowInvalidation) {
    // Update the value of mCompositedFrameInvalid.
    if (mIsCurrentlyDecoded) {
      // It is safe to clear mCompositedFrameInvalid safe to do for images that
      // are fully decoded but aren't finished animating because before we paint
      // the refresh driver will call into us to advance to the correct frame,
      // and that will succeed because we have all the frames.
      if (mCompositedFrameInvalid) {
        // Invalidate if we are marking the composited frame valid.
        ret.SizeTo(aSize);
      }
      mCompositedFrameInvalid = false;
    } else {
      if (mHasRequestedDecode) {
        MOZ_ASSERT(StaticPrefs::image_mem_animated_discardable_AtStartup());
        mCompositedFrameInvalid = true;
      }
    }
    // Otherwise don't change the value of mCompositedFrameInvalid, it will be
    // updated by RequestRefresh.
  }

  return ret;
}

void AnimationState::NotifyDecodeComplete() { mHasBeenDecoded = true; }

void AnimationState::ResetAnimation() { mCurrentAnimationFrameIndex = 0; }

void AnimationState::SetAnimationMode(uint16_t aAnimationMode) {
  mAnimationMode = aAnimationMode;
}

void AnimationState::UpdateKnownFrameCount(uint32_t aFrameCount) {
  if (aFrameCount <= mFrameCount) {
    // Nothing to do. Since we can redecode animated images, we may see the same
    // sequence of updates replayed again, so seeing a smaller frame count than
    // what we already know about doesn't indicate an error.
    return;
  }

  MOZ_ASSERT(!mHasBeenDecoded, "Adding new frames after decoding is finished?");
  MOZ_ASSERT(aFrameCount <= mFrameCount + 1, "Skipped a frame?");

  mFrameCount = aFrameCount;
}

Maybe<uint32_t> AnimationState::FrameCount() const {
  return mHasBeenDecoded ? Some(mFrameCount) : Nothing();
}

void AnimationState::SetFirstFrameRefreshArea(const IntRect& aRefreshArea) {
  mFirstFrameRefreshArea = aRefreshArea;
}

void AnimationState::InitAnimationFrameTimeIfNecessary() {
  if (mCurrentAnimationFrameTime.IsNull()) {
    mCurrentAnimationFrameTime = TimeStamp::Now();
  }
}

void AnimationState::SetAnimationFrameTime(const TimeStamp& aTime) {
  mCurrentAnimationFrameTime = aTime;
}

bool AnimationState::MaybeAdvanceAnimationFrameTime(const TimeStamp& aTime) {
  if (!StaticPrefs::image_animated_resume_from_last_displayed() ||
      mCurrentAnimationFrameTime >= aTime) {
    return false;
  }

  // We are configured to stop an animation when it is out of view, and restart
  // it from the same point when it comes back into view. The same applies if it
  // was discarded while out of view.
  mCurrentAnimationFrameTime = aTime;
  return true;
}

uint32_t AnimationState::GetCurrentAnimationFrameIndex() const {
  return mCurrentAnimationFrameIndex;
}

FrameTimeout AnimationState::LoopLength() const {
  // If we don't know the loop length yet, we have to treat it as infinite.
  if (!mLoopLength) {
    return FrameTimeout::Forever();
  }

  MOZ_ASSERT(mHasBeenDecoded,
             "We know the loop length but decoding isn't done?");

  // If we're not looping, a single loop time has no meaning.
  if (mAnimationMode != imgIContainer::kNormalAnimMode) {
    return FrameTimeout::Forever();
  }

  return *mLoopLength;
}

///////////////////////////////////////////////////////////////////////////////
// FrameAnimator implementation.
///////////////////////////////////////////////////////////////////////////////

TimeStamp FrameAnimator::GetCurrentImgFrameEndTime(
    AnimationState& aState, FrameTimeout aCurrentTimeout) const {
  if (aCurrentTimeout == FrameTimeout::Forever()) {
    // We need to return a sentinel value in this case, because our logic
    // doesn't work correctly if we have an infinitely long timeout. We use one
    // year in the future as the sentinel because it works with the loop in
    // RequestRefresh() below.
    // XXX(seth): It'd be preferable to make our logic work correctly with
    // infinitely long timeouts.
    return TimeStamp::NowLoRes() + TimeDuration::FromMilliseconds(31536000);
  }

  TimeDuration durationOfTimeout =
      TimeDuration::FromMilliseconds(aCurrentTimeout.AsMilliseconds());
  return aState.mCurrentAnimationFrameTime + durationOfTimeout;
}

RefreshResult FrameAnimator::AdvanceFrame(AnimationState& aState,
                                          DrawableSurface& aFrames,
                                          RefPtr<imgFrame>& aCurrentFrame,
                                          TimeStamp aTime) {
  AUTO_PROFILER_LABEL("FrameAnimator::AdvanceFrame", GRAPHICS);

  RefreshResult ret;

  // Determine what the next frame is, taking into account looping.
  uint32_t currentFrameIndex = aState.mCurrentAnimationFrameIndex;
  uint32_t nextFrameIndex = currentFrameIndex + 1;

  // Check if we're at the end of the loop. (FrameCount() returns Nothing() if
  // we don't know the total count yet.)
  if (aState.FrameCount() == Some(nextFrameIndex)) {
    // If we are not looping forever, initialize the loop counter
    if (aState.mLoopRemainingCount < 0 && aState.LoopCount() >= 0) {
      aState.mLoopRemainingCount = aState.LoopCount();
    }

    // If animation mode is "loop once", or we're at end of loop counter,
    // it's time to stop animating.
    if (aState.mAnimationMode == imgIContainer::kLoopOnceAnimMode ||
        aState.mLoopRemainingCount == 0) {
      ret.mAnimationFinished = true;
    }

    nextFrameIndex = 0;

    if (aState.mLoopRemainingCount > 0) {
      aState.mLoopRemainingCount--;
    }

    // If we're done, exit early.
    if (ret.mAnimationFinished) {
      return ret;
    }
  }

  if (nextFrameIndex >= aState.KnownFrameCount()) {
    // We've already advanced to the last decoded frame, nothing more we can do.
    // We're blocked by network/decoding from displaying the animation at the
    // rate specified, so that means the frame we are displaying (the latest
    // available) is the frame we want to be displaying at this time. So we
    // update the current animation time. If we didn't update the current
    // animation time then it could lag behind, which would indicate that we are
    // behind in the animation and should try to catch up. When we are done
    // decoding (and thus can loop around back to the start of the animation) we
    // would then jump to a random point in the animation to try to catch up.
    // But we were never behind in the animation.
    aState.mCurrentAnimationFrameTime = aTime;
    return ret;
  }

  // There can be frames in the surface cache with index >= KnownFrameCount()
  // which GetRawFrame() can access because an async decoder has decoded them,
  // but which AnimationState doesn't know about yet because we haven't received
  // the appropriate notification on the main thread. Make sure we stay in sync
  // with AnimationState.
  MOZ_ASSERT(nextFrameIndex < aState.KnownFrameCount());
  RefPtr<imgFrame> nextFrame = aFrames.GetFrame(nextFrameIndex);

  // We should always check to see if we have the next frame even if we have
  // previously finished decoding. If we needed to redecode (e.g. due to a draw
  // failure) we would have discarded all the old frames and may not yet have
  // the new ones. DrawableSurface::RawAccessRef promises to only return
  // finished frames.
  if (!nextFrame) {
    // Uh oh, the frame we want to show is currently being decoded (partial).
    // Similar to the above case, we could be blocked by network or decoding,
    // and so we should advance our current time rather than risk jumping
    // through the animation. We will wait until the next refresh driver tick
    // and try again.
    aState.mCurrentAnimationFrameTime = aTime;
    return ret;
  }

  if (nextFrame->GetTimeout() == FrameTimeout::Forever()) {
    ret.mAnimationFinished = true;
  }

  if (nextFrameIndex == 0) {
    ret.mDirtyRect = aState.FirstFrameRefreshArea();
  } else {
    ret.mDirtyRect = nextFrame->GetDirtyRect();
  }

  aState.mCurrentAnimationFrameTime =
      GetCurrentImgFrameEndTime(aState, aCurrentFrame->GetTimeout());

  // If we can get closer to the current time by a multiple of the image's loop
  // time, we should. We can only do this if we're done decoding; otherwise, we
  // don't know the full loop length, and LoopLength() will have to return
  // FrameTimeout::Forever(). We also skip this for images with a finite loop
  // count if we have initialized mLoopRemainingCount (it only gets initialized
  // after one full loop).
  FrameTimeout loopTime = aState.LoopLength();
  if (loopTime != FrameTimeout::Forever() &&
      (aState.LoopCount() < 0 || aState.mLoopRemainingCount >= 0)) {
    TimeDuration delay = aTime - aState.mCurrentAnimationFrameTime;
    if (delay.ToMilliseconds() > loopTime.AsMilliseconds()) {
      // Explicitly use integer division to get the floor of the number of
      // loops.
      uint64_t loops = static_cast<uint64_t>(delay.ToMilliseconds()) /
                       loopTime.AsMilliseconds();

      // If we have a finite loop count limit the number of loops we advance.
      if (aState.mLoopRemainingCount >= 0) {
        MOZ_ASSERT(aState.LoopCount() >= 0);
        loops =
            std::min(loops, CheckedUint64(aState.mLoopRemainingCount).value());
      }

      aState.mCurrentAnimationFrameTime +=
          TimeDuration::FromMilliseconds(loops * loopTime.AsMilliseconds());

      if (aState.mLoopRemainingCount >= 0) {
        MOZ_ASSERT(loops <= CheckedUint64(aState.mLoopRemainingCount).value());
        aState.mLoopRemainingCount -= CheckedInt32(loops).value();
      }
    }
  }

  // Set currentAnimationFrameIndex at the last possible moment
  aState.mCurrentAnimationFrameIndex = nextFrameIndex;
  aCurrentFrame = std::move(nextFrame);
  aFrames.Advance(nextFrameIndex);

  // If we're here, we successfully advanced the frame.
  ret.mFrameAdvanced = true;

  return ret;
}

void FrameAnimator::ResetAnimation(AnimationState& aState) {
  aState.ResetAnimation();

  // Our surface provider is synchronized to our state, so we need to reset its
  // state as well, if we still have one.
  SurfaceCache::ResetAnimation(
      ImageKey(mImage),
      RasterSurfaceKey(mSize, DefaultSurfaceFlags(), PlaybackType::eAnimated));

  // Calling Reset on the surface of the animation can cause discarding surface
  // providers to throw out all their frames so refresh our state.
  OrientedIntRect rect =
      OrientedIntRect::FromUnknownRect(aState.UpdateState(mImage, mSize));

  if (!rect.IsEmpty()) {
    nsCOMPtr<nsIEventTarget> eventTarget = do_GetMainThread();
    RefPtr<RasterImage> image = mImage;
    nsCOMPtr<nsIRunnable> ev = NS_NewRunnableFunction(
        "FrameAnimator::ResetAnimation",
        [=]() -> void { image->NotifyProgress(NoProgress, rect); });
    eventTarget->Dispatch(ev.forget(), NS_DISPATCH_NORMAL);
  }
}

RefreshResult FrameAnimator::RequestRefresh(AnimationState& aState,
                                            const TimeStamp& aTime) {
  // By default, an empty RefreshResult.
  RefreshResult ret;

  if (aState.IsDiscarded()) {
    aState.MaybeAdvanceAnimationFrameTime(aTime);
    return ret;
  }

  // Get the animation frames once now, and pass them down to callees because
  // the surface could be discarded at anytime on a different thread. This is
  // must easier to reason about then trying to write code that is safe to
  // having the surface disappear at anytime.
  LookupResult result = SurfaceCache::Lookup(
      ImageKey(mImage),
      RasterSurfaceKey(mSize, DefaultSurfaceFlags(), PlaybackType::eAnimated),
      /* aMarkUsed = */ true);

  ret.mDirtyRect = aState.UpdateStateInternal(result, mSize);
  if (aState.IsDiscarded() || !result) {
    aState.MaybeAdvanceAnimationFrameTime(aTime);
    return ret;
  }

  RefPtr<imgFrame> currentFrame =
      result.Surface().GetFrame(aState.mCurrentAnimationFrameIndex);

  // only advance the frame if the current time is greater than or
  // equal to the current frame's end time.
  if (!currentFrame) {
    MOZ_ASSERT(StaticPrefs::image_mem_animated_discardable_AtStartup());
    MOZ_ASSERT(aState.GetHasRequestedDecode() &&
               !aState.GetIsCurrentlyDecoded());
    MOZ_ASSERT(aState.mCompositedFrameInvalid);
    // Nothing we can do but wait for our previous current frame to be decoded
    // again so we can determine what to do next.
    aState.MaybeAdvanceAnimationFrameTime(aTime);
    return ret;
  }

  TimeStamp currentFrameEndTime =
      GetCurrentImgFrameEndTime(aState, currentFrame->GetTimeout());

  // If nothing has accessed the composited frame since the last time we
  // advanced, then there is no point in continuing to advance the animation.
  // This has the effect of freezing the animation while not in view.
  if (!result.Surface().MayAdvance() &&
      aState.MaybeAdvanceAnimationFrameTime(aTime)) {
    return ret;
  }

  while (currentFrameEndTime <= aTime) {
    TimeStamp oldFrameEndTime = currentFrameEndTime;

    RefreshResult frameRes =
        AdvanceFrame(aState, result.Surface(), currentFrame, aTime);

    // Accumulate our result for returning to callers.
    ret.Accumulate(frameRes);

    // currentFrame was updated by AdvanceFrame so it is still current.
    currentFrameEndTime =
        GetCurrentImgFrameEndTime(aState, currentFrame->GetTimeout());

    // If we didn't advance a frame, and our frame end time didn't change,
    // then we need to break out of this loop & wait for the frame(s)
    // to finish downloading.
    if (!frameRes.mFrameAdvanced && currentFrameEndTime == oldFrameEndTime) {
      break;
    }
  }

  // We should only mark the composited frame as valid and reset the dirty rect
  // if we advanced (meaning the next frame was actually produced somehow), the
  // composited frame was previously invalid (so we may need to repaint
  // everything) and either the frame index is valid (to know we were doing
  // blending on the main thread, instead of on the decoder threads in advance),
  // or the current frame is a full frame (blends off the main thread).
  //
  // If for some reason we forget to reset aState.mCompositedFrameInvalid, then
  // GetCompositedFrame will fail, even if we have all the data available for
  // display.
  if (currentFrameEndTime > aTime && aState.mCompositedFrameInvalid) {
    aState.mCompositedFrameInvalid = false;
    ret.mDirtyRect = IntRect(IntPoint(0, 0), mSize);
  }

  MOZ_ASSERT(!aState.mIsCurrentlyDecoded || !aState.mCompositedFrameInvalid);

  return ret;
}

LookupResult FrameAnimator::GetCompositedFrame(AnimationState& aState,
                                               bool aMarkUsed) {
  LookupResult result = SurfaceCache::Lookup(
      ImageKey(mImage),
      RasterSurfaceKey(mSize, DefaultSurfaceFlags(), PlaybackType::eAnimated),
      aMarkUsed);

  if (result) {
    // If we are getting the frame directly (e.g. through tests or canvas), we
    // need to ensure the animation is marked to allow advancing to the next
    // frame.
    result.Surface().MarkMayAdvance();
  }

  if (aState.mCompositedFrameInvalid) {
    MOZ_ASSERT(StaticPrefs::image_mem_animated_discardable_AtStartup());
    MOZ_ASSERT(aState.GetHasRequestedDecode());
    MOZ_ASSERT(!aState.GetIsCurrentlyDecoded());

    if (result.Type() == MatchType::EXACT) {
      // If our composited frame is marked as invalid but our frames are in the
      // surface cache we might just have not updated our internal state yet.
      // This can happen if the image is not in a document so that
      // RequestRefresh is not getting called to advance the frame.
      // RequestRefresh would result in our composited frame getting marked as
      // valid either at the end of RequestRefresh when we are able to advance
      // to the current time or if advancing frames eventually causes us to
      // decode all of the frames of the image resulting in DecodeComplete
      // getting called which calls UpdateState. The reason we care about this
      // is that img.decode promises won't resolve until GetCompositedFrame
      // returns a frame.
      OrientedIntRect rect = OrientedIntRect::FromUnknownRect(
          aState.UpdateStateInternal(result, mSize));

      if (!rect.IsEmpty()) {
        nsCOMPtr<nsIEventTarget> eventTarget = do_GetMainThread();
        RefPtr<RasterImage> image = mImage;
        nsCOMPtr<nsIRunnable> ev = NS_NewRunnableFunction(
            "FrameAnimator::GetCompositedFrame",
            [=]() -> void { image->NotifyProgress(NoProgress, rect); });
        eventTarget->Dispatch(ev.forget(), NS_DISPATCH_NORMAL);
      }
    }

    // If it's still invalid we have to return.
    if (aState.mCompositedFrameInvalid) {
      if (result.Type() == MatchType::NOT_FOUND) {
        return result;
      }
      return LookupResult(MatchType::PENDING);
    }
  }

  // Otherwise return the raw frame. DoBlend is required to ensure that we only
  // hit this case if the frame is not paletted and doesn't require compositing.
  if (!result) {
    return result;
  }

  // Seek to the appropriate frame. If seeking fails, it means that we couldn't
  // get the frame we're looking for; treat this as if the lookup failed.
  if (NS_FAILED(result.Surface().Seek(aState.mCurrentAnimationFrameIndex))) {
    if (result.Type() == MatchType::NOT_FOUND) {
      return result;
    }
    return LookupResult(MatchType::PENDING);
  }

  return result;
}

}  // namespace image
}  // namespace mozilla
