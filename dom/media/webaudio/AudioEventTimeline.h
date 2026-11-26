/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef AudioEventTimeline_h_
#define AudioEventTimeline_h_

#include "MainThreadUtils.h"
#include "WebAudioUtils.h"
#include "math.h"
#include "mozilla/Assertions.h"
#include "mozilla/DefineEnum.h"
#include "mozilla/ErrorResult.h"
#include "nsTArray.h"

// XXX Avoid including this here by moving function bodies to the cpp file
#include "js/GCAPI.h"

namespace mozilla {

class AudioNodeTrack;

namespace dom {

struct AudioTimelineEvent {
  MOZ_DEFINE_ENUM_WITH_BASE_AND_TOSTRING_AT_CLASS_SCOPE(
      Type, uint32_t,
      (SetValueAtTime, LinearRamp, ExponentialRamp, SetTarget, SetValueCurve,
       Track, Cancel));

  class TimeUnion {
   public:
    // double 0.0 is bit-identical to int64_t 0.
    TimeUnion()
        : mSeconds()
#if DEBUG
          ,
          mIsInSeconds(true),
          mIsInTicks(true)
#endif
    {
    }
    explicit TimeUnion(double aTime)
        : mSeconds(aTime)
#if DEBUG
          ,
          mIsInSeconds(true),
          mIsInTicks(false)
#endif
    {
    }
    explicit TimeUnion(int64_t aTime)
        : mTicks(aTime)
#if DEBUG
          ,
          mIsInSeconds(false),
          mIsInTicks(true)
#endif
    {
    }

    double operator=(double aTime) {
#if DEBUG
      mIsInSeconds = true;
      mIsInTicks = true;
#endif
      return mSeconds = aTime;
    }
    int64_t operator=(int64_t aTime) {
#if DEBUG
      mIsInSeconds = true;
      mIsInTicks = true;
#endif
      return mTicks = aTime;
    }

    template <class TimeType>
    TimeType Get() const;

   private:
    union {
      double mSeconds;
      int64_t mTicks;
    };
#ifdef DEBUG
    bool mIsInSeconds;
    bool mIsInTicks;

   public:
    bool IsInTicks() const { return mIsInTicks; };
#endif
  };

  AudioTimelineEvent(Type aType, double aTime, float aValue,
                     double aTimeConstant = 0.0);
  // For SetValueCurve
  AudioTimelineEvent(Type aType, const nsTArray<float>& aValues,
                     double aStartTime, double aDuration);
  AudioTimelineEvent(const AudioTimelineEvent& rhs);
  ~AudioTimelineEvent();

  template <class TimeType>
  TimeType Time() const {
    return mTime.Get<TimeType>();
  }
  // If this event is a curve event, this returns the end time of the curve.
  // Otherwise, this returns the time of the event.
  template <class TimeType>
  double EndTime() const;

  float NominalValue() const {
    MOZ_ASSERT(mType != SetValueCurve);
    return mValue;
  }
  float StartValue() const {
    MOZ_ASSERT(mType == SetValueCurve);
    return mCurve[0];
  }
  // Value for an event, or for a ValueCurve event, this is the value of the
  // last element of the curve.
  float EndValue() const;

  double TimeConstant() const {
    MOZ_ASSERT(mType == SetTarget);
    return mTimeConstant;
  }
  uint32_t CurveLength() const {
    MOZ_ASSERT(mType == SetValueCurve);
    return mCurveLength;
  }
  double Duration() const {
    MOZ_ASSERT(mType == SetValueCurve);
    return mDuration;
  }
  /**
   * Converts an AudioTimelineEvent's floating point time members to tick
   * values with respect to a destination AudioNodeTrack.
   *
   * This needs to be called for each AudioTimelineEvent that gets sent to an
   * AudioNodeEngine, on the engine side where the AudioTimlineEvent is
   * received.  This means that such engines need to be aware of their
   * destination tracks as well.
   */
  void ConvertToTicks(AudioNodeTrack* aDestination);

  template <class TimeType>
  void FillTargetApproach(TimeType aBufferStartTime, Span<float> aBuffer,
                          double v0) const;
  template <class TimeType>
  void FillFromValueCurve(TimeType aBufferStartTime, Span<float> aBuffer) const;

  const Type mType;

