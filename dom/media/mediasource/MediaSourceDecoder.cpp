/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "MediaSourceDecoder.h"

#include <algorithm>

#include "ExternalEngineStateMachine.h"
#include "MediaDecoder.h"
#include "MediaDecoderStateMachine.h"
#include "MediaShutdownManager.h"
#include "MediaSource.h"
#include "MediaSourceDemuxer.h"
#include "MediaSourceUtils.h"
#include "SourceBuffer.h"
#include "SourceBufferList.h"
#include "VideoUtils.h"
#include "base/process_util.h"
#include "mozilla/Logging.h"

extern mozilla::LogModule* GetMediaSourceLog();

#define MSE_DEBUG(arg, ...)                                                  \
  DDMOZ_LOG_FMT(GetMediaSourceLog(), mozilla::LogLevel::Debug, "::{}: " arg, \
                __func__, ##__VA_ARGS__)
#define MSE_DEBUGV(arg, ...)                                                   \
  DDMOZ_LOG_FMT(GetMediaSourceLog(), mozilla::LogLevel::Verbose, "::{}: " arg, \
                __func__, ##__VA_ARGS__)

using namespace mozilla::media;

namespace mozilla {

MediaSourceDecoder::MediaSourceDecoder(MediaDecoderInit& aInit)
    : MediaDecoder(aInit), mMediaSource(nullptr), mEnded(false) {
  mExplicitDuration.emplace(UnspecifiedNaN<double>());
}

already_AddRefed<MediaDecoderStateMachineBase>
MediaSourceDecoder::CreateStateMachine(bool aDisableExternalEngine) {
  MOZ_ASSERT(NS_IsMainThread());
  // if `mDemuxer` already exists, that means we're in the process of recreating
  // the state machine. The track buffers are tied to the demuxer so we would
  // need to reuse it.
  if (!mDemuxer) {
    mDemuxer = new MediaSourceDemuxer(AbstractMainThread());
  }
  MediaFormatReaderInit init;
  init.mVideoFrameContainer = GetVideoFrameContainer();
  init.mCrashHelper = GetOwner()->CreateGMPCrashHelper();
  init.mFrameStats = mFrameStats;
  init.mMediaDecoderOwnerID = mOwner;
  static Atomic<uint32_t> sTrackingIdCounter(0);
  init.mTrackingId.emplace(TrackingId::Source::MSEDecoder, sTrackingIdCounter++,
                           TrackingId::TrackAcrossProcesses::Yes);
  mReader = new MediaFormatReader(init, mDemuxer);
#ifdef MOZ_WMF_CDM
  // ExternalEngineStateMachine is primarily used for encrypted playback when
  // the key system is supported via the WMF-based CDM. However, we cannot
  // currently determine the purpose of the playback, so we will always start
  // with ExternalEngineStateMachine. If this is not the case, we will switch
  // back to MediaDecoderStateMachine. The following outlines different
  // scenarios:
  // 1) Playback is non-encrypted or media format is not supported
  //    An internal error NS_ERROR_DOM_MEDIA_EXTERNAL_ENGINE_NOT_SUPPORTED_ERR
  //    will be received, resulting in a switch to another state machine.
  // 2) Playback is encrypted but the media key is not yet set
  //   2-1) If the CDMProxy is not WMF-based CDM when setting the media key,
  //        An internal error NS_ERROR_DOM_MEDIA_CDM_PROXY_NOT_SUPPORTED_ERR
  //        will be received, causing a switch to another state machine.
  //   2-2) If the CDMProxy is WMF-based CDM when setting the media key,
  //        There will be no error, and ExternalEngineStateMachine will operate.
  // 3) Playback is encrypted and the media key is already set
  //   3-1) If the CDMProxy is not WMF-based CDM,
  //        An internal error NS_ERROR_DOM_MEDIA_CDM_PROXY_NOT_SUPPORTED_ERR
  //        will be received, resulting in a switch to another state machine.
  //   3-2) If the CDMProxy is WMF-based CDM,
  //        There will be no error, and ExternalEngineStateMachine will operate.
  // Additionally, for testing purposes, non-encrypted playback can be performed
  // via ExternalEngineStateMachine as well by modifying the preference value.
  bool isCDMNotSupported =
      !!mOwner->GetCDMProxy() && !mOwner->GetCDMProxy()->AsWMFCDMProxy();
  if (StaticPrefs::media_wmf_media_engine_enabled() && !isCDMNotSupported &&
      !aDisableExternalEngine) {
    return MakeAndAddRef<ExternalEngineStateMachine>(this, mReader);
  }
#endif
  return MakeAndAddRef<MediaDecoderStateMachine>(this, mReader);
}

nsresult MediaSourceDecoder::Load(nsIPrincipal* aPrincipal) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!GetStateMachine());

  mPrincipal = aPrincipal;

  nsresult rv = MediaShutdownManager::Instance().Register(this);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  return CreateAndInitStateMachine(!mEnded);
}

template <typename IntervalType>
IntervalType MediaSourceDecoder::GetSeekableImpl() {
  MOZ_ASSERT(NS_IsMainThread());
  if (!mMediaSource) {
    NS_WARNING("MediaSource element isn't attached");
    return IntervalType();
  }

  TimeIntervals seekable;
  double duration = mMediaSource->Duration();
  if (std::isnan(duration)) {
    // Return empty range.
  } else if (duration > 0 && std::isinf(duration)) {
    media::TimeIntervals buffered = GetBuffered();

    // 1. If live seekable range is not empty:
    if (mMediaSource->HasLiveSeekableRange()) {
      // 1. Let union ranges be the union of live seekable range and the
      // HTMLMediaElement.buffered attribute.
      TimeRanges unionRanges =
          media::TimeRanges(buffered) + mMediaSource->LiveSeekableRange();
      // 2. Return a single range with a start time equal to the earliest start
      // time in union ranges and an end time equal to the highest end time in
      // union ranges and abort these steps.
      if constexpr (std::is_same_v<IntervalType, TimeRanges>) {
        TimeRanges seekableRange = media::TimeRanges(
            TimeRange(unionRanges.GetStart(), unionRanges.GetEnd()));
        return seekableRange;
      } else {
        MOZ_RELEASE_ASSERT(false);
      }
    }

    if (!buffered.IsEmpty()) {
      seekable += media::TimeInterval(TimeUnit::Zero(), buffered.GetEnd());
    }
  } else {
    if constexpr (std::is_same_v<IntervalType, TimeRanges>) {
      // Common case: seekable in entire range of the media.
      return TimeRanges(TimeRange(0, duration));
    } else if constexpr (std::is_same_v<IntervalType, TimeIntervals>) {
      seekable += media::TimeInterval(TimeUnit::Zero(),
                                      mDuration.match(DurationToTimeUnit()));
    } else {
      MOZ_RELEASE_ASSERT(false);
    }
  }
  MSE_DEBUG("ranges={}", DumpTimeRanges(seekable).get());
  return IntervalType(std::move(seekable));
}

media::TimeIntervals MediaSourceDecoder::GetSeekable() {
  return GetSeekableImpl<media::TimeIntervals>();
}

media::TimeRanges MediaSourceDecoder::GetSeekableTimeRanges() {
  return GetSeekableImpl<media::TimeRanges>();
}

media::TimeIntervals MediaSourceDecoder::GetBuffered() {
  MOZ_ASSERT(NS_IsMainThread());

  if (!mMediaSource) {
    NS_WARNING("MediaSource element isn't attached");
    return media::TimeIntervals::Invalid();
  }
  dom::SourceBufferList* sourceBuffers = mMediaSource->ActiveSourceBuffers();
  if (!sourceBuffers) {
    // Media source object is shutting down.
    return TimeIntervals();
  }
  TimeUnit highestEndTime;
  nsTArray<media::TimeIntervals> activeRanges;
  media::TimeIntervals buffered;

  for (uint32_t i = 0; i < sourceBuffers->Length(); i++) {
    bool found;
    dom::SourceBuffer* sb = sourceBuffers->IndexedGetter(i, found);
    MOZ_ASSERT(found);

    activeRanges.AppendElement(sb->GetTimeIntervals());
    highestEndTime =
        std::max(highestEndTime, activeRanges.LastElement().GetEnd());
  }

  buffered += media::TimeInterval(TimeUnit::Zero(), highestEndTime);

  for (auto& range : activeRanges) {
    if (mEnded && !range.IsEmpty()) {
      // Set the end time on the last range to highestEndTime by adding a
      // new range spanning the current end time to highestEndTime, which
      // Normalize() will then merge with the old last range.
      range += media::TimeInterval(range.GetEnd(), highestEndTime);
    }
    buffered.Intersection(range);
  }

  MSE_DEBUG("ranges={}", DumpTimeRanges(buffered).get());
  return buffered;
}

void MediaSourceDecoder::Shutdown() {
  MOZ_ASSERT(NS_IsMainThread());
  MSE_DEBUG("Shutdown");
  // Detach first so that TrackBuffers are unused on the main thread when
  // shut down on the decode task queue.
  if (mMediaSource) {
    mMediaSource->Detach();
  }
  mDemuxer = nullptr;

  MediaDecoder::Shutdown();
}

void MediaSourceDecoder::AttachMediaSource(dom::MediaSource* aMediaSource) {
  MOZ_ASSERT(!mMediaSource && !GetStateMachine() && NS_IsMainThread());
  mMediaSource = aMediaSource;
  DDLINKCHILD("mediasource", aMediaSource);
}

void MediaSourceDecoder::DetachMediaSource() {
  MOZ_ASSERT(mMediaSource && NS_IsMainThread());
  DDUNLINKCHILD(mMediaSource);
  mMediaSource = nullptr;
}

void MediaSourceDecoder::Ended(bool aEnded) {
  MOZ_ASSERT(NS_IsMainThread());
  if (aEnded) {
    // We want the MediaSourceReader to refresh its buffered range as it may
    // have been modified (end lined up).
    NotifyDataArrived();
  }
  mEnded = aEnded;
  GetStateMachine()->DispatchIsLiveStream(!mEnded);
}

void MediaSourceDecoder::AddSizeOfResources(ResourceSizes* aSizes) {
  MOZ_ASSERT(NS_IsMainThread());
  if (GetDemuxer()) {
    GetDemuxer()->AddSizeOfResources(aSizes);
  }
}

void MediaSourceDecoder::SetInitialDuration(const TimeUnit& aDuration) {
  MOZ_ASSERT(NS_IsMainThread());
  // Only use the decoded duration if one wasn't already
  // set.
  if (!mMediaSource || !std::isnan(ExplicitDuration())) {
    return;
  }
  SetMediaSourceDuration(aDuration);
}

void MediaSourceDecoder::SetMediaSourceDuration(const TimeUnit& aDuration) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!IsShutdown());
  if (aDuration.IsPositiveOrZero()) {
    // Truncate to microsecond resolution for consistency with the
    // SourceBuffer.buffered getter.
    TimeUnit inUsec = aDuration.ToBase(USECS_PER_S);
    if (inUsec.IsValid()) {
      SetExplicitDuration(inUsec.ToSeconds());
    } else {
      // ToBase(USECS_PER_S) overflows for near-INT64_MAX timestamps; fall back
      // to floating point precision to avoid crashing.
      MSE_DEBUG("duration overflow converting to usecs (duration={})",
                aDuration.ToString().get());
      SetExplicitDuration(aDuration.ToSeconds());
    }
  } else {
    SetExplicitDuration(PositiveInfinity<double>());
  }
}

