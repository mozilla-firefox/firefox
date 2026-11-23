/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ExternalEngineStateMachine.h"

#include <memory>

#include "PerformanceRecorder.h"
#ifdef MOZ_WMF_MEDIA_ENGINE
#  include "MFMediaEngineDecoderModule.h"
#  include "mozilla/MFMediaEngineChild.h"
#  include "mozilla/StaticPrefs_media.h"
#endif
#ifdef MOZ_WMF_CDM
#  include "mozilla/EMEUtils.h"
#endif
#include "VideoUtils.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/Atomics.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/glean/DomMediaPlatformsWmfMetrics.h"
#include "nsPrintfCString.h"
#include "nsThreadUtils.h"

namespace mozilla {

extern LazyLogModule gMediaDecoderLog;

#define FMT(x, ...) \
  "Decoder=%p, State=%s, " x, mDecoderID, GetStateStr(), ##__VA_ARGS__
#define LOG(x, ...)                                                            \
  DDMOZ_LOG_FMT(gMediaDecoderLog, LogLevel::Debug, "Decoder={}, State={}, " x, \
                fmt::ptr(mDecoderID), GetStateStr(), ##__VA_ARGS__)
#define LOGV(x, ...)                                              \
  DDMOZ_LOG_FMT(gMediaDecoderLog, LogLevel::Verbose,              \
                "Decoder={}, State={}, " x, fmt::ptr(mDecoderID), \
                GetStateStr(), ##__VA_ARGS__)
#define LOGW(x, ...) NS_WARNING(nsPrintfCString(FMT(x, ##__VA_ARGS__)).get())
#define LOGE(x, ...)                                                   \
  NS_DebugBreak(NS_DEBUG_WARNING,                                      \
                nsPrintfCString(FMT(x, ##__VA_ARGS__)).get(), nullptr, \
                __FILE__, __LINE__)

const char* ExternalEngineEventToStr(ExternalEngineEvent aEvent) {
#define EVENT_TO_STR(event)        \
  case ExternalEngineEvent::event: \
    return #event
  switch (aEvent) {
    EVENT_TO_STR(LoadedMetaData);
    EVENT_TO_STR(LoadedFirstFrame);
    EVENT_TO_STR(LoadedData);
    EVENT_TO_STR(Waiting);
    EVENT_TO_STR(Playing);
    EVENT_TO_STR(Seeked);
    EVENT_TO_STR(BufferingStarted);
    EVENT_TO_STR(BufferingEnded);
    EVENT_TO_STR(Timeupdate);
    EVENT_TO_STR(Ended);
    EVENT_TO_STR(RequestForAudio);
    EVENT_TO_STR(RequestForVideo);
    EVENT_TO_STR(AudioEnough);
    EVENT_TO_STR(VideoEnough);
    default:
      MOZ_ASSERT_UNREACHABLE("Undefined event!");
      return "Undefined";
  }
#undef EVENT_TO_STR
}

/**
 * This class monitors the amount of crash happened for a remote engine
 * process. It the amount of crash of the remote process exceeds the defined
 * threshold, then `ShouldRecoverProcess()` will return false to indicate that
 * we should not keep spawning that remote process because it's too easy to
 * crash.
 *
 * In addition, we also have another mechanism in the media format reader
 * (MFR) to detect crash amount of remote processes, but that would only
 * happen during the decoding process. The main reason to choose using this
 * simple monitor, instead of the mechanism in the MFR is because that
 * mechanism can't detect every crash happening in the remote process, such as
 * crash happening during initializing the remote engine, or setting the CDM
 * pipepline, which can happen prior to decoding.
 */
class ProcessCrashMonitor final {
 public:
  static void NotifyCrash() {
    StaticMutexAutoLock lock(sMutex);
    auto* monitor = ProcessCrashMonitor::EnsureInstance();
    if (!monitor) {
      return;
    }
    monitor->mCrashNums++;
  }
  static bool ShouldRecoverProcess() {
    StaticMutexAutoLock lock(sMutex);
    auto* monitor = ProcessCrashMonitor::EnsureInstance();
    if (!monitor) {
      return false;
    }
    return monitor->mCrashNums <= monitor->mMaxCrashes;
  }
  ProcessCrashMonitor(const ProcessCrashMonitor&) = delete;
  ProcessCrashMonitor& operator=(const ProcessCrashMonitor&) = delete;

 private:
  ProcessCrashMonitor() : mCrashNums(0) {
#ifdef MOZ_WMF_MEDIA_ENGINE
    mMaxCrashes = StaticPrefs::media_wmf_media_engine_max_crashes();
#else
    mMaxCrashes = 0;
#endif
  };

  static ProcessCrashMonitor* EnsureInstance() {
    if (sIsShutdown) {
      return nullptr;
    }
    if (!sCrashMonitor) {
      sCrashMonitor.reset(new ProcessCrashMonitor());
      GetMainThreadSerialEventTarget()->Dispatch(
          NS_NewRunnableFunction("ProcessCrashMonitor::EnsureInstance", [&] {
            RunOnShutdown(
                [&] {
                  StaticMutexAutoLock lock(sMutex);
                  sCrashMonitor.reset();
                  sIsShutdown = true;
                },
                ShutdownPhase::XPCOMShutdown);
          }));
    }
    return sCrashMonitor.get();
  }

  static StaticMutex sMutex;
  static UniquePtr<ProcessCrashMonitor> sCrashMonitor;
  static Atomic<bool> sIsShutdown;

  uint32_t mCrashNums;
  uint32_t mMaxCrashes;
};

StaticMutex ProcessCrashMonitor::sMutex;
constinit UniquePtr<ProcessCrashMonitor> ProcessCrashMonitor::sCrashMonitor;
Atomic<bool> ProcessCrashMonitor::sIsShutdown{false};

/* static */
const char* ExternalEngineStateMachine::StateToStr(State aNextState) {
#define STATE_TO_STR(state) \
  case State::state:        \
    return #state
  switch (aNextState) {
    STATE_TO_STR(InitEngine);
    STATE_TO_STR(ReadingMetadata);
    STATE_TO_STR(RunningEngine);
    STATE_TO_STR(SeekingData);
    STATE_TO_STR(ShutdownEngine);
    STATE_TO_STR(RecoverEngine);
    default:
      MOZ_ASSERT_UNREACHABLE("Undefined state!");
      return "Undefined";
  }
#undef STATE_TO_STR
}

const char* ExternalEngineStateMachine::GetStateStr() const {
  return StateToStr(mState.mName);
}

static bool IsBeingProfiledOrLogEnabled() {
  return MOZ_LOG_TEST(gMediaDecoderLog, LogLevel::Info) ||
         profiler_thread_is_being_profiled_for_markers();
}

void ExternalEngineStateMachine::ChangeStateTo(State aNextState) {
  if (IsBeingProfiledOrLogEnabled()) {
    nsPrintfCString msg("Change state : '%s' -> '%s' (play-state=%d)",
                        StateToStr(mState.mName), StateToStr(aNextState),
                        mPlayState.Ref());
    LOG("{}", msg.get());
    PROFILER_MARKER_TEXT("EESM::ChangeStateTo", MEDIA_PLAYBACK, {}, msg);
  }
  // Assert the possible state transitions.
  MOZ_ASSERT_IF(
      mState.IsReadingMetadata(),
      aNextState == State::InitEngine || aNextState == State::ShutdownEngine);
  MOZ_ASSERT_IF(mState.IsInitEngine(), aNextState == State::RunningEngine ||
                                           aNextState == State::ShutdownEngine);
  MOZ_ASSERT_IF(mState.IsRunningEngine(),
                aNextState == State::SeekingData ||
                    aNextState == State::ShutdownEngine ||
                    aNextState == State::RecoverEngine);
  MOZ_ASSERT_IF(mState.IsSeekingData(),
                aNextState == State::RunningEngine ||
                    aNextState == State::ShutdownEngine ||
                    aNextState == State::RecoverEngine);
  MOZ_ASSERT_IF(mState.IsShutdownEngine(), aNextState == State::ShutdownEngine);
  MOZ_ASSERT_IF(
      mState.IsRecoverEngine(),
      aNextState == State::SeekingData || aNextState == State::ShutdownEngine);
  if (aNextState == State::SeekingData) {
    mState = StateObject({StateObject::SeekingData()});
  } else if (aNextState == State::InitEngine) {
    mState = StateObject({StateObject::InitEngine()});
  } else if (aNextState == State::RunningEngine) {
    mState = StateObject({StateObject::RunningEngine()});
  } else if (aNextState == State::ShutdownEngine) {
    mState = StateObject({StateObject::ShutdownEngine()});
  } else if (aNextState == State::RecoverEngine) {
    mState = StateObject({StateObject::RecoverEngine()});
  } else {
    MOZ_ASSERT_UNREACHABLE("Wrong state!");
  }
  NotifyAudibleStateChangeIfNeeded();
}

ExternalEngineStateMachine::ExternalEngineStateMachine(
    MediaDecoder* aDecoder, MediaFormatReader* aReader)
    : MediaDecoderStateMachineBase(aDecoder, aReader) {
  LOG("Created ExternalEngineStateMachine");
  MOZ_ASSERT(mState.IsReadingMetadata());
  ReadMetadata();
}

ExternalEngineStateMachine::~ExternalEngineStateMachine() {
  LOG("ExternalEngineStateMachine is destroyed");
}

void ExternalEngineStateMachine::InitEngine() {
  MOZ_ASSERT(mState.IsInitEngine() || mState.IsRecoverEngine());
#ifdef MOZ_WMF_MEDIA_ENGINE
  mEngine.reset(new MFMediaEngineWrapper(this, mFrameStats));
#endif
  if (mEngine) {
    MOZ_ASSERT(mInfo);
    if (IsBeingProfiledOrLogEnabled()) {
      nsPrintfCString msg{"mMinimizePreroll %d IsEncryptedCustomIdent %d",
                          mMinimizePreroll, mReader->IsEncryptedCustomIdent()};
      LOG("Init engine, {}", msg.get());
      PROFILER_MARKER_TEXT("EESM::InitEngine", MEDIA_PLAYBACK, {}, msg);
    }
    auto* state = mState.AsInitEngine();
    ExternalPlaybackEngine::InitFlagSet flags;
    if (mMinimizePreroll) {
      flags += ExternalPlaybackEngine::InitFlag::ShouldPreload;
    }
    if (mReader->IsEncryptedCustomIdent()) {
      flags += ExternalPlaybackEngine::InitFlag::EncryptedCustomIdent;
    }
    state->mInitPromise = mEngine->Init(*mInfo, flags);
    state->mInitPromise
        ->Then(OwnerThread(), __func__, this,
               &ExternalEngineStateMachine::OnEngineInitSuccess,
               &ExternalEngineStateMachine::OnEngineInitFailure)
        ->Track(state->mEngineInitRequest);
  }
}

void ExternalEngineStateMachine::OnEngineInitSuccess() {
  AssertOnTaskQueue();
  AUTO_PROFILER_LABEL("ExternalEngineStateMachine::OnEngineInitSuccess",
                      MEDIA_PLAYBACK);
  MOZ_ASSERT(mState.IsInitEngine() || mState.IsRecoverEngine());
  if (IsBeingProfiledOrLogEnabled()) {
    nsPrintfCString msg("Initialized the external playback engine %" PRIu64,
                        mEngine->Id());
    LOG("{}", msg.get());
    PROFILER_MARKER_TEXT("EESM::OnEngineInitSuccess", MEDIA_PLAYBACK, {}, msg);
  }
  auto* state = mState.AsInitEngine();
  state->mEngineInitRequest.Complete();
  mReader->UpdateMediaEngineId(mEngine->Id());
  state->mInitPromise = nullptr;
  mIsEngineReady = true;
  if (mState.IsInitEngine()) {
    StartRunningEngine();
    return;
  }
  // We just recovered from CDM process crash, seek to previous position.
  ReportRecoveryTelemetry(true);
  SeekTarget target(mCurrentPosition.Ref(), SeekTarget::Type::Accurate);
  Seek(target);
}

void ExternalEngineStateMachine::OnEngineInitFailure() {
  AssertOnTaskQueue();
  MOZ_ASSERT(mState.IsInitEngine() || mState.IsRecoverEngine());
  LOGE("Failed to initialize the external playback engine");
  PROFILER_MARKER_UNTYPED("EESM::OnEngineInitFailure", MEDIA_PLAYBACK);
  mIsEngineReady = false;
  if (mState.IsRecoverEngine()) {
    ReportRecoveryTelemetry(false);
  }
  auto* state = mState.AsInitEngine();
  state->mEngineInitRequest.Complete();
  state->mInitPromise = nullptr;
  // Even if we failed to initialize the media engine, we still want to try
  // again with the normal state machine, so don't return a fatal error, return
  // NS_ERROR_DOM_MEDIA_EXTERNAL_ENGINE_NOT_SUPPORTED_ERR instead.
  ReportTelemetry(NS_ERROR_DOM_MEDIA_MEDIA_ENGINE_INITIALIZATION_ERR);
  DecodeError(MediaResult(NS_ERROR_DOM_MEDIA_EXTERNAL_ENGINE_NOT_SUPPORTED_ERR,
                          __func__));
}

void ExternalEngineStateMachine::ReadMetadata() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mState.IsReadingMetadata());
  PROFILER_MARKER_UNTYPED("EESM::ReadMetadata", MEDIA_PLAYBACK);
  (void)OwnerThread()->Dispatch(NS_NewRunnableFunction(
      "ExternalEngineStateMachine::ReadMetadata",
      [self = RefPtr<ExternalEngineStateMachine>{this}, this] {
        mReader->ReadMetadata()
            ->Then(OwnerThread(), __func__, this,
                   &ExternalEngineStateMachine::OnMetadataRead,
                   &ExternalEngineStateMachine::OnMetadataNotRead)
            ->Track(mState.AsReadingMetadata()->mMetadataRequest);
      }));
}

void ExternalEngineStateMachine::OnMetadataRead(MetadataHolder&& aMetadata) {
  AssertOnTaskQueue();
  AUTO_PROFILER_LABEL("ExternalEngineStateMachine::OnMetadataRead",
                      MEDIA_PLAYBACK);
  MOZ_ASSERT(mState.IsReadingMetadata());
  LOG("OnMetadataRead");

  mState.AsReadingMetadata()->mMetadataRequest.Complete();
  mInfo.emplace(*aMetadata.mInfo);
  mMediaSeekable = Info().mMediaSeekable;
  mMediaSeekableOnlyInBufferedRanges =
      Info().mMediaSeekableOnlyInBufferedRanges;

  if (!IsFormatSupportedByExternalEngine(*mInfo)) {
    // Only record it when a CDM is waiting for the engine, because clear
    // playback falls back here on every machine without hardware video
    // decoding.
    if (mPendingCDMProxy) {
      ReportTelemetry(NS_ERROR_DOM_MEDIA_EXTERNAL_ENGINE_NOT_SUPPORTED_ERR);
    }
    // The external engine doesn't support the type, try to notify the decoder
    // to use our own state machine again. Not a real "error", because it would
    // fallback to another state machine, unless a CDM is pending.
    DecodeError(
        MediaResult(NS_ERROR_DOM_MEDIA_EXTERNAL_ENGINE_NOT_SUPPORTED_ERR));
    return;
  }

#ifdef MOZ_WMF_MEDIA_ENGINE
  // Only support encrypted playback. Not a real "error", because it would
  // fallback to another state machine.
  if ((!mInfo->IsEncrypted() && !mReader->IsEncryptedCustomIdent()) &&
      StaticPrefs::media_wmf_media_engine_enabled() == 2) {
    LOG("External engine only supports encrypted playback by the pref");
    DecodeError(
        MediaResult(NS_ERROR_DOM_MEDIA_EXTERNAL_ENGINE_NOT_SUPPORTED_ERR));
    return;
  }
#endif

  if (Info().mMetadataDuration.isSome()) {
    mDuration = Info().mMetadataDuration;
  } else if (Info().mUnadjustedMetadataEndTime.isSome()) {
    const media::TimeUnit unadjusted = Info().mUnadjustedMetadataEndTime.ref();
    const media::TimeUnit adjustment = Info().mStartTime;
    mInfo->mMetadataDuration.emplace(unadjusted - adjustment);
    mDuration = Info().mMetadataDuration;
  }

  // If we don't know the duration by this point, we assume infinity, per spec.
  if (mDuration.Ref().isNothing()) {
    mDuration = Some(media::TimeUnit::FromInfinity());
  }
  MOZ_ASSERT(mDuration.Ref().isSome());

  if (mInfo->HasVideo()) {
    mVideoDisplay = mInfo->mVideo.mDisplay;
  }

  if (IsBeingProfiledOrLogEnabled()) {
    nsPrintfCString msg(
        "a=%s, v=%s, size=[%dx%d], image=[%dx%d], duration=%s, encrypted=%d, "
        "IsEncryptedCustomIdent=%d",
        mInfo->HasAudio() ? mInfo->mAudio.mMimeType.get() : "none",
        mInfo->HasVideo() ? mInfo->mVideo.mMimeType.get() : "none",
        mVideoDisplay.width, mVideoDisplay.height, mInfo->mVideo.mImage.width,
        mInfo->mVideo.mImage.height, mDuration.Ref()->ToString().get(),
        mInfo->IsEncrypted(), mReader->IsEncryptedCustomIdent());
    LOG("Metadata loaded : {}", msg.get());
    PROFILER_MARKER_TEXT("EESM::OnMetadataRead", MEDIA_PLAYBACK, {}, msg);
  }

  mMetadataLoadedEvent.Notify(std::move(aMetadata.mInfo),
                              std::move(aMetadata.mTags),
                              MediaDecoderEventVisibility::Observable);
  ChangeStateTo(State::InitEngine);
  InitEngine();
}

void ExternalEngineStateMachine::OnMetadataNotRead(const MediaResult& aError) {
  AssertOnTaskQueue();
  MOZ_ASSERT(mState.IsReadingMetadata());
  LOGE("Decode metadata failed, shutting down decoder");
  PROFILER_MARKER_UNTYPED("EESM::OnMetadataNotRead", MEDIA_PLAYBACK);
  mState.AsReadingMetadata()->mMetadataRequest.Complete();
  ReportTelemetry(aError);
  DecodeError(aError);
}

bool ExternalEngineStateMachine::IsFormatSupportedByExternalEngine(
    const MediaInfo& aInfo) {
  AssertOnTaskQueue();
  MOZ_ASSERT(mState.IsReadingMetadata());
#ifdef MOZ_WMF_MEDIA_ENGINE
  const bool audioSupported =
      !aInfo.HasAudio() ||
      MFMediaEngineDecoderModule::SupportsConfig(aInfo.mAudio);
  const bool videoSupported =
      !aInfo.HasVideo() ||
      MFMediaEngineDecoderModule::SupportsConfig(aInfo.mVideo);
  LOG("audio={} (supported={}), video={}(supported={})",
      aInfo.HasAudio() ? aInfo.mAudio.mMimeType.get() : "none", audioSupported,
      aInfo.HasVideo() ? aInfo.mVideo.mMimeType.get() : "none", videoSupported);
  return audioSupported && videoSupported;
#else
  return false;
#endif
}

RefPtr<MediaDecoder::SeekPromise> ExternalEngineStateMachine::InvokeSeek(
    const SeekTarget& aTarget) {
  return InvokeAsync(
      OwnerThread(), __func__,
      [self = RefPtr<ExternalEngineStateMachine>(this), this,
       target = aTarget]() -> RefPtr<MediaDecoder::SeekPromise> {
        AssertOnTaskQueue();
        if (mState.IsShutdownEngine()) {
          return MediaDecoder::SeekPromise::CreateAndReject(true, __func__);
        }
        if (!mIsEngineReady) {
          LOG("Can't perform seek ({}) now, add a pending seek task",
              target.GetTime().ToMicroseconds());
          // We haven't added any pending seek before
          if (mPendingSeek.mPromise.IsEmpty()) {
            mPendingTasks.AppendElement(NS_NewRunnableFunction(
                "ExternalEngineStateMachine::InvokeSeek",
                [self = RefPtr{this}, this] {
                  if (!mPendingSeek.Exists()) {
                    return;
                  }
                  Seek(*mPendingSeek.mTarget)
                      ->Then(OwnerThread(), __func__,
                             [self = RefPtr{this},
                              this](const MediaDecoder::SeekPromise::
                                        ResolveOrRejectValue& aVal) {
                               mPendingSeekRequest.Complete();
                               if (aVal.IsResolve()) {
                                 mPendingSeek.Resolve(__func__);
                               } else {
                                 mPendingSeek.RejectIfExists(__func__);
                               }
                               mPendingSeek = SeekJob();
                             })
                      ->Track(mPendingSeekRequest);
                }));
          } else {
            // Reject previous pending promise, as we will create a new one
            LOG("Replace previous pending seek with a new one");
            mPendingSeek.RejectIfExists(__func__);
            mPendingSeekRequest.DisconnectIfExists();
          }
          mPendingSeek.mTarget = Some(target);
          return mPendingSeek.mPromise.Ensure(__func__);
        }
        if (mPendingSeek.Exists()) {
          LOG("Discard pending seek because another new seek happens");
          mPendingSeek.RejectIfExists(__func__);
          mPendingSeek = SeekJob();
          mPendingSeekRequest.DisconnectIfExists();
        }
        return self->Seek(target);
      });
}

RefPtr<MediaDecoder::SeekPromise> ExternalEngineStateMachine::Seek(
    const SeekTarget& aTarget) {
  AssertOnTaskQueue();
  if (!mState.IsRunningEngine() && !mState.IsSeekingData() &&
      !mState.IsRecoverEngine()) {
    MOZ_ASSERT(false, "Can't seek due to unsupported state.");
    return MediaDecoder::SeekPromise::CreateAndReject(true, __func__);
  }
  // We don't support these type of seek, because they're depending on the
  // implementation of the external engine, which might not be supported.
  if (aTarget.IsNextFrame() || aTarget.IsVideoOnly()) {
    return MediaDecoder::SeekPromise::CreateAndReject(true, __func__);
  }
  if (IsBeingProfiledOrLogEnabled()) {
    nsPrintfCString msg("Start seeking to %" PRId64,
                        aTarget.GetTime().ToMicroseconds());
    LOG("{}", msg.get());
    PROFILER_MARKER_TEXT("EESM::Seek", MEDIA_PLAYBACK, {}, msg);
  }
  auto* state = mState.AsSeekingData();
  if (!state) {
    // We're in other states, so change the state to seeking.
    ChangeStateTo(State::SeekingData);
    state = mState.AsSeekingData();
  }
  state->SetTarget(aTarget);

  // Update related status.
  mSentPlaybackEndedEvent = false;
  mOnPlaybackEvent.Notify(MediaPlaybackEvent::SeekStarted);
  mOnNextFrameStatus.Notify(MediaDecoderOwner::NEXT_FRAME_UNAVAILABLE_SEEKING);

  // Notify the external playback engine about seeking. After the engine changes
  // its current time, it would send `seeked` event.
  mEngine->Seek(aTarget.GetTime());
  state->mWaitingEngineSeeked = true;
  SeekReader();
  return state->mSeekJob.mPromise.Ensure(__func__);
}

void ExternalEngineStateMachine::SeekReader() {
  AssertOnTaskQueue();
  MOZ_ASSERT(mState.IsSeekingData());
  auto* state = mState.AsSeekingData();

  // Reset the reader first and ask it to perform a demuxer seek.
  ResetDecode();
  state->mWaitingReaderSeeked = true;
  if (IsBeingProfiledOrLogEnabled()) {
    nsPrintfCString msg("Seek reader to %" PRId64,
                        state->GetTargetTime().ToMicroseconds());
    LOG("{}", msg.get());
    PROFILER_MARKER_TEXT("EESM::SeekReader", MEDIA_PLAYBACK, {}, msg);
  }
  mReader->Seek(state->mSeekJob.mTarget.ref())
      ->Then(OwnerThread(), __func__, this,
             &ExternalEngineStateMachine::OnSeekResolved,
             &ExternalEngineStateMachine::OnSeekRejected)
      ->Track(state->mSeekRequest);
}

void ExternalEngineStateMachine::OnSeekResolved(const media::TimeUnit& aUnit) {
  AUTO_PROFILER_LABEL("ExternalEngineStateMachine::OnSeekResolved",
                      MEDIA_PLAYBACK);
  AssertOnTaskQueue();
  MOZ_ASSERT(mState.IsSeekingData());
  auto* state = mState.AsSeekingData();

  LOG("OnReaderSeekResolved");
  PROFILER_MARKER_UNTYPED("EESM::OnReaderSeekResolved", MEDIA_PLAYBACK);
  state->mSeekRequest.Complete();
  state->mWaitingReaderSeeked = false;

  // Start sending new data to the external playback engine.
  if (HasAudio()) {
    mHasEnoughAudio = false;
    OnRequestAudio();
  }
  if (HasVideo()) {
    mHasEnoughVideo = false;
#ifdef MOZ_WMF_CDM
    mVideoEOSSentToEngine = false;
#endif
    OnRequestVideo();
  }
  CheckIfSeekCompleted();
}

void ExternalEngineStateMachine::OnSeekRejected(
    const SeekRejectValue& aReject) {
  AUTO_PROFILER_LABEL("ExternalEngineStateMachine::OnSeekRejected",
                      MEDIA_PLAYBACK);
  AssertOnTaskQueue();
  MOZ_ASSERT(mState.IsSeekingData());
  auto* state = mState.AsSeekingData();

  LOG("OnReaderSeekRejected");
  PROFILER_MARKER_UNTYPED("EESM::OnReaderSeekRejected", MEDIA_PLAYBACK);
  state->mSeekRequest.Complete();
  if (aReject.mError == NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA) {
    LOG("OnSeekRejected reason=WAITING_FOR_DATA type={}",
        MediaData::EnumValueToString(aReject.mType));
    MOZ_ASSERT_IF(aReject.mType == MediaData::Type::AUDIO_DATA,
                  !IsRequestingAudioData());
    MOZ_ASSERT_IF(aReject.mType == MediaData::Type::VIDEO_DATA,
                  !IsRequestingVideoData());
    MOZ_ASSERT_IF(aReject.mType == MediaData::Type::AUDIO_DATA,
                  !IsWaitingAudioData());
    MOZ_ASSERT_IF(aReject.mType == MediaData::Type::VIDEO_DATA,
                  !IsWaitingVideoData());

    // Fire 'waiting' to notify the player that we are waiting for data.
    mOnNextFrameStatus.Notify(
        MediaDecoderOwner::NEXT_FRAME_UNAVAILABLE_SEEKING);
    WaitForData(aReject.mType);
    return;
  }

  if (aReject.mError == NS_ERROR_DOM_MEDIA_END_OF_STREAM) {
    EndOfStream(aReject.mType);
    return;
  }

  MOZ_ASSERT(NS_FAILED(aReject.mError),
             "Cancels should also disconnect mSeekRequest");
  state->RejectIfExists(__func__);
  ReportTelemetry(aReject.mError);
  DecodeError(aReject.mError);
}

bool ExternalEngineStateMachine::IsSeeking() {
  AssertOnTaskQueue();
  const auto* state = mState.AsSeekingData();
  return state && state->IsSeeking();
}

void ExternalEngineStateMachine::CheckIfSeekCompleted() {
  AssertOnTaskQueue();
  MOZ_ASSERT(mState.IsSeekingData());
  auto* state = mState.AsSeekingData();
  if (state->mWaitingEngineSeeked || state->mWaitingReaderSeeked) {
    LOG("Seek hasn't been completed yet, waitEngineSeeked={}, "
        "waitReaderSeeked={}",
        state->mWaitingEngineSeeked, state->mWaitingReaderSeeked);
    return;
  }

  // As seeking should be accurate and we can't control the exact timing inside
  // the external media engine. We always set the newCurrentTime = seekTime
  // so that the updated HTMLMediaElement.currentTime will always be the seek
  // target.
  if (state->GetTargetTime() != mCurrentPosition) {
    LOG("Force adjusting current time ({}) to match to target ({})",
        mCurrentPosition.Ref().ToMicroseconds(),
        state->GetTargetTime().ToMicroseconds());
    mCurrentPosition = state->GetTargetTime();
  }

  LOG("Seek completed");
  PROFILER_MARKER_TEXT(
      "EESM::SeekCompleted", MEDIA_PLAYBACK, {},
      nsPrintfCString("currentTime %" PRId64,
                      mCurrentPosition.Ref().ToMicroseconds()));
  state->Resolve(__func__);
  mOnPlaybackEvent.Notify(MediaPlaybackEvent::Invalidate);
  mOnNextFrameStatus.Notify(MediaDecoderOwner::NEXT_FRAME_AVAILABLE);
  StartRunningEngine();
}

void ExternalEngineStateMachine::ResetDecode() {
  AssertOnTaskQueue();
  if (!mInfo) {
    return;
  }

  LOG("ResetDecode");
  MediaFormatReader::TrackSet tracks;
  if (HasVideo()) {
    mVideoDataRequest.DisconnectIfExists();
    mVideoWaitRequest.DisconnectIfExists();
    tracks += TrackInfo::kVideoTrack;
  }
  if (HasAudio()) {
    mAudioDataRequest.DisconnectIfExists();
    mAudioWaitRequest.DisconnectIfExists();
    tracks += TrackInfo::kAudioTrack;
  }
  mReader->ResetDecode(tracks);
}

RefPtr<GenericPromise> ExternalEngineStateMachine::InvokeSetSink(
    const RefPtr<AudioDeviceInfo>& aSink) {
  MOZ_ASSERT(NS_IsMainThread());
  // TODO : can media engine support this?
  return GenericPromise::CreateAndReject(NS_ERROR_FAILURE, __func__);
}

RefPtr<ShutdownPromise> ExternalEngineStateMachine::Shutdown() {
  AssertOnTaskQueue();
  if (mState.IsShutdownEngine()) {
    LOG("Already shutdown");
    return mState.AsShutdownEngine()->mShutdown;
  }

  LOG("Shutdown");
  mIsEngineReady = false;
  ChangeStateTo(State::ShutdownEngine);
  ResetDecode();

  mAudioDataRequest.DisconnectIfExists();
  mVideoDataRequest.DisconnectIfExists();
  mAudioWaitRequest.DisconnectIfExists();
  mVideoWaitRequest.DisconnectIfExists();

  mDuration.DisconnectAll();
  mCurrentPosition.DisconnectAll();
  mIsAudioDataAudible.DisconnectAll();

  mMetadataManager.Disconnect();

  mPendingCDMProxy = nullptr;
  mSetCDMProxyRequest.DisconnectIfExists();

  mPendingSeek.RejectIfExists(__func__);
  mPendingSeekRequest.DisconnectIfExists();

  mPendingTasks.Clear();

  if (mEngine) {
    mEngine->Shutdown();
  }

  auto* state = mState.AsShutdownEngine();
  state->mShutdown = mReader->Shutdown()->Then(
      OwnerThread(), __func__, [self = RefPtr{this}, this]() {
        LOG("Shutting down state machine task queue");
        return OwnerThread()->BeginShutdown();
      });
  return state->mShutdown;
}

void ExternalEngineStateMachine::BufferedRangeUpdated() {
  AssertOnTaskQueue();
  AUTO_PROFILER_LABEL("ExternalEngineStateMachine::BufferedRangeUpdated",
                      MEDIA_PLAYBACK);

  // While playing an unseekable stream of unknown duration, mDuration
  // is updated as we play. But if data is being downloaded
  // faster than played, mDuration won't reflect the end of playable data
  // since we haven't played the frame at the end of buffered data. So update
  // mDuration here as new data is downloaded to prevent such a lag.
  if (mBuffered.Ref().IsInvalid()) {
    return;
  }

  bool exists;
  media::TimeUnit end{mBuffered.Ref().GetEnd(&exists)};
  if (!exists) {
    return;
  }

  // Use estimated duration from buffer ranges when mDuration is unknown or
  // the estimated duration is larger.
  if (mDuration.Ref().isNothing() || mDuration.Ref()->IsInfinite() ||
      end > mDuration.Ref().ref()) {
    mDuration = Some(end);
    DDLOG(DDLogCategory::Property, "duration_us",
          mDuration.Ref()->ToMicroseconds());
  }
}

#define PERFORM_WHEN_ALLOW(Func)                                          \
  do {                                                                    \
    if (mState.IsShutdownEngine() || mHasFatalError ||                    \
        AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) { \
      return;                                                             \
    }                                                                     \
    if (!mIsEngineReady) {                                                \
      LOG("{} is called before init", __func__);                          \
      mPendingTasks.AppendElement(NewRunnableMethod(                      \
          __func__, this, &ExternalEngineStateMachine::Func));            \
      return;                                                             \
    }                                                                     \
  } while (false)

void ExternalEngineStateMachine::SetPlaybackRate(double aPlaybackRate) {
  AssertOnTaskQueue();
  // TODO : consider to make `mPlaybackRate` a mirror to fit other usages like
  // `mVolume` and `mPreservesPitch`.
  mPlaybackRate = aPlaybackRate;
  PlaybackRateChanged();
}

void ExternalEngineStateMachine::PlaybackRateChanged() {
  AssertOnTaskQueue();
  PERFORM_WHEN_ALLOW(PlaybackRateChanged);
  MOZ_ASSERT(mState.IsReadingMetadata() || mState.IsRunningEngine() ||
             mState.IsSeekingData());
  mEngine->SetPlaybackRate(mPlaybackRate);
}

void ExternalEngineStateMachine::VolumeChanged() {
  AssertOnTaskQueue();
  PERFORM_WHEN_ALLOW(VolumeChanged);
  MOZ_ASSERT(mState.IsReadingMetadata() || mState.IsRunningEngine() ||
             mState.IsSeekingData());
  mEngine->SetVolume(mVolume);
}

void ExternalEngineStateMachine::PreservesPitchChanged() {
  AssertOnTaskQueue();
  PERFORM_WHEN_ALLOW(PreservesPitchChanged);
  MOZ_ASSERT(mState.IsReadingMetadata() || mState.IsRunningEngine() ||
             mState.IsSeekingData());
  mEngine->SetPreservesPitch(mPreservesPitch);
}

void ExternalEngineStateMachine::PlayStateChanged() {
  AssertOnTaskQueue();
  PERFORM_WHEN_ALLOW(PlayStateChanged);
  MOZ_ASSERT(mState.IsReadingMetadata() || mState.IsRunningEngine() ||
             mState.IsSeekingData());
  if (mPlayState == MediaDecoder::PLAY_STATE_PLAYING) {
    mEngine->Play();
  } else if (mPlayState == MediaDecoder::PLAY_STATE_PAUSED) {
    mEngine->Pause();
  }
  NotifyAudibleStateChangeIfNeeded();
}

void ExternalEngineStateMachine::LoopingChanged() {
  AssertOnTaskQueue();
  PERFORM_WHEN_ALLOW(LoopingChanged);
  MOZ_ASSERT(mState.IsReadingMetadata() || mState.IsRunningEngine() ||
             mState.IsSeekingData());
  mEngine->SetLooping(mLooping);
}

#undef PERFORM_WHEN_ALLOW

void ExternalEngineStateMachine::EndOfStream(MediaData::Type aType) {
  AssertOnTaskQueue();
  MOZ_ASSERT(mState.IsRunningEngine() || mState.IsSeekingData());
  static auto DataTypeToTrackType = [](const MediaData::Type& aType) {
    if (aType == MediaData::Type::VIDEO_DATA) {
      return TrackInfo::TrackType::kVideoTrack;
    }
    if (aType == MediaData::Type::AUDIO_DATA) {
      return TrackInfo::TrackType::kAudioTrack;
    }
    return TrackInfo::TrackType::kUndefinedTrack;
  };
#ifdef MOZ_WMF_CDM
  if (aType == MediaData::Type::VIDEO_DATA) {
    mVideoEOSSentToEngine = true;
  }
#endif
  mEngine->NotifyEndOfStream(DataTypeToTrackType(aType));
}

void ExternalEngineStateMachine::WaitForData(MediaData::Type aType) {
  AssertOnTaskQueue();
  MOZ_ASSERT(mState.IsRunningEngine() || mState.IsSeekingData());
  AUTO_PROFILER_LABEL("ExternalEngineStateMachine::WaitForData",
                      MEDIA_PLAYBACK);
  MOZ_ASSERT(aType == MediaData::Type::AUDIO_DATA ||
             aType == MediaData::Type::VIDEO_DATA);

  LOG("WaitForData");
  RefPtr<ExternalEngineStateMachine> self = this;
  if (aType == MediaData::Type::AUDIO_DATA) {
    MOZ_ASSERT(HasAudio());
    mReader->WaitForData(MediaData::Type::AUDIO_DATA)
        ->Then(
            OwnerThread(), __func__,
            [self, this](MediaData::Type aType) {
              AUTO_PROFILER_LABEL(
                  "ExternalEngineStateMachine::WaitForData:AudioResolved",
                  MEDIA_PLAYBACK);
              MOZ_ASSERT(aType == MediaData::Type::AUDIO_DATA);
              LOG("Done waiting for audio data");
              mAudioWaitRequest.Complete();
              MaybeFinishWaitForData();
            },
            [self, this](const WaitForDataRejectValue& aRejection) {
              AUTO_PROFILER_LABEL(
                  "ExternalEngineStateMachine::WaitForData:AudioRejected",
                  MEDIA_PLAYBACK);
              mAudioWaitRequest.Complete();
              DecodeError(NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA);
            })
        ->Track(mAudioWaitRequest);
  } else {
    MOZ_ASSERT(HasVideo());
    mReader->WaitForData(MediaData::Type::VIDEO_DATA)
        ->Then(
            OwnerThread(), __func__,
            [self, this](MediaData::Type aType) {
              AUTO_PROFILER_LABEL(
                  "ExternalEngineStateMachine::WaitForData:VideoResolved",
                  MEDIA_PLAYBACK);
              MOZ_ASSERT(aType == MediaData::Type::VIDEO_DATA);
              LOG("Done waiting for video data");
              mVideoWaitRequest.Complete();
              MaybeFinishWaitForData();
            },
            [self, this](const WaitForDataRejectValue& aRejection) {
              AUTO_PROFILER_LABEL(
                  "ExternalEngineStateMachine::WaitForData:VideoRejected",
                  MEDIA_PLAYBACK);
              mVideoWaitRequest.Complete();
              DecodeError(NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA);
            })
        ->Track(mVideoWaitRequest);
  }
}

void ExternalEngineStateMachine::MaybeFinishWaitForData() {
  AssertOnTaskQueue();
  MOZ_ASSERT(mState.IsRunningEngine() || mState.IsSeekingData());

  bool isWaitingForAudio = HasAudio() && mAudioWaitRequest.Exists();
  bool isWaitingForVideo = HasVideo() && mVideoWaitRequest.Exists();
  if (isWaitingForAudio || isWaitingForVideo) {
    LOG("Still waiting for data (waitAudio={}, waitVideo={})",
        isWaitingForAudio, isWaitingForVideo);
    return;
  }

  LOG("Finished waiting for data");
  if (mState.IsSeekingData()) {
    SeekReader();
    return;
  }
  if (HasAudio()) {
    RunningEngineUpdate(MediaData::Type::AUDIO_DATA);
  }
  if (HasVideo()) {
    RunningEngineUpdate(MediaData::Type::VIDEO_DATA);
  }
}

void ExternalEngineStateMachine::StartRunningEngine() {
  ChangeStateTo(State::RunningEngine);
  // Manually check the play state because the engine might be recovered from
  // crash or just get recreated, so PlayStateChanged() won't be triggered.
  if (mPlayState == MediaDecoder::PLAY_STATE_PLAYING) {
    mEngine->Play();
  }
  if (HasAudio()) {
    RunningEngineUpdate(MediaData::Type::AUDIO_DATA);
  }
  if (HasVideo()) {
    RunningEngineUpdate(MediaData::Type::VIDEO_DATA);
  }
  // Run tasks which was called before the engine is ready.
  if (!mPendingTasks.IsEmpty()) {
    for (auto& task : mPendingTasks) {
      (void)OwnerThread()->Dispatch(task.forget());
    }
    mPendingTasks.Clear();
  }
}

void ExternalEngineStateMachine::RunningEngineUpdate(MediaData::Type aType) {
  AssertOnTaskQueue();
  MOZ_ASSERT(mState.IsRunningEngine() || mState.IsSeekingData());
  if (aType == MediaData::Type::AUDIO_DATA && !mHasEnoughAudio) {
    OnRequestAudio();
  }
  if (aType == MediaData::Type::VIDEO_DATA && !mHasEnoughVideo
#ifdef MOZ_WMF_CDM
      && !mVideoEOSSentToEngine
#endif
  ) {
    OnRequestVideo();
  }
}

void ExternalEngineStateMachine::OnRequestAudio() {
  AssertOnTaskQueue();
  MOZ_ASSERT(mState.IsRunningEngine() || mState.IsSeekingData());

  if (!HasAudio()) {
    return;
  }

  if (IsRequestingAudioData() || mAudioWaitRequest.Exists() || IsSeeking()) {
    LOGV(
        "No need to request audio, isRequesting={}, waitingAudio={}, "
        "isSeeking={}",
        IsRequestingAudioData(), mAudioWaitRequest.Exists(), IsSeeking());
    return;
  }

  PerformanceRecorder<PlaybackStage> perfRecorder(MediaStage::RequestData);
  RefPtr<ExternalEngineStateMachine> self = this;
  mReader->RequestAudioData()
      ->Then(
          OwnerThread(), __func__,
          [this, self, perfRecorder(std::move(perfRecorder))](
              const RefPtr<AudioData>& aAudio) mutable {
            perfRecorder.Record();
            mAudioDataRequest.Complete();
            AUTO_PROFILER_LABEL(
                "ExternalEngineStateMachine::OnRequestAudio:Resolved",
                MEDIA_PLAYBACK);
            MOZ_ASSERT(aAudio);
            RunningEngineUpdate(MediaData::Type::AUDIO_DATA);
          },
          [this, self](const MediaResult& aError) {
            mAudioDataRequest.Complete();
            AUTO_PROFILER_LABEL(
                "ExternalEngineStateMachine::OnRequestAudio:Rejected",
                MEDIA_PLAYBACK);
            LOG("OnRequestAudio ErrorName={} Message={}",
                aError.ErrorName().get(), aError.Message().get());
            switch (aError.Code()) {
              case NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA:
                WaitForData(MediaData::Type::AUDIO_DATA);
                break;
              case NS_ERROR_DOM_MEDIA_CANCELED:
                OnRequestAudio();
                break;
              case NS_ERROR_DOM_MEDIA_END_OF_STREAM:
                LOG("Reach to the end, no more audio data");
                EndOfStream(MediaData::Type::AUDIO_DATA);
                break;
              case NS_ERROR_DOM_MEDIA_REMOTE_CRASHED_MF_CDM_ERR:
                // We will handle the process crash in `NotifyErrorInternal()`
                // so here just silently ignore this.
                break;
              default:
                ReportTelemetry(aError);
                DecodeError(aError);
            }
          })
      ->Track(mAudioDataRequest);
}

void ExternalEngineStateMachine::OnRequestVideo() {
  AssertOnTaskQueue();
  MOZ_ASSERT(mState.IsRunningEngine() || mState.IsSeekingData());

  if (!HasVideo()) {
    return;
  }

  if (IsTrackingVideoData() || IsSeeking()) {
    LOGV(
        "No need to request video, isRequesting={}, waitingVideo={}, "
        "isSeeking={}",
        IsRequestingVideoData(), mVideoWaitRequest.Exists(), IsSeeking());
    return;
  }

  PerformanceRecorder<PlaybackStage> perfRecorder(MediaStage::RequestData,
                                                  Info().mVideo.mImage.height);
  RefPtr<ExternalEngineStateMachine> self = this;
  mReader->RequestVideoData(GetVideoThreshold(), false)
      ->Then(
          OwnerThread(), __func__,
          [this, self, perfRecorder(std::move(perfRecorder))](
              const RefPtr<VideoData>& aVideo) mutable {
            perfRecorder.Record();
            mVideoDataRequest.Complete();
            AUTO_PROFILER_LABEL(
                "ExternalEngineStateMachine::OnRequestVideo:Resolved",
                MEDIA_PLAYBACK);
            MOZ_ASSERT(aVideo);
            if (!mHasReceivedFirstDecodedVideoFrame) {
              mHasReceivedFirstDecodedVideoFrame = true;
              OnLoadedFirstFrame();
            }
            RunningEngineUpdate(MediaData::Type::VIDEO_DATA);
            // Send image to PIP window.
            if (mSecondaryVideoContainer.Ref()) {
              mSecondaryVideoContainer.Ref()->SetCurrentFrame(
                  mVideoDisplay, aVideo->mImage, TimeStamp::Now(),
                  media::TimeUnit::Invalid(), aVideo->mTime);
            } else {
              mVideoFrameContainer->SetCurrentFrame(
                  mVideoDisplay, aVideo->mImage, TimeStamp::Now(),
                  media::TimeUnit::Invalid(), aVideo->mTime);
            }
            // The seek and first frame invalidations can run before the frame
            // of a new size is set here, and the element only applies the
            // size, and fires `resize`, on an invalidation.
            if (mVideoDisplay != mLastFrameDisplay) {
              mLastFrameDisplay = mVideoDisplay;
              mOnPlaybackEvent.Notify(MediaPlaybackEvent::Invalidate);
            }
          },
          [this, self](const MediaResult& aError) {
            mVideoDataRequest.Complete();
            AUTO_PROFILER_LABEL(
                "ExternalEngineStateMachine::OnRequestVideo:Rejected",
                MEDIA_PLAYBACK);
            LOG("OnRequestVideo ErrorName={} Message={}",
                aError.ErrorName().get(), aError.Message().get());
            switch (aError.Code()) {
              case NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA:
                WaitForData(MediaData::Type::VIDEO_DATA);
                break;
              case NS_ERROR_DOM_MEDIA_CANCELED:
                OnRequestVideo();
                break;
              case NS_ERROR_DOM_MEDIA_END_OF_STREAM:
                LOG("Reach to the end, no more video data");
                EndOfStream(MediaData::Type::VIDEO_DATA);
                break;
              case NS_ERROR_DOM_MEDIA_REMOTE_CRASHED_MF_CDM_ERR:
                // We will handle the process crash in `NotifyErrorInternal()`
                // so here just silently ignore this.
                break;
              default:
                ReportTelemetry(aError);
                DecodeError(aError);
            }
          })
      ->Track(mVideoDataRequest);
}

void ExternalEngineStateMachine::OnLoadedFirstFrame() {
  AssertOnTaskQueue();
  // We will wait until receive the first video frame.
  if (mInfo->HasVideo() &&
      !mHasReceivedFirstDecodedVideoFrame
#ifdef MOZ_WMF_CDM
      // In frame server mode no decoded frame is ever delivered to us, so we
      // must not gate the first-frame-loaded event on receiving one.
      && !mIsFrameServerMode
#endif
  ) {
    LOG("Hasn't received first decoded video frame");
    return;
  }
  LOG("OnLoadedFirstFrame");
  MediaDecoderEventVisibility visibility =
      mSentFirstFrameLoadedEvent ? MediaDecoderEventVisibility::Suppressed
                                 : MediaDecoderEventVisibility::Observable;
  mSentFirstFrameLoadedEvent = true;
  mFirstFrameLoadedEvent.Notify(std::make_unique<MediaInfo>(Info()),
                                visibility);
  mOnNextFrameStatus.Notify(MediaDecoderOwner::NEXT_FRAME_AVAILABLE);
}

void ExternalEngineStateMachine::OnLoadedData() {
  AssertOnTaskQueue();
  // In case the external engine doesn't send the first frame loaded event
  // correctly.
  LOG("OnLoadedData");
  if (!mSentFirstFrameLoadedEvent) {
    OnLoadedFirstFrame();
  }
  mOnNextFrameStatus.Notify(MediaDecoderOwner::NEXT_FRAME_AVAILABLE);
}

void ExternalEngineStateMachine::OnWaiting() {
  AssertOnTaskQueue();
  LOG("OnWaiting");
  mOnNextFrameStatus.Notify(
      MediaDecoderOwner::NEXT_FRAME_UNAVAILABLE_BUFFERING);
}

void ExternalEngineStateMachine::OnPlaying() {
  AssertOnTaskQueue();
  LOG("OnPlaying");
  mOnNextFrameStatus.Notify(MediaDecoderOwner::NEXT_FRAME_AVAILABLE);
}

void ExternalEngineStateMachine::OnSeeked() {
  AssertOnTaskQueue();
  if (!mState.IsSeekingData()) {
    LOG("Engine Seeking has been completed, ignore the event");
    return;
  }
  MOZ_ASSERT(mState.IsSeekingData());

  auto* state = mState.AsSeekingData();
  if (!state->mSeekJob.Exists()) {
    LOG("Seeking has been rejected, ignore the event");
    return;
  }

  const auto currentTime = mEngine->GetCurrentPosition();
  if (IsBeingProfiledOrLogEnabled()) {
    nsPrintfCString msg("target=%" PRId64 ", currentTime=%" PRId64,
                        state->GetTargetTime().ToMicroseconds(),
                        currentTime.ToMicroseconds());
    LOG("OnEngineSeeked : {}", msg.get());
    PROFILER_MARKER_TEXT("EESM::OnEngineSeeked", MEDIA_PLAYBACK, {}, msg);
  }
  // It's possible to receive multiple seeked event if we seek the engine
  // before the previous seeking finishes, so we would wait until the last
  // seeking is finished.
  if (currentTime >= state->GetTargetTime()) {
    state->mWaitingEngineSeeked = false;
    CheckIfSeekCompleted();
  }
}

void ExternalEngineStateMachine::OnBufferingStarted() {
  AssertOnTaskQueue();
  mOnNextFrameStatus.Notify(
      MediaDecoderOwner::NEXT_FRAME_UNAVAILABLE_BUFFERING);
  if (HasAudio()) {
    WaitForData(MediaData::Type::AUDIO_DATA);
  }
  if (HasVideo()) {
    WaitForData(MediaData::Type::VIDEO_DATA);
  }
  if (IsBeingProfiledOrLogEnabled()) {
    nsPrintfCString msg("hasAudio=%d, hasVideo=%d", HasAudio(), HasVideo());
    LOG("OnBufferingStarted : {}", msg.get());
    PROFILER_MARKER_TEXT("EESM::OnBufferingStarted", MEDIA_PLAYBACK, {}, msg);
  }
}

void ExternalEngineStateMachine::OnBufferingEnded() {
  AssertOnTaskQueue();
  LOG("OnBufferingEnded");
  PROFILER_MARKER_UNTYPED("EESM::OnBufferingEnded", MEDIA_PLAYBACK);
  mOnNextFrameStatus.Notify(MediaDecoderOwner::NEXT_FRAME_AVAILABLE);
}

void ExternalEngineStateMachine::OnEnded() {
  AssertOnTaskQueue();
  if (mSentPlaybackEndedEvent) {
    return;
  }
  LOG("Playback is ended");
  PROFILER_MARKER_UNTYPED("EESM::OnEnded", MEDIA_PLAYBACK);
  mOnNextFrameStatus.Notify(MediaDecoderOwner::NEXT_FRAME_UNAVAILABLE);
  mOnPlaybackEvent.Notify(MediaPlaybackEvent::PlaybackEnded);
  mSentPlaybackEndedEvent = true;
}

void ExternalEngineStateMachine::OnTimeupdate() {
  AssertOnTaskQueue();
  if (IsSeeking()) {
    return;
  }
  mCurrentPosition = mEngine->GetCurrentPosition();
  if (mDuration.Ref().ref() < mCurrentPosition.Ref()) {
    mDuration = Some(mCurrentPosition.Ref());
  }
  if (IsBeingProfiledOrLogEnabled()) {
    nsPrintfCString msg("current time=%" PRId64 ", duration=%" PRId64,
                        mCurrentPosition.Ref().ToMicroseconds(),
                        mDuration.Ref()->ToMicroseconds());
    LOG("OnTimeupdate, {}", msg.get());
    PROFILER_MARKER_TEXT("EESM::OnTimeupdate", MEDIA_PLAYBACK, {}, msg);
  }
}

void ExternalEngineStateMachine::NotifyEventInternal(
    ExternalEngineEvent aEvent) {
  AssertOnTaskQueue();
  AUTO_PROFILER_LABEL("ExternalEngineStateMachine::NotifyEventInternal",
                      MEDIA_PLAYBACK);
  if (mState.IsShutdownEngine()) {
    return;
  }
  PROFILER_MARKER_TEXT("EESM::NotifyEventInternal", MEDIA_PLAYBACK, {},
                       nsPrintfCString("%s", ExternalEngineEventToStr(aEvent)));
  switch (aEvent) {
    case ExternalEngineEvent::LoadedMetaData:
      // We read metadata by ourselves, ignore this if there is any.
      MOZ_ASSERT(mInfo);
      break;
    case ExternalEngineEvent::LoadedFirstFrame:
      OnLoadedFirstFrame();
      break;
    case ExternalEngineEvent::LoadedData:
      OnLoadedData();
      break;
    case ExternalEngineEvent::Waiting:
      OnWaiting();
      break;
    case ExternalEngineEvent::Playing:
      OnPlaying();
      break;
    case ExternalEngineEvent::Seeked:
      OnSeeked();
      break;
    case ExternalEngineEvent::BufferingStarted:
      OnBufferingStarted();
      break;
    case ExternalEngineEvent::BufferingEnded:
      OnBufferingEnded();
      break;
    case ExternalEngineEvent::Timeupdate:
      OnTimeupdate();
      break;
    case ExternalEngineEvent::Ended:
      OnEnded();
      break;
    case ExternalEngineEvent::RequestForAudio:
      mHasEnoughAudio = false;
      if (ShouldRunEngineUpdateForRequest()) {
        RunningEngineUpdate(MediaData::Type::AUDIO_DATA);
      }
      break;
    case ExternalEngineEvent::RequestForVideo:
      mHasEnoughVideo = false;
      if (ShouldRunEngineUpdateForRequest()) {
        RunningEngineUpdate(MediaData::Type::VIDEO_DATA);
      }
      break;
    case ExternalEngineEvent::AudioEnough:
      mHasEnoughAudio = true;
      break;
    case ExternalEngineEvent::VideoEnough:
      mHasEnoughVideo = true;
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Undefined event!");
      break;
  }
}

bool ExternalEngineStateMachine::ShouldRunEngineUpdateForRequest() {
  // Running engine update will request new data, which could be run on
  // `RunningEngine` or `SeekingData` state. However, in `SeekingData` we should
  // only request new data after finishing reader seek, otherwise the reader
  // would start requesting data from a wrong position.
  return mState.IsRunningEngine() ||
         (mState.AsSeekingData() &&
          !mState.AsSeekingData()->mWaitingReaderSeeked);
}

void ExternalEngineStateMachine::NotifyErrorInternal(
    const MediaResult& aError) {
  AssertOnTaskQueue();
  LOG("Engine error: {}", aError.Description().get());
  PROFILER_MARKER_TEXT("EESM::NotifyErrorInternal", MEDIA_PLAYBACK, {},
                       aError.Description());
  if (mState.IsRecoverEngine()) {
    // Suppress errors that arrive during engine recovery to avoid
    // interrupting the in-progress reinit sequence.
    LOG("Ignoring error during hardware reset recovery: {}",
        aError.Description().get());
    return;
  }
  if (aError == NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR) {
    // The external engine doesn't support the type, try to notify the decoder
    // to use our own state machine again.
    ReportTelemetry(NS_ERROR_DOM_MEDIA_EXTERNAL_ENGINE_NOT_SUPPORTED_ERR);
    DecodeError(
        MediaResult(NS_ERROR_DOM_MEDIA_EXTERNAL_ENGINE_NOT_SUPPORTED_ERR));
  } else if (aError == NS_ERROR_DOM_MEDIA_REMOTE_CRASHED_MF_CDM_ERR) {
    ReportTelemetry(NS_ERROR_DOM_MEDIA_REMOTE_CRASHED_MF_CDM_ERR);
    RecoverFromCDMProcessCrashIfNeeded();
  } else if (mState.IsInitEngine() && mKeySystem.IsEmpty()) {
    // If any error occurs during media engine initialization, we should attempt
    // to use another state machine for playback. Unless the key system is
    // already set, it indicates that playback can only be initiated via the
    // media engine. In this case, we will propagate the error and refrain
    // from trying another state machine.
    LOG("Error happened on the engine initialization, the media engine "
        "playback might not be supported");
    ReportTelemetry(NS_ERROR_DOM_MEDIA_MEDIA_ENGINE_INITIALIZATION_ERR);
    DecodeError(
        MediaResult(NS_ERROR_DOM_MEDIA_EXTERNAL_ENGINE_NOT_SUPPORTED_ERR));
  } else {
    ReportTelemetry(aError);
    DecodeError(aError);
  }
}

void ExternalEngineStateMachine::NotifyResizingInternal(uint32_t aWidth,
                                                        uint32_t aHeight) {
  if (IsBeingProfiledOrLogEnabled()) {
    nsPrintfCString msg("video resize from [%d,%d] to [%d,%d]",
                        mVideoDisplay.width, mVideoDisplay.height, aWidth,
                        aHeight);
    LOG("{}", msg.get());
    PROFILER_MARKER_TEXT("EESM::NotifyResizingInternal", MEDIA_PLAYBACK, {},
                         msg);
  }
  mVideoDisplay = gfx::IntSize{aWidth, aHeight};
}

#ifdef MOZ_WMF_CDM
void ExternalEngineStateMachine::NotifyFrameServerModeInternal() {
  AssertOnTaskQueue();
  LOG("NotifyFrameServerModeInternal: engine is in frame server mode");
  MOZ_ASSERT(IsWMFClearKeySystemAndSupported(NS_ConvertUTF8toUTF16(mKeySystem)),
             "Frame server mode is only for WMFClearKey");
  mIsFrameServerMode = true;
  mOnPlaybackEvent.Notify(MediaPlaybackEvent::FrameServerMode);
  mHasReceivedFirstDecodedVideoFrame = true;
  if (!mSentFirstFrameLoadedEvent) {
    OnLoadedFirstFrame();
  }
}
#endif

void ExternalEngineStateMachine::RecoverFromCDMProcessCrashIfNeeded() {
  AssertOnTaskQueue();
  if (mState.IsRecoverEngine()) {
    LOG("In the recover state already");
    return;
  }
  ProcessCrashMonitor::NotifyCrash();
  if (!ProcessCrashMonitor::ShouldRecoverProcess()) {
    LOG("CDM process has crashed too many times, abort recovery");
    DecodeError(
        MediaResult(NS_ERROR_DOM_MEDIA_EXTERNAL_ENGINE_NOT_SUPPORTED_ERR));
    return;
  }

  if (mState.IsInitEngine()) {
    LOG("Failed on the engine initialization, the media engine playback might "
        "not be supported");
    DecodeError(
        MediaResult(NS_ERROR_DOM_MEDIA_EXTERNAL_ENGINE_NOT_SUPPORTED_ERR));
    return;
  }

  if (IsBeingProfiledOrLogEnabled()) {
    nsPrintfCString msg(
        "CDM process crashed, recover the engine again (last time=%" PRId64 ")",
        mCurrentPosition.Ref().ToMicroseconds());
    LOG("{}", msg.get());
    PROFILER_MARKER_TEXT("EESM::RecoverFromCDMProcessCrashIfNeeded",
                         MEDIA_PLAYBACK, {}, msg);
  }
  mIsEngineReady = false;
  ChangeStateTo(State::RecoverEngine);
  if (HasVideo()) {
    mVideoDataRequest.DisconnectIfExists();
    mVideoWaitRequest.DisconnectIfExists();
  }
  if (HasAudio()) {
    mAudioDataRequest.DisconnectIfExists();
    mAudioWaitRequest.DisconnectIfExists();
  }
  // Ask the reader to shutdown current decoders which are no longer available
  // due to the remote process crash.
  mReader->ReleaseResources();
  InitEngine();
}

void ExternalEngineStateMachine::RecoverFromHardwareReset() {
  AssertOnTaskQueue();
  if (mState.IsRecoverEngine()) {
    LOG("In the recover state already");
    return;
  }
  mRecoveryAttempts++;
  if (IsBeingProfiledOrLogEnabled()) {
    nsPrintfCString msg(
        "Hardware context reset, recovering engine (pos=%" PRId64 ")",
        mCurrentPosition.Ref().ToMicroseconds());
    LOG("{}", msg.get());
    PROFILER_MARKER_TEXT("EESM::RecoverFromHardwareReset", MEDIA_PLAYBACK, {},
                         msg);
  }
  mIsEngineReady = false;
  ChangeStateTo(State::RecoverEngine);
  if (HasVideo()) {
    mVideoDataRequest.DisconnectIfExists();
    mVideoWaitRequest.DisconnectIfExists();
  }
  if (HasAudio()) {
    mAudioDataRequest.DisconnectIfExists();
    mAudioWaitRequest.DisconnectIfExists();
  }
  MOZ_ASSERT(mEngine);
  mEngine->Shutdown();
  mReader->ReleaseResources();
  InitEngine();
}

#ifdef MOZ_WMF_CDM
void ExternalEngineStateMachine::NotifyWaitingForKeyInternal() {
  AssertOnTaskQueue();
  mReader->NotifyWaitingForKey();
}
#endif

media::TimeUnit ExternalEngineStateMachine::GetVideoThreshold() {
  AssertOnTaskQueue();
  if (auto* state = mState.AsSeekingData()) {
    return state->GetTargetTime();
  }
  return mCurrentPosition.Ref();
}

void ExternalEngineStateMachine::UpdateSecondaryVideoContainer() {
  AssertOnTaskQueue();
  LOG("UpdateSecondaryVideoContainer={}",
      fmt::ptr(mSecondaryVideoContainer.Ref().get()));
  mOnSecondaryVideoContainerInstalled.Notify(mSecondaryVideoContainer.Ref());
}

RefPtr<SetCDMPromise> ExternalEngineStateMachine::SetCDMProxy(
    CDMProxy* aProxy) {
  if (!OnTaskQueue()) {
    return InvokeAsync(OwnerThread(), __func__,
                       [self = RefPtr{this}, proxy = RefPtr{aProxy}, this]() {
                         return SetCDMProxy(proxy);
                       });
  }
  AssertOnTaskQueue();
  if (mState.IsShutdownEngine()) {
    return SetCDMPromise::CreateAndReject(NS_ERROR_FAILURE, __func__);
  }

  if (!aProxy) {
    // A null proxy means the CDM is being cleared. The EESM-specific path
    // below requires a valid WMFCDMProxy to obtain its ID and register with
    // the engine, so it cannot handle null. Delegate to the base class, which
    // clears the CDM state on the reader via mReader->SetCDMProxy(null).
    // For WMFClearKey, failing to clear the CDM state would leave the reader
    // holding a stale reference to the old proxy, causing use-after-free or
    // silent decryption failures on subsequent playback.
    mPendingCDMProxy = nullptr;
    return MediaDecoderStateMachineBase::SetCDMProxy(aProxy);
  }

  if (!mIsEngineReady) {
    LOG("SetCDMProxy is called before init");
    mReader->SetEncryptedCustomIdent();
    if (!mPendingCDMProxy) {
      mPendingTasks.AppendElement(NS_NewRunnableFunction(
          "ExternalEngineStateMachine::ApplyPendingCDMProxy",
          [self = RefPtr{this}, this] { ApplyPendingCDMProxy(); }));
    }
    mPendingCDMProxy = aProxy;
    LOG("Pending CDM proxy {} until the engine is ready", fmt::ptr(aProxy));
    // EME 7.2 step 5.3.1: "Associate the CDM instance represented by mediaKeys
    // with the media element for decrypting media data." That does not depend
    // on the engine, so the promise settles here. If the engine refuses the
    // stream or cannot take the CDM, that is reported as a media error.
    // https://w3c.github.io/encrypted-media/#dom-htmlmediaelement-setmediakeys
    return SetCDMPromise::CreateAndResolve(true, __func__);
  }

  // TODO : set CDM proxy again if we recreate the media engine after crash.
  // A proxy set while the engine was not ready is replaced by this one.
  mPendingCDMProxy = nullptr;
  mKeySystem = NS_ConvertUTF16toUTF8(aProxy->KeySystem());
  if (IsBeingProfiledOrLogEnabled()) {
    nsPrintfCString msg("SetCDMProxy=%p (key-system=%s)", aProxy,
                        mKeySystem.get());
    LOG("{}", msg.get());
    PROFILER_MARKER_TEXT("EESM::SetCDMProxy", MEDIA_PLAYBACK, {}, msg);
  }
  MOZ_DIAGNOSTIC_ASSERT(mEngine);
  // TODO : we should check the result of setting CDM proxy in the MFCDM process
  if (!mEngine->SetCDMProxy(aProxy)) {
    LOG("Failed to set CDM proxy on the engine");
    return SetCDMPromise::CreateAndReject(NS_ERROR_DOM_MEDIA_CDM_ERR, __func__);
  }
  return MediaDecoderStateMachineBase::SetCDMProxy(aProxy);
}

void ExternalEngineStateMachine::ApplyPendingCDMProxy() {
  AssertOnTaskQueue();
  RefPtr<CDMProxy> proxy = std::move(mPendingCDMProxy);
  if (!proxy) {
    LOG("No pending CDM proxy to apply");
    return;
  }
  // A proxy applied before the engine was recreated is replaced by this one.
  mSetCDMProxyRequest.DisconnectIfExists();
  SetCDMProxy(proxy)
      ->Then(OwnerThread(), __func__,
             [self = RefPtr{this},
              this](const SetCDMPromise::ResolveOrRejectValue& aVal) {
               mSetCDMProxyRequest.Complete();
               if (aVal.IsResolve()) {
                 LOG("Applied the pending CDM proxy");
                 return;
               }
               // The reader rejects with an invalid state error when a newer
               // proxy replaces this one or when it shuts down, and neither is
               // a playback failure.
               if (aVal.RejectValue() == NS_ERROR_DOM_INVALID_STATE_ERR) {
                 LOG("The pending CDM proxy was replaced or the reader shut "
                     "down");
                 return;
               }
               DecodeError(aVal.RejectValue());
             })
      ->Track(mSetCDMProxyRequest);
}

nsresult ExternalEngineStateMachine::IsCDMProxySupported(CDMProxy* aProxy) {
#ifdef MOZ_WMF_CDM
  MOZ_ASSERT(aProxy);

  // The CDM needs to be hosted in the same process of the external engine, and
  // only WMFCDM meets this requirement.
  if (!aProxy->AsWMFCDMProxy()) {
    return NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR;
  }

  // 1=enabled encrypted and clear, 2=enabled encrypted
  if (StaticPrefs::media_wmf_media_engine_enabled() != 1 &&
      StaticPrefs::media_wmf_media_engine_enabled() != 2) {
    return NS_ERROR_DOM_MEDIA_NOT_ALLOWED_ERR;
  }

  return NS_OK;
#else
  return NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR;
#endif
}

void ExternalEngineStateMachine::ReportTelemetry(const MediaResult& aError) {
  glean::mfcdm::ErrorExtra extraData;
  extraData.errorName = Some(aError.ErrorName());
  extraData.currentState = Some(nsAutoCString{StateToStr(mState.mName)});
  nsAutoCString resolution;
  if (mInfo) {
    if (mInfo->HasAudio()) {
      extraData.audioCodec = Some(mInfo->mAudio.mMimeType);
    }
    if (mInfo->HasVideo()) {
      extraData.videoCodec = Some(mInfo->mVideo.mMimeType);
      DetermineResolutionForTelemetry(*mInfo, resolution);
      extraData.resolution = Some(resolution);
    }
  }
  if (!mKeySystem.IsEmpty()) {
    extraData.keySystem = Some(mKeySystem);
  }
  if (auto platformErrorCode = aError.GetPlatformErrorCode()) {
    extraData.platformError = platformErrorCode;
  }
  // These gfxVars are populated once in the parent process before any content
  // process starts and are not modified afterwards. This runs on the state
  // machine task queue rather than the main thread, so copy the values out
  // instead of holding a reference into the gfxVars singleton.
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
  const nsCString adapterVendorID = gfx::gfxVars::AdapterVendorID();
  if (!adapterVendorID.IsEmpty()) {
    extraData.adapterVendorId = Some(adapterVendorID);
    extraData.adapterDeviceId = Some(gfx::gfxVars::AdapterDeviceID());
    extraData.adapterDriverVersion = Some(gfx::gfxVars::AdapterDriverVersion());
  }
  glean::mfcdm::error.Record(Some(extraData));
  if (MOZ_LOG_TEST(gMediaDecoderLog, LogLevel::Debug)) {
    nsPrintfCString logMessage{"MFCDM Error event, error=%s",
                               aError.ErrorName().get()};
    if (auto platformErrorCode = aError.GetPlatformErrorCode()) {
      logMessage.Append(nsPrintfCString{", hr=%x", *platformErrorCode});
    }
    if (mInfo) {
      if (mInfo->HasAudio()) {
        logMessage.Append(
            nsPrintfCString{", audio=%s", mInfo->mAudio.mMimeType.get()});
      }
      if (mInfo->HasVideo()) {
        logMessage.Append(nsPrintfCString{", video=%s, resolution=%s",
                                          mInfo->mVideo.mMimeType.get(),
                                          resolution.get()});
      }
    }
    if (!mKeySystem.IsEmpty()) {
      logMessage.Append(nsPrintfCString{", keySystem=%s", mKeySystem.get()});
    }
    LOG("{}", logMessage.get());
  }
}

void ExternalEngineStateMachine::ReportRecoveryTelemetry(bool aRecovered) {
  glean::mfcdm::RecoveryExtra extraData;
  extraData.recovered = Some(aRecovered);
  extraData.attempts = Some(mRecoveryAttempts);
  if (mHardwareResetError) {
    extraData.platformError = mHardwareResetError;
  }
  if (!mKeySystem.IsEmpty()) {
    extraData.keySystem = Some(mKeySystem);
  }
  extraData.currentState = Some(nsAutoCString{StateToStr(mState.mName)});
  glean::mfcdm::recovery.Record(Some(extraData));
  LOG("MFCDM Recovery event, recovered={}, attempts={}", aRecovered,
      mRecoveryAttempts);

  mRecoveryAttempts = 0;
  mHardwareResetError = Nothing();
}

void ExternalEngineStateMachine::DecodeError(const MediaResult& aError) {
  // The normal state machine cannot use a Media Foundation CDM, so falling back
  // to it with one pending would leave playback waiting for a key forever.
  if (aError == NS_ERROR_DOM_MEDIA_EXTERNAL_ENGINE_NOT_SUPPORTED_ERR &&
      mPendingCDMProxy) {
    LOG("Cannot fall back to another state machine with a pending CDM proxy");
    nsAutoCString message("The media engine cannot play this stream");
    if (!aError.Message().IsEmpty()) {
      message.AppendPrintf(": %s", aError.Message().get());
    }
    DecodeError(MediaResult(NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR, message));
    return;
  }
  if (aError != NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA ||
      aError != NS_ERROR_DOM_MEDIA_CANCELED) {
    mHasFatalError = true;
  }
  MediaDecoderStateMachineBase ::DecodeError(aError);
}

void ExternalEngineStateMachine::NotifyAudibleStateChangeIfNeeded() {
  // Only perform a simple check because we can't access audio data from the
  // external engine.
  mIsAudioDataAudible = mInfo && HasAudio() &&
                        mPlayState == MediaDecoder::PLAY_STATE_PLAYING &&
                        mState.IsRunningEngine();
}

#undef FMT
#undef LOG
#undef LOGV
#undef LOGW
#undef LOGE

}  // namespace mozilla
