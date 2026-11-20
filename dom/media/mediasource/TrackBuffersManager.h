/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_TRACKBUFFERSMANAGER_H_
#define MOZILLA_TRACKBUFFERSMANAGER_H_

#include "MediaContainerType.h"
#include "MediaData.h"
#include "MediaDataDemuxer.h"
#include "MediaResult.h"
#include "MediaSourceDecoder.h"
#include "MediaSpan.h"
#include "SourceBufferTask.h"
#include "TimeUnits.h"
#include "mozilla/Atomics.h"
#include "mozilla/EventTargetCapability.h"
#include "mozilla/Maybe.h"
#include "mozilla/Mutex.h"
#include "mozilla/NotNull.h"
#include "mozilla/TaskQueue.h"
#include "mozilla/dom/MediaDebugInfoBinding.h"
#include "nsTArray.h"

namespace mozilla {

class AbstractThread;
class ContainerParser;
class MediaByteBuffer;
class MediaRawData;
class MediaSourceDemuxer;
class SourceBufferResource;

namespace dom {
enum class MediaSourceEndOfStreamError : uint8_t;
}  // namespace dom

class SourceBufferTaskQueue {
 public:
  SourceBufferTaskQueue() = default;

  ~SourceBufferTaskQueue() {
    MOZ_ASSERT(mQueue.IsEmpty(), "All tasks must have been processed");
  }

  void Push(SourceBufferTask* aTask) { mQueue.AppendElement(aTask); }

  already_AddRefed<SourceBufferTask> Pop() {
    if (!mQueue.Length()) {
      return nullptr;
    }
    RefPtr<SourceBufferTask> task = std::move(mQueue[0]);
    mQueue.RemoveElementAt(0);
    return task.forget();
  }

  nsTArray<RefPtr<SourceBufferTask>>::size_type Length() const {
    return mQueue.Length();
  }

