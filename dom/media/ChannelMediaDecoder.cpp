/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ChannelMediaDecoder.h"

#include "BaseMediaResource.h"
#include "ChannelMediaResource.h"
#include "DecoderTraits.h"
#include "ExternalEngineStateMachine.h"
#include "MediaDecoderStateMachine.h"
#include "MediaFormatReader.h"
#include "MediaShutdownManager.h"
#include "VideoUtils.h"
#include "base/process_util.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_media.h"

namespace mozilla {

using TimeUnit = media::TimeUnit;

extern LazyLogModule gMediaDecoderLog;
#define LOG(x, ...) \
  DDMOZ_LOG_FMT(gMediaDecoderLog, LogLevel::Debug, x, ##__VA_ARGS__)
#define LOGD(x, ...) \
  MOZ_LOG_FMT(gMediaDecoderLog, LogLevel::Debug, x, ##__VA_ARGS__)

ChannelMediaDecoder::ResourceCallback::ResourceCallback(
    AbstractThread* aMainThread)
    : mAbstractMainThread(aMainThread) {
  MOZ_ASSERT(aMainThread);
  DecoderDoctorLogger::LogConstructionAndBase(
      "ChannelMediaDecoder::ResourceCallback", this,
      static_cast<const MediaResourceCallback*>(this));
}

ChannelMediaDecoder::ResourceCallback::~ResourceCallback() {
  DecoderDoctorLogger::LogDestruction("ChannelMediaDecoder::ResourceCallback",
                                      this);
}

void ChannelMediaDecoder::ResourceCallback::Connect(
    ChannelMediaDecoder* aDecoder) {
  MOZ_ASSERT(NS_IsMainThread());
  mDecoder = aDecoder;
  DecoderDoctorLogger::LinkParentAndChild(
      "ChannelMediaDecoder::ResourceCallback", this, "decoder", mDecoder);
  mTimer = NS_NewTimer(mAbstractMainThread->AsEventTarget());
}

void ChannelMediaDecoder::ResourceCallback::Disconnect() {
  MOZ_ASSERT(NS_IsMainThread());
  if (mDecoder) {
    DecoderDoctorLogger::UnlinkParentAndChild(
        "ChannelMediaDecoder::ResourceCallback", this, mDecoder);
    mDecoder = nullptr;
    mTimer->Cancel();
    mTimer = nullptr;
  }
}

AbstractThread* ChannelMediaDecoder::ResourceCallback::AbstractMainThread()
    const {
  return mAbstractMainThread;
}

MediaDecoderOwner* ChannelMediaDecoder::ResourceCallback::GetMediaOwner()
    const {
  MOZ_ASSERT(NS_IsMainThread());
  return mDecoder ? mDecoder->GetOwner() : nullptr;
}

void ChannelMediaDecoder::ResourceCallback::NotifyNetworkError(
    const MediaResult& aError) {
  MOZ_ASSERT(NS_IsMainThread());
  DDLOGEX2("ChannelMediaDecoder::ResourceCallback", this, DDLogCategory::Log,
           "network_error", aError);
  if (mDecoder) {
    mDecoder->NetworkError(aError);
  }
}

/* static */
void ChannelMediaDecoder::ResourceCallback::TimerCallback(nsITimer* aTimer,
                                                          void* aClosure) {
  MOZ_ASSERT(NS_IsMainThread());
  ResourceCallback* thiz = static_cast<ResourceCallback*>(aClosure);
  MOZ_ASSERT(thiz->mDecoder);
  thiz->mDecoder->NotifyReaderDataArrived();
  thiz->mTimerArmed = false;
}

void ChannelMediaDecoder::ResourceCallback::NotifyDataArrived() {
  MOZ_ASSERT(NS_IsMainThread());
  DDLOGEX2("ChannelMediaDecoder::ResourceCallback", this, DDLogCategory::Log,
           "data_arrived", true);

  if (!mDecoder) {
    return;
  }

  RefPtr<ChannelMediaDecoder> decoder = mDecoder;
  decoder->DownloadProgressed();

  if (!mDecoder || mTimerArmed) {
    return;
  }
  // In situations where these notifications come from stochastic network
  // activity, we can save significant computation by throttling the
  // calls to MediaDecoder::NotifyDataArrived() which will update the buffer
  // ranges of the reader.
  mTimerArmed = true;
  mTimer->InitWithNamedFuncCallback(
      TimerCallback, this, sDelay, nsITimer::TYPE_ONE_SHOT,
      "ChannelMediaDecoder::ResourceCallback::TimerCallback"_ns);
}

void ChannelMediaDecoder::ResourceCallback::NotifyDataEnded(nsresult aStatus) {
  DDLOGEX2("ChannelMediaDecoder::ResourceCallback", this, DDLogCategory::Log,
           "data_ended", aStatus);
  MOZ_ASSERT(NS_IsMainThread());
  if (mDecoder) {
    mDecoder->NotifyDownloadEnded(aStatus);
  }
}

void ChannelMediaDecoder::ResourceCallback::NotifyPrincipalChanged() {
  MOZ_ASSERT(NS_IsMainThread());
  DDLOGEX2("ChannelMediaDecoder::ResourceCallback", this, DDLogCategory::Log,
           "principal_changed", true);
  if (mDecoder) {
    mDecoder->NotifyPrincipalChanged();
  }
}

void ChannelMediaDecoder::NotifyPrincipalChanged() {
  MOZ_ASSERT(NS_IsMainThread());
  MediaDecoder::NotifyPrincipalChanged();
  if (!mInitialChannelPrincipalKnown) {
    // We'll receive one notification when the channel's initial principal
    // is known, after all HTTP redirects have resolved. This isn't really a
    // principal change, so return here to avoid the mSameOriginMedia check
    // below.
    mInitialChannelPrincipalKnown = true;
    return;
  }
  if (!mSameOriginMedia) {
    // Block mid-flight redirects to non CORS same origin destinations.
    // See bugs 1441153, 1443942.
    LOG("ChannnelMediaDecoder prohibited cross origin redirect blocked.");
    NetworkError(MediaResult(NS_ERROR_DOM_BAD_URI,
                             "Prohibited cross origin redirect blocked"));
  }
}

void ChannelMediaDecoder::ResourceCallback::NotifySuspendedStatusChanged(
    bool aSuspendedByCache) {
  MOZ_ASSERT(NS_IsMainThread());
  DDLOGEX2("ChannelMediaDecoder::ResourceCallback", this, DDLogCategory::Log,
           "suspended_status_changed", aSuspendedByCache);
  MediaDecoderOwner* owner = GetMediaOwner();
  if (owner) {
    owner->NotifySuspendedByCache(aSuspendedByCache);
  }
}

ChannelMediaDecoder::ChannelMediaDecoder(MediaDecoderInit& aInit)
    : MediaDecoder(aInit),
      mResourceCallback(
          new ResourceCallback(aInit.mOwner->AbstractMainThread())) {
  mResourceCallback->Connect(this);
}

/* static */
already_AddRefed<ChannelMediaDecoder> ChannelMediaDecoder::Create(
    MediaDecoderInit& aInit, DecoderDoctorDiagnostics* aDiagnostics) {
  MOZ_ASSERT(NS_IsMainThread());
  RefPtr<ChannelMediaDecoder> decoder;
  if (DecoderTraits::CanHandleContainerType(aInit.mContainerType,
                                            aDiagnostics) != CANPLAY_NO) {
    decoder = new ChannelMediaDecoder(aInit);
    return decoder.forget();
  }

  return nullptr;
}

bool ChannelMediaDecoder::CanClone() {
  MOZ_ASSERT(NS_IsMainThread());
  return mResource && mResource->CanClone();
}

already_AddRefed<ChannelMediaDecoder> ChannelMediaDecoder::Clone(
    MediaDecoderInit& aInit) {
  if (!mResource || DecoderTraits::CanHandleContainerType(
                        aInit.mContainerType, nullptr) == CANPLAY_NO) {
    return nullptr;
  }
  RefPtr<ChannelMediaDecoder> decoder = new ChannelMediaDecoder(aInit);
  nsresult rv = decoder->Load(mResource);
  if (NS_FAILED(rv)) {
    decoder->Shutdown();
    return nullptr;
  }
  return decoder.forget();
}

already_AddRefed<MediaDecoderStateMachineBase>
ChannelMediaDecoder::CreateStateMachine(bool aDisableExternalEngine) {
  MOZ_ASSERT(NS_IsMainThread());
  MediaFormatReaderInit init;
  init.mVideoFrameContainer = GetVideoFrameContainer();
  init.mCrashHelper = GetOwner()->CreateGMPCrashHelper();
  init.mFrameStats = mFrameStats;
  init.mResource = mResource;
  init.mMediaDecoderOwnerID = mOwner;
  static Atomic<uint32_t> sTrackingIdCounter(0);
  init.mTrackingId.emplace(TrackingId::Source::ChannelDecoder,
                           sTrackingIdCounter++,
                           TrackingId::TrackAcrossProcesses::Yes);
  mReader = DecoderTraits::CreateReader(ContainerType(), init);
  if (NS_WARN_IF(!mReader)) {
    return nullptr;
  }

#ifdef MOZ_WMF_MEDIA_ENGINE
  // This state machine is mainly used for the encrypted playback. However, for
  // testing purpose we would also use it the non-encrypted playback.
  // 1=enabled encrypted and clear, 3=enabled clear
  if ((StaticPrefs::media_wmf_media_engine_enabled() == 1 ||
       StaticPrefs::media_wmf_media_engine_enabled() == 3) &&
      StaticPrefs::media_wmf_media_engine_channel_decoder_enabled() &&
      !aDisableExternalEngine) {
    return MakeAndAddRef<ExternalEngineStateMachine>(this, mReader);
  }
#endif
  return MakeAndAddRef<MediaDecoderStateMachine>(this, mReader);
}

void ChannelMediaDecoder::Shutdown() {
  mResourceCallback->Disconnect();
  MediaDecoder::Shutdown();

  if (mResource) {
    // Force any outstanding seek and byterange requests to complete
    // to prevent shutdown from deadlocking.
    mResourceClosePromise = mResource->Close();
  }
}

void ChannelMediaDecoder::ShutdownInternal() {
  if (!mResourceClosePromise) {
    MediaShutdownManager::Instance().Unregister(this);
    return;
  }

  mResourceClosePromise->Then(
      AbstractMainThread(), __func__,
      [self = RefPtr<ChannelMediaDecoder>(this)] {
        MediaShutdownManager::Instance().Unregister(self);
      });
}

nsresult ChannelMediaDecoder::Load(nsIChannel* aChannel,
                                   bool aIsPrivateBrowsing,
                                   nsIStreamListener** aStreamListener) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mResource);
  MOZ_ASSERT(aStreamListener);

  mResource = BaseMediaResource::Create(mResourceCallback, aChannel,
                                        aIsPrivateBrowsing);
  if (!mResource) {
    return NS_ERROR_FAILURE;
  }
  DDLINKCHILD("resource", mResource.get());

  nsresult rv = MediaShutdownManager::Instance().Register(this);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  rv = mResource->Open(aStreamListener);
  NS_ENSURE_SUCCESS(rv, rv);
  return CreateAndInitStateMachine(mResource->IsLiveStream());
}

nsresult ChannelMediaDecoder::Load(BaseMediaResource* aOriginal) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mResource);

  mResource = aOriginal->CloneData(mResourceCallback);
  if (!mResource) {
    return NS_ERROR_FAILURE;
  }
  DDLINKCHILD("resource", mResource.get());

  nsresult rv = MediaShutdownManager::Instance().Register(this);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  return CreateAndInitStateMachine(mResource->IsLiveStream());
}