void MediaSourceDecoder::SetMediaSourceDuration(double aDuration) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!IsShutdown());
  if (aDuration >= 0) {
    SetExplicitDuration(aDuration);
  } else {
    SetExplicitDuration(PositiveInfinity<double>());
  }
}

RefPtr<GenericPromise> MediaSourceDecoder::RequestDebugInfo(
    dom::MediaSourceDecoderDebugInfo& aInfo) {
  // This should be safe to call off main thead, but there's no such usage at
  // time of writing. Can be carefully relaxed if needed.
  MOZ_ASSERT(NS_IsMainThread(), "Expects to be called on main thread.");
  nsTArray<RefPtr<GenericPromise>> promises;
  if (mReader) {
    promises.AppendElement(mReader->RequestDebugInfo(aInfo.mReader));
  }
  if (mDemuxer) {
    promises.AppendElement(mDemuxer->GetDebugInfo(aInfo.mDemuxer));
  }
  return GenericPromise::All(GetCurrentSerialEventTarget(), promises)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          []() { return GenericPromise::CreateAndResolve(true, __func__); },
          [] {
            return GenericPromise::CreateAndReject(NS_ERROR_FAILURE, __func__);
          });
}

double MediaSourceDecoder::GetDuration() {
  MOZ_ASSERT(NS_IsMainThread());
  return ExplicitDuration();
}

