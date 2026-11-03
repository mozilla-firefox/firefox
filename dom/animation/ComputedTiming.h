/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ComputedTiming_h
#define mozilla_ComputedTiming_h

#include "mozilla/StickyTimeDuration.h"
#include "mozilla/dom/AnimationEffectBinding.h"  // FillMode
#include "mozilla/dom/Nullable.h"

namespace mozilla {

/**
 * Stores the results of calculating the timing properties of an animation
 * at a given sample time.
 */
struct ComputedTiming {
  // The total duration of the animation including all iterations.
  // Will equal StickyTimeDuration::Forever() if the animation repeats
  // indefinitely.
  StickyTimeDuration mActiveDuration;
  // The time within the active interval.
  StickyTimeDuration mActiveTime;
  // The effect end time in local time (i.e. an offset from the effect's
  // start time). Will equal StickyTimeDuration::Forever() if the animation
  // plays indefinitely.
  StickyTimeDuration mEndTime;
  // Progress towards the end of the current iteration. If the effect is
  // being sampled backwards, this will go from 1.0 to 0.0.
  // Will be null if the animation is neither animating nor
  // filling at the sampled time.
  dom::Nullable<double> mProgress;
  // Zero-based iteration index (meaningless if mProgress is null).
  uint64_t mCurrentIteration = 0;
  // Unlike TimingParams::mIterations, this value is
  // guaranteed to be in the range [0, Infinity].
  double mIterations = 1.0;
  double mIterationStart = 0.0;
  StickyTimeDuration mDuration;

  // This is the computed fill mode so it is never auto
  dom::FillMode mFill = dom::FillMode::None;
  bool FillsForwards() const {
    MOZ_ASSERT(mFill != dom::FillMode::Auto,
               "mFill should not be Auto in ComputedTiming.");
    return mFill == dom::FillMode::Both || mFill == dom::FillMode::Forwards;
  }
  bool FillsBackwards() const {
    MOZ_ASSERT(mFill != dom::FillMode::Auto,
               "mFill should not be Auto in ComputedTiming.");
    return mFill == dom::FillMode::Both || mFill == dom::FillMode::Backwards;
  }

  enum class AnimationPhase : uint8_t {
    Idle,    // Not sampled (null sample time)
    Before,  // Sampled prior to the start of the active interval
    Active,  // Sampled within the active interval
    After    // Sampled after (or at) the end of the active interval
  };
  AnimationPhase mPhase = AnimationPhase::Idle;

  bool mBeforeFlag = false;
};

}  // namespace mozilla

#endif  // mozilla_ComputedTiming_h