 private:
  union {
    float mValue;
    uint32_t mCurveLength;  // for SetValueCurve
  };
  union {
    double mTimeConstant;
    // mCurve contains a buffer of SetValueCurve samples.  We sample the
    // values in the buffer depending on how far along we are in time.
    // If we're at time T and the event has started as time T0 and has a
    // duration of D, we sample the buffer at floor(mCurveLength*(T-T0)/D)
    // if T<T0+D, and just take the last sample in the buffer otherwise.
    float* mCurve;
  };
  union {
    // mPerTickRatio is used only with SetTarget and int64_t TimeType.
    double mPerTickRatio;
    double mDuration;  // for SetValueCurve
  };

  // This member is accessed using the `Time` method.
  //
  // The time for an event can either be in seconds or in ticks.
  // Initially the time of the event is always in seconds.
  // In order to convert it to ticks, call SetTimeInTicks.  Once this
  // method has been called for an event, the time cannot be converted
  // back to seconds.
  TimeUnion mTime;
};

template <>
inline double AudioTimelineEvent::TimeUnion::Get<double>() const {
  MOZ_ASSERT(mIsInSeconds);
  return mSeconds;
}
template <>
inline int64_t AudioTimelineEvent::TimeUnion::Get<int64_t>() const {
  MOZ_ASSERT(mIsInTicks);
  return mTicks;
}

class AudioEventTimeline {
 public:
  explicit AudioEventTimeline(float aDefaultValue)
      : mDefaultValue(aDefaultValue),
        mSetTargetStartValue(aDefaultValue),
        mSimpleValue(Some(aDefaultValue)) {}

  bool ValidateEvent(const AudioTimelineEvent& aEvent, ErrorResult& aRv) const {
    MOZ_ASSERT(NS_IsMainThread());

    auto TimeOf = [](const AudioTimelineEvent& aEvent) -> double {
      return aEvent.Time<double>();
    };

    // Validate the event itself
    if (!WebAudioUtils::IsTimeValid(TimeOf(aEvent))) {
      aRv.ThrowRangeError<MSG_INVALID_AUDIOPARAM_METHOD_START_TIME_ERROR>();
      return false;
    }

    switch (aEvent.mType) {
      case AudioTimelineEvent::SetValueCurve:
        if (aEvent.CurveLength() < 2) {
          aRv.ThrowInvalidStateError("Curve length must be at least 2");
          return false;
        }
        if (aEvent.Duration() <= 0) {
          aRv.ThrowRangeError(
              "The curve duration for setValueCurveAtTime must be strictly "
              "positive.");
          return false;
        }
        MOZ_ASSERT(IsValid(aEvent.Duration()));
        break;
      case AudioTimelineEvent::SetTarget:
        if (!WebAudioUtils::IsTimeValid(aEvent.TimeConstant())) {
          aRv.ThrowRangeError(
              "The exponential constant passed to setTargetAtTime must be "
              "non-negative.");
          return false;
        }
        [[fallthrough]];
      default:
        MOZ_ASSERT(IsValid(aEvent.NominalValue()));
    }

    // Make sure that new events don't fall within the duration of a
    // curve event.
    for (unsigned i = 0; i < mEvents.Length(); ++i) {
      if (mEvents[i].mType == AudioTimelineEvent::SetValueCurve &&
          TimeOf(mEvents[i]) <= TimeOf(aEvent) &&
          TimeOf(mEvents[i]) + mEvents[i].Duration() > TimeOf(aEvent)) {
        aRv.ThrowNotSupportedError("Can't add events during a curve event");
        return false;
      }
    }

    // Make sure that new curve events don't fall in a range which includes
    // other events.
    if (aEvent.mType == AudioTimelineEvent::SetValueCurve) {
      for (unsigned i = 0; i < mEvents.Length(); ++i) {
        if (TimeOf(aEvent) < TimeOf(mEvents[i]) &&
            TimeOf(aEvent) + aEvent.Duration() > TimeOf(mEvents[i])) {
          aRv.ThrowNotSupportedError(
              "Can't add curve events that overlap other events");
          return false;
        }
      }
    }

    // Make sure that invalid values are not used for exponential curves
    if (aEvent.mType == AudioTimelineEvent::ExponentialRamp) {
      if (aEvent.NominalValue() == 0.f) {
        aRv.ThrowRangeError(
            "The value passed to exponentialRampToValueAtTime must be "
            "non-zero.");
        return false;
      }
    }
    return true;
  }

