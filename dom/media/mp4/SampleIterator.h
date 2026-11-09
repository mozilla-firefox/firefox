/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_MEDIA_MP4_SAMPLE_ITERATOR_H_
#define DOM_MEDIA_MP4_SAMPLE_ITERATOR_H_

#include <map>

#include "ByteStream.h"
#include "DecoderDoctorLogger.h"
#include "MP4Interval.h"
#include "MediaData.h"
#include "MediaResource.h"
#include "MoofParser.h"
#include "TimeUnits.h"
#include "mozilla/ResultVariant.h"
#include "nsISupportsImpl.h"

namespace mozilla {

struct CencSampleEncryptionInfoEntry;
class IndiceWrapper;
class MP4SampleIndex;
struct Sample;

struct SampleLocation {
  size_t mMoof;
  size_t mSample;
  constexpr auto operator<=>(const SampleLocation& aOther) const = default;
};

DDLoggedTypeDeclName(SampleIterator);

class SampleIterator : public DecoderDoctorLifeLogger<SampleIterator> {
 public:
  explicit SampleIterator(MP4SampleIndex* aIndex);
  ~SampleIterator();
  bool HasNext();
  already_AddRefed<mozilla::MediaRawData> GetNextHeader();
  Result<already_AddRefed<mozilla::MediaRawData>, MediaResult> GetNext();
  SampleLocation CurrentPosition() const;

  // Seek to the last keyframe at or before the target. If it's confirmed that
  // that doesn't exist, try seeking to the next one after. If that also fails,
  // the iterator is reset to the first sample.
  void Seek(const media::TimeUnit& aTime);

  // Advance to the first keyframe at or after a threshold time according to
  // current information.
  // Best effort, the caller has to verify the sample is really a keyframe.
  void TrySkipToNextKeyframe(const media::TimeUnit& aTimeThreshold);
  media::TimeUnit GetNextKeyframeTime();

  // Save definitive result of codec based keyframe analysis for the sample
  // returned by the last call to `GetNext`.
  // `GetNext` must have been called before calling this.
  void ConfirmCurrentIsKeyframe(bool aIsKeyframe);

 private:
  Result<Sample*, nsresult> Get();

  // Gets the sample description entry for the current moof, or nullptr if
  // called without a valid current moof.
  SampleDescriptionEntry* GetSampleDescriptionEntry();
  const CencSampleEncryptionInfoEntry* GetSampleEncryptionEntry() const;

  // Determines the encryption scheme in use for the current sample. If the
  // the scheme cannot be unambiguously determined, will return an error with
  // the reason.
  //
  // Returns: Ok(CryptoScheme) if a crypto scheme, including None, can be
  // determined, or Err(nsCString) if there is an issue determining the scheme.
  Result<CryptoScheme, nsCString> GetEncryptionScheme();

  void Next();
  RefPtr<MP4SampleIndex> mIndex;
  friend class MP4SampleIndex;
  size_t mCurrentMoof;
  size_t mCurrentSample;
};

DDLoggedTypeDeclName(KeyframeMap);

class KeyframeMap : public DecoderDoctorLifeLogger<KeyframeMap> {
 public:
  typedef media::Interval<media::TimeUnit> Interval;

  // Source of keyframe information. `Container` means stss or sample flags,
  // `Codec` means keyframes are confirmed by codec-specific packet analysis.
  // When marking keyframes, `Codec` overrides `Container` information.
  MOZ_DEFINE_ENUM_CLASS_WITH_TOSTRING_AT_CLASS_SCOPE(KeyframeInfo,
                                                     (Container, Codec));

  struct Entry {
    Interval mCompositionRange;
    SampleLocation mLocation;
    KeyframeInfo mSource;
    bool operator==(const Entry& other) const = default;
  };

  void MarkKeyframe(const Interval& aCompositionRange, SampleLocation aLocation,
                    KeyframeInfo aSource);
  void MarkNotKeyframe(const Interval& aCompositionRange, KeyframeInfo aSource);
  void Clear();

  // Get last keyframe up to and including a timestamp.
  Maybe<Entry> GetNearestUpTo(const media::TimeUnit& aTime) const;

  // Get first keyframe at or after a timestamp.
  Maybe<Entry> GetNextAfter(const media::TimeUnit& aTime) const;

  // Get first keyframe at or after a timestamp starting from the given sample
  // index.
  Maybe<Entry> GetNextAfter(const media::TimeUnit& aTime,
                            const SampleLocation& aLocation) const;
  Maybe<Entry> Get(const media::TimeUnit& aTime) const;