 private:
  nsTArray<RefPtr<SourceBufferTask>> mQueue;
};

DDLoggedTypeDeclName(TrackBuffersManager);

class TrackBuffersManager final
    : public DecoderDoctorLifeLogger<TrackBuffersManager> {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(TrackBuffersManager);

  enum class EvictDataResult : int8_t {
    NO_DATA_EVICTED,
    CANT_EVICT,
    BUFFER_FULL,
  };

  using TrackType = TrackInfo::TrackType;
  using MediaType = MediaData::Type;
  using TrackBuffer = nsTArray<RefPtr<MediaRawData>>;
  using AppendPromise = SourceBufferTask::AppendPromise;
  using RangeRemovalPromise = SourceBufferTask::RangeRemovalPromise;

  // Interface for SourceBuffer
  TrackBuffersManager(MediaSourceDecoder* aParentDecoder,
                      const MediaContainerType& aType);

  // Queue a task to add data to the end of the input buffer and run the MSE
  // Buffer Append Algorithm
  // 3.5.5 Buffer Append Algorithm.
  // http://w3c.github.io/media-source/index.html#sourcebuffer-buffer-append
  RefPtr<AppendPromise> AppendData(already_AddRefed<MediaByteBuffer> aData,
                                   const SourceBufferAttributes& aAttributes);

  // Queue a task to abort any pending AppendData.
  // Does nothing at this stage.
  void AbortAppendData();

  // Queue a task to run MSE Reset Parser State Algorithm.
  // 3.5.2 Reset Parser State
  void ResetParserState(SourceBufferAttributes& aAttributes);

  // Queue a task to run the MSE range removal algorithm.
  // http://w3c.github.io/media-source/#sourcebuffer-coded-frame-removal
  RefPtr<RangeRemovalPromise> RangeRemoval(media::TimeUnit aStart,
                                           media::TimeUnit aEnd);

  // Schedule data eviction if necessary as the next call to AppendData will
  // add aSize bytes.
  // Eviction is done in two steps, first remove data up to aPlaybackTime
  // and if still more space is needed remove from the end.
  EvictDataResult EvictData(const media::TimeUnit& aPlaybackTime, int64_t aSize,
                            TrackType aType);

  // Schedule data eviction if necessary and the size of eviction would be
  // determined automatically. This is currently used when the buffer's size is
  // close to full and the normal procedure to evict data is not enough.
  void EvictDataWithoutSize(TrackType aType, const media::TimeUnit& aTarget);

  // Queue a task to run ChangeType
  void ChangeType(const MediaContainerType& aType);

  // Returns the buffered range currently managed.
  // This may be called on any thread.
  // Buffered must conform to
  // http://w3c.github.io/media-source/index.html#widl-SourceBuffer-buffered
  media::TimeIntervals Buffered() const;
  media::TimeUnit HighestStartTime() const;
  media::TimeUnit HighestEndTime() const;

  // Return the size of the data managed by this SourceBufferContentManager.
  int64_t GetSize() const;

  // Indicate that the MediaSource parent object got into "ended" state.
  void SetEnded(const dom::Optional<dom::MediaSourceEndOfStreamError>& aError);

  // The parent SourceBuffer is about to be destroyed.
  void Detach();

  // Return the eviction threshold, in bytes, for a track type (audio or video).
  // When the track type isn't passed in (kUndefinedTrack), this returns the
  // value for video if a video track is present. Specifying the track type
  // explicitely is useful when initialization hasn't finished, but the track
  // type is known already.
  int64_t EvictionThreshold(
      TrackInfo::TrackType aType = TrackInfo::TrackType::kUndefinedTrack) const;

  // Interface for MediaSourceDemuxer
  MediaInfo GetMetadata() const;
  const TrackBuffer& GetTrackBuffer(TrackInfo::TrackType aTrack) const;
  const media::TimeIntervals& Buffered(TrackInfo::TrackType) const;
  const media::TimeUnit& HighestStartTime(TrackInfo::TrackType) const;
  media::TimeIntervals SafeBuffered(TrackInfo::TrackType) const;
  bool HaveAllData() const { return mHaveAllData; }
  uint32_t Evictable(TrackInfo::TrackType aTrack) const;
  media::TimeUnit Seek(TrackInfo::TrackType aTrack,
                       const media::TimeUnit& aTime,
                       const media::TimeUnit& aFuzz);
  uint32_t SkipToNextRandomAccessPoint(TrackInfo::TrackType aTrack,
                                       const media::TimeUnit& aTimeThreadshold,
                                       const media::TimeUnit& aFuzz,
                                       bool& aFound);

  already_AddRefed<MediaRawData> GetSample(TrackInfo::TrackType aTrack,
                                           const media::TimeUnit& aFuzz,
                                           MediaResult& aResult);
  int32_t FindCurrentPosition(TrackInfo::TrackType aTrack,
                              const media::TimeUnit& aFuzz) const
      MOZ_REQUIRES(mTaskQueueCapability);

  // Will set the next GetSample index if needed. This information is determined
  // through the value of mNextSampleTimecode. Return false if the index
  // couldn't be determined or if there's nothing more that could be demuxed.
  // This occurs if either the track buffer doesn't contain the required
  // timecode or is empty.
  nsresult SetNextGetSampleIndexIfNeeded(TrackInfo::TrackType aTrack,
                                         const media::TimeUnit& aFuzz)
      MOZ_REQUIRES(mTaskQueueCapability);

  media::TimeUnit GetNextRandomAccessPoint(TrackInfo::TrackType aTrack,
                                           const media::TimeUnit& aFuzz);

  // Requests that the TrackBuffersManager populates aInfo with debug
  // information. This may be done asynchronously, and aInfo should *not* be
  // accessed by the caller until the returned promise is resolved or rejected.
  RefPtr<GenericPromise> RequestDebugInfo(
      dom::TrackBuffersManagerDebugInfo& aInfo) const;
  void AddSizeOfResources(MediaSourceDecoder::ResourceSizes* aSizes) const;

 private:
  using CodedFrameProcessingPromise = MozPromise<bool, MediaResult, true>;

  ~TrackBuffersManager();
  // main thread:
  void Reopen();

  // All following functions run on the taskqueue.
  RefPtr<AppendPromise> DoAppendData(already_AddRefed<MediaByteBuffer> aData,
                                     const SourceBufferAttributes& aAttributes);
  void ScheduleSegmentParserLoop() MOZ_REQUIRES(mTaskQueueCapability);
  void SegmentParserLoop() MOZ_REQUIRES(mTaskQueueCapability);
  void InitializationSegmentReceived() MOZ_REQUIRES(mTaskQueueCapability);
  void ShutdownDemuxers() MOZ_REQUIRES(mTaskQueueCapability);
  void CreateDemuxerforMIMEType() MOZ_REQUIRES(mTaskQueueCapability);
  void ResetDemuxingState() MOZ_REQUIRES(mTaskQueueCapability);
  void NeedMoreData() MOZ_REQUIRES(mTaskQueueCapability);
  void RejectAppend(const MediaResult& aRejectValue, const char* aName)
      MOZ_REQUIRES(mTaskQueueCapability);
  // Will return a promise that will be resolved once all frames of the current
  // media segment have been processed.
  RefPtr<CodedFrameProcessingPromise> CodedFrameProcessing()
      MOZ_REQUIRES(mTaskQueueCapability);
  void CompleteCodedFrameProcessing() MOZ_REQUIRES(mTaskQueueCapability);
  // Called by ResetParserState.
  void CompleteResetParserState() MOZ_REQUIRES(mTaskQueueCapability);
  RefPtr<RangeRemovalPromise> CodedFrameRemovalWithPromise(
      const media::TimeInterval& aInterval) MOZ_REQUIRES(mTaskQueueCapability);
  bool CodedFrameRemoval(const media::TimeInterval& aInterval)
      MOZ_REQUIRES(mTaskQueueCapability);
  // Removes all coded frames -- this is not to spec and should be used as a
  // last resort to clear buffers only if other methods cannot.
  void RemoveAllCodedFrames() MOZ_REQUIRES(mTaskQueueCapability);
  void SetAppendState(SourceBufferAttributes::AppendState aAppendState)
      MOZ_REQUIRES(mTaskQueueCapability);

  bool HasVideo() const { return mVideoTracks.mNumTracks > 0; }
  bool HasAudio() const { return mAudioTracks.mNumTracks > 0; }

  // The input buffer as per
  // http://w3c.github.io/media-source/index.html#sourcebuffer-input-buffer
  Maybe<MediaSpan> mInputBuffer MOZ_GUARDED_BY(mTaskQueueCapability);
  // Buffer full flag as per
  // https://w3c.github.io/media-source/#sourcebuffer-buffer-full-flag. Accessed
  // on both the main thread and the task queue.
  Atomic<bool> mBufferFull;
  bool mFirstInitializationSegmentReceived MOZ_GUARDED_BY(mTaskQueueCapability);
  bool mChangeTypeReceived MOZ_GUARDED_BY(mTaskQueueCapability);
  // Set to true once a new segment is started.
  bool mNewMediaSegmentStarted MOZ_GUARDED_BY(mTaskQueueCapability);
  bool mActiveTrack MOZ_GUARDED_BY(mTaskQueueCapability);
  MediaContainerType mType MOZ_GUARDED_BY(mTaskQueueCapability);

  // ContainerParser objects and methods.
  // Those are used to parse the incoming input buffer.

  // Recreate the ContainerParser and if aReuseInitData is true then
  // feed it with the previous init segment found.
  void RecreateParser(bool aReuseInitData) MOZ_REQUIRES(mTaskQueueCapability);
  UniquePtr<ContainerParser> mParser;

  // Demuxer objects and methods.
  void AppendDataToCurrentInputBuffer(const MediaSpan& aData)
      MOZ_REQUIRES(mTaskQueueCapability);

  RefPtr<MediaByteBuffer> mInitData MOZ_GUARDED_BY(mTaskQueueCapability);

  // Checks if a new set of init data is a repeat of the last set of init data
  // received. Because streams may retransmit the same init data (or
  // functionally equivalent init data) we do not want to perform costly
  // operations each time we receive init data, only when it's actually
  // different data.
  bool IsRepeatInitData(const MediaInfo& aNewMediaInfo) const
      MOZ_REQUIRES(mTaskQueueCapability);

  // Temporary input buffer to handle partial media segment header.
  // We store the current input buffer content into it should we need to
  // reinitialize the demuxer once we have some samples and a discontinuity is
  // detected.
  Maybe<MediaSpan> mPendingInputBuffer MOZ_GUARDED_BY(mTaskQueueCapability);
  RefPtr<SourceBufferResource> mCurrentInputBuffer
      MOZ_GUARDED_BY(mTaskQueueCapability);
  RefPtr<MediaDataDemuxer> mInputDemuxer MOZ_GUARDED_BY(mTaskQueueCapability);
  // Length already processed in current media segment.
  uint64_t mProcessedInput MOZ_GUARDED_BY(mTaskQueueCapability);
  Maybe<media::TimeUnit> mLastParsedEndTime
      MOZ_GUARDED_BY(mTaskQueueCapability);

  void OnDemuxerInitDone(const MediaResult& aResult);
  void OnDemuxerInitFailed(const MediaResult& aError);
  void OnDemuxerResetDone(const MediaResult& aResult)
      MOZ_REQUIRES(mTaskQueueCapability);
  MozPromiseRequestHolder<MediaDataDemuxer::InitPromise> mDemuxerInitRequest;

  void OnDemuxFailed(TrackType aTrack, const MediaResult& aError)
      MOZ_REQUIRES(mTaskQueueCapability);
  void DoDemuxVideo() MOZ_REQUIRES(mTaskQueueCapability);
  void OnVideoDemuxCompleted(
      const RefPtr<MediaTrackDemuxer::SamplesHolder>& aSamples);
  void OnVideoDemuxFailed(const MediaResult& aError) {
    mVideoTracks.mDemuxRequest.Complete();
    mTaskQueueCapability->AssertOnCurrentThread();
    OnDemuxFailed(TrackType::kVideoTrack, aError);
  }
  void DoDemuxAudio() MOZ_REQUIRES(mTaskQueueCapability);
  void OnAudioDemuxCompleted(
      const RefPtr<MediaTrackDemuxer::SamplesHolder>& aSamples);
  void OnAudioDemuxFailed(const MediaResult& aError) {
    mAudioTracks.mDemuxRequest.Complete();
    mTaskQueueCapability->AssertOnCurrentThread();
    OnDemuxFailed(TrackType::kAudioTrack, aError);
  }

  // Dispatches an "encrypted" event is any sample in array has initData
  // present.
  void MaybeDispatchEncryptedEvent(
      const nsTArray<RefPtr<MediaRawData>>& aSamples);

  void DoEvictData(const media::TimeUnit& aPlaybackTime,
                   Maybe<int64_t> aSizeToEvict)
      MOZ_REQUIRES(mTaskQueueCapability);

  void GetDebugInfo(dom::TrackBuffersManagerDebugInfo& aInfo) const
      MOZ_REQUIRES(mTaskQueueCapability);

  struct TrackData {
    TrackData() : mNumTracks(0), mNeedRandomAccessPoint(true), mSizeBuffer(0) {}
    Atomic<uint32_t> mNumTracks;
    // Definition of variables:
    // https://w3c.github.io/media-source/#track-buffers
    // Last decode timestamp variable that stores the decode timestamp of the
    // last coded frame appended in the current coded frame group.
    // The variable is initially unset to indicate that no coded frames have
    // been appended yet.
    Maybe<media::TimeUnit> mLastDecodeTimestamp;
    // Last frame duration variable that stores the coded frame duration of the
    // last coded frame appended in the current coded frame group.
    // The variable is initially unset to indicate that no coded frames have
    // been appended yet.
    Maybe<media::TimeUnit> mLastFrameDuration;
    // Highest end timestamp variable that stores the highest coded frame end
    // timestamp across all coded frames in the current coded frame group that
    // were appended to this track buffer.
    // The variable is initially unset to indicate that no coded frames have
    // been appended yet.
    Maybe<media::TimeUnit> mHighestEndTimestamp;
    // Highest presentation timestamp in track buffer.
    // Protected by global monitor, except when reading on the task queue as it
    // is only written there.
    media::TimeUnit mHighestStartTimestamp;
    // Longest frame duration seen since last random access point.
    // Only ever accessed when mLastDecodeTimestamp and mLastFrameDuration are
    // set.
    media::TimeUnit mLongestFrameDuration;
    // Need random access point flag variable that keeps track of whether the
    // track buffer is waiting for a random access point coded frame.
    // The variable is initially set to true to indicate that random access
    // point coded frame is needed before anything can be added to the track
    // buffer.
    bool mNeedRandomAccessPoint;
    RefPtr<MediaTrackDemuxer> mDemuxer;
    MozPromiseRequestHolder<MediaTrackDemuxer::SamplesPromise> mDemuxRequest;
    // Highest end timestamp of the last media segment demuxed.
    media::TimeUnit mLastParsedEndTime;

    // If set, position where the next contiguous frame will be inserted.
    // If a discontinuity is detected, it will be unset and recalculated upon
    // the next insertion.
    Maybe<uint32_t> mNextInsertionIndex;
    // Samples just demuxed, but not yet parsed.
    TrackBuffer mQueuedSamples;
    const TrackBuffer& GetTrackBuffer() const {
      MOZ_RELEASE_ASSERT(mBuffers.Length(),
                         "TrackBuffer must have been created");
      return mBuffers.LastElement();
    }
    TrackBuffer& GetTrackBuffer() {
      MOZ_RELEASE_ASSERT(mBuffers.Length(),
                         "TrackBuffer must have been created");
      return mBuffers.LastElement();
    }
    // We only manage a single track of each type at this time.
    nsTArray<TrackBuffer> mBuffers;
    // Track buffer ranges variable that represents the presentation time ranges
    // occupied by the coded frames currently stored in the track buffer.
    media::TimeIntervals mBufferedRanges;
    // Sanitized mBufferedRanges with a fuzz of half a sample's duration applied
    // This buffered ranges is the basis of what is exposed to the JS.
    media::TimeIntervals mSanitizedBufferedRanges;
    // Byte size of all samples contained in this track buffer.
    uint32_t mSizeBuffer;
    // TrackInfo of the first metadata received.
    RefPtr<TrackInfoSharedPtr> mInfo;
    // TrackInfo of the last metadata parsed (updated with each init segment.
    RefPtr<TrackInfoSharedPtr> mLastInfo;

    // If set, position of the next sample to be retrieved by GetSample().
    // If the position is equal to the TrackBuffer's length, it indicates that
    // we've reached EOS.
    Maybe<uint32_t> mNextGetSampleIndex;
    // Approximation of the next sample's decode timestamp.
    media::TimeUnit mNextSampleTimecode;
    // Approximation of the next sample's presentation timestamp.
    media::TimeUnit mNextSampleTime;

    struct EvictionIndex {
      EvictionIndex() { Reset(); }
      void Reset() {
        mEvictable = 0;
        mLastIndex = 0;
      }
      uint32_t mEvictable = 0;
      uint32_t mLastIndex = 0;
    };
    // Size of data that can be safely evicted during the next eviction
    // cycle.
    // We consider as evictable all frames up to the last keyframe prior to
    // mNextGetSampleIndex. If mNextGetSampleIndex isn't set, then we assume
    // that we can't yet evict data.
    // Protected by global monitor, except when reading on the task queue as it
    // is only written there.
    EvictionIndex mEvictionIndex;

    void ResetAppendState() {
      mLastDecodeTimestamp.reset();
      mLastFrameDuration.reset();
      mHighestEndTimestamp.reset();
      mNeedRandomAccessPoint = true;
      mNextInsertionIndex.reset();
    }

    void Reset() {
      ResetAppendState();
      mEvictionIndex.Reset();
      for (auto& buffer : mBuffers) {
        buffer.Clear();
      }
      mSizeBuffer = 0;
      mNextGetSampleIndex.reset();
      mBufferedRanges.Clear();
      mSanitizedBufferedRanges.Clear();
    }

    void AddSizeOfResources(MediaSourceDecoder::ResourceSizes* aSizes) const;
  };

  void CheckSequenceDiscontinuity(const media::TimeUnit& aPresentationTime)
      MOZ_REQUIRES(mTaskQueueCapability);
  void ProcessFrames(TrackBuffer& aSamples, TrackData& aTrackData)
      MOZ_REQUIRES(mTaskQueueCapability);
  media::TimeInterval PresentationInterval(const TrackBuffer& aSamples) const
      MOZ_REQUIRES(mTaskQueueCapability);
  bool CheckNextInsertionIndex(TrackData& aTrackData,
                               const media::TimeUnit& aSampleTime)
      MOZ_REQUIRES(mTaskQueueCapability);
  void InsertFrames(TrackBuffer& aSamples,
                    const media::TimeIntervals& aIntervals,
                    TrackData& aTrackData) MOZ_REQUIRES(mTaskQueueCapability);
  // Sets the end of each video frame at a link between a buffered frame and
  // one of the aCount frames inserted at aStart to the start of the next frame
  // in presentation order: it extends the frame, or cuts back an end that came
  // from a later buffered frame. Adds the extended time to the buffered ranges.
  void EndFramesAtNextPresentation(TrackData& aTrackData, uint32_t aStart,
                                   uint32_t aCount)
      MOZ_REQUIRES(mTaskQueueCapability);
  void UpdateHighestTimestamp(TrackData& aTrackData,
                              const media::TimeUnit& aHighestTime)
      MOZ_REQUIRES(mTaskQueueCapability);
  // Remove all frames and their dependencies contained in aIntervals.
  // Return the index at which frames were first removed or 0 if no frames
  // removed.
  enum class RemovalMode {
    kRemoveFrame,
    kTruncateFrame,
  };
  uint32_t RemoveFrames(const media::TimeIntervals& aIntervals,
                        TrackData& aTrackData, uint32_t aStartIndex,
                        RemovalMode aMode);
  // Recalculate track's evictable amount.
  void ResetEvictionIndex(TrackData& aTrackData);
  void UpdateEvictionIndex(TrackData& aTrackData, uint32_t aCurrentIndex);
  // Find index of sample. Return a negative value if not found.
  uint32_t FindSampleIndex(const TrackBuffer& aTrackBuffer,
                           const media::TimeInterval& aInterval);
  const MediaRawData* GetSample(TrackInfo::TrackType aTrack, uint32_t aIndex,
                                const media::TimeUnit& aExpectedDts,
                                const media::TimeUnit& aExpectedPts,
                                const media::TimeUnit& aFuzz);
  void UpdateBufferedRanges();
  void RejectProcessing(const MediaResult& aRejectValue, const char* aName);
  void ResolveProcessing(bool aResolveValue, const char* aName);
  MozPromiseRequestHolder<CodedFrameProcessingPromise> mProcessingRequest;
  MozPromiseHolder<CodedFrameProcessingPromise> mProcessingPromise;

  // Trackbuffers definition.
  nsTArray<const TrackData*> GetTracksList() const;
  nsTArray<TrackData*> GetTracksList();
  TrackData& GetTracksData(TrackType aTrack) {
    switch (aTrack) {
      case TrackType::kVideoTrack:
        return mVideoTracks;
      case TrackType::kAudioTrack:
      default:
        return mAudioTracks;
    }
  }
  const TrackData& GetTracksData(TrackType aTrack) const {
    switch (aTrack) {
      case TrackType::kVideoTrack:
        return mVideoTracks;
      case TrackType::kAudioTrack:
      default:
        return mAudioTracks;
    }
  }
  TrackData mVideoTracks;
  TrackData mAudioTracks;

  // TaskQueue methods and objects.
  RefPtr<TaskQueue> GetTaskQueueSafe() const {
    MutexAutoLock mut(mMutex);
    return mTaskQueue;
  }
  NotNull<AbstractThread*> TaskQueueFromTaskQueue() const {
#ifdef DEBUG
    RefPtr<TaskQueue> taskQueue = GetTaskQueueSafe();
    MOZ_ASSERT(taskQueue && taskQueue->IsCurrentThreadIn());
#endif
    return WrapNotNull(mTaskQueue.get());
  }
  bool OnTaskQueue() const {
    auto taskQueue = TaskQueueFromTaskQueue();
    return taskQueue->IsCurrentThreadIn();
  }
  void ResetTaskQueue() {
    MutexAutoLock mut(mMutex);
    mTaskQueue = nullptr;
  }

  // SourceBuffer Queues and running context.
  SourceBufferTaskQueue mQueue;
  void QueueTask(SourceBufferTask* aTask);
  void ProcessTasks();
  // Set if the TrackBuffersManager is currently processing a task.
  // At this stage, this task is always a AppendBufferTask.
  RefPtr<SourceBufferTask> mCurrentTask MOZ_GUARDED_BY(mTaskQueueCapability);
  // Current SourceBuffer state for ongoing task.
  // Its content is returned to the SourceBuffer once the AppendBufferTask has
  // completed.
  UniquePtr<SourceBufferAttributes> mSourceBufferAttributes
      MOZ_GUARDED_BY(mTaskQueueCapability);
  // The current sourcebuffer append window. It's content is equivalent to
  // mSourceBufferAttributes.mAppendWindowStart/End
  media::Interval<double> mAppendWindow MOZ_GUARDED_BY(mTaskQueueCapability);

  // Strong references to external objects.
  nsMainThreadPtrHandle<MediaSourceDecoder> mParentDecoder;

  const RefPtr<AbstractThread> mAbstractMainThread;

  // Return public highest end time across all aTracks.
  // Monitor must be held.
  media::TimeUnit HighestEndTime(
      nsTArray<const media::TimeIntervals*>& aTracks) const;

  // true if endOfStream() has been called without error.
  Atomic<bool> mHaveAllData{false};

  // Global size of this source buffer content.
  Atomic<int64_t> mSizeSourceBuffer;
  const int64_t mVideoEvictionThreshold;
  const int64_t mAudioEvictionThreshold;
  // A ratio of buffer fullness that we use for the auto eviction,
  const double mEvictionBufferWatermarkRatio;
  enum class EvictionState {
    NO_EVICTION_NEEDED,
    EVICTION_NEEDED,
    EVICTION_COMPLETED,
  };
  Atomic<EvictionState> mEvictionState;

  // Monitor to protect following objects accessed across multiple threads.
  mutable Mutex mMutex MOZ_UNANNOTATED;
  // mTaskQueue is only ever written after construction on the task queue.
  // As such, it can be accessed while on task queue without the need for the
  // mutex.
  RefPtr<TaskQueue> mTaskQueue;
  // Stable audio and video track time ranges.
  media::TimeIntervals mVideoBufferedRanges;
  media::TimeIntervals mAudioBufferedRanges;
  // MediaInfo of the first init segment read.
  MediaInfo mInfo;
  // Set to true if MediaSource readyState has changed to ended.
  bool mEnded MOZ_GUARDED_BY(mMutex) = false;
  // End mutex protected members.

  // EventTargetCapability used to ensure we're running on the task queue
  // as expected for various accesses.
  // TODO: we could store only this and dispatch to it, rather than also having
  // mTaskQueue. However, there's special locking around mTaskQueue, so we keep
  // both for now.
  Maybe<EventTargetCapability<TaskQueue>> mTaskQueueCapability;

  Maybe<media::TimeUnit> mFrameEndTimeBeforeRecreateDemuxer;
};

}  // namespace mozilla

#endif /* MOZILLA_TRACKBUFFERSMANAGER_H_ */