MediaDecoderOwner::NextFrameStatus
MediaSourceDecoder::NextFrameBufferedStatus() {
  MOZ_ASSERT(NS_IsMainThread());

  if (!mMediaSource ||
      mMediaSource->ReadyState() == dom::MediaSourceReadyState::Closed) {
    return MediaDecoderOwner::NEXT_FRAME_UNAVAILABLE;
  }

  // Next frame hasn't been decoded yet.
  // Use the buffered range to consider if we have the next frame available.
  auto currentPosition = CurrentPosition();
  TimeIntervals buffered = GetBuffered();
  buffered.SetFuzz(MediaSourceDemuxer::EOS_FUZZ / 2);
  TimeInterval interval(
      currentPosition, currentPosition + DEFAULT_NEXT_FRAME_AVAILABLE_BUFFERED);
  return buffered.ContainsWithStrictEnd(ClampIntervalToEnd(interval))
             ? MediaDecoderOwner::NEXT_FRAME_AVAILABLE
             : MediaDecoderOwner::NEXT_FRAME_UNAVAILABLE;
}

bool MediaSourceDecoder::CanPlayThroughImpl() {
  MOZ_ASSERT(NS_IsMainThread());

  if (NextFrameBufferedStatus() == MediaDecoderOwner::NEXT_FRAME_UNAVAILABLE) {
    return false;
  }

  if (std::isnan(mMediaSource->Duration())) {
    // Don't have any data yet.
    return false;
  }
  TimeUnit duration = TimeUnit::FromSeconds(mMediaSource->Duration());
  auto currentPosition = CurrentPosition();
  if (duration <= currentPosition) {
    return true;
  }
  // If we have data up to the mediasource's duration or 3s ahead, we can
  // assume that we can play without interruption.
  TimeIntervals buffered = GetBuffered();
  buffered.SetFuzz(MediaSourceDemuxer::EOS_FUZZ / 2);
  TimeUnit timeAhead =
      std::min(duration, currentPosition + TimeUnit::FromSeconds(3));
  TimeInterval interval(currentPosition, timeAhead);
  return buffered.ToMicrosecondResolution().ContainsWithStrictEnd(
      ClampIntervalToEnd(interval));
}