  // Whether the codec level keyframe analysis covers this timestamp.
  bool HaveChecked(const media::TimeUnit& aTime) const {
    return mCheckedIntervals.Contains(aTime);
  };

  // Whether the codec level keyframe analysis covers this interval.
  bool HaveChecked(const Interval& aInterval) const {
    return mCheckedIntervals.Contains(aInterval);
  }
  Maybe<media::TimeUnit> MinSampleTime() const { return mMinSampleTime; };
  Maybe<media::TimeUnit> MaxSampleTime() const { return mMaxSampleTime; };

 private:
  Maybe<media::TimeUnit> mMinSampleTime;
  Maybe<media::TimeUnit> mMaxSampleTime;
  std::map<media::TimeUnit, Entry> mMap;
  // Time ranges where keyframe info is definitive
  media::IntervalSet<media::TimeUnit> mCheckedIntervals;
};

DDLoggedTypeDeclName(MP4SampleIndex);

class MP4SampleIndex : public DecoderDoctorLifeLogger<MP4SampleIndex> {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MP4SampleIndex)

  struct Indice {
    uint64_t start_offset;
    uint64_t end_offset;
    int64_t start_composition;
    int64_t end_composition;
    int64_t start_decode;
    bool sync;
  };

  struct MP4DataOffset {
    MP4DataOffset(uint32_t aIndex, int64_t aStartOffset)
        : mIndex(aIndex), mStartOffset(aStartOffset), mEndOffset(0) {}

    bool operator==(int64_t aStartOffset) const {
      return mStartOffset == aStartOffset;
    }

    bool operator!=(int64_t aStartOffset) const {
      return mStartOffset != aStartOffset;
    }

    bool operator<(int64_t aStartOffset) const {
      return mStartOffset < aStartOffset;
    }

    struct EndOffsetComparator {
      bool Equals(const MP4DataOffset& a, const int64_t& b) const {
        return a.mEndOffset == b;
      }

      bool LessThan(const MP4DataOffset& a, const int64_t& b) const {
        return a.mEndOffset < b;
      }
    };

    uint32_t mIndex;
    int64_t mStartOffset;
    int64_t mEndOffset;
    MP4Interval<media::TimeUnit> mTime;
  };

  MP4SampleIndex(const mozilla::IndiceWrapper& aIndices, ByteStream* aSource,
                 uint32_t aTrackId, bool aIsAudio, uint32_t aTimeScale);

  void UpdateMoofIndex(const mozilla::MediaByteRangeSet& aByteRanges,
                       bool aCanEvict);
  void UpdateMoofIndex(const mozilla::MediaByteRangeSet& aByteRanges);
  mozilla::media::TimeIntervals ConvertByteRangesToTimeRanges(
      const mozilla::MediaByteRangeSet& aByteRanges);
  uint64_t GetEvictionOffset(const media::TimeUnit& aTime);
  bool IsFragmented() { return !!mMoofParser; }

  // Return result of codec based keyframe analysis, if it exists.
  Maybe<bool> IsKeyframe(const media::TimeUnit& aTime) const;
  size_t NumSamplesBetween(const SampleLocation& aStart,
                           const SampleLocation& aEnd) const;
  // Whether the nearest keyframe before and after the given time are known with
  // certainty i.e., they themselves and all samples between them have been
  // verified on the codec level.
  bool HaveCheckedSurroundingKeyframes(const media::TimeUnit& aTime) const;

  friend class SampleIterator;

 private:
  ~MP4SampleIndex();
  void RegisterIterator(SampleIterator* aIterator);
  void UnregisterIterator(SampleIterator* aIterator);

  // Call mMoofParser->BlockingReadNextMoof and update keyframe map with new
  // samples.
  nsresult BlockingReadNextMoof();

  ByteStream* mSource;
  FallibleTArray<Sample> mIndex;
  FallibleTArray<MP4DataOffset> mDataOffset;
  UniquePtr<MoofParser> mMoofParser;
  nsTArray<SampleIterator*> mIterators;

  // ConvertByteRangesToTimeRanges cache
  mozilla::MediaByteRangeSet mLastCachedRanges;
  mozilla::media::TimeIntervals mLastBufferedRanges;
  bool mIsAudio;
  KeyframeMap mKeyframeMap;
};
}  // namespace mozilla

#endif
