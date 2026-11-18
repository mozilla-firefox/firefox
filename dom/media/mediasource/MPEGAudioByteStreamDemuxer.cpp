/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MPEGAudioByteStreamDemuxer.h"

#include <algorithm>

#include "MediaData.h"
#include "TimeUnits.h"
#include "mozilla/Casting.h"
#include "mozilla/Logging.h"
#include "mozilla/Try.h"

extern mozilla::LogModule* GetMediaSourceSamplesLog();

#define MSE_DEBUG(arg, ...)                                           \
  DDMOZ_LOG_FMT(GetMediaSourceSamplesLog(), mozilla::LogLevel::Debug, \
                "::{}: " arg, __func__, ##__VA_ARGS__)
#define MSE_DEBUGV(arg, ...)                                            \
  DDMOZ_LOG_FMT(GetMediaSourceSamplesLog(), mozilla::LogLevel::Verbose, \
                "::{}: " arg, __func__, ##__VA_ARGS__)

namespace mozilla {

using media::TimeIntervals;
using media::TimeUnit;

// Holds any MPEG audio frame.
static constexpr size_t kMPEGAudioPeekSize = mpeg_audio::kMaxFrameLength;

static MediaResult InvalidMPEGAudioData() {
  return MediaResult(NS_ERROR_DOM_MEDIA_DEMUXER_ERR,
                     "Invalid MPEG audio byte stream");
}

MPEGAudioByteStreamDemuxer::MPEGAudioByteStreamDemuxer(
    SourceBufferResource* aSource)
    : mSource(aSource) {
  DDLINKCHILD("source", aSource);
}

RefPtr<MediaDataDemuxer::InitPromise> MPEGAudioByteStreamDemuxer::Init() {
  if (!mTrackDemuxer) {
    mTrackDemuxer = new MPEGAudioByteStreamTrackDemuxer(mSource);
    DDLINKCHILD("track demuxer", mTrackDemuxer.get());
  }
  if (!mTrackDemuxer->Init()) {
    return InitPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_METADATA_ERR,
                                        __func__);
  }
  return InitPromise::CreateAndResolve(NS_OK, __func__);
}

uint32_t MPEGAudioByteStreamDemuxer::GetNumberTracks(
    TrackInfo::TrackType aType) const {
  return aType == TrackInfo::kAudioTrack && mTrackDemuxer ? 1 : 0;
}

already_AddRefed<MediaTrackDemuxer> MPEGAudioByteStreamDemuxer::GetTrackDemuxer(
    TrackInfo::TrackType aType, uint32_t aTrackNumber) {
  if (aType != TrackInfo::kAudioTrack || aTrackNumber || !mTrackDemuxer) {
    return nullptr;
  }
  return RefPtr<MediaTrackDemuxer>(mTrackDemuxer).forget();
}

MPEGAudioByteStreamTrackDemuxer::MPEGAudioByteStreamTrackDemuxer(
    SourceBufferResource* aSource)
    : mSource(aSource) {
  DDLINKCHILD("source", aSource);
}

bool MPEGAudioByteStreamTrackDemuxer::Init() {
  // The resource starts with the initialization segment.
  MOZ_ASSERT(mSource->GetCachedDataEnd(0) ==
             int64_t(mpeg_audio::kFrameHeaderSize));
  uint8_t header[mpeg_audio::kFrameHeaderSize];
  if (NS_FAILED(mSource->ReadFromCache(reinterpret_cast<char*>(header), 0,
                                       sizeof(header)))) {
    return false;
  }
  Maybe<mpeg_audio::FrameInfo> frame = mpeg_audio::ParseFrameHeader(header);
  if (!frame) {
    return false;
  }
  mConfig = *frame;
  mInfo.mMimeType = "audio/mpeg"_ns;
  mInfo.mRate = mConfig.mSampleRate;
  mInfo.mChannels = mConfig.mChannels;
  mInfo.mBitDepth = 16;
  // The media segments are appended after the initialization segment.
  mOffset = sizeof(header);
  MSE_DEBUG("layer={} rate={} channels={}", mConfig.mLayer, mInfo.mRate,
            mInfo.mChannels);
  return true;
}

UniquePtr<TrackInfo> MPEGAudioByteStreamTrackDemuxer::GetInfo() const {
  return mInfo.Clone();
}

RefPtr<MPEGAudioByteStreamTrackDemuxer::SeekPromise>
MPEGAudioByteStreamTrackDemuxer::Seek(const TimeUnit& aTime) {
  MOZ_ASSERT_UNREACHABLE("TrackBuffersManager doesn't seek its input demuxer");
  return SeekPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_DEMUXER_ERR, __func__);
}

RefPtr<MPEGAudioByteStreamTrackDemuxer::SamplesPromise>
MPEGAudioByteStreamTrackDemuxer::GetSamples(int32_t aNumSamples) {
  MOZ_ASSERT(aNumSamples);
  RefPtr<SamplesHolder> samples = new SamplesHolder();
  // A negative aNumSamples asks for all the available samples.
  while (aNumSamples < 0 ||
         samples->GetSamples().Length() < static_cast<size_t>(aNumSamples)) {
    Result<RefPtr<MediaRawData>, MediaResult> sample = GetNextSample();
    if (sample.isErr()) {
      return SamplesPromise::CreateAndReject(sample.unwrapErr(), __func__);
    }
    RefPtr<MediaRawData> next = sample.unwrap();
    if (!next) {
      break;
    }
    samples->AppendSample(std::move(next));
  }
  if (samples->GetSamples().IsEmpty()) {
    return SamplesPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA,
                                           __func__);
  }
  return SamplesPromise::CreateAndResolve(samples, __func__);
}

