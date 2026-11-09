/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MP4Demuxer.h"

#include <stdint.h>

#include <algorithm>
#include <memory>

#include "AOMDecoder.h"
#include "AnnexB.h"
#include "BufferStream.h"
#include "H264.h"
#include "H265.h"
#include "MP4Decoder.h"
#include "MP4Metadata.h"
#include "MoofParser.h"
#include "ResourceStream.h"
#include "SampleIterator.h"
#include "TimeUnits.h"
#include "VPXDecoder.h"
#include "mozilla/Span.h"
#include "mozilla/StaticPrefs_media.h"
#include "nsPrintfCString.h"

#define LOG(arg, ...)                                                     \
  DDMOZ_LOG_FMT(gMediaDemuxerLog, mozilla::LogLevel::Debug, "::{}: " arg, \
                __func__, ##__VA_ARGS__)

namespace mozilla {

using TimeUnit = media::TimeUnit;
using TimeInterval = media::TimeInterval;
using TimeIntervals = media::TimeIntervals;

DDLoggedTypeDeclNameAndBase(MP4TrackDemuxer, MediaTrackDemuxer);

class MP4TrackDemuxer : public MediaTrackDemuxer,
                        public DecoderDoctorLifeLogger<MP4TrackDemuxer> {
 public:
  MP4TrackDemuxer(MediaResource* aResource, UniquePtr<TrackInfo>&& aInfo,
                  const IndiceWrapper& aIndices, uint32_t aTimeScale);

  UniquePtr<TrackInfo> GetInfo() const override;

  RefPtr<SeekPromise> Seek(const TimeUnit& aTime) override;

  RefPtr<SamplesPromise> GetSamples(int32_t aNumSamples = 1) override;

  void Reset() override;

  nsresult GetNextRandomAccessPoint(TimeUnit* aTime) override;

  RefPtr<SkipAccessPointPromise> SkipToNextRandomAccessPoint(
      const TimeUnit& aTimeThreshold) override;

  TimeIntervals GetBuffered() override;

  void NotifyDataRemoved();
  void NotifyDataArrived();

 private:
  Result<already_AddRefed<MediaRawData>, MediaResult> GetNextSample();
  void EnsureUpToDateIndex();
  void SetNextKeyFrameTime();
  RefPtr<MediaResource> mResource;
  RefPtr<ResourceStream> mStream;
  UniquePtr<TrackInfo> mInfo;
  RefPtr<MP4SampleIndex> mIndex;
  UniquePtr<SampleIterator> mIterator;
  Maybe<TimeUnit> mNextKeyframeTime;
  // Queued samples extracted by the demuxer, but not yet returned.
  RefPtr<MediaRawData> mQueuedSample;
  bool mNeedReIndex;
  enum CodecType { kH264, kVP9, kAV1, kAAC, kHEVC, kOther } mType = kOther;
};

MP4Demuxer::MP4Demuxer(MediaResource* aResource)
    : mResource(aResource),
      mStream(new ResourceStream(aResource)),
      mIsSeekable(false) {
  DDLINKCHILD("resource", aResource);
  DDLINKCHILD("stream", mStream.get());
}

RefPtr<MP4Demuxer::InitPromise> MP4Demuxer::Init() {
  AutoPinned<ResourceStream> stream(mStream);

  // 'result' will capture the first warning, if any.
  MediaResult result{NS_OK};

  MP4Metadata::ResultAndByteBuffer initData = MP4Metadata::Metadata(stream);
  if (!initData.Ref()) {
    return InitPromise::CreateAndReject(
        NS_FAILED(initData.Result())
            ? std::move(initData.Result())
            : MediaResult(NS_ERROR_DOM_MEDIA_DEMUXER_ERR,
                          RESULT_DETAIL("Invalid MP4 metadata or OOM")),
        __func__);
  } else if (NS_FAILED(initData.Result()) && result == NS_OK) {
    result = std::move(initData.Result());
  }

  RefPtr<BufferStream> bufferstream = new BufferStream(initData.Ref());

  MP4Metadata metadata{bufferstream};
  DDLINKCHILD("metadata", &metadata);
  nsresult rv = metadata.Parse();
  if (NS_FAILED(rv)) {
    return InitPromise::CreateAndReject(
        MediaResult(rv, RESULT_DETAIL("Parse MP4 metadata failed")), __func__);
  }

  auto audioTrackCount = metadata.GetNumberTracks(TrackInfo::kAudioTrack);
  if (audioTrackCount.Ref() == MP4Metadata::NumberTracksError()) {
    if (StaticPrefs::media_playback_warnings_as_errors()) {
      return InitPromise::CreateAndReject(
          MediaResult(
              NS_ERROR_DOM_MEDIA_DEMUXER_ERR,
              RESULT_DETAIL("Invalid audio track (%s)",
                            audioTrackCount.Result().Description().get())),
          __func__);
    }
    audioTrackCount.Ref() = 0;
  }

  auto videoTrackCount = metadata.GetNumberTracks(TrackInfo::kVideoTrack);
  if (videoTrackCount.Ref() == MP4Metadata::NumberTracksError()) {
    if (StaticPrefs::media_playback_warnings_as_errors()) {
      return InitPromise::CreateAndReject(
          MediaResult(
              NS_ERROR_DOM_MEDIA_DEMUXER_ERR,
              RESULT_DETAIL("Invalid video track (%s)",
                            videoTrackCount.Result().Description().get())),
          __func__);
    }
    videoTrackCount.Ref() = 0;
  }

  if (audioTrackCount.Ref() == 0 && videoTrackCount.Ref() == 0) {
    return InitPromise::CreateAndReject(
        MediaResult(
            NS_ERROR_DOM_MEDIA_DEMUXER_ERR,
            RESULT_DETAIL("No MP4 audio (%s) or video (%s) tracks",
                          audioTrackCount.Result().Description().get(),
                          videoTrackCount.Result().Description().get())),
        __func__);
  }

  if (NS_FAILED(audioTrackCount.Result()) && result == NS_OK) {
    result = std::move(audioTrackCount.Result());
  }
  if (NS_FAILED(videoTrackCount.Result()) && result == NS_OK) {
    result = std::move(videoTrackCount.Result());
  }

  if (audioTrackCount.Ref() != 0) {
    for (size_t i = 0; i < audioTrackCount.Ref(); i++) {
      MP4Metadata::ResultAndTrackInfo info =
          metadata.GetTrackInfo(TrackInfo::kAudioTrack, i);
      if (!info.Ref()) {
        if (StaticPrefs::media_playback_warnings_as_errors()) {
          return InitPromise::CreateAndReject(
              MediaResult(NS_ERROR_DOM_MEDIA_DEMUXER_ERR,
                          RESULT_DETAIL("Invalid MP4 audio track (%s)",
                                        info.Result().Description().get())),
              __func__);
        }
        if (result == NS_OK) {
          result =
              MediaResult(NS_ERROR_DOM_MEDIA_DEMUXER_ERR,
                          RESULT_DETAIL("Invalid MP4 audio track (%s)",
                                        info.Result().Description().get()));
        }
        continue;
      } else if (NS_FAILED(info.Result()) && result == NS_OK) {
        result = std::move(info.Result());
      }
      MP4Metadata::ResultAndIndice indices =
          metadata.GetTrackIndice(info.Ref()->mTrackId);
      if (!indices.Ref()) {
        if (NS_FAILED(info.Result()) && result == NS_OK) {
          result = std::move(indices.Result());
        }
        continue;
      }
      LOG("Created audio track demuxer for info ({})",
          info.Ref()->ToString().get());
      RefPtr<MP4TrackDemuxer> demuxer =
          new MP4TrackDemuxer(mResource, std::move(info.Ref()),
                              *indices.Ref().get(), info.Ref()->mTimeScale);
      DDLINKCHILD("audio demuxer", demuxer.get());
      mAudioDemuxers.AppendElement(std::move(demuxer));
    }
  }

  if (videoTrackCount.Ref() != 0) {
    for (size_t i = 0; i < videoTrackCount.Ref(); i++) {
      MP4Metadata::ResultAndTrackInfo info =
          metadata.GetTrackInfo(TrackInfo::kVideoTrack, i);
      if (!info.Ref()) {
        if (StaticPrefs::media_playback_warnings_as_errors()) {
          return InitPromise::CreateAndReject(
              MediaResult(NS_ERROR_DOM_MEDIA_DEMUXER_ERR,
                          RESULT_DETAIL("Invalid MP4 video track (%s)",
                                        info.Result().Description().get())),
              __func__);
        }
        if (result == NS_OK) {
          result =
              MediaResult(NS_ERROR_DOM_MEDIA_DEMUXER_ERR,
                          RESULT_DETAIL("Invalid MP4 video track (%s)",
                                        info.Result().Description().get()));
        }
        continue;
      } else if (NS_FAILED(info.Result()) && result == NS_OK) {
        result = std::move(info.Result());
      }
      MP4Metadata::ResultAndIndice indices =
          metadata.GetTrackIndice(info.Ref()->mTrackId);
      if (!indices.Ref()) {
        if (NS_FAILED(info.Result()) && result == NS_OK) {
          result = std::move(indices.Result());
        }
        continue;
      }
      LOG("Created video track demuxer for info ({})",
          info.Ref()->ToString().get());
      RefPtr<MP4TrackDemuxer> demuxer =
          new MP4TrackDemuxer(mResource, std::move(info.Ref()),
                              *indices.Ref().get(), info.Ref()->mTimeScale);
      DDLINKCHILD("video demuxer", demuxer.get());
      mVideoDemuxers.AppendElement(std::move(demuxer));
    }
  }

  MP4Metadata::ResultAndCryptoFile cryptoFile = metadata.Crypto();
  if (NS_FAILED(cryptoFile.Result()) && result == NS_OK) {
    result = std::move(cryptoFile.Result());
  }
  MOZ_ASSERT(cryptoFile.Ref());
  if (cryptoFile.Ref()->valid) {
    const nsTArray<PsshInfo>& psshs = cryptoFile.Ref()->pssh;
    for (uint32_t i = 0; i < psshs.Length(); i++) {
      mCryptoInitData.AppendElements(psshs[i].data);
    }
  }

  mIsSeekable = metadata.CanSeek();

  return InitPromise::CreateAndResolve(result, __func__);
}

uint32_t MP4Demuxer::GetNumberTracks(TrackInfo::TrackType aType) const {
  switch (aType) {
    case TrackInfo::kAudioTrack:
      return uint32_t(mAudioDemuxers.Length());
    case TrackInfo::kVideoTrack:
      return uint32_t(mVideoDemuxers.Length());
    default:
      return 0;
  }
}

already_AddRefed<MediaTrackDemuxer> MP4Demuxer::GetTrackDemuxer(
    TrackInfo::TrackType aType, uint32_t aTrackNumber) {
  switch (aType) {
    case TrackInfo::kAudioTrack:
      if (aTrackNumber >= uint32_t(mAudioDemuxers.Length())) {
        return nullptr;
      }
      return RefPtr<MediaTrackDemuxer>(mAudioDemuxers[aTrackNumber]).forget();
    case TrackInfo::kVideoTrack:
      if (aTrackNumber >= uint32_t(mVideoDemuxers.Length())) {
        return nullptr;
      }
      return RefPtr<MediaTrackDemuxer>(mVideoDemuxers[aTrackNumber]).forget();
    default:
      return nullptr;
  }
}

bool MP4Demuxer::IsSeekable() const { return mIsSeekable; }

void MP4Demuxer::NotifyDataArrived() {
  for (auto& dmx : mAudioDemuxers) {
    dmx->NotifyDataArrived();
  }
  for (auto& dmx : mVideoDemuxers) {
    dmx->NotifyDataArrived();
  }
}

void MP4Demuxer::NotifyDataRemoved() {
  for (auto& dmx : mAudioDemuxers) {
    dmx->NotifyDataRemoved();
  }
  for (auto& dmx : mVideoDemuxers) {
    dmx->NotifyDataRemoved();
  }
}

UniquePtr<EncryptionInfo> MP4Demuxer::GetCrypto() {
  UniquePtr<EncryptionInfo> crypto;
  if (!mCryptoInitData.IsEmpty()) {
    crypto = std::make_unique<EncryptionInfo>();
    crypto->AddInitData(u"cenc"_ns, mCryptoInitData);
  }
  return crypto;
}

MP4TrackDemuxer::MP4TrackDemuxer(MediaResource* aResource,
                                 UniquePtr<TrackInfo>&& aInfo,
                                 const IndiceWrapper& aIndices,
                                 uint32_t aTimeScale)
    : mResource(aResource),
      mStream(new ResourceStream(aResource)),
      mInfo(std::move(aInfo)),
      mIndex(new MP4SampleIndex(aIndices, mStream, mInfo->mTrackId,
                                mInfo->IsAudio(), aTimeScale)),
      mIterator(std::make_unique<SampleIterator>(mIndex)),
      mNeedReIndex(true) {
  EnsureUpToDateIndex();  // Force update of index

  VideoInfo* videoInfo = mInfo->GetAsVideoInfo();
  AudioInfo* audioInfo = mInfo->GetAsAudioInfo();
  if (videoInfo && MP4Decoder::IsH264(mInfo->mMimeType)) {
    mType = kH264;
    RefPtr<MediaByteBuffer> extraData = videoInfo->mExtraData;
    SPSData spsdata;
    if (H264::DecodeSPSFromExtraData(extraData, spsdata) &&
        spsdata.pic_width > 0 && spsdata.pic_height > 0 &&
        H264::EnsureSPSIsSane(spsdata)) {
      videoInfo->mImage.width = spsdata.pic_width;
      videoInfo->mImage.height = spsdata.pic_height;
      videoInfo->mDisplay.width = spsdata.display_width;
      videoInfo->mDisplay.height = spsdata.display_height;
    }
  } else if (videoInfo && VPXDecoder::IsVP9(mInfo->mMimeType)) {
    mType = kVP9;
  } else if (videoInfo && AOMDecoder::IsAV1(mInfo->mMimeType)) {
    mType = kAV1;
  } else if (audioInfo && MP4Decoder::IsAAC(mInfo->mMimeType)) {
    mType = kAAC;
  } else if (videoInfo && MP4Decoder::IsHEVC(mInfo->mMimeType)) {
    mType = kHEVC;
    if (auto rv = H265::DecodeSPSFromHVCCExtraData(videoInfo->mExtraData);
        rv.isOk()) {
      const auto sps = rv.unwrap();
      videoInfo->mImage.width = sps.GetImageSize().Width();
      videoInfo->mImage.height = sps.GetImageSize().Height();
      videoInfo->mDisplay.width = sps.GetDisplaySize().Width();
      videoInfo->mDisplay.height = sps.GetDisplaySize().Height();
    }
  }
}

UniquePtr<TrackInfo> MP4TrackDemuxer::GetInfo() const { return mInfo->Clone(); }

void MP4TrackDemuxer::EnsureUpToDateIndex() {
  if (!mNeedReIndex) {
    return;
  }
  AutoPinned<MediaResource> resource(mResource);
  MediaByteRangeSet byteRanges;
  nsresult rv = resource->GetCachedRanges(byteRanges);
  if (NS_FAILED(rv)) {
    return;
  }
  mIndex->UpdateMoofIndex(byteRanges);
  mNeedReIndex = false;
}

RefPtr<MP4TrackDemuxer::SeekPromise> MP4TrackDemuxer::Seek(
    const TimeUnit& aTime) {
  auto seekTime = aTime;
  mQueuedSample = nullptr;

  mIterator->Seek(seekTime);

  bool hasSeenValidSamples = false, seekingFromFirstSyncSample = false;
  // Check what time we actually seeked to.
  do {
    auto next = GetNextSample();
    if (next.isErr()) {
      auto error = next.unwrapErr();
      // If we reached EOS without finding an IDR I-frame we retry from the
      // first sync sample in the stss, which is always an IDR.
      if ((mType == kH264 || mType == kHEVC) &&
          error == NS_ERROR_DOM_MEDIA_END_OF_STREAM && hasSeenValidSamples &&
          !seekingFromFirstSyncSample) {
        LOG("Can not find a key frame from the closet sync sample, try again "
            "from the first sync sample");
        seekingFromFirstSyncSample = true;
        mIterator->Seek(seekTime, SampleIterator::SyncSampleMode::First);
        continue;
      }
      return SeekPromise::CreateAndReject(error, __func__);
    }
    RefPtr<MediaRawData> sample = next.unwrap();
    if (!sample->Size()) {
      // This sample can't be decoded, continue searching.
      continue;
    }
    if (sample->mKeyframe) {
      MOZ_DIAGNOSTIC_ASSERT(sample->HasValidTime());
      mQueuedSample = sample;
      seekTime = mQueuedSample->mTime;
    }
    hasSeenValidSamples = true;
  } while (!mQueuedSample);

  SetNextKeyFrameTime();

  return SeekPromise::CreateAndResolve(seekTime, __func__);
}

Result<already_AddRefed<MediaRawData>, MediaResult>
MP4TrackDemuxer::GetNextSample() {
  auto next = mIterator->GetNext();
  if (next.isErr()) {
    return next;
  }
  RefPtr<MediaRawData> sample = next.unwrap();

  if (mInfo->GetAsVideoInfo()) {
    sample->mExtraData = mInfo->GetAsVideoInfo()->mExtraData;
    if (mType == kH264 && !sample->mCrypto.IsEncrypted()) {
      H264::FrameType type = H264::GetFrameType(sample);
      switch (type) {
        case H264::FrameType::I_FRAME_IDR:
        case H264::FrameType::I_FRAME_OTHER:
        case H264::FrameType::OTHER: {
          bool keyframe = type == H264::FrameType::I_FRAME_IDR;
          if (sample->mKeyframe != keyframe) {
            NS_WARNING(nsPrintfCString("Frame incorrectly marked as %skeyframe "
                                       "@ pts:%" PRId64 " dur:%" PRId64
                                       " dts:%" PRId64,
                                       keyframe ? "" : "non-",
                                       sample->mTime.ToMicroseconds(),
                                       sample->mDuration.ToMicroseconds(),
                                       sample->mTimecode.ToMicroseconds())
                           .get());
            sample->mKeyframe = keyframe;
          }
          break;
        }
        case H264::FrameType::INVALID:
          NS_WARNING(nsPrintfCString("Invalid H264 frame @ pts:%" PRId64
                                     " dur:%" PRId64 " dts:%" PRId64,
                                     sample->mTime.ToMicroseconds(),
                                     sample->mDuration.ToMicroseconds(),
                                     sample->mTimecode.ToMicroseconds())
                         .get());
          // We could reject the sample now, however demuxer errors are fatal.
          // So we keep the invalid frame, relying on the H264 decoder to
          // handle the error later.
          // TODO: make demuxer errors non-fatal.
          break;
      }
    } else if (mType == kHEVC && !sample->mCrypto.IsEncrypted()) {
#ifdef MOZ_APPLEMEDIA
      // VideoToolbox can return a bad data error if a CRA frame is the first
      // sample after a seek. Only IDR_W_RADL/IDR_N_LP are safe starting points.
      auto isKeyframe = H265::IsKeyFrame(sample);
#else
      // On non-Apple platforms, the other RAP types are acceptable.
      auto isKeyframe = H265::IsRandomAccessPoint(sample);
#endif
      bool keyframe = isKeyframe.isOk() && isKeyframe.unwrap();
      if (sample->mKeyframe != keyframe) {
        NS_WARNING(nsPrintfCString(
                       "HEVC frame incorrectly marked as %skeyframe "
                       "@ pts:%" PRId64 " dur:%" PRId64 " dts:%" PRId64,
                       keyframe ? "" : "non-", sample->mTime.ToMicroseconds(),
                       sample->mDuration.ToMicroseconds(),
                       sample->mTimecode.ToMicroseconds())
                       .get());
        sample->mKeyframe = keyframe;
      }
    } else if (mType == kVP9 && !sample->mCrypto.IsEncrypted()) {
      bool keyframe = VPXDecoder::IsKeyframe(
          Span<const uint8_t>(sample->Data(), sample->Size()),
          VPXDecoder::Codec::VP9);
      if (sample->mKeyframe != keyframe) {
        NS_WARNING(nsPrintfCString(
                       "Frame incorrectly marked as %skeyframe "
                       "@ pts:%" PRId64 " dur:%" PRId64 " dts:%" PRId64,
                       keyframe ? "" : "non-", sample->mTime.ToMicroseconds(),
                       sample->mDuration.ToMicroseconds(),
                       sample->mTimecode.ToMicroseconds())
                       .get());
        sample->mKeyframe = keyframe;
      }
    } else if (mType == kAV1 && !sample->mCrypto.IsEncrypted()) {
      bool keyframe = AOMDecoder::IsKeyframe(
          Span<const uint8_t>(sample->Data(), sample->Size()));
      if (sample->mKeyframe != keyframe) {
        NS_WARNING(nsPrintfCString(
                       "Frame incorrectly marked as %skeyframe "
                       "@ pts:%" PRId64 " dur:%" PRId64 " dts:%" PRId64,
                       keyframe ? "" : "non-", sample->mTime.ToMicroseconds(),
                       sample->mDuration.ToMicroseconds(),
                       sample->mTimecode.ToMicroseconds())
                       .get());
        sample->mKeyframe = keyframe;
      }
    }
  }

  // Adjust trimming information if needed.
  if (mInfo->GetAsAudioInfo()) {
    AudioInfo* info = mInfo->GetAsAudioInfo();
    TimeUnit originalPts = sample->mTime;
    TimeUnit originalEnd = sample->GetEndTime();
    if (sample->mTime.IsNegative()) {
      sample->mTime = TimeUnit::Zero(originalPts);
      sample->mDuration = std::max(TimeUnit::Zero(sample->mTime),
                                   originalPts + sample->mDuration);
      sample->mOriginalPresentationWindow =
          Some(TimeInterval{originalPts, originalEnd});
    }
    // The demuxer only knows the presentation time of the packet, not the
    // actual number of samples that will be decoded from this packet.
    // However we need to trim the last packet to the correct duration.
    // Find the actual size of the decoded packet to know how many samples to
    // trim. This only works because the packet size are constant.
    TimeUnit totalMediaDurationIncludingTrimming =
        info->mDuration - info->mMediaTime;
    if (mType == kAAC &&
        sample->GetEndTime() >= totalMediaDurationIncludingTrimming &&
        totalMediaDurationIncludingTrimming.IsPositive()) {
      // Seek backward a bit.
      mIterator->Seek(sample->mTime - sample->mDuration);
      RefPtr<MediaRawData> previousSample =
          mIterator->GetNext().unwrapOr(nullptr);
      if (previousSample) {
        TimeInterval fullPacketDuration{previousSample->mTime,
                                        previousSample->GetEndTime()};
        sample->mOriginalPresentationWindow = Some(TimeInterval{
            originalPts, originalPts + fullPacketDuration.Length()});
      }
      // Seek back so we're back at the original location -- there's no packet
      // left anyway.
      mIterator->Seek(sample->mTime);
      RefPtr<MediaRawData> dummy = mIterator->GetNext().unwrapOr(nullptr);
    }
  }

  if (MOZ_LOG_TEST(gMediaDemuxerLog, LogLevel::Verbose)) {
    bool isAudio = mInfo->GetAsAudioInfo();
    TimeUnit originalStart = TimeUnit::Invalid();
    TimeUnit originalEnd = TimeUnit::Invalid();
    if (sample->mOriginalPresentationWindow) {
      originalStart = sample->mOriginalPresentationWindow->mStart;
      originalEnd = sample->mOriginalPresentationWindow->mEnd;
    }
    LOG("{} packet demuxed (track id: {}): [{},{}], duration: {} (original "
        "time: [{},{}])",
        isAudio ? "Audio" : "Video", mInfo->mTrackId,
        sample->mTime.ToString().get(), sample->GetEndTime().ToString().get(),
        sample->mDuration.ToString().get(), originalStart.ToString().get(),
        originalEnd.ToString().get());
  }

  return sample.forget();
}

RefPtr<MP4TrackDemuxer::SamplesPromise> MP4TrackDemuxer::GetSamples(
    int32_t aNumSamples) {
  EnsureUpToDateIndex();
  RefPtr<SamplesHolder> samples = new SamplesHolder;
  if (!aNumSamples) {
    return SamplesPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_DEMUXER_ERR,
                                           __func__);
  }

  if (mQueuedSample) {
    NS_ASSERTION(mQueuedSample->mKeyframe, "mQueuedSample must be a keyframe");
    samples->AppendSample(std::move(mQueuedSample));
    MOZ_ASSERT(!mQueuedSample);
    aNumSamples--;
  }
  while (aNumSamples) {
    auto next = GetNextSample();
    if (next.isErr()) {
      nsresult rv = next.inspectErr().Code();
      if ((rv != NS_ERROR_DOM_MEDIA_END_OF_STREAM &&
           rv != NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA) ||
          samples->GetSamples().IsEmpty()) {
        return SamplesPromise::CreateAndReject(next.unwrapErr(), __func__);
      }
      break;
    }
    RefPtr<MediaRawData> sample = next.unwrap();
    if (!sample->Size()) {
      continue;
    }
    MOZ_DIAGNOSTIC_ASSERT(sample->HasValidTime());
    samples->AppendSample(std::move(sample));
    aNumSamples--;
  }

  if (mNextKeyframeTime.isNothing() ||
      samples->GetSamples().LastElement()->mTime >= mNextKeyframeTime.value()) {
    SetNextKeyFrameTime();
  }
  return SamplesPromise::CreateAndResolve(samples, __func__);
}

void MP4TrackDemuxer::SetNextKeyFrameTime() {
  mNextKeyframeTime.reset();
  TimeUnit frameTime = mIterator->GetNextKeyframeTime();
  if (frameTime.IsValid()) {
    mNextKeyframeTime.emplace(frameTime);
  }
}

void MP4TrackDemuxer::Reset() {
  mQueuedSample = nullptr;
  // TODO: verify this
  mIterator->Seek(TimeUnit::FromNegativeInfinity());
  SetNextKeyFrameTime();
}

nsresult MP4TrackDemuxer::GetNextRandomAccessPoint(TimeUnit* aTime) {
  if (mNextKeyframeTime.isNothing()) {
    // There's no next key frame.
    *aTime = TimeUnit::FromInfinity();
  } else {
    *aTime = mNextKeyframeTime.value();
  }
  return NS_OK;
}

RefPtr<MP4TrackDemuxer::SkipAccessPointPromise>
MP4TrackDemuxer::SkipToNextRandomAccessPoint(const TimeUnit& aTimeThreshold) {
  mQueuedSample = nullptr;
  // Loop until we reach the next keyframe after the threshold.
  uint32_t parsed = 0;
  Maybe<SkipFailureHolder> failure;
  while (true) {
    auto next = GetNextSample();
    if (next.isErr()) {
      failure.emplace(next.unwrapErr(), parsed);
      break;
    }
    RefPtr<MediaRawData> sample = next.unwrap();
    parsed++;
    MOZ_DIAGNOSTIC_ASSERT(sample->HasValidTime());
    if (sample->mKeyframe && sample->mTime >= aTimeThreshold) {
      mQueuedSample = sample;
      break;
    }
  }
  SetNextKeyFrameTime();
  if (failure.isSome()) {
    return SkipAccessPointPromise::CreateAndReject(failure.extract(), __func__);
  }
  return SkipAccessPointPromise::CreateAndResolve(parsed, __func__);
}

TimeIntervals MP4TrackDemuxer::GetBuffered() {
  EnsureUpToDateIndex();
  AutoPinned<MediaResource> resource(mResource);
  MediaByteRangeSet byteRanges;
  nsresult rv = resource->GetCachedRanges(byteRanges);

  if (NS_FAILED(rv)) {
    return TimeIntervals();
  }

  TimeIntervals timeRanges = mIndex->ConvertByteRangesToTimeRanges(byteRanges);
  if (AudioInfo* info = mInfo->GetAsAudioInfo(); info) {
    // Trim as in GetNextSample().
    TimeUnit totalMediaDurationIncludingTrimming =
        info->mDuration - info->mMediaTime;
    auto end = TimeUnit::FromInfinity();
    if (mType == kAAC && totalMediaDurationIncludingTrimming.IsPositive()) {
      end = info->mDuration;
    }
    if (timeRanges.GetStart().IsNegative() || timeRanges.GetEnd() > end) {
      TimeInterval trimming(TimeUnit::Zero(timeRanges.GetStart()), end);
      timeRanges = timeRanges.Intersection(trimming);
    }
  }

  return timeRanges;
}

void MP4TrackDemuxer::NotifyDataArrived() { mNeedReIndex = true; }

void MP4TrackDemuxer::NotifyDataRemoved() {
  AutoPinned<MediaResource> resource(mResource);
  MediaByteRangeSet byteRanges;
  nsresult rv = resource->GetCachedRanges(byteRanges);
  if (NS_FAILED(rv)) {
    return;
  }
  mIndex->UpdateMoofIndex(byteRanges, true /* can evict */);
  mNeedReIndex = false;
}

}  // namespace mozilla

#undef LOG
