/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AudioEventTimeline.h"

#include "AudioNodeTrack.h"
#include "mozilla/ErrorResult.h"

using mozilla::Span;

// v1 and v0 are passed from float variables but converted to double for
// double precision interpolation.
static void FillLinearRamp(double aBufferStartTime, Span<float> aBuffer,
                           double t0, double v0, double t1, double v1) {
  double bufferStartDelta = aBufferStartTime - t0;
  double gradient = (v1 - v0) / (t1 - t0);
  for (size_t i = 0; i < aBuffer.Length(); ++i) {
    double v = v0 + (bufferStartDelta + static_cast<double>(i)) * gradient;
    aBuffer[i] = static_cast<float>(v);
  }
}

static void FillExponentialRamp(double aBufferStartTime, Span<float> aBuffer,
                                double t0, float v0, double t1, float v1) {
  MOZ_ASSERT(aBuffer.Length() >= 1);
  double fullRatio = static_cast<double>(v1) / v0;
  if (v0 == 0.f || fullRatio < 0.0) {
    std::fill_n(aBuffer.Elements(), aBuffer.Length(), v0);
    return;
  }

  double tDelta = t1 - t0;
  // Calculate the value for the first tick from the curve initial value.
  // v(t) = v0 * (v1/v0)^((t-t0)/(t1-t0))
  double exponent = (aBufferStartTime - t0) / tDelta;
  // The power function can amplify rounding error in the exponent by
  // ((t−t0)/(t1−t0)) ln (v1/v0).  The single precision exponent argument for
  // powf() would be sufficient when max(v1/v0,v0/v1) <= e, where e is Euler's
  // number, but fdlibm's single precision powf() is not expected to provide
  // speed advantages over double precision pow().
  double v = v0 * fdlibm_pow(fullRatio, exponent);
  aBuffer[0] = static_cast<float>(v);
  if (aBuffer.Length() == 1) {
    return;
  }

  // Use the inter-tick ratio to calculate values at other ticks.
  // v(t+1) = (v1/v0)^(1/(t1-t0)) * v(t)
  // Double precision is used so that accumulation of rounding error is not
  // significant.
  double tickRatio = fdlibm_pow(fullRatio, 1.0 / tDelta);
  for (size_t i = 1; i < aBuffer.Length(); ++i) {
    v *= tickRatio;
    aBuffer[i] = static_cast<float>(v);
  }
}

template <typename TimeType, typename DurationType>
static size_t LimitedCountForDuration(size_t aMax, DurationType aDuration);

template <>
size_t LimitedCountForDuration<double>(size_t aMax, double aDuration) {
  // aDuration is in seconds, so tick arithmetic is inappropriate,
  // and unnecessary.
  // GetValuesAtTime() is not available, so at most one value is fetched.
  MOZ_ASSERT(aMax <= 1);
  return aMax;
}
template <>
size_t LimitedCountForDuration<int64_t>(size_t aMax, int64_t aDuration) {
  MOZ_ASSERT(aDuration >= 0);
  // int64_t aDuration is in ticks.
  // On 32-bit systems, aDuration may be larger than SIZE_MAX.
  // Determine the larger with int64_t to avoid truncating before the
  // comparison.
  return static_cast<int64_t>(aMax) <= aDuration
             ? aMax
             : static_cast<size_t>(aDuration);
}
template <>
size_t LimitedCountForDuration<int64_t>(size_t aMax, double aDuration) {
  MOZ_ASSERT(aDuration >= 0);
  // double aDuration is in ticks.
  // AudioTimelineEvent::mDuration may be larger than INT64_MAX.
  // On 32-bit systems, mDuration may be larger than SIZE_MAX.
  // Determine the larger with double to avoid truncating before the
  // comparison.
  return static_cast<double>(aMax) <= aDuration
             ? aMax
             : static_cast<size_t>(aDuration);
}

static float* NewCurveCopy(Span<const float> aCurve) {
  if (aCurve.Length() == 0) {
    return nullptr;
  }

  float* curve = new float[aCurve.Length()];
  mozilla::PodCopy(curve, aCurve.Elements(), aCurve.Length());
  return curve;
}

