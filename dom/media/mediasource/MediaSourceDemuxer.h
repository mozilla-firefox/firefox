/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#if !defined(MediaSourceDemuxer_h_)
#  define MediaSourceDemuxer_h_

#  include "MediaDataDemuxer.h"
#  include "MediaResource.h"
#  include "MediaSource.h"
#  include "TrackBuffersManager.h"
#  include "mozilla/EventTargetAndLockCapability.h"
#  include "mozilla/Maybe.h"
#  include "mozilla/TaskQueue.h"
#  include "mozilla/dom/MediaDebugInfoBinding.h"

namespace mozilla {

class AbstractThread;
class MediaResult;
class MediaSourceTrackDemuxer;

DDLoggedTypeDeclNameAndBase(MediaSourceDemuxer, MediaDataDemuxer);
DDLoggedTypeNameAndBase(MediaSourceTrackDemuxer, MediaTrackDemuxer);

class MediaSourceDemuxer : public MediaDataDemuxer,
                           public DecoderDoctorLifeLogger<MediaSourceDemuxer> {
 public:
  explicit MediaSourceDemuxer(AbstractThread* aAbstractMainThread);

  RefPtr<InitPromise> Init() override;

  uint32_t GetNumberTracks(TrackInfo::TrackType aType) const override;

  already_AddRefed<MediaTrackDemuxer> GetTrackDemuxer(
      TrackInfo::TrackType aType, uint32_t aTrackNumber) override;

  bool IsSeekable() const override;

  UniquePtr<EncryptionInfo> GetCrypto() override;

  bool ShouldComputeStartTime() const override { return false; }

  /* interface for TrackBuffersManager */
  void AttachSourceBuffer(const RefPtr<TrackBuffersManager>& aSourceBuffer);
  void DetachSourceBuffer(const RefPtr<TrackBuffersManager>& aSourceBuffer);
  TaskQueue* GetTaskQueue() { return mTaskQueue; }
  void NotifyInitDataArrived();

  // Populates aInfo with info describing the state of the MediaSource internal
  // buffered data. Used for debugging purposes.
  // aInfo should *not* be accessed until the returned promise has been resolved
  // or rejected.
  RefPtr<GenericPromise> GetDebugInfo(
      dom::MediaSourceDemuxerDebugInfo& aInfo) const;

  void AddSizeOfResources(MediaSourceDecoder::ResourceSizes* aSizes);

  // Gap allowed between frames.
  // Due to inaccuracies in determining buffer end
  // frames (Bug 1065207). This value is based on videos seen in the wild.
  static constexpr media::TimeUnit EOS_FUZZ =
      media::TimeUnit::FromMicroseconds(500000);

  // Largest gap allowed between muxed streams with different
  // start times. The specs suggest up to a "reasonably short" gap of
  // one second, which we use here.
  // See: https://www.w3.org/TR/media-source-2/#presentation-start-time
  static constexpr media::TimeUnit EOS_FUZZ_START =
      media::TimeUnit::FromMicroseconds(1000000);

 private:
  ~MediaSourceDemuxer();
  friend class MediaSourceTrackDemuxer;
  // Scan source buffers and update information.
  bool ScanSourceBuffersForContent();
  RefPtr<TrackBuffersManager> GetManager(TrackInfo::TrackType aType)
      MOZ_REQUIRES(mMutex);
  TrackInfo* GetTrackInfo(TrackInfo::TrackType) MOZ_REQUIRES(mMutex);
  void DoAttachSourceBuffer(RefPtr<TrackBuffersManager>&& aSourceBuffer);
  void DoDetachSourceBuffer(const RefPtr<TrackBuffersManager>& aSourceBuffer);
  bool OnTaskQueue() {
    return !GetTaskQueue() || GetTaskQueue()->IsCurrentThreadIn();
  }

  RefPtr<TaskQueue> mTaskQueue;
  // Accessed on mTaskQueue or from destructor
  nsTArray<RefPtr<TrackBuffersManager>> mSourceBuffers;
  MozPromiseHolder<InitPromise> mInitPromise;

  // Mutex to protect members below across multiple threads.
  mutable Mutex mMutex;
  nsTArray<RefPtr<MediaSourceTrackDemuxer>> mDemuxers MOZ_GUARDED_BY(mMutex);
  RefPtr<TrackBuffersManager> mAudioTrack MOZ_GUARDED_BY(mMutex);
  RefPtr<TrackBuffersManager> mVideoTrack MOZ_GUARDED_BY(mMutex);
  MediaInfo mInfo MOZ_GUARDED_BY(mMutex);
};

class MediaSourceTrackDemuxer
    : public MediaTrackDemuxer,
      public DecoderDoctorLifeLogger<MediaSourceTrackDemuxer> {
 public:
  MediaSourceTrackDemuxer(MediaSourceDemuxer* aParent,
                          TrackInfo::TrackType aType,
                          TrackBuffersManager* aManager)
      MOZ_REQUIRES(aParent->mMutex);

  UniquePtr<TrackInfo> GetInfo() const override;

  RefPtr<SeekPromise> Seek(const media::TimeUnit& aTime) override;

  RefPtr<SamplesPromise> GetSamples(int32_t aNumSamples = 1) override;

  void Reset() override;

  nsresult GetNextRandomAccessPoint(media::TimeUnit* aTime) override;

  RefPtr<SkipAccessPointPromise> SkipToNextRandomAccessPoint(
      const media::TimeUnit& aTimeThreshold) override;

  media::TimeIntervals GetBuffered() override;

  void BreakCycles() override;

  bool GetSamplesMayBlock() const override { return false; }

  bool HasManager(TrackBuffersManager* aManager) const;
  void DetachManager();

 private:
  mozilla::Mutex& Mutex() MOZ_RETURN_CAPABILITY(mLock.Lock()) {
    return mLock.Lock();
  }
  const EventTargetCapability<mozilla::TaskQueue>& TaskQueue() const
      MOZ_RETURN_CAPABILITY(mLock.Target()) {
    return mLock.Target();
  }

  RefPtr<SeekPromise> DoSeek(const media::TimeUnit& aTime);
  RefPtr<SamplesPromise> DoGetSamples(int32_t aNumSamples);
  RefPtr<SkipAccessPointPromise> DoSkipToNextRandomAccessPoint(
      const media::TimeUnit& aTimeThreadshold);
  already_AddRefed<MediaRawData> GetSample(MediaResult& aError);
  // Return the timestamp of the next keyframe after mLastSampleIndex.
  media::TimeUnit GetNextRandomAccessPoint();
  // Shift a sample of a track that declares a decoder delay, so that its
  // decoded audio lines up with its timestamps, and trim the decoded audio to
  // what the track buffer kept.
  void CompensateDecoderDelay(MediaRawData* aSample);

  RefPtr<MediaSourceDemuxer> mParent;

  TrackInfo::TrackType mType;
  // Mutex protecting members below accessed from multiple threads.
  EventTargetAndLockCapability<mozilla::TaskQueue, mozilla::Mutex> mLock;
  media::TimeUnit mNextRandomAccessPoint MOZ_GUARDED_BY(mLock);
  // Would be accessed in MFR's demuxer proxy task queue and TaskQueue, and
  // only be set on the TaskQueue. It can be accessed while on TaskQueue without
  // the need for the lock.
  RefPtr<TrackBuffersManager> mManager MOZ_GUARDED_BY(mLock);

  // Only accessed on TaskQueue
  Maybe<RefPtr<MediaRawData>> mNextSample;
  // Set to true following a reset. Ensure that the next sample demuxed
  // is available at position 0.
  // Only accessed on TaskQueue
  bool mReset;

  // Amount of pre-roll time when seeking.
  // Set to 80ms if track is Opus.
  const media::TimeUnit mPreRoll;

  // The stream of the last sample returned since the decoder last restarted,
  // as it does after a seek or a reset. A sample from another stream is
  // decoded by a new decoder. Only accessed on TaskQueue.
  Maybe<uint32_t> mLastStreamID;
  // The time before which decoded audio is dropped, until the decoded audio
  // reaches it. Only accessed on TaskQueue.
  Maybe<media::TimeUnit> mTrimBefore;
};

}  // namespace mozilla

#endif