void ChannelMediaDecoder::NotifyDownloadEnded(nsresult aStatus) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(!IsShutdown());

  LOG("NotifyDownloadEnded, status={:x}", static_cast<uint32_t>(aStatus));

  if (NS_SUCCEEDED(aStatus)) {
    // Download ends successfully. This is a stream with a finite length.
    GetStateMachine()->DispatchIsLiveStream(false);
    NotifyReaderDataArrived();
  }

  MediaDecoderOwner* owner = GetOwner();
  if (NS_SUCCEEDED(aStatus) || aStatus == NS_BASE_STREAM_CLOSED) {
    nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(
        "ChannelMediaDecoder::UpdatePlaybackRate",
        [playbackStats = mPlaybackStatistics,
         res = RefPtr<BaseMediaResource>(mResource),
         duration = mDuration.match(DurationToTimeUnit())]() {
          (void)UpdateResourceOfPlaybackByteRate(playbackStats, res, duration);
        });
    nsresult rv = GetStateMachine()->OwnerThread()->Dispatch(r.forget());
    MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
    (void)rv;
    owner->DownloadSuspended();
    // NotifySuspendedStatusChanged will tell the element that download
    // has been suspended "by the cache", which is true since we never
    // download anything. The element can then transition to HAVE_ENOUGH_DATA.
    owner->NotifySuspendedByCache(true);
  } else if (aStatus == NS_BINDING_ABORTED) {
    // Download has been cancelled by user.
    owner->LoadAborted();
  } else {
    NetworkError(MediaResult(aStatus, "Download aborted"));
  }
}

