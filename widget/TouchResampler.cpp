/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TouchResampler.h"

/**
 * TouchResampler implementation
 */

namespace mozilla {
namespace widget {

// The values below have been tested and found to be acceptable on a device
// with a display refresh rate of 60Hz and touch sampling rate of 100Hz.
// While their "ideal" values are dependent on the exact rates of each device,
// the values we've picked below should be somewhat robust across a variation of
// different rates. They mostly aim to avoid making predictions that are too far
// away (in terms of distance) from the finger, and to detect pauses in the
// finger motion without too much delay.

// Maximum time between two consecutive data points to consider resampling
// between them.
// Values between 1x and 5x of the touch sampling interval are reasonable.
static const int32_t kTouchResampleWindowSize = 40;

// These next two values constrain the sampling timestamp.
// Our caller will usually adjust frame timestamps to be slightly in the past,
// for example by 5ms. This means that, during normal operation, we will
// maximally need to predict by [touch sampling rate] minus 5ms.
// So we would like kTouchResampleMaxPredictMs to satisfy the following:
// kTouchResampleMaxPredictMs + [frame time adjust] > [touch sampling rate]
static const int32_t kTouchResampleMaxPredictMs = 8;
// This one is a protection against very outdated frame timestamps.
// Values larger than the touch sampling interval and less than 3x of the vsync
// interval are reasonable.
static const int32_t kTouchResampleMaxBacksampleMs = 20;

// The maximum age of the most recent data point to consider resampling.
// Should be between 1x and 3x of the touch sampling interval.
static const int32_t kTouchResampleOldTouchThresholdMs = 17;

uint64_t TouchResampler::ProcessEvent(MultiTouchInput&& aInput) {
  mCurrentTouches.UpdateFromEvent(aInput);

  uint64_t eventId = mNextEventId;
  mNextEventId++;

  if (aInput.mType == MultiTouchInput::MULTITOUCH_MOVE) {
    // Touch move events are deferred until NotifyFrame.
    mDeferredTouchMoveEvents.push({std::move(aInput), eventId});
  } else {
    // Non-move events are transferred to the outgoing queue unmodified.
    // If there are pending touch move events, flush those out first, so that
    // events are emitted in the right order.
    FlushDeferredTouchMoveEventsUnresampled();
    if (mInResampledState) {
      // Return to a non-resampled state before emitting a non-move event.
      ReturnToNonResampledState();
    }
    EmitEvent(std::move(aInput), eventId);
  }

  return eventId;
}

void TouchResampler::NotifyFrame(const TimeStamp& aTimeStamp) {
  TimeStamp lastTouchTime = mCurrentTouches.LatestDataPointTime();
  if (mDeferredTouchMoveEvents.empty() ||
      (lastTouchTime &&
       lastTouchTime < aTimeStamp - TimeDuration::FromMilliseconds(
                                        kTouchResampleOldTouchThresholdMs))) {
    // We haven't received a touch move event in a while, so the fingers must
    // have stopped moving. Flush any old touch move events.
    FlushDeferredTouchMoveEventsUnresampled();

    if (mInResampledState) {
      // Make sure we pause at the resting position that we actually observed,
      // and not at a resampled position.
      ReturnToNonResampledState();
    }

    // Clear touch location history so that we don't resample across a pause.
    mCurrentTouches.ClearDataPoints();
    return;
  }

  MOZ_RELEASE_ASSERT(lastTouchTime);
  TimeStamp lowerBound = lastTouchTime - TimeDuration::FromMilliseconds(
                                             kTouchResampleMaxBacksampleMs);
  TimeStamp upperBound = lastTouchTime + TimeDuration::FromMilliseconds(
                                             kTouchResampleMaxPredictMs);
  TimeStamp sampleTime = std::clamp(aTimeStamp, lowerBound, upperBound);

  if (mLastEmittedEventTime && sampleTime < mLastEmittedEventTime) {
    // Keep emitted timestamps in order.
    sampleTime = mLastEmittedEventTime;
  }

  // We have at least one pending touch move event. Pick one of the events from
  // mDeferredTouchMoveEvents as the base event for the resampling adjustment.
  // We want to produce an event stream whose timestamps are in the right order.
  // As the base event, use the first event that's at or after sampleTime,
  // unless there is no such event, in that case use the last one we have. We
  // will set the timestamp on the resampled event to sampleTime later.
  // Flush out any older events so that everything remains in the right order.
  MultiTouchInput input;
  uint64_t eventId;
  while (true) {
    MOZ_RELEASE_ASSERT(!mDeferredTouchMoveEvents.empty());
    std::tie(input, eventId) = std::move(mDeferredTouchMoveEvents.front());
    mDeferredTouchMoveEvents.pop();
    if (mDeferredTouchMoveEvents.empty() || input.mTimeStamp >= sampleTime) {
      break;
    }
    // Flush this event to the outgoing queue without resampling. What ends up
    // on the screen will still be smooth because we will proceed to emit a
    // resampled event before the paint for this frame starts.
    PrependLeftoverHistoricalData(&input);
    MOZ_RELEASE_ASSERT(input.mTimeStamp < sampleTime);
    EmitEvent(std::move(input), eventId);
  }

  mOriginalOfResampledTouchMove = Nothing();

  // Compute the resampled touch positions.
  nsTArray<ScreenIntPoint> resampledPositions;
  bool anyPositionDifferentFromOriginal = false;
  for (const auto& touch : input.mTouches) {
    ScreenIntPoint resampledPosition =
        mCurrentTouches.ResampleTouchPositionAtTime(
            touch.mIdentifier, touch.mScreenPoint, sampleTime);
    if (resampledPosition != touch.mScreenPoint) {
      anyPositionDifferentFromOriginal = true;
    }
    resampledPositions.AppendElement(resampledPosition);
  }

  if (anyPositionDifferentFromOriginal) {
    // Store a copy of the original event, so that we can return to an
    // non-resampled position later, if necessary.
    mOriginalOfResampledTouchMove = Some(input);

    // Add the original observed position to the historical data, as well as any
    // leftover historical positions from the previous touch move event, and
    // store the resampled values in the "final" position of the event.
    PrependLeftoverHistoricalData(&input);
    for (size_t i = 0; i < input.mTouches.Length(); i++) {
      auto& touch = input.mTouches[i];
      touch.mHistoricalData.AppendElement(SingleTouchData::HistoricalTouchData{
          input.mTimeStamp,
          touch.mScreenPoint,
          touch.mLocalScreenPoint,
          touch.mRadius,
          touch.mRotationAngle,
          touch.mForce,
      });

      // Remove any historical touch data that's in the future, compared to
      // sampleTime. This data will be included by upcoming touch move
      // events. This only happens if the frame timestamp can be older than the
      // event timestamp, i.e. if interpolation occurs (rather than
      // extrapolation).
      auto futureDataStart = std::find_if(
          touch.mHistoricalData.begin(), touch.mHistoricalData.end(),
          [sampleTime](
              const SingleTouchData::HistoricalTouchData& aHistoricalData) {
            return aHistoricalData.mTimeStamp > sampleTime;
          });
      if (futureDataStart != touch.mHistoricalData.end()) {
        nsTArray<SingleTouchData::HistoricalTouchData> futureData(
            Span<SingleTouchData::HistoricalTouchData>(touch.mHistoricalData)
                .From(futureDataStart.GetIndex()));
        touch.mHistoricalData.TruncateLength(futureDataStart.GetIndex());
        mRemainingTouchData.insert({touch.mIdentifier, std::move(futureData)});
      }

      touch.mScreenPoint = resampledPositions[i];
    }
    input.mTimeStamp = sampleTime;
  }

  EmitEvent(std::move(input), eventId);
  mInResampledState = anyPositionDifferentFromOriginal;
}

void TouchResampler::PrependLeftoverHistoricalData(MultiTouchInput* aInput) {
  for (auto& touch : aInput->mTouches) {
    auto leftoverData = mRemainingTouchData.find(touch.mIdentifier);
    if (leftoverData != mRemainingTouchData.end()) {
      nsTArray<SingleTouchData::HistoricalTouchData> data =
          std::move(leftoverData->second);
      mRemainingTouchData.erase(leftoverData);
      touch.mHistoricalData.InsertElementsAt(0, data);
    }

    if (TimeStamp cutoffTime = mLastEmittedEventTime) {
      // If we received historical touch data that was further in the past than
      // the last resampled event, discard that data so that the touch data
      // points are emitted in order.
      touch.mHistoricalData.RemoveElementsBy(
          [cutoffTime](const SingleTouchData::HistoricalTouchData& aTouchData) {
            return aTouchData.mTimeStamp < cutoffTime;
          });
    }
  }
  mRemainingTouchData.clear();
}

void TouchResampler::FlushDeferredTouchMoveEventsUnresampled() {
  while (!mDeferredTouchMoveEvents.empty()) {
    auto [input, eventId] = std::move(mDeferredTouchMoveEvents.front());
    mDeferredTouchMoveEvents.pop();
    PrependLeftoverHistoricalData(&input);
    EmitEvent(std::move(input), eventId);
    mInResampledState = false;
    mOriginalOfResampledTouchMove = Nothing();
  }
}

void TouchResampler::ReturnToNonResampledState() {
  MOZ_RELEASE_ASSERT(mInResampledState);
  MOZ_RELEASE_ASSERT(mDeferredTouchMoveEvents.empty(),
                     "Don't call this if there is a deferred touch move event. "
                     "We can return to the non-resampled state by sending that "
                     "event, rather than a copy of a previous event.");

  // The last outgoing event was a resampled touch move event.
  // Return to the non-resampled state, by sending a touch move event to
  // "overwrite" any resampled positions with the original observed positions.
  MultiTouchInput input = std::move(*mOriginalOfResampledTouchMove);
  mOriginalOfResampledTouchMove = Nothing();

  // For the event's timestamp, we want to backdate the correction as far as we
  // can, while still preserving timestamp ordering. But we also don't want to
  // backdate it to be older than it was originally.
  if (mLastEmittedEventTime > input.mTimeStamp) {
    input.mTimeStamp = mLastEmittedEventTime;
  }

  // Assemble the correct historical touch data for this event.
  // We don't want to include data points that we've already sent out with the
  // resampled event. And from the leftover data points, we only want those that
  // don't duplicate the final time + position of this event.
  for (auto& touch : input.mTouches) {
    touch.mHistoricalData.Clear();
  }
  PrependLeftoverHistoricalData(&input);
  for (auto& touch : input.mTouches) {
    touch.mHistoricalData.RemoveElementsBy([&](const auto& histData) {
      return histData.mTimeStamp >= input.mTimeStamp;
    });
  }

  EmitExtraEvent(std::move(input));
  mInResampledState = false;
}

void TouchResampler::TouchInfo::Update(const SingleTouchData& aTouch,
                                       const TimeStamp& aEventTime) {
  for (const auto& historicalData : aTouch.mHistoricalData) {
    mBaseDataPoint = mLatestDataPoint;
    mLatestDataPoint =
        Some(DataPoint{historicalData.mTimeStamp, historicalData.mScreenPoint});
  }
  mBaseDataPoint = mLatestDataPoint;
  mLatestDataPoint = Some(DataPoint{aEventTime, aTouch.mScreenPoint});
}

ScreenIntPoint TouchResampler::TouchInfo::ResampleAtTime(
    const ScreenIntPoint& aLastObservedPosition, const TimeStamp& aTimeStamp) {
  TimeStamp cutoff =
      aTimeStamp - TimeDuration::FromMilliseconds(kTouchResampleWindowSize);
  if (!mBaseDataPoint || !mLatestDataPoint ||
      !(mBaseDataPoint->mTimeStamp < mLatestDataPoint->mTimeStamp) ||
      mBaseDataPoint->mTimeStamp < cutoff) {
    return aLastObservedPosition;
  }

  // For the actual resampling, connect the last two data points with a line and
  // sample along that line.
  TimeStamp t1 = mBaseDataPoint->mTimeStamp;
  TimeStamp t2 = mLatestDataPoint->mTimeStamp;
  double t = (aTimeStamp - t1) / (t2 - t1);

  double x1 = mBaseDataPoint->mPosition.x;
  double x2 = mLatestDataPoint->mPosition.x;
  double y1 = mBaseDataPoint->mPosition.y;
  double y2 = mLatestDataPoint->mPosition.y;

  int32_t resampledX = round(x1 + t * (x2 - x1));
  int32_t resampledY = round(y1 + t * (y2 - y1));
  return ScreenIntPoint(resampledX, resampledY);
}

void TouchResampler::CurrentTouches::UpdateFromEvent(
    const MultiTouchInput& aInput) {
  switch (aInput.mType) {
    case MultiTouchInput::MULTITOUCH_START: {
      // A new touch has been added; make sure mTouches reflects the current
      // touches in the event.
      nsTArray<TouchInfo> newTouches;
      for (const auto& touch : aInput.mTouches) {
        const auto touchInfo = TouchByIdentifier(touch.mIdentifier);
        if (touchInfo != mTouches.end()) {
          // This is one of the existing touches.
          newTouches.AppendElement(std::move(*touchInfo));
          mTouches.RemoveElementAt(touchInfo);
        } else {
          // This is the new touch.
          newTouches.AppendElement(TouchInfo{
              touch.mIdentifier, Nothing(),
              Some(DataPoint{aInput.mTimeStamp, touch.mScreenPoint})});
        }
      }
      MOZ_ASSERT(mTouches.IsEmpty(), "Missing touch end before touch start?");
      mTouches = std::move(newTouches);
      break;
    }

    case MultiTouchInput::MULTITOUCH_MOVE: {
      // The touches have moved.
      // Add position information to the history data points.
      for (const auto& touch : aInput.mTouches) {
        const auto touchInfo = TouchByIdentifier(touch.mIdentifier);
        MOZ_ASSERT(touchInfo != mTouches.end());
        if (touchInfo != mTouches.end()) {
          touchInfo->Update(touch, aInput.mTimeStamp);
        }
      }
      mLatestDataPointTime = aInput.mTimeStamp;
      break;
    }

    case MultiTouchInput::MULTITOUCH_END: {
      // A touch has been removed.
      MOZ_RELEASE_ASSERT(aInput.mTouches.Length() == 1);
      const auto touchInfo = TouchByIdentifier(aInput.mTouches[0].mIdentifier);
      MOZ_ASSERT(touchInfo != mTouches.end());
      if (touchInfo != mTouches.end()) {
        mTouches.RemoveElementAt(touchInfo);
      }
      break;
    }

    case MultiTouchInput::MULTITOUCH_CANCEL:
      // All touches are canceled.
      mTouches.Clear();
      break;
  }
}

nsTArray<TouchResampler::TouchInfo>::iterator
TouchResampler::CurrentTouches::TouchByIdentifier(int32_t aIdentifier) {
  return std::find_if(mTouches.begin(), mTouches.end(),
                      [aIdentifier](const TouchInfo& info) {
                        return info.mIdentifier == aIdentifier;
                      });
}

ScreenIntPoint TouchResampler::CurrentTouches::ResampleTouchPositionAtTime(
    int32_t aIdentifier, const ScreenIntPoint& aLastObservedPosition,
    const TimeStamp& aTimeStamp) {
  const auto touchInfo = TouchByIdentifier(aIdentifier);
  MOZ_ASSERT(touchInfo != mTouches.end());
  if (touchInfo != mTouches.end()) {
    return touchInfo->ResampleAtTime(aLastObservedPosition, aTimeStamp);
  }
  return aLastObservedPosition;
}

}  // namespace widget
}  // namespace mozilla
