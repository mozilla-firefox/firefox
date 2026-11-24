/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MPEGAudioByteStreamDemuxer.h"

#include <algorithm>

#include "Adts.h"
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

// MPEG audio decoders output the audio of a frame later than its timestamp.
// MP3TrackDemuxer compensates for it as part of the encoder delay, but the
// generated timestamps of the byte stream format can't account for it. The
// MPEG audio standards don't state the delay. 529 is the conventional value
// for Layer III, from LAME: its encoder.h gives decoders a delay of 528
// samples (240 for the polyphase filterbank and 288 for the MDCT), and an
// encoder and a decoder a total delay of the encoder's plus 528 + 1.
static constexpr uint32_t kLayer3DecoderDelay = 529;
// Layers I and II have no MDCT, which leaves the decoder's share of the
// polyphase filterbank delay: 241 of the 481 samples that LAME observes
// between an encoder and a decoder. Chromium uses 529 for every layer.
static constexpr uint32_t kLayer1And2DecoderDelay = 241;

static MediaResult InvalidMPEGAudioData() {
  return MediaResult(NS_ERROR_DOM_MEDIA_DEMUXER_ERR,
                     "Invalid MPEG audio byte stream");
}

MPEGAudioByteStreamDemuxer::MPEGAudioByteStreamDemuxer(
    SourceBufferResource* aSource, mpeg_audio::Format aFormat)
    : mSource(aSource), mFormat(aFormat) {
  DDLINKCHILD("source", aSource);
}

RefPtr<MediaDataDemuxer::InitPromise> MPEGAudioByteStreamDemuxer::Init() {
  if (!mTrackDemuxer) {
    mTrackDemuxer = new MPEGAudioByteStreamTrackDemuxer(mSource, mFormat);
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
    SourceBufferResource* aSource, mpeg_audio::Format aFormat)
    : mSource(aSource), mFormat(aFormat) {
  DDLINKCHILD("source", aSource);
}

bool MPEGAudioByteStreamTrackDemuxer::Init() {
  // The resource starts with the initialization segment.
  const size_t headerSize = mpeg_audio::FrameHeaderSize(mFormat);
  MOZ_ASSERT(mSource->GetCachedDataEnd(0) == int64_t(headerSize));
  uint8_t header[mpeg_audio::kADTSHeaderSize];
  static_assert(mpeg_audio::kADTSHeaderSize >=
                mpeg_audio::kMPEGAudioHeaderSize);
  if (NS_FAILED(mSource->ReadFromCache(reinterpret_cast<char*>(header), 0,
                                       AssertedCast<uint32_t>(headerSize)))) {
    return false;
  }
  Maybe<mpeg_audio::FrameInfo> frame =
      mpeg_audio::ParseFrameHeader(mFormat, Span(header, headerSize));
  if (!frame) {
    return false;
  }
  mConfig = *frame;
  mInfo.mRate = mConfig.mSampleRate;
  mInfo.mChannels = mConfig.mChannels;
  mInfo.mBitDepth = 16;
  if (mFormat == mpeg_audio::Format::ADTS) {
    mInfo.mMimeType = "audio/mp4a-latm"_ns;
    mInfo.mProfile = mInfo.mExtendedProfile = mConfig.mObjectType;
    ADTS::FrameHeader adtsHeader;
    MOZ_ALWAYS_TRUE(adtsHeader.Parse(Span(header, headerSize)));
    AudioCodecSpecificBinaryBlob blob;
    ADTS::InitAudioSpecificConfig(adtsHeader, blob.mBinaryBlob);
    mInfo.mCodecSpecificConfig = AudioCodecSpecificVariant{std::move(blob)};
  } else {
    mInfo.mMimeType = "audio/mpeg"_ns;
    Mp3CodecSpecificData mp3CodecSpecificData;
    mp3CodecSpecificData.mDecoderDelayFrames =
        mConfig.mLayer == 3 ? kLayer3DecoderDelay : kLayer1And2DecoderDelay;
    mInfo.mCodecSpecificConfig =
        AudioCodecSpecificVariant{std::move(mp3CodecSpecificData)};
  }
  // The media segments are appended after the initialization segment.
  mOffset = AssertedCast<int64_t>(headerSize);
  MSE_DEBUG("layer={} object type={} rate={} channels={}", mConfig.mLayer,
            mConfig.mObjectType, mInfo.mRate, mInfo.mChannels);
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
    // The peeked data can hold any frame.
    const size_t peekSize = mpeg_audio::MaxFrameLength(mFormat);
    Span<const uint8_t> data = MOZ_TRY(Peek(peekSize));
    const mpeg_audio::Unit unit =
        mpeg_audio::ParseUnit(mFormat, data, /* aConfirmSync = */ false);
    switch (unit.mType) {
      case mpeg_audio::Unit::Type::NeedMoreData:
        MOZ_ASSERT(data.Length() < peekSize);
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
        // Decoders expect the raw data block of an ADTS frame, without its
        // header, and whole MPEG audio frames.
        const size_t payloadStart =
            mFormat == mpeg_audio::Format::ADTS ? unit.mFrame.mHeaderLength : 0;
        const Span<const uint8_t> payload =
            data.FromTo(payloadStart, static_cast<size_t>(unit.mLength));
        RefPtr<MediaRawData> sample =
            new MediaRawData(payload.Elements(), payload.Length());
        if (sample->Size() != payload.Length()) {
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