bool ChannelMediaDecoder::CanPlayThroughImpl() {
  MOZ_ASSERT(NS_IsMainThread());
  return mCanPlayThrough;
}

void ChannelMediaDecoder::OnPlaybackEvent(const MediaPlaybackEvent& aEvent) {
  MOZ_ASSERT(NS_IsMainThread());
  switch (aEvent.mType) {
    case MediaPlaybackEvent::PlaybackStarted:
      mPlaybackByteOffset = aEvent.mData.as<int64_t>();
      mPlaybackStatistics.Start();
      break;
    case MediaPlaybackEvent::PlaybackProgressed: {
      int64_t newPos = aEvent.mData.as<int64_t>();
      mPlaybackStatistics.AddBytes(newPos - mPlaybackByteOffset);
      mPlaybackByteOffset = newPos;
      break;
    }
    case MediaPlaybackEvent::PlaybackStopped: {
      int64_t newPos = aEvent.mData.as<int64_t>();
      mPlaybackStatistics.AddBytes(newPos - mPlaybackByteOffset);
      mPlaybackByteOffset = newPos;
      mPlaybackStatistics.Stop();
      break;
    }
    default:
      break;
  }
  MediaDecoder::OnPlaybackEvent(aEvent);
}

void ChannelMediaDecoder::DurationChanged() {
  MOZ_ASSERT(NS_IsMainThread());
  MediaDecoder::DurationChanged();
  // Duration has changed so we should recompute playback byte rate
  nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(
      "ChannelMediaDecoder::UpdatePlaybackRate",
      [playbackStats = mPlaybackStatistics,
       res = RefPtr<BaseMediaResource>(mResource),
       duration = mDuration.match(DurationToTimeUnit())]() {
        (void)UpdateResourceOfPlaybackByteRate(playbackStats, res, duration);
      });
  nsresult rv = GetStateMachine()->OwnerThread()->Dispatch(r.forget());
  MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
  (void)rv;
}