namespace mozilla::dom {

AudioTimelineEvent::AudioTimelineEvent(Type aType, double aTime, float aValue,
                                       double aTimeConstant)
    : mType(aType),
      mValue(aValue),
      mTimeConstant(aTimeConstant),
      mPerTickRatio(std::numeric_limits<double>::quiet_NaN()),
      mTime(aTime) {}

AudioTimelineEvent::AudioTimelineEvent(Type aType,
                                       const nsTArray<float>& aValues,
                                       double aStartTime, double aDuration)
    : mType(aType),
      mCurveLength(aValues.Length()),
      mCurve(NewCurveCopy(aValues)),
      mDuration(aDuration),
      mTime(aStartTime) {
  MOZ_ASSERT(aType == AudioTimelineEvent::SetValueCurve);
}

AudioTimelineEvent::AudioTimelineEvent(const AudioTimelineEvent& rhs)
    : mType(rhs.mType), mTime(rhs.mTime) {
  if (mType == AudioTimelineEvent::SetValueCurve) {
    mCurveLength = rhs.mCurveLength;
    mCurve = NewCurveCopy(Span(rhs.mCurve, rhs.mCurveLength));
    mDuration = rhs.mDuration;
  } else {
    mValue = rhs.mValue;
    mTimeConstant = rhs.mTimeConstant;
    mPerTickRatio = rhs.mPerTickRatio;
  }
}

AudioTimelineEvent::~AudioTimelineEvent() {
  if (mType == AudioTimelineEvent::SetValueCurve) {
    delete[] mCurve;
  }
}

template <class TimeType>
double AudioTimelineEvent::EndTime() const {
  MOZ_ASSERT(mType != AudioTimelineEvent::SetTarget);
  if (mType == AudioTimelineEvent::SetValueCurve) {
    return Time<TimeType>() + mDuration;
  }
  return Time<TimeType>();
}

template double AudioTimelineEvent::EndTime<double>() const;
template double AudioTimelineEvent::EndTime<int64_t>() const;

float AudioTimelineEvent::EndValue() const {
  if (mType == AudioTimelineEvent::SetValueCurve) {
    return mCurve[mCurveLength - 1];
  }
  return mValue;
};

void AudioTimelineEvent::ConvertToTicks(AudioNodeTrack* aDestination) {
  mTime = aDestination->SecondsToNearestTrackTime(mTime.Get<double>());
  switch (mType) {
    case SetTarget:
      mTimeConstant *= aDestination->mSampleRate;
      // exp(-1/timeConstant) is usually very close to 1, but its effect
      // depends on the difference from 1 and rounding errors would
      // accumulate, so use double precision to retain precision in the
      // difference.  Single precision expm1f() would be sufficient, but the
      // arithmetic in AudioTimelineEvent::FillTargetApproach() is simpler
      // with exp().
      mPerTickRatio =
          mTimeConstant == 0.0 ? 0.0 : fdlibm_exp(-1.0 / mTimeConstant);

      break;
    case SetValueCurve:
      mDuration *= aDestination->mSampleRate;
      break;
    default:
      break;
  }
}

template <class TimeType>
void AudioTimelineEvent::FillTargetApproach(TimeType aBufferStartTime,
                                            Span<float> aBuffer,
                                            double v0) const {
  MOZ_ASSERT(mType == SetTarget);
  MOZ_ASSERT(aBuffer.Length() >= 1);
  double v1 = mValue;
  double vDelta = v0 - v1;
  if (vDelta == 0.0 || mTimeConstant == 0.0) {
    std::fill_n(aBuffer.Elements(), aBuffer.Length(), mValue);
    return;
  }

  // v(t) = v1 + vDelta(t) where vDelta(t) = (v0-v1) * e^(-(t-t0)/timeConstant).
  // Calculate the value for the first element in the buffer using this
  // formulation.
  vDelta *= fdlibm_expf(-(aBufferStartTime - Time<TimeType>()) / mTimeConstant);
  for (size_t i = 0; true;) {
    aBuffer[i] = static_cast<float>(v1 + vDelta);
    ++i;
    if (i == aBuffer.Length()) {
      return;
    }
    // For other buffer elements, use the pre-computed exp(-1/timeConstant)
    // for the inter-tick ratio of the difference from the target.
    // vDelta(t+1) = vDelta(t) * e^(-1/timeConstant)
    vDelta *= mPerTickRatio;
  }
}

static_assert(TRACK_TIME_MAX >> FloatingPoint<double>::kSignificandWidth == 0,
              "double precision must be exact for integer tick counts");
template <class TimeType>
void AudioTimelineEvent::FillFromValueCurve(TimeType aBufferStartTime,
                                            Span<float> aBuffer) const {
  MOZ_ASSERT(mType == SetValueCurve);
  double curveStartTime = Time<TimeType>();
  MOZ_ASSERT(aBufferStartTime >= curveStartTime);
  MOZ_ASSERT(aBufferStartTime - curveStartTime <= mDuration);
  MOZ_ASSERT((std::is_same_v<TimeType, int64_t>) || aBuffer.Length() == 1);
  MOZ_ASSERT(!(std::is_same_v<TimeType, int64_t>) ||
             aBufferStartTime - curveStartTime + aBuffer.Length() - 1 <=
                 mDuration);
  uint32_t stepCount = mCurveLength - 1;
  double timeStep = mDuration / stepCount;

  for (size_t fillStart = 0; fillStart < aBuffer.Length();) {
    // Find the curve sample index, spec'd as `k`, corresponding to a time less
    // than or equal to the first buffer element to be filled.
    double stepPos =
        (aBufferStartTime + fillStart - curveStartTime) / mDuration * stepCount;
    // GetValuesAtTimeHelperInternal() calls this only when
    // aBufferStartTime + fillStart - curveStartTime <= mDuration.
    MOZ_ASSERT(stepPos >= 0 && stepPos <= UINT32_MAX - 1);
    uint32_t currentNode = floor(stepPos);
    if (currentNode >= stepCount) {
      auto remaining = aBuffer.From(fillStart);
      std::fill_n(remaining.Elements(), remaining.Length(), mCurve[stepCount]);
      return;
    }

    // Linearly interpolate to fill the buffer elements for any ticks between
    // curve samples k and k + 1 inclusive.
    double tCurrent = curveStartTime + currentNode * timeStep;
    uint32_t nextNode = currentNode + 1;
    double tNext = curveStartTime + nextNode * timeStep;
    // The first buffer index that cannot be filled with these curve samples
    size_t fillEnd = LimitedCountForDuration<TimeType>(
        aBuffer.Length(),
        // This parameter is used only when time is in ticks:
        // If tNext aligns exactly with a tick then fill to tNext, thus
        // ensuring that fillStart is advanced even when timeStep is so small
        // that tNext == tCurrent.
        floor(tNext - aBufferStartTime) + 1.0);
    TimeType fillStartTime =
        aBufferStartTime + static_cast<TimeType>(fillStart);
    FillLinearRamp(fillStartTime, aBuffer.FromTo(fillStart, fillEnd), tCurrent,
                   mCurve[currentNode], tNext, mCurve[nextNode]);
    fillStart = fillEnd;
  }
}

template <class TimeType>
float AudioEventTimeline::ComputeSetTargetStartValue(
    const AudioTimelineEvent* aPreviousEvent, TimeType aTime) {
  mSetTargetStartTime = aTime;
  GetValuesAtTimeHelperInternal(aTime, Span(&mSetTargetStartValue, 1),
                                aPreviousEvent, nullptr);
  return mSetTargetStartValue;
}

template void AudioEventTimeline::CleanupEventsOlderThan(double);
template void AudioEventTimeline::CleanupEventsOlderThan(int64_t);
template <class TimeType>
void AudioEventTimeline::CleanupEventsOlderThan(TimeType aTime) {
  auto TimeOf =
      [](const decltype(mEvents)::const_iterator& aEvent) -> TimeType {
    return aEvent->Time<TimeType>();
  };

  if (mSimpleValue.isSome()) {
    return;  // already only a single event
  }

  // Find first event to keep.  Keep one event prior to aTime.
  auto begin = mEvents.cbegin();
  auto end = mEvents.cend();
  auto event = begin + 1;
  for (; event < end && aTime > TimeOf(event); ++event) {
  }
  auto firstToKeep = event - 1;

  if (firstToKeep->mType != AudioTimelineEvent::SetTarget) {
    // The value is constant if there is a single remaining non-SetTarget event
    // that has already passed.
    if (end - firstToKeep == 1 && aTime >= firstToKeep->EndTime<TimeType>()) {
      mSimpleValue.emplace(firstToKeep->EndValue());
    }
  } else {
    // The firstToKeep event is a SetTarget.  Set its initial value if
    // not already set.  First find the most recent event where the value at
    // the end time of the event is known, either from the event or for
    // SetTarget events because it has already been calculated.  This may not
    // have been calculated if GetValuesAtTime() was not called for the start
    // time of the SetTarget event.
    for (event = firstToKeep;
         event > begin && event->mType == AudioTimelineEvent::SetTarget &&
         TimeOf(event) > mSetTargetStartTime.Get<TimeType>();
         --event) {
    }
    // Compute SetTarget start times.
    for (; event < firstToKeep; ++event) {
      MOZ_ASSERT((event + 1)->mType == AudioTimelineEvent::SetTarget);
      ComputeSetTargetStartValue(&*event, TimeOf(event + 1));
    }
  }
  if (firstToKeep == begin) {
    return;
  }

  mEvents.RemoveElementsRange(begin, firstToKeep);
}

// This method computes the AudioParam value at a given time based on the event
// timeline
template <class TimeType>
void AudioEventTimeline::GetValuesAtTimeHelper(TimeType aTime, float* aBuffer,
                                               const size_t aSize) {
  MOZ_ASSERT(aBuffer);
  MOZ_ASSERT(aSize);

  auto TimeOf = [](const AudioTimelineEvent& aEvent) -> TimeType {
    return aEvent.Time<TimeType>();
  };

  size_t eventIndex = 0;
  const AudioTimelineEvent* previous = nullptr;

  // Let's remove old events except the last one: we need it to calculate some
  // curves.
  CleanupEventsOlderThan(aTime);

  for (size_t bufferIndex = 0; bufferIndex < aSize;) {
    bool timeMatchesEventIndex = false;
    const AudioTimelineEvent* next;
    for (;; ++eventIndex) {
      if (eventIndex >= mEvents.Length()) {
        next = nullptr;
        break;
      }

      next = &mEvents[eventIndex];
      if (aTime < TimeOf(*next)) {
        break;
      }

#ifdef DEBUG
      MOZ_ASSERT(next->mType == AudioTimelineEvent::SetValueAtTime ||
                 next->mType == AudioTimelineEvent::SetTarget ||
                 next->mType == AudioTimelineEvent::LinearRamp ||
                 next->mType == AudioTimelineEvent::ExponentialRamp ||
                 next->mType == AudioTimelineEvent::SetValueCurve);
#endif

      if (TimesEqual(aTime, TimeOf(*next))) {
        timeMatchesEventIndex = true;
        aBuffer[bufferIndex] = GetValueAtTimeOfEvent<TimeType>(next, previous);
        // Advance to next event, which may or may not have the same time.
      }
      previous = next;
    }

    if (timeMatchesEventIndex) {
      // The time matches one of the events exactly.
      MOZ_ASSERT(TimesEqual(aTime, TimeOf(mEvents[eventIndex - 1])));
      ++bufferIndex;
      ++aTime;
    } else {
      size_t count = aSize - bufferIndex;
      if (next) {
        count = LimitedCountForDuration<TimeType>(count, TimeOf(*next) - aTime);
      }
      GetValuesAtTimeHelperInternal(aTime, Span(aBuffer + bufferIndex, count),
                                    previous, next);
      bufferIndex += count;
      aTime += static_cast<TimeType>(count);
    }
  }
}
template void AudioEventTimeline::GetValuesAtTimeHelper(double aTime,
                                                        float* aBuffer,
                                                        const size_t aSize);
template void AudioEventTimeline::GetValuesAtTimeHelper(int64_t aTime,
                                                        float* aBuffer,
                                                        const size_t aSize);

template <class TimeType>
float AudioEventTimeline::GetValueAtTimeOfEvent(
    const AudioTimelineEvent* aEvent, const AudioTimelineEvent* aPrevious) {
  TimeType time = aEvent->Time<TimeType>();
  switch (aEvent->mType) {
    case AudioTimelineEvent::SetTarget:
      // Start the curve, from the last value of the previous event.
      return ComputeSetTargetStartValue(aPrevious, time);
    case AudioTimelineEvent::SetValueCurve:
      return aEvent->StartValue();
    default:
      // For other event types
      return aEvent->NominalValue();
  }
}

template <class TimeType>
void AudioEventTimeline::GetValuesAtTimeHelperInternal(
    TimeType aStartTime, Span<float> aBuffer,
    const AudioTimelineEvent* aPrevious, const AudioTimelineEvent* aNext) {
  MOZ_ASSERT(aBuffer.Length() >= 1);
  MOZ_ASSERT((std::is_same_v<TimeType, int64_t>) || aBuffer.Length() == 1);

  // If the requested time is before all of the existing events
  if (!aPrevious) {
    std::fill_n(aBuffer.Elements(), aBuffer.Length(), mDefaultValue);
    return;
  }

  auto TimeOf = [](const AudioTimelineEvent* aEvent) -> TimeType {
    return aEvent->Time<TimeType>();
  };
  auto EndTimeOf = [](const AudioTimelineEvent* aEvent) -> double {
    return aEvent->EndTime<TimeType>();
  };

  // SetTarget nodes can be handled no matter what their next node is (if
  // they have one)
  if (aPrevious->mType == AudioTimelineEvent::SetTarget) {
    aPrevious->FillTargetApproach(aStartTime, aBuffer, mSetTargetStartValue);
    return;
  }

  // SetValueCurve events can be handled no matter what their next node is
  // (if they have one), when aStartTime is in the curve region.
  if (aPrevious->mType == AudioTimelineEvent::SetValueCurve) {
    double remainingDuration =
        TimeOf(aPrevious) - aStartTime + aPrevious->Duration();
    if (remainingDuration >= 0.0) {
      // aBuffer.Length() is 1 if remainingDuration is not in ticks.
      size_t count = LimitedCountForDuration<TimeType>(
          aBuffer.Length(),
          // This parameter is used only when time is in ticks:
          // Fill the last tick in the curve before possible ramps below.
          floor(remainingDuration) + 1.0);
      // GetValueAtTimeOfEvent() will set the value at the end of the curve if
      // another event immediately follows.
      MOZ_ASSERT(!aNext ||
                 aStartTime + static_cast<TimeType>(count - 1) < TimeOf(aNext));
      aPrevious->FillFromValueCurve(aStartTime,
                                    Span(aBuffer.Elements(), count));
      aBuffer = aBuffer.From(count);
      if (aBuffer.Length() == 0) {
        return;
      }
      aStartTime += static_cast<TimeType>(count);
    }
  }

  // Handle the cases where our range ends up in a ramp event
  if (aNext) {
    switch (aNext->mType) {
      case AudioTimelineEvent::LinearRamp:
        FillLinearRamp(aStartTime, aBuffer, EndTimeOf(aPrevious),
                       aPrevious->EndValue(), TimeOf(aNext),
                       aNext->NominalValue());
        return;
      case AudioTimelineEvent::ExponentialRamp:
        FillExponentialRamp(aStartTime, aBuffer, EndTimeOf(aPrevious),
                            aPrevious->EndValue(), TimeOf(aNext),
                            aNext->NominalValue());
        return;
      case AudioTimelineEvent::SetValueAtTime:
      case AudioTimelineEvent::SetTarget:
      case AudioTimelineEvent::SetValueCurve:
        break;
      case AudioTimelineEvent::Cancel:
      case AudioTimelineEvent::Track:
        MOZ_ASSERT(false, "Should have been handled earlier.");
    }
  }

  // Now handle all other cases
  switch (aPrevious->mType) {
    case AudioTimelineEvent::SetValueAtTime:
    case AudioTimelineEvent::LinearRamp:
    case AudioTimelineEvent::ExponentialRamp:
      break;
    case AudioTimelineEvent::SetValueCurve:
      MOZ_ASSERT(aStartTime - TimeOf(aPrevious) >= aPrevious->Duration());
      break;
    case AudioTimelineEvent::SetTarget:
      MOZ_FALLTHROUGH_ASSERT("AudioTimelineEvent::SetTarget");
    case AudioTimelineEvent::Cancel:
    case AudioTimelineEvent::Track:
      MOZ_ASSERT(false, "Should have been handled earlier.");
  }
  // If the next event type is neither linear or exponential ramp, the
  // value is constant.
  std::fill_n(aBuffer.Elements(), aBuffer.Length(), aPrevious->EndValue());
}

}  // namespace mozilla::dom
