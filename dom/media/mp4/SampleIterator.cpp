/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SampleIterator.h"

#include <algorithm>
#include <limits>

#include "BufferReader.h"
#include "MP4Interval.h"
#include "MP4Metadata.h"
#include "MediaDataDemuxer.h"
#include "SinfParser.h"
#include "mozilla/RefPtr.h"

#define LOG(arg, ...)                                                     \
  DDMOZ_LOG_FMT(gMediaDemuxerLog, mozilla::LogLevel::Debug, "::{}: " arg, \
                __func__, ##__VA_ARGS__)

#define LOGV(arg, ...)                                                      \
  DDMOZ_LOG_FMT(gMediaDemuxerLog, mozilla::LogLevel::Verbose, "::{}: " arg, \
                __func__, ##__VA_ARGS__)

using namespace mozilla::media;

namespace mozilla {

class MOZ_STACK_CLASS RangeFinder {
 public:
  // Given that we're processing this in order we don't use a binary search
  // to find the apropriate time range. Instead we search linearly from the
  // last used point.
  explicit RangeFinder(const MediaByteRangeSet& ranges)
      : mRanges(ranges), mIndex(0) {
    // Ranges must be normalised for this to work
  }

  bool Contains(const MediaByteRange& aByteRange);

 private:
  const MediaByteRangeSet& mRanges;
  size_t mIndex;
};

bool RangeFinder::Contains(const MediaByteRange& aByteRange) {
  if (mRanges.IsEmpty()) {
    return false;
  }

  if (mRanges[mIndex].ContainsStrict(aByteRange)) {
    return true;
  }

  if (aByteRange.mStart < mRanges[mIndex].mStart) {
    // Search backwards
    do {
      if (!mIndex) {
        return false;
      }
      --mIndex;
      if (mRanges[mIndex].ContainsStrict(aByteRange)) {
        return true;
      }
    } while (aByteRange.mStart < mRanges[mIndex].mStart);

    return false;
  }

  while (aByteRange.mEnd > mRanges[mIndex].mEnd) {
    if (mIndex == mRanges.Length() - 1) {
      return false;
    }
    ++mIndex;
    if (mRanges[mIndex].ContainsStrict(aByteRange)) {
      return true;
    }
  }

  return false;
}

Maybe<KeyframeMap::Entry> KeyframeMap::GetNearestUpTo(
    const TimeUnit& aTime) const {
  const auto ub = mMap.upper_bound(aTime);
  if (ub != mMap.cbegin()) {
    return Some(std::prev(ub)->second);
  }
  return Nothing();
}

Maybe<KeyframeMap::Entry> KeyframeMap::GetNextAfter(
    const TimeUnit& aTime) const {
  const auto lb = mMap.lower_bound(aTime);
  if (lb != mMap.cend()) {
    return Some(lb->second);
  }
  return Nothing();
}

Maybe<KeyframeMap::Entry> KeyframeMap::GetNextAfter(
    const TimeUnit& aTime, const SampleLocation& aLocation) const {
  for (auto it = mMap.lower_bound(aTime); it != mMap.cend(); ++it) {
    if (aLocation <= it->second.mLocation) {
      return Some(it->second);
    }
  }
  return Nothing();
}

Maybe<KeyframeMap::Entry> KeyframeMap::Get(const media::TimeUnit& aTime) const {
  const auto found = mMap.find(aTime);
  if (found != mMap.cend()) {
    return Some(found->second);
  }
  return Nothing();
}

void KeyframeMap::MarkKeyframe(const Interval& aCompositionRange,
                               SampleLocation aLocation, KeyframeInfo aSource) {
  if (!mMinSampleTime || aCompositionRange.mStart < mMinSampleTime.value()) {
    mMinSampleTime = Some(aCompositionRange.mStart);
  }
  if (!mMaxSampleTime || mMaxSampleTime.value() < aCompositionRange.mEnd) {
    mMaxSampleTime = Some(aCompositionRange.mEnd);
  }
  if (aSource == KeyframeInfo::Container &&
      mCheckedIntervals.Contains(aCompositionRange)) {
    // already have `Codec` info, ignore
    return;
  }
  LOGV("Mark keyframe at PTS {} in moof {}, sample {} ({})",
       aCompositionRange.mStart.ToMicroseconds(), aLocation.mMoof,
       aLocation.mSample, EnumValueToString(aSource));
  const Entry entry{aCompositionRange, aLocation, aSource};
  if (aSource == KeyframeMap::KeyframeInfo::Codec) {
    mCheckedIntervals.Add(aCompositionRange);
    mMap.insert_or_assign(aCompositionRange.mStart, entry);
  } else {
    mMap.emplace(aCompositionRange.mStart, entry);
  }
}

void KeyframeMap::MarkNotKeyframe(const Interval& aCompositionRange,
                                  KeyframeInfo aSource) {
  if (!mMinSampleTime || aCompositionRange.mStart < mMinSampleTime.value()) {
    mMinSampleTime = Some(aCompositionRange.mStart);
  }
  if (!mMaxSampleTime || mMaxSampleTime.value() < aCompositionRange.mEnd) {
    mMaxSampleTime = Some(aCompositionRange.mEnd);
  }
  if (aSource == KeyframeInfo::Container) {
    // `Container` can not remove a `Codec` keyframe
    const auto existing = mMap.find(aCompositionRange.mStart);
    if (existing != mMap.cend() &&
        existing->second.mSource == KeyframeInfo::Codec) {
      return;
    }
  } else if (aSource == KeyframeInfo::Codec) {
    mCheckedIntervals.Add(aCompositionRange);
  }
  LOGV("Mark not keyframe at PTS {} ({})",
       aCompositionRange.mStart.ToMicroseconds(), EnumValueToString(aSource));
  mMap.erase(aCompositionRange.mStart);
}

void KeyframeMap::Clear() {
  LOGV("");
  mCheckedIntervals.Clear();
  mMinSampleTime.reset();
  mMaxSampleTime.reset();
  mMap.clear();
}

SampleIterator::SampleIterator(MP4SampleIndex* aIndex)
    : mIndex(aIndex), mCurrentMoof(0), mCurrentSample(0) {
  mIndex->RegisterIterator(this);
}

SampleIterator::~SampleIterator() { mIndex->UnregisterIterator(this); }

bool SampleIterator::HasNext() { return Get().isOk(); }

already_AddRefed<MediaRawData> SampleIterator::GetNextHeader() {
  auto current = Get();
  if (current.isErr()) {
    return nullptr;
  }
  Sample* s = current.unwrap();

  int64_t length = std::numeric_limits<int64_t>::max();
  mIndex->mSource->Length(&length);
  if (s->mByteRange.mEnd > length) {
    // We don't have this complete sample.
    return nullptr;
  }

  RefPtr<MediaRawData> sample = new MediaRawData();
  sample->mTimecode = s->mDecodeTime;
  sample->mTime = s->mCompositionRange.start;
  sample->mDuration = s->mCompositionRange.Length();
  sample->mOffset = s->mByteRange.mStart;
  sample->mKeyframe = s->mSync;
  Next();
  return sample.forget();
}

Result<already_AddRefed<MediaRawData>, MediaResult> SampleIterator::GetNext() {
  auto current = Get();
  if (current.isErr()) {
    return current.propagateErr();
  }
  Sample* s = current.unwrap();

  int64_t length = std::numeric_limits<int64_t>::max();
  mIndex->mSource->Length(&length);
  if (s->mByteRange.mEnd > length) {
    return Err(MediaResult::Logged(
        NS_ERROR_DOM_MEDIA_RANGE_ERR,
        RESULT_DETAIL("Sample data byte range beyond end of resource"),
        gMediaDemuxerLog));
  }

  RefPtr<MediaRawData> sample = new MediaRawData();
  sample->mTimecode = s->mDecodeTime;
  sample->mTime = s->mCompositionRange.start;
  sample->mDuration = s->mCompositionRange.Length();
  sample->mOffset = s->mByteRange.mStart;
  sample->mKeyframe = s->mSync;

  UniquePtr<MediaRawDataWriter> writer(sample->CreateWriter());
  // Do the blocking read
  if (!writer->SetSize(s->mByteRange.Length())) {
    return Err(MediaResult::Logged(NS_ERROR_OUT_OF_MEMORY, __func__,
                                   gMediaDemuxerLog));
  }

  size_t bytesRead;
  nsresult rv = mIndex->mSource->ReadAt(sample->mOffset, writer->Data(),
                                        sample->Size(), &bytesRead);
  if (NS_FAILED(rv) || bytesRead != sample->Size()) {
    return Err(MediaResult::Logged(
        // Fewer bytes read means end of stream, or the bytes are not
        // available because a network error has occurred.
        // A sample range extending past the end of stream is a bad range.
        NS_FAILED(rv) ? rv : NS_ERROR_DOM_MEDIA_RANGE_ERR,
        RESULT_DETAIL("Sample data read failed"), gMediaDemuxerLog));
  }

  MoofParser* moofParser = mIndex->mMoofParser.get();
  if (!moofParser) {
    // File is not fragmented, we can't have crypto, just early return.
    Next();
    return sample.forget();
  }

  const nsTArray<Moof>& moofs = moofParser->Moofs();
  const Moof* currentMoof = &moofs[mCurrentMoof];
  // We need to check if this moof has init data the CDM expects us to surface.
  // This should happen when handling the first sample, even if that sample
  // isn't encrypted (samples later in the moof may be).
  if (mCurrentSample == 0) {
    if (!currentMoof->mPsshes.IsEmpty()) {
      // This Moof contained crypto init data. Report that. We only report
      // the init data on the Moof's first sample, to avoid reporting it more
      // than once per Moof.
      writer->mCrypto.mInitDatas.AppendElements(currentMoof->mPsshes);
      writer->mCrypto.mInitDataType = u"cenc"_ns;
    }
  }

  auto cryptoSchemeResult = GetEncryptionScheme();
  if (cryptoSchemeResult.isErr()) {
    return Err(MediaResult::Logged(NS_ERROR_DOM_MEDIA_DEMUXER_ERR,
                                   cryptoSchemeResult.unwrapErr(),
                                   gMediaDemuxerLog));
  }
  CryptoScheme cryptoScheme = cryptoSchemeResult.unwrap();
  if (cryptoScheme == CryptoScheme::None) {
    // No crypto to handle, early return.
    Next();
    return sample.forget();
  }

  writer->mCrypto.mCryptoScheme = cryptoScheme;
  MOZ_ASSERT(writer->mCrypto.mCryptoScheme != CryptoScheme::None,
             "Should have early returned if we don't have a crypto scheme!");
  MOZ_ASSERT(writer->mCrypto.mKeyId.IsEmpty(),
             "Sample should not already have a key ID");
  MOZ_ASSERT(writer->mCrypto.mConstantIV.IsEmpty(),
             "Sample should not already have a constant IV");
  const CencSampleEncryptionInfoEntry* sampleInfo = GetSampleEncryptionEntry();
  if (sampleInfo) {
    // Use sample group information if present, this supersedes track level
    // information.
    writer->mCrypto.mKeyId.AppendElements(sampleInfo->mKeyId);
    writer->mCrypto.mIVSize = sampleInfo->mIVSize;
    writer->mCrypto.mCryptByteBlock = sampleInfo->mCryptByteBlock;
    writer->mCrypto.mSkipByteBlock = sampleInfo->mSkipByteBlock;
    writer->mCrypto.mConstantIV.AppendElements(sampleInfo->mConsantIV);
  } else {
    // Use the crypto info from track metadata
    writer->mCrypto.mKeyId.AppendElements(moofParser->mSinf.mDefaultKeyID, 16);
    writer->mCrypto.mIVSize = moofParser->mSinf.mDefaultIVSize;
    writer->mCrypto.mCryptByteBlock = moofParser->mSinf.mDefaultCryptByteBlock;
    writer->mCrypto.mSkipByteBlock = moofParser->mSinf.mDefaultSkipByteBlock;
    writer->mCrypto.mConstantIV.AppendElements(
        moofParser->mSinf.mDefaultConstantIV);
  }

  if ((writer->mCrypto.mIVSize == 0 && writer->mCrypto.mConstantIV.IsEmpty()) ||
      (writer->mCrypto.mIVSize != 0 &&
       (s->mCencRange.IsEmpty() && !currentMoof->SencIsValid()))) {
    // If mIVSize == 0, this indicates that a constant IV is in use, thus we
    // should have a non empty constant IV. Alternatively if IV size is non
    // zero, we should have an IV for this sample, which we need to look up
    // in mCencRange (which must then be non empty). If neither of these are
    // true we have bad crypto data, so bail.
    return Err(MediaResult::Logged(NS_ERROR_DOM_MEDIA_DEMUXER_ERR,
                                   RESULT_DETAIL("Crypto IV size inconsistent"),
                                   gMediaDemuxerLog));
  }
  // Retrieve encryption information
  // This information might come from two places: the senc box, or the
  // auxiliary data (indicated by saio and saiz boxes)
  // Try to use senc information first, and fallback to auxiliary data if not
  // present
  if (currentMoof->SencIsValid()) {
    if (writer->mCrypto.mIVSize != s->mIV.Length()) {
      return Err(MediaResult::Logged(
          NS_ERROR_DOM_MEDIA_DEMUXER_ERR,
          RESULT_DETAIL("Inconsistent crypto IV size"), gMediaDemuxerLog));
    }
    writer->mCrypto.mIV = s->mIV;
    writer->mCrypto.mPlainSizes = s->mPlainSizes;
    writer->mCrypto.mEncryptedSizes = s->mEncryptedSizes;
  } else if (!s->mCencRange.IsEmpty()) {
    // The size comes from an 8 bit field
    AutoTArray<uint8_t, 256> cencAuxInfo;
    cencAuxInfo.SetLength(s->mCencRange.Length());
    // Sample Auxiliary Information may be stored anywhere in the file, but
    // encryption is supported in only fragmented mp4, so the offsets are
    // assumed in the traf or a subsequent box.
    rv = mIndex->mSource->ReadAt(s->mCencRange.mStart, cencAuxInfo.Elements(),
                                 cencAuxInfo.Length(), &bytesRead);
    if (NS_FAILED(rv) || bytesRead != cencAuxInfo.Length()) {
      return Err(MediaResult::Logged(
          // Unless pref "eme.mse-only" is set to false, encryption is supported
          // only in MSE, where fewer bytes means end of stream.  mCencRange
          // extending past the end of stream means an error with the range.
          NS_FAILED(rv) ? rv : NS_ERROR_DOM_MEDIA_RANGE_ERR,
          RESULT_DETAIL("cenc Sample Auxiliary Information read failed"),
          gMediaDemuxerLog));
    }
    BufferReader reader(cencAuxInfo);
    if (!reader.ReadArray(writer->mCrypto.mIV, writer->mCrypto.mIVSize)) {
      return Err(MediaResult::Logged(
          NS_ERROR_DOM_MEDIA_DEMUXER_ERR,
          RESULT_DETAIL("sample InitializationVector error"),
          gMediaDemuxerLog));
    }

    // Parse the auxiliary information for subsample information
    auto res = reader.ReadU16();
    if (res.isOk() && res.unwrap() > 0) {
      uint16_t count = res.unwrap();

      for (size_t i = 0; i < count; i++) {
        auto res_16 = reader.ReadU16();
        auto res_32 = reader.ReadU32();
        if (res_16.isErr() || res_32.isErr()) {
          return Err(MediaResult::Logged(
              NS_ERROR_DOM_MEDIA_DEMUXER_ERR,
              RESULT_DETAIL("cenc subsample_count too large for"
                            "CencSampleAuxiliaryDataFormat"),
              gMediaDemuxerLog));
        }
        writer->mCrypto.mPlainSizes.AppendElement(res_16.unwrap());
        writer->mCrypto.mEncryptedSizes.AppendElement(res_32.unwrap());
      }
    } else {
      // No subsample information means the entire sample is encrypted.
      writer->mCrypto.mPlainSizes.AppendElement(0);
      writer->mCrypto.mEncryptedSizes.AppendElement(sample->Size());
    }
  }

  Next();

  return sample.forget();
}

SampleLocation SampleIterator::CurrentPosition() const {
  return {mCurrentMoof, mCurrentSample};
}

void SampleIterator::ConfirmCurrentIsKeyframe(bool aIsKeyframe) {
  // must be called after GetNext(), which incremented mCurrentSample in Next()
  if (NS_WARN_IF(mCurrentSample == 0) || NS_WARN_IF(mIndex->mIsAudio)) {
    return;
  }
  SampleLocation location{mCurrentMoof, mCurrentSample - 1};
  const Sample& sample = mIndex->mMoofParser
                             ? mIndex->mMoofParser->Moofs()[location.mMoof]
                                   .mIndex[location.mSample]
                             : mIndex->mIndex[location.mSample];
  KeyframeMap::Interval interval(sample.mCompositionRange.start,
                                 sample.mCompositionRange.end);
  if (aIsKeyframe) {
    mIndex->mKeyframeMap.MarkKeyframe(interval, location,
                                      KeyframeMap::KeyframeInfo::Codec);
  } else {
    mIndex->mKeyframeMap.MarkNotKeyframe(interval,
                                         KeyframeMap::KeyframeInfo::Codec);
  }
}

SampleDescriptionEntry* SampleIterator::GetSampleDescriptionEntry() {
  nsTArray<Moof>& moofs = mIndex->mMoofParser->Moofs();
  Moof& currentMoof = moofs[mCurrentMoof];
  uint32_t sampleDescriptionIndex =
      currentMoof.mTfhd.mDefaultSampleDescriptionIndex;
  // Mp4 indices start at 1, shift down 1 so we index our array correctly.
  sampleDescriptionIndex--;
  FallibleTArray<SampleDescriptionEntry>& sampleDescriptions =
      mIndex->mMoofParser->mSampleDescriptions;
  if (sampleDescriptionIndex >= sampleDescriptions.Length()) {
    // The sample description index is invalid, the mp4 is malformed. Bail out.
    return nullptr;
  }
  return &sampleDescriptions[sampleDescriptionIndex];
}

const CencSampleEncryptionInfoEntry* SampleIterator::GetSampleEncryptionEntry()
    const {
  return mIndex->mMoofParser->GetSampleEncryptionEntry(mCurrentMoof,
                                                       mCurrentSample);
}

Result<CryptoScheme, nsCString> SampleIterator::GetEncryptionScheme() {
  // See ISO/IEC 23001-7 for information on the metadata being checked.
  MoofParser* moofParser = mIndex->mMoofParser.get();
  if (!moofParser) {
    // This mp4 isn't fragmented so it can't be encrypted.
    return CryptoScheme::None;
  }

  SampleDescriptionEntry* sampleDescriptionEntry = GetSampleDescriptionEntry();
  if (!sampleDescriptionEntry) {
    // For the file to be valid the tfhd must reference a sample description
    // entry.
    // If we encounter this error often, we may consider using the first
    // sample description entry if the index is out of bounds.
    return mozilla::Err(RESULT_DETAIL(
        "Could not determine encryption scheme due to bad index for sample "
        "description entry."));
  }

  if (!sampleDescriptionEntry->mIsEncryptedEntry) {
    return CryptoScheme::None;
  }

  if (!moofParser->mSinf.IsValid()) {
    // The sample description entry says this sample is encrypted, but we
    // don't have a valid sinf box. This shouldn't happen as the sinf box is
    // part of the sample description entry. Suggests a malformed file, bail.
    return mozilla::Err(RESULT_DETAIL(
        "Could not determine encryption scheme. Sample description entry "
        "indicates encryption, but could not find associated sinf box."));
  }

  const CencSampleEncryptionInfoEntry* sampleInfo = GetSampleEncryptionEntry();
  if (sampleInfo && !sampleInfo->mIsEncrypted) {
    // May not have sample encryption info, but if we do, it should match other
    // metadata.
    return mozilla::Err(RESULT_DETAIL(
        "Could not determine encryption scheme. Sample description entry "
        "indicates encryption, but sample encryption entry indicates sample is "
        "not encrypted. These should be consistent."));
  }

  if (moofParser->mSinf.mDefaultEncryptionType == AtomType("cenc")) {
    return CryptoScheme::Cenc;
  } else if (moofParser->mSinf.mDefaultEncryptionType == AtomType("cbcs")) {
    return CryptoScheme::Cbcs;
  }
  return mozilla::Err(RESULT_DETAIL(
      "Could not determine encryption scheme. Sample description entry "
      "reports sample is encrypted, but no scheme, or an unsupported scheme "
      "is in use."));
}

Result<Sample*, nsresult> SampleIterator::Get() {
  if (!mIndex->mMoofParser) {
    MOZ_ASSERT(!mCurrentMoof);
    if (mCurrentSample >= mIndex->mIndex.Length()) {
      return Err(NS_ERROR_DOM_MEDIA_END_OF_STREAM);
    }
    return &mIndex->mIndex[mCurrentSample];
  }

  nsTArray<Moof>& moofs = mIndex->mMoofParser->Moofs();
  while (true) {
    if (mCurrentMoof == moofs.Length()) {
      nsresult rv = mIndex->BlockingReadNextMoof();
      if (NS_FAILED(rv)) {
        return Err(rv);
      }
      MOZ_ASSERT(mCurrentMoof < moofs.Length());
    }
    if (mCurrentSample < moofs[mCurrentMoof].mIndex.Length()) {
      break;
    }
    mCurrentSample = 0;
    ++mCurrentMoof;
  }
  return &moofs[mCurrentMoof].mIndex[mCurrentSample];
}

void SampleIterator::Next() { ++mCurrentSample; }

void SampleIterator::Seek(const TimeUnit& aTime) {
  mCurrentSample = 0;
  mCurrentMoof = 0;
  if (!mIndex->mIsAudio) {
    auto entry = mIndex->mKeyframeMap.GetNearestUpTo(aTime);
    if (!entry && mIndex->HaveCheckedSurroundingKeyframes(aTime)) {
      LOG("No keyframe before target {}, seek to next one after",
          aTime.ToString());
      entry = mIndex->mKeyframeMap.GetNextAfter(aTime);
    }
    if (entry) {
      const auto kfLoc = entry.value().mLocation;
      mCurrentSample = kfLoc.mSample;
      mCurrentMoof = kfLoc.mMoof;
      MOZ_ASSERT_DEBUG_OR_FUZZING(!mIndex->mMoofParser ||
                                  mCurrentMoof <
                                      mIndex->mMoofParser->Moofs().Length());
      return;
    }
  } else {
    size_t prevMoof = mCurrentMoof;
    size_t prevSample = mCurrentSample;
    while (Sample* sample = Get().unwrapOr(nullptr)) {
      if (sample->mCompositionRange.start == aTime) {
        break;
      }
      if (sample->mCompositionRange.start > aTime) {
        mCurrentSample = prevSample;
        mCurrentMoof = prevMoof;
        break;
      }
      prevSample = mCurrentSample;
      prevMoof = mCurrentMoof;
      Next();
    }
    if (!Get().unwrapOr(nullptr)) {
      // go back if we seeked past the last sample
      mCurrentSample = prevSample;
      mCurrentMoof = prevMoof;
    }
    if (!mIndex->mIsAudio) {
      LOGV("Fallback linear search: moof {}, sample {}", mCurrentMoof,
           mCurrentSample);
    }
  }
}

void SampleIterator::TrySkipToNextKeyframe(const TimeUnit& aTimeThreshold) {
  LOGV("Time threshold: {}", aTimeThreshold.ToString());
  if (!mIndex->mIsAudio) {
    bool doRetryLookup;
    do {
      doRetryLookup = false;
      auto entry = mIndex->mKeyframeMap.GetNextAfter(
          aTimeThreshold, SampleLocation(mCurrentMoof, mCurrentSample));
      if (entry) {
        mCurrentSample = entry.value().mLocation.mSample;
        mCurrentMoof = entry.value().mLocation.mMoof;
        LOGV("Skip to keyframe at moof {}, sample {}", mCurrentMoof,
             mCurrentSample);
        MOZ_ASSERT_DEBUG_OR_FUZZING(!mIndex->mMoofParser ||
                                    mCurrentMoof <
                                        mIndex->mMoofParser->Moofs().Length());
        return;
      } else if (mIndex->mMoofParser) {
        // If the time threshold falls after the last moof, fetch the next one
        // and try using the keyframe map again.
        // This avoids the fallback search for well-formed fragmented files.
        if (auto lastBefore =
                mIndex->mKeyframeMap.GetNearestUpTo(aTimeThreshold)) {
          const size_t threshMoof =
              std::max(mCurrentMoof, lastBefore.value().mLocation.mMoof);
          if (threshMoof + 1 == mIndex->mMoofParser->Moofs().Length()) {
            if (NS_SUCCEEDED(mIndex->BlockingReadNextMoof())) {
              LOGV("Loaded next moof, retry skip");
              doRetryLookup = true;
            }
          }
        }
      } else if (mIndex->HaveCheckedSurroundingKeyframes(aTimeThreshold)) {
        // We know there is no next keyframe, advance to the end.
        mCurrentSample = mIndex->mIndex.Length();
        return;
      }
    } while (doRetryLookup);
  }
  // Otherwise fall back to full search in the demuxer.
  LOG("Fall back to exhaustive search");
  while (Sample* sample = Get().unwrapOr(nullptr)) {
    if (sample->mCompositionRange.start >= aTimeThreshold) {
      break;
    }
    Next();
  }
}

TimeUnit SampleIterator::GetNextKeyframeTime() {
  SampleIterator itr(*this);
  const Sample* sample = itr.Get().unwrapOr(nullptr);
  if (sample) {
    if (mIndex->mIsAudio) {
      return sample->mCompositionRange.start;
    }
    bool doRetry = mIndex->IsFragmented();
    do {
      if (auto entry = mIndex->mKeyframeMap.GetNextAfter(
              sample->mCompositionRange.start)) {
        return entry.value().mCompositionRange.mStart;
      }
      if (doRetry) {
        // If fragmented, the next keyframe might be in a not yet loaded
        // fragment.
        doRetry = NS_SUCCEEDED(mIndex->BlockingReadNextMoof());
      }
    } while (doRetry);
  }
  return TimeUnit::Invalid();
}

MP4SampleIndex::MP4SampleIndex(const IndiceWrapper& aIndices,
                               ByteStream* aSource, uint32_t aTrackId,
                               bool aIsAudio, uint32_t aTimeScale)
    : mSource(aSource), mIsAudio(aIsAudio) {
  if (!aIndices.Length()) {
    mMoofParser =
        std::make_unique<MoofParser>(aSource, AsVariant(aTrackId), aIsAudio);
  } else {
    if (!mIndex.SetCapacity(aIndices.Length(), fallible)) {
      // OOM.
      return;
    }
    media::IntervalSet<TimeUnit> intervalTime;
    MediaByteRange intervalRange;
    bool progressive = true;
    int64_t lastOffset = 0;
    for (size_t i = 0; i < aIndices.Length(); i++) {
      Indice indice{};
      int64_t timescale = static_cast<int64_t>(aTimeScale);
      if (!aIndices.GetIndice(i, indice)) {
        // Out of index?
        return;
      }
      if (indice.start_offset > uint64_t(INT64_MAX) ||
          indice.end_offset > uint64_t(INT64_MAX)) {
        mDataOffset.Clear();
        return;
      }
      Sample sample;
      sample.mByteRange =
          MediaByteRange(indice.start_offset, indice.end_offset);
      sample.mCompositionRange = MP4Interval<media::TimeUnit>(
          TimeUnit(indice.start_composition, timescale),
          TimeUnit(indice.end_composition, timescale));
      sample.mDecodeTime = TimeUnit(indice.start_decode, timescale);
      sample.mSync = indice.sync || mIsAudio;
      // FIXME: Make this infallible after bug 968520 is done.
      MOZ_ALWAYS_TRUE(mIndex.AppendElement(sample, fallible));
      if (AssertedCast<int64_t>(indice.start_offset) < lastOffset) {
        NS_WARNING("Chunks in MP4 out of order, expect slow down");
        progressive = false;
      }
      lastOffset = AssertedCast<int64_t>(indice.end_offset);

      // Pack audio samples in group of 128.
      if (sample.mSync && progressive && (!mIsAudio || !(i % 128))) {
        if (mDataOffset.Length()) {
          auto& last = mDataOffset.LastElement();
          last.mEndOffset = intervalRange.mEnd;
          NS_ASSERTION(intervalTime.Length() == 1,
                       "Discontinuous samples between keyframes");
          last.mTime.start = intervalTime.GetStart();
          last.mTime.end = intervalTime.GetEnd();
        }
        if (!mDataOffset.AppendElement(
                MP4DataOffset(mIndex.Length() - 1,
                              AssertedCast<int64_t>(indice.start_offset)),
                fallible)) {
          // OOM.
          return;
        }
        intervalTime = media::IntervalSet<TimeUnit>();
        intervalRange = MediaByteRange();
      }
      const media::Interval<TimeUnit> sampleInterval(
          sample.mCompositionRange.start, sample.mCompositionRange.end);
      intervalTime += sampleInterval;
      intervalRange = intervalRange.Span(sample.mByteRange);

      if (!mIsAudio && sample.mSync) {
        // prepopulate keyframe map using sync sample flags
        mKeyframeMap.MarkKeyframe(sampleInterval, {0, mIndex.Length() - 1},
                                  KeyframeMap::KeyframeInfo::Container);
      } else if (!mIsAudio) {
        mKeyframeMap.MarkNotKeyframe(
            KeyframeMap::Interval(TimeUnit(indice.start_composition, timescale),
                                  TimeUnit(indice.end_composition, timescale)),
            KeyframeMap::KeyframeInfo::Container);
      }
    }

    if (mDataOffset.Length() && progressive) {
      Indice indice;
      if (!aIndices.GetIndice(aIndices.Length() - 1, indice)) {
        return;
      }
      if (indice.end_offset > uint64_t(INT64_MAX)) {
        return;
      }
      auto& last = mDataOffset.LastElement();
      last.mEndOffset = AssertedCast<int64_t>(indice.end_offset);
      last.mTime =
          MP4Interval<TimeUnit>(intervalTime.GetStart(), intervalTime.GetEnd());
    } else {
      mDataOffset.Clear();
    }
  }
}

MP4SampleIndex::~MP4SampleIndex() = default;

nsresult MP4SampleIndex::BlockingReadNextMoof() {
  MOZ_ASSERT_DEBUG_OR_FUZZING(mMoofParser);
  if (!mMoofParser) {
    return NS_ERROR_FAILURE;
  }
  LOGV("");
  nsresult rv = mMoofParser->BlockingReadNextMoof();
  if (NS_FAILED(rv)) {
    return rv;
  }
  const auto& moofIndex = mMoofParser->Moofs().LastElement().mIndex;
  for (size_t i = 0; i < moofIndex.Length(); ++i) {
    const auto& sample = moofIndex[i];
    KeyframeMap::Interval interval(sample.mCompositionRange.start,
                                   sample.mCompositionRange.end);
    if (sample.mSync) {
      mKeyframeMap.MarkKeyframe(
          interval, SampleLocation{mMoofParser->Moofs().Length() - 1, i},
          KeyframeMap::KeyframeInfo::Container);
    } else {
      mKeyframeMap.MarkNotKeyframe(interval,
                                   KeyframeMap::KeyframeInfo::Container);
    }
  }
  return rv;
}

void MP4SampleIndex::UpdateMoofIndex(const MediaByteRangeSet& aByteRanges) {
  UpdateMoofIndex(aByteRanges, false);
}

void MP4SampleIndex::UpdateMoofIndex(const MediaByteRangeSet& aByteRanges,
                                     bool aCanEvict) {
  if (!mMoofParser) {
    return;
  }
  size_t moofs = mMoofParser->Moofs().Length();
  bool canEvict = aCanEvict && moofs > 1;
  if (canEvict) {
    // Check that we can trim the mMoofParser. We can only do so if all
    // iterators have demuxed all possible samples.
    for (const SampleIterator* iterator : mIterators) {
      if ((iterator->mCurrentSample == 0 && iterator->mCurrentMoof == moofs) ||
          iterator->mCurrentMoof == moofs - 1) {
        continue;
      }
      canEvict = false;
      break;
    }
  }
  if (canEvict) {
    mKeyframeMap.Clear();
  }
  mMoofParser->RebuildFragmentedIndex(aByteRanges, &canEvict);
  if (canEvict) {
    // The moofparser got trimmed. Adjust all registered iterators.
    for (SampleIterator* iterator : mIterators) {
      iterator->mCurrentMoof -= moofs - 1;
    }
  }
  if (!mIsAudio) {
    for (size_t moofIdx = 0; moofIdx < mMoofParser->Moofs().Length();
         ++moofIdx) {
      const auto& moof = mMoofParser->Moofs()[moofIdx];
      for (size_t i = 0; i < moof.mIndex.Length(); ++i) {
        const auto& sample = moof.mIndex[i];
        KeyframeMap::Interval interval(sample.mCompositionRange.start,
                                       sample.mCompositionRange.end);
        if (sample.mSync) {
          mKeyframeMap.MarkKeyframe(interval, SampleLocation{moofIdx, i},
                                    KeyframeMap::KeyframeInfo::Container);
        } else {
          mKeyframeMap.MarkNotKeyframe(interval,
                                       KeyframeMap::KeyframeInfo::Container);
        }
      }
    }
  }
}

TimeIntervals MP4SampleIndex::ConvertByteRangesToTimeRanges(
    const MediaByteRangeSet& aByteRanges) {
  if (aByteRanges == mLastCachedRanges) {
    return mLastBufferedRanges;
  }
  mLastCachedRanges = aByteRanges;

  if (mDataOffset.Length()) {
    TimeIntervals timeRanges;
    for (const auto& range : aByteRanges) {
      uint32_t start = mDataOffset.IndexOfFirstElementGt(range.mStart - 1);
      if (!mIsAudio && start == mDataOffset.Length()) {
        continue;
      }
      uint32_t end = mDataOffset.IndexOfFirstElementGt(
          range.mEnd, MP4DataOffset::EndOffsetComparator());
      if (!mIsAudio && end < start) {
        continue;
      }
      if (mIsAudio && start &&
          range.Intersects(MediaByteRange(mDataOffset[start - 1].mStartOffset,
                                          mDataOffset[start - 1].mEndOffset))) {
        // Check if previous audio data block contains some available samples.
        for (size_t i = mDataOffset[start - 1].mIndex; i < mIndex.Length();
             i++) {
          if (range.ContainsStrict(mIndex[i].mByteRange)) {
            timeRanges += TimeInterval(mIndex[i].mCompositionRange.start,
                                       mIndex[i].mCompositionRange.end);
          }
        }
      }
      if (end > start) {
        for (uint32_t i = start; i < end; i++) {
          timeRanges += TimeInterval(mDataOffset[i].mTime.start,
                                     mDataOffset[i].mTime.end);
        }
      }
      if (end < mDataOffset.Length()) {
        // Find samples in partial block contained in the byte range.
        for (size_t i = mDataOffset[end].mIndex;
             i < mIndex.Length() && range.ContainsStrict(mIndex[i].mByteRange);
             i++) {
          timeRanges += TimeInterval(mIndex[i].mCompositionRange.start,
                                     mIndex[i].mCompositionRange.end);
        }
      }
    }
    mLastBufferedRanges = timeRanges;
    return timeRanges;
  }

  RangeFinder rangeFinder(aByteRanges);
  nsTArray<MP4Interval<media::TimeUnit>> timeRanges;
  nsTArray<FallibleTArray<Sample>*> indexes;
  if (mMoofParser) {
    // We take the index out of the moof parser and move it into a local
    // variable so we don't get concurrency issues. It gets freed when we
    // exit this function.
    for (int i = 0; i < mMoofParser->Moofs().Length(); i++) {
      Moof& moof = mMoofParser->Moofs()[i];

      // We need the entire moof in order to play anything
      if (rangeFinder.Contains(moof.mRange)) {
        if (rangeFinder.Contains(moof.mMdatRange)) {
          MP4Interval<media::TimeUnit>::SemiNormalAppend(timeRanges,
                                                         moof.mTimeRange);
        } else {
          indexes.AppendElement(&moof.mIndex);
        }
      }
    }
  } else {
    indexes.AppendElement(&mIndex);
  }

  bool hasSync = false;
  for (size_t i = 0; i < indexes.Length(); i++) {
    FallibleTArray<Sample>* index = indexes[i];
    for (size_t j = 0; j < index->Length(); j++) {
      const Sample& sample = (*index)[j];
      if (!rangeFinder.Contains(sample.mByteRange)) {
        // We process the index in decode order so we clear hasSync when we hit
        // a range that isn't buffered.
        hasSync = false;
        continue;
      }

      hasSync |= sample.mSync;
      if (!hasSync) {
        continue;
      }

      MP4Interval<media::TimeUnit>::SemiNormalAppend(timeRanges,
                                                     sample.mCompositionRange);
    }
  }

  // This fixes up when the compositon order differs from the byte range order
  nsTArray<MP4Interval<TimeUnit>> timeRangesNormalized;
  MP4Interval<media::TimeUnit>::Normalize(timeRanges, &timeRangesNormalized);
  // convert timeRanges.
  media::TimeIntervals ranges;
  for (size_t i = 0; i < timeRangesNormalized.Length(); i++) {
    ranges += media::TimeInterval(timeRangesNormalized[i].start,
                                  timeRangesNormalized[i].end);
  }
  mLastBufferedRanges = ranges;
  return ranges;
}

uint64_t MP4SampleIndex::GetEvictionOffset(const TimeUnit& aTime) {
  uint64_t offset = std::numeric_limits<uint64_t>::max();
  if (mMoofParser) {
    // We need to keep the whole moof if we're keeping any of it because the
    // parser doesn't keep parsed moofs.
    for (int i = 0; i < mMoofParser->Moofs().Length(); i++) {
      Moof& moof = mMoofParser->Moofs()[i];

      if (!moof.mTimeRange.Length().IsZero() && moof.mTimeRange.end > aTime) {
        offset = std::min(offset, uint64_t(std::min(moof.mRange.mStart,
                                                    moof.mMdatRange.mStart)));
      }
    }
  } else {
    // We've already parsed and stored the moov so we don't need to keep it.
    // All we need to keep is the sample data itself.
    for (size_t i = 0; i < mIndex.Length(); i++) {
      const Sample& sample = mIndex[i];
      if (aTime >= sample.mCompositionRange.end) {
        offset = std::min(offset, uint64_t(sample.mByteRange.mEnd));
      }
    }
  }
  return offset;
}

Maybe<bool> MP4SampleIndex::IsKeyframe(const TimeUnit& aTime) const {
  if (!mKeyframeMap.HaveChecked(aTime)) {
    return Nothing();
  }
  return Some(mKeyframeMap.Get(aTime).isSome());
}

size_t MP4SampleIndex::NumSamplesBetween(const SampleLocation& aStart,
                                         const SampleLocation& aEnd) const {
  MOZ_ASSERT_DEBUG_OR_FUZZING(aStart <= aEnd);
  if (aStart > aEnd) {
    return 0;
  }
  if (mMoofParser) {
    const auto nMoofs = mMoofParser->Moofs().Length();
    MOZ_ASSERT_DEBUG_OR_FUZZING(aEnd.mMoof < nMoofs);
    if (aEnd.mMoof >= nMoofs) {
      return 0;
    }
    const size_t startMoofLen =
        mMoofParser->Moofs()[aStart.mMoof].mIndex.Length();
    const size_t endMoofLen = mMoofParser->Moofs()[aEnd.mMoof].mIndex.Length();
    MOZ_ASSERT_DEBUG_OR_FUZZING(aStart.mSample <= startMoofLen &&
                                aEnd.mSample <= endMoofLen);
    size_t count = startMoofLen - std::min(startMoofLen, aStart.mSample) +
                   std::min(endMoofLen, aEnd.mSample);
    for (size_t moof = aStart.mMoof + 1; moof < aEnd.mMoof; ++moof) {
      count += mMoofParser->Moofs()[moof].mIndex.Length();
    }
    return count;
  } else {
    MOZ_ASSERT_DEBUG_OR_FUZZING(aEnd.mSample <= mIndex.Length());
    if (aEnd.mSample > mIndex.Length()) {
      return 0;
    }
    return aEnd.mSample - aStart.mSample;
  }
}

bool MP4SampleIndex::HaveCheckedSurroundingKeyframes(
    const TimeUnit& aTime) const {
  const auto firstSample = mKeyframeMap.MinSampleTime();
  const auto lastSample = mKeyframeMap.MaxSampleTime();
  if (!firstSample || !lastSample) {
    return false;
  }
  const Maybe<TimeUnit> nextAfter = mKeyframeMap.GetNextAfter(aTime).map(
      [](KeyframeMap::Entry entry) { return entry.mCompositionRange.mEnd; });
  if (mMoofParser && !nextAfter) {
    // Past the last keyframe, but there might be more fragments after.
    return false;
  }
  const TimeUnit rangeEnd = nextAfter.valueOr(lastSample.value());
  const TimeUnit rangeStart = mKeyframeMap.GetNearestUpTo(aTime)
                                  .map([](KeyframeMap::Entry entry) {
                                    return entry.mCompositionRange.mStart;
                                  })
                                  .valueOr(firstSample.value());
  return mKeyframeMap.HaveChecked(KeyframeMap::Interval(rangeStart, rangeEnd));
}

void MP4SampleIndex::RegisterIterator(SampleIterator* aIterator) {
  mIterators.AppendElement(aIterator);
}

void MP4SampleIndex::UnregisterIterator(SampleIterator* aIterator) {
  mIterators.RemoveElement(aIterator);
}

}  // namespace mozilla

#undef LOG