void ChannelMediaDecoder::DownloadProgressed() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(!IsShutdown());

  GetOwner()->DownloadProgressed();
  if (IsShutdown()) {
    return;
  }

  using StatsPromise = MozPromise<MediaStatistics, bool, true>;
  InvokeAsync(GetStateMachine()->OwnerThread(), __func__,
              [playbackStats = mPlaybackStatistics,
               res = RefPtr<BaseMediaResource>(mResource),
               duration = mDuration.match(DurationToTimeUnit()),
               playbackByteOffset = mPlaybackByteOffset]() {
                auto rateInfo = UpdateResourceOfPlaybackByteRate(playbackStats,
                                                                 res, duration);
                MediaStatistics result;
                result.mDownloadByteRate =
                    res->GetDownloadRate(&result.mDownloadByteRateReliable);
                result.mDownloadBytePosition =
                    res->GetCachedDataEnd(playbackByteOffset);
                result.mTotalBytes = res->GetLength();
                result.mPlaybackByteRate = rateInfo.mRate;
                result.mPlaybackByteRateReliable = rateInfo.mReliable;
                result.mPlaybackByteOffset = playbackByteOffset;
                return StatsPromise::CreateAndResolve(result, __func__);
              })
      ->Then(
          mAbstractMainThread, __func__,
          [=, this,
           self = RefPtr<ChannelMediaDecoder>(this)](MediaStatistics aStats) {
            if (IsShutdown()) {
              return;
            }
            mCanPlayThrough = aStats.CanPlayThrough();
            LOGD("Can play through: {} [{}]", mCanPlayThrough,
                 aStats.ToString());
            GetStateMachine()->DispatchCanPlayThrough(mCanPlayThrough);
            mResource->ThrottleReadahead(ShouldThrottleDownload(aStats));
            // Update readyState since mCanPlayThrough might have changed.
            GetOwner()->UpdateReadyState();
          },
          []() { MOZ_ASSERT_UNREACHABLE("Promise not resolved"); });
}

