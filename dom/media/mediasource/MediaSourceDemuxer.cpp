/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaSourceDemuxer.h"

#include <stdint.h>

#include <algorithm>

#include "MediaSourceUtils.h"
#include "SourceBufferList.h"
#include "VideoUtils.h"
#include "nsPrintfCString.h"

extern mozilla::LogModule* GetMediaSourceLog();

#define MSE_DEBUG(arg, ...)                                                  \
  DDMOZ_LOG_FMT(GetMediaSourceLog(), mozilla::LogLevel::Debug, "::{}: " arg, \
                __func__, ##__VA_ARGS__)
#define MSE_DEBUGV(arg, ...)                                                   \
  DDMOZ_LOG_FMT(GetMediaSourceLog(), mozilla::LogLevel::Verbose, "::{}: " arg, \
                __func__, ##__VA_ARGS__)

namespace mozilla {

typedef TrackInfo::TrackType TrackType;
using media::TimeInterval;
using media::TimeIntervals;
using media::TimeUnit;

MediaSourceDemuxer::MediaSourceDemuxer(AbstractThread* aAbstractMainThread)
    : mTaskQueue(
          TaskQueue::Create(GetMediaThreadPool(MediaThreadType::SUPERVISOR),
                            "MediaSourceDemuxer::mTaskQueue")),
      mMutex("MediaSourceDemuxer") {
  MOZ_ASSERT(NS_IsMainThread());
}

constexpr TimeUnit MediaSourceDemuxer::EOS_FUZZ;
constexpr TimeUnit MediaSourceDemuxer::EOS_FUZZ_START;

RefPtr<MediaSourceDemuxer::InitPromise> MediaSourceDemuxer::Init() {
  RefPtr<MediaSourceDemuxer> self = this;
  return InvokeAsync(GetTaskQueue(), __func__, [self]() {
    if (self->ScanSourceBuffersForContent()) {
      return InitPromise::CreateAndResolve(NS_OK, __func__);
    }

    RefPtr<InitPromise> p = self->mInitPromise.Ensure(__func__);

    return p;
  });
}

void MediaSourceDemuxer::AddSizeOfResources(
    MediaSourceDecoder::ResourceSizes* aSizes) {
  MOZ_ASSERT(NS_IsMainThread());

  // NB: The track buffers must only be accessed on the TaskQueue.
  RefPtr<MediaSourceDemuxer> self = this;
  RefPtr<MediaSourceDecoder::ResourceSizes> sizes = aSizes;
  nsCOMPtr<nsIRunnable> task = NS_NewRunnableFunction(
      "MediaSourceDemuxer::AddSizeOfResources", [self, sizes]() {
        for (const RefPtr<TrackBuffersManager>& manager :
             self->mSourceBuffers) {
          manager->AddSizeOfResources(sizes);
        }
      });

  nsresult rv = GetTaskQueue()->Dispatch(task.forget());
  MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
  (void)rv;
}

void MediaSourceDemuxer::NotifyInitDataArrived() {
  RefPtr<MediaSourceDemuxer> self = this;
  nsCOMPtr<nsIRunnable> task = NS_NewRunnableFunction(
      "MediaSourceDemuxer::NotifyInitDataArrived", [self]() {
        if (self->mInitPromise.IsEmpty()) {
          return;
        }
        if (self->ScanSourceBuffersForContent()) {
          self->mInitPromise.ResolveIfExists(NS_OK, __func__);
        }
      });
  nsresult rv = GetTaskQueue()->Dispatch(task.forget());
  MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
  (void)rv;
}

bool MediaSourceDemuxer::ScanSourceBuffersForContent() {
  MOZ_ASSERT(OnTaskQueue());

  if (mSourceBuffers.IsEmpty()) {
    return false;
  }

  MutexAutoLock mon(mMutex);

  bool haveEmptySourceBuffer = false;
  for (const auto& sourceBuffer : mSourceBuffers) {
    MediaInfo info = sourceBuffer->GetMetadata();
    if (!info.HasAudio() && !info.HasVideo()) {
      haveEmptySourceBuffer = true;
    }
    if (info.HasAudio() && !mAudioTrack) {
      mInfo.mAudio = info.mAudio;
      mAudioTrack = sourceBuffer;
    }
    if (info.HasVideo() && !mVideoTrack) {
      mInfo.mVideo = info.mVideo;
      mVideoTrack = sourceBuffer;
    }
    if (info.IsEncrypted() && !mInfo.IsEncrypted()) {
      mInfo.mCrypto = info.mCrypto;
    }
  }
  if (mInfo.HasAudio() && mInfo.HasVideo()) {
    // We have both audio and video. We can ignore non-ready source buffer.
    return true;
  }
  return !haveEmptySourceBuffer;
}

uint32_t MediaSourceDemuxer::GetNumberTracks(TrackType aType) const {
  MutexAutoLock mon(mMutex);

  switch (aType) {
    case TrackType::kAudioTrack:
      return mInfo.HasAudio() ? 1u : 0;
    case TrackType::kVideoTrack:
      return mInfo.HasVideo() ? 1u : 0;
    default:
      return 0;
  }
}

already_AddRefed<MediaTrackDemuxer> MediaSourceDemuxer::GetTrackDemuxer(
    TrackType aType, uint32_t aTrackNumber) {
  MutexAutoLock mon(mMutex);
  RefPtr<TrackBuffersManager> manager = GetManager(aType);
  if (!manager) {
    return nullptr;
  }
  RefPtr<MediaSourceTrackDemuxer> e =
      new MediaSourceTrackDemuxer(this, aType, manager);
  DDLINKCHILD("track demuxer", e.get());
  mDemuxers.AppendElement(e);
  return e.forget();
}

bool MediaSourceDemuxer::IsSeekable() const { return true; }

UniquePtr<EncryptionInfo> MediaSourceDemuxer::GetCrypto() {
  MutexAutoLock mon(mMutex);
  auto crypto = std::make_unique<EncryptionInfo>();
  *crypto = mInfo.mCrypto;
  return crypto;
}

void MediaSourceDemuxer::AttachSourceBuffer(
    const RefPtr<TrackBuffersManager>& aSourceBuffer) {
  nsCOMPtr<nsIRunnable> task = NewRunnableMethod<RefPtr<TrackBuffersManager>&&>(
      "MediaSourceDemuxer::DoAttachSourceBuffer", this,
      &MediaSourceDemuxer::DoAttachSourceBuffer, aSourceBuffer);
  nsresult rv = GetTaskQueue()->Dispatch(task.forget());
  MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
  (void)rv;
}

void MediaSourceDemuxer::DoAttachSourceBuffer(
    RefPtr<mozilla::TrackBuffersManager>&& aSourceBuffer) {
  MOZ_ASSERT(OnTaskQueue());
  mSourceBuffers.AppendElement(std::move(aSourceBuffer));
  ScanSourceBuffersForContent();
}

void MediaSourceDemuxer::DetachSourceBuffer(
    const RefPtr<TrackBuffersManager>& aSourceBuffer) {
  nsCOMPtr<nsIRunnable> task =
      NS_NewRunnableFunction("MediaSourceDemuxer::DoDetachSourceBuffer",
                             [self = RefPtr{this}, aSourceBuffer]() {
                               self->DoDetachSourceBuffer(aSourceBuffer);
                             });
  nsresult rv = GetTaskQueue()->Dispatch(task.forget());
  MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
  (void)rv;
}

void MediaSourceDemuxer::DoDetachSourceBuffer(
    const RefPtr<TrackBuffersManager>& aSourceBuffer) {
  MOZ_ASSERT(OnTaskQueue());
  mSourceBuffers.RemoveElementsBy(
      [&aSourceBuffer](const RefPtr<TrackBuffersManager> aLinkedSourceBuffer) {
        return aLinkedSourceBuffer == aSourceBuffer;
      });

  AutoTArray<RefPtr<MediaSourceTrackDemuxer>, 2> matchingDemuxers;
  {
    MutexAutoLock mon(mMutex);
    if (aSourceBuffer == mAudioTrack) {
      mAudioTrack = nullptr;
    }
    if (aSourceBuffer == mVideoTrack) {
      mVideoTrack = nullptr;
    }

    mDemuxers.RemoveElementsBy(
        [&](RefPtr<MediaSourceTrackDemuxer>& elementRef) {
          if (!elementRef->HasManager(aSourceBuffer)) {
            return false;
          }
          matchingDemuxers.AppendElement(std::move(elementRef));
          return true;
        });
  }

  for (MediaSourceTrackDemuxer* demuxer : matchingDemuxers) {
    demuxer->DetachManager();
  }
  ScanSourceBuffersForContent();
}

TrackInfo* MediaSourceDemuxer::GetTrackInfo(TrackType aTrack) {
  switch (aTrack) {
    case TrackType::kAudioTrack:
      return &mInfo.mAudio;
    case TrackType::kVideoTrack:
      return &mInfo.mVideo;
    default:
      return nullptr;
  }
}

RefPtr<TrackBuffersManager> MediaSourceDemuxer::GetManager(TrackType aTrack) {
  switch (aTrack) {
    case TrackType::kAudioTrack:
      return mAudioTrack;
    case TrackType::kVideoTrack:
      return mVideoTrack;
    default:
      return nullptr;
  }
}

MediaSourceDemuxer::~MediaSourceDemuxer() {
  mInitPromise.RejectIfExists(NS_ERROR_DOM_MEDIA_CANCELED, __func__);
}

RefPtr<GenericPromise> MediaSourceDemuxer::GetDebugInfo(
    dom::MediaSourceDemuxerDebugInfo& aInfo) const {
  MutexAutoLock mon(mMutex);
  nsTArray<RefPtr<GenericPromise>> promises;
  if (mAudioTrack) {
    promises.AppendElement(mAudioTrack->RequestDebugInfo(aInfo.mAudioTrack));
  }
  if (mVideoTrack) {
    promises.AppendElement(mVideoTrack->RequestDebugInfo(aInfo.mVideoTrack));
  }
  return GenericPromise::All(GetCurrentSerialEventTarget(), promises)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          []() { return GenericPromise::CreateAndResolve(true, __func__); },
          [] {
            return GenericPromise::CreateAndReject(NS_ERROR_FAILURE, __func__);
          });
}

MediaSourceTrackDemuxer::MediaSourceTrackDemuxer(MediaSourceDemuxer* aParent,
                                                 TrackInfo::TrackType aType,
                                                 TrackBuffersManager* aManager)
    : mParent(aParent),
      mType(aType),
      mLock("MediaSourceTrackDemuxer", mParent->GetTaskQueue()),
      mManager(aManager),
      mReset(true),
      mPreRoll(TimeUnit::FromMicroseconds(
          mParent->GetTrackInfo(mType)->mMimeType.EqualsLiteral("audio/opus") ||
                  mParent->GetTrackInfo(mType)->mMimeType.EqualsLiteral(
                      "audio/vorbis")
              ? 80000
          : mParent->GetTrackInfo(mType)->mMimeType.EqualsLiteral(
                "audio/mp4a-latm")
              // AAC encoder delay is by default 2112 audio frames.
              // See
              // https://developer.apple.com/library/content/documentation/QuickTime/QTFF/QTFFAppenG/QTFFAppenG.html
              // So we always seek 2112 frames
              ? (2112 * 1000000ULL /
                 mParent->GetTrackInfo(mType)->GetAsAudioInfo()->mRate)
              : 0)) {
  MOZ_ASSERT(mParent);
  MOZ_ASSERT(mLock.Target().GetEventTarget());
}

UniquePtr<TrackInfo> MediaSourceTrackDemuxer::GetInfo() const {
  MutexAutoLock mon(mParent->mMutex);
  return mParent->GetTrackInfo(mType)->Clone();
}

RefPtr<MediaSourceTrackDemuxer::SeekPromise> MediaSourceTrackDemuxer::Seek(
    const TimeUnit& aTime) {
  MOZ_ASSERT(mParent, "Called after BreackCycle()");
  return InvokeAsync(mParent->GetTaskQueue(), this, __func__,
                     &MediaSourceTrackDemuxer::DoSeek, aTime);
}

RefPtr<MediaSourceTrackDemuxer::SamplesPromise>
MediaSourceTrackDemuxer::GetSamples(int32_t aNumSamples) {
  MOZ_ASSERT(mParent, "Called after BreackCycle()");
  return InvokeAsync(mParent->GetTaskQueue(), this, __func__,
                     &MediaSourceTrackDemuxer::DoGetSamples, aNumSamples);
}

void MediaSourceTrackDemuxer::Reset() {
  MOZ_ASSERT(mParent, "Called after BreackCycle()");
  RefPtr<MediaSourceTrackDemuxer> self = this;
  nsCOMPtr<nsIRunnable> task =
      NS_NewRunnableFunction("MediaSourceTrackDemuxer::Reset", [self]() {
        self->TaskQueue().AssertOnCurrentThread();
        self->mLock.NoteOnTarget();

        self->mNextSample.reset();
        self->mReset = true;
        self->mLastStreamID.reset();
        if (!self->mManager) {
          return;
        }
        self->mManager->Seek(self->mType, TimeUnit::Zero(), TimeUnit::Zero());
        {
          MutexAutoLock lock(self->Mutex());
          self->mLock.ClearCurrentAccess();
          self->mLock.NoteExclusiveAccess();
          self->mNextRandomAccessPoint =
              self->mManager->GetNextRandomAccessPoint(
                  self->mType, MediaSourceDemuxer::EOS_FUZZ);
        }
      });
  nsresult rv = mParent->GetTaskQueue()->Dispatch(task.forget());
  MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
  (void)rv;
}

nsresult MediaSourceTrackDemuxer::GetNextRandomAccessPoint(TimeUnit* aTime) {
  MutexAutoLock lock(Mutex());
  mLock.NoteLockHeld();
  *aTime = mNextRandomAccessPoint;
  return NS_OK;
}

RefPtr<MediaSourceTrackDemuxer::SkipAccessPointPromise>
MediaSourceTrackDemuxer::SkipToNextRandomAccessPoint(
    const TimeUnit& aTimeThreshold) {
  return InvokeAsync(mParent->GetTaskQueue(), this, __func__,
                     &MediaSourceTrackDemuxer::DoSkipToNextRandomAccessPoint,
                     aTimeThreshold);
}

media::TimeIntervals MediaSourceTrackDemuxer::GetBuffered() {
  MutexAutoLock lock(Mutex());
  mLock.NoteLockHeld();
  if (!mManager) {
    return media::TimeIntervals();
  }
  return mManager->Buffered();
}

void MediaSourceTrackDemuxer::BreakCycles() {
  RefPtr<MediaSourceTrackDemuxer> self = this;
  nsCOMPtr<nsIRunnable> task =
      NS_NewRunnableFunction("MediaSourceTrackDemuxer::BreakCycles", [self]() {
        self->DetachManager();
        self->mParent = nullptr;
      });
  nsresult rv = mParent->GetTaskQueue()->Dispatch(task.forget());
  MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
  (void)rv;
}

RefPtr<MediaSourceTrackDemuxer::SeekPromise> MediaSourceTrackDemuxer::DoSeek(
    const TimeUnit& aTime) {
  TaskQueue().AssertOnCurrentThread();
  mLock.NoteOnTarget();

  if (!mManager) {
    return SeekPromise::CreateAndReject(
        MediaResult(NS_ERROR_DOM_MEDIA_CANCELED,
                    RESULT_DETAIL("manager is detached.")),
        __func__);
  }

  TimeIntervals buffered = mManager->Buffered(mType);
  // Fuzz factor represents a +/- threshold. So when seeking it allows the gap
  // to be twice as big as the fuzz value. We only want to allow EOS_FUZZ gap.
  buffered.SetFuzz(MediaSourceDemuxer::EOS_FUZZ / 2);
  TimeUnit seekTime = std::max(aTime - mPreRoll, TimeUnit::Zero());

  if (mManager->HaveAllData() && seekTime >= buffered.GetEnd()) {
    // We're attempting to seek past the end time. Cap seekTime so that we seek
    // to the last sample instead.
    seekTime = std::max(mManager->HighestStartTime(mType) - mPreRoll,
                        TimeUnit::Zero());
  }

  MSE_DEBUG("DoSeek, original target={}{}, seekTime={}{}, buffered={}",
            aTime.ToMicroseconds(), aTime.ToString().get(),
            seekTime.ToMicroseconds(), seekTime.ToString().get(),
            DumpTimeRanges(buffered).get());
  if (!buffered.ContainsWithStrictEnd(seekTime)) {
    if (!buffered.ContainsWithStrictEnd(aTime)) {
      // Target isn't in the buffered range, so we can perform an eviction if
      // needed.
      mManager->EvictDataWithoutSize(mType, seekTime);
      // We don't have the data to seek to.
      return SeekPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA,
                                          __func__);
    }
    // Theoretically we should reject the promise with WAITING_FOR_DATA,
    // however, to avoid unwanted regressions we assume that if at this time
    // we don't have the wanted data it won't come later.
    // Instead of using the pre-rolled time, use the earliest time available in
    // the interval.
    TimeIntervals::IndexType index = buffered.Find(aTime);
    MOZ_ASSERT(index != TimeIntervals::NoIndex);
    MSE_DEBUG(
        "Can't find seekTime {} in the buffer range, use the earliest "
        "time {}",
        seekTime.ToMicroseconds(), buffered[index].mStart.ToMicroseconds());
    seekTime = buffered[index].mStart;
  }
  seekTime = mManager->Seek(mType, seekTime, MediaSourceDemuxer::EOS_FUZZ);
  mLastStreamID.reset();
  MediaResult result = NS_OK;
  RefPtr<MediaRawData> sample =
      mManager->GetSample(mType, TimeUnit::Zero(), result);
  MOZ_ASSERT(NS_SUCCEEDED(result) && sample);
  if (sample) {
    mNextSample = Some(sample);
  }
  mReset = false;
  {
    MutexAutoLock lock(Mutex());
    mLock.ClearCurrentAccess();
    mLock.NoteExclusiveAccess();
    mNextRandomAccessPoint =
        mManager->GetNextRandomAccessPoint(mType, MediaSourceDemuxer::EOS_FUZZ);
  }
  return SeekPromise::CreateAndResolve(seekTime, __func__);
}

RefPtr<MediaSourceTrackDemuxer::SamplesPromise>
MediaSourceTrackDemuxer::DoGetSamples(int32_t aNumSamples) {
  TaskQueue().AssertOnCurrentThread();
  mLock.NoteOnTarget();
  if (!mManager) {
    return SamplesPromise::CreateAndReject(
        MediaResult(NS_ERROR_DOM_MEDIA_CANCELED,
                    RESULT_DETAIL("manager is detached.")),
        __func__);
  }

  if (mReset) {
    // If a reset was recently performed, we ensure that the data
    // we are about to retrieve is still available.
    TimeIntervals buffered = mManager->Buffered(mType);
    if (buffered.IsEmpty() && mManager->HaveAllData()) {
      return SamplesPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_END_OF_STREAM,
                                             __func__);
    }

    // We use a larger fuzz to determine the presentation start
    // time than the fuzz we use to determine acceptable gaps between
    // frames. This is needed to fix embedded video issues as seen in the wild
    // from different muxed stream start times.
    // See: https://www.w3.org/TR/media-source-2/#presentation-start-time
    buffered.SetFuzz(MediaSourceDemuxer::EOS_FUZZ_START);
    if (!buffered.ContainsWithStrictEnd(TimeUnit::Zero())) {
      return SamplesPromise::CreateAndReject(
          NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA, __func__);
    }
    mReset = false;
  }
  RefPtr<MediaRawData> sample;
  if (mNextSample) {
    sample = mNextSample.ref();
    mNextSample.reset();
  } else {
    MediaResult result = NS_OK;
    sample = mManager->GetSample(mType, MediaSourceDemuxer::EOS_FUZZ, result);
    if (!sample) {
      if (result == NS_ERROR_DOM_MEDIA_END_OF_STREAM ||
          result == NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA) {
        return SamplesPromise::CreateAndReject(
            (result == NS_ERROR_DOM_MEDIA_END_OF_STREAM &&
             mManager->HaveAllData())
                ? NS_ERROR_DOM_MEDIA_END_OF_STREAM
                : NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA,
            __func__);
      }
      return SamplesPromise::CreateAndReject(result, __func__);
    }
  }
  MOZ_DIAGNOSTIC_ASSERT(sample);
  {
    MutexAutoLock lock(Mutex());
    mLock.ClearCurrentAccess();
    mLock.NoteExclusiveAccess();
    // Diagnostic asserts for bug 1810396
    MOZ_DIAGNOSTIC_ASSERT(sample, "Invalid sample pointer found!");
    MOZ_DIAGNOSTIC_ASSERT(sample->HasValidTime(), "Invalid sample time found!");
    if (!sample) {
      return SamplesPromise::CreateAndReject(NS_ERROR_NULL_POINTER, __func__);
    }
    if (mNextRandomAccessPoint <= sample->mTime) {
      mNextRandomAccessPoint = mManager->GetNextRandomAccessPoint(
          mType, MediaSourceDemuxer::EOS_FUZZ);
    }
  }
  CompensateDecoderDelay(sample);
  RefPtr<SamplesHolder> samples = new SamplesHolder;
  samples->AppendSample(std::move(sample));
  return SamplesPromise::CreateAndResolve(samples, __func__);
}