  template <typename TimeType>
  void InsertEvent(const AudioTimelineEvent& aEvent) {
    mSimpleValue.reset();
    for (unsigned i = 0; i < mEvents.Length(); ++i) {
      if (aEvent.Time<TimeType>() == mEvents[i].Time<TimeType>()) {
        // If two events happen at the same time, have them in chronological
        // order of insertion.
        do {
          ++i;
        } while (i < mEvents.Length() &&
                 aEvent.Time<TimeType>() == mEvents[i].Time<TimeType>());
        mEvents.InsertElementAt(i, aEvent);
        return;
      }
      // Otherwise, place the event right after the latest existing event
      if (aEvent.Time<TimeType>() < mEvents[i].Time<TimeType>()) {
        mEvents.InsertElementAt(i, aEvent);
        return;
      }
    }

    // If we couldn't find a place for the event, just append it to the list
    mEvents.AppendElement(aEvent);
  }

  bool HasSimpleValue() const { return mSimpleValue.isSome(); }

  float GetValue() const {
    // This method should only be called if HasSimpleValue() returns true
    MOZ_ASSERT(HasSimpleValue());
    return mSimpleValue.value();
  }

  template <typename TimeType>
  void CancelScheduledValues(TimeType aCancelStartTime) {
    for (unsigned i = 0; i < mEvents.Length(); ++i) {
      if (mEvents[i].Time<TimeType>() >= aCancelStartTime ||
          (mEvents[i].mType == AudioTimelineEvent::SetValueCurve &&
           // If the curve extends past the cancel time, it is cancelled.
           // The event is not removed on equality;
           // see https://github.com/WebAudio/web-audio-api/issues/2643
           mEvents[i].EndTime<TimeType>() > aCancelStartTime)) {
#ifdef DEBUG
        // Sanity check: the array should be sorted, so all of the following
        // events should have a time greater than aCancelStartTime too.
        for (unsigned j = i + 1; j < mEvents.Length(); ++j) {
          MOZ_ASSERT(mEvents[j].Time<TimeType>() >= aCancelStartTime);
        }
#endif
        mEvents.TruncateLength(i);
        break;
      }
    }
    if (mEvents.IsEmpty()) {
      mSimpleValue = Some(mDefaultValue);
    }
  }

  static bool TimesEqual(int64_t aLhs, int64_t aRhs) { return aLhs == aRhs; }

  // Since we are going to accumulate error by adding 0.01 multiple time in a
  // loop, we want to fuzz the equality check in GetValueAtTime.
  static bool TimesEqual(double aLhs, double aRhs) {
    const float kEpsilon = 0.0000000001f;
    return fabs(aLhs - aRhs) < kEpsilon;
  }

  template <class TimeType>
  float GetValueAtTime(TimeType aTime) {
    float result;
    GetValuesAtTimeHelper(aTime, &result, 1);
    return result;
  }

  void GetValuesAtTime(int64_t aTime, float* aBuffer, const size_t aSize) {
    MOZ_ASSERT(aBuffer);
    GetValuesAtTimeHelper(aTime, aBuffer, aSize);
  }
  void GetValuesAtTime(double aTime, float* aBuffer,
                       const size_t aSize) = delete;

  // Return the number of events scheduled
  uint32_t GetEventCount() const { return mEvents.Length(); }

  template <class TimeType>
  void CleanupEventsOlderThan(TimeType aTime);

 private:
  template <class TimeType>
  void GetValuesAtTimeHelper(TimeType aTime, float* aBuffer,
                             const size_t aSize);

  template <class TimeType>
  float GetValueAtTimeOfEvent(const AudioTimelineEvent* aEvent,
                              const AudioTimelineEvent* aPrevious);

  template <class TimeType>
  void GetValuesAtTimeHelperInternal(TimeType aStartTime, Span<float> aBuffer,
                                     const AudioTimelineEvent* aPrevious,
                                     const AudioTimelineEvent* aNext);

  static bool IsValid(double value) { return std::isfinite(value); }

  template <class TimeType>
  float ComputeSetTargetStartValue(const AudioTimelineEvent* aPreviousEvent,
                                   TimeType aTime);

  // This is a sorted array of the events in the timeline.  Queries of this
  // data structure should probably be more frequent than modifications to it,
  // and that is the reason why we're using a simple array as the data
  // structure. We can optimize this in the future if the performance of the
  // array ends up being a bottleneck.
  nsTArray<AudioTimelineEvent> mEvents;
  float mDefaultValue;
  // This is the value of this AudioParam at the end of the previous
  // event for SetTarget curves.
  float mSetTargetStartValue;
  AudioTimelineEvent::TimeUnion mSetTargetStartTime;
  Maybe<float> mSimpleValue;
};

}  // namespace dom
}  // namespace mozilla

#endif