/* static */
ChannelMediaDecoder::PlaybackRateInfo
ChannelMediaDecoder::UpdateResourceOfPlaybackByteRate(
    const MediaChannelStatistics& aStats, BaseMediaResource* aResource,
    const TimeUnit& aDuration) {
  MOZ_ASSERT(!NS_IsMainThread());

  uint32_t byteRatePerSecond = 0;
  int64_t length = aResource->GetLength();
  bool rateIsReliable = false;
  if (aDuration.IsValid() && !aDuration.IsInfinite() &&
      aDuration.IsPositive() && length >= 0 &&
      length / aDuration.ToSeconds() < UINT32_MAX) {
    // Both the duration and total content length are known.
    byteRatePerSecond = uint32_t(length / aDuration.ToSeconds());
    rateIsReliable = true;
  } else {
    byteRatePerSecond = aStats.GetRate(&rateIsReliable);
  }

  // Adjust rate if necessary.
  if (rateIsReliable) {
    // Avoid passing a zero rate
    byteRatePerSecond = std::max(byteRatePerSecond, 1u);
  } else {
    // Set a minimum rate of 10,000 bytes per second ... sometimes we just
    // don't have good data
    byteRatePerSecond = std::max(byteRatePerSecond, 10000u);
  }
  aResource->SetPlaybackRate(byteRatePerSecond);
  return {byteRatePerSecond, rateIsReliable};
}

bool ChannelMediaDecoder::ShouldThrottleDownload(
    const MediaStatistics& aStats) {
  // We throttle the download if either the throttle override pref is set
  // (so that we always throttle at the readahead limit on mobile if using
  // a cellular network) or if the download is fast enough that there's no
  // concern about playback being interrupted.
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_TRUE(GetStateMachine(), false);

  int64_t length = aStats.mTotalBytes;
  if (length > 0 &&
      length <= int64_t(StaticPrefs::media_memory_cache_max_size()) * 1024) {
    // Don't throttle the download of small resources. This is to speed
    // up seeking, as seeks into unbuffered ranges would require starting
    // up a new HTTP transaction, which adds latency.
    LOGD("Not throttling download: media resource is small");
    return false;
  }

  if (OnCellularConnection() &&
      Preferences::GetBool(
          "media.throttle-cellular-regardless-of-download-rate", false)) {
    LOGD(
        "Throttling download: on cellular, and "
        "media.throttle-cellular-regardless-of-download-rate is true.");
    return true;
  }

  if (!aStats.mDownloadByteRateReliable || !aStats.mPlaybackByteRateReliable) {
    LOGD(
        "Not throttling download: download rate ({}) playback rate ({}) is not "
        "reliable",
        aStats.mDownloadByteRate, aStats.mPlaybackByteRate);
    return false;
  }
  uint32_t factor =
      std::max(2u, Preferences::GetUint("media.throttle-factor", 2));
  bool throttle = aStats.mDownloadByteRate > factor * aStats.mPlaybackByteRate;
  LOGD(
      "ShouldThrottleDownload: {} (download rate({}) > factor({}) * playback "
      "rate({}))",
      throttle ? "true" : "false", aStats.mDownloadByteRate, factor,
      aStats.mPlaybackByteRate);
  return throttle;
}

void ChannelMediaDecoder::AddSizeOfResources(ResourceSizes* aSizes) {
  MOZ_ASSERT(NS_IsMainThread());
  if (mResource) {
    aSizes->mByteSize += mResource->SizeOfIncludingThis(aSizes->mMallocSizeOf);
  }
}

already_AddRefed<nsIPrincipal> ChannelMediaDecoder::GetCurrentPrincipal() {
  MOZ_ASSERT(NS_IsMainThread());
  return mResource ? mResource->GetCurrentPrincipal() : nullptr;
}

bool ChannelMediaDecoder::HadCrossOriginRedirects() {
  MOZ_ASSERT(NS_IsMainThread());
  return mResource ? mResource->HadCrossOriginRedirects() : false;
}

bool ChannelMediaDecoder::IsTransportSeekable() {
  MOZ_ASSERT(NS_IsMainThread());
  return mResource->IsTransportSeekable();
}

void ChannelMediaDecoder::SetLoadInBackground(bool aLoadInBackground) {
  MOZ_ASSERT(NS_IsMainThread());
  if (mResource) {
    mResource->SetLoadInBackground(aLoadInBackground);
  }
}

void ChannelMediaDecoder::Suspend() {
  MOZ_ASSERT(NS_IsMainThread());
  if (mResource) {
    mResource->Suspend(true);
  }
  MediaDecoder::Suspend();
}