RefPtr<MPEGAudioByteStreamTrackDemuxer::SkipAccessPointPromise>
MPEGAudioByteStreamTrackDemuxer::SkipToNextRandomAccessPoint(
    const TimeUnit& aTimeThreshold) {
  MOZ_ASSERT_UNREACHABLE("Only used for video");
  return SkipAccessPointPromise::CreateAndReject(
      SkipFailureHolder(NS_ERROR_DOM_MEDIA_DEMUXER_ERR, 0), __func__);
}

TimeIntervals MPEGAudioByteStreamTrackDemuxer::GetBuffered() {
  MOZ_ASSERT_UNREACHABLE("TrackBuffersManager tracks the buffered ranges");
  return TimeIntervals();
}

Result<RefPtr<MediaRawData>, MediaResult>
MPEGAudioByteStreamTrackDemuxer::GetNextSample() {
  while (true) {
    Span<const uint8_t> data = MOZ_TRY(Peek(kMPEGAudioPeekSize));
    const mpeg_audio::Unit unit =
        mpeg_audio::ParseUnit(data, /* aConfirmSync = */ false);
    switch (unit.mType) {
      case mpeg_audio::Unit::Type::NeedMoreData:
        MOZ_ASSERT(data.Length() < kMPEGAudioPeekSize);
        return RefPtr<MediaRawData>();
      case mpeg_audio::Unit::Type::Error:
        return Err(InvalidMPEGAudioData());
      case mpeg_audio::Unit::Type::Metadata:
      case mpeg_audio::Unit::Type::Garbage:
        MOZ_ASSERT_UNREACHABLE("Media segments only hold frames");
        return Err(InvalidMPEGAudioData());
      case mpeg_audio::Unit::Type::InfoFrame:
        MSE_DEBUGV("skipping an info frame at {}", mOffset);
        mOffset += AssertedCast<int64_t>(unit.mLength);
        break;
      case mpeg_audio::Unit::Type::Frame: {
        if (!unit.mFrame.HasSameConfig(mConfig)) {
          // MPEGAudioContainerParser ends a media segment before a frame
          // that needs a new initialization segment.
          MOZ_ASSERT_UNREACHABLE("Unexpected frame configuration");
          return Err(InvalidMPEGAudioData());
        }
        RefPtr<MediaRawData> sample =
            new MediaRawData(data.Elements(), unit.mLength);
        if (sample->Size() != unit.mLength) {
          return Err(MediaResult(NS_ERROR_OUT_OF_MEMORY, __func__));
        }
        sample->mOffset = mOffset;
        sample->mTime = TimeUnit(mSamples, mConfig.mSampleRate);
        sample->mTimecode = sample->mTime;
        sample->mDuration =
            TimeUnit(mConfig.mSamplesPerFrame, mConfig.mSampleRate);
        // Every MPEG audio frame is a random access point.
        // https://w3c.github.io/mse-byte-stream-format-mpeg-audio/#mpeg-segments
        sample->mKeyframe = true;
        mSamples += mConfig.mSamplesPerFrame;
        mOffset += AssertedCast<int64_t>(unit.mLength);
        return sample;
      }
    }
  }
}

Result<Span<const uint8_t>, MediaResult> MPEGAudioByteStreamTrackDemuxer::Peek(
    size_t aSize) {
  const int64_t end = mSource->GetCachedDataEnd(mOffset);
  if (end <= mOffset) {
    return Span<const uint8_t>();
  }
  const size_t size =
      static_cast<size_t>(std::min<int64_t>(aSize, end - mOffset));
  if (const uint8_t* data = mSource->GetContiguousAccess(mOffset, size)) {
    return Span(data, size);
  }
  mScratch.SetLength(size);
  nsresult rv = mSource->ReadFromCache(
      reinterpret_cast<char*>(mScratch.Elements()), mOffset, size);
  if (NS_FAILED(rv)) {
    return Err(MediaResult(rv, __func__));
  }
  return Span<const uint8_t>(mScratch);
}

}  // namespace mozilla

#undef MSE_DEBUG
#undef MSE_DEBUGV
