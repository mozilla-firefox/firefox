/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TrackBuffersManager.h"

#include "ContainerParser.h"
#include "MP4Demuxer.h"
#include "MPEGAudioByteStreamDemuxer.h"
#include "MediaInfo.h"
#include "MediaSourceDemuxer.h"
#include "MediaSourceUtils.h"
#include "SourceBuffer.h"
#include "SourceBufferResource.h"
#include "SourceBufferTask.h"
#include "WebMDemuxer.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/Preferences.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/StaticPrefs_media.h"
#include "nsMimeTypes.h"

extern mozilla::LogModule* GetMediaSourceLog();

#define MSE_DEBUG(arg, ...)                                                  \
  DDMOZ_LOG_FMT(GetMediaSourceLog(), mozilla::LogLevel::Debug, "::{}: " arg, \
                __func__, ##__VA_ARGS__)
#define MSE_DEBUGV(arg, ...)                                                   \
  DDMOZ_LOG_FMT(GetMediaSourceLog(), mozilla::LogLevel::Verbose, "::{}: " arg, \
                __func__, ##__VA_ARGS__)

mozilla::LogModule* GetMediaSourceSamplesLog() {
  static mozilla::LazyLogModule sLogModule("MediaSourceSamples");
  return sLogModule;
}
#define SAMPLE_DEBUG(arg, ...)                                        \
  DDMOZ_LOG_FMT(GetMediaSourceSamplesLog(), mozilla::LogLevel::Debug, \
                "::{}: " arg, __func__, ##__VA_ARGS__)
#define SAMPLE_DEBUGV(arg, ...)                                         \
  DDMOZ_LOG_FMT(GetMediaSourceSamplesLog(), mozilla::LogLevel::Verbose, \
                "::{}: " arg, __func__, ##__VA_ARGS__)

namespace mozilla {

using dom::SourceBufferAppendMode;
using media::Interval;
using media::TimeInterval;
using media::TimeIntervals;
using media::TimeUnit;
using AppendBufferResult = SourceBufferTask::AppendBufferResult;
using AppendState = SourceBufferAttributes::AppendState;

static Atomic<uint32_t> sStreamSourceID(0u);

class DispatchKeyNeededEvent : public Runnable {
 public:
  DispatchKeyNeededEvent(MediaSourceDecoder* aDecoder,
                         const nsTArray<uint8_t>& aInitData,
                         const nsString& aInitDataType)
      : Runnable("DispatchKeyNeededEvent"),
        mDecoder(aDecoder),
        mInitData(aInitData.Clone()),
        mInitDataType(aInitDataType) {}
  NS_IMETHOD Run() override {
    // Note: Null check the owner, as the decoder could have been shutdown
    // since this event was dispatched.
    MediaDecoderOwner* owner = mDecoder->GetOwner();
    if (owner) {
      owner->DispatchEncrypted(mInitData, mInitDataType);
    }
    mDecoder = nullptr;
    return NS_OK;
  }

 private:
  RefPtr<MediaSourceDecoder> mDecoder;
  nsTArray<uint8_t> mInitData;
  nsString mInitDataType;
};

TrackBuffersManager::TrackBuffersManager(MediaSourceDecoder* aParentDecoder,
                                         const MediaContainerType& aType)
    : mBufferFull(false),
      mFirstInitializationSegmentReceived(false),
      mChangeTypeReceived(false),
      mNewMediaSegmentStarted(false),
      mActiveTrack(false),
      mType(aType),
      mParser(ContainerParser::CreateForMIMEType(aType)),
      mProcessedInput(0),
      mParentDecoder(new nsMainThreadPtrHolder<MediaSourceDecoder>(
          "TrackBuffersManager::mParentDecoder", aParentDecoder,
          false /* strict */)),
      mAbstractMainThread(aParentDecoder->AbstractMainThread()),
      mVideoEvictionThreshold(Preferences::GetUint(
          "media.mediasource.eviction_threshold.video", 150 * 1024 * 1024)),
      mAudioEvictionThreshold(Preferences::GetUint(
          "media.mediasource.eviction_threshold.audio", 20 * 1024 * 1024)),
      mEvictionBufferWatermarkRatio(0.9),
      mEvictionState(EvictionState::NO_EVICTION_NEEDED),
      mMutex("TrackBuffersManager"),
      mTaskQueue(aParentDecoder->GetDemuxer()->GetTaskQueue()),
      mTaskQueueCapability(Some(EventTargetCapability{mTaskQueue.get()})) {
  MOZ_ASSERT(NS_IsMainThread(), "Must be instanciated on the main thread");
  DDLINKCHILD("parser", mParser.get());
}

TrackBuffersManager::~TrackBuffersManager() { ShutdownDemuxers(); }

RefPtr<TrackBuffersManager::AppendPromise> TrackBuffersManager::AppendData(
    already_AddRefed<MediaByteBuffer> aData,
    const SourceBufferAttributes& aAttributes) {
  MOZ_ASSERT(NS_IsMainThread());
  RefPtr<MediaByteBuffer> data(aData);
  MSE_DEBUG("Appending {} bytes", data->Length());

  Reopen();

  return InvokeAsync(static_cast<AbstractThread*>(GetTaskQueueSafe().get()),
                     this, __func__, &TrackBuffersManager::DoAppendData,
                     data.forget(), aAttributes);
}

RefPtr<TrackBuffersManager::AppendPromise> TrackBuffersManager::DoAppendData(
    already_AddRefed<MediaByteBuffer> aData,
    const SourceBufferAttributes& aAttributes) {
  RefPtr<AppendBufferTask> task =
      new AppendBufferTask(std::move(aData), aAttributes);
  RefPtr<AppendPromise> p = task->mPromise.Ensure(__func__);
  QueueTask(task);

  return p;
}

void TrackBuffersManager::QueueTask(SourceBufferTask* aTask) {
  // The source buffer is a wrapped native, it would be unlinked twice and so
  // the TrackBuffersManager::Detach() would also be called twice. Since the
  // detach task has been done before, we could ignore this task.
  RefPtr<TaskQueue> taskQueue = GetTaskQueueSafe();
  if (!taskQueue) {
    MOZ_ASSERT(aTask->GetType() == SourceBufferTask::Type::Detach,
               "only detach task could happen here!");
    MSE_DEBUG("Could not queue the task '{}' without task queue",
              aTask->GetTypeName());
    return;
  }

  if (!taskQueue->IsCurrentThreadIn()) {
    nsresult rv =
        taskQueue->Dispatch(NewRunnableMethod<RefPtr<SourceBufferTask>>(
            "TrackBuffersManager::QueueTask", this,
            &TrackBuffersManager::QueueTask, aTask));
    MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
    (void)rv;
    return;
  }
  mQueue.Push(aTask);
  ProcessTasks();
}

void TrackBuffersManager::ProcessTasks() {
  // ProcessTask is always called OnTaskQueue, however it is possible that it is
  // called once again after a first Detach task has run, in which case
  // mTaskQueue would be null.
  // This can happen under two conditions:
  // 1- Two Detach tasks were queued in a row due to a double cycle collection.
  // 2- An call to ProcessTasks() had queued another run of ProcessTasks while
  //    a Detach task is pending.
  // We handle these two cases by aborting early.
  // A second Detach task was queued, prior the first one running, ignore it.
  if (!mTaskQueue) {
    RefPtr<SourceBufferTask> task = mQueue.Pop();
    if (!task) {
      return;
    }
    MOZ_RELEASE_ASSERT(task->GetType() == SourceBufferTask::Type::Detach,
                       "only detach task could happen here!");
    MSE_DEBUG("Could not process the task '{}' after detached",
              task->GetTypeName());
    return;
  }

  mTaskQueueCapability->AssertOnCurrentThread();
  typedef SourceBufferTask::Type Type;

  if (mCurrentTask) {
    // Already have a task pending. ProcessTask will be scheduled once the
    // current task complete.
    return;
  }
  RefPtr<SourceBufferTask> task = mQueue.Pop();
  if (!task) {
    // nothing to do.
    return;
  }

  MSE_DEBUG("Process task '{}'", task->GetTypeName());
  switch (task->GetType()) {
    case Type::AppendBuffer:
      mCurrentTask = task;
      if (!mInputBuffer || mInputBuffer->IsEmpty()) {
        // Note: we reset mInputBuffer here to ensure it doesn't grow unbounded.
        mInputBuffer.reset();
        mInputBuffer = Some(MediaSpan(task->As<AppendBufferTask>()->mBuffer));
      } else {
        // mInputBuffer wasn't empty, so we can't just reset it, but we move
        // the data into a new buffer to clear out data no longer in the span.
        MSE_DEBUG(
            "mInputBuffer not empty during append -- data will be copied to "
            "new buffer. mInputBuffer->Length()={} "
            "mInputBuffer->Buffer()->Length()={}",
            mInputBuffer->Length(), mInputBuffer->Buffer()->Length());
        const RefPtr<MediaByteBuffer> newBuffer{new MediaByteBuffer()};
        // Set capacity outside of ctor to let us explicitly handle OOM.
        const size_t newCapacity =
            mInputBuffer->Length() +
            task->As<AppendBufferTask>()->mBuffer->Length();
        if (!newBuffer->SetCapacity(newCapacity, fallible)) {
          RejectAppend(NS_ERROR_OUT_OF_MEMORY, __func__);
          return;
        }
        // Use infallible appends as we've already set capacity above.
        newBuffer->AppendElements(mInputBuffer->Elements(),
                                  mInputBuffer->Length());
        newBuffer->AppendElements(*task->As<AppendBufferTask>()->mBuffer);
        mInputBuffer = Some(MediaSpan(newBuffer));
      }
      mSourceBufferAttributes = std::make_unique<SourceBufferAttributes>(
          task->As<AppendBufferTask>()->mAttributes);
      mAppendWindow =
          Interval<double>(mSourceBufferAttributes->GetAppendWindowStart(),
                           mSourceBufferAttributes->GetAppendWindowEnd());
      ScheduleSegmentParserLoop();
      break;
    case Type::RangeRemoval: {
      bool rv = CodedFrameRemoval(task->As<RangeRemovalTask>()->mRange);
      task->As<RangeRemovalTask>()->mPromise.Resolve(rv, __func__);
      break;
    }
    case Type::EvictData:
      DoEvictData(task->As<EvictDataTask>()->mPlaybackTime,
                  task->As<EvictDataTask>()->mSizeToEvict);
      break;
    case Type::Abort:
      // not handled yet, and probably never.
      break;
    case Type::Reset:
      CompleteResetParserState();
      break;
    case Type::Detach:
      mCurrentInputBuffer = nullptr;
      MOZ_DIAGNOSTIC_ASSERT(mQueue.Length() == 0,
                            "Detach task must be the last");
      mVideoTracks.Reset();
      mAudioTracks.Reset();
      ShutdownDemuxers();
      ResetTaskQueue();
      return;
    case Type::ChangeType:
      MOZ_RELEASE_ASSERT(!mCurrentTask);
      MSE_DEBUG("Processing type change from {} -> {}",
                mType.OriginalString().get(),
                task->As<ChangeTypeTask>()->mType.OriginalString().get());
      mType = task->As<ChangeTypeTask>()->mType;
      mChangeTypeReceived = true;
      mInitData = nullptr;
      // A new input buffer will be created once we receive a new init segment.
      // The first segment received after a changeType call must be an init
      // segment.
      mCurrentInputBuffer = nullptr;
      CompleteResetParserState();
      break;
    default:
      NS_WARNING("Invalid Task");
  }
  TaskQueueFromTaskQueue()->Dispatch(
      NewRunnableMethod("TrackBuffersManager::ProcessTasks", this,
                        &TrackBuffersManager::ProcessTasks));
}

// The MSE spec requires that we abort the current SegmentParserLoop
// which is then followed by a call to ResetParserState.
// However due to our asynchronous design this causes inherent difficulties.
// As the spec behaviour is non deterministic anyway, we instead process all
// pending frames found in the input buffer.
void TrackBuffersManager::AbortAppendData() {
  MOZ_ASSERT(NS_IsMainThread());
  MSE_DEBUG("");

  QueueTask(new AbortTask());
}

void TrackBuffersManager::ResetParserState(
    SourceBufferAttributes& aAttributes) {
  MOZ_ASSERT(NS_IsMainThread());
  MSE_DEBUG("");

  // Spec states:
  // 1. If the append state equals PARSING_MEDIA_SEGMENT and the input buffer
  // contains some complete coded frames, then run the coded frame processing
  // algorithm until all of these complete coded frames have been processed.
  // However, we will wait until all coded frames have been processed regardless
  // of the value of append state.
  QueueTask(new ResetTask());

  // ResetParserState has some synchronous steps that much be performed now.
  // The remaining steps will be performed once the ResetTask gets executed.

  // 6. If the mode attribute equals "sequence", then set the group start
  // timestamp to the group end timestamp
  if (aAttributes.GetAppendMode() == SourceBufferAppendMode::Sequence) {
    aAttributes.SetGroupStartTimestamp(aAttributes.GetGroupEndTimestamp());
  }
  // 8. Set append state to WAITING_FOR_SEGMENT.
  aAttributes.SetAppendState(AppendState::WAITING_FOR_SEGMENT);
}

RefPtr<TrackBuffersManager::RangeRemovalPromise>
TrackBuffersManager::RangeRemoval(TimeUnit aStart, TimeUnit aEnd) {
  MOZ_ASSERT(NS_IsMainThread());
  MSE_DEBUG("From {:.2f} to {:.2f}", aStart.ToSeconds(), aEnd.ToSeconds());

  Reopen();

  return InvokeAsync(static_cast<AbstractThread*>(GetTaskQueueSafe().get()),
                     this, __func__,
                     &TrackBuffersManager::CodedFrameRemovalWithPromise,
                     TimeInterval(aStart, aEnd));
}

TrackBuffersManager::EvictDataResult TrackBuffersManager::EvictData(
    const TimeUnit& aPlaybackTime, int64_t aSize, TrackType aType) {
  MOZ_ASSERT(NS_IsMainThread());

  if (aSize > EvictionThreshold(aType)) {
    // We're adding more data than we can hold.
    return EvictDataResult::BUFFER_FULL;
  }
  const int64_t toEvict = GetSize() + aSize - EvictionThreshold(aType);

  const uint32_t canEvict =
      Evictable(HasVideo() ? TrackInfo::kVideoTrack : TrackInfo::kAudioTrack);

  MSE_DEBUG(
      "currentTime={} buffered={}kB, eviction threshold={}kB, "
      "evict={}kB canevict={}kB",
      aPlaybackTime.ToMicroseconds(), GetSize() / 1024,
      EvictionThreshold(aType) / 1024, toEvict / 1024, canEvict / 1024);

  if (toEvict <= 0) {
    mEvictionState = EvictionState::NO_EVICTION_NEEDED;
    return EvictDataResult::NO_DATA_EVICTED;
  }

  EvictDataResult result;

  if (mBufferFull && mEvictionState == EvictionState::EVICTION_COMPLETED &&
      canEvict < uint32_t(toEvict)) {
    // Our buffer is currently full. We will make another eviction attempt.
    // However, the current appendBuffer will fail as we can't know ahead of
    // time if the eviction will later succeed.
    result = EvictDataResult::BUFFER_FULL;
  } else {
    mEvictionState = EvictionState::EVICTION_NEEDED;
    result = EvictDataResult::NO_DATA_EVICTED;
  }
  MSE_DEBUG("Reached our size limit, schedule eviction of {} bytes ({})",
            toEvict,
            result == EvictDataResult::BUFFER_FULL ? "buffer full"
                                                   : "no data evicted");
  QueueTask(new EvictDataTask(aPlaybackTime, toEvict));

  return result;
}

void TrackBuffersManager::EvictDataWithoutSize(TrackType aType,
                                               const media::TimeUnit& aTarget) {
  MOZ_ASSERT(OnTaskQueue());
  auto& track = GetTracksData(aType);
  const auto bufferedSz = track.mSizeBuffer;
  const auto evictionSize = EvictionThreshold(aType);
  const double watermarkRatio = bufferedSz / (double)(evictionSize);  // can > 1

  MSE_DEBUG(
      "EvictDataWithoutSize, track={}, buffered={}kB, eviction threshold={}kB, "
      "wRatio={:f}, target={}, bufferedRange={}",
      TrackTypeToStr(aType), bufferedSz / 1024, evictionSize / 1024,
      watermarkRatio, aTarget.ToMicroseconds(),
      DumpTimeRanges(track.mBufferedRanges).get());

  // This type of eviction MUST only be used when the target is not in the
  // current buffered range, which means all data are evictable. Otherwise, we
  // have to calculate the amount of evictable data.
  MOZ_ASSERT(track.mBufferedRanges.Find(aTarget) == TimeIntervals::NoIndex);

  // This type of eviction is introduced to mitigate the pressure of the buffer
  // nearly being full. If the buffer is still far from being full, we do
  // nothing.
  if (watermarkRatio < mEvictionBufferWatermarkRatio) {
    return;
  }
  MSE_DEBUG("Queued EvictDataTask to evict size automatically");
  QueueTask(new EvictDataTask(aTarget));
}

void TrackBuffersManager::ChangeType(const MediaContainerType& aType) {
  MOZ_ASSERT(NS_IsMainThread());

  QueueTask(new ChangeTypeTask(aType));
}

TimeIntervals TrackBuffersManager::Buffered() const {
  MSE_DEBUG("");

  // http://w3c.github.io/media-source/index.html#widl-SourceBuffer-buffered

  MutexAutoLock mut(mMutex);
  nsTArray<const TimeIntervals*> tracks;
  if (HasVideo()) {
    tracks.AppendElement(&mVideoBufferedRanges);
  }
  if (HasAudio()) {
    tracks.AppendElement(&mAudioBufferedRanges);
  }

  // 2. Let highest end time be the largest track buffer ranges end time across
  // all the track buffers managed by this SourceBuffer object.
  TimeUnit highestEndTime = HighestEndTime(tracks);

  // 3. Let intersection ranges equal a TimeRange object containing a single
  // range from 0 to highest end time.
  TimeIntervals intersection{
      TimeInterval(TimeUnit::FromSeconds(0), highestEndTime)};

  // 4. For each track buffer managed by this SourceBuffer, run the following
  // steps:
  //   1. Let track ranges equal the track buffer ranges for the current track
  //   buffer.
  for (const TimeIntervals* trackRanges : tracks) {
    // 2. If readyState is "ended", then set the end time on the last range in
    // track ranges to highest end time.
    // 3. Let new intersection ranges equal the intersection between the
    // intersection ranges and the track ranges.
    if (mEnded) {
      TimeIntervals tR = *trackRanges;
      tR.Add(TimeInterval(tR.GetEnd(), highestEndTime));
      intersection.Intersection(tR);
    } else {
      intersection.Intersection(*trackRanges);
    }
  }
  return intersection;
}

int64_t TrackBuffersManager::GetSize() const { return mSizeSourceBuffer; }

void TrackBuffersManager::SetEnded(
    const dom::Optional<dom::MediaSourceEndOfStreamError>& aError) {
  MOZ_ASSERT(NS_IsMainThread());
  MutexAutoLock lock(mMutex);
  if (!aError.WasPassed()) {
    // error is not set.
    // Notify the media element that it now has all of the media data.
    // https://w3c.github.io/media-source/#dfn-end-of-stream
    mHaveAllData = true;
  }
  mEnded = true;
}

void TrackBuffersManager::Reopen() {
  MOZ_ASSERT(NS_IsMainThread());
  MutexAutoLock lock(mMutex);
  mHaveAllData = false;
  mEnded = false;
}

void TrackBuffersManager::Detach() {
  MOZ_ASSERT(NS_IsMainThread());
  MSE_DEBUG("");
  QueueTask(new DetachTask());
}

void TrackBuffersManager::CompleteResetParserState() {
  mTaskQueueCapability->AssertOnCurrentThread();
  AUTO_PROFILER_LABEL("TrackBuffersManager::CompleteResetParserState",
                      MEDIA_PLAYBACK);
  MSE_DEBUG("");

  // We shouldn't change mInputDemuxer while a demuxer init/reset request is
  // being processed. See bug 1239983.
  MOZ_DIAGNOSTIC_ASSERT(!mDemuxerInitRequest.Exists(),
                        "Previous AppendBuffer didn't complete");

  for (auto& track : GetTracksList()) {
    // 2. Unset the last decode timestamp on all track buffers.
    // 3. Unset the last frame duration on all track buffers.
    // 4. Unset the highest end timestamp on all track buffers.
    // 5. Set the need random access point flag on all track buffers to true.
    track->ResetAppendState();

    // if we have been aborted, we may have pending frames that we are going
    // to discard now.
    track->mQueuedSamples.Clear();
  }

  // 7. Remove all bytes from the input buffer.
  mPendingInputBuffer.reset();
  mInputBuffer.reset();
  if (mCurrentInputBuffer) {
    mCurrentInputBuffer->EvictAll();
    // The demuxer will be recreated during the next run of SegmentParserLoop.
    // As such we don't need to notify it that data has been removed.
    mCurrentInputBuffer = new SourceBufferResource();
  }

  // We could be left with a demuxer in an unusable state. It needs to be
  // recreated. Unless we have a pending changeType operation, we store in the
  // InputBuffer an init segment which will be parsed during the next Segment
  // Parser Loop and a new demuxer will be created and initialized.
  // If we are in the middle of a changeType operation, then we do not have an
  // init segment yet. The next appendBuffer operation will need to provide such
  // init segment.
  if (mFirstInitializationSegmentReceived && !mChangeTypeReceived) {
    MOZ_ASSERT(mInitData && mInitData->Length(),
               "we must have an init segment");
    // The aim here is really to destroy our current demuxer.
    CreateDemuxerforMIMEType();
    if (mParser->HasImplicitInitSegment()) {
      // The next media segment carries its own initialization segment.
      // Parsing the stored one would also parse it as media.
      RecreateParser(false);
      return;
    }
    // Recreate our input buffer. We can't directly assign the initData buffer
    // to mInputBuffer as it will get modified in the Segment Parser Loop.
    mInputBuffer = Some(MediaSpan::WithCopyOf(mInitData));
    RecreateParser(true);
  } else {
    RecreateParser(false);
  }
}

int64_t TrackBuffersManager::EvictionThreshold(
    TrackInfo::TrackType aType) const {
  MOZ_ASSERT(aType != TrackInfo::kTextTrack);
  if (aType == TrackInfo::kVideoTrack ||
      (aType == TrackInfo::kUndefinedTrack && HasVideo())) {
    return mVideoEvictionThreshold;
  }
  return mAudioEvictionThreshold;
}

void TrackBuffersManager::DoEvictData(const TimeUnit& aPlaybackTime,
                                      Maybe<int64_t> aSizeToEvict) {
  mTaskQueueCapability->AssertOnCurrentThread();
  AUTO_PROFILER_LABEL("TrackBuffersManager::DoEvictData", MEDIA_PLAYBACK);
  MSE_DEBUG("DoEvictData, time={}", aPlaybackTime.ToMicroseconds());

  mEvictionState = EvictionState::EVICTION_COMPLETED;

  // Video is what takes the most space, only evict there if we have video.
  auto& track = HasVideo() ? mVideoTracks : mAudioTracks;
  const auto& buffer = track.GetTrackBuffer();
  if (buffer.IsEmpty()) {
    // Buffer has been emptied while the eviction was queued, nothing to do.
    return;
  }
  if (track.mBufferedRanges.IsEmpty()) {
    MSE_DEBUG(
        "DoEvictData running with no buffered ranges. 0 duration data likely "
        "present in our buffer(s). Evicting all data!");
    // We have no buffered ranges, but may still have data. This happens if the
    // buffer is full of 0 duration data. Normal removal procedures don't clear
    // 0 duration data, so blow away all our data.
    RemoveAllCodedFrames();
    return;
  }

  // The targeted playback time isn't in the buffered range yet, we're waiting
  // for the data so all data in current buffered range are evictable.
  if (!aSizeToEvict) {
    // If the targeted time has been appended to our buffer range, then we need
    // to recalculate which part of data is evictable. We will only perform
    // following eviction when all data in the buffer range are evictable.
    if (track.mBufferedRanges.Find(aPlaybackTime) != TimeIntervals::NoIndex) {
      return;
    }
    // Evict data until the size falls below the threshold we set.
    const int64_t sizeToEvict =
        GetSize() - static_cast<int64_t>(EvictionThreshold() *
                                         mEvictionBufferWatermarkRatio);
    // Another eviction or frame removal has been executed before this task.
    if (sizeToEvict <= 0) {
      return;
    }
    int64_t toEvict = sizeToEvict;

    // We need to evict data from a place which is the furthest from the
    // playback time, otherwise we might incorrectly evict the data which should
    // have stayed in the buffer in order to decode the frame at the targeted
    // playback time. Eg. targeted time is X, and we might need a key frame from
    // X-5. Therefore, we should evict data from a place which is far from X
    // (maybe X±100)
    const TimeUnit start = track.mBufferedRanges.GetStart();
    const TimeUnit end = track.mBufferedRanges.GetEnd();
    MSE_DEBUG("PlaybackTime={}, extents=[{}, {}]",
              aPlaybackTime.ToMicroseconds(), start.ToMicroseconds(),
              end.ToMicroseconds());
    if (end - aPlaybackTime > aPlaybackTime - start) {
      size_t evictedFramesStartIndex = buffer.Length();
      while (evictedFramesStartIndex > 0 && toEvict > 0) {
        --evictedFramesStartIndex;
        toEvict -= AssertedCast<int64_t>(
            buffer[evictedFramesStartIndex]->ComputedSizeOfIncludingThis());
      }
      MSE_DEBUG("Auto evicting {} bytes [{}, inf] from tail",
                sizeToEvict - toEvict,
                buffer[evictedFramesStartIndex]->mTime.ToMicroseconds());
      CodedFrameRemoval(TimeInterval(buffer[evictedFramesStartIndex]->mTime,
                                     TimeUnit::FromInfinity()));
    } else {
      uint32_t lastKeyFrameIndex = 0;
      int64_t partialEvict = 0;
      for (uint32_t i = 0; i < buffer.Length(); i++) {
        const auto& frame = buffer[i];
        if (frame->mKeyframe) {
          lastKeyFrameIndex = i;
          toEvict -= partialEvict;
          if (toEvict <= 0) {
            break;
          }
          partialEvict = 0;
        }
        partialEvict +=
            AssertedCast<int64_t>(frame->ComputedSizeOfIncludingThis());
      }
      TimeUnit start = track.mBufferedRanges[0].mStart;
      TimeUnit end =
          buffer[lastKeyFrameIndex]->mTime - TimeUnit::FromMicroseconds(1);
      MSE_DEBUG("Auto evicting {} bytes [{}, {}] from head",
                sizeToEvict - toEvict, start.ToMicroseconds(),
                end.ToMicroseconds());
      if (end > start) {
        CodedFrameRemoval(TimeInterval(start, end));
      }
    }
    return;
  }

  // Remove any data we've already played, or before the next sample to be
  // demuxed whichever is lowest.
  TimeUnit lowerLimit = std::min(track.mNextSampleTime, aPlaybackTime);
  uint32_t lastKeyFrameIndex = 0;
  int64_t sizeToEvict = *aSizeToEvict;
  int64_t toEvict = sizeToEvict;
  int64_t partialEvict = 0;
  for (uint32_t i = 0; i < buffer.Length(); i++) {
    const auto& frame = buffer[i];
    if (frame->mKeyframe) {
      lastKeyFrameIndex = i;
      toEvict -= partialEvict;
      if (toEvict <= 0) {
        break;
      }
      partialEvict = 0;
    }
    if (frame->GetEndTime() >= lowerLimit) {
      break;
    }
    partialEvict += AssertedCast<int64_t>(frame->ComputedSizeOfIncludingThis());
  }

  const int64_t finalSize = mSizeSourceBuffer - sizeToEvict;

  if (lastKeyFrameIndex > 0) {
    MSE_DEBUG("Step1. Evicting {} bytes prior currentTime",
              sizeToEvict - toEvict);
    TimeUnit start = track.mBufferedRanges[0].mStart;
    TimeUnit end =
        buffer[lastKeyFrameIndex]->mTime - TimeUnit::FromMicroseconds(1);
    if (end > start) {
      CodedFrameRemoval(TimeInterval(start, end));
    }
  }

  if (mSizeSourceBuffer <= finalSize) {
    MSE_DEBUG("Total buffer size is already smaller than final size");
    return;
  }

  toEvict = mSizeSourceBuffer - finalSize;

  // See if we can evict data into the future.
  // We do not evict data from the currently used buffered interval.

  TimeUnit currentPosition = std::max(aPlaybackTime, track.mNextSampleTime);
  TimeIntervals futureBuffered(
      TimeInterval(currentPosition, TimeUnit::FromInfinity()));
  futureBuffered.Intersection(track.mBufferedRanges);
  futureBuffered.SetFuzz(MediaSourceDemuxer::EOS_FUZZ / 2);
  if (futureBuffered.Length() <= 1) {
    // We have one continuous segment ahead of us:
    MSE_DEBUG("Nothing in future can be evicted");
    return;
  }

  // Don't evict before the end of the current segment
  TimeUnit upperLimit = futureBuffered[0].mEnd;
  uint32_t evictedFramesStartIndex = buffer.Length();
  for (uint32_t i = buffer.Length(); i-- > 0;) {
    const auto& frame = buffer[i];
    if (frame->mTime <= upperLimit || toEvict <= 0) {
      // We've reached a frame that shouldn't be evicted -> Evict after it ->
      // i+1. Or the previous loop reached the eviction threshold -> Evict from
      // it -> i+1.
      evictedFramesStartIndex = i + 1;
      break;
    }
    toEvict -= AssertedCast<int64_t>(frame->ComputedSizeOfIncludingThis());
  }
  if (evictedFramesStartIndex < buffer.Length()) {
    MSE_DEBUG("Step2. Evicting {} bytes from trailing data",
              mSizeSourceBuffer - finalSize - toEvict);
    CodedFrameRemoval(TimeInterval(buffer[evictedFramesStartIndex]->mTime,
                                   TimeUnit::FromInfinity()));
  }
}

RefPtr<TrackBuffersManager::RangeRemovalPromise>
TrackBuffersManager::CodedFrameRemovalWithPromise(
    const TimeInterval& aInterval) {
  mTaskQueueCapability->AssertOnCurrentThread();

  RefPtr<RangeRemovalTask> task = new RangeRemovalTask(aInterval);
  RefPtr<RangeRemovalPromise> p = task->mPromise.Ensure(__func__);
  QueueTask(task);

  return p;
}

bool TrackBuffersManager::CodedFrameRemoval(const TimeInterval& aInterval) {
  MOZ_ASSERT(OnTaskQueue());
  AUTO_PROFILER_LABEL("TrackBuffersManager::CodedFrameRemoval", MEDIA_PLAYBACK);
  MSE_DEBUG("From {:.2f}s to {:.2f}", aInterval.mStart.ToSeconds(),
            aInterval.mEnd.ToSeconds());

#if DEBUG
  if (HasVideo()) {
    MSE_DEBUG("before video ranges={}",
              DumpTimeRangesRaw(mVideoTracks.mBufferedRanges).get());
  }
  if (HasAudio()) {
    MSE_DEBUG("before audio ranges={}",
              DumpTimeRangesRaw(mAudioTracks.mBufferedRanges).get());
  }
#endif

  // 1. Let start be the starting presentation timestamp for the removal range.
  TimeUnit start = aInterval.mStart;
  // 2. Let end be the end presentation timestamp for the removal range.
  TimeUnit end = aInterval.mEnd;

  bool dataRemoved = false;

  // 3. For each track buffer in this source buffer, run the following steps:
  for (auto* track : GetTracksList()) {
    MSE_DEBUGV("Processing {} track", track->mInfo->mMimeType.get());
    // 1. Let remove end timestamp be the current value of duration
    // See bug: https://www.w3.org/Bugs/Public/show_bug.cgi?id=28727
    // At worse we will remove all frames until the end, unless a key frame is
    // found between the current interval's end and the trackbuffer's end.
    TimeUnit removeEndTimestamp = track->mBufferedRanges.GetEnd();

    if (start > removeEndTimestamp) {
      // Nothing to remove.
      continue;
    }

    // 2. If this track buffer has a random access point timestamp that is
    // greater than or equal to end, then update remove end timestamp to that
    // random access point timestamp.
    if (end < track->mBufferedRanges.GetEnd()) {
      for (auto& frame : track->GetTrackBuffer()) {
        if (frame->mKeyframe && frame->mTime >= end) {
          removeEndTimestamp = frame->mTime;
          break;
        }
      }
    }

    // 3. Remove all media data, from this track buffer, that contain starting
    // timestamps greater than or equal to start and less than the remove end
    // timestamp.
    // 4. Remove decoding dependencies of the coded frames removed in the
    // previous step: Remove all coded frames between the coded frames removed
    // in the previous step and the next random access point after those removed
    // frames.
    TimeIntervals removedInterval{TimeInterval(start, removeEndTimestamp)};
    RemoveFrames(removedInterval, *track, 0, RemovalMode::kRemoveFrame);

    // 5. If this object is in activeSourceBuffers, the current playback
    // position is greater than or equal to start and less than the remove end
    // timestamp, and HTMLMediaElement.readyState is greater than HAVE_METADATA,
    // then set the HTMLMediaElement.readyState attribute to HAVE_METADATA and
    // stall playback. This will be done by the MDSM during playback.
    // TODO properly, so it works even if paused.
  }

  UpdateBufferedRanges();

  // Update our reported total size.
  mSizeSourceBuffer = mVideoTracks.mSizeBuffer + mAudioTracks.mSizeBuffer;

  // 4. If buffer full flag equals true and this object is ready to accept more
  // bytes, then set the buffer full flag to false.
  if (mBufferFull && mSizeSourceBuffer < EvictionThreshold()) {
    mBufferFull = false;
  }

  return dataRemoved;
}

void TrackBuffersManager::RemoveAllCodedFrames() {
  // This is similar to RemoveCodedFrames, but will attempt to remove ALL
  // the frames. This is not to spec, as explained below at step 3.1. Steps
  // below coincide with Remove Coded Frames algorithm from the spec.
  MSE_DEBUG("RemoveAllCodedFrames called.");
  MOZ_ASSERT(OnTaskQueue());
  AUTO_PROFILER_LABEL("TrackBuffersManager::RemoveAllCodedFrames",
                      MEDIA_PLAYBACK);

  // 1. Let start be the starting presentation timestamp for the removal range.
  TimeUnit start{};
  // 2. Let end be the end presentation timestamp for the removal range.
  TimeUnit end = TimeUnit::FromMicroseconds(1);
  // Find an end time such that our range will include every frame in every
  // track. We do this by setting the end of our interval to the largest end
  // time seen + 1 microsecond.
  for (TrackData* track : GetTracksList()) {
    for (auto& frame : track->GetTrackBuffer()) {
      MOZ_ASSERT(frame->mTime >= start,
                 "Shouldn't have frame at negative time!");
      TimeUnit frameEnd = frame->mTime + frame->mDuration;
      if (frameEnd > end) {
        end = frameEnd + TimeUnit::FromMicroseconds(1);
      }
    }
  }

  // 3. For each track buffer in this source buffer, run the following steps:
  TimeIntervals removedInterval{TimeInterval(start, end)};
  for (TrackData* track : GetTracksList()) {
    // 1. Let remove end timestamp be the current value of duration
    // ^ It's off spec, but we ignore this in order to clear 0 duration frames.
    // If we don't ignore this rule and our buffer is full of 0 duration frames
    // at timestamp n, we get an eviction range of [0, n). When we get to step
    // 3.3 below, the 0 duration frames will not be evicted because their
    // timestamp is not less than remove end timestamp -- it will in fact be
    // equal to remove end timestamp.
    //
    // 2. If this track buffer has a random access point timestamp that is
    // greater than or equal to end, then update remove end timestamp to that
    // random access point timestamp.
    // ^ We've made sure end > any sample's timestamp, so can skip this.
    //
    // 3. Remove all media data, from this track buffer, that contain starting
    // timestamps greater than or equal to start and less than the remove end
    // timestamp.
    // 4. Remove decoding dependencies of the coded frames removed in the
    // previous step: Remove all coded frames between the coded frames removed
    // in the previous step and the next random access point after those removed
    // frames.

    // This should remove every frame in the track because removedInterval was
    // constructed such that every frame in any track falls into that interval.
    RemoveFrames(removedInterval, *track, 0, RemovalMode::kRemoveFrame);

    // 5. If this object is in activeSourceBuffers, the current playback
    // position is greater than or equal to start and less than the remove end
    // timestamp, and HTMLMediaElement.readyState is greater than HAVE_METADATA,
    // then set the HTMLMediaElement.readyState attribute to HAVE_METADATA and
    // stall playback. This will be done by the MDSM during playback.
    // TODO properly, so it works even if paused.
  }

  UpdateBufferedRanges();
#ifdef DEBUG
  {
    MutexAutoLock lock(mMutex);
    MOZ_ASSERT(
        mAudioBufferedRanges.IsEmpty(),
        "Should have no buffered video ranges after evicting everything.");
    MOZ_ASSERT(
        mVideoBufferedRanges.IsEmpty(),
        "Should have no buffered video ranges after evicting everything.");
  }
#endif
  mSizeSourceBuffer = mVideoTracks.mSizeBuffer + mAudioTracks.mSizeBuffer;
  MOZ_ASSERT(mSizeSourceBuffer == 0,
             "Buffer should be empty after evicting everything!");
  if (mBufferFull && mSizeSourceBuffer < EvictionThreshold()) {
    mBufferFull = false;
  }
}

void TrackBuffersManager::UpdateBufferedRanges() {
  MutexAutoLock mut(mMutex);

  mVideoBufferedRanges = mVideoTracks.mSanitizedBufferedRanges;
  mAudioBufferedRanges = mAudioTracks.mSanitizedBufferedRanges;

#if DEBUG
  if (HasVideo()) {
    MSE_DEBUG("after video ranges={}",
              DumpTimeRangesRaw(mVideoTracks.mBufferedRanges).get());
  }
  if (HasAudio()) {
    MSE_DEBUG("after audio ranges={}",
              DumpTimeRangesRaw(mAudioTracks.mBufferedRanges).get());
  }
#endif
  if (profiler_thread_is_being_profiled_for_markers()) {
    nsPrintfCString msg("buffered, ");
    if (HasVideo()) {
      msg += "video="_ns;
      msg += DumpTimeRangesRaw(mVideoTracks.mBufferedRanges);
    }
    if (HasAudio()) {
      msg += "audio="_ns;
      msg += DumpTimeRangesRaw(mAudioTracks.mBufferedRanges);
    }
    PROFILER_MARKER_TEXT("UpdateBufferedRanges", MEDIA_PLAYBACK, {}, msg);
  }
}

void TrackBuffersManager::SegmentParserLoop() {
  MOZ_ASSERT(OnTaskQueue());
  AUTO_PROFILER_LABEL("TrackBuffersManager::SegmentParserLoop", MEDIA_PLAYBACK);

  while (true) {
    // 1. If the input buffer is empty, then jump to the need more data step
    // below.
    if (!mInputBuffer || mInputBuffer->IsEmpty()) {
      NeedMoreData();
      return;
    }
    // 2. If the input buffer contains bytes that violate the SourceBuffer
    // byte stream format specification, then run the append error algorithm
    // with the decode error parameter set to true and abort this algorithm.
    // TODO

    // 3. Remove any bytes that the byte stream format specifications say must
    // be ignored from the start of the input buffer. This is only done while
    // waiting for a segment, when the input buffer starts between segments.
    // ContainerParsers that don't remove such bytes skip over them instead.
    if (mSourceBufferAttributes->GetAppendState() ==
        AppendState::WAITING_FOR_SEGMENT) {
      if (size_t ignored = mParser->SkipIgnorableBytesAtStart(*mInputBuffer)) {
        MSE_DEBUGV("Ignoring {} bytes", ignored);
        mInputBuffer->RemoveFront(ignored);
        continue;
      }
    }

    // 4. If the append state equals WAITING_FOR_SEGMENT, then run the following
    // steps:
    if (mSourceBufferAttributes->GetAppendState() ==
        AppendState::WAITING_FOR_SEGMENT) {
      MediaResult haveInitSegment =
          mParser->IsInitSegmentPresent(*mInputBuffer);
      if (NS_SUCCEEDED(haveInitSegment)) {
        SetAppendState(AppendState::PARSING_INIT_SEGMENT);
        if (mFirstInitializationSegmentReceived && !mChangeTypeReceived) {
          // This is a new initialization segment. Obsolete the old one.
          RecreateParser(false);
        }
        continue;
      }
      MediaResult haveMediaSegment =
          mParser->IsMediaSegmentPresent(*mInputBuffer);
      if (NS_SUCCEEDED(haveMediaSegment)) {
        SetAppendState(AppendState::PARSING_MEDIA_SEGMENT);
        mNewMediaSegmentStarted = true;
        continue;
      }
      // We have neither an init segment nor a media segment.
      // Check if it was invalid data.
      if (haveInitSegment != NS_ERROR_NOT_AVAILABLE) {
        MSE_DEBUG("Found invalid data.");
        RejectAppend(haveInitSegment, __func__);
        return;
      }
      if (haveMediaSegment != NS_ERROR_NOT_AVAILABLE) {
        MSE_DEBUG("Found invalid data.");
        RejectAppend(haveMediaSegment, __func__);
        return;
      }
      MSE_DEBUG("Found incomplete data.");
      NeedMoreData();
      return;
    }

    MOZ_ASSERT(mSourceBufferAttributes->GetAppendState() ==
                   AppendState::PARSING_INIT_SEGMENT ||
               mSourceBufferAttributes->GetAppendState() ==
                   AppendState::PARSING_MEDIA_SEGMENT);

    TimeUnit start, end;
    MediaResult newData = NS_ERROR_NOT_AVAILABLE;

    if (mSourceBufferAttributes->GetAppendState() ==
            AppendState::PARSING_INIT_SEGMENT ||
        (mSourceBufferAttributes->GetAppendState() ==
             AppendState::PARSING_MEDIA_SEGMENT &&
         mFirstInitializationSegmentReceived && !mChangeTypeReceived)) {
      newData = mParser->ParseStartAndEndTimestamps(*mInputBuffer, start, end);
      if (NS_FAILED(newData) && newData.Code() != NS_ERROR_NOT_AVAILABLE) {
        RejectAppend(newData, __func__);
        return;
      }
      mProcessedInput += mInputBuffer->Length();
    }

    // 5. If the append state equals PARSING_INIT_SEGMENT, then run the
    // following steps:
    if (mSourceBufferAttributes->GetAppendState() ==
        AppendState::PARSING_INIT_SEGMENT) {
      if (mParser->HasImplicitInitSegment()
              ? !mParser->HasCompleteInitData()
              : mParser->InitSegmentRange().IsEmpty()) {
        mInputBuffer.reset();
        NeedMoreData();
        return;
      }
      InitializationSegmentReceived();
      return;
    }
    if (mSourceBufferAttributes->GetAppendState() ==
        AppendState::PARSING_MEDIA_SEGMENT) {
      // 1. If the first initialization segment received flag is false, then run
      //    the append error algorithm with the decode error parameter set to
      //    true and abort this algorithm.
      //    Or we are in the process of changeType, in which case we must first
      //    get an init segment before getting a media segment.
      if (!mFirstInitializationSegmentReceived || mChangeTypeReceived) {
        RejectAppend(NS_ERROR_FAILURE, __func__);
        return;
      }

      // We can't feed some demuxers (WebMDemuxer) with data that do not have
      // monotonizally increasing timestamps. So we check if we have a
      // discontinuity from the previous segment parsed.
      // If so, recreate a new demuxer to ensure that the demuxer is only fed
      // monotonically increasing data.
      if (mNewMediaSegmentStarted) {
        if (NS_SUCCEEDED(newData) && mLastParsedEndTime.isSome() &&
            start < mLastParsedEndTime.ref()) {
          nsPrintfCString msg(
              "Re-creating demuxer, new start (%" PRId64
              ") is smaller than last parsed end time (%" PRId64 ")",
              start.ToMicroseconds(), mLastParsedEndTime->ToMicroseconds());
          if (profiler_thread_is_being_profiled_for_markers()) {
            PROFILER_MARKER_TEXT("Re-create demuxer", MEDIA_PLAYBACK, {}, msg);
          }
          MSE_DEBUG("{}", msg.get());
          mFrameEndTimeBeforeRecreateDemuxer = Some(end);
          ResetDemuxingState();
          return;
        }
        if (NS_SUCCEEDED(newData) || !mParser->MediaSegmentRange().IsEmpty()) {
          if (mPendingInputBuffer) {
            // We now have a complete media segment header. We can resume
            // parsing the data.
            AppendDataToCurrentInputBuffer(*mPendingInputBuffer);
            mPendingInputBuffer.reset();
          }
          mNewMediaSegmentStarted = false;
        } else {
          // We don't have any data to demux yet, stash aside the data.
          // This also handles the case:
          // 2. If the input buffer does not contain a complete media segment
          // header yet, then jump to the need more data step below.
          if (!mPendingInputBuffer) {
            mPendingInputBuffer = Some(MediaSpan(*mInputBuffer));
          } else {
            // Note we reset mInputBuffer below, so this won't end up appending
            // the contents of mInputBuffer to itself.
            mPendingInputBuffer->Append(*mInputBuffer);
          }

          mInputBuffer.reset();
          NeedMoreData();
          return;
        }
      }

      // 3. If the input buffer contains one or more complete coded frames, then
      // run the coded frame processing algorithm.
      RefPtr<TrackBuffersManager> self = this;
      CodedFrameProcessing()
          ->Then(
              TaskQueueFromTaskQueue(), __func__,
              [self](bool aNeedMoreData) {
                self->mTaskQueueCapability->AssertOnCurrentThread();
                self->mProcessingRequest.Complete();
                if (aNeedMoreData) {
                  self->NeedMoreData();
                } else {
                  self->ScheduleSegmentParserLoop();
                }
              },
              [self](const MediaResult& aRejectValue) {
                self->mTaskQueueCapability->AssertOnCurrentThread();
                self->mProcessingRequest.Complete();
                self->RejectAppend(aRejectValue, __func__);
              })
          ->Track(mProcessingRequest);
      return;
    }
  }
}

void TrackBuffersManager::NeedMoreData() {
  MSE_DEBUG("");
  MOZ_DIAGNOSTIC_ASSERT(mCurrentTask &&
                        mCurrentTask->GetType() ==
                            SourceBufferTask::Type::AppendBuffer);
  MOZ_DIAGNOSTIC_ASSERT(mSourceBufferAttributes);

  mCurrentTask->As<AppendBufferTask>()->mPromise.Resolve(
      SourceBufferTask::AppendBufferResult(mActiveTrack,
                                           *mSourceBufferAttributes),
      __func__);
  mSourceBufferAttributes = nullptr;
  mCurrentTask = nullptr;
  ProcessTasks();
}

void TrackBuffersManager::RejectAppend(const MediaResult& aRejectValue,
                                       const char* aName) {
  MSE_DEBUG("rv={}", static_cast<uint32_t>(aRejectValue.Code()));
  MOZ_DIAGNOSTIC_ASSERT(mCurrentTask &&
                        mCurrentTask->GetType() ==
                            SourceBufferTask::Type::AppendBuffer);

  mCurrentTask->As<AppendBufferTask>()->mPromise.Reject(aRejectValue, __func__);
  mSourceBufferAttributes = nullptr;
  mCurrentTask = nullptr;
  ProcessTasks();
}

void TrackBuffersManager::ScheduleSegmentParserLoop() {
  MOZ_ASSERT(OnTaskQueue());
  TaskQueueFromTaskQueue()->Dispatch(
      NewRunnableMethod("TrackBuffersManager::SegmentParserLoop", this,
                        &TrackBuffersManager::SegmentParserLoop));
}

void TrackBuffersManager::ShutdownDemuxers() {
  if (profiler_thread_is_being_profiled_for_markers()) {
    PROFILER_MARKER_UNTYPED("ShutdownDemuxers", MEDIA_PLAYBACK);
  }
  if (mVideoTracks.mDemuxer) {
    mVideoTracks.mDemuxer->BreakCycles();
    mVideoTracks.mDemuxer = nullptr;
  }
  if (mAudioTracks.mDemuxer) {
    mAudioTracks.mDemuxer->BreakCycles();
    mAudioTracks.mDemuxer = nullptr;
  }
  // We shouldn't change mInputDemuxer while a demuxer init/reset request is
  // being processed. See bug 1239983.
  MOZ_DIAGNOSTIC_ASSERT(!mDemuxerInitRequest.Exists());
  mInputDemuxer = nullptr;
  mLastParsedEndTime.reset();
}

void TrackBuffersManager::CreateDemuxerforMIMEType() {
  mTaskQueueCapability->AssertOnCurrentThread();
  MSE_DEBUG("mType.OriginalString={}", mType.OriginalString().get());
  ShutdownDemuxers();

  if (mType.Type() == MEDIAMIMETYPE(VIDEO_WEBM) ||
      mType.Type() == MEDIAMIMETYPE(AUDIO_WEBM)) {
    if (mFrameEndTimeBeforeRecreateDemuxer) {
      MSE_DEBUG(
          "CreateDemuxerFromMimeType: "
          "mFrameEndTimeBeforeRecreateDemuxer={}",
          mFrameEndTimeBeforeRecreateDemuxer->ToMicroseconds());
    }
    mInputDemuxer = new WebMDemuxer(mCurrentInputBuffer, true,
                                    mFrameEndTimeBeforeRecreateDemuxer);
    mFrameEndTimeBeforeRecreateDemuxer.reset();
    DDLINKCHILD("demuxer", mInputDemuxer.get());
    return;
  }

  if (mType.Type() == MEDIAMIMETYPE(VIDEO_MP4) ||
      mType.Type() == MEDIAMIMETYPE(AUDIO_MP4)) {
    mInputDemuxer = new MP4Demuxer(mCurrentInputBuffer);
    mFrameEndTimeBeforeRecreateDemuxer.reset();
    DDLINKCHILD("demuxer", mInputDemuxer.get());
    return;
  }

  if (mType.Type() == MEDIAMIMETYPE(AUDIO_MP3)) {
    mInputDemuxer = new MPEGAudioByteStreamDemuxer(mCurrentInputBuffer);
    mFrameEndTimeBeforeRecreateDemuxer.reset();
    DDLINKCHILD("demuxer", mInputDemuxer.get());
    return;
  }
  NS_WARNING("Not supported (yet)");
}

// We reset the demuxer by creating a new one and initializing it.
void TrackBuffersManager::ResetDemuxingState() {
  MOZ_ASSERT(OnTaskQueue());
  MOZ_ASSERT(mParser && mParser->HasInitData());
  AUTO_PROFILER_LABEL("TrackBuffersManager::ResetDemuxingState",
                      MEDIA_PLAYBACK);
  if (profiler_thread_is_being_profiled_for_markers()) {
    PROFILER_MARKER_UNTYPED("ResetDemuxingState", MEDIA_PLAYBACK);
  }

  RecreateParser(true);
  mCurrentInputBuffer = new SourceBufferResource();
  // The demuxer isn't initialized yet ; we don't want to notify it
  // that data has been appended yet ; so we simply append the init segment
  // to the resource.
  mCurrentInputBuffer->AppendData(mParser->InitData());
  CreateDemuxerforMIMEType();
  if (!mInputDemuxer) {
    RejectAppend(NS_ERROR_FAILURE, __func__);
    return;
  }
  mInputDemuxer->Init()
      ->Then(TaskQueueFromTaskQueue(), __func__, this,
             &TrackBuffersManager::OnDemuxerResetDone,
             &TrackBuffersManager::OnDemuxerInitFailed)
      ->Track(mDemuxerInitRequest);
}

void TrackBuffersManager::OnDemuxerResetDone(const MediaResult& aResult) {
  MOZ_ASSERT(OnTaskQueue());
  mDemuxerInitRequest.Complete();

  if (NS_FAILED(aResult) && StaticPrefs::media_playback_warnings_as_errors()) {
    RejectAppend(aResult, __func__);
    return;
  }

  // mInputDemuxer shouldn't have been destroyed while a demuxer init/reset
  // request was being processed. See bug 1239983.
  MOZ_DIAGNOSTIC_ASSERT(mInputDemuxer);

  if (aResult != NS_OK && mParentDecoder) {
    RefPtr<TrackBuffersManager> self = this;
    mAbstractMainThread->Dispatch(NS_NewRunnableFunction(
        "TrackBuffersManager::OnDemuxerResetDone", [self, aResult]() {
          if (self->mParentDecoder && self->mParentDecoder->GetOwner()) {
            self->mParentDecoder->GetOwner()->DecodeWarning(aResult);
          }
        }));
  }

  // Recreate track demuxers.
  uint32_t numVideos = mInputDemuxer->GetNumberTracks(TrackInfo::kVideoTrack);
  if (numVideos) {
    // We currently only handle the first video track.
    mVideoTracks.mDemuxer =
        mInputDemuxer->GetTrackDemuxer(TrackInfo::kVideoTrack, 0);
    MOZ_ASSERT(mVideoTracks.mDemuxer);
    DDLINKCHILD("video demuxer", mVideoTracks.mDemuxer.get());
  }

  uint32_t numAudios = mInputDemuxer->GetNumberTracks(TrackInfo::kAudioTrack);
  if (numAudios) {
    // We currently only handle the first audio track.
    mAudioTracks.mDemuxer =
        mInputDemuxer->GetTrackDemuxer(TrackInfo::kAudioTrack, 0);
    MOZ_ASSERT(mAudioTracks.mDemuxer);
    DDLINKCHILD("audio demuxer", mAudioTracks.mDemuxer.get());
  }

  if (mPendingInputBuffer) {
    // We had a partial media segment header stashed aside.
    // Reparse its content so we can continue parsing the current input buffer.
    TimeUnit start, end;
    mParser->ParseStartAndEndTimestamps(*mPendingInputBuffer, start, end);
    mProcessedInput += mPendingInputBuffer->Length();
  }

  SegmentParserLoop();
}

void TrackBuffersManager::AppendDataToCurrentInputBuffer(
    const MediaSpan& aData) {
  MOZ_ASSERT(mCurrentInputBuffer);
  mCurrentInputBuffer->AppendData(aData);
  mInputDemuxer->NotifyDataArrived();
}

void TrackBuffersManager::InitializationSegmentReceived() {
  MOZ_ASSERT(OnTaskQueue());
  MOZ_ASSERT(mParser->HasCompleteInitData());
  AUTO_PROFILER_LABEL("TrackBuffersManager::InitializationSegmentReceived",
                      MEDIA_PLAYBACK);
  if (profiler_thread_is_being_profiled_for_markers()) {
    PROFILER_MARKER_UNTYPED("InitializationSegmentReceived", MEDIA_PLAYBACK);
  }

  int64_t endInit = mParser->InitSegmentRange().mEnd;
  if (mInputBuffer->Length() > mProcessedInput ||
      int64_t(mProcessedInput - mInputBuffer->Length()) > endInit) {
    // Something is not quite right with the data appended. Refuse it.
    RejectAppend(MediaResult(NS_ERROR_FAILURE,
                             "Invalid state following initialization segment"),
                 __func__);
    return;
  }

  mCurrentInputBuffer = new SourceBufferResource();
  // The demuxer isn't initialized yet ; we don't want to notify it
  // that data has been appended yet ; so we simply append the init segment
  // to the resource.
  mCurrentInputBuffer->AppendData(mParser->InitData());
  uint32_t length = endInit - (mProcessedInput - mInputBuffer->Length());
  MOZ_RELEASE_ASSERT(length <= mInputBuffer->Length());
  mInputBuffer->RemoveFront(length);
  CreateDemuxerforMIMEType();
  if (!mInputDemuxer) {
    NS_WARNING("TODO type not supported");
    RejectAppend(NS_ERROR_DOM_NOT_SUPPORTED_ERR, __func__);
    return;
  }
  mInputDemuxer->Init()
      ->Then(TaskQueueFromTaskQueue(), __func__, this,
             &TrackBuffersManager::OnDemuxerInitDone,
             &TrackBuffersManager::OnDemuxerInitFailed)
      ->Track(mDemuxerInitRequest);
}

bool TrackBuffersManager::IsRepeatInitData(
    const MediaInfo& aNewMediaInfo) const {
  MOZ_ASSERT(OnTaskQueue());
  if (!mInitData) {
    // There is no previous init data, so this cannot be a repeat.
    return false;
  }

  if (mChangeTypeReceived) {
    // If we're received change type we want to reprocess init data.
    return false;
  }

  MOZ_DIAGNOSTIC_ASSERT(mInitData, "Init data should be non-null");
  if (*mInitData == *mParser->InitData()) {
    // We have previous init data, and it's the same binary data as we've just
    // parsed.
    return true;
  }

  // At this point the binary data doesn't match, but it's possible to have the
  // different binary representations for the same logical init data. These
  // checks can be revised as we encounter such cases in the wild.

  bool audioInfoIsRepeat = false;
  if (aNewMediaInfo.HasAudio()) {
    if (!mAudioTracks.mLastInfo) {
      // There is no old audio info, so this can't be a repeat.
      return false;
    }
    audioInfoIsRepeat =
        *mAudioTracks.mLastInfo->GetAsAudioInfo() == aNewMediaInfo.mAudio;
    if (!aNewMediaInfo.HasVideo()) {
      // Only have audio.
      return audioInfoIsRepeat;
    }
  }

  bool videoInfoIsRepeat = false;
  if (aNewMediaInfo.HasVideo()) {
    if (!mVideoTracks.mLastInfo) {
      // There is no old video info, so this can't be a repeat.
      return false;
    }
    videoInfoIsRepeat =
        *mVideoTracks.mLastInfo->GetAsVideoInfo() == aNewMediaInfo.mVideo;
    if (!aNewMediaInfo.HasAudio()) {
      // Only have video.
      return videoInfoIsRepeat;
    }
  }

  if (audioInfoIsRepeat && videoInfoIsRepeat) {
    MOZ_DIAGNOSTIC_ASSERT(
        aNewMediaInfo.HasVideo() && aNewMediaInfo.HasAudio(),
        "This should only be reachable if audio and video are present");
    // Video + audio are present and both have the same init data.
    return true;
  }

  return false;
}

void TrackBuffersManager::OnDemuxerInitDone(const MediaResult& aResult) {
  mTaskQueueCapability->AssertOnCurrentThread();
  MOZ_DIAGNOSTIC_ASSERT(mInputDemuxer, "mInputDemuxer has been destroyed");
  AUTO_PROFILER_LABEL("TrackBuffersManager::OnDemuxerInitDone", MEDIA_PLAYBACK);

  mDemuxerInitRequest.Complete();

  if (NS_FAILED(aResult) && StaticPrefs::media_playback_warnings_as_errors()) {
    RejectAppend(aResult, __func__);
    return;
  }

  MediaInfo info;

  uint32_t numVideos = mInputDemuxer->GetNumberTracks(TrackInfo::kVideoTrack);
  if (numVideos) {
    // We currently only handle the first video track.
    mVideoTracks.mDemuxer =
        mInputDemuxer->GetTrackDemuxer(TrackInfo::kVideoTrack, 0);
    MOZ_ASSERT(mVideoTracks.mDemuxer);
    DDLINKCHILD("video demuxer", mVideoTracks.mDemuxer.get());
    info.mVideo = *mVideoTracks.mDemuxer->GetInfo()->GetAsVideoInfo();
    info.mVideo.mTrackId = 2;
  }

  uint32_t numAudios = mInputDemuxer->GetNumberTracks(TrackInfo::kAudioTrack);
  if (numAudios) {
    // We currently only handle the first audio track.
    mAudioTracks.mDemuxer =
        mInputDemuxer->GetTrackDemuxer(TrackInfo::kAudioTrack, 0);
    MOZ_ASSERT(mAudioTracks.mDemuxer);
    DDLINKCHILD("audio demuxer", mAudioTracks.mDemuxer.get());
    info.mAudio = *mAudioTracks.mDemuxer->GetInfo()->GetAsAudioInfo();
    info.mAudio.mTrackId = 1;
  }

  TimeUnit videoDuration = numVideos ? info.mVideo.mDuration : TimeUnit::Zero();
  TimeUnit audioDuration = numAudios ? info.mAudio.mDuration : TimeUnit::Zero();

  TimeUnit duration = std::max(videoDuration, audioDuration);
  // 1. Update the duration attribute if it currently equals NaN.
  // Those steps are performed by the MediaSourceDecoder::SetInitialDuration
  mAbstractMainThread->Dispatch(NewRunnableMethod<TimeUnit>(
      "MediaSourceDecoder::SetInitialDuration", mParentDecoder.get(),
      &MediaSourceDecoder::SetInitialDuration,
      !duration.IsZero() ? duration : TimeUnit::FromInfinity()));

  // 2. If the initialization segment has no audio, video, or text tracks, then
  // run the append error algorithm with the decode error parameter set to true
  // and abort these steps.
  if (!numVideos && !numAudios) {
    RejectAppend(NS_ERROR_FAILURE, __func__);
    return;
  }

  // 3. If the first initialization segment received flag is true, then run the
  // following steps:
  if (mFirstInitializationSegmentReceived) {
    if (numVideos != mVideoTracks.mNumTracks ||
        numAudios != mAudioTracks.mNumTracks) {
      RejectAppend(NS_ERROR_FAILURE, __func__);
      return;
    }
    // 1. If more than one track for a single type are present (ie 2 audio
    // tracks), then the Track IDs match the ones in the first initialization
    // segment.
    // TODO
    // 2. Add the appropriate track descriptions from this initialization
    // segment to each of the track buffers.
    // TODO
    // 3. Set the need random access point flag on all track buffers to true.
    mVideoTracks.mNeedRandomAccessPoint = true;
    mAudioTracks.mNeedRandomAccessPoint = true;
  }

  // Check if we've received the same init data again. Some streams will
  // resend the same data. In these cases we don't need to change the stream
  // id as it's the same stream. Doing so would recreate decoders, possibly
  // leading to gaps in audio and/or video (see bug 1450952).
  bool isRepeatInitData = IsRepeatInitData(info);

  MOZ_ASSERT(mFirstInitializationSegmentReceived || !isRepeatInitData,
             "Should never detect repeat init data for first segment!");

  // If we have new init data we configure and set track info as needed. If we
  // have repeat init data we carry forward our existing track info.
  if (!isRepeatInitData) {
    // Increase our stream id.
    uint32_t streamID = sStreamSourceID++;

    // 4. Let active track flag equal false.
    bool activeTrack = false;

    // 5. If the first initialization segment received flag is false, then run
    // the following steps:
    if (!mFirstInitializationSegmentReceived) {
      MSE_DEBUG("Get first init data");
      mAudioTracks.mNumTracks = numAudios;
      // TODO:
      // 1. If the initialization segment contains tracks with codecs the user
      // agent does not support, then run the append error algorithm with the
      // decode error parameter set to true and abort these steps.

      // 2. For each audio track in the initialization segment, run following
      // steps: for (uint32_t i = 0; i < numAudios; i++) {
      if (numAudios) {
        // 1. Let audio byte stream track ID be the Track ID for the current
        // track being processed.
        // 2. Let audio language be a BCP 47 language tag for the language
        // specified in the initialization segment for this track or an empty
        // string if no language info is present.
        // 3. If audio language equals an empty string or the 'und' BCP 47
        // value, then run the default track language algorithm with
        // byteStreamTrackID set to audio byte stream track ID and type set to
        // "audio" and assign the value returned by the algorithm to audio
        // language.
        // 4. Let audio label be a label specified in the initialization segment
        // for this track or an empty string if no label info is present.
        // 5. If audio label equals an empty string, then run the default track
        // label algorithm with byteStreamTrackID set to audio byte stream track
        // ID and type set to "audio" and assign the value returned by the
        // algorithm to audio label.
        // 6. Let audio kinds be an array of kind strings specified in the
        // initialization segment for this track or an empty array if no kind
        // information is provided.
        // 7. If audio kinds equals an empty array, then run the default track
        // kinds algorithm with byteStreamTrackID set to audio byte stream track
        // ID and type set to "audio" and assign the value returned by the
        // algorithm to audio kinds.
        // 8. For each value in audio kinds, run the following steps:
        //   1. Let current audio kind equal the value from audio kinds for this
        //   iteration of the loop.
        //   2. Let new audio track be a new AudioTrack object.
        //   3. Generate a unique ID and assign it to the id property on new
        //   audio track.
        //   4. Assign audio language to the language property on new audio
        //   track.
        //   5. Assign audio label to the label property on new audio track.
        //   6. Assign current audio kind to the kind property on new audio
        //   track.
        //   7. If audioTracks.length equals 0, then run the following steps:
        //     1. Set the enabled property on new audio track to true.
        //     2. Set active track flag to true.
        activeTrack = true;
        //   8. Add new audio track to the audioTracks attribute on this
        //   SourceBuffer object.
        //   9. Queue a task to fire a trusted event named addtrack, that does
        //   not bubble and is not cancelable, and that uses the TrackEvent
        //   interface, at the AudioTrackList object referenced by the
        //   audioTracks attribute on this SourceBuffer object.
        //   10. Add new audio track to the audioTracks attribute on the
        //   HTMLMediaElement.
        //   11. Queue a task to fire a trusted event named addtrack, that does
        //   not bubble and is not cancelable, and that uses the TrackEvent
        //   interface, at the AudioTrackList object referenced by the
        //   audioTracks attribute on the HTMLMediaElement.
        mAudioTracks.mBuffers.AppendElement(TrackBuffer());
        // 10. Add the track description for this track to the track buffer.
        mAudioTracks.mInfo = new TrackInfoSharedPtr(info.mAudio, streamID);
        mAudioTracks.mLastInfo = mAudioTracks.mInfo;
      }

      mVideoTracks.mNumTracks = numVideos;
      // 3. For each video track in the initialization segment, run following
      // steps: for (uint32_t i = 0; i < numVideos; i++) {
      if (numVideos) {
        // 1. Let video byte stream track ID be the Track ID for the current
        // track being processed.
        // 2. Let video language be a BCP 47 language tag for the language
        // specified in the initialization segment for this track or an empty
        // string if no language info is present.
        // 3. If video language equals an empty string or the 'und' BCP 47
        // value, then run the default track language algorithm with
        // byteStreamTrackID set to video byte stream track ID and type set to
        // "video" and assign the value returned by the algorithm to video
        // language.
        // 4. Let video label be a label specified in the initialization segment
        // for this track or an empty string if no label info is present.
        // 5. If video label equals an empty string, then run the default track
        // label algorithm with byteStreamTrackID set to video byte stream track
        // ID and type set to "video" and assign the value returned by the
        // algorithm to video label.
        // 6. Let video kinds be an array of kind strings specified in the
        // initialization segment for this track or an empty array if no kind
        // information is provided.
        // 7. If video kinds equals an empty array, then run the default track
        // kinds algorithm with byteStreamTrackID set to video byte stream track
        // ID and type set to "video" and assign the value returned by the
        // algorithm to video kinds.
        // 8. For each value in video kinds, run the following steps:
        //   1. Let current video kind equal the value from video kinds for this
        //   iteration of the loop.
        //   2. Let new video track be a new VideoTrack object.
        //   3. Generate a unique ID and assign it to the id property on new
        //   video track.
        //   4. Assign video language to the language property on new video
        //   track.
        //   5. Assign video label to the label property on new video track.
        //   6. Assign current video kind to the kind property on new video
        //   track.
        //   7. If videoTracks.length equals 0, then run the following steps:
        //     1. Set the selected property on new video track to true.
        //     2. Set active track flag to true.
        activeTrack = true;
        //   8. Add new video track to the videoTracks attribute on this
        //   SourceBuffer object.
        //   9. Queue a task to fire a trusted event named addtrack, that does
        //   not bubble and is not cancelable, and that uses the TrackEvent
        //   interface, at the VideoTrackList object referenced by the
        //   videoTracks attribute on this SourceBuffer object.
        //   10. Add new video track to the videoTracks attribute on the
        //   HTMLMediaElement.
        //   11. Queue a task to fire a trusted event named addtrack, that does
        //   not bubble and is not cancelable, and that uses the TrackEvent
        //   interface, at the VideoTrackList object referenced by the
        //   videoTracks attribute on the HTMLMediaElement.
        mVideoTracks.mBuffers.AppendElement(TrackBuffer());
        // 10. Add the track description for this track to the track buffer.
        mVideoTracks.mInfo = new TrackInfoSharedPtr(info.mVideo, streamID);
        mVideoTracks.mLastInfo = mVideoTracks.mInfo;
      }
      // 4. For each text track in the initialization segment, run following
      // steps:
      // 5. If active track flag equals true, then run the following steps:
      // This is handled by SourceBuffer once the promise is resolved.
      if (activeTrack) {
        mActiveTrack = true;
      }

      // 6. Set first initialization segment received flag to true.
      mFirstInitializationSegmentReceived = true;
    } else {
      MSE_DEBUG("Get new init data");
      mAudioTracks.mLastInfo = new TrackInfoSharedPtr(info.mAudio, streamID);
      mVideoTracks.mLastInfo = new TrackInfoSharedPtr(info.mVideo, streamID);
    }

    UniquePtr<EncryptionInfo> crypto = mInputDemuxer->GetCrypto();
    if (crypto && crypto->IsEncrypted()) {
      // Try and dispatch 'encrypted'. Won't go if ready state still
      // HAVE_NOTHING.
      for (uint32_t i = 0; i < crypto->mInitDatas.Length(); i++) {
        nsCOMPtr<nsIRunnable> r = new DispatchKeyNeededEvent(
            mParentDecoder, crypto->mInitDatas[i].mInitData,
            crypto->mInitDatas[i].mType);
        mAbstractMainThread->Dispatch(r.forget());
      }
      info.mCrypto = *crypto;
      // We clear our crypto init data array, so the MediaFormatReader will
      // not emit an encrypted event for the same init data again.
      info.mCrypto.mInitDatas.Clear();
    }

    {
      MutexAutoLock mut(mMutex);
      mInfo = std::move(info);
    }
  }
  // We now have a valid init data ; we can store it for later use.
  mInitData = mParser->InitData();

  // We have now completed the changeType operation.
  mChangeTypeReceived = false;

  // 3. Remove the initialization segment bytes from the beginning of the input
  // buffer. This step has already been done in InitializationSegmentReceived
  // when we transferred the content into mCurrentInputBuffer.
  mCurrentInputBuffer->EvictAll();
  mInputDemuxer->NotifyDataRemoved();
  RecreateParser(true);

  // 4. Set append state to WAITING_FOR_SEGMENT.
  SetAppendState(AppendState::WAITING_FOR_SEGMENT);
  // 5. Jump to the loop top step above.
  ScheduleSegmentParserLoop();

  if (aResult != NS_OK && mParentDecoder) {
    RefPtr<TrackBuffersManager> self = this;
    mAbstractMainThread->Dispatch(NS_NewRunnableFunction(
        "TrackBuffersManager::OnDemuxerInitDone", [self, aResult]() {
          if (self->mParentDecoder && self->mParentDecoder->GetOwner()) {
            self->mParentDecoder->GetOwner()->DecodeWarning(aResult);
          }
        }));
  }
}

void TrackBuffersManager::OnDemuxerInitFailed(const MediaResult& aError) {
  mTaskQueueCapability->AssertOnCurrentThread();
  MSE_DEBUG("");
  MOZ_ASSERT(aError != NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA);
  mDemuxerInitRequest.Complete();

  RejectAppend(aError, __func__);
}

RefPtr<TrackBuffersManager::CodedFrameProcessingPromise>
TrackBuffersManager::CodedFrameProcessing() {
  MOZ_ASSERT(OnTaskQueue());
  MOZ_ASSERT(mProcessingPromise.IsEmpty());
  AUTO_PROFILER_LABEL("TrackBuffersManager::CodedFrameProcessing",
                      MEDIA_PLAYBACK);

  MediaByteRange mediaRange = mParser->MediaSegmentRange();
  if (mediaRange.IsEmpty()) {
    AppendDataToCurrentInputBuffer(*mInputBuffer);
    mInputBuffer.reset();
  } else {
    MOZ_ASSERT(mProcessedInput >= mInputBuffer->Length());
    if (int64_t(mProcessedInput - mInputBuffer->Length()) > mediaRange.mEnd) {
      // Something is not quite right with the data appended. Refuse it.
      // This would typically happen if the previous media segment was partial
      // yet a new complete media segment was added.
      return CodedFrameProcessingPromise::CreateAndReject(NS_ERROR_FAILURE,
                                                          __func__);
    }
    // The mediaRange is offset by the init segment position previously added.
    uint32_t length =
        mediaRange.mEnd - (mProcessedInput - mInputBuffer->Length());
    if (!length) {
      // We've completed our earlier media segment and no new data is to be
      // processed. This happens with some containers that can't detect that a
      // media segment is ending until a new one starts.
      RefPtr<CodedFrameProcessingPromise> p =
          mProcessingPromise.Ensure(__func__);
      CompleteCodedFrameProcessing();
      return p;
    }
    AppendDataToCurrentInputBuffer(mInputBuffer->To(length));
    mInputBuffer->RemoveFront(length);
  }

  RefPtr<CodedFrameProcessingPromise> p = mProcessingPromise.Ensure(__func__);

  DoDemuxVideo();

  return p;
}

void TrackBuffersManager::OnDemuxFailed(TrackType aTrack,
                                        const MediaResult& aError) {
  MOZ_ASSERT(OnTaskQueue());
  MSE_DEBUG("Failed to demux {}, failure:{}",
            aTrack == TrackType::kVideoTrack ? "video" : "audio",
            aError.ErrorName().get());
  switch (aError.Code()) {
    case NS_ERROR_DOM_MEDIA_END_OF_STREAM:
    case NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA:
      if (aTrack == TrackType::kVideoTrack) {
        DoDemuxAudio();
      } else {
        CompleteCodedFrameProcessing();
      }
      break;
    default:
      // https://w3c.github.io/media-source/#sourcebuffer-segment-parser-loop
      // 2. If the [[input buffer]] contains bytes that violate the
      //    SourceBuffer byte stream format specification, then run the append
      //    error algorithm and abort this algorithm.
      RejectProcessing(aError, __func__);
      break;
  }
}

void TrackBuffersManager::DoDemuxVideo() {
  MOZ_ASSERT(OnTaskQueue());
  if (!HasVideo()) {
    DoDemuxAudio();
    return;
  }
  mVideoTracks.mDemuxer->GetSamples(-1)
      ->Then(TaskQueueFromTaskQueue(), __func__, this,
             &TrackBuffersManager::OnVideoDemuxCompleted,
             &TrackBuffersManager::OnVideoDemuxFailed)
      ->Track(mVideoTracks.mDemuxRequest);
}

void TrackBuffersManager::MaybeDispatchEncryptedEvent(
    const nsTArray<RefPtr<MediaRawData>>& aSamples) {
  // Try and dispatch 'encrypted'. Won't go if ready state still HAVE_NOTHING.
  for (const RefPtr<MediaRawData>& sample : aSamples) {
    for (const nsTArray<uint8_t>& initData : sample->mCrypto.mInitDatas) {
      nsCOMPtr<nsIRunnable> r = new DispatchKeyNeededEvent(
          mParentDecoder, initData, sample->mCrypto.mInitDataType);
      mAbstractMainThread->Dispatch(r.forget());
    }
  }
}

void TrackBuffersManager::OnVideoDemuxCompleted(
    const RefPtr<MediaTrackDemuxer::SamplesHolder>& aSamples) {
  mTaskQueueCapability->AssertOnCurrentThread();
  mVideoTracks.mDemuxRequest.Complete();
  mVideoTracks.mQueuedSamples.AppendElements(aSamples->GetSamples());
  MSE_DEBUG("{} video samples demuxed, queued-sz={}",
            aSamples->GetSamples().Length(),
            mVideoTracks.mQueuedSamples.Length());

  MaybeDispatchEncryptedEvent(aSamples->GetSamples());
  DoDemuxAudio();
}

void TrackBuffersManager::DoDemuxAudio() {
  MOZ_ASSERT(OnTaskQueue());
  if (!HasAudio()) {
    CompleteCodedFrameProcessing();
    return;
  }
  mAudioTracks.mDemuxer->GetSamples(-1)
      ->Then(TaskQueueFromTaskQueue(), __func__, this,
             &TrackBuffersManager::OnAudioDemuxCompleted,
             &TrackBuffersManager::OnAudioDemuxFailed)
      ->Track(mAudioTracks.mDemuxRequest);
}

void TrackBuffersManager::OnAudioDemuxCompleted(
    const RefPtr<MediaTrackDemuxer::SamplesHolder>& aSamples) {
  mTaskQueueCapability->AssertOnCurrentThread();
  MSE_DEBUG("{} audio samples demuxed", aSamples->GetSamples().Length());
  // When using MSE, it's possible for each fragments to have their own
  // duration, with a duration that is incorrectly rounded. Ignore the trimming
  // information set by the demuxer to ensure a continous playback.
  for (const auto& sample : aSamples->GetSamples()) {
    sample->mOriginalPresentationWindow = Nothing();
  }
  mAudioTracks.mDemuxRequest.Complete();
  mAudioTracks.mQueuedSamples.AppendElements(aSamples->GetSamples());
  CompleteCodedFrameProcessing();

  MaybeDispatchEncryptedEvent(aSamples->GetSamples());
}

void TrackBuffersManager::CompleteCodedFrameProcessing() {
  MOZ_ASSERT(OnTaskQueue());
  AUTO_PROFILER_LABEL("TrackBuffersManager::CompleteCodedFrameProcessing",
                      MEDIA_PLAYBACK);

  // 1. For each coded frame in the media segment run the following steps:
  // Coded Frame Processing steps 1.1 to 1.21.

  if (mSourceBufferAttributes->GetAppendMode() ==
          SourceBufferAppendMode::Sequence &&
      mVideoTracks.mQueuedSamples.Length() &&
      mAudioTracks.mQueuedSamples.Length()) {
    // When we are in sequence mode, the order in which we process the frames is
    // important as it determines the future value of timestampOffset.
    // So we process the earliest sample first. See bug 1293576.
    TimeInterval videoInterval =
        PresentationInterval(mVideoTracks.mQueuedSamples);
    TimeInterval audioInterval =
        PresentationInterval(mAudioTracks.mQueuedSamples);
    if (audioInterval.mStart < videoInterval.mStart) {
      ProcessFrames(mAudioTracks.mQueuedSamples, mAudioTracks);
      ProcessFrames(mVideoTracks.mQueuedSamples, mVideoTracks);
    } else {
      ProcessFrames(mVideoTracks.mQueuedSamples, mVideoTracks);
      ProcessFrames(mAudioTracks.mQueuedSamples, mAudioTracks);
    }
  } else {
    ProcessFrames(mVideoTracks.mQueuedSamples, mVideoTracks);
    ProcessFrames(mAudioTracks.mQueuedSamples, mAudioTracks);
  }

#if defined(DEBUG)
  if (HasVideo()) {
    const auto& track = mVideoTracks.GetTrackBuffer();
    MOZ_ASSERT(track.IsEmpty() || track[0]->mKeyframe);
    for (uint32_t i = 1; i < track.Length(); i++) {
      MOZ_ASSERT(
          (track[i - 1]->mTrackInfo->GetID() == track[i]->mTrackInfo->GetID() &&
           track[i - 1]->mTimecode <= track[i]->mTimecode) ||
          track[i]->mKeyframe);
    }
  }
  if (HasAudio()) {
    const auto& track = mAudioTracks.GetTrackBuffer();
    MOZ_ASSERT(track.IsEmpty() || track[0]->mKeyframe);
    for (uint32_t i = 1; i < track.Length(); i++) {
      MOZ_ASSERT(
          (track[i - 1]->mTrackInfo->GetID() == track[i]->mTrackInfo->GetID() &&
           track[i - 1]->mTimecode <= track[i]->mTimecode) ||
          track[i]->mKeyframe);
    }
  }
#endif

  mVideoTracks.mQueuedSamples.Clear();
  mAudioTracks.mQueuedSamples.Clear();

  UpdateBufferedRanges();

  // Update our reported total size.
  mSizeSourceBuffer = mVideoTracks.mSizeBuffer + mAudioTracks.mSizeBuffer;

  // Return to step 6.4 of Segment Parser Loop algorithm
  // 4. If this SourceBuffer is full and cannot accept more media data, then set
  // the buffer full flag to true.
  if (mSizeSourceBuffer >= EvictionThreshold()) {
    mBufferFull = true;
  }

  // 5. If the input buffer does not contain a complete media segment, then jump
  // to the need more data step below.
  if (mParser->MediaSegmentRange().IsEmpty()) {
    ResolveProcessing(true, __func__);
    return;
  }

  mLastParsedEndTime = Some(std::max(mAudioTracks.mLastParsedEndTime,
                                     mVideoTracks.mLastParsedEndTime));

  // 6. Remove the media segment bytes from the beginning of the input buffer.
  // Clear our demuxer from any already processed data.
  int64_t safeToEvict =
      std::min(HasVideo() ? mVideoTracks.mDemuxer->GetEvictionOffset(
                                mVideoTracks.mLastParsedEndTime)
                          : INT64_MAX,
               HasAudio() ? mAudioTracks.mDemuxer->GetEvictionOffset(
                                mAudioTracks.mLastParsedEndTime)
                          : INT64_MAX);
  mCurrentInputBuffer->EvictBefore(safeToEvict);

  mInputDemuxer->NotifyDataRemoved();
  RecreateParser(true);

  // 7. Set append state to WAITING_FOR_SEGMENT.
  SetAppendState(AppendState::WAITING_FOR_SEGMENT);

  // 8. Jump to the loop top step above.
  ResolveProcessing(false, __func__);
}

void TrackBuffersManager::RejectProcessing(const MediaResult& aRejectValue,
                                           const char* aName) {
  mProcessingPromise.RejectIfExists(aRejectValue, __func__);
}

void TrackBuffersManager::ResolveProcessing(bool aResolveValue,
                                            const char* aName) {
  mProcessingPromise.ResolveIfExists(aResolveValue, __func__);
}

void TrackBuffersManager::CheckSequenceDiscontinuity(
    const TimeUnit& aPresentationTime) {
  if (mSourceBufferAttributes->GetAppendMode() ==
          SourceBufferAppendMode::Sequence &&
      mSourceBufferAttributes->HaveGroupStartTimestamp()) {
    mSourceBufferAttributes->SetTimestampOffset(
        mSourceBufferAttributes->GetGroupStartTimestamp() - aPresentationTime);
    mSourceBufferAttributes->SetGroupEndTimestamp(
        mSourceBufferAttributes->GetGroupStartTimestamp());
    mVideoTracks.mNeedRandomAccessPoint = true;
    mAudioTracks.mNeedRandomAccessPoint = true;
    mSourceBufferAttributes->ResetGroupStartTimestamp();
  }
}

TimeInterval TrackBuffersManager::PresentationInterval(
    const TrackBuffer& aSamples) const {
  TimeInterval presentationInterval =
      TimeInterval(aSamples[0]->mTime, aSamples[0]->GetEndTime());

  for (uint32_t i = 1; i < aSamples.Length(); i++) {
    const auto& sample = aSamples[i];
    presentationInterval = presentationInterval.Span(
        TimeInterval(sample->mTime, sample->GetEndTime()));
  }
  return presentationInterval;
}

void TrackBuffersManager::ProcessFrames(TrackBuffer& aSamples,
                                        TrackData& aTrackData) {
  AUTO_PROFILER_LABEL("TrackBuffersManager::ProcessFrames", MEDIA_PLAYBACK);
  if (!aSamples.Length()) {
    return;
  }

  // 1. If generate timestamps flag equals true
  // Let presentation timestamp equal 0.
  // Otherwise
  // Let presentation timestamp be a double precision floating point
  // representation of the coded frame's presentation timestamp in seconds.
  TimeUnit presentationTimestamp = mSourceBufferAttributes->mGenerateTimestamps
                                       ? TimeUnit::Zero()
                                       : aSamples[0]->mTime;

  // 3. If mode equals "sequence" and group start timestamp is set, then run the
  // following steps:
  CheckSequenceDiscontinuity(presentationTimestamp);

  // 5. Let track buffer equal the track buffer that the coded frame will be
  // added to.
  auto& trackBuffer = aTrackData;

  TimeIntervals samplesRange;
  uint32_t sizeNewSamples = 0;
  TrackBuffer samples;  // array that will contain the frames to be added
                        // to our track buffer.

  // We assume that no frames are contiguous within a media segment and as such
  // don't need to check for discontinuity except for the first frame and should
  // a frame be ignored due to the target window.
  bool needDiscontinuityCheck = true;

  // Highest presentation time seen in samples block.
  TimeUnit highestSampleTime;

  if (aSamples.Length()) {
    aTrackData.mLastParsedEndTime = TimeUnit();
  }

  auto addToSamples = [&](MediaRawData* aSample,
                          const TimeInterval& aInterval) {
    aSample->mTime = aInterval.mStart;
    aSample->mDuration = aInterval.Length();
    aSample->mTrackInfo = trackBuffer.mLastInfo;
    SAMPLE_DEBUGV(
        "Add sample [{}{},{}{}] by interval {}",
        aSample->mTime.ToMicroseconds(), aSample->mTime.ToString().get(),
        aSample->GetEndTime().ToMicroseconds(),
        aSample->GetEndTime().ToString().get(), aInterval.ToString().get());
    MOZ_DIAGNOSTIC_ASSERT(aSample->HasValidTime());
    MOZ_DIAGNOSTIC_ASSERT(TimeInterval(aSample->mTime, aSample->GetEndTime()) ==
                          aInterval);
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
    auto oldRangeEnd = samplesRange.GetEnd();
#endif
    samplesRange += aInterval;
    // For debug purpose, if the sample range grows, it should match the
    // sample's end time.
    MOZ_DIAGNOSTIC_ASSERT_IF(samplesRange.GetEnd() > oldRangeEnd,
                             samplesRange.GetEnd() == aSample->GetEndTime());
    sizeNewSamples += aSample->ComputedSizeOfIncludingThis();
    samples.AppendElement(aSample);
  };

  // Will be set to the last frame dropped due to being outside mAppendWindow.
  // It will be added prior the first following frame which can be added to the
  // track buffer.
  // This sample will be set with a duration of only 1us which will cause it to
  // be dropped once returned by the decoder.
  // This sample is required to "prime" the decoder so that the following frame
  // can be fully decoded.
  RefPtr<MediaRawData> previouslyDroppedSample;
  for (auto& sample : aSamples) {
    const TimeUnit sampleEndTime = sample->GetEndTime();
    if (sampleEndTime > aTrackData.mLastParsedEndTime) {
      aTrackData.mLastParsedEndTime = sampleEndTime;
    }

    // We perform step 10 right away as we can't do anything should a keyframe
    // be needed until we have one.

    // 10. If the need random access point flag on track buffer equals true,
    // then run the following steps:
    if (trackBuffer.mNeedRandomAccessPoint) {
      // 1. If the coded frame is not a random access point, then drop the coded
      // frame and jump to the top of the loop to start processing the next
      // coded frame.
      if (!sample->mKeyframe) {
        previouslyDroppedSample = nullptr;
        nsPrintfCString msg("skipping sample [%" PRId64 ",%" PRId64 "]",
                            sample->mTime.ToMicroseconds(),
                            sample->GetEndTime().ToMicroseconds());
        if (profiler_thread_is_being_profiled_for_markers()) {
          PROFILER_MARKER_TEXT("Skipping Frame", MEDIA_PLAYBACK, {}, msg);
        }
        SAMPLE_DEBUGV("{}", msg.get());
        continue;
      }
      // 2. Set the need random access point flag on track buffer to false.
      trackBuffer.mNeedRandomAccessPoint = false;
    }

    // We perform step 1,2 and 4 at once:
    // 1. If generate timestamps flag equals true:
    //   Let presentation timestamp equal 0.
    //   Let decode timestamp equal 0.
    // Otherwise:
    //   Let presentation timestamp be a double precision floating point
    //   representation of the coded frame's presentation timestamp in seconds.
    //   Let decode timestamp be a double precision floating point
    //   representation of the coded frame's decode timestamp in seconds.

    // 2. Let frame duration be a double precision floating point representation
    // of the coded frame's duration in seconds. Step 3 is performed earlier or
    // when a discontinuity has been detected.
    // 4. If timestampOffset is not 0, then run the following steps:

    TimeUnit sampleTime = sample->mTime;
    TimeUnit sampleTimecode = sample->mTimecode;
    TimeUnit sampleDuration = sample->mDuration;
    // Keep the timestamp, set by js, in the time base of the container.
    TimeUnit timestampOffset =
        mSourceBufferAttributes->GetTimestampOffset().ToBase(sample->mTime);

    TimeUnit intervalStart;
    TimeUnit intervalEnd;
    TimeUnit decodeTimestamp;

    if (mSourceBufferAttributes->mGenerateTimestamps) {
      intervalStart = timestampOffset;
      intervalEnd = timestampOffset + sampleDuration;
      decodeTimestamp = timestampOffset;
    } else {
      intervalStart = timestampOffset + sampleTime;
      intervalEnd = timestampOffset + sampleTime + sampleDuration;
      decodeTimestamp = timestampOffset + sampleTimecode;
    }

    if (!intervalStart.IsValid() || !intervalEnd.IsValid() ||
        !decodeTimestamp.IsValid()) {
      SAMPLE_DEBUG(
          "Skipping sample with invalid timestamp after applying offset "
          "(intervalStart valid: {}, intervalEnd valid: {}, decodeTimestamp "
          "valid: {})",
          intervalStart.IsValid() ? "yes" : "no",
          intervalEnd.IsValid() ? "yes" : "no",
          decodeTimestamp.IsValid() ? "yes" : "no");
      continue;
    }

    TimeInterval sampleInterval(intervalStart, intervalEnd);

    SAMPLE_DEBUG(
        "Processing {} frame [{}{},{}{}] (adjusted:[{}{},{}{}]), dts:{}, "
        "duration:{}, kf:{})",
        aTrackData.mInfo->mMimeType.get(), sample->mTime.ToMicroseconds(),
        sample->mTime.ToString().get(), sample->GetEndTime().ToMicroseconds(),
        sample->GetEndTime().ToString().get(),
        sampleInterval.mStart.ToMicroseconds(),
        sampleInterval.mStart.ToString().get(),
        sampleInterval.mEnd.ToMicroseconds(),
        sampleInterval.mEnd.ToString().get(),
        sample->mTimecode.ToMicroseconds(), sample->mDuration.ToMicroseconds(),
        sample->mKeyframe);

    // 6. If last decode timestamp for track buffer is set and decode timestamp
    // is less than last decode timestamp: OR If last decode timestamp for track
    // buffer is set and the difference between decode timestamp and last decode
    // timestamp is greater than 2 times last frame duration:
    if (needDiscontinuityCheck && trackBuffer.mLastDecodeTimestamp.isSome() &&
        (decodeTimestamp < trackBuffer.mLastDecodeTimestamp.ref() ||
         (decodeTimestamp - trackBuffer.mLastDecodeTimestamp.ref() >
          trackBuffer.mLongestFrameDuration * 2))) {
      if (profiler_thread_is_being_profiled_for_markers()) {
        PROFILER_MARKER_UNTYPED("Discontinuity detected", MEDIA_PLAYBACK);
      }
      MSE_DEBUG("Discontinuity detected.");
      SourceBufferAppendMode appendMode =
          mSourceBufferAttributes->GetAppendMode();

      // 1a. If mode equals "segments":
      if (appendMode == SourceBufferAppendMode::Segments) {
        // Set group end timestamp to presentation timestamp.
        mSourceBufferAttributes->SetGroupEndTimestamp(sampleInterval.mStart);
      }
      // 1b. If mode equals "sequence":
      if (appendMode == SourceBufferAppendMode::Sequence) {
        // Set group start timestamp equal to the group end timestamp.
        mSourceBufferAttributes->SetGroupStartTimestamp(
            mSourceBufferAttributes->GetGroupEndTimestamp());
      }
      for (auto& track : GetTracksList()) {
        // 2. Unset the last decode timestamp on all track buffers.
        // 3. Unset the last frame duration on all track buffers.
        // 4. Unset the highest end timestamp on all track buffers.
        // 5. Set the need random access point flag on all track buffers to
        // true.
        MSE_DEBUG("Resetting append state");
        track->ResetAppendState();
      }
      // 6. Jump to the Loop Top step above to restart processing of the current
      // coded frame. Rather that restarting the process for the frame, we run
      // the first steps again instead.
      // 3. If mode equals "sequence" and group start timestamp is set, then run
      // the following steps:
      TimeUnit presentationTimestamp =
          mSourceBufferAttributes->mGenerateTimestamps ? TimeUnit()
                                                       : sampleTime;
      CheckSequenceDiscontinuity(presentationTimestamp);

      if (!sample->mKeyframe) {
        previouslyDroppedSample = nullptr;
        continue;
      }
      if (appendMode == SourceBufferAppendMode::Sequence) {
        // mSourceBufferAttributes->GetTimestampOffset() was modified during
        // CheckSequenceDiscontinuity. We need to update our variables.
        timestampOffset =
            mSourceBufferAttributes->GetTimestampOffset().ToBase(sample->mTime);
        sampleInterval =
            mSourceBufferAttributes->mGenerateTimestamps
                ? TimeInterval(timestampOffset,
                               timestampOffset + sampleDuration)
                : TimeInterval(timestampOffset + sampleTime,
                               timestampOffset + sampleTime + sampleDuration);
        decodeTimestamp = mSourceBufferAttributes->mGenerateTimestamps
                              ? timestampOffset
                              : timestampOffset + sampleTimecode;
      }
      trackBuffer.mNeedRandomAccessPoint = false;
      needDiscontinuityCheck = false;
    }

    // 7. Let frame end timestamp equal the sum of presentation timestamp and
    // frame duration.
    // "presentation timestamp" and "frame duration" are double precision
    // representations.
    Interval<double> frameStartAndEnd(
        sampleInterval.mStart.ToSeconds(),
        sampleInterval.mStart.ToSeconds() + sampleDuration.ToSeconds());
    // 8. If presentation timestamp is less than appendWindowStart, then set the
    // need random access point flag to true, drop the coded frame, and jump to
    // the top of the loop to start processing the next coded frame.
    // 9. If frame end timestamp is greater than appendWindowEnd, then set the
    // need random access point flag to true, drop the coded frame, and jump to
    // the top of the loop to start processing the next coded frame.
    if (!mAppendWindow.ContainsStrict(frameStartAndEnd)) {
      // Calculate the intersection using the same denominator as will be used
      // for the sample time and duration so that a truncated sample is
      // collected only if its truncated duration would be non-zero.
      TimeInterval appendWindow(
          TimeUnit::FromSecondsWithBaseOf(mAppendWindow.mStart, sampleTime),
          TimeUnit::FromSecondsWithBaseOf(mAppendWindow.mEnd, sampleTime));
      if (appendWindow.IntersectsStrict(sampleInterval)) {
        // 8. Note: Some implementations MAY choose to collect some of these
        //    coded frames with presentation timestamp less than
        //    appendWindowStart and use them to generate a splice at the first
        //    coded frame that has a presentation timestamp greater than or
        //    equal to appendWindowStart even if that frame is not a random
        //    access point. Supporting this requires multiple decoders or faster
        //    than real-time decoding so for now this behavior will not be a
        //    normative requirement.
        // 9. Note: Some implementations MAY choose to collect coded frames with
        //    presentation timestamp less than appendWindowEnd and frame end
        //    timestamp greater than appendWindowEnd and use them to generate a
        //    splice across the portion of the collected coded frames within the
        //    append window at time of collection, and the beginning portion of
        //    later processed frames which only partially overlap the end of the
        //    collected coded frames. Supporting this requires multiple decoders
        //    or faster than real-time decoding so for now this behavior will
        //    not be a normative requirement. In conjunction with collecting
        //    coded frames that span appendWindowStart, implementations MAY thus
        //    support gapless audio splicing.
        TimeInterval intersection = appendWindow.Intersection(sampleInterval);
        sample->mOriginalPresentationWindow = Some(sampleInterval);
        MSE_DEBUGV("will truncate frame from [{}{},{}{}] to [{}{},{}{}]",
                   sampleInterval.mStart.ToMicroseconds(),
                   sampleInterval.mStart.ToString().get(),
                   sampleInterval.mEnd.ToMicroseconds(),
                   sampleInterval.mEnd.ToString().get(),
                   intersection.mStart.ToMicroseconds(),
                   intersection.mStart.ToString().get(),
                   intersection.mEnd.ToMicroseconds(),
                   intersection.mEnd.ToString().get());
        sampleInterval = intersection;
      } else {
        sample->mOriginalPresentationWindow = Some(sampleInterval);
        sample->mTimecode = decodeTimestamp;
        previouslyDroppedSample = sample;
        MSE_DEBUGV(
            "frame [{}{},{}{}] outside appendWindow [{}.6{},{}.6{}] "
            "dropping",
            sampleInterval.mStart.ToMicroseconds(),
            sampleInterval.mStart.ToString().get(),
            sampleInterval.mEnd.ToMicroseconds(),
            sampleInterval.mEnd.ToString().get(), mAppendWindow.mStart,
            appendWindow.mStart.ToString().get(), mAppendWindow.mEnd,
            appendWindow.mEnd.ToString().get());
        if (samples.Length()) {
          // We are creating a discontinuity in the samples.
          // Insert the samples processed so far.
          InsertFrames(samples, samplesRange, trackBuffer);
          samples.Clear();
          samplesRange = TimeIntervals();
          trackBuffer.mSizeBuffer += sizeNewSamples;
          sizeNewSamples = 0;
          UpdateHighestTimestamp(trackBuffer, highestSampleTime);
        }
        trackBuffer.mNeedRandomAccessPoint = true;
        needDiscontinuityCheck = true;
        // The coded frame processing algorithm only updates timestampOffset
        // for generated timestamps after keeping a frame, so every frame
        // following one dropped before appendWindowStart would be dropped too.
        // Deliberately deviate from it as Chrome does, advancing past such
        // frames. The last decode timestamp advances too, so that no
        // discontinuity is detected among them, which would rewind
        // timestampOffset (unlike Chrome). Frames dropped after
        // appendWindowEnd still follow the spec (also unlike Chrome), so a
        // following append continues from the window's end.
        if (mSourceBufferAttributes->mGenerateTimestamps &&
            sampleInterval.mEnd <= appendWindow.mStart) {
          mSourceBufferAttributes->SetTimestampOffset(sampleInterval.mEnd);
          if (trackBuffer.mLastDecodeTimestamp.isSome()) {
            trackBuffer.mLastDecodeTimestamp = Some(decodeTimestamp);
          }
        }
        continue;
      }
    }
    if (previouslyDroppedSample) {
      MSE_DEBUGV("Adding silent frame");
      // This "silent" sample will be added so that it starts exactly before the
      // first usable one. The duration of the actual sample will be adjusted so
      // that the total duration stay the same. This sample will be dropped
      // after decoding by the AudioTrimmer (if audio).
      TimeInterval previouslyDroppedSampleInterval =
          TimeInterval(sampleInterval.mStart, sampleInterval.mStart);
      addToSamples(previouslyDroppedSample, previouslyDroppedSampleInterval);
      previouslyDroppedSample = nullptr;
      sampleInterval.mStart += previouslyDroppedSampleInterval.Length();
    }

    sample->mTimecode = decodeTimestamp;
    addToSamples(sample, sampleInterval);

    // Steps 11,12,13,14, 15 and 16 will be done in one block in InsertFrames.

    trackBuffer.mLongestFrameDuration =
        trackBuffer.mLastFrameDuration.isSome()
            ? sample->mKeyframe
                  ? sampleDuration
                  : std::max(sampleDuration, trackBuffer.mLongestFrameDuration)
            : sampleDuration;

    // 17. Set last decode timestamp for track buffer to decode timestamp.
    trackBuffer.mLastDecodeTimestamp = Some(decodeTimestamp);
    // 18. Set last frame duration for track buffer to frame duration.
    trackBuffer.mLastFrameDuration = Some(sampleDuration);

    // 19. If highest end timestamp for track buffer is unset or frame end
    // timestamp is greater than highest end timestamp, then set highest end
    // timestamp for track buffer to frame end timestamp.
    if (trackBuffer.mHighestEndTimestamp.isNothing() ||
        sampleInterval.mEnd > trackBuffer.mHighestEndTimestamp.ref()) {
      trackBuffer.mHighestEndTimestamp = Some(sampleInterval.mEnd);
    }
    if (sampleInterval.mStart > highestSampleTime) {
      highestSampleTime = sampleInterval.mStart;
    }
    // 20. If frame end timestamp is greater than group end timestamp, then set
    // group end timestamp equal to frame end timestamp.
    if (sampleInterval.mEnd > mSourceBufferAttributes->GetGroupEndTimestamp()) {
      mSourceBufferAttributes->SetGroupEndTimestamp(sampleInterval.mEnd);
    }
    // 21. If generate timestamps flag equals true, then set timestampOffset
    // equal to frame end timestamp.
    if (mSourceBufferAttributes->mGenerateTimestamps) {
      mSourceBufferAttributes->SetTimestampOffset(sampleInterval.mEnd);
    }
  }

  if (samples.Length()) {
    InsertFrames(samples, samplesRange, trackBuffer);
    trackBuffer.mSizeBuffer += sizeNewSamples;
    UpdateHighestTimestamp(trackBuffer, highestSampleTime);
  }
}

bool TrackBuffersManager::CheckNextInsertionIndex(TrackData& aTrackData,
                                                  const TimeUnit& aSampleTime) {
  if (aTrackData.mNextInsertionIndex.isSome()) {
    return true;
  }

  const TrackBuffer& data = aTrackData.GetTrackBuffer();

  if (data.IsEmpty() || aSampleTime < aTrackData.mBufferedRanges.GetStart()) {
    aTrackData.mNextInsertionIndex = Some(0u);
    return true;
  }

  // Find which discontinuity we should insert the frame before.
  TimeInterval target;
  for (const auto& interval : aTrackData.mBufferedRanges) {
    if (aSampleTime < interval.mStart) {
      target = interval;
      break;
    }
  }
  if (target.IsEmpty()) {
    // No target found, it will be added at the end of the track buffer.
    aTrackData.mNextInsertionIndex = Some(uint32_t(data.Length()));
    return true;
  }
  // We now need to find the first frame of the searched interval.
  // We will insert our new frames right before.
  for (uint32_t i = 0; i < data.Length(); i++) {
    const RefPtr<MediaRawData>& sample = data[i];
    if (sample->mTime >= target.mStart ||
        sample->GetEndTime() > target.mStart) {
      aTrackData.mNextInsertionIndex = Some(i);
      return true;
    }
  }
  NS_ASSERTION(false, "Insertion Index Not Found");
  return false;
}

void TrackBuffersManager::InsertFrames(TrackBuffer& aSamples,
                                       const TimeIntervals& aIntervals,
                                       TrackData& aTrackData) {
  AUTO_PROFILER_LABEL("TrackBuffersManager::InsertFrames", MEDIA_PLAYBACK);
  // 5. Let track buffer equal the track buffer that the coded frame will be
  // added to.
  auto& trackBuffer = aTrackData;

  MSE_DEBUGV("Processing {} {} frames(start:{} end:{})", aSamples.Length(),
             aTrackData.mInfo->mMimeType.get(),
             aIntervals.GetStart().ToMicroseconds(),
             aIntervals.GetEnd().ToMicroseconds());
  PROFILER_MARKER_FMT("InsertFrames", MEDIA_PLAYBACK, {},
                      "Processing {} {}frames(start:{} end:{})",
                      aSamples.Length(), aTrackData.mInfo->mMimeType.get(),
                      aIntervals.GetStart().ToMicroseconds(),
                      aIntervals.GetEnd().ToMicroseconds());

  // 11. Let spliced audio frame be an unset variable for holding audio splice
  // information
  // 12. Let spliced timed text frame be an unset variable for holding timed
  // text splice information

  // 13. If last decode timestamp for track buffer is unset and presentation
  // timestamp falls within the presentation interval of a coded frame in track
  // buffer,then run the following steps: For now we only handle replacing
  // existing frames with the new ones. So we skip this step.

  // 14. Remove existing coded frames in track buffer:
  //   a) If highest end timestamp for track buffer is not set:
  //      Remove all coded frames from track buffer that have a presentation
  //      timestamp greater than or equal to presentation timestamp and less
  //      than frame end timestamp.
  //   b) If highest end timestamp for track buffer is set and less than or
  //   equal to presentation timestamp:
  //      Remove all coded frames from track buffer that have a presentation
  //      timestamp greater than or equal to highest end timestamp and less than
  //      frame end timestamp

  // There is an ambiguity on how to remove frames, which was lodged with:
  // https://www.w3.org/Bugs/Public/show_bug.cgi?id=28710, implementing as per
  // bug description.

  // 15. Remove decoding dependencies of the coded frames removed in the
  // previous step: Remove all coded frames between the coded frames removed in
  // the previous step and the next random access point after those removed
  // frames.

  if (trackBuffer.mBufferedRanges.IntersectsStrict(aIntervals)) {
    if (aSamples[0]->mKeyframe &&
        (mType.Type() == MEDIAMIMETYPE("video/webm") ||
         mType.Type() == MEDIAMIMETYPE("audio/webm"))) {
      // We are starting a new GOP, we do not have to worry about breaking an
      // existing current coded frame group. Reset the next insertion index
      // so the search for when to start our frames removal can be exhaustive.
      // This is a workaround for bug 1276184 and only until either bug 1277733
      // or bug 1209386 is fixed.
      // With the webm container, we can't always properly determine the
      // duration of the last frame, which may cause the last frame of a cluster
      // to overlap the following frame.
      trackBuffer.mNextInsertionIndex.reset();
    }
    uint32_t index = RemoveFrames(aIntervals, trackBuffer,
                                  trackBuffer.mNextInsertionIndex.refOr(0),
                                  RemovalMode::kTruncateFrame);
    if (index) {
      trackBuffer.mNextInsertionIndex = Some(index);
    }
  }

  // 16. Add the coded frame with the presentation timestamp, decode timestamp,
  // and frame duration to the track buffer.
  if (!CheckNextInsertionIndex(aTrackData, aSamples[0]->mTime)) {
    RejectProcessing(NS_ERROR_FAILURE, __func__);
    return;
  }

  // Adjust our demuxing index if necessary.
  if (trackBuffer.mNextGetSampleIndex.isSome()) {
    if (trackBuffer.mNextInsertionIndex.ref() ==
            trackBuffer.mNextGetSampleIndex.ref() &&
        aIntervals.GetEnd() >= trackBuffer.mNextSampleTime) {
      MSE_DEBUG("Next sample to be played got overwritten");
      trackBuffer.mNextGetSampleIndex.reset();
      ResetEvictionIndex(trackBuffer);
    } else if (trackBuffer.mNextInsertionIndex.ref() <=
               trackBuffer.mNextGetSampleIndex.ref()) {
      trackBuffer.mNextGetSampleIndex.ref() += aSamples.Length();
      // We could adjust the eviction index so that the new data gets added to
      // the evictable amount (as it is prior currentTime). However, considering
      // new data is being added prior the current playback, it's likely that
      // this data will be played next, and as such we probably don't want to
      // have it evicted too early. So instead reset the eviction index instead.
      ResetEvictionIndex(trackBuffer);
    }
  }

  TrackBuffer& data = trackBuffer.GetTrackBuffer();
  const uint32_t insertionStart = trackBuffer.mNextInsertionIndex.ref();
  data.InsertElementsAt(insertionStart, aSamples);
  trackBuffer.mNextInsertionIndex.ref() += aSamples.Length();

  // Update our buffered range with new sample interval.
  trackBuffer.mBufferedRanges += aIntervals;

  MSE_DEBUG("Inserted {} frame:{}, buffered-range:{}, mHighestEndTimestamp={}",
            aTrackData.mInfo->mMimeType.get(), DumpTimeRanges(aIntervals).get(),
            DumpTimeRanges(trackBuffer.mBufferedRanges).get(),
            trackBuffer.mHighestEndTimestamp
                ? trackBuffer.mHighestEndTimestamp->ToString().get()
                : "none");
  // We allow a fuzz factor in our interval of half a frame length,
  // as fuzz is +/- value, giving an effective leeway of a full frame
  // length.
  if (!aIntervals.IsEmpty()) {
    TimeIntervals range(aIntervals);
    range.SetFuzz(trackBuffer.mLongestFrameDuration / 2);
    trackBuffer.mSanitizedBufferedRanges += range;
  }

  EndFramesAtNextPresentation(trackBuffer, insertionStart, aSamples.Length());
}

void TrackBuffersManager::EndFramesAtNextPresentation(TrackData& aTrackData,
                                                      uint32_t aStart,
                                                      uint32_t aCount) {
  // Only ISO BMFF stores no display duration; WebM can store one per block.
  const bool isMp4 = mType.Type() == MEDIAMIMETYPE(VIDEO_MP4) ||
                     mType.Type() == MEDIAMIMETYPE(AUDIO_MP4);
  if (!StaticPrefs::media_mp4_frame_ends_across_fragments_enabled() || !isMp4 ||
      &aTrackData != &mVideoTracks || !aCount) {
    return;
  }
  // Find the frames next to the aCount new frames at index aStart in decode
  // order: up to 16 on each side, the largest H.264 and HEVC decoded picture
  // buffer.
  constexpr uint32_t kMaxReorderedFrames = 16;
  // Coded frame processing step 6 runs when "decode timestamp is less than
  // last decode timestamp" or "the difference between decode timestamp and
  // last decode timestamp is greater than 2 times last frame duration":
  // https://w3c.github.io/media-source/#sourcebuffer-coded-frame-processing
  // As in ProcessFrames, the longest frame duration is used.
  const TimeUnit maxDecodeStep = aTrackData.mLongestFrameDuration * 2;
  TrackBuffer& data = aTrackData.GetTrackBuffer();
  // Returns true if the frame at aIndex and the frame after it in decode order
  // have no discontinuity between them.
  auto isContinuous = [&](uint32_t aIndex) {
    const TimeUnit& earlier = data[aIndex]->mTimecode;
    const TimeUnit& later = data[aIndex + 1]->mTimecode;
    if (later < earlier) {
      return false;
    }
    const TimeUnit step = later - earlier;
    return step.IsValid() && step <= maxDecodeStep;
  };
  uint32_t begin = aStart;
  while (begin > 0 && aStart - begin < kMaxReorderedFrames &&
         isContinuous(begin - 1)) {
    begin--;
  }
  const uint32_t newEnd = aStart + aCount;
  uint32_t end = newEnd;
  while (end < data.Length() && end - newEnd < kMaxReorderedFrames &&
         isContinuous(end - 1)) {
    end++;
  }

  // Put the frames in presentation order.
  struct Frame {
    MediaRawData* mSample;
    // True for the aCount frames that this append inserted, false for the
    // frames that were already buffered.
    bool mIsInThisAppend;
  };
  AutoTArray<Frame, 64> frames;
  for (uint32_t i = begin; i < end; i++) {
    frames.AppendElement(Frame{data[i].get(), i >= aStart && i < newEnd});
  }
  frames.StableSort([](const Frame& aA, const Frame& aB) {
    const TimeUnit& a = aA.mSample->mTime;
    const TimeUnit& b = aB.mSample->mTime;
    return a < b ? -1 : (a > b ? 1 : 0);
  });

  // ISO BMFF gives a frame a composition time but no composition duration
  // (ISO/IEC 14496-12 8.6.1.3 and 8.6.1.4), so a frame is displayed until the
  // next frame in presentation order, which can come in another append. Coded
  // frame processing sets a frame's duration only when it adds the frame
  // (https://w3c.github.io/media-source/#sourcebuffer-coded-frame-processing),
  // so this step goes beyond it: at a link between a buffered frame and a new
  // one, end the earlier frame at the start of the later one.
  TimeIntervals extended;
  for (size_t i = 1; i < frames.Length(); i++) {
    if (frames[i - 1].mIsInThisAppend == frames[i].mIsInThisAppend) {
      continue;
    }
    MediaRawData* frame = frames[i - 1].mSample;
    const TimeUnit& nextStart = frames[i].mSample->mTime;
    const TimeUnit oldEnd = frame->GetEndTime();
    if (nextStart < oldEnd) {
      // A new frame inside a buffered frame that ends at the start of the next
      // buffered frame: that end came from the next frame, and the new frames
      // up to that buffered frame cover the rest, so end the buffered frame at
      // the new frame. A buffered frame that ends elsewhere keeps its duration.
      if (frames[i - 1].mIsInThisAppend || nextStart <= frame->mTime) {
        continue;
      }
      size_t next = i + 1;
      while (next < frames.Length() && frames[next].mIsInThisAppend) {
        next++;
      }
      if (next == frames.Length() || frames[next].mSample->mTime != oldEnd) {
        continue;
      }
      MSE_DEBUGV("End frame [{},{}] at new frame start {}",
                 frame->mTime.ToMicroseconds(), oldEnd.ToMicroseconds(),
                 nextStart.ToMicroseconds());
      frame->mDuration = nextStart - frame->mTime;
      continue;
    }
    // A frame cut at the append window end keeps its end.
    if (frame->mOriginalPresentationWindow) {
      continue;
    }
    // The frame and the next frame can use different time bases, so the new
    // end can be rounded. Use the real end, so that buffered matches the
    // frames. The decode end moves with the duration, so it must stay valid.
    const TimeUnit duration = nextStart - frame->mTime;
    const TimeUnit newFrameEnd =
        duration.IsValid() ? frame->mTime + duration : TimeUnit::Invalid();
    const TimeUnit newDecodeEnd =
        duration.IsValid() ? frame->mTimecode + duration : TimeUnit::Invalid();
    if (!newFrameEnd.IsValid() || newFrameEnd.IsInfinite() ||
        !newDecodeEnd.IsValid() || newDecodeEnd.IsInfinite() ||
        newFrameEnd <= oldEnd) {
      continue;
    }
    MSE_DEBUGV("Extend frame [{},{}] to next presentation start {}",
               frame->mTime.ToMicroseconds(), oldEnd.ToMicroseconds(),
               nextStart.ToMicroseconds());
    frame->mDuration = duration;
    extended += TimeInterval(oldEnd, newFrameEnd);
  }
  // Track buffer ranges follow the frame durations:
  // https://w3c.github.io/media-source/#track-buffer-ranges
  if (extended.IsEmpty()) {
    return;
  }
  aTrackData.mBufferedRanges += extended;
  extended.SetFuzz(aTrackData.mLongestFrameDuration / 2);
  aTrackData.mSanitizedBufferedRanges += extended;
}

void TrackBuffersManager::UpdateHighestTimestamp(
    TrackData& aTrackData, const media::TimeUnit& aHighestTime) {
  if (aHighestTime > aTrackData.mHighestStartTimestamp) {
    MutexAutoLock mut(mMutex);
    aTrackData.mHighestStartTimestamp = aHighestTime;
  }
}

uint32_t TrackBuffersManager::RemoveFrames(const TimeIntervals& aIntervals,
                                           TrackData& aTrackData,
                                           uint32_t aStartIndex,
                                           RemovalMode aMode) {
  AUTO_PROFILER_LABEL("TrackBuffersManager::RemoveFrames", MEDIA_PLAYBACK);
  TrackBuffer& data = aTrackData.GetTrackBuffer();
  Maybe<uint32_t> firstRemovedIndex;
  uint32_t lastRemovedIndex = 0;

  TimeIntervals intervals =
      aIntervals.ToBase(aTrackData.mHighestStartTimestamp);

  // We loop from aStartIndex to avoid removing frames that we inserted earlier
  // and part of the current coded frame group. This is allows to handle step
  // 14 of the coded frame processing algorithm without having to check the
  // value of highest end timestamp: "Remove existing coded frames in track
  // buffer:
  //  If highest end timestamp for track buffer is not set:
  //   Remove all coded frames from track buffer that have a presentation
  //   timestamp greater than or equal to presentation timestamp and less than
  //   frame end timestamp.
  //  If highest end timestamp for track buffer is set and less than or equal to
  //  presentation timestamp:
  //   Remove all coded frames from track buffer that have a presentation
  //   timestamp greater than or equal to highest end timestamp and less than
  //   frame end timestamp.
  TimeUnit intervalsEnd = intervals.GetEnd();
  for (uint32_t i = aStartIndex; i < data.Length(); i++) {
    RefPtr<MediaRawData>& sample = data[i];
    if (intervals.ContainsStrict(sample->mTime)) {
      // The start of this existing frame will be overwritten, we drop that
      // entire frame.
      MSE_DEBUGV("overriding start of frame [{},{}] with [{},{}] dropping",
                 sample->mTime.ToMicroseconds(),
                 sample->GetEndTime().ToMicroseconds(),
                 intervals.GetStart().ToMicroseconds(),
                 intervals.GetEnd().ToMicroseconds());
      if (firstRemovedIndex.isNothing()) {
        firstRemovedIndex = Some(i);
      }
      lastRemovedIndex = i;
      continue;
    }
    TimeInterval sampleInterval(sample->mTime, sample->GetEndTime());
    if (aMode == RemovalMode::kTruncateFrame &&
        intervals.IntersectsStrict(sampleInterval)) {
      // The sample to be overwritten is only partially covered.
      TimeIntervals intersection =
          Intersection(intervals, TimeIntervals(sampleInterval));
      bool found = false;
      TimeUnit startTime = intersection.GetStart(&found);
      MOZ_DIAGNOSTIC_ASSERT(found, "Must intersect with added coded frames");
      (void)found;
      // Signal that this frame should be truncated when decoded.
      if (!sample->mOriginalPresentationWindow) {
        sample->mOriginalPresentationWindow = Some(sampleInterval);
      }
      MOZ_ASSERT(startTime > sample->mTime);
      sample->mDuration = startTime - sample->mTime;
      MOZ_DIAGNOSTIC_ASSERT(sample->mDuration.IsValid());
      MSE_DEBUGV(
          "partial overwrite of frame [{},{}] with [{},{}] trim to "
          "[{},{}]",
          sampleInterval.mStart.ToMicroseconds(),
          sampleInterval.mEnd.ToMicroseconds(),
          intervals.GetStart().ToMicroseconds(),
          intervals.GetEnd().ToMicroseconds(), sample->mTime.ToMicroseconds(),
          sample->GetEndTime().ToMicroseconds());
      continue;
    }

    if (sample->mTime >= intervalsEnd) {
      // We can break the loop now. All frames up to the next keyframe will be
      // removed during the next step.
      break;
    }
  }

  if (firstRemovedIndex.isNothing()) {
    return 0;
  }

  // Remove decoding dependencies of the coded frames removed in the previous
  // step: Remove all coded frames between the coded frames removed in the
  // previous step and the next random access point after those removed frames.
  for (uint32_t i = lastRemovedIndex + 1; i < data.Length(); i++) {
    const RefPtr<MediaRawData>& sample = data[i];
    if (sample->mKeyframe) {
      break;
    }
    lastRemovedIndex = i;
  }

  uint32_t sizeRemoved = 0;
  TimeIntervals removedIntervals;
  for (uint32_t i = firstRemovedIndex.ref(); i <= lastRemovedIndex; i++) {
    const RefPtr<MediaRawData> sample = data[i];
    TimeInterval sampleInterval =
        TimeInterval(sample->mTime, sample->GetEndTime());
    removedIntervals += sampleInterval;
    sizeRemoved += sample->ComputedSizeOfIncludingThis();
  }
  aTrackData.mSizeBuffer -= sizeRemoved;

  nsPrintfCString msg("Removing frames from:%u for %s (frames:%u) ([%f, %f))",
                      firstRemovedIndex.ref(),
                      aTrackData.mInfo->mMimeType.get(),
                      lastRemovedIndex - firstRemovedIndex.ref() + 1,
                      removedIntervals.GetStart().ToSeconds(),
                      removedIntervals.GetEnd().ToSeconds());
  MSE_DEBUG("{}", msg.get());
  if (profiler_thread_is_being_profiled_for_markers()) {
    PROFILER_MARKER_TEXT("RemoveFrames", MEDIA_PLAYBACK, {}, msg);
  }

  if (aTrackData.mNextGetSampleIndex.isSome()) {
    if (aTrackData.mNextGetSampleIndex.ref() >= firstRemovedIndex.ref() &&
        aTrackData.mNextGetSampleIndex.ref() <= lastRemovedIndex) {
      MSE_DEBUG("Next sample to be played got evicted");
      aTrackData.mNextGetSampleIndex.reset();
      ResetEvictionIndex(aTrackData);
    } else if (aTrackData.mNextGetSampleIndex.ref() > lastRemovedIndex) {
      uint32_t samplesRemoved = lastRemovedIndex - firstRemovedIndex.ref() + 1;
      aTrackData.mNextGetSampleIndex.ref() -= samplesRemoved;
      if (aTrackData.mEvictionIndex.mLastIndex > lastRemovedIndex) {
        MOZ_DIAGNOSTIC_ASSERT(
            aTrackData.mEvictionIndex.mLastIndex >= samplesRemoved &&
                aTrackData.mEvictionIndex.mEvictable >= sizeRemoved,
            "Invalid eviction index");
        MutexAutoLock mut(mMutex);
        aTrackData.mEvictionIndex.mLastIndex -= samplesRemoved;
        aTrackData.mEvictionIndex.mEvictable -= sizeRemoved;
      } else {
        ResetEvictionIndex(aTrackData);
      }
    }
  }

  if (aTrackData.mNextInsertionIndex.isSome()) {
    if (aTrackData.mNextInsertionIndex.ref() > firstRemovedIndex.ref() &&
        aTrackData.mNextInsertionIndex.ref() <= lastRemovedIndex + 1) {
      aTrackData.ResetAppendState();
      MSE_DEBUG("NextInsertionIndex got reset.");
    } else if (aTrackData.mNextInsertionIndex.ref() > lastRemovedIndex + 1) {
      aTrackData.mNextInsertionIndex.ref() -=
          lastRemovedIndex - firstRemovedIndex.ref() + 1;
    }
  }

  // Update our buffered range to exclude the range just removed.
  MSE_DEBUG("Removing {} from bufferedRange {}",
            DumpTimeRanges(removedIntervals).get(),
            DumpTimeRanges(aTrackData.mBufferedRanges).get());
  aTrackData.mBufferedRanges -= removedIntervals;

  // Update sanitized buffered ranges.  As well as subtracting intervals for
  // the removed samples, adjacent gaps between samples that were filled in
  // through Interval fuzz also need to be removed.  Calculate the complete
  // gaps left due to frame removal by comparing the remaining buffered
  // intervals with the removed intervals, and then subtract these gaps from
  // mSanitizedBufferedRanges.
  TimeIntervals gaps;
  // If the earliest buffered interval was removed then a leading interval will
  // be removed from mSanitizedBufferedRanges.
  Maybe<TimeUnit> gapStart =
      Some(aTrackData.mSanitizedBufferedRanges.GetStart());
  for (auto removedInterval = removedIntervals.cbegin(),
            bufferedInterval = aTrackData.mBufferedRanges.cbegin();
       removedInterval < removedIntervals.cend();) {
    if (bufferedInterval == aTrackData.mBufferedRanges.cend()) {
      // No more buffered intervals.  The rest has been removed.
      // gapStart has always been set here, either before the loop or when
      // bufferedInterval was incremented.
      gaps += TimeInterval(gapStart.value(),
                           aTrackData.mSanitizedBufferedRanges.GetEnd());
      break;
    }
    if (bufferedInterval->mEnd <= removedInterval->mStart) {
      gapStart = Some(bufferedInterval->mEnd);
      ++bufferedInterval;
      continue;
    }
    MOZ_ASSERT(removedInterval->mEnd <= bufferedInterval->mStart);
    // If gapStart is not set then gaps already includes the gap up to
    // bufferedInterval.
    if (gapStart) {
      gaps += TimeInterval(gapStart.value(), bufferedInterval->mStart);
      gapStart.reset();
    }
    ++removedInterval;
  }
  MSE_DEBUG("Removing {} from mSanitizedBufferedRanges {}",
            DumpTimeRanges(gaps).get(),
            DumpTimeRanges(aTrackData.mSanitizedBufferedRanges).get());
  aTrackData.mSanitizedBufferedRanges -= gaps;

  data.RemoveElementsAt(firstRemovedIndex.ref(),
                        lastRemovedIndex - firstRemovedIndex.ref() + 1);

  if (removedIntervals.GetEnd() >= aTrackData.mHighestStartTimestamp &&
      removedIntervals.GetStart() <= aTrackData.mHighestStartTimestamp) {
    // The sample with the highest presentation time got removed.
    // Rescan the trackbuffer to determine the new one.
    TimeUnit highestStartTime;
    for (const auto& sample : data) {
      if (sample->mTime > highestStartTime) {
        highestStartTime = sample->mTime;
      }
    }
    MutexAutoLock mut(mMutex);
    aTrackData.mHighestStartTimestamp = highestStartTime;
  }

  MSE_DEBUG(
      "After removing frames, {} data sz={}, highestStartTimestamp={: }"
      ", bufferedRange={}, sanitizedBufferedRanges={}",
      aTrackData.mInfo->mMimeType.get(), data.Length(),
      aTrackData.mHighestStartTimestamp.ToMicroseconds(),
      DumpTimeRanges(aTrackData.mBufferedRanges).get(),
      DumpTimeRanges(aTrackData.mSanitizedBufferedRanges).get());

  // If all frames are removed, both buffer and buffered range should be empty.
  if (data.IsEmpty()) {
    MOZ_ASSERT(aTrackData.mBufferedRanges.IsEmpty());
    // We still can't figure out why above assertion would fail, so we keep it
    // on debug build, and do a workaround for other builds to ensure that
    // buffered range should match the data.
    if (!aTrackData.mBufferedRanges.IsEmpty()) {
      NS_WARNING(
          nsPrintfCString("Empty data but has non-empty buffered range %s ?!",
                          DumpTimeRanges(aTrackData.mBufferedRanges).get())
              .get());
      aTrackData.mBufferedRanges.Clear();
    }
  }
  if (aTrackData.mBufferedRanges.IsEmpty()) {
    TimeIntervals sampleIntervals;
    for (const auto& sample : data) {
      sampleIntervals += TimeInterval(sample->mTime, sample->GetEndTime());
    }
    MOZ_ASSERT(sampleIntervals.IsEmpty());
    // We still can't figure out why above assertion would fail, so we keep it
    // on debug build, and do a workaround for other builds to ensure that
    // buffered range should match the data.
    if (!sampleIntervals.IsEmpty()) {
      NS_WARNING(
          nsPrintfCString(
              "Empty buffer range but has non-empty sample intervals %s ?!",
              DumpTimeRanges(sampleIntervals).get())
              .get());
      aTrackData.mBufferedRanges += sampleIntervals;
      TimeIntervals range(sampleIntervals);
      range.SetFuzz(aTrackData.mLongestFrameDuration / 2);
      aTrackData.mSanitizedBufferedRanges += range;
    }
  }

  return firstRemovedIndex.ref();
}

void TrackBuffersManager::RecreateParser(bool aReuseInitData) {
  MOZ_ASSERT(OnTaskQueue());
  // Recreate our parser for only the data remaining. This is required
  // as it has parsed the entire InputBuffer provided.
  // Once the old TrackBuffer/MediaSource implementation is removed
  // we can optimize this part. TODO
  if (mParser) {
    DDUNLINKCHILD(mParser.get());
  }
  mParser = ContainerParser::CreateForMIMEType(mType);
  DDLINKCHILD("parser", mParser.get());
  if (aReuseInitData && mInitData) {
    MSE_DEBUG("Using existing init data to reset parser");
    TimeUnit start, end;
    mParser->ParseStartAndEndTimestamps(MediaSpan(mInitData), start, end);
    mProcessedInput = mInitData->Length();
  } else {
    MSE_DEBUG("Resetting parser, not reusing init data");
    mProcessedInput = 0;
  }
}

nsTArray<TrackBuffersManager::TrackData*> TrackBuffersManager::GetTracksList() {
  nsTArray<TrackData*> tracks;
  if (HasVideo()) {
    tracks.AppendElement(&mVideoTracks);
  }
  if (HasAudio()) {
    tracks.AppendElement(&mAudioTracks);
  }
  return tracks;
}

nsTArray<const TrackBuffersManager::TrackData*>
TrackBuffersManager::GetTracksList() const {
  nsTArray<const TrackData*> tracks;
  if (HasVideo()) {
    tracks.AppendElement(&mVideoTracks);
  }
  if (HasAudio()) {
    tracks.AppendElement(&mAudioTracks);
  }
  return tracks;
}

void TrackBuffersManager::SetAppendState(AppendState aAppendState) {
  MSE_DEBUG("AppendState changed from {} to {}",
            SourceBufferAttributes::EnumValueToString(
                mSourceBufferAttributes->GetAppendState()),
            SourceBufferAttributes::EnumValueToString(aAppendState));
  mSourceBufferAttributes->SetAppendState(aAppendState);
}

MediaInfo TrackBuffersManager::GetMetadata() const {
  MutexAutoLock mut(mMutex);
  return mInfo;
}

const TimeIntervals& TrackBuffersManager::Buffered(
    TrackInfo::TrackType aTrack) const {
  MOZ_ASSERT(OnTaskQueue());
  return GetTracksData(aTrack).mBufferedRanges;
}

const media::TimeUnit& TrackBuffersManager::HighestStartTime(
    TrackInfo::TrackType aTrack) const {
  MOZ_ASSERT(OnTaskQueue());
  return GetTracksData(aTrack).mHighestStartTimestamp;
}

TimeIntervals TrackBuffersManager::SafeBuffered(
    TrackInfo::TrackType aTrack) const {
  MutexAutoLock mut(mMutex);
  return aTrack == TrackInfo::kVideoTrack ? mVideoBufferedRanges
                                          : mAudioBufferedRanges;
}

TimeUnit TrackBuffersManager::HighestStartTime() const {
  MutexAutoLock mut(mMutex);
  TimeUnit highestStartTime;
  for (auto& track : GetTracksList()) {
    highestStartTime =
        std::max(track->mHighestStartTimestamp, highestStartTime);
  }
  return highestStartTime;
}

TimeUnit TrackBuffersManager::HighestEndTime() const {
  MutexAutoLock mut(mMutex);

  nsTArray<const TimeIntervals*> tracks;
  if (HasVideo()) {
    tracks.AppendElement(&mVideoBufferedRanges);
  }
  if (HasAudio()) {
    tracks.AppendElement(&mAudioBufferedRanges);
  }
  return HighestEndTime(tracks);
}

TimeUnit TrackBuffersManager::HighestEndTime(
    nsTArray<const TimeIntervals*>& aTracks) const {
  mMutex.AssertCurrentThreadOwns();

  TimeUnit highestEndTime;

  for (const auto& trackRanges : aTracks) {
    highestEndTime = std::max(trackRanges->GetEnd(), highestEndTime);
  }
  return highestEndTime;
}

void TrackBuffersManager::ResetEvictionIndex(TrackData& aTrackData) {
  MutexAutoLock mut(mMutex);
  MSE_DEBUG("ResetEvictionIndex for {}", aTrackData.mInfo->mMimeType.get());
  aTrackData.mEvictionIndex.Reset();
}

void TrackBuffersManager::UpdateEvictionIndex(TrackData& aTrackData,
                                              uint32_t currentIndex) {
  uint32_t evictable = 0;
  TrackBuffer& data = aTrackData.GetTrackBuffer();
  MOZ_DIAGNOSTIC_ASSERT(currentIndex >= aTrackData.mEvictionIndex.mLastIndex,
                        "Invalid call");
  MOZ_DIAGNOSTIC_ASSERT(
      currentIndex == data.Length() || data[currentIndex]->mKeyframe,
      "Must stop at keyframe");

  for (uint32_t i = aTrackData.mEvictionIndex.mLastIndex; i < currentIndex;
       i++) {
    evictable += data[i]->ComputedSizeOfIncludingThis();
  }
  aTrackData.mEvictionIndex.mLastIndex = currentIndex;
  MutexAutoLock mut(mMutex);
  aTrackData.mEvictionIndex.mEvictable += evictable;
  MSE_DEBUG("UpdateEvictionIndex for {} (idx={}, evictable={})",
            aTrackData.mInfo->mMimeType.get(),
            aTrackData.mEvictionIndex.mLastIndex,
            aTrackData.mEvictionIndex.mEvictable);
}

const TrackBuffersManager::TrackBuffer& TrackBuffersManager::GetTrackBuffer(
    TrackInfo::TrackType aTrack) const {
  MOZ_ASSERT(OnTaskQueue());
  return GetTracksData(aTrack).GetTrackBuffer();
}

uint32_t TrackBuffersManager::FindSampleIndex(const TrackBuffer& aTrackBuffer,
                                              const TimeInterval& aInterval) {
  TimeUnit target = aInterval.mStart - aInterval.mFuzz;

  for (uint32_t i = 0; i < aTrackBuffer.Length(); i++) {
    const RefPtr<MediaRawData>& sample = aTrackBuffer[i];
    if (sample->mTime >= target || sample->GetEndTime() > target) {
      return i;
    }
  }
  MOZ_ASSERT(false, "FindSampleIndex called with invalid arguments");

  return 0;
}

TimeUnit TrackBuffersManager::Seek(TrackInfo::TrackType aTrack,
                                   const TimeUnit& aTime,
                                   const TimeUnit& aFuzz) {
  MOZ_ASSERT(OnTaskQueue());
  AUTO_PROFILER_LABEL("TrackBuffersManager::Seek", MEDIA_PLAYBACK);
  auto& trackBuffer = GetTracksData(aTrack);
  const TrackBuffersManager::TrackBuffer& track = GetTrackBuffer(aTrack);
  MSE_DEBUG("Seek, track={}, target={}", TrackTypeToStr(aTrack),
            aTime.ToMicroseconds());

  if (!track.Length()) {
    // This a reset. It will be followed by another valid seek.
    trackBuffer.mNextGetSampleIndex = Some(uint32_t(0));
    trackBuffer.mNextSampleTimecode = TimeUnit();
    trackBuffer.mNextSampleTime = TimeUnit();
    ResetEvictionIndex(trackBuffer);
    return TimeUnit();
  }

  uint32_t i = 0;

  if (aTime != TimeUnit()) {
    // Determine the interval of samples we're attempting to seek to.
    TimeIntervals buffered = trackBuffer.mBufferedRanges;
    // Fuzz factor is +/- aFuzz; as we want to only eliminate gaps
    // that are less than aFuzz wide, we set a fuzz factor aFuzz/2.
    buffered.SetFuzz(aFuzz / 2);
    TimeIntervals::IndexType index = buffered.Find(aTime);
    MOZ_ASSERT(index != TimeIntervals::NoIndex,
               "We shouldn't be called if aTime isn't buffered");
    TimeInterval target = buffered[index];
    target.mFuzz = aFuzz;
    i = FindSampleIndex(track, target);
  }

  Maybe<TimeUnit> lastKeyFrameTime;
  TimeUnit lastKeyFrameTimecode;
  uint32_t lastKeyFrameIndex = 0;
  for (; i < track.Length(); i++) {
    const RefPtr<MediaRawData>& sample = track[i];
    TimeUnit sampleTime = sample->mTime;
    if (sampleTime > aTime && lastKeyFrameTime.isSome()) {
      break;
    }
    if (sample->mKeyframe) {
      lastKeyFrameTimecode = sample->mTimecode;
      lastKeyFrameTime = Some(sampleTime);
      lastKeyFrameIndex = i;
    }
    if (sampleTime == aTime ||
        (sampleTime > aTime && lastKeyFrameTime.isSome())) {
      break;
    }
  }
  MSE_DEBUG(
      "Keyframe {} found at {} @ {}", lastKeyFrameTime.isSome() ? "" : "not",
      lastKeyFrameTime.refOr(TimeUnit()).ToMicroseconds(), lastKeyFrameIndex);

  trackBuffer.mNextGetSampleIndex = Some(lastKeyFrameIndex);
  trackBuffer.mNextSampleTimecode = lastKeyFrameTimecode;
  trackBuffer.mNextSampleTime = lastKeyFrameTime.refOr(TimeUnit());
  ResetEvictionIndex(trackBuffer);
  UpdateEvictionIndex(trackBuffer, lastKeyFrameIndex);

  return lastKeyFrameTime.refOr(TimeUnit());
}

uint32_t TrackBuffersManager::SkipToNextRandomAccessPoint(
    TrackInfo::TrackType aTrack, const TimeUnit& aTimeThreadshold,
    const media::TimeUnit& aFuzz, bool& aFound) {
  mTaskQueueCapability->AssertOnCurrentThread();
  AUTO_PROFILER_LABEL("TrackBuffersManager::SkipToNextRandomAccessPoint",
                      MEDIA_PLAYBACK);
  uint32_t parsed = 0;
  auto& trackData = GetTracksData(aTrack);
  const TrackBuffer& track = GetTrackBuffer(aTrack);
  aFound = false;

  // SkipToNextRandomAccessPoint can only be called if aTimeThreadshold is known
  // to be buffered.

  if (NS_FAILED(SetNextGetSampleIndexIfNeeded(aTrack, aFuzz))) {
    return 0;
  }

  TimeUnit nextSampleTimecode = trackData.mNextSampleTimecode;
  TimeUnit nextSampleTime = trackData.mNextSampleTime;
  uint32_t i = trackData.mNextGetSampleIndex.ref();
  uint32_t originalPos = i;

  for (; i < track.Length(); i++) {
    const MediaRawData* sample =
        GetSample(aTrack, i, nextSampleTimecode, nextSampleTime, aFuzz);
    if (!sample) {
      break;
    }
    if (sample->mKeyframe && sample->mTime >= aTimeThreadshold) {
      aFound = true;
      break;
    }
    nextSampleTimecode = sample->GetEndTimecode();
    nextSampleTime = sample->GetEndTime();
    parsed++;
  }

  // Adjust the next demux time and index so that the next call to
  // SkipToNextRandomAccessPoint will not count again the parsed sample as
  // skipped.
  if (aFound) {
    trackData.mNextSampleTimecode = track[i]->mTimecode;
    trackData.mNextSampleTime = track[i]->mTime;
    trackData.mNextGetSampleIndex = Some(i);
  } else if (i > 0) {
    // Go back to the previous keyframe or the original position so the next
    // demux can succeed and be decoded.
    for (uint32_t j = i; j-- > originalPos;) {
      const RefPtr<MediaRawData>& sample = track[j];
      if (sample->mKeyframe) {
        trackData.mNextSampleTimecode = sample->mTimecode;
        trackData.mNextSampleTime = sample->mTime;
        trackData.mNextGetSampleIndex = Some(uint32_t(j));
        // We are unable to skip to a keyframe past aTimeThreshold, however
        // we are speeding up decoding by dropping the unplayable frames.
        // So we can mark aFound as true.
        aFound = true;
        break;
      }
      parsed--;
    }
  }

  if (aFound) {
    UpdateEvictionIndex(trackData, trackData.mNextGetSampleIndex.ref());
  }

  return parsed;
}

const MediaRawData* TrackBuffersManager::GetSample(TrackInfo::TrackType aTrack,
                                                   uint32_t aIndex,
                                                   const TimeUnit& aExpectedDts,
                                                   const TimeUnit& aExpectedPts,
                                                   const TimeUnit& aFuzz) {
  MOZ_ASSERT(OnTaskQueue());
  const TrackBuffer& track = GetTrackBuffer(aTrack);

  if (aIndex >= track.Length()) {
    MSE_DEBUGV(
        "Can't get sample due to reaching to the end, index={}, "
        "length={}",
        aIndex, track.Length());
    // reached the end.
    return nullptr;
  }

  if (!(aExpectedDts + aFuzz).IsValid() || !(aExpectedPts + aFuzz).IsValid()) {
    // Time overflow, it seems like we also reached the end.
    MSE_DEBUGV(
        "Can't get sample due to time overflow, expectedPts={}"
        ", aExpectedDts={}, fuzz={}",
        aExpectedPts.ToMicroseconds(), aExpectedPts.ToMicroseconds(),
        aFuzz.ToMicroseconds());
    return nullptr;
  }

  const RefPtr<MediaRawData>& sample = track[aIndex];
  if (!aIndex || sample->mTimecode <= aExpectedDts + aFuzz ||
      sample->mTime <= aExpectedPts + aFuzz) {
    MOZ_DIAGNOSTIC_ASSERT(sample->HasValidTime());
    return sample;
  }

  MSE_DEBUGV(
      "Can't get sample due to big gap, sample={}"
      ", expectedPts={}, aExpectedDts={}"
      ", fuzz={}",
      sample->mTime.ToMicroseconds(), aExpectedPts.ToMicroseconds(),
      aExpectedPts.ToMicroseconds(), aFuzz.ToMicroseconds());

  // Gap is too big. End of Stream or Waiting for Data.
  // TODO, check that we have continuous data based on the sanitized buffered
  // range instead.
  return nullptr;
}

already_AddRefed<MediaRawData> TrackBuffersManager::GetSample(
    TrackInfo::TrackType aTrack, const TimeUnit& aFuzz, MediaResult& aResult) {
  mTaskQueueCapability->AssertOnCurrentThread();
  AUTO_PROFILER_LABEL("TrackBuffersManager::GetSample", MEDIA_PLAYBACK);
  auto& trackData = GetTracksData(aTrack);
  const TrackBuffer& track = GetTrackBuffer(aTrack);

  aResult = NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA;

  if (trackData.mNextGetSampleIndex.isSome()) {
    if (trackData.mNextGetSampleIndex.ref() >= track.Length()) {
      aResult = NS_ERROR_DOM_MEDIA_END_OF_STREAM;
      return nullptr;
    }
    const MediaRawData* sample = GetSample(
        aTrack, trackData.mNextGetSampleIndex.ref(),
        trackData.mNextSampleTimecode, trackData.mNextSampleTime, aFuzz);
    if (!sample) {
      return nullptr;
    }

    RefPtr<MediaRawData> p = sample->Clone();
    if (!p) {
      aResult = MediaResult(NS_ERROR_OUT_OF_MEMORY, __func__);
      return nullptr;
    }
    if (p->mKeyframe) {
      UpdateEvictionIndex(trackData, trackData.mNextGetSampleIndex.ref());
    }
    trackData.mNextGetSampleIndex.ref()++;
    // Estimate decode timestamp and timestamp of the next sample.
    TimeUnit nextSampleTimecode = sample->GetEndTimecode();
    TimeUnit nextSampleTime = sample->GetEndTime();
    const MediaRawData* nextSample =
        GetSample(aTrack, trackData.mNextGetSampleIndex.ref(),
                  nextSampleTimecode, nextSampleTime, aFuzz);
    if (nextSample) {
      // We have a valid next sample, can use exact values.
      trackData.mNextSampleTimecode = nextSample->mTimecode;
      trackData.mNextSampleTime = nextSample->mTime;
    } else {
      // Next sample isn't available yet. Use estimates.
      trackData.mNextSampleTimecode = nextSampleTimecode;
      trackData.mNextSampleTime = nextSampleTime;
    }
    aResult = NS_OK;
    return p.forget();
  }

  aResult = SetNextGetSampleIndexIfNeeded(aTrack, aFuzz);

  if (NS_FAILED(aResult)) {
    return nullptr;
  }

  MOZ_RELEASE_ASSERT(trackData.mNextGetSampleIndex.isSome() &&
                     trackData.mNextGetSampleIndex.ref() < track.Length());
  const RefPtr<MediaRawData>& sample =
      track[trackData.mNextGetSampleIndex.ref()];
  RefPtr<MediaRawData> p = sample->Clone();
  if (!p) {
    // OOM
    aResult = MediaResult(NS_ERROR_OUT_OF_MEMORY, __func__);
    return nullptr;
  }
  MOZ_DIAGNOSTIC_ASSERT(p->HasValidTime());

  // Find the previous keyframe to calculate the evictable amount.
  uint32_t i = trackData.mNextGetSampleIndex.ref();
  for (; !track[i]->mKeyframe; i--) {
  }
  UpdateEvictionIndex(trackData, i);

  trackData.mNextGetSampleIndex.ref()++;
  trackData.mNextSampleTimecode = sample->GetEndTimecode();
  trackData.mNextSampleTime = sample->GetEndTime();
  return p.forget();
}

int32_t TrackBuffersManager::FindCurrentPosition(TrackInfo::TrackType aTrack,
                                                 const TimeUnit& aFuzz) const {
  MOZ_ASSERT(OnTaskQueue());
  const auto& trackData = GetTracksData(aTrack);
  const TrackBuffer& track = GetTrackBuffer(aTrack);
  int32_t trackLength = AssertedCast<int32_t>(track.Length());
  // A video frame made longer to the next frame in presentation order can
  // cover the decode times of the frames after it, so a decode interval ends
  // at the next frame's decode time at the latest.
  // Decode times can go back between coded frame groups, so only a later next
  // decode time ends the interval.
  auto decodeEnd = [&](int32_t aIndex) {
    const TimeUnit end = track[aIndex]->GetEndTimecode();
    if (aIndex + 1 >= trackLength) {
      return end;
    }
    const TimeUnit& next = track[aIndex + 1]->mTimecode;
    return next > track[aIndex]->mTimecode && next < end ? next : end;
  };

  // Perform an exact search first.
  for (int32_t i = 0; i < trackLength; i++) {
    const RefPtr<MediaRawData>& sample = track[i];
    TimeInterval sampleInterval{sample->mTimecode, decodeEnd(i)};

    if (sampleInterval.ContainsStrict(trackData.mNextSampleTimecode)) {
      return i;
    }
    if (sampleInterval.mStart > trackData.mNextSampleTimecode) {
      // Samples are ordered by timecode. There's no need to search
      // any further.
      break;
    }
  }

  for (int32_t i = 0; i < trackLength; i++) {
    const RefPtr<MediaRawData>& sample = track[i];
    TimeInterval sampleInterval{sample->mTimecode, decodeEnd(i), aFuzz};

    if (sampleInterval.ContainsWithStrictEnd(trackData.mNextSampleTimecode)) {
      return i;
    }
    if (sampleInterval.mStart - aFuzz > trackData.mNextSampleTimecode) {
      // Samples are ordered by timecode. There's no need to search
      // any further.
      break;
    }
  }

  // We couldn't find our sample by decode timestamp. Attempt to find it using
  // presentation timestamp. There will likely be small jerkiness.
  for (int32_t i = 0; i < trackLength; i++) {
    const RefPtr<MediaRawData>& sample = track[i];
    TimeInterval sampleInterval{sample->mTime, sample->GetEndTime(), aFuzz};

    if (sampleInterval.ContainsWithStrictEnd(trackData.mNextSampleTimecode)) {
      return i;
    }
  }

  // Still not found.
  return -1;
}

uint32_t TrackBuffersManager::Evictable(TrackInfo::TrackType aTrack) const {
  MutexAutoLock mut(mMutex);
  return GetTracksData(aTrack).mEvictionIndex.mEvictable;
}

TimeUnit TrackBuffersManager::GetNextRandomAccessPoint(
    TrackInfo::TrackType aTrack, const TimeUnit& aFuzz) {
  mTaskQueueCapability->AssertOnCurrentThread();

  // So first determine the current position in the track buffer if necessary.
  if (NS_FAILED(SetNextGetSampleIndexIfNeeded(aTrack, aFuzz))) {
    return TimeUnit::FromInfinity();
  }

  auto& trackData = GetTracksData(aTrack);
  const TrackBuffersManager::TrackBuffer& track = GetTrackBuffer(aTrack);

  uint32_t i = trackData.mNextGetSampleIndex.ref();
  TimeUnit nextSampleTimecode = trackData.mNextSampleTimecode;
  TimeUnit nextSampleTime = trackData.mNextSampleTime;

  for (; i < track.Length(); i++) {
    const MediaRawData* sample =
        GetSample(aTrack, i, nextSampleTimecode, nextSampleTime, aFuzz);
    if (!sample) {
      break;
    }
    if (sample->mKeyframe) {
      return sample->mTime;
    }
    nextSampleTimecode = sample->GetEndTimecode();
    nextSampleTime = sample->GetEndTime();
  }
  return TimeUnit::FromInfinity();
}

nsresult TrackBuffersManager::SetNextGetSampleIndexIfNeeded(
    TrackInfo::TrackType aTrack, const TimeUnit& aFuzz) {
  MOZ_ASSERT(OnTaskQueue());
  auto& trackData = GetTracksData(aTrack);
  const TrackBuffer& track = GetTrackBuffer(aTrack);

  if (trackData.mNextGetSampleIndex.isSome()) {
    // We already know the next GetSample index.
    return NS_OK;
  }

  if (!track.Length()) {
    // There's nothing to find yet.
    return NS_ERROR_DOM_MEDIA_END_OF_STREAM;
  }

  if (trackData.mNextSampleTimecode == TimeUnit()) {
    // First demux, get first sample.
    trackData.mNextGetSampleIndex = Some(0u);
    return NS_OK;
  }

  if (trackData.mNextSampleTimecode > track.LastElement()->GetEndTimecode()) {
    // The next element is past our last sample. We're done.
    trackData.mNextGetSampleIndex = Some(uint32_t(track.Length()));
    return NS_ERROR_DOM_MEDIA_END_OF_STREAM;
  }

  int32_t pos = FindCurrentPosition(aTrack, aFuzz);
  if (pos < 0) {
    // Not found, must wait for more data.
    MSE_DEBUG("Couldn't find sample (pts:{} dts:{})",
              trackData.mNextSampleTime.ToMicroseconds(),
              trackData.mNextSampleTimecode.ToMicroseconds());
    return NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA;
  }
  trackData.mNextGetSampleIndex = Some(uint32_t(pos));
  return NS_OK;
}

void TrackBuffersManager::TrackData::AddSizeOfResources(
    MediaSourceDecoder::ResourceSizes* aSizes) const {
  for (const TrackBuffer& buffer : mBuffers) {
    for (const MediaRawData* data : buffer) {
      aSizes->mByteSize += data->SizeOfIncludingThis(aSizes->mMallocSizeOf);
    }
  }
}

RefPtr<GenericPromise> TrackBuffersManager::RequestDebugInfo(
    dom::TrackBuffersManagerDebugInfo& aInfo) const {
  const RefPtr<TaskQueue> taskQueue = GetTaskQueueSafe();
  if (!taskQueue) {
    return GenericPromise::CreateAndResolve(true, __func__);
  }
  if (!taskQueue->IsCurrentThreadIn()) {
    // Run the request on the task queue if it's not already.
    return InvokeAsync(taskQueue.get(), __func__,
                       [this, self = RefPtr{this}, &aInfo] {
                         return RequestDebugInfo(aInfo);
                       });
  }
  mTaskQueueCapability->AssertOnCurrentThread();
  GetDebugInfo(aInfo);
  return GenericPromise::CreateAndResolve(true, __func__);
}

void TrackBuffersManager::GetDebugInfo(
    dom::TrackBuffersManagerDebugInfo& aInfo) const {
  MOZ_ASSERT(OnTaskQueue(),
             "This shouldn't be called off the task queue because we're about "
             "to touch a lot of data that is used on the task queue");
  CopyUTF8toUTF16(mType.Type().AsString(), aInfo.mType);

  if (HasAudio()) {
    aInfo.mNextSampleTime = mAudioTracks.mNextSampleTime.ToSeconds();
    aInfo.mNumSamples =
        AssertedCast<int32_t>(mAudioTracks.mBuffers[0].Length());
    aInfo.mBufferSize = AssertedCast<int32_t>(mAudioTracks.mSizeBuffer);
    aInfo.mEvictable = AssertedCast<int32_t>(Evictable(TrackInfo::kAudioTrack));
    aInfo.mNextGetSampleIndex =
        AssertedCast<int32_t>(mAudioTracks.mNextGetSampleIndex.valueOr(-1));
    aInfo.mNextInsertionIndex =
        AssertedCast<int32_t>(mAudioTracks.mNextInsertionIndex.valueOr(-1));
    media::TimeIntervals ranges = SafeBuffered(TrackInfo::kAudioTrack);
    dom::Sequence<dom::BufferRange> items;
    for (uint32_t i = 0; i < ranges.Length(); ++i) {
      // dom::Sequence is a FallibleTArray
      dom::BufferRange* range = items.AppendElement(fallible);
      if (!range) {
        break;
      }
      range->mStart = ranges.Start(i).ToSeconds();
      range->mEnd = ranges.End(i).ToSeconds();
    }
    aInfo.mRanges = std::move(items);
  } else if (HasVideo()) {
    aInfo.mNextSampleTime = mVideoTracks.mNextSampleTime.ToSeconds();
    aInfo.mNumSamples =
        AssertedCast<int32_t>(mVideoTracks.mBuffers[0].Length());
    aInfo.mBufferSize = AssertedCast<int32_t>(mVideoTracks.mSizeBuffer);
    aInfo.mEvictable = AssertedCast<int32_t>(Evictable(TrackInfo::kVideoTrack));
    aInfo.mNextGetSampleIndex =
        AssertedCast<int32_t>(mVideoTracks.mNextGetSampleIndex.valueOr(-1));
    aInfo.mNextInsertionIndex =
        AssertedCast<int32_t>(mVideoTracks.mNextInsertionIndex.valueOr(-1));
    media::TimeIntervals ranges = SafeBuffered(TrackInfo::kVideoTrack);
    dom::Sequence<dom::BufferRange> items;
    for (uint32_t i = 0; i < ranges.Length(); ++i) {
      // dom::Sequence is a FallibleTArray
      dom::BufferRange* range = items.AppendElement(fallible);
      if (!range) {
        break;
      }
      range->mStart = ranges.Start(i).ToSeconds();
      range->mEnd = ranges.End(i).ToSeconds();
    }
    aInfo.mRanges = std::move(items);
  }
}

void TrackBuffersManager::AddSizeOfResources(
    MediaSourceDecoder::ResourceSizes* aSizes) const {
  mTaskQueueCapability->AssertOnCurrentThread();

  if (mInputBuffer.isSome() && mInputBuffer->Buffer()) {
    // mInputBuffer should be the sole owner of the underlying buffer, so this
    // won't double count.
    aSizes->mByteSize += mInputBuffer->Buffer()->ShallowSizeOfIncludingThis(
        aSizes->mMallocSizeOf);
  }
  if (mInitData) {
    aSizes->mByteSize +=
        mInitData->ShallowSizeOfIncludingThis(aSizes->mMallocSizeOf);
  }
  if (mPendingInputBuffer.isSome() && mPendingInputBuffer->Buffer()) {
    // mPendingInputBuffer should be the sole owner of the underlying buffer, so
    // this won't double count.
    aSizes->mByteSize +=
        mPendingInputBuffer->Buffer()->ShallowSizeOfIncludingThis(
            aSizes->mMallocSizeOf);
  }

  mVideoTracks.AddSizeOfResources(aSizes);
  mAudioTracks.AddSizeOfResources(aSizes);
}

}  // namespace mozilla
#undef MSE_DEBUG
#undef MSE_DEBUGV
#undef SAMPLE_DEBUG
#undef SAMPLE_DEBUGV