void ChannelMediaDecoder::Resume() {
  MOZ_ASSERT(NS_IsMainThread());
  if (mResource) {
    mResource->Resume();
  }
  MediaDecoder::Resume();
}

void ChannelMediaDecoder::MetadataLoaded(
    UniquePtr<MediaInfo> aInfo, UniquePtr<MetadataTags> aTags,
    MediaDecoderEventVisibility aEventVisibility) {
  MediaDecoder::MetadataLoaded(std::move(aInfo), std::move(aTags),
                               aEventVisibility);
  // Set mode to PLAYBACK after reading metadata.
  mResource->SetReadMode(MediaCacheStream::MODE_PLAYBACK);
}

bool ChannelMediaDecoder::MediaStatistics::CanPlayThrough() const {
  // Number of estimated seconds worth of data we need to have buffered
  // ahead of the current playback position before we allow the media decoder
  // to report that it can play through the entire media without the decode
  // catching up with the download. Having this margin make the
  // CanPlayThrough() calculation more stable in the case of
  // fluctuating bitrates.
  static const int64_t CAN_PLAY_THROUGH_MARGIN = 1;

  LOGD(
      "CanPlayThrough: mPlaybackByteRate: {}, mDownloadByteRate: {}, "
      "mTotalBytes"
      ": {}, mDownloadBytePosition: {}, mPlaybackByteOffset: {}, "
      "mDownloadByteRateReliable: {}, mPlaybackByteRateReliable: {}",
      mPlaybackByteRate, mDownloadByteRate, mTotalBytes, mDownloadBytePosition,
      mPlaybackByteOffset, mDownloadByteRateReliable,
      mPlaybackByteRateReliable);

  if ((mTotalBytes < 0 && mDownloadByteRateReliable) ||
      (mTotalBytes >= 0 && mTotalBytes == mDownloadBytePosition)) {
    LOGD("CanPlayThrough: true (early return)");
    return true;
  }

  if (!mDownloadByteRateReliable || !mPlaybackByteRateReliable) {
    LOGD("CanPlayThrough: false (rate unreliable: download({})/playback({}))",
         mDownloadByteRateReliable, mPlaybackByteRateReliable);
    return false;
  }

  int64_t bytesToDownload = mTotalBytes - mDownloadBytePosition;
  int64_t bytesToPlayback = mTotalBytes - mPlaybackByteOffset;
  double timeToDownload = bytesToDownload / mDownloadByteRate;
  double timeToPlay = bytesToPlayback / mPlaybackByteRate;

  if (timeToDownload > timeToPlay) {
    // Estimated time to download is greater than the estimated time to play.
    // We probably can't play through without having to stop to buffer.
    LOGD("CanPlayThrough: false (download speed too low)");
    return false;
  }

  // Estimated time to download is less than the estimated time to play.
  // We can probably play through without having to buffer, but ensure that
  // we've got a reasonable amount of data buffered after the current
  // playback position, so that if the bitrate of the media fluctuates, or if
  // our download rate or decode rate estimation is otherwise inaccurate,
  // we don't suddenly discover that we need to buffer. This is particularly
  // required near the start of the media, when not much data is downloaded.
  int64_t readAheadMargin =
      static_cast<int64_t>(mPlaybackByteRate * CAN_PLAY_THROUGH_MARGIN);
  return mDownloadBytePosition > mPlaybackByteOffset + readAheadMargin;
}

nsCString ChannelMediaDecoder::MediaStatistics::ToString() const {
  nsCString str;
  str.AppendFmt("MediaStatistics: ");
  str.AppendFmt(" mTotalBytes={}", mTotalBytes);
  str.AppendFmt(" mDownloadBytePosition={}", mDownloadBytePosition);
  str.AppendFmt(" mPlaybackByteOffset={}", mPlaybackByteOffset);
  str.AppendFmt(" mDownloadByteRate={}", mDownloadByteRate);
  str.AppendFmt(" mPlaybackByteRate={}", mPlaybackByteRate);
  str.AppendFmt(" mDownloadByteRateReliable={}", mDownloadByteRateReliable);
  str.AppendFmt(" mPlaybackByteRateReliable={}", mPlaybackByteRateReliable);
  return str;
}

}  // namespace mozilla

// avoid redefined macro in unified build
#undef LOG
#undef LOGD
