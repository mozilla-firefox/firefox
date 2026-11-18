/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_MEDIA_MEDIASOURCE_MPEGAUDIOBYTESTREAMDEMUXER_H_
#define DOM_MEDIA_MEDIASOURCE_MPEGAUDIOBYTESTREAMDEMUXER_H_

#include "MPEGAudioByteStream.h"
#include "MediaDataDemuxer.h"
#include "MediaInfo.h"
#include "SourceBufferResource.h"
#include "mozilla/Result.h"
#include "mozilla/Span.h"
#include "nsTArray.h"

namespace mozilla {

class MPEGAudioByteStreamTrackDemuxer;

DDLoggedTypeDeclNameAndBase(MPEGAudioByteStreamDemuxer, MediaDataDemuxer);

// Demuxes the MSE byte stream format for MPEG audio for TrackBuffersManager.
// Init() reads the initialization segment that MPEGAudioContainerParser
// extracted, a frame header, from the start of the resource. The frames that
// are appended after it, without the bytes that the container parser ignores,
// are returned as samples. Their timestamps only count the samples that came
// before them, as the coded frame processing algorithm generates the
// presentation timestamps.
class MPEGAudioByteStreamDemuxer
    : public MediaDataDemuxer,
      public DecoderDoctorLifeLogger<MPEGAudioByteStreamDemuxer> {
 public:
  explicit MPEGAudioByteStreamDemuxer(SourceBufferResource* aSource);

  RefPtr<InitPromise> Init() override;
  uint32_t GetNumberTracks(TrackInfo::TrackType aType) const override;
  already_AddRefed<MediaTrackDemuxer> GetTrackDemuxer(
      TrackInfo::TrackType aType, uint32_t aTrackNumber) override;
  bool IsSeekable() const override { return false; }

 private:
  const RefPtr<SourceBufferResource> mSource;
  RefPtr<MPEGAudioByteStreamTrackDemuxer> mTrackDemuxer;
};

DDLoggedTypeNameAndBase(MPEGAudioByteStreamTrackDemuxer, MediaTrackDemuxer);

class MPEGAudioByteStreamTrackDemuxer
    : public MediaTrackDemuxer,
      public DecoderDoctorLifeLogger<MPEGAudioByteStreamTrackDemuxer> {
 public:
  explicit MPEGAudioByteStreamTrackDemuxer(SourceBufferResource* aSource);

  bool Init();

  UniquePtr<TrackInfo> GetInfo() const override;
  RefPtr<SeekPromise> Seek(const media::TimeUnit& aTime) override;
  RefPtr<SamplesPromise> GetSamples(int32_t aNumSamples = 1) override;
  void Reset() override {}
  RefPtr<SkipAccessPointPromise> SkipToNextRandomAccessPoint(
      const media::TimeUnit& aTimeThreshold) override;
  int64_t GetResourceOffset() const override { return mOffset; }
  media::TimeIntervals GetBuffered() override;
  // Everything before the first frame that hasn't been demuxed can be evicted.
  int64_t GetEvictionOffset(const media::TimeUnit& aTime) override {
    return mOffset;
  }

 private:
  ~MPEGAudioByteStreamTrackDemuxer() = default;

  // Returns nullptr if the next frame isn't complete yet.
  Result<RefPtr<MediaRawData>, MediaResult> GetNextSample();
  // Returns up to aSize bytes from mOffset, as many as are available.
  Result<Span<const uint8_t>, MediaResult> Peek(size_t aSize);

  const RefPtr<SourceBufferResource> mSource;
  AudioInfo mInfo;
  mpeg_audio::FrameInfo mConfig;
  // Offset of the first frame that hasn't been demuxed.
  int64_t mOffset = 0;
  // Number of audio samples per channel demuxed so far.
  int64_t mSamples = 0;
  // Holds bytes that span several appended buffers.
  nsTArray<uint8_t> mScratch;
};

}  // namespace mozilla

#endif  // DOM_MEDIA_MEDIASOURCE_MPEGAUDIOBYTESTREAMDEMUXER_H_