TimeInterval MediaSourceDecoder::ClampIntervalToEnd(
    const TimeInterval& aInterval) {
  MOZ_ASSERT(NS_IsMainThread());

  if (!mEnded) {
    return aInterval;
  }
  TimeUnit duration = mDuration.match(DurationToTimeUnit());
  if (duration < aInterval.mStart) {
    return aInterval;
  }
  return TimeInterval(aInterval.mStart, std::min(aInterval.mEnd, duration),
                      aInterval.mFuzz);
}

void MediaSourceDecoder::NotifyInitDataArrived() {
  MOZ_ASSERT(NS_IsMainThread());
  if (mDemuxer) {
    mDemuxer->NotifyInitDataArrived();
  }
}

void MediaSourceDecoder::NotifyDataArrived() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(!IsShutdown());
  NotifyReaderDataArrived();
  GetOwner()->DownloadProgressed();
}

already_AddRefed<nsIPrincipal> MediaSourceDecoder::GetCurrentPrincipal() {
  MOZ_ASSERT(NS_IsMainThread());
  return do_AddRef(mPrincipal);
}

bool MediaSourceDecoder::HadCrossOriginRedirects() {
  MOZ_ASSERT(NS_IsMainThread());
  return false;
}

#ifdef MOZ_WMF_MEDIA_ENGINE
void MediaSourceDecoder::MetadataLoaded(
    UniquePtr<MediaInfo> aInfo, UniquePtr<MetadataTags> aTags,
    MediaDecoderEventVisibility aEventVisibility) {
  // If the metadata has been loaded before, we don't want to notify throughout
  // that again when switching from media engine playback to normal playback.
  if (mPendingStatusUpdateForNewlyCreatedStateMachine && mFiredMetadataLoaded) {
    MSE_DEBUG(
        "Metadata already loaded and being informed by previous state machine");
    SetStatusUpdateForNewlyCreatedStateMachineIfNeeded();
    return;
  }
  MediaDecoder::MetadataLoaded(std::move(aInfo), std::move(aTags),
                               aEventVisibility);
}
#endif

#undef MSE_DEBUG
#undef MSE_DEBUGV

}  // namespace mozilla