void MediaSourceTrackDemuxer::CompensateDecoderDelay(MediaRawData* aSample) {
  TaskQueue().AssertOnCurrentThread();
  MOZ_ASSERT(aSample->mTrackInfo);
  const uint32_t streamID = aSample->mTrackInfo->GetID();
  const bool decoderRestarts = mLastStreamID != Some(streamID);
  mLastStreamID = Some(streamID);

  const AudioInfo* info = aSample->mTrackInfo->GetAsAudioInfo();
  if (!info || !info->mRate ||
      !info->mCodecSpecificConfig.is<Mp3CodecSpecificData>()) {
    return;
  }
  const uint32_t delayFrames =
      info->mCodecSpecificConfig.as<Mp3CodecSpecificData>().mDecoderDelayFrames;
  if (!delayFrames) {
    return;
  }
  const TimeUnit delay(delayFrames, info->mRate);

  // The interval of the frame and the part of it to keep, which differ when
  // the frame was trimmed to the append window.
  const TimeInterval frame = aSample->mOriginalPresentationWindow.valueOr(
      TimeInterval(aSample->mTime, aSample->GetEndTime()));
  const TimeInterval kept(aSample->mTime, aSample->GetEndTime());
  const TimeInterval decoded(frame.mStart - delay, frame.mEnd - delay);

  // The audio before the kept part of a frame is dropped, including what the
  // decoder only outputs with the following frames. After a restart, so is
  // what the decoder outputs before the first frame.
  if (kept.mStart > frame.mStart) {
    mTrimBefore = Some(kept.mStart);
  } else if (decoderRestarts) {
    mTrimBefore = Some(frame.mStart);
  }
  TimeInterval trimmed = decoded;
  if (mTrimBefore) {
    trimmed.mStart = std::clamp(*mTrimBefore, decoded.mStart, decoded.mEnd);
    if (*mTrimBefore <= decoded.mEnd) {
      mTrimBefore.reset();
    }
  }
  if (kept.mEnd < frame.mEnd) {
    trimmed.mEnd = std::clamp(kept.mEnd, trimmed.mStart, decoded.mEnd);
  }

  MSE_DEBUGV(
      "stream={} decoderRestarts={} frame=[{},{}] kept=[{},{}] "
      "decoded=[{},{}] trimmed=[{},{}]",
      streamID, decoderRestarts, frame.mStart.ToMicroseconds(),
      frame.mEnd.ToMicroseconds(), kept.mStart.ToMicroseconds(),
      kept.mEnd.ToMicroseconds(), decoded.mStart.ToMicroseconds(),
      decoded.mEnd.ToMicroseconds(), trimmed.mStart.ToMicroseconds(),
      trimmed.mEnd.ToMicroseconds());

  aSample->mTime = trimmed.mStart;
  aSample->mDuration = trimmed.Length();
  aSample->mTimecode = aSample->mTimecode - delay;
  if (trimmed == decoded) {
    aSample->mOriginalPresentationWindow.reset();
  } else {
    aSample->mOriginalPresentationWindow = Some(decoded);
  }
}

