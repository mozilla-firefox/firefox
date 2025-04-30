/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#if !defined(MediaFormatReader_h_)
#  define MediaFormatReader_h_

#  include "FrameStatistics.h"
#  include "MediaDataDemuxer.h"
#  include "MediaEventSource.h"
#  include "MediaMetadataManager.h"
#  include "MediaPromiseDefs.h"
#  include "PlatformDecoderModule.h"
#  include "SeekTarget.h"
#  include "mozilla/Atomics.h"
#  include "mozilla/Maybe.h"
#  include "mozilla/MozPromise.h"
#  include "mozilla/Mutex.h"
#  include "mozilla/StateMirroring.h"
#  include "mozilla/StaticPrefs_media.h"
#  include "mozilla/TaskQueue.h"
#  include "mozilla/TimeStamp.h"
#  include "mozilla/ThreadSafeWeakPtr.h"
#  include "mozilla/dom/MediaDebugInfoBinding.h"

namespace mozilla {

class CDMProxy;
class GMPCrashHelper;
class MediaResource;
class VideoFrameContainer;

struct WaitForDataRejectValue {
  enum Reason { SHUTDOWN, CANCELED };

  WaitForDataRejectValue(MediaData::Type aType, Reason aReason)
      : mType(aType), mReason(aReason) {}
  MediaData::Type mType;
  Reason mReason;
};

struct SeekRejectValue {
  MOZ_IMPLICIT SeekRejectValue(const MediaResult& aError)
      : mType(MediaData::Type::NULL_DATA), mError(aError) {}
  MOZ_IMPLICIT SeekRejectValue(nsresult aResult)
      : mType(MediaData::Type::NULL_DATA), mError(aResult) {}
  SeekRejectValue(MediaData::Type aType, const MediaResult& aError)
      : mType(aType), mError(aError) {}
  MediaData::Type mType;
  MediaResult mError;
};

struct MetadataHolder {
  UniquePtr<MediaInfo> mInfo;
  UniquePtr<MetadataTags> mTags;
};

using MediaDecoderOwnerID = void*;

struct MOZ_STACK_CLASS MediaFormatReaderInit {
  MediaResource* mResource = nullptr;
  VideoFrameContainer* mVideoFrameContainer = nullptr;
  FrameStatistics* mFrameStats = nullptr;
  already_AddRefed<layers::KnowsCompositor> mKnowsCompositor;
  already_AddRefed<GMPCrashHelper> mCrashHelper;
  // Used in bug 1393399 for temporary telemetry.
  MediaDecoderOwnerID mMediaDecoderOwnerID = nullptr;
  Maybe<TrackingId> mTrackingId;
};

DDLoggedTypeDeclName(MediaFormatReader);

class MediaFormatReader final
    : public SupportsThreadSafeWeakPtr<MediaFormatReader>,
      public DecoderDoctorLifeLogger<MediaFormatReader> {
  static const bool IsExclusive = true;
  using TrackType = TrackInfo::TrackType;
  using NotifyDataArrivedPromise = MozPromise<bool, MediaResult, IsExclusive>;

 public:
  MOZ_DECLARE_REFCOUNTED_TYPENAME(MediaFormatReader)

  using TrackSet = EnumSet<TrackInfo::TrackType>;
  using MetadataPromise = MozPromise<MetadataHolder, MediaResult, IsExclusive>;

  template <typename Type>
  using DataPromise = MozPromise<RefPtr<Type>, MediaResult, IsExclusive>;
  using AudioDataPromise = DataPromise<AudioData>;
  using VideoDataPromise = DataPromise<VideoData>;

  using SeekPromise = MozPromise<media::TimeUnit, SeekRejectValue, IsExclusive>;

  // Note that, conceptually, WaitForData makes sense in a non-exclusive sense.
  // But in the current architecture it's only ever used exclusively (by MDSM),
  // so we mark it that way to verify our assumptions. If you have a use-case
  // for multiple WaitForData consumers, feel free to flip the exclusivity here.
  using WaitForDataPromise =
      MozPromise<MediaData::Type, WaitForDataRejectValue, IsExclusive>;

  MediaFormatReader(MediaFormatReaderInit& aInit, MediaDataDemuxer* aDemuxer);
  virtual ~MediaFormatReader();

  // Initializes the reader, returns NS_OK on success, or NS_ERROR_FAILURE
  // on failure.
  nsresult Init();

  size_t SizeOfVideoQueueInFrames();
  size_t SizeOfAudioQueueInFrames();

  // Requests one video sample from the reader.
  RefPtr<VideoDataPromise> RequestVideoData(
      const media::TimeUnit& aTimeThreshold,
      bool aRequestNextVideoKeyFrame = false);

  // Requests one audio sample from the reader.
  //
  // The decode should be performed asynchronously, and the promise should
  // be resolved when it is complete.
  RefPtr<AudioDataPromise> RequestAudioData();

  // The default implementation of AsyncReadMetadata is implemented in terms of
  // synchronous ReadMetadata() calls. Implementations may also
  // override AsyncReadMetadata to create a more proper async implementation.
  RefPtr<MetadataPromise> AsyncReadMetadata();

  // Fills aInfo with the latest cached data required to present the media,
  // ReadUpdatedMetadata will always be called once ReadMetadata has succeeded.
  void ReadUpdatedMetadata(MediaInfo* aInfo);

  RefPtr<SeekPromise> Seek(const SeekTarget& aTarget);

  // Called once new data has been cached by the MediaResource.
  // mBuffered should be recalculated and updated accordingly.
  void NotifyDataArrived();

  // Update ID for the external playback engine. Currently it's only used on
  // Windows when the media engine playback is enabled.
  void UpdateMediaEngineId(uint64_t aMediaEngineId);

  // This function will be called if the media key is set before playback
  // starts, indicating the playback should be encrypted.
  void SetEncryptedCustomIdent();

  bool IsEncryptedCustomIdent() const { return mEncryptedCustomIdent; }

 protected:
  // Recomputes mBuffered.
  void UpdateBuffered();

 public:
  // Called by MDSM in dormant state to release resources allocated by this
  // reader. The reader can resume decoding by calling Seek() to a specific
  // position.
  void ReleaseResources();

  bool OnTaskQueue() const { return OwnerThread()->IsCurrentThreadIn(); }

  // Resets all state related to decoding, emptying all buffers etc.
  // Cancels all pending Request*Data() request callbacks, rejects any
  // outstanding seek promises, and flushes the decode pipeline. The
  // decoder must not call any of the callbacks for outstanding
  // Request*Data() calls after this is called. Calls to Request*Data()
  // made after this should be processed as usual.
  //
  // Normally this call preceedes a Seek() call, or shutdown.
  //
  // aParam is a set of TrackInfo::TrackType enums specifying which
  // queues need to be reset, defaulting to both audio and video tracks.
  nsresult ResetDecode(const TrackSet& aTracks);

  // Destroys the decoding state. The reader cannot be made usable again.
  // This is different from ReleaseMediaResources() as it is irreversable,
  // whereas ReleaseMediaResources() is.  Must be called on the decode
  // thread.
  RefPtr<ShutdownPromise> Shutdown();

  // Returns true if this decoder reader uses hardware accelerated video
  // decoding.
  bool VideoIsHardwareAccelerated() const;

  // By default, the state machine polls the reader once per second when it's
  // in buffering mode. Some readers support a promise-based mechanism by which
  // they notify the state machine when the data arrives.
  bool IsWaitForDataSupported() const { return true; }

  RefPtr<WaitForDataPromise> WaitForData(MediaData::Type aType);

  // The MediaDecoderStateMachine uses various heuristics that assume that
  // raw media data is arriving sequentially from a network channel. This
  // makes sense in the <video src="foo"> case, but not for more advanced use
  // cases like MSE.
  bool UseBufferingHeuristics() const { return mTrackDemuxersMayBlock; }

  RefPtr<SetCDMPromise> SetCDMProxy(CDMProxy* aProxy);

  // Requests that the MediaFormatReader populates aInfo with debug information.
  // This may be done asynchronously, and aInfo should *not* be accessed by the
  // caller until the returned promise is resolved or rejected.
  RefPtr<GenericPromise> RequestDebugInfo(
      dom::MediaFormatReaderDebugInfo& aInfo);

  // Switch the video decoder to NullDecoderModule. It might takes effective
  // since a few samples later depends on how much demuxed samples are already
  // queued in the original video decoder.
  void SetVideoNullDecode(bool aIsNullDecode);

  void UpdateCompositor(already_AddRefed<layers::KnowsCompositor>);

  void UpdateDuration(const media::TimeUnit& aDuration) {
    MOZ_ASSERT(OnTaskQueue());
    UpdateBuffered();
  }

  AbstractCanonical<media::TimeIntervals>* CanonicalBuffered() {
    return &mBuffered;
  }

  TaskQueue* OwnerThread() const { return mTaskQueue; }

  TimedMetadataEventSource& TimedMetadataEvent() { return mTimedMetadataEvent; }

  // Notified by the OggDemuxer during playback when chained ogg is detected.
  MediaEventSource<void>& OnMediaNotSeekable() { return mOnMediaNotSeekable; }

  TimedMetadataEventProducer& TimedMetadataProducer() {
    return mTimedMetadataEvent;
  }

  MediaEventProducer<void>& MediaNotSeekableProducer() {
    return mOnMediaNotSeekable;
  }

  // Notified if the reader can't decode a sample due to a missing decryption
  // key.
  MediaEventSource<TrackInfo::TrackType>& OnTrackWaitingForKey() {
    return mOnTrackWaitingForKey;
  }

  MediaEventProducer<TrackInfo::TrackType>& OnTrackWaitingForKeyProducer() {
    return mOnTrackWaitingForKey;
  }

  MediaEventSource<nsTArray<uint8_t>, nsString>& OnEncrypted() {
    return mOnEncrypted;
  }

  MediaEventSource<void>& OnWaitingForKey() { return mOnWaitingForKey; }

  MediaEventSource<MediaResult>& OnDecodeWarning() { return mOnDecodeWarning; }

  MediaEventProducer<VideoInfo, AudioInfo>& OnTrackInfoUpdatedEvent() {
    return mTrackInfoUpdatedEvent;
  }

 private:
  bool HasVideo() const { return mVideo.mTrackDemuxer; }
  bool HasAudio() const { return mAudio.mTrackDemuxer; }

  bool IsWaitingOnCDMResource();

  bool InitDemuxer();
  // Notify the track demuxers that new data has been received.
  void NotifyTrackDemuxers();
  void ReturnOutput(MediaData* aData, TrackType aTrack);

  // Enqueues a task to call Update(aTrack) on the decoder task queue.
  // Lock for corresponding track must be held.
  void ScheduleUpdate(TrackType aTrack);
  void Update(TrackType aTrack);
  // Handle actions should more data be received.
  // Returns true if no more action is required.
  bool UpdateReceivedNewData(TrackType aTrack);
  // Called when new samples need to be demuxed.
  void RequestDemuxSamples(TrackType aTrack);
  // Handle demuxed samples by the input behavior.
  void HandleDemuxedSamples(TrackType aTrack,
                            FrameStatistics::AutoNotifyDecoded& aA);
  // Decode any pending already demuxed samples.
  void DecodeDemuxedSamples(TrackType aTrack, MediaRawData* aSample);

  struct InternalSeekTarget {
    InternalSeekTarget(const media::TimeInterval& aTime, bool aDropTarget)
        : mTime(aTime),
          mDropTarget(aDropTarget),
          mWaiting(false),
          mHasSeeked(false) {}

    media::TimeUnit Time() const { return mTime.mStart; }
    media::TimeUnit EndTime() const { return mTime.mEnd; }
    bool Contains(const media::TimeUnit& aTime) const {
      return mTime.Contains(aTime);
    }

    media::TimeInterval mTime;
    bool mDropTarget;
    // Whether known waiting for more raw packets, either for the random
    // access point or dependent frames.
    bool mWaiting;
    // Whether `MediaTrackDemuxer::Seek()` has found the preceding random
    // access point.
    bool mHasSeeked;
  };

  // Perform an internal seek to aTime. If aDropTarget is true then
  // the first sample past the target will be dropped.
  void InternalSeek(TrackType aTrack, const InternalSeekTarget& aTarget);
  // Return the end time of the internal seek target if it exists. Otherwise,
  // return infinity.
  media::TimeUnit GetInternalSeekTargetEndTime() const;

  // Drain the current decoder.
  void DrainDecoder(TrackType aTrack);
  void NotifyNewOutput(TrackType aTrack,
                       MediaDataDecoder::DecodedData&& aResults);
  void NotifyError(TrackType aTrack, const MediaResult& aError);
  void NotifyWaitingForData(TrackType aTrack);
  void NotifyWaitingForKey(TrackType aTrack);
  void NotifyEndOfStream(TrackType aTrack);

  void ExtractCryptoInitData(nsTArray<uint8_t>& aInitData);

  // Initializes mLayersBackendType if possible.
  void InitLayersBackendType();

  void Reset(TrackType aTrack);
  void DropDecodedSamples(TrackType aTrack);

  // Return a target timeunit which the reader should skip to, this would be
  // either the timethreshold we pass, or the time of the next keyframe. Return
  // nothing if we don't need to skip.
  // @param aTimeThreshold
  // The time that we expect the time of next video frame should be or go beyond
  // @param aRequestNextVideoKeyFrame
  // If true and the next keyframe's time is larger than aTimeThreshold, skip to
  // the next keyframe time instead of aTimeThreshold.
  Maybe<media::TimeUnit> ShouldSkip(media::TimeUnit aTimeThreshold,
                                    bool aRequestNextVideoKeyFrame);

  void SetVideoDecodeThreshold();

  size_t SizeOfQueue(TrackType aTrack);

  void NotifyTrackInfoUpdated();

  enum class DrainState {
    None,
    DrainRequested,
    Draining,
    PartialDrainPending,
    DrainCompleted,
    DrainAborted,
  };

  class SharedShutdownPromiseHolder : public MozPromiseHolder<ShutdownPromise> {
    NS_INLINE_DECL_THREADSAFE_REFCOUNTING(SharedShutdownPromiseHolder)
   private:
    ~SharedShutdownPromiseHolder() = default;
  };

  struct DecoderData {
    DecoderData(MediaFormatReader* aOwner, MediaData::Type aType,
                uint32_t aNumOfMaxError)
        : mOwner(aOwner),
          mType(aType),
          mMutex("DecoderData"),
          mDescription("uninitialized"),
          mProcessName(""),
          mCodecName(""),
          mUpdateScheduled(false),
          mDemuxEOS(false),
          mWaitingForDataStartTime(Nothing()),
          mWaitingForKey(false),
          mReceivedNewData(false),
          mFlushing(false),
          mFlushed(true),
          mDrainState(DrainState::None),
          mNumOfConsecutiveDecodingError(0),
          mMaxConsecutiveDecodingError(aNumOfMaxError),
          mNumOfConsecutiveRDDOrGPUCrashes(0),
          mMaxConsecutiveRDDOrGPUCrashes(
              StaticPrefs::media_rdd_process_max_crashes()),
          mNumOfConsecutiveUtilityCrashes(0),
          mMaxConsecutiveUtilityCrashes(
              StaticPrefs::media_utility_process_max_crashes()),
          mFirstFrameTime(Some(media::TimeUnit::Zero())),
          mNumSamplesInput(0),
          mNumSamplesOutput(0),
          mNumSamplesOutputTotal(0),
          mNumSamplesSkippedTotal(0),
          mSizeOfQueue(0),
          mIsHardwareAccelerated(false),
          mLastStreamSourceID(UINT32_MAX),
          mIsNullDecode(false),
          mHardwareDecodingDisabled(false) {
      DecoderDoctorLogger::LogConstruction("MediaFormatReader::DecoderData",
                                           this);
    }

    ~DecoderData() {
      DecoderDoctorLogger::LogDestruction("MediaFormatReader::DecoderData",
                                          this);
    }

    MediaFormatReader* mOwner;
    // Disambiguate Audio vs Video.
    MediaData::Type mType;
    RefPtr<MediaTrackDemuxer> mTrackDemuxer;
    // TaskQueue on which decoder can choose to decode.
    // Only non-null up until the decoder is created.
    RefPtr<TaskQueue> mTaskQueue;

    // Mutex protecting mDescription, mDecoder, mTrackDemuxer, mWorkingInfo,
    // mProcessName and mCodecName as those can be read outside the TaskQueue.
    // They are only written on the TaskQueue however, as such mMutex doesn't
    // need to be held when those members are read on the TaskQueue.
    Mutex mMutex MOZ_UNANNOTATED;
    // The platform decoder.
    RefPtr<MediaDataDecoder> mDecoder;
    nsCString mDescription;
    nsCString mProcessName;
    nsCString mCodecName;
    void ShutdownDecoder();

    // Only accessed from reader's task queue.
    bool mUpdateScheduled;
    bool mDemuxEOS;
    Maybe<TimeStamp> mWaitingForDataStartTime;
    bool mWaitingForKey;
    bool mReceivedNewData;
    UniquePtr<PerformanceRecorderMulti<PlaybackStage>> mDecodePerfRecorder;

    // Pending seek.
    MozPromiseRequestHolder<MediaTrackDemuxer::SeekPromise> mSeekRequest;

    // Queued demuxed samples waiting to be decoded.
    nsTArray<RefPtr<MediaRawData>> mQueuedSamples;
    MozPromiseRequestHolder<MediaTrackDemuxer::SamplesPromise> mDemuxRequest;
    // A WaitingPromise is pending if the demuxer is waiting for data or
    // if the decoder is waiting for a key.
    MozPromiseHolder<WaitForDataPromise> mWaitingPromise;
    bool HasWaitingPromise() const {
      MOZ_ASSERT(mOwner->OnTaskQueue());
      return !mWaitingPromise.IsEmpty();
    }

    bool IsWaitingForData() const {
      MOZ_ASSERT(mOwner->OnTaskQueue());
      return !!mWaitingForDataStartTime;
    }

    bool IsWaitingForKey() const {
      MOZ_ASSERT(mOwner->OnTaskQueue());
      return mWaitingForKey && mDecodeRequest.Exists();
    }

    // MediaDataDecoder handler's variables.
    MozPromiseRequestHolder<MediaDataDecoder::DecodePromise> mDecodeRequest;
    bool mFlushing;  // True if flush is in action.
    // Set to true if the last operation run on the decoder was a flush.
    bool mFlushed;
    RefPtr<SharedShutdownPromiseHolder> mShutdownPromise;

    MozPromiseRequestHolder<MediaDataDecoder::DecodePromise> mDrainRequest;
    DrainState mDrainState;
    bool HasPendingDrain() const { return mDrainState != DrainState::None; }
    bool HasCompletedDrain() const {
      return mDrainState == DrainState::DrainCompleted ||
             mDrainState == DrainState::DrainAborted;
    }
    void RequestDrain() {
      MOZ_RELEASE_ASSERT(mDrainState == DrainState::None);
      mDrainState = DrainState::DrainRequested;
    }

    void StartRecordDecodingPerf(const TrackType aTrack,
                                 const MediaRawData* aSample);

    // Track decoding error and fail when we hit the limit.
    uint32_t mNumOfConsecutiveDecodingError;
    uint32_t mMaxConsecutiveDecodingError;

    // Track RDD or GPU process crashes and fail when we hit the limit.
    uint32_t mNumOfConsecutiveRDDOrGPUCrashes;
    uint32_t mMaxConsecutiveRDDOrGPUCrashes;

    // Track Utility process crashes and fail when we hit the limit.
    uint32_t mNumOfConsecutiveUtilityCrashes;
    uint32_t mMaxConsecutiveUtilityCrashes;

    // Set when we haven't yet decoded the first frame.
    // Cleared once the first frame has been decoded.
    // This is used to determine, upon error, if we should try again to decode
    // the frame, or skip to the next keyframe.
    Maybe<media::TimeUnit> mFirstFrameTime;

    Maybe<MediaResult> mError;
    bool HasFatalError() const {
      if (!mError.isSome()) {
        return false;
      }
      if (mError.ref() == NS_ERROR_DOM_MEDIA_DECODE_ERR) {
        // Allow decode errors to be non-fatal, but give up
        // if we have too many, or if warnings should be treated as errors.
        return mNumOfConsecutiveDecodingError > mMaxConsecutiveDecodingError ||
               StaticPrefs::media_playback_warnings_as_errors();
      }
      if (mError.ref() == NS_ERROR_DOM_MEDIA_NEED_NEW_DECODER) {
        // If the caller asked for a new decoder we shouldn't treat
        // it as fatal.
        return false;
      }
      if (mError.ref() ==
          NS_ERROR_DOM_MEDIA_REMOTE_DECODER_CRASHED_RDD_OR_GPU_ERR) {
        // Allow RDD crashes to be non-fatal, but give up
        // if we have too many, or if warnings should be treated as errors.
        return mNumOfConsecutiveRDDOrGPUCrashes >
                   mMaxConsecutiveRDDOrGPUCrashes ||
               StaticPrefs::media_playback_warnings_as_errors();
      }
      if (mError.ref() ==
          NS_ERROR_DOM_MEDIA_REMOTE_DECODER_CRASHED_UTILITY_ERR) {
        bool tooManyConsecutiveCrashes =
            mNumOfConsecutiveUtilityCrashes > mMaxConsecutiveUtilityCrashes;
        // TODO: Telemetry?
        return tooManyConsecutiveCrashes ||
               StaticPrefs::media_playback_warnings_as_errors();
      }
      if (mError.ref() ==
          NS_ERROR_DOM_MEDIA_REMOTE_DECODER_CRASHED_MF_CDM_ERR) {
        return false;
      }
      // All other error types are fatal
      return true;
    }

    // If set, all decoded samples prior mTimeThreshold will be dropped.
    // Used for internal seeking when a change of stream is detected or when
    // encountering data discontinuity.
    Maybe<InternalSeekTarget> mTimeThreshold;
    // Time of last decoded sample returned.
    Maybe<media::TimeInterval> mLastDecodedSampleTime;

    // Decoded samples returned my mDecoder awaiting being returned to
    // state machine upon request.
    nsTArray<RefPtr<MediaData>> mOutput;
    uint64_t mNumSamplesInput;
    uint64_t mNumSamplesOutput;
    uint64_t mNumSamplesOutputTotal;
    uint64_t mNumSamplesSkippedTotal;

    // These get overridden in the templated concrete class.
    // Indicate if we have a pending promise for decoded frame.
    // Rejecting the promise will stop the reader from decoding ahead.
    virtual bool HasPromise() const = 0;
    virtual void RejectPromise(const MediaResult& aError,
                               StaticString aMethodName) = 0;

    // Clear track demuxer related data.
    void ResetDemuxer() {
      mDemuxRequest.DisconnectIfExists();
      mSeekRequest.DisconnectIfExists();
      mTrackDemuxer->Reset();
      mQueuedSamples.Clear();
    }

    // Flush the decoder if present and reset decoding related data.
    // Following a flush, the decoder is ready to accept any new data.
    void Flush();

    bool CancelWaitingForKey() {
      if (!mWaitingForKey) {
        return false;
      }
      mWaitingForKey = false;
      if (IsWaitingForData() || !HasWaitingPromise()) {
        return false;
      }
      mWaitingPromise.Resolve(mType, __func__);
      return true;
    }

    // Reset the state of the DecoderData, clearing all queued frames
    // (pending demuxed and decoded).
    // The track demuxer is *not* reset.
    void ResetState() {
      MOZ_ASSERT(mOwner->OnTaskQueue());
      mDemuxEOS = false;
      mWaitingForDataStartTime.reset();
      mQueuedSamples.Clear();
      mDecodeRequest.DisconnectIfExists();
      mDrainRequest.DisconnectIfExists();
      mDrainState = DrainState::None;
      CancelWaitingForKey();
      mTimeThreshold.reset();
      mLastDecodedSampleTime.reset();
      mOutput.Clear();
      mNumSamplesInput = 0;
      mNumSamplesOutput = 0;
      mSizeOfQueue = 0;
      mNextStreamSourceID.reset();
      if (!HasFatalError()) {
        mError.reset();
      }
    }

    // Return whether an InternalSeek() has been requested but has not yet
    // seeked to the random access point preceding that target.
    bool HasInternalSeekPending() const {
      return mTimeThreshold && !mTimeThreshold.ref().mHasSeeked;
    }

    // Return the current TrackInfo in the stream. If the stream content never
    // changed since AsyncReadMetadata was called then the TrackInfo used is
    // mOriginalInfo, other it will be mInfo. The later case is only ever true
    // with MSE or the WebMDemuxer.
    const TrackInfo* GetCurrentInfo() const {
      if (mInfo) {
        return *mInfo;
      }
      return mOriginalInfo.get();
    }
    // Return the current TrackInfo updated as per the decoder output.
    // Typically for audio, the number of channels and/or sampling rate can vary
    // between what was found in the metadata and what the decoder returned.
    const TrackInfo* GetWorkingInfo() const { return mWorkingInfo.get(); }
    bool IsEncrypted() const { return GetCurrentInfo()->mCrypto.IsEncrypted(); }

    // Used by the MDSM for logging purposes.
    Atomic<size_t> mSizeOfQueue;
    // Used by the MDSM to determine if video decoding is hardware accelerated.
    // This value is updated after a frame is successfully decoded.
    Atomic<bool> mIsHardwareAccelerated;
    // Sample format monitoring.
    uint32_t mLastStreamSourceID;
    Maybe<uint32_t> mNextStreamSourceID;
    media::TimeIntervals mTimeRanges;
    Maybe<media::TimeUnit> mLastTimeRangesEnd;
    // TrackInfo as first discovered during ReadMetadata.
    UniquePtr<TrackInfo> mOriginalInfo;
    // Written exclusively on the TaskQueue, can be read on MDSM's TaskQueue.
    // Must be read with parent's mutex held.
    UniquePtr<TrackInfo> mWorkingInfo;
    RefPtr<TrackInfoSharedPtr> mInfo;
    Maybe<media::TimeUnit> mFirstDemuxedSampleTime;
    // Use NullDecoderModule or not.
    bool mIsNullDecode;
    bool mHardwareDecodingDisabled;
    // Whether we have reported hardware decoding support for video. Used only
    // on reader's task queue,
    bool mHasReportedVideoHardwareSupportTelemtry = false;

    class {
     public:
      float Mean() const { return mMean; }

      void Update(const media::TimeUnit& aValue) {
        if (aValue == media::TimeUnit::Zero()) {
          return;
        }
        mMean += static_cast<float>((1.0f / aValue.ToSeconds() - mMean) /
                                    static_cast<double>(++mCount));
      }

      void Reset() {
        mMean = 0;
        mCount = 0;
      }

     private:
      float mMean = 0;
      uint64_t mCount = 0;
    } mMeanRate;
  };

  template <typename Type>
  class DecoderDataWithPromise : public DecoderData {
   public:
    DecoderDataWithPromise(MediaFormatReader* aOwner, MediaData::Type aType,
                           uint32_t aNumOfMaxError)
        : DecoderData(aOwner, aType, aNumOfMaxError), mHasPromise(false) {
      DecoderDoctorLogger::LogConstructionAndBase(
          "MediaFormatReader::DecoderDataWithPromise", this,
          "MediaFormatReader::DecoderData",
          static_cast<const MediaFormatReader::DecoderData*>(this));
    }

    ~DecoderDataWithPromise() {
      DecoderDoctorLogger::LogDestruction(
          "MediaFormatReader::DecoderDataWithPromise", this);
    }

    bool HasPromise() const override { return mHasPromise; }

    RefPtr<DataPromise<Type>> EnsurePromise(StaticString aMethodName) {
      MOZ_ASSERT(mOwner->OnTaskQueue());
      mHasPromise = true;
      return mPromise.Ensure(aMethodName);
    }

    void ResolvePromise(Type* aData, StaticString aMethodName) {
      MOZ_ASSERT(mOwner->OnTaskQueue());
      mPromise.Resolve(aData, aMethodName);
      mHasPromise = false;
    }

    void RejectPromise(const MediaResult& aError,
                       StaticString aMethodName) override {
      MOZ_ASSERT(mOwner->OnTaskQueue());
      mPromise.Reject(aError, aMethodName);
      mHasPromise = false;
    }

   private:
    MozPromiseHolder<DataPromise<Type>> mPromise;
    Atomic<bool> mHasPromise;
  };

  // Decode task queue.
  RefPtr<TaskQueue> mTaskQueue;

  DecoderDataWithPromise<AudioData> mAudio;
  DecoderDataWithPromise<VideoData> mVideo;

  Watchable<bool> mWorkingInfoChanged;
  WatchManager<MediaFormatReader> mWatchManager;
  bool mIsWatchingWorkingInfo;

  // Returns true when the decoder for this track needs input.
  bool NeedInput(DecoderData& aDecoder);

  DecoderData& GetDecoderData(TrackType aTrack);

  // Demuxer objects.
  class DemuxerProxy;
  UniquePtr<DemuxerProxy> mDemuxer;
  bool mDemuxerInitDone;
  void OnDemuxerInitDone(const MediaResult& aResult);
  void OnDemuxerInitFailed(const MediaResult& aError);
  MozPromiseRequestHolder<MediaDataDemuxer::InitPromise> mDemuxerInitRequest;
  MozPromiseRequestHolder<NotifyDataArrivedPromise> mNotifyDataArrivedPromise;
  bool mPendingNotifyDataArrived;
  void OnDemuxFailed(TrackType aTrack, const MediaResult& aError);

  void DoDemuxVideo();
  void OnVideoDemuxCompleted(
      const RefPtr<MediaTrackDemuxer::SamplesHolder>& aSamples);
  void OnVideoDemuxFailed(const MediaResult& aError) {
    OnDemuxFailed(TrackType::kVideoTrack, aError);
  }

  void DoDemuxAudio();
  void OnAudioDemuxCompleted(
      const RefPtr<MediaTrackDemuxer::SamplesHolder>& aSamples);
  void OnAudioDemuxFailed(const MediaResult& aError) {
    OnDemuxFailed(TrackType::kAudioTrack, aError);
  }

  void SkipVideoDemuxToNextKeyFrame(media::TimeUnit aTimeThreshold);
  MozPromiseRequestHolder<MediaTrackDemuxer::SkipAccessPointPromise>
      mSkipRequest;
  void VideoSkipReset(uint32_t aSkipped);
  void OnVideoSkipCompleted(uint32_t aSkipped);
  void OnVideoSkipFailed(MediaTrackDemuxer::SkipFailureHolder aFailure);

  // The last number of decoded output frames that we've reported to
  // MediaDecoder::NotifyDecoded(). We diff the number of output video
  // frames every time that DecodeVideoData() is called, and report the
  // delta there.
  uint64_t mLastReportedNumDecodedFrames;

  // Timestamp of the previous decoded video keyframe, in microseconds.
  int64_t mPreviousDecodedKeyframeTime_us;
  // Default mLastDecodedKeyframeTime_us value, must be bigger than anything.
  static const int64_t sNoPreviousDecodedKeyframe = INT64_MAX;

  RefPtr<layers::KnowsCompositor> mKnowsCompositor;

  // Metadata objects
  // True if we've read the streams' metadata.
  bool mInitDone;
  MozPromiseHolder<MetadataPromise> mMetadataPromise;
  bool IsEncrypted() const;

  // Set to true if any of our track buffers may be blocking.
  bool mTrackDemuxersMayBlock;

  // Seeking objects.
  void SetSeekTarget(const SeekTarget& aTarget);
  bool IsSeeking() const { return mPendingSeekTime.isSome(); }
  bool IsVideoOnlySeeking() const {
    return IsSeeking() && mOriginalSeekTarget.IsVideoOnly();
  }
  bool IsAudioOnlySeeking() const {
    return IsSeeking() && mOriginalSeekTarget.IsAudioOnly();
  }
  void ScheduleSeek();
  void AttemptSeek();
  void OnSeekFailed(TrackType aTrack, const MediaResult& aError);
  void DoVideoSeek();
  void OnVideoSeekCompleted(media::TimeUnit aTime);
  void OnVideoSeekFailed(const MediaResult& aError);
  bool mSeekScheduled;

  void DoAudioSeek();
  void OnAudioSeekCompleted(media::TimeUnit aTime);
  void OnAudioSeekFailed(const MediaResult& aError);
  // The SeekTarget that was last given to Seek()
  SeekTarget mOriginalSeekTarget;
  // Temporary seek information while we wait for the data
  Maybe<media::TimeUnit> mFallbackSeekTime;
  Maybe<media::TimeUnit> mPendingSeekTime;
  MozPromiseHolder<SeekPromise> mSeekPromise;

  RefPtr<VideoFrameContainer> mVideoFrameContainer;
  layers::ImageContainer* GetImageContainer();

  RefPtr<CDMProxy> mCDMProxy;

  RefPtr<GMPCrashHelper> mCrashHelper;

  void SetNullDecode(TrackType aTrack, bool aIsNullDecode);

  class DecoderFactory;
  UniquePtr<DecoderFactory> mDecoderFactory;

  class ShutdownPromisePool;
  UniquePtr<ShutdownPromisePool> mShutdownPromisePool;

  MediaEventListener mOnTrackWaitingForKeyListener;

  void OnFirstDemuxCompleted(
      TrackInfo::TrackType aType,
      const RefPtr<MediaTrackDemuxer::SamplesHolder>& aSamples);

  void OnFirstDemuxFailed(TrackInfo::TrackType aType,
                          const MediaResult& aError);

  void MaybeResolveMetadataPromise();

  // Stores presentation info required for playback.
  MediaInfo mInfo;

  UniquePtr<MetadataTags> mTags;

  // A flag indicating if the start time is known or not.
  bool mHasStartTime = false;

  void ShutdownDecoder(TrackType aTrack);
  RefPtr<ShutdownPromise> TearDownDecoders();

  bool mShutdown = false;

  // Buffered range.
  Canonical<media::TimeIntervals> mBuffered;

  // Used to send TimedMetadata to the listener.
  TimedMetadataEventProducer mTimedMetadataEvent;

  // Notify if this media is not seekable.
  MediaEventProducer<void> mOnMediaNotSeekable;

  // Notify if we are waiting for a decryption key.
  MediaEventProducer<TrackInfo::TrackType> mOnTrackWaitingForKey;

  MediaEventProducer<nsTArray<uint8_t>, nsString> mOnEncrypted;

  MediaEventProducer<void> mOnWaitingForKey;

  MediaEventProducer<MediaResult> mOnDecodeWarning;

  MediaEventProducer<VideoInfo, AudioInfo> mTrackInfoUpdatedEvent;

  RefPtr<FrameStatistics> mFrameStats;

  // Used in bug 1393399 for telemetry.
  const MediaDecoderOwnerID mMediaDecoderOwnerID;

  bool ResolveSetCDMPromiseIfDone(TrackType aTrack);
  void PrepareToSetCDMForTrack(TrackType aTrack);
  MozPromiseHolder<SetCDMPromise> mSetCDMPromise;
  TrackSet mSetCDMForTracks{};
  bool IsDecoderWaitingForCDM(TrackType aTrack);

  void GetDebugInfo(dom::MediaFormatReaderDebugInfo& aInfo);

  // Only be used on Windows when the media engine playback is enabled.
  Maybe<uint64_t> mMediaEngineId;

  const Maybe<TrackingId> mTrackingId;

  // The start time of reading the metdata and how long does it take. This
  // measurement includes the time of downloading media resource over the
  // internet.
  Maybe<TimeStamp> mReadMetadataStartTime;
  TimeDuration mReadMetaDataTime;

  // The total amount of time we have been waiting for the video data due to
  // lacking of data.
  TimeDuration mTotalWaitingForVideoDataTime;

  // https://github.com/w3c/encrypted-media/issues/251#issuecomment-819783073
  // Treat playback as encrypted if the media key is set before playback starts,
  // this allows websites to start with non-encrypted stream and switch to
  // encrypted stream later.
  Atomic<bool> mEncryptedCustomIdent;
};

}  // namespace mozilla

#endif