RefPtr<MediaSourceTrackDemuxer::SkipAccessPointPromise>
MediaSourceTrackDemuxer::DoSkipToNextRandomAccessPoint(
    const TimeUnit& aTimeThreadshold) {
  TaskQueue().AssertOnCurrentThread();
  mLock.NoteOnTarget();

  if (!mManager) {
    return SkipAccessPointPromise::CreateAndReject(
        SkipFailureHolder(MediaResult(NS_ERROR_DOM_MEDIA_CANCELED,
                                      RESULT_DETAIL("manager is detached.")),
                          0),
        __func__);
  }

  uint32_t parsed = 0;
  // Ensure that the data we are about to skip to is still available.
  TimeIntervals buffered = mManager->Buffered(mType);
  buffered.SetFuzz(MediaSourceDemuxer::EOS_FUZZ / 2);
  if (buffered.ContainsWithStrictEnd(aTimeThreadshold)) {
    bool found;
    parsed = mManager->SkipToNextRandomAccessPoint(
        mType, aTimeThreadshold, MediaSourceDemuxer::EOS_FUZZ, found);
    if (found) {
      return SkipAccessPointPromise::CreateAndResolve(parsed, __func__);
    }
  }
  SkipFailureHolder holder(mManager->HaveAllData()
                               ? NS_ERROR_DOM_MEDIA_END_OF_STREAM
                               : NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA,
                           parsed);
  return SkipAccessPointPromise::CreateAndReject(holder, __func__);
}

bool MediaSourceTrackDemuxer::HasManager(TrackBuffersManager* aManager) const {
  TaskQueue().AssertOnCurrentThread();
  mLock.NoteOnTarget();
  return mManager == aManager;
}

void MediaSourceTrackDemuxer::DetachManager() {
  TaskQueue().AssertOnCurrentThread();
  MutexAutoLock lock(Mutex());
  mLock.NoteExclusiveAccess();
  mManager = nullptr;
}

#undef MSE_DEBUG
#undef MSE_DEBUGV

}  // namespace mozilla
