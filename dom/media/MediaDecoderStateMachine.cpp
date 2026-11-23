/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaDecoderStateMachine.h"

#include <stdint.h>

#include <algorithm>
#include <memory>
#include <utility>

#include "AudioSegment.h"
#include "DOMMediaStream.h"
#include "ImageContainer.h"
#include "MediaDecoder.h"
#include "MediaShutdownManager.h"
#include "MediaTimer.h"
#include "MediaTrackGraph.h"
#include "PerformanceRecorder.h"
#include "ReaderProxy.h"
#include "TimeUnits.h"
#include "VideoSegment.h"
#include "VideoUtils.h"
#include "mediasink/AudioSink.h"
#include "mediasink/AudioSinkWrapper.h"
#include "mediasink/DecodedStream.h"
#include "mediasink/VideoSink.h"
#include "mozilla/Logging.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/ProfilerMarkerTypes.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/SharedThreadPool.h"
#include "mozilla/Sprintf.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/TaskQueue.h"
#include "nsIMemoryReporter.h"
#include "nsPrintfCString.h"
#include "nsTArray.h"

namespace mozilla {

using namespace mozilla::media;

#define NS_DispatchToMainThread(...) \
  CompileError_UseAbstractThreadDispatchInstead

// avoid redefined macro in unified build
#undef FMT
#undef LOG
#undef LOGV
#undef LOGW
#undef LOGE
#undef SFMT
#undef SLOG
#undef SLOGW
#undef SLOGE

#define FMT(x, ...) "Decoder=%p " x, mDecoderID, ##__VA_ARGS__
#define LOG(x, ...)                                                 \
  DDMOZ_LOG_FMT(gMediaDecoderLog, LogLevel::Debug, "Decoder={} " x, \
                fmt::ptr(mDecoderID), ##__VA_ARGS__)
#define LOGV(x, ...)                                                  \
  DDMOZ_LOG_FMT(gMediaDecoderLog, LogLevel::Verbose, "Decoder={} " x, \
                fmt::ptr(mDecoderID), ##__VA_ARGS__)
#define LOGW(x, ...) NS_WARNING(nsPrintfCString(FMT(x, ##__VA_ARGS__)).get())
#define LOGE(x, ...)                                                   \
  NS_DebugBreak(NS_DEBUG_WARNING,                                      \
                nsPrintfCString(FMT(x, ##__VA_ARGS__)).get(), nullptr, \
                __FILE__, __LINE__)

// Used by StateObject and its sub-classes
#define SFMT(x, ...)                                                     \
  "Decoder=%p state=%s " x, mMaster->mDecoderID, ToStateStr(GetState()), \
      ##__VA_ARGS__
#define SLOG(x, ...)                                                         \
  DDMOZ_LOGEX_FMT(mMaster, gMediaDecoderLog, LogLevel::Debug, "state={} " x, \
                  ToStateStr(GetState()), ##__VA_ARGS__)
#define SLOGW(x, ...) NS_WARNING(nsPrintfCString(SFMT(x, ##__VA_ARGS__)).get())
#define SLOGE(x, ...)                                                   \
  NS_DebugBreak(NS_DEBUG_WARNING,                                       \
                nsPrintfCString(SFMT(x, ##__VA_ARGS__)).get(), nullptr, \
                __FILE__, __LINE__)

// Certain constants get stored as member variables and then adjusted by various
// scale factors on a per-decoder basis. We want to make sure to avoid using
// these constants directly, so we put them in a namespace.
namespace detail {

// Resume a suspended video decoder to the current playback position plus this
// time premium for compensating the seeking delay.
static constexpr auto RESUME_VIDEO_PREMIUM = TimeUnit::FromMicroseconds(125000);

static const int64_t AMPLE_AUDIO_USECS = 2000000;

// If more than this much decoded audio is queued, we'll hold off
// decoding more audio.
static constexpr auto AMPLE_AUDIO_THRESHOLD =
    TimeUnit::FromMicroseconds(AMPLE_AUDIO_USECS);

}  // namespace detail

// If we have fewer than LOW_VIDEO_FRAMES decoded frames, and
// we're not "prerolling video", we'll skip the video up to the next keyframe
// which is at or after the current playback position.
static const uint32_t LOW_VIDEO_FRAMES = 2;

// Arbitrary "frame duration" when playing only audio.
static const uint32_t AUDIO_DURATION_USECS = 40000;

namespace detail {

// If we have less than this much buffered data available, we'll consider
// ourselves to be running low on buffered data. We determine how much
// buffered data we have remaining using the reader's GetBuffered()
// implementation.
static const int64_t LOW_BUFFER_THRESHOLD_USECS = 5000000;

static constexpr auto LOW_BUFFER_THRESHOLD =
    TimeUnit::FromMicroseconds(LOW_BUFFER_THRESHOLD_USECS);

// LOW_BUFFER_THRESHOLD_USECS needs to be greater than AMPLE_AUDIO_USECS,
// otherwise the skip-to-keyframe logic can activate when we're running low on
// data.
static_assert(LOW_BUFFER_THRESHOLD_USECS > AMPLE_AUDIO_USECS,
              "LOW_BUFFER_THRESHOLD_USECS is too small");

}  // namespace detail

// Amount of excess data to add in to the "should we buffer" calculation.
static constexpr auto EXHAUSTED_DATA_MARGIN =
    TimeUnit::FromMicroseconds(100000);

static const uint32_t MIN_VIDEO_QUEUE_SIZE = 3;

template <typename Type, typename Function>
static void DiscardFramesFromTail(MediaQueue<Type>& aQueue,
                                  const Function&& aTest) {
  while (aQueue.GetSize()) {
    if (aTest(aQueue.PeekBack()->mTime.ToMicroseconds())) {
      RefPtr<Type> releaseMe = aQueue.PopBack();
      continue;
    }
    break;
  }
}

// Delay, in milliseconds, that tabs needs to be in background before video
// decoding is suspended.
static TimeDuration SuspendBackgroundVideoDelay() {
  return TimeDuration::FromMilliseconds(
      StaticPrefs::media_suspend_background_video_delay_ms());
}

class MediaDecoderStateMachine::StateObject {
 public:
  virtual ~StateObject() = default;
  virtual void Exit() {}  // Exit action.
  virtual void Step() {}  // Perform a 'cycle' of this state object.
  virtual State GetState() const = 0;

  // Event handlers for various events.
  virtual void HandleAudioCaptured() {}
  virtual void HandleAudioDecoded(AudioData* aAudio) {
    Crash("Unexpected event!", __func__);
  }
  virtual void HandleVideoDecoded(VideoData* aVideo) {
    Crash("Unexpected event!", __func__);
  }
  virtual void HandleAudioWaited(MediaData::Type aType) {
    Crash("Unexpected event!", __func__);
  }
  virtual void HandleVideoWaited(MediaData::Type aType) {
    Crash("Unexpected event!", __func__);
  }
  virtual void HandleWaitingForAudio() { Crash("Unexpected event!", __func__); }
  virtual void HandleAudioCanceled() { Crash("Unexpected event!", __func__); }
  virtual void HandleEndOfAudio() { Crash("Unexpected event!", __func__); }
  virtual void HandleWaitingForVideo() { Crash("Unexpected event!", __func__); }
  virtual void HandleVideoCanceled() { Crash("Unexpected event!", __func__); }
  virtual void HandleEndOfVideo() { Crash("Unexpected event!", __func__); }

  virtual RefPtr<MediaDecoder::SeekPromise> HandleSeek(
      const SeekTarget& aTarget);

  virtual RefPtr<ShutdownPromise> HandleShutdown();

  virtual void HandleVideoSuspendTimeout() = 0;

  virtual void HandleResumeVideoDecoding(const TimeUnit& aTarget);

  virtual void HandlePlayStateChanged(MediaDecoder::PlayState aPlayState) {}

  virtual void GetDebugInfo(
      dom::MediaDecoderStateMachineDecodingStateDebugInfo& aInfo) {}

  virtual void HandleLoopingChanged() {}

 private:
  template <class S, typename R, typename... As>
  auto ReturnTypeHelper(R (S::*)(As...)) -> R;

  void Crash(const char* aReason, const char* aSite) {
    char buf[1024];
    SprintfLiteral(buf, "%s state=%s callsite=%s", aReason,
                   ToStateStr(GetState()), aSite);
    MOZ_ReportAssertionFailure(buf, __FILE__, __LINE__);
    MOZ_CRASH();
  }

 protected:
  enum class EventVisibility : int8_t { Observable, Suppressed };

  using Master = MediaDecoderStateMachine;
  explicit StateObject(Master* aPtr) : mMaster(aPtr) {}
  TaskQueue* OwnerThread() const { return mMaster->mTaskQueue; }
  ReaderProxy* Reader() const { return mMaster->mReader; }
  const MediaInfo& Info() const { return mMaster->Info(); }
  MediaQueue<AudioData>& AudioQueue() const { return mMaster->mAudioQueue; }
  MediaQueue<VideoData>& VideoQueue() const { return mMaster->mVideoQueue; }

  template <class S, typename... Args, size_t... Indexes>
  auto CallEnterMemberFunction(S* aS, std::tuple<Args...>& aTuple,
                               std::index_sequence<Indexes...>)
      -> decltype(ReturnTypeHelper(&S::Enter)) {
    AUTO_PROFILER_LABEL("StateObject::CallEnterMemberFunction", MEDIA_PLAYBACK);
    return aS->Enter(std::move(std::get<Indexes>(aTuple))...);
  }

  // Note this function will delete the current state object.
  // Don't access members to avoid UAF after this call.
  template <class S, typename... Ts>
  auto SetState(Ts&&... aArgs) -> decltype(ReturnTypeHelper(&S::Enter)) {
    // |aArgs| must be passed by reference to avoid passing MOZ_NON_PARAM class
    // SeekJob by value.  See bug 1287006 and bug 1338374.  But we still *must*
    // copy the parameters, because |Exit()| can modify them.  See bug 1312321.
    // So we 1) pass the parameters by reference, but then 2) immediately copy
    // them into a Tuple to be safe against modification, and finally 3) move
    // the elements of the Tuple into the final function call.
    auto copiedArgs = std::make_tuple(std::forward<Ts>(aArgs)...);

    // Copy mMaster which will reset to null.
    auto* master = mMaster;

    auto* s = new S(master);

    // It's possible to seek again during seeking, otherwise the new state
    // should always be different from the original one.
    MOZ_ASSERT(GetState() != s->GetState() ||
               GetState() == DECODER_STATE_SEEKING_ACCURATE ||
               GetState() == DECODER_STATE_SEEKING_FROMDORMANT ||
               GetState() == DECODER_STATE_SEEKING_NEXTFRAMESEEKING ||
               GetState() == DECODER_STATE_SEEKING_VIDEOONLY);

    SLOG("change state to: {}", ToStateStr(s->GetState()));
    PROFILER_MARKER_TEXT("MDSM::StateChange", MEDIA_PLAYBACK, {},
                         nsPrintfCString("%s", ToStateStr(s->GetState())));

    Exit();

    // Delete the old state asynchronously to avoid UAF if the caller tries to
    // access its members after SetState() returns.
    master->OwnerThread()->DispatchDirectTask(
        NS_NewRunnableFunction("MDSM::StateObject::DeleteOldState",
                               [toDelete = std::move(master->mStateObj)]() {}));
    // Also reset mMaster to catch potentail UAF.
    mMaster = nullptr;

    master->mStateObj.reset(s);
    return CallEnterMemberFunction(s, copiedArgs,
                                   std::index_sequence_for<Ts...>{});
  }

  RefPtr<MediaDecoder::SeekPromise> SetSeekingState(
      SeekJob&& aSeekJob, EventVisibility aVisibility);

  void SetDecodingState();

  // Take a raw pointer in order not to change the life cycle of MDSM.
  // It is guaranteed to be valid by MDSM.
  Master* mMaster;
};

/**
 * Purpose: decode metadata like duration and dimensions of the media resource.
 *
 * Transition to other states when decoding metadata is done:
 *   SHUTDOWN if failing to decode metadata.
 *   DECODING_FIRSTFRAME otherwise.
 */
class MediaDecoderStateMachine::DecodeMetadataState
    : public MediaDecoderStateMachine::StateObject {
 public:
  explicit DecodeMetadataState(Master* aPtr) : StateObject(aPtr) {}

  void Enter() {
    MOZ_ASSERT(!mMaster->mVideoDecodeSuspended);
    MOZ_ASSERT(!mMetadataRequest.Exists());
    SLOG("Dispatching AsyncReadMetadata");

    // We disconnect mMetadataRequest in Exit() so it is fine to capture
    // a raw pointer here.
    Reader()
        ->ReadMetadata()
        ->Then(
            OwnerThread(), __func__,
            [this](MetadataHolder&& aMetadata) {
              OnMetadataRead(std::move(aMetadata));
            },
            [this](const MediaResult& aError) { OnMetadataNotRead(aError); })
        ->Track(mMetadataRequest);
  }

  void Exit() override { mMetadataRequest.DisconnectIfExists(); }

  State GetState() const override { return DECODER_STATE_DECODING_METADATA; }

  RefPtr<MediaDecoder::SeekPromise> HandleSeek(
      const SeekTarget& aTarget) override {
    MOZ_DIAGNOSTIC_CRASH("Can't seek while decoding metadata.");
    return MediaDecoder::SeekPromise::CreateAndReject(true, __func__);
  }

  void HandleVideoSuspendTimeout() override {
    // Do nothing since no decoders are created yet.
  }

  void HandleResumeVideoDecoding(const TimeUnit&) override {
    // We never suspend video decoding in this state.
    MOZ_ASSERT(false, "Shouldn't have suspended video decoding.");
  }

 private:
  void OnMetadataRead(MetadataHolder&& aMetadata);

  void OnMetadataNotRead(const MediaResult& aError) {
    AUTO_PROFILER_LABEL("DecodeMetadataState::OnMetadataNotRead",
                        MEDIA_PLAYBACK);

    mMetadataRequest.Complete();
    SLOGE("Decode metadata failed, shutting down decoder");
    mMaster->DecodeError(aError);
  }

  MozPromiseRequestHolder<MediaFormatReader::MetadataPromise> mMetadataRequest;
};

/**
 * Purpose: release decoder resources to save memory and hardware resources.
 *
 * Transition to:
 *   SEEKING if any seek request or play state changes to PLAYING.
 */
class MediaDecoderStateMachine::DormantState
    : public MediaDecoderStateMachine::StateObject {
 public:
  explicit DormantState(Master* aPtr) : StateObject(aPtr) {}

  void Enter() {
    if (mMaster->IsPlaying()) {
      mMaster->StopPlayback();
    }

    // Calculate the position to seek to when exiting dormant.
    auto t = mMaster->mMediaSink->IsStarted() ? mMaster->GetClock()
                                              : mMaster->GetMediaTime();
    mMaster->AdjustByLooping(t);
    mPendingSeek.mTarget.emplace(t, SeekTarget::Accurate);
    // SeekJob asserts |mTarget.IsValid() == !mPromise.IsEmpty()| so we
    // need to create the promise even it is not used at all.
    // The promise may be used when coming out of DormantState into
    // SeekingState.
    RefPtr<MediaDecoder::SeekPromise> x =
        mPendingSeek.mPromise.Ensure(__func__);

    // Reset the decoding state to ensure that any queued video frames are
    // released and don't consume video memory.
    mMaster->ResetDecode();

    // No need to call StopMediaSink() here.
    // We will do it during seeking when exiting dormant.

    // Ignore WAIT_FOR_DATA since we won't decode in dormant.
    mMaster->mAudioWaitRequest.DisconnectIfExists();
    mMaster->mVideoWaitRequest.DisconnectIfExists();

    MaybeReleaseResources();
  }

  void Exit() override {
    // mPendingSeek is either moved when exiting dormant or
    // should be rejected here before transition to SHUTDOWN.
    mPendingSeek.RejectIfExists(__func__);
  }

  State GetState() const override { return DECODER_STATE_DORMANT; }

  RefPtr<MediaDecoder::SeekPromise> HandleSeek(
      const SeekTarget& aTarget) override;

  void HandleVideoSuspendTimeout() override {
    // Do nothing since we've released decoders in Enter().
  }

  void HandleResumeVideoDecoding(const TimeUnit&) override {
    // Do nothing since we won't resume decoding until exiting dormant.
  }

  void HandlePlayStateChanged(MediaDecoder::PlayState aPlayState) override;

  void HandleAudioDecoded(AudioData*) override { MaybeReleaseResources(); }
  void HandleVideoDecoded(VideoData*) override { MaybeReleaseResources(); }
  void HandleWaitingForAudio() override { MaybeReleaseResources(); }
  void HandleWaitingForVideo() override { MaybeReleaseResources(); }
  void HandleAudioCanceled() override { MaybeReleaseResources(); }
  void HandleVideoCanceled() override { MaybeReleaseResources(); }
  void HandleEndOfAudio() override { MaybeReleaseResources(); }
  void HandleEndOfVideo() override { MaybeReleaseResources(); }

 private:
  void MaybeReleaseResources() {
    if (!mMaster->mAudioDataRequest.Exists() &&
        !mMaster->mVideoDataRequest.Exists()) {
      // Release decoders only when they are idle. Otherwise it might cause
      // decode error later when resetting decoders during seeking.
      mMaster->mReader->ReleaseResources();
    }
  }

  SeekJob mPendingSeek;
};

/**
 * Purpose: decode the 1st audio and video frames to fire the 'loadeddata'
 * event.
 *
 * Transition to:
 *   SHUTDOWN if any decode error.
 *   SEEKING if any seek request.
 *   DECODING/LOOPING_DECODING when the 'loadeddata' event is fired.
 */
class MediaDecoderStateMachine::DecodingFirstFrameState
    : public MediaDecoderStateMachine::StateObject {
 public:
  explicit DecodingFirstFrameState(Master* aPtr) : StateObject(aPtr) {}

  void Enter();

  void Exit() override {
    // mPendingSeek is either moved in MaybeFinishDecodeFirstFrame()
    // or should be rejected here before transition to SHUTDOWN.
    mPendingSeek.RejectIfExists(__func__);
  }

  State GetState() const override { return DECODER_STATE_DECODING_FIRSTFRAME; }

  void HandleAudioDecoded(AudioData* aAudio) override {
    mMaster->PushAudio(aAudio);
    MaybeFinishDecodeFirstFrame();
  }

  void HandleVideoDecoded(VideoData* aVideo) override {
    mMaster->PushVideo(aVideo);
    MaybeFinishDecodeFirstFrame();
  }

  void HandleWaitingForAudio() override {
    mMaster->WaitForData(MediaData::Type::AUDIO_DATA);
  }

  void HandleAudioCanceled() override { mMaster->RequestAudioData(); }

  void HandleEndOfAudio() override {
    AudioQueue().Finish();
    MaybeFinishDecodeFirstFrame();
  }

  void HandleWaitingForVideo() override {
    mMaster->WaitForData(MediaData::Type::VIDEO_DATA);
  }

  void HandleVideoCanceled() override {
    mMaster->RequestVideoData(media::TimeUnit());
  }

  void HandleEndOfVideo() override {
    VideoQueue().Finish();
    MaybeFinishDecodeFirstFrame();
  }

  void HandleAudioWaited(MediaData::Type aType) override {
    mMaster->RequestAudioData();
  }

  void HandleVideoWaited(MediaData::Type aType) override {
    mMaster->RequestVideoData(media::TimeUnit());
  }

  void HandleVideoSuspendTimeout() override {
    // Do nothing for we need to decode the 1st video frame to get the
    // dimensions.
  }

  void HandleResumeVideoDecoding(const TimeUnit&) override {
    // We never suspend video decoding in this state.
    MOZ_ASSERT(false, "Shouldn't have suspended video decoding.");
  }

  RefPtr<MediaDecoder::SeekPromise> HandleSeek(
      const SeekTarget& aTarget) override {
    if (mMaster->mIsMSE) {
      return StateObject::HandleSeek(aTarget);
    }
    // Delay seek request until decoding first frames for non-MSE media.
    SLOG("Not Enough Data to seek at this stage, queuing seek");
    mPendingSeek.RejectIfExists(__func__);
    mPendingSeek.mTarget.emplace(aTarget);
    return mPendingSeek.mPromise.Ensure(__func__);
  }

 private:
  // Notify FirstFrameLoaded if having decoded first frames and
  // transition to SEEKING if there is any pending seek, or DECODING otherwise.
  void MaybeFinishDecodeFirstFrame();

  SeekJob mPendingSeek;
};

/**
 * Purpose: decode audio/video data for playback.
 *
 * Transition to:
 *   DORMANT if playback is paused for a while.
 *   SEEKING if any seek request.
 *   SHUTDOWN if any decode error.
 *   BUFFERING if playback can't continue due to lack of decoded data.
 *   COMPLETED when having decoded all audio/video data.
 *   LOOPING_DECODING when media start seamless looping
 */
class MediaDecoderStateMachine::DecodingState
    : public MediaDecoderStateMachine::StateObject {
 public:
  explicit DecodingState(Master* aPtr)
      : StateObject(aPtr), mDormantTimer(OwnerThread()) {}

  void Enter();

  void Exit() override {
    if (!mDecodeStartTime.IsNull()) {
      TimeDuration decodeDuration = TimeStamp::Now() - mDecodeStartTime;
      SLOG("Exiting DECODING, decoded for {:.3f}s", decodeDuration.ToSeconds());
    }
    mDormantTimer.Reset();
    mOnAudioPopped.DisconnectIfExists();
    mOnVideoPopped.DisconnectIfExists();
  }

  void Step() override;

  State GetState() const override { return DECODER_STATE_DECODING; }

  void HandleAudioDecoded(AudioData* aAudio) override {
    mMaster->PushAudio(aAudio);
    DispatchDecodeTasksIfNeeded();
    MaybeStopPrerolling();
  }

  void HandleVideoDecoded(VideoData* aVideo) override {
    // We only do this check when we're not looping, which can be known by
    // checking the queue's offset.
    const auto currentTime = mMaster->GetMediaTime();
    if (aVideo->GetEndTime() < currentTime &&
        VideoQueue().GetOffset() == media::TimeUnit::Zero()) {
      if (!mVideoFirstLateTime) {
        mVideoFirstLateTime = Some(TimeStamp::Now());
      }
      PROFILER_MARKER("Video falling behind", MEDIA_PLAYBACK, {},
                      VideoFallingBehindMarker, aVideo->mTime.ToMicroseconds(),
                      currentTime.ToMicroseconds());
      SLOG("video {} starts being late (current={})",
           aVideo->mTime.ToMicroseconds(), currentTime.ToMicroseconds());
    } else {
      mVideoFirstLateTime.reset();
    }
    mMaster->PushVideo(aVideo);
    DispatchDecodeTasksIfNeeded();
    MaybeStopPrerolling();
  }

  void HandleAudioCanceled() override { mMaster->RequestAudioData(); }

  void HandleVideoCanceled() override {
    mMaster->RequestVideoData(mMaster->GetMediaTime(),
                              ShouldRequestNextKeyFrame());
  }

  void HandleEndOfAudio() override;
  void HandleEndOfVideo() override;

  void HandleWaitingForAudio() override {
    mMaster->WaitForData(MediaData::Type::AUDIO_DATA);
    MaybeStopPrerolling();
  }

  void HandleWaitingForVideo() override {
    mMaster->WaitForData(MediaData::Type::VIDEO_DATA);
    MaybeStopPrerolling();
  }

  void HandleAudioWaited(MediaData::Type aType) override {
    mMaster->RequestAudioData();
  }

  void HandleVideoWaited(MediaData::Type aType) override {
    mMaster->RequestVideoData(mMaster->GetMediaTime(),
                              ShouldRequestNextKeyFrame());
  }

  void HandleAudioCaptured() override {
    MaybeStopPrerolling();
    // MediaSink is changed. Schedule Step() to check if we can start playback.
    mMaster->ScheduleStateMachine();
  }

  void HandleVideoSuspendTimeout() override {
    // No video, so nothing to suspend.
    if (!mMaster->HasVideo()) {
      return;
    }

    PROFILER_MARKER_UNTYPED("MDSM::EnterVideoSuspend", MEDIA_PLAYBACK);
    mMaster->mVideoDecodeSuspended = true;
    mMaster->mOnPlaybackEvent.Notify(MediaPlaybackEvent::EnterVideoSuspend);
    Reader()->SetVideoBlankDecode(true);
  }

  void HandlePlayStateChanged(MediaDecoder::PlayState aPlayState) override {
    // Schedule Step() to check if we can start or stop playback.
    mMaster->ScheduleStateMachine();
    if (aPlayState == MediaDecoder::PLAY_STATE_PLAYING) {
      // Try to dispatch decoding tasks for mMinimizePreroll might be reset.
      DispatchDecodeTasksIfNeeded();
    }

    if (aPlayState == MediaDecoder::PLAY_STATE_PAUSED) {
      StartDormantTimer();
      mVideoFirstLateTime.reset();
    } else {
      mDormantTimer.Reset();
    }
  }

  void GetDebugInfo(
      dom::MediaDecoderStateMachineDecodingStateDebugInfo& aInfo) override {
    aInfo.mIsPrerolling = mIsPrerolling;
  }

  void HandleLoopingChanged() override { SetDecodingState(); }

 protected:
  virtual void EnsureAudioDecodeTaskQueued();
  virtual void EnsureVideoDecodeTaskQueued();

  virtual bool ShouldStopPrerolling() const {
    return mIsPrerolling &&
           (DonePrerollingAudio() ||
            IsWaitingData(MediaData::Type::AUDIO_DATA)) &&
           (DonePrerollingVideo() ||
            IsWaitingData(MediaData::Type::VIDEO_DATA));
  }

  virtual bool IsWaitingData(MediaData::Type aType) const {
    if (aType == MediaData::Type::AUDIO_DATA) {
      return mMaster->IsWaitingAudioData();
    }
    MOZ_ASSERT(aType == MediaData::Type::VIDEO_DATA);
    return mMaster->IsWaitingVideoData();
  }

  void MaybeStopPrerolling() {
    if (ShouldStopPrerolling()) {
      mIsPrerolling = false;
      // Check if we can start playback.
      mMaster->ScheduleStateMachine();
    }
  }

  bool ShouldRequestNextKeyFrame() const {
    if (!mVideoFirstLateTime) {
      return false;
    }
    const double elapsedTimeMs =
        (TimeStamp::Now() - *mVideoFirstLateTime).ToMilliseconds();
    const bool rv = elapsedTimeMs >=
                    StaticPrefs::media_decoder_skip_when_video_too_slow_ms();
    if (rv) {
      PROFILER_MARKER_UNTYPED("Skipping to next keyframe", MEDIA_PLAYBACK);
      SLOG(
          "video has been late behind media time for {:f} ms, should skip to "
          "next key frame",
          elapsedTimeMs);
    }
    return rv;
  }

  virtual bool IsBufferingAllowed() const { return true; }

 private:
  void DispatchDecodeTasksIfNeeded();
  void MaybeStartBuffering();

  // At the start of decoding we want to "preroll" the decode until we've
  // got a few frames decoded before we consider whether decode is falling
  // behind. Otherwise our "we're falling behind" logic will trigger
  // unnecessarily if we start playing as soon as the first sample is
  // decoded. These two fields store how many video frames and audio
  // samples we must consume before are considered to be finished prerolling.
  TimeUnit AudioPrerollThreshold() const {
    // A warm seek-resume uses a smaller, pref-tunable cushion (the pipeline is
    // primed, so a small amount restarts the clock promptly and refills in the
    // background); otherwise the normal cold-start cushion. Only the threshold
    // value differs, so the playback-rate scaling is applied once below.
    const TimeUnit threshold =
        mMaster->mStartSinkAfterWarmSeek
            ? TimeUnit::FromMicroseconds(
                  StaticPrefs::media_seek_resume_audio_preroll_usecs())
            : mMaster->mAmpleAudioThreshold / 2;
    return threshold.MultDouble(mMaster->mPlaybackRate);
  }

  uint32_t VideoPrerollFrames() const {
    if (mMaster->mStartSinkAfterWarmSeek) {
      // Warm seek-resume: a small, pref-tunable number of frames is enough; the
      // clock starts on the target frame and the queue refills in the
      // background. Software decoding is slower, so it uses a larger cushion
      // than hardware decoding to avoid stalling at resume.
      return mMaster->mReader->VideoIsHardwareAccelerated()
                 ? StaticPrefs::media_seek_resume_video_preroll_frames_hw()
                 : StaticPrefs::media_seek_resume_video_preroll_frames_sw();
    }
    uint32_t preroll = static_cast<uint32_t>(
        mMaster->GetAmpleVideoFrames() / 2. * mMaster->mPlaybackRate + 1);
    // Keep it under maximal queue size.
    mMaster->mReader->GetMaxVideoQueueSize().apply(
        [&preroll](const uint32_t& x) { preroll = std::min(preroll, x); });
    return preroll;
  }

  bool DonePrerollingAudio() const {
    return !mMaster->IsAudioDecoding() ||
           mMaster->GetDecodedAudioDuration() >= AudioPrerollThreshold();
  }

  bool DonePrerollingVideo() const {
    return !mMaster->IsVideoDecoding() ||
           static_cast<uint32_t>(mMaster->VideoQueue().GetSize()) >=
               VideoPrerollFrames();
  }

  void StartDormantTimer() {
    if (!mMaster->mMediaSeekable) {
      // Don't enter dormant if the media is not seekable because we need to
      // seek when exiting dormant.
      return;
    }

    auto timeout = StaticPrefs::media_dormant_on_pause_timeout_ms();
    if (timeout < 0) {
      // Disabled when timeout is negative.
      return;
    }

    if (timeout == 0) {
      // Enter dormant immediately without scheduling a timer.
      SetState<DormantState>();
      return;
    }

    if (mMaster->mMinimizePreroll) {
      SetState<DormantState>();
      return;
    }

    TimeStamp target =
        TimeStamp::Now() + TimeDuration::FromMilliseconds(timeout);

    mDormantTimer.Ensure(
        target,
        [this]() {
          AUTO_PROFILER_LABEL("DecodingState::StartDormantTimer:SetDormant",
                              MEDIA_PLAYBACK);
          mDormantTimer.CompleteRequest();
          SetState<DormantState>();
        },
        [this]() { mDormantTimer.CompleteRequest(); });
  }

  // Time at which we started decoding.
  TimeStamp mDecodeStartTime;

  // When we start decoding (either for the first time, or after a pause)
  // we may be low on decoded data. We don't want our "low data" logic to
  // kick in and decide that we're low on decoded data because the download
  // can't keep up with the decode, and cause us to pause playback. So we
  // have a "preroll" stage, where we ignore the results of our "low data"
  // logic during the first few frames of our decode. This occurs during
  // playback.
  bool mIsPrerolling = true;

  // Fired when playback is paused for a while to enter dormant.
  DelayedScheduler<TimeStamp> mDormantTimer;

  MediaEventListener mOnAudioPopped;
  MediaEventListener mOnVideoPopped;

  // If video has been later than the media time, this will records when the
  // video started being late. It will be reset once video catches up with the
  // media time.
  Maybe<TimeStamp> mVideoFirstLateTime;
};

/**
 * Purpose: decode audio data for playback when media is in seamless
 * looping, we will adjust media time to make samples time monotonically
 * increasing. All its methods runs on its owner thread (MDSM thread).
 *
 * Transition to:
 *   DORMANT if playback is paused for a while.
 *   SEEKING if any seek request.
 *   SHUTDOWN if any decode error.
 *   BUFFERING if playback can't continue due to lack of decoded data.
 *   COMPLETED when the media resource is closed and no data is available
 *             anymore.
 *   DECODING when media stops seamless looping.
 */
class MediaDecoderStateMachine::LoopingDecodingState
    : public MediaDecoderStateMachine::DecodingState {
 public:
  explicit LoopingDecodingState(Master* aPtr)
      : DecodingState(aPtr),
        mIsReachingAudioEOS(!mMaster->IsAudioDecoding()),
        mIsReachingVideoEOS(!mMaster->IsVideoDecoding()),
        mAudioEndedBeforeEnteringStateWithoutDuration(false),
        mVideoEndedBeforeEnteringStateWithoutDuration(false) {
    MOZ_ASSERT(mMaster->mLooping);
    SLOG(
        "LoopingDecodingState ctor, mIsReachingAudioEOS={}, "
        "mIsReachingVideoEOS={}",
        mIsReachingAudioEOS, mIsReachingVideoEOS);
    // If the track has reached EOS and we already have its last data, then we
    // can know its duration. But if playback starts from EOS (due to seeking),
    // the decoded end time would be zero because none of data gets decoded yet.
    if (mIsReachingAudioEOS) {
      if (mMaster->HasLastDecodedData(MediaData::Type::AUDIO_DATA) &&
          !mMaster->mAudioTrackDecodedDuration) {
        mMaster->mAudioTrackDecodedDuration.emplace(
            mMaster->mDecodedAudioEndTime);
        SLOG("determine mAudioTrackDecodedDuration");
      } else {
        mAudioEndedBeforeEnteringStateWithoutDuration = true;
        SLOG("still don't know mAudioTrackDecodedDuration");
      }
    }

    if (mIsReachingVideoEOS) {
      if (mMaster->HasLastDecodedData(MediaData::Type::VIDEO_DATA) &&
          !mMaster->mVideoTrackDecodedDuration) {
        mMaster->mVideoTrackDecodedDuration.emplace(
            mMaster->mDecodedVideoEndTime);
        SLOG("determine mVideoTrackDecodedDuration");
      } else {
        mVideoEndedBeforeEnteringStateWithoutDuration = true;
        SLOG("still don't know mVideoTrackDecodedDuration");
      }
    }

    // We might be able to determine the duration already, let's check.
    if (mIsReachingAudioEOS || mIsReachingVideoEOS) {
      (void)DetermineOriginalDecodedDurationIfNeeded();
    }

    // If we've looped at least once before, then we need to update queue offset
    // correctly to make the media data time and the clock time consistent.
    // Otherwise, it would cause a/v desync.
    if (mMaster->mOriginalDecodedDuration != media::TimeUnit::Zero()) {
      if (mIsReachingAudioEOS && mMaster->HasAudio()) {
        AudioQueue().SetOffset(AudioQueue().GetOffset() +
                               mMaster->mOriginalDecodedDuration);
      }
      if (mIsReachingVideoEOS && mMaster->HasVideo()) {
        VideoQueue().SetOffset(VideoQueue().GetOffset() +
                               mMaster->mOriginalDecodedDuration);
      }
    }
  }

  void Enter() {
    if (mMaster->HasAudio() && mIsReachingAudioEOS) {
      SLOG("audio has ended, request the data again.");
      RequestDataFromStartPosition(TrackInfo::TrackType::kAudioTrack);
    }
    if (mMaster->HasVideo() && mIsReachingVideoEOS) {
      SLOG("video has ended, request the data again.");
      RequestDataFromStartPosition(TrackInfo::TrackType::kVideoTrack);
    }
    DecodingState::Enter();
  }

  void Exit() override {
    MOZ_DIAGNOSTIC_ASSERT(mMaster->OnTaskQueue());
    SLOG(
        "Leaving looping state, offset [a={},v={}], endtime [a={},v={}], track "
        "duration [a={},v={}], waiting={}",
        AudioQueue().GetOffset().ToMicroseconds(),
        VideoQueue().GetOffset().ToMicroseconds(),
        mMaster->mDecodedAudioEndTime.ToMicroseconds(),
        mMaster->mDecodedVideoEndTime.ToMicroseconds(),
        mMaster->mAudioTrackDecodedDuration
            ? mMaster->mAudioTrackDecodedDuration->ToMicroseconds()
            : 0,
        mMaster->mVideoTrackDecodedDuration
            ? mMaster->mVideoTrackDecodedDuration->ToMicroseconds()
            : 0,
        mDataWaitingTimestampAdjustment
            ? MediaData::EnumValueToString(
                  mDataWaitingTimestampAdjustment->mType)
            : "none");
    if (ShouldDiscardLoopedData(MediaData::Type::AUDIO_DATA)) {
      DiscardLoopedData(MediaData::Type::AUDIO_DATA);
    }
    if (ShouldDiscardLoopedData(MediaData::Type::VIDEO_DATA)) {
      DiscardLoopedData(MediaData::Type::VIDEO_DATA);
    }

    if (mMaster->HasAudio() && HasDecodedLastAudioFrame()) {
      SLOG("Mark audio queue as finished");
      mMaster->mAudioDataRequest.DisconnectIfExists();
      mMaster->mAudioWaitRequest.DisconnectIfExists();
      AudioQueue().Finish();
    }
    if (mMaster->HasVideo() && HasDecodedLastVideoFrame()) {
      SLOG("Mark video queue as finished");
      mMaster->mVideoDataRequest.DisconnectIfExists();
      mMaster->mVideoWaitRequest.DisconnectIfExists();
      VideoQueue().Finish();
    }

    // Clear waiting data should be done after marking queue as finished.
    mDataWaitingTimestampAdjustment = nullptr;

    mAudioDataRequest.DisconnectIfExists();
    mVideoDataRequest.DisconnectIfExists();
    mAudioSeekRequest.DisconnectIfExists();
    mVideoSeekRequest.DisconnectIfExists();
    DecodingState::Exit();
  }

  ~LoopingDecodingState() {
    MOZ_DIAGNOSTIC_ASSERT(!mAudioDataRequest.Exists());
    MOZ_DIAGNOSTIC_ASSERT(!mVideoDataRequest.Exists());
    MOZ_DIAGNOSTIC_ASSERT(!mAudioSeekRequest.Exists());
    MOZ_DIAGNOSTIC_ASSERT(!mVideoSeekRequest.Exists());
  }

  State GetState() const override { return DECODER_STATE_LOOPING_DECODING; }

  void HandleAudioDecoded(AudioData* aAudio) override {
    // TODO : check if we need to update mOriginalDecodedDuration

    // After pushing data to the queue, timestamp might be adjusted.
    DecodingState::HandleAudioDecoded(aAudio);
    mMaster->mDecodedAudioEndTime =
        std::max(aAudio->GetEndTime(), mMaster->mDecodedAudioEndTime);
    SLOG("audio sample after time-adjustment [{},{}]",
         aAudio->mTime.ToMicroseconds(), aAudio->GetEndTime().ToMicroseconds());
  }

  void HandleVideoDecoded(VideoData* aVideo) override {
    // TODO : check if we need to update mOriginalDecodedDuration

    // Here sample still keeps its original timestamp.

    // This indicates there is a shorter audio track, and it's the first time in
    // the looping (audio ends but video is playing) so that we haven't been
    // able to determine the decoded duration. Therefore, we fill the gap
    // between two tracks before video ends. Afterward, this adjustment will be
    // done in `HandleEndOfAudio()`.
    if (mMaster->mOriginalDecodedDuration == media::TimeUnit::Zero() &&
        mMaster->mAudioTrackDecodedDuration &&
        aVideo->GetEndTime() > *mMaster->mAudioTrackDecodedDuration) {
      media::TimeUnit gap;
      // First time we fill gap between the video frame to the last audio.
      if (auto prevVideo = VideoQueue().PeekBack();
          prevVideo &&
          prevVideo->GetEndTime() < *mMaster->mAudioTrackDecodedDuration) {
        gap =
            aVideo->GetEndTime().ToBase(*mMaster->mAudioTrackDecodedDuration) -
            *mMaster->mAudioTrackDecodedDuration;
      }
      // Then fill the gap for all following videos.
      else {
        gap = aVideo->mDuration.ToBase(*mMaster->mAudioTrackDecodedDuration);
      }
      SLOG(
          "Longer video {}{} (audio-durtaion={}{}), insert silence to fill "
          "the gap {}{}",
          aVideo->GetEndTime().ToMicroseconds(),
          aVideo->GetEndTime().ToString().get(),
          mMaster->mAudioTrackDecodedDuration->ToMicroseconds(),
          mMaster->mAudioTrackDecodedDuration->ToString().get(),
          gap.ToMicroseconds(), gap.ToString().get());
      PushFakeAudioDataIfNeeded(gap);
    }

    // After pushing data to the queue, timestamp might be adjusted.
    DecodingState::HandleVideoDecoded(aVideo);
    mMaster->mDecodedVideoEndTime =
        std::max(aVideo->GetEndTime(), mMaster->mDecodedVideoEndTime);
    SLOG("video sample after time-adjustment [{},{}]",
         aVideo->mTime.ToMicroseconds(), aVideo->GetEndTime().ToMicroseconds());
  }

  void HandleEndOfAudio() override {
    mIsReachingAudioEOS = true;
    if (!mMaster->mAudioTrackDecodedDuration &&
        mMaster->HasLastDecodedData(MediaData::Type::AUDIO_DATA)) {
      mMaster->mAudioTrackDecodedDuration.emplace(
          mMaster->mDecodedAudioEndTime);
    }
    if (DetermineOriginalDecodedDurationIfNeeded()) {
      AudioQueue().SetOffset(AudioQueue().GetOffset() +
                             mMaster->mOriginalDecodedDuration);
    }

    // This indicates that the audio track is shorter than the video track, so
    // we need to add some silence to fill the gap.
    if (mMaster->mAudioTrackDecodedDuration &&
        mMaster->mOriginalDecodedDuration >
            *mMaster->mAudioTrackDecodedDuration) {
      MOZ_ASSERT(mMaster->HasVideo());
      MOZ_ASSERT(mMaster->mVideoTrackDecodedDuration);
      MOZ_ASSERT(mMaster->mOriginalDecodedDuration ==
                 *mMaster->mVideoTrackDecodedDuration);
      auto gap = mMaster->mOriginalDecodedDuration.ToBase(
                     *mMaster->mAudioTrackDecodedDuration) -
                 *mMaster->mAudioTrackDecodedDuration;
      SLOG(
          "Audio track is shorter than the original decoded duration "
          "(a={}{}, t={}{}), insert silence to fill the gap {}{}",
          mMaster->mAudioTrackDecodedDuration->ToMicroseconds(),
          mMaster->mAudioTrackDecodedDuration->ToString().get(),
          mMaster->mOriginalDecodedDuration.ToMicroseconds(),
          mMaster->mOriginalDecodedDuration.ToString().get(),
          gap.ToMicroseconds(), gap.ToString().get());
      PushFakeAudioDataIfNeeded(gap);
    }

    SLOG(
        "received audio EOS when seamless looping, starts seeking, "
        "audioLoopingOffset=[{}], mAudioTrackDecodedDuration=[{}]",
        AudioQueue().GetOffset().ToMicroseconds(),
        mMaster->mAudioTrackDecodedDuration
            ? mMaster->mAudioTrackDecodedDuration->ToMicroseconds()
            : 0);
    if (!IsRequestingDataFromStartPosition(MediaData::Type::AUDIO_DATA)) {
      RequestDataFromStartPosition(TrackInfo::TrackType::kAudioTrack);
    }
    ProcessSamplesWaitingAdjustmentIfAny();
  }

  void HandleEndOfVideo() override {
    mIsReachingVideoEOS = true;
    if (!mMaster->mVideoTrackDecodedDuration &&
        mMaster->HasLastDecodedData(MediaData::Type::VIDEO_DATA)) {
      mMaster->mVideoTrackDecodedDuration.emplace(
          mMaster->mDecodedVideoEndTime);
    }
    if (DetermineOriginalDecodedDurationIfNeeded()) {
      VideoQueue().SetOffset(VideoQueue().GetOffset() +
                             mMaster->mOriginalDecodedDuration);
    }

    SLOG(
        "received video EOS when seamless looping, starts seeking, "
        "videoLoopingOffset=[{}], mVideoTrackDecodedDuration=[{}]",
        VideoQueue().GetOffset().ToMicroseconds(),
        mMaster->mVideoTrackDecodedDuration
            ? mMaster->mVideoTrackDecodedDuration->ToMicroseconds()
            : 0);
    if (!IsRequestingDataFromStartPosition(MediaData::Type::VIDEO_DATA)) {
      RequestDataFromStartPosition(TrackInfo::TrackType::kVideoTrack);
    }
    ProcessSamplesWaitingAdjustmentIfAny();
  }

 private:
  void RequestDataFromStartPosition(TrackInfo::TrackType aType) {
    MOZ_DIAGNOSTIC_ASSERT(aType == TrackInfo::TrackType::kAudioTrack ||
                          aType == TrackInfo::TrackType::kVideoTrack);

    const bool isAudio = aType == TrackInfo::TrackType::kAudioTrack;
    MOZ_ASSERT_IF(isAudio, mMaster->HasAudio());
    MOZ_ASSERT_IF(!isAudio, mMaster->HasVideo());

    if (IsReaderSeeking()) {
      MOZ_ASSERT(!mPendingSeekingType);
      mPendingSeekingType = Some(aType);
      SLOG("Delay {} seeking until the reader finishes current seeking",
           isAudio ? "audio" : "video");
      return;
    }

    auto& seekRequest = isAudio ? mAudioSeekRequest : mVideoSeekRequest;
    Reader()->ResetDecode(aType);
    Reader()
        ->Seek(SeekTarget(media::TimeUnit::Zero(), SeekTarget::Type::Accurate,
                          isAudio ? SeekTarget::Track::AudioOnly
                                  : SeekTarget::Track::VideoOnly))
        ->Then(
            OwnerThread(), __func__,
            [this, isAudio, master = RefPtr{mMaster}]() mutable -> void {
              AUTO_PROFILER_LABEL(
                  isAudio ? "LoopingDecodingState::"
                            "RequestDataFromStartPosition(audio)::SeekResolved"
                          : "LoopingDecodingState::"
                            "RequestDataFromStartPosition(video)::SeekResolved",
                  MEDIA_PLAYBACK);
              if (auto& state = master->mStateObj;
                  state &&
                  state->GetState() != DECODER_STATE_LOOPING_DECODING) {
                MOZ_RELEASE_ASSERT(false, "This shouldn't happen!");
                return;
              }
              if (isAudio) {
                mAudioSeekRequest.Complete();
              } else {
                mVideoSeekRequest.Complete();
              }
              SLOG(
                  "seeking completed, start to request first {} sample "
                  "(queued={}, decoder-queued={})",
                  isAudio ? "audio" : "video",
                  isAudio ? AudioQueue().GetSize() : VideoQueue().GetSize(),
                  isAudio ? Reader()->SizeOfAudioQueueInFrames()
                          : Reader()->SizeOfVideoQueueInFrames());
              if (isAudio) {
                RequestAudioDataFromReaderAfterEOS();
              } else {
                RequestVideoDataFromReaderAfterEOS();
              }
              if (mPendingSeekingType) {
                auto seekingType = *mPendingSeekingType;
                mPendingSeekingType.reset();
                SLOG("Perform pending {} seeking", TrackTypeToStr(seekingType));
                RequestDataFromStartPosition(seekingType);
              }
            },
            [this, isAudio, master = RefPtr{mMaster}](
                const SeekRejectValue& aReject) mutable -> void {
              AUTO_PROFILER_LABEL(
                  isAudio ? "LoopingDecodingState::"
                            "RequestDataFromStartPosition(audio)::SeekRejected"
                          : "LoopingDecodingState::"
                            "RequestDataFromStartPosition(video)::SeekRejected",
                  MEDIA_PLAYBACK);
              if (auto& state = master->mStateObj;
                  state &&
                  state->GetState() != DECODER_STATE_LOOPING_DECODING) {
                MOZ_RELEASE_ASSERT(false, "This shouldn't happen!");
                return;
              }
              if (isAudio) {
                mAudioSeekRequest.Complete();
              } else {
                mVideoSeekRequest.Complete();
              }
              HandleError(aReject.mError, isAudio);
            })
        ->Track(seekRequest);
  }

  void RequestAudioDataFromReaderAfterEOS() {
    MOZ_ASSERT(mMaster->HasAudio());
    Reader()
        ->RequestAudioData()
        ->Then(
            OwnerThread(), __func__,
            [this, master = RefPtr{mMaster}](const RefPtr<AudioData>& aAudio) {
              AUTO_PROFILER_LABEL(
                  "LoopingDecodingState::"
                  "RequestAudioDataFromReader::"
                  "RequestDataResolved",
                  MEDIA_PLAYBACK);
              if (auto& state = master->mStateObj;
                  state &&
                  state->GetState() != DECODER_STATE_LOOPING_DECODING) {
                MOZ_RELEASE_ASSERT(false, "This shouldn't happen!");
                return;
              }
              mIsReachingAudioEOS = false;
              mAudioDataRequest.Complete();
              SLOG(
                  "got audio decoded sample "
                  "[{},{}]",
                  aAudio->mTime.ToMicroseconds(),
                  aAudio->GetEndTime().ToMicroseconds());
              if (ShouldPutDataOnWaiting(MediaData::Type::AUDIO_DATA)) {
                SLOG(
                    "decoded audio sample needs to wait for timestamp "
                    "adjustment after EOS");
                PutDataOnWaiting(aAudio);
                return;
              }
              HandleAudioDecoded(aAudio);
              ProcessSamplesWaitingAdjustmentIfAny();
            },
            [this, master = RefPtr{mMaster}](const MediaResult& aError) {
              AUTO_PROFILER_LABEL(
                  "LoopingDecodingState::"
                  "RequestAudioDataFromReader::"
                  "RequestDataRejected",
                  MEDIA_PLAYBACK);
              if (auto& state = master->mStateObj;
                  state &&
                  state->GetState() != DECODER_STATE_LOOPING_DECODING) {
                MOZ_RELEASE_ASSERT(false, "This shouldn't happen!");
                return;
              }
              mAudioDataRequest.Complete();
              HandleError(aError, true /* isAudio */);
            })
        ->Track(mAudioDataRequest);
  }

  void RequestVideoDataFromReaderAfterEOS() {
    MOZ_ASSERT(mMaster->HasVideo());
    Reader()
        ->RequestVideoData(media::TimeUnit(),
                           false /* aRequestNextVideoKeyFrame */)
        ->Then(
            OwnerThread(), __func__,
            [this, master = RefPtr{mMaster}](const RefPtr<VideoData>& aVideo) {
              AUTO_PROFILER_LABEL(
                  "LoopingDecodingState::"
                  "RequestVideoDataFromReaderAfterEOS()::"
                  "RequestDataResolved",
                  MEDIA_PLAYBACK);
              if (auto& state = master->mStateObj;
                  state &&
                  state->GetState() != DECODER_STATE_LOOPING_DECODING) {
                MOZ_RELEASE_ASSERT(false, "This shouldn't happen!");
                return;
              }
              mIsReachingVideoEOS = false;
              mVideoDataRequest.Complete();
              SLOG(
                  "got video decoded sample "
                  "[{},{}]",
                  aVideo->mTime.ToMicroseconds(),
                  aVideo->GetEndTime().ToMicroseconds());
              if (ShouldPutDataOnWaiting(MediaData::Type::VIDEO_DATA)) {
                SLOG(
                    "decoded video sample needs to wait for timestamp "
                    "adjustment after EOS");
                PutDataOnWaiting(aVideo);
                return;
              }
              mMaster->mBypassingSkipToNextKeyFrameCheck = true;
              HandleVideoDecoded(aVideo);
              ProcessSamplesWaitingAdjustmentIfAny();
            },
            [this, master = RefPtr{mMaster}](const MediaResult& aError) {
              AUTO_PROFILER_LABEL(
                  "LoopingDecodingState::"
                  "RequestVideoDataFromReaderAfterEOS()::"
                  "RequestDataRejected",
                  MEDIA_PLAYBACK);
              if (auto& state = master->mStateObj;
                  state &&
                  state->GetState() != DECODER_STATE_LOOPING_DECODING) {
                MOZ_RELEASE_ASSERT(false, "This shouldn't happen!");
                return;
              }
              mVideoDataRequest.Complete();
              HandleError(aError, false /* isAudio */);
            })
        ->Track(mVideoDataRequest);
  }

  void HandleError(const MediaResult& aError, bool aIsAudio);

  bool ShouldRequestData(MediaData::Type aType) const {
    MOZ_DIAGNOSTIC_ASSERT(aType == MediaData::Type::AUDIO_DATA ||
                          aType == MediaData::Type::VIDEO_DATA);

    if (aType == MediaData::Type::AUDIO_DATA &&
        (mAudioSeekRequest.Exists() || mAudioDataRequest.Exists() ||
         IsDataWaitingForTimestampAdjustment(MediaData::Type::AUDIO_DATA) ||
         mMaster->IsWaitingAudioData())) {
      return false;
    }
    if (aType == MediaData::Type::VIDEO_DATA &&
        (mVideoSeekRequest.Exists() || mVideoDataRequest.Exists() ||
         IsDataWaitingForTimestampAdjustment(MediaData::Type::VIDEO_DATA) ||
         mMaster->IsWaitingVideoData())) {
      return false;
    }
    return true;
  }

  void HandleAudioCanceled() override {
    if (ShouldRequestData(MediaData::Type::AUDIO_DATA)) {
      mMaster->RequestAudioData();
    }
  }

  void HandleAudioWaited(MediaData::Type aType) override {
    if (ShouldRequestData(MediaData::Type::AUDIO_DATA)) {
      mMaster->RequestAudioData();
    }
  }

  void HandleVideoCanceled() override {
    if (ShouldRequestData(MediaData::Type::VIDEO_DATA)) {
      mMaster->RequestVideoData(mMaster->GetMediaTime(),
                                ShouldRequestNextKeyFrame());
    };
  }

  void HandleVideoWaited(MediaData::Type aType) override {
    if (ShouldRequestData(MediaData::Type::VIDEO_DATA)) {
      mMaster->RequestVideoData(mMaster->GetMediaTime(),
                                ShouldRequestNextKeyFrame());
    };
  }

  void EnsureAudioDecodeTaskQueued() override {
    if (!ShouldRequestData(MediaData::Type::AUDIO_DATA)) {
      return;
    }
    DecodingState::EnsureAudioDecodeTaskQueued();
  }

  void EnsureVideoDecodeTaskQueued() override {
    if (!ShouldRequestData(MediaData::Type::VIDEO_DATA)) {
      return;
    }
    DecodingState::EnsureVideoDecodeTaskQueued();
  }

  bool DetermineOriginalDecodedDurationIfNeeded() {
    // Duration would only need to be set once, unless we get more data which is
    // larger than the duration. That can happen on MSE (reopen stream).
    if (mMaster->mOriginalDecodedDuration != media::TimeUnit::Zero()) {
      return true;
    }

    // Single track situations
    if (mMaster->HasAudio() && !mMaster->HasVideo() &&
        mMaster->mAudioTrackDecodedDuration) {
      mMaster->mOriginalDecodedDuration = *mMaster->mAudioTrackDecodedDuration;
      SLOG("audio only, duration={}",
           mMaster->mOriginalDecodedDuration.ToMicroseconds());
      return true;
    }
    if (mMaster->HasVideo() && !mMaster->HasAudio() &&
        mMaster->mVideoTrackDecodedDuration) {
      mMaster->mOriginalDecodedDuration = *mMaster->mVideoTrackDecodedDuration;
      SLOG("video only, duration={}",
           mMaster->mOriginalDecodedDuration.ToMicroseconds());
      return true;
    }
    // Two tracks situation
    if (mMaster->HasAudio() && mMaster->HasVideo()) {
      // Both tracks have ended so that we can check which track is longer.
      if (mMaster->mAudioTrackDecodedDuration &&
          mMaster->mVideoTrackDecodedDuration) {
        mMaster->mOriginalDecodedDuration =
            std::max(*mMaster->mVideoTrackDecodedDuration,
                     *mMaster->mAudioTrackDecodedDuration);
        SLOG("Both tracks ended, original duration={} (a={}, v={})",
             mMaster->mOriginalDecodedDuration.ToMicroseconds(),
             mMaster->mAudioTrackDecodedDuration->ToMicroseconds(),
             mMaster->mVideoTrackDecodedDuration->ToMicroseconds());
        return true;
      }
      // When entering the state, video has ended but audio hasn't, which means
      // audio is longer.
      if (mMaster->mAudioTrackDecodedDuration &&
          mVideoEndedBeforeEnteringStateWithoutDuration) {
        mMaster->mOriginalDecodedDuration =
            *mMaster->mAudioTrackDecodedDuration;
        mVideoEndedBeforeEnteringStateWithoutDuration = false;
        SLOG("audio is longer, duration={}",
             mMaster->mOriginalDecodedDuration.ToMicroseconds());
        return true;
      }
      // When entering the state, audio has ended but video hasn't, which means
      // video is longer.
      if (mMaster->mVideoTrackDecodedDuration &&
          mAudioEndedBeforeEnteringStateWithoutDuration) {
        mMaster->mOriginalDecodedDuration =
            *mMaster->mVideoTrackDecodedDuration;
        mAudioEndedBeforeEnteringStateWithoutDuration = false;
        SLOG("video is longer, duration={}",
             mMaster->mOriginalDecodedDuration.ToMicroseconds());
        return true;
      }
      SLOG("Still waiting for another track ends...");
      MOZ_ASSERT(!mMaster->mAudioTrackDecodedDuration ||
                 !mMaster->mVideoTrackDecodedDuration);
    }
    SLOG("can't determine the original decoded duration yet");
    MOZ_ASSERT(mMaster->mOriginalDecodedDuration == media::TimeUnit::Zero());
    return false;
  }

  void ProcessSamplesWaitingAdjustmentIfAny() {
    if (!mDataWaitingTimestampAdjustment) {
      return;
    }

    RefPtr<MediaData> data = mDataWaitingTimestampAdjustment;
    mDataWaitingTimestampAdjustment = nullptr;
    const bool isAudio = data->mType == MediaData::Type::AUDIO_DATA;
    SLOG("process {} sample waiting for timestamp adjustment",
         isAudio ? "audio" : "video");
    if (isAudio) {
      // Waiting sample is for next round of looping, so the queue offset
      // shouldn't be zero. This happens when the track has reached EOS before
      // entering the state (and looping never happens before). Same for below
      // video case.
      if (AudioQueue().GetOffset() == media::TimeUnit::Zero()) {
        AudioQueue().SetOffset(mMaster->mOriginalDecodedDuration);
      }
      HandleAudioDecoded(data->As<AudioData>());
    } else {
      MOZ_DIAGNOSTIC_ASSERT(data->mType == MediaData::Type::VIDEO_DATA);
      if (VideoQueue().GetOffset() == media::TimeUnit::Zero()) {
        VideoQueue().SetOffset(mMaster->mOriginalDecodedDuration);
      }
      HandleVideoDecoded(data->As<VideoData>());
    }
  }

  bool IsDataWaitingForTimestampAdjustment(MediaData::Type aType) const {
    return mDataWaitingTimestampAdjustment &&
           mDataWaitingTimestampAdjustment->mType == aType;
  }

  bool ShouldPutDataOnWaiting(MediaData::Type aType) const {
    // If another track is already waiting, this track shouldn't be waiting.
    // This case only happens when both tracks reached EOS before entering the
    // looping decoding state, so we don't know the decoded duration yet (used
    // to adjust timestamp) But this is fine, because both tracks will start
    // from 0 so we don't need to adjust them now.
    if (mDataWaitingTimestampAdjustment &&
        !IsDataWaitingForTimestampAdjustment(aType)) {
      return false;
    }

    // Only have one track, no need to wait.
    if ((aType == MediaData::Type::AUDIO_DATA && !mMaster->HasVideo()) ||
        (aType == MediaData::Type::VIDEO_DATA && !mMaster->HasAudio())) {
      return false;
    }

    // We don't know the duration yet, so we can't calculate the looping offset.
    return mMaster->mOriginalDecodedDuration == media::TimeUnit::Zero();
  }

  void PutDataOnWaiting(MediaData* aData) {
    MOZ_ASSERT(!mDataWaitingTimestampAdjustment);
    mDataWaitingTimestampAdjustment = aData;
    SLOG("put {} [{},{}] on waiting",
         MediaData::EnumValueToString(aData->mType),
         aData->mTime.ToMicroseconds(), aData->GetEndTime().ToMicroseconds());
    MaybeStopPrerolling();
  }

  bool ShouldDiscardLoopedData(MediaData::Type aType) const {
    if (!mMaster->mMediaSink->IsStarted()) {
      return false;
    }

    MOZ_DIAGNOSTIC_ASSERT(aType == MediaData::Type::AUDIO_DATA ||
                          aType == MediaData::Type::VIDEO_DATA);
    const bool isAudio = aType == MediaData::Type::AUDIO_DATA;
    if (isAudio && !mMaster->HasAudio()) {
      return false;
    }
    if (!isAudio && !mMaster->HasVideo()) {
      return false;
    }

    /**
     * If media cancels looping, we should check whether there is media data
     * whose time is later than EOS. If so, we should discard them because we
     * won't have a chance to play them.
     *
     *    playback                     last decoded
     *    position          EOS        data time
     *   ----|---------------|------------|---------> (Increasing timeline)
     *    mCurrent         looping      mMaster's
     *    ClockTime        offset      mDecodedXXXEndTime
     *
     */
    const auto offset =
        isAudio ? AudioQueue().GetOffset() : VideoQueue().GetOffset();
    const auto endTime =
        isAudio ? mMaster->mDecodedAudioEndTime : mMaster->mDecodedVideoEndTime;
    const auto clockTime = mMaster->GetClock();
    return (offset != media::TimeUnit::Zero() && clockTime < offset &&
            offset < endTime);
  }

  void DiscardLoopedData(MediaData::Type aType) {
    MOZ_DIAGNOSTIC_ASSERT(aType == MediaData::Type::AUDIO_DATA ||
                          aType == MediaData::Type::VIDEO_DATA);
    const bool isAudio = aType == MediaData::Type::AUDIO_DATA;
    const auto offset =
        isAudio ? AudioQueue().GetOffset() : VideoQueue().GetOffset();
    if (offset == media::TimeUnit::Zero()) {
      return;
    }

    SLOG("Discard {} frames after the time={}", isAudio ? "audio" : "video",
         offset.ToMicroseconds());
    if (isAudio) {
      DiscardFramesFromTail(AudioQueue(), [&](int64_t aSampleTime) {
        return aSampleTime > offset.ToMicroseconds();
      });
    } else {
      DiscardFramesFromTail(VideoQueue(), [&](int64_t aSampleTime) {
        return aSampleTime > offset.ToMicroseconds();
      });
    }
  }

  void PushFakeAudioDataIfNeeded(const media::TimeUnit& aDuration) {
    MOZ_ASSERT(Info().HasAudio());

    const auto& audioInfo = Info().mAudio;
    CheckedInt64 frames = aDuration.ToTicksAtRate(audioInfo.mRate);
    if (!frames.isValid() || !audioInfo.mChannels || !audioInfo.mRate) {
      NS_WARNING("Can't create fake audio, invalid frames/channel/rate?");
      return;
    }

    if (!frames.value()) {
      NS_WARNING(nsPrintfCString("Duration (%s) too short, no frame needed",
                                 aDuration.ToString().get())
                     .get());
      return;
    }

    // If we can get the last sample, use its frame. Otherwise, use common 1024.
    int64_t typicalPacketFrameCount = 1024;
    if (RefPtr<AudioData> audio = AudioQueue().PeekBack();
        audio && audio->Frames()) {
      typicalPacketFrameCount = audio->Frames();
    }

    media::TimeUnit totalDuration = TimeUnit::Zero(audioInfo.mRate);
    // Generate fake audio in a smaller size of audio chunk.
    while (frames.value()) {
      int64_t packetFrameCount =
          std::min(frames.value(), typicalPacketFrameCount);
      frames -= packetFrameCount;
      AlignedAudioBuffer samples(packetFrameCount * audioInfo.mChannels);
      if (!samples) {
        NS_WARNING("Can't create audio buffer, OOM?");
        return;
      }
      // `mDecodedAudioEndTime` is adjusted time, and we want unadjusted time
      // otherwise the time would be adjusted twice when pushing sample into the
      // media queue.
      media::TimeUnit startTime = mMaster->mDecodedAudioEndTime;
      if (AudioQueue().GetOffset() != media::TimeUnit::Zero()) {
        startTime -= AudioQueue().GetOffset();
      }
      RefPtr<AudioData> data(new AudioData(0, startTime, std::move(samples),
                                           audioInfo.mChannels,
                                           audioInfo.mRate));
      SLOG("Created fake audio data (duration={}, frame-left={})",
           data->mDuration.ToString().get(), frames.value());
      totalDuration += data->mDuration;
      HandleAudioDecoded(data);
    }
    SLOG("Pushed fake silence audio data in total duration={}{}",
         totalDuration.ToMicroseconds(), totalDuration.ToString().get());
  }

  bool HasDecodedLastAudioFrame() const {
    // when we're going to leave looping state and have got EOS before, we
    // should mark audio queue as ended because we have got all data we need.
    return mAudioDataRequest.Exists() || mAudioSeekRequest.Exists() ||
           ShouldDiscardLoopedData(MediaData::Type::AUDIO_DATA) ||
           IsDataWaitingForTimestampAdjustment(MediaData::Type::AUDIO_DATA) ||
           mIsReachingAudioEOS;
  }

  bool HasDecodedLastVideoFrame() const {
    // when we're going to leave looping state and have got EOS before, we
    // should mark video queue as ended because we have got all data we need.
    return mVideoDataRequest.Exists() || mVideoSeekRequest.Exists() ||
           ShouldDiscardLoopedData(MediaData::Type::VIDEO_DATA) ||
           IsDataWaitingForTimestampAdjustment(MediaData::Type::VIDEO_DATA) ||
           mIsReachingVideoEOS;
  }

  bool ShouldStopPrerolling() const override {
    // These checks is used to handle the media queue aren't opened correctly
    // because they've been close before entering the looping state. Therefore,
    // we need to preroll data in order to let new data to reopen the queue
    // automatically. Otherwise, playback can't start successfully.
    bool isWaitingForNewData = false;
    if (mMaster->HasAudio()) {
      isWaitingForNewData |= (mIsReachingAudioEOS && AudioQueue().IsFinished());
    }
    if (mMaster->HasVideo()) {
      isWaitingForNewData |= (mIsReachingVideoEOS && VideoQueue().IsFinished());
    }
    return !isWaitingForNewData && DecodingState::ShouldStopPrerolling();
  }

  bool IsReaderSeeking() const {
    return mAudioSeekRequest.Exists() || mVideoSeekRequest.Exists();
  }

  bool IsWaitingData(MediaData::Type aType) const override {
    if (aType == MediaData::Type::AUDIO_DATA) {
      return mMaster->IsWaitingAudioData() ||
             IsDataWaitingForTimestampAdjustment(MediaData::Type::AUDIO_DATA);
    }
    MOZ_DIAGNOSTIC_ASSERT(aType == MediaData::Type::VIDEO_DATA);
    return mMaster->IsWaitingVideoData() ||
           IsDataWaitingForTimestampAdjustment(MediaData::Type::VIDEO_DATA);
  }

  bool IsRequestingDataFromStartPosition(MediaData::Type aType) const {
    MOZ_DIAGNOSTIC_ASSERT(aType == MediaData::Type::AUDIO_DATA ||
                          aType == MediaData::Type::VIDEO_DATA);
    if (aType == MediaData::Type::AUDIO_DATA) {
      return mAudioSeekRequest.Exists() || mAudioDataRequest.Exists();
    }
    return mVideoSeekRequest.Exists() || mVideoDataRequest.Exists();
  }

  bool IsBufferingAllowed() const override {
    return !mIsReachingAudioEOS && !mIsReachingVideoEOS;
  }

  bool mIsReachingAudioEOS;
  bool mIsReachingVideoEOS;

  /**
   * If we have both tracks which have different length, when one track ends
   * first, we can't adjust new data from that track if another longer track
   * hasn't ended yet. The adjusted timestamp needs to be based off the longer
   * track's last data's timestamp, because otherwise it would cause a deviation
   * and eventually a/v unsync. Those sample needs to be stored and we will
   * adjust their timestamp later.
   *
   * Following graph explains the situation in details.
   * o : decoded data with timestamp adjusted or no adjustment (not looping yet)
   * x : decoded data without timestamp adjustment.
   * - : stop decoding and nothing happens
   * EOS : the track reaches to the end. We now know the offset of the track.
   *
   * Timeline ----------------------------------->
   * Track1 :  o EOS x  -  -  o
   * Track2 :  o  o  o EOS o  o
   *
   * Before reaching track2's EOS, we can't adjust samples from track1 because
   * track2 might have longer duration than track1. The sample X would be
   * stored in `mDataWaitingTimestampAdjustment` and we would also stop decoding
   * for track1.
   *
   * After reaching track2's EOS, now we know another track's offset, and the
   * larger one would be used for `mOriginalDecodedDuration`. Once that duration
   * has been determined, we will no longer need to put samples on waiting
   * because we already know how to adjust timestamp.
   */
  RefPtr<MediaData> mDataWaitingTimestampAdjustment;

  MozPromiseRequestHolder<MediaFormatReader::SeekPromise> mAudioSeekRequest;
  MozPromiseRequestHolder<MediaFormatReader::SeekPromise> mVideoSeekRequest;
  MozPromiseRequestHolder<AudioDataPromise> mAudioDataRequest;
  MozPromiseRequestHolder<VideoDataPromise> mVideoDataRequest;

  // The media format reader only allows seeking a track at a time, if we're
  // already in seeking, then delay the new seek until the current one finishes.
  Maybe<TrackInfo::TrackType> mPendingSeekingType;

  // These are used to track a special case where the playback starts from EOS
  // position via seeking. So even if EOS has reached, none of data has been
  // decoded yet. They will be reset when `mOriginalDecodedDuration` is
  // determined.
  bool mAudioEndedBeforeEnteringStateWithoutDuration;
  bool mVideoEndedBeforeEnteringStateWithoutDuration;
};

/**
 * Purpose: seek to a particular new playback position.
 *
 * Transition to:
 *   SEEKING if any new seek request.
 *   SHUTDOWN if seek failed.
 *   COMPLETED if the new playback position is the end of the media resource.
 *   NextFrameSeekingState if completing a NextFrameSeekingFromDormantState.
 *   DECODING/LOOPING_DECODING otherwise.
 */
class MediaDecoderStateMachine::SeekingState
    : public MediaDecoderStateMachine::StateObject {
 public:
  explicit SeekingState(Master* aPtr)
      : StateObject(aPtr), mVisibility(static_cast<EventVisibility>(0)) {}

  RefPtr<MediaDecoder::SeekPromise> Enter(SeekJob&& aSeekJob,
                                          EventVisibility aVisibility) {
    mSeekJob = std::move(aSeekJob);
    mVisibility = aVisibility;

    // Suppressed visibility comes from two cases: (1) leaving dormant state,
    // and (2) resuming suspended video decoder. We want both cases to be
    // transparent to the user. So we only notify the change when the seek
    // request is from the user.
    if (mVisibility == EventVisibility::Observable) {
      // Don't stop playback for a video-only seek since we want to keep playing
      // audio and we don't need to stop playback while leaving dormant for the
      // playback should has been stopped.
      mMaster->StopPlayback(MediaSink::StopReason::Seeking);
      mMaster->UpdatePlaybackPositionInternal(mSeekJob.mTarget->GetTime());
      mMaster->mOnPlaybackEvent.Notify(MediaPlaybackEvent::SeekStarted);
      mMaster->mOnNextFrameStatus.Notify(
          MediaDecoderOwner::NEXT_FRAME_UNAVAILABLE_SEEKING);
    }

    RefPtr<MediaDecoder::SeekPromise> p = mSeekJob.mPromise.Ensure(__func__);

    DoSeek();

    return p;
  }

  virtual void Exit() override = 0;

  State GetState() const override = 0;

  void HandleAudioDecoded(AudioData* aAudio) override = 0;
  void HandleVideoDecoded(VideoData* aVideo) override = 0;
  void HandleAudioWaited(MediaData::Type aType) override = 0;
  void HandleVideoWaited(MediaData::Type aType) override = 0;

  void HandleVideoSuspendTimeout() override {
    // Do nothing since we want a valid video frame to show when seek is done.
  }

  void HandleResumeVideoDecoding(const TimeUnit&) override {
    // Do nothing. We will resume video decoding in the decoding state.
  }

  // We specially handle next frame seeks by ignoring them if we're already
  // seeking.
  RefPtr<MediaDecoder::SeekPromise> HandleSeek(
      const SeekTarget& aTarget) override {
    if (aTarget.IsNextFrame()) {
      // We ignore next frame seeks if we already have a seek pending
      SLOG("Already SEEKING, ignoring seekToNextFrame");
      MOZ_ASSERT(!mSeekJob.mPromise.IsEmpty(), "Seek shouldn't be finished");
      return MediaDecoder::SeekPromise::CreateAndReject(
          /* aRejectValue = */ true, __func__);
    }

    return StateObject::HandleSeek(aTarget);
  }

 protected:
  SeekJob mSeekJob;
  EventVisibility mVisibility;

  virtual void DoSeek() = 0;
  // Transition to the next state (defined by the subclass) when seek is
  // completed.
  virtual void GoToNextState() { SetDecodingState(); }
  void SeekCompleted();
  virtual TimeUnit CalculateNewCurrentTime() const = 0;
};

class MediaDecoderStateMachine::AccurateSeekingState
    : public MediaDecoderStateMachine::SeekingState {
 public:
  explicit AccurateSeekingState(Master* aPtr) : SeekingState(aPtr) {}

  State GetState() const override { return DECODER_STATE_SEEKING_ACCURATE; }

  RefPtr<MediaDecoder::SeekPromise> Enter(SeekJob&& aSeekJob,
                                          EventVisibility aVisibility) {
    MOZ_ASSERT(aSeekJob.mTarget->IsAccurate() || aSeekJob.mTarget->IsFast());
    mCurrentTimeBeforeSeek = mMaster->GetMediaTime();
    return SeekingState::Enter(std::move(aSeekJob), aVisibility);
  }

  void Exit() override {
    // Disconnect MediaDecoder.
    mSeekJob.RejectIfExists(__func__);

    // Disconnect ReaderProxy.
    mSeekRequest.DisconnectIfExists();

    mWaitRequest.DisconnectIfExists();
  }

  void HandleAudioDecoded(AudioData* aAudio) override {
    MOZ_ASSERT(!mDoneAudioSeeking || !mDoneVideoSeeking,
               "Seek shouldn't be finished");
    MOZ_ASSERT(aAudio);

    AdjustFastSeekIfNeeded(aAudio);

    if (mSeekJob.mTarget->IsFast()) {
      // Non-precise seek; we can stop the seek at the first sample.
      mMaster->PushAudio(aAudio);
      mDoneAudioSeeking = true;
    } else {
      nsresult rv = DropAudioUpToSeekTarget(aAudio);
      if (NS_FAILED(rv)) {
        mMaster->DecodeError(rv);
        return;
      }
    }

    if (!mDoneAudioSeeking) {
      RequestAudioData();
      return;
    }
    MaybeFinishSeek();
  }

  void HandleVideoDecoded(VideoData* aVideo) override {
    MOZ_ASSERT(!mDoneAudioSeeking || !mDoneVideoSeeking,
               "Seek shouldn't be finished");
    MOZ_ASSERT(aVideo);

    AdjustFastSeekIfNeeded(aVideo);

    if (mSeekJob.mTarget->IsFast()) {
      // Non-precise seek. We can stop the seek at the first sample.
      mMaster->PushVideo(aVideo);
      mDoneVideoSeeking = true;
    } else {
      nsresult rv = DropVideoUpToSeekTarget(aVideo);
      if (NS_FAILED(rv)) {
        mMaster->DecodeError(rv);
        return;
      }
    }

    if (!mDoneVideoSeeking) {
      RequestVideoData();
      return;
    }
    MaybeFinishSeek();
  }

  void HandleWaitingForAudio() override {
    MOZ_ASSERT(!mDoneAudioSeeking);
    mMaster->WaitForData(MediaData::Type::AUDIO_DATA);
  }

  void HandleAudioCanceled() override {
    MOZ_ASSERT(!mDoneAudioSeeking);
    RequestAudioData();
  }

  void HandleEndOfAudio() override {
    HandleEndOfAudioInternal();
    MaybeFinishSeek();
  }

  void HandleWaitingForVideo() override {
    MOZ_ASSERT(!mDoneVideoSeeking);
    mMaster->WaitForData(MediaData::Type::VIDEO_DATA);
  }

  void HandleVideoCanceled() override {
    MOZ_ASSERT(!mDoneVideoSeeking);
    RequestVideoData();
  }

  void HandleEndOfVideo() override {
    HandleEndOfVideoInternal();
    MaybeFinishSeek();
  }

  void HandleAudioWaited(MediaData::Type aType) override {
    MOZ_ASSERT(!mDoneAudioSeeking || !mDoneVideoSeeking,
               "Seek shouldn't be finished");
    if (mSeekRequest.Exists()) {
      return;
    }
    RequestAudioData();
  }

  void HandleVideoWaited(MediaData::Type aType) override {
    MOZ_ASSERT(!mDoneAudioSeeking || !mDoneVideoSeeking,
               "Seek shouldn't be finished");
    if (mSeekRequest.Exists()) {
      return;
    }
    RequestVideoData();
  }

  void DoSeek() override {
    mDoneAudioSeeking = !Info().HasAudio();
    mDoneVideoSeeking = !Info().HasVideo();

    // Resetting decode should be called after stopping media sink, which can
    // ensure that we have an empty media queue before seeking the demuxer.
    // Only observable seeks paused playback for reuse, so only they stop as a
    // seek; a suppressed seek keeps playing and stops normally.
    if (mVisibility == EventVisibility::Observable) {
      mMaster->StopMediaSink(MediaSink::StopReason::Seeking);
    } else {
      mMaster->StopMediaSink();
    }
    mMaster->ResetDecode();

    DemuxerSeek();
  }

  TimeUnit CalculateNewCurrentTime() const override {
    const auto seekTime = mSeekJob.mTarget->GetTime();

    // For the accurate seek, we always set the newCurrentTime = seekTime so
    // that the updated HTMLMediaElement.currentTime will always be the seek
    // target; we rely on the MediaSink to handles the gap between the
    // newCurrentTime and the real decoded samples' start time.
    if (mSeekJob.mTarget->IsAccurate()) {
      return seekTime;
    }

    // For the fast seek, we update the newCurrentTime with the decoded audio
    // and video samples, set it to be the one which is closet to the seekTime.
    if (mSeekJob.mTarget->IsFast()) {
      RefPtr<AudioData> audio = AudioQueue().PeekFront();
      RefPtr<VideoData> video = VideoQueue().PeekFront();

      // A situation that both audio and video approaches the end.
      if (!audio && !video) {
        return seekTime;
      }

      const int64_t audioStart =
          audio ? audio->mTime.ToMicroseconds() : INT64_MAX;
      const int64_t videoStart =
          video ? video->mTime.ToMicroseconds() : INT64_MAX;
      const int64_t audioGap = std::abs(audioStart - seekTime.ToMicroseconds());
      const int64_t videoGap = std::abs(videoStart - seekTime.ToMicroseconds());
      return TimeUnit::FromMicroseconds(audioGap <= videoGap ? audioStart
                                                             : videoStart);
    }

    MOZ_ASSERT(false, "AccurateSeekTask doesn't handle other seek types.");
    return TimeUnit::Zero();
  }

 protected:
  void DemuxerSeek() {
    // Request the demuxer to perform seek.
    Reader()
        ->Seek(mSeekJob.mTarget.ref())
        ->Then(
            OwnerThread(), __func__,
            [this](const media::TimeUnit& aUnit) { OnSeekResolved(aUnit); },
            [this](const SeekRejectValue& aReject) { OnSeekRejected(aReject); })
        ->Track(mSeekRequest);
  }

  void OnSeekResolved(media::TimeUnit) {
    AUTO_PROFILER_LABEL("AccurateSeekingState::OnSeekResolved", MEDIA_PLAYBACK);
    mSeekRequest.Complete();

    // We must decode the first samples of active streams, so we can determine
    // the new stream time. So dispatch tasks to do that.
    if (!mDoneVideoSeeking) {
      RequestVideoData();
    }
    if (!mDoneAudioSeeking) {
      RequestAudioData();
    }
  }

  void OnSeekRejected(const SeekRejectValue& aReject) {
    AUTO_PROFILER_LABEL("AccurateSeekingState::OnSeekRejected", MEDIA_PLAYBACK);
    mSeekRequest.Complete();

    if (aReject.mError == NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA) {
      SLOG("OnSeekRejected reason=WAITING_FOR_DATA type={}",
           MediaData::EnumValueToString(aReject.mType));
      MOZ_ASSERT_IF(aReject.mType == MediaData::Type::AUDIO_DATA,
                    !mMaster->IsRequestingAudioData());
      MOZ_ASSERT_IF(aReject.mType == MediaData::Type::VIDEO_DATA,
                    !mMaster->IsRequestingVideoData());
      MOZ_ASSERT_IF(aReject.mType == MediaData::Type::AUDIO_DATA,
                    !mMaster->IsWaitingAudioData());
      MOZ_ASSERT_IF(aReject.mType == MediaData::Type::VIDEO_DATA,
                    !mMaster->IsWaitingVideoData());

      // Fire 'waiting' to notify the player that we are waiting for data.
      mMaster->mOnNextFrameStatus.Notify(
          MediaDecoderOwner::NEXT_FRAME_UNAVAILABLE_SEEKING);

      Reader()
          ->WaitForData(aReject.mType)
          ->Then(
              OwnerThread(), __func__,
              [this](MediaData::Type aType) {
                AUTO_PROFILER_LABEL(
                    "AccurateSeekingState::OnSeekRejected:WaitDataResolved",
                    MEDIA_PLAYBACK);
                SLOG("OnSeekRejected wait promise resolved");
                mWaitRequest.Complete();
                // Reset the sought tracks in the reader again, as DoSeek() did,
                // so that the retry does not find anything the reader started
                // on them while waiting, such as recovery from a decoder error.
                Reader()->ResetDecode(mSeekJob.mTarget->IsVideoOnly()
                                          ? TrackSet(TrackInfo::kVideoTrack)
                                          : TrackSet(TrackInfo::kAudioTrack,
                                                     TrackInfo::kVideoTrack));
                DemuxerSeek();
              },
              [this](const WaitForDataRejectValue& aRejection) {
                AUTO_PROFILER_LABEL(
                    "AccurateSeekingState::OnSeekRejected:WaitDataRejected",
                    MEDIA_PLAYBACK);
                SLOG("OnSeekRejected wait promise rejected");
                mWaitRequest.Complete();
                mMaster->DecodeError(NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA);
              })
          ->Track(mWaitRequest);
      return;
    }

    if (aReject.mError == NS_ERROR_DOM_MEDIA_END_OF_STREAM) {
      if (!mDoneAudioSeeking) {
        HandleEndOfAudioInternal();
      }
      if (!mDoneVideoSeeking) {
        HandleEndOfVideoInternal();
      }
      MaybeFinishSeek();
      return;
    }

    MOZ_ASSERT(NS_FAILED(aReject.mError),
               "Cancels should also disconnect mSeekRequest");
    mMaster->DecodeError(aReject.mError);
  }

  void RequestAudioData() {
    MOZ_ASSERT(!mDoneAudioSeeking);
    mMaster->RequestAudioData();
  }

  virtual void RequestVideoData() {
    MOZ_ASSERT(!mDoneVideoSeeking);
    mMaster->RequestVideoData(media::TimeUnit());
  }

  void AdjustFastSeekIfNeeded(MediaData* aSample) {
    if (mSeekJob.mTarget->IsFast() &&
        mSeekJob.mTarget->GetTime() > mCurrentTimeBeforeSeek &&
        aSample->mTime < mCurrentTimeBeforeSeek) {
      // We are doing a fastSeek, but we ended up *before* the previous
      // playback position. This is surprising UX, so switch to an accurate
      // seek and decode to the seek target. This is not conformant to the
      // spec, fastSeek should always be fast, but until we get the time to
      // change all Readers to seek to the keyframe after the currentTime
      // in this case, we'll just decode forward. Bug 1026330.
      mSeekJob.mTarget->SetType(SeekTarget::Accurate);
    }
  }

  nsresult DropAudioUpToSeekTarget(AudioData* aAudio) {
    MOZ_ASSERT(aAudio && mSeekJob.mTarget->IsAccurate());

    if (mSeekJob.mTarget->GetTime() >= aAudio->GetEndTime()) {
      // Our seek target lies after the frames in this AudioData. Don't
      // push it onto the audio queue, and keep decoding forwards.
      return NS_OK;
    }

    if (aAudio->mTime > mSeekJob.mTarget->GetTime()) {
      // The seek target doesn't lie in the audio block just after the last
      // audio frames we've seen which were before the seek target. This
      // could have been the first audio data we've seen after seek, i.e. the
      // seek terminated after the seek target in the audio stream. Just
      // abort the audio decode-to-target, the state machine will play
      // silence to cover the gap. Typically this happens in poorly muxed
      // files.
      SLOGW("Audio not synced after seek, maybe a poorly muxed file?");
      mMaster->PushAudio(aAudio);
      mDoneAudioSeeking = true;
      return NS_OK;
    }

    bool ok = aAudio->SetTrimWindow(
        {mSeekJob.mTarget->GetTime().ToBase(aAudio->mTime),
         aAudio->GetEndTime()});
    if (!ok) {
      return NS_ERROR_DOM_MEDIA_OVERFLOW_ERR;
    }

    MOZ_ASSERT(AudioQueue().GetSize() == 0,
               "Should be the 1st sample after seeking");
    mMaster->PushAudio(aAudio);
    mDoneAudioSeeking = true;

    return NS_OK;
  }

  nsresult DropVideoUpToSeekTarget(VideoData* aVideo) {
    MOZ_ASSERT(aVideo);
    SLOG("DropVideoUpToSeekTarget() frame [{}, {}]",
         aVideo->mTime.ToMicroseconds(), aVideo->GetEndTime().ToMicroseconds());
    const auto target = GetSeekTarget();

    // If the frame end time is less than the seek target, we won't want
    // to display this frame after the seek, so discard it.
    if (target >= aVideo->GetEndTime()) {
      SLOG("DropVideoUpToSeekTarget() pop video frame [{}, {}] target={}",
           aVideo->mTime.ToMicroseconds(),
           aVideo->GetEndTime().ToMicroseconds(), target.ToMicroseconds());
      PROFILER_MARKER_UNTYPED("MDSM::DropVideoUpToSeekTarget", MEDIA_PLAYBACK);
      mFirstVideoFrameAfterSeek = aVideo;
    } else {
      if (target >= aVideo->mTime && aVideo->GetEndTime() >= target) {
        // The seek target lies inside this frame's time slice. Adjust the
        // frame's start time to match the seek target.
        aVideo->UpdateTimestamp(target);
      }
      mFirstVideoFrameAfterSeek = nullptr;

      SLOG(
          "DropVideoUpToSeekTarget() found video frame [{}, {}] containing "
          "target={}",
          aVideo->mTime.ToMicroseconds(), aVideo->GetEndTime().ToMicroseconds(),
          target.ToMicroseconds());

      MOZ_ASSERT(VideoQueue().GetSize() == 0,
                 "Should be the 1st sample after seeking");
      mMaster->PushVideo(aVideo);
      mDoneVideoSeeking = true;
    }

    return NS_OK;
  }

  void HandleEndOfAudioInternal() {
    MOZ_ASSERT(!mDoneAudioSeeking);
    AudioQueue().Finish();
    mDoneAudioSeeking = true;
  }

  void HandleEndOfVideoInternal() {
    MOZ_ASSERT(!mDoneVideoSeeking);
    if (mFirstVideoFrameAfterSeek) {
      // Hit the end of stream. Move mFirstVideoFrameAfterSeek into
      // mSeekedVideoData so we have something to display after seeking.
      mMaster->PushVideo(mFirstVideoFrameAfterSeek);
    }
    VideoQueue().Finish();
    mDoneVideoSeeking = true;
  }

  void MaybeFinishSeek() {
    if (mDoneAudioSeeking && mDoneVideoSeeking) {
      SeekCompleted();
    }
  }

  /*
   * Track the current seek promise made by the reader.
   */
  MozPromiseRequestHolder<MediaFormatReader::SeekPromise> mSeekRequest;

  /*
   * Internal state.
   */
  media::TimeUnit mCurrentTimeBeforeSeek;
  bool mDoneAudioSeeking = false;
  bool mDoneVideoSeeking = false;
  MozPromiseRequestHolder<WaitForDataPromise> mWaitRequest;

  // This temporarily stores the first frame we decode after we seek.
  // This is so that if we hit end of stream while we're decoding to reach
  // the seek target, we will still have a frame that we can display as the
  // last frame in the media.
  RefPtr<VideoData> mFirstVideoFrameAfterSeek;

 private:
  virtual media::TimeUnit GetSeekTarget() const {
    return mSeekJob.mTarget->GetTime();
  }
};

/*
 * Remove samples from the queue until aCompare() returns false.
 * aCompare A function object with the signature bool(int64_t) which returns
 *          true for samples that should be removed.
 */
template <typename Type, typename Function>
static void DiscardFrames(MediaQueue<Type>& aQueue, const Function& aCompare) {
  while (aQueue.GetSize() > 0) {
    if (aCompare(aQueue.PeekFront()->mTime.ToMicroseconds())) {
      RefPtr<Type> releaseMe = aQueue.PopFront();
      continue;
    }
    break;
  }
}

class MediaDecoderStateMachine::NextFrameSeekingState
    : public MediaDecoderStateMachine::SeekingState {
 public:
  explicit NextFrameSeekingState(Master* aPtr) : SeekingState(aPtr) {}

  State GetState() const override {
    return DECODER_STATE_SEEKING_NEXTFRAMESEEKING;
  }

  RefPtr<MediaDecoder::SeekPromise> Enter(SeekJob&& aSeekJob,
                                          EventVisibility aVisibility) {
    MOZ_ASSERT(aSeekJob.mTarget->IsNextFrame());
    mCurrentTime = mMaster->GetMediaTime();
    mDuration = mMaster->Duration();
    return SeekingState::Enter(std::move(aSeekJob), aVisibility);
  }

  void Exit() override {
    // Disconnect my async seek operation.
    if (mAsyncSeekTask) {
      mAsyncSeekTask->Cancel();
    }

    // Disconnect MediaDecoder.
    mSeekJob.RejectIfExists(__func__);
  }

  void HandleAudioDecoded(AudioData* aAudio) override {
    mMaster->PushAudio(aAudio);
  }

  void HandleVideoDecoded(VideoData* aVideo) override {
    MOZ_ASSERT(aVideo);
    MOZ_ASSERT(!mSeekJob.mPromise.IsEmpty(), "Seek shouldn't be finished");
    MOZ_ASSERT(NeedMoreVideo());

    if (aVideo->mTime > mCurrentTime) {
      mMaster->PushVideo(aVideo);
      FinishSeek();
    } else {
      RequestVideoData();
    }
  }

  void HandleWaitingForAudio() override {
    MOZ_ASSERT(!mSeekJob.mPromise.IsEmpty(), "Seek shouldn't be finished");
    // We don't care about audio decode errors in this state which will be
    // handled by other states after seeking.
  }

  void HandleAudioCanceled() override {
    MOZ_ASSERT(!mSeekJob.mPromise.IsEmpty(), "Seek shouldn't be finished");
    // We don't care about audio decode errors in this state which will be
    // handled by other states after seeking.
  }

  void HandleEndOfAudio() override {
    MOZ_ASSERT(!mSeekJob.mPromise.IsEmpty(), "Seek shouldn't be finished");
    // We don't care about audio decode errors in this state which will be
    // handled by other states after seeking.
  }

  void HandleWaitingForVideo() override {
    MOZ_ASSERT(!mSeekJob.mPromise.IsEmpty(), "Seek shouldn't be finished");
    MOZ_ASSERT(NeedMoreVideo());
    mMaster->WaitForData(MediaData::Type::VIDEO_DATA);
  }

  void HandleVideoCanceled() override {
    MOZ_ASSERT(!mSeekJob.mPromise.IsEmpty(), "Seek shouldn't be finished");
    MOZ_ASSERT(NeedMoreVideo());
    RequestVideoData();
  }

  void HandleEndOfVideo() override {
    MOZ_ASSERT(!mSeekJob.mPromise.IsEmpty(), "Seek shouldn't be finished");
    MOZ_ASSERT(NeedMoreVideo());
    VideoQueue().Finish();
    FinishSeek();
  }

  void HandleAudioWaited(MediaData::Type aType) override {
    // We don't care about audio in this state.
  }

  void HandleVideoWaited(MediaData::Type aType) override {
    MOZ_ASSERT(!mSeekJob.mPromise.IsEmpty(), "Seek shouldn't be finished");
    MOZ_ASSERT(NeedMoreVideo());
    RequestVideoData();
  }

  TimeUnit CalculateNewCurrentTime() const override {
    // The HTMLMediaElement.currentTime should be updated to the seek target
    // which has been updated to the next frame's time.
    return mSeekJob.mTarget->GetTime();
  }

  void DoSeek() override {
    // Only observable seeks paused playback for reuse, so only they stop as a
    // seek; a suppressed seek keeps playing and stops normally.
    if (mVisibility == EventVisibility::Observable) {
      mMaster->StopMediaSink(MediaSink::StopReason::Seeking);
    } else {
      mMaster->StopMediaSink();
    }

    auto currentTime = mCurrentTime;
    DiscardFrames(VideoQueue(), [currentTime](int64_t aSampleTime) {
      return aSampleTime <= currentTime.ToMicroseconds();
    });

    // If there is a pending video request, finish the seeking if we don't need
    // more data, or wait for HandleVideoDecoded() to finish seeking.
    if (mMaster->IsRequestingVideoData()) {
      if (!NeedMoreVideo()) {
        FinishSeek();
      }
      return;
    }

    // Otherwise, we need to do the seek operation asynchronously for a special
    // case (video with no data)which has no data at all, the 1st
    // seekToNextFrame() operation reaches the end of the media. If we did the
    // seek operation synchronously, we immediately resolve the SeekPromise in
    // mSeekJob and then switch to the CompletedState which dispatches an
    // "ended" event. However, the ThenValue of the SeekPromise has not yet been
    // set, so the promise resolving is postponed and then the JS developer
    // receives the "ended" event before the seek promise is resolved. An
    // asynchronous seek operation helps to solve this issue since while the
    // seek is actually performed, the ThenValue of SeekPromise has already been
    // set so that it won't be postponed.
    RefPtr<Runnable> r = mAsyncSeekTask = new AysncNextFrameSeekTask(this);
    nsresult rv = OwnerThread()->Dispatch(r.forget());
    MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
    (void)rv;
  }

 private:
  void DoSeekInternal() {
    // We don't need to discard frames to the mCurrentTime here because we have
    // done it at DoSeek() and any video data received in between either
    // finishes the seek operation or be discarded, see HandleVideoDecoded().

    if (!NeedMoreVideo()) {
      FinishSeek();
    } else if (!mMaster->IsTrackingVideoData()) {
      RequestVideoData();
    }
  }

  class AysncNextFrameSeekTask : public Runnable {
   public:
    explicit AysncNextFrameSeekTask(NextFrameSeekingState* aStateObject)
        : Runnable(
              "MediaDecoderStateMachine::NextFrameSeekingState::"
              "AysncNextFrameSeekTask"),
          mStateObj(aStateObject) {}

    void Cancel() { mStateObj = nullptr; }

    NS_IMETHOD Run() override {
      if (mStateObj) {
        AUTO_PROFILER_LABEL("AysncNextFrameSeekTask::Run", MEDIA_PLAYBACK);
        mStateObj->DoSeekInternal();
      }
      return NS_OK;
    }

   private:
    NextFrameSeekingState* mStateObj;
  };

  void RequestVideoData() { mMaster->RequestVideoData(media::TimeUnit()); }

  bool NeedMoreVideo() const {
    // Need to request video when we have none and video queue is not finished.
    return VideoQueue().GetSize() == 0 && !VideoQueue().IsFinished();
  }

  // Update the seek target's time before resolving this seek task, the updated
  // time will be used in the MDSM::SeekCompleted() to update the MDSM's
  // position.
  void UpdateSeekTargetTime() {
    RefPtr<VideoData> data = VideoQueue().PeekFront();
    if (data) {
      mSeekJob.mTarget->SetTime(data->mTime);
    } else {
      MOZ_ASSERT(VideoQueue().AtEndOfStream());
      mSeekJob.mTarget->SetTime(mDuration);
    }
  }

  void FinishSeek() {
    MOZ_ASSERT(!NeedMoreVideo());
    UpdateSeekTargetTime();
    auto time = mSeekJob.mTarget->GetTime().ToMicroseconds();
    DiscardFrames(AudioQueue(),
                  [time](int64_t aSampleTime) { return aSampleTime < time; });
    SeekCompleted();
  }

  /*
   * Internal state.
   */
  TimeUnit mCurrentTime;
  TimeUnit mDuration;
  RefPtr<AysncNextFrameSeekTask> mAsyncSeekTask;
};

class MediaDecoderStateMachine::NextFrameSeekingFromDormantState
    : public MediaDecoderStateMachine::AccurateSeekingState {
 public:
  explicit NextFrameSeekingFromDormantState(Master* aPtr)
      : AccurateSeekingState(aPtr) {}

  State GetState() const override { return DECODER_STATE_SEEKING_FROMDORMANT; }

  RefPtr<MediaDecoder::SeekPromise> Enter(SeekJob&& aCurrentSeekJob,
                                          SeekJob&& aFutureSeekJob) {
    mFutureSeekJob = std::move(aFutureSeekJob);

    AccurateSeekingState::Enter(std::move(aCurrentSeekJob),
                                EventVisibility::Suppressed);

    // Once seekToNextFrame() is called, we assume the user is likely to keep
    // calling seekToNextFrame() repeatedly, and so, we should prevent the MDSM
    // from getting into Dormant state.
    mMaster->mMinimizePreroll = false;

    return mFutureSeekJob.mPromise.Ensure(__func__);
  }

  void Exit() override {
    mFutureSeekJob.RejectIfExists(__func__);
    AccurateSeekingState::Exit();
  }

 private:
  SeekJob mFutureSeekJob;

  // We don't want to transition to DecodingState once this seek completes,
  // instead, we transition to NextFrameSeekingState.
  void GoToNextState() override {
    SetState<NextFrameSeekingState>(std::move(mFutureSeekJob),
                                    EventVisibility::Observable);
  }
};

class MediaDecoderStateMachine::VideoOnlySeekingState
    : public MediaDecoderStateMachine::AccurateSeekingState {
 public:
  explicit VideoOnlySeekingState(Master* aPtr) : AccurateSeekingState(aPtr) {}

  State GetState() const override { return DECODER_STATE_SEEKING_VIDEOONLY; }

  RefPtr<MediaDecoder::SeekPromise> Enter(SeekJob&& aSeekJob,
                                          EventVisibility aVisibility) {
    MOZ_ASSERT(aSeekJob.mTarget->IsVideoOnly());
    MOZ_ASSERT(aVisibility == EventVisibility::Suppressed);

    RefPtr<MediaDecoder::SeekPromise> p =
        AccurateSeekingState::Enter(std::move(aSeekJob), aVisibility);

    // Dispatch a mozvideoonlyseekbegin event to indicate UI for corresponding
    // changes.
    mMaster->mOnPlaybackEvent.Notify(MediaPlaybackEvent::VideoOnlySeekBegin);

    return p;
  }

  void Exit() override {
    // We are completing or discarding this video-only seek operation now,
    // dispatch an event so that the UI can change in response to the end
    // of video-only seek.
    mMaster->mOnPlaybackEvent.Notify(
        MediaPlaybackEvent::VideoOnlySeekCompleted);

    AccurateSeekingState::Exit();
  }

  void HandleAudioDecoded(AudioData* aAudio) override {
    MOZ_ASSERT(mDoneAudioSeeking && !mDoneVideoSeeking,
               "Seek shouldn't be finished");
    MOZ_ASSERT(aAudio);

    // Video-only seek doesn't reset audio decoder. There might be pending audio
    // requests when AccurateSeekTask::Seek() begins. We will just store the
    // data without checking |mDiscontinuity| or calling
    // DropAudioUpToSeekTarget().
    mMaster->PushAudio(aAudio);
  }

  void HandleWaitingForAudio() override {}

  void HandleAudioCanceled() override {}

  void HandleEndOfAudio() override {}

  void HandleAudioWaited(MediaData::Type aType) override {
    MOZ_ASSERT(!mDoneAudioSeeking || !mDoneVideoSeeking,
               "Seek shouldn't be finished");

    // Ignore pending requests from video-only seek.
  }

  void DoSeek() override {
    // TODO: keep decoding audio.
    mDoneAudioSeeking = true;
    mDoneVideoSeeking = !Info().HasVideo();

    const auto offset = VideoQueue().GetOffset();
    mMaster->ResetDecode(TrackInfo::kVideoTrack);

    // Entering video-only state and we've looped at least once before, so we
    // need to set offset in order to let new video frames catch up with the
    // clock time.
    if (offset != media::TimeUnit::Zero()) {
      VideoQueue().SetOffset(offset);
    }

    DemuxerSeek();
  }

 protected:
  // Allow skip-to-next-key-frame to kick in if we fall behind the current
  // playback position so decoding has a better chance to catch up.
  void RequestVideoData() override {
    MOZ_ASSERT(!mDoneVideoSeeking);

    auto clock = mMaster->mMediaSink->IsStarted() ? mMaster->GetClock()
                                                  : mMaster->GetMediaTime();
    mMaster->AdjustByLooping(clock);
    const auto& nextKeyFrameTime = GetNextKeyFrameTime();

    auto threshold = clock;

    if (nextKeyFrameTime.IsValid() &&
        clock >= (nextKeyFrameTime - sSkipToNextKeyFrameThreshold)) {
      threshold = nextKeyFrameTime;
    }

    mMaster->RequestVideoData(threshold);
  }

 private:
  // Trigger skip to next key frame if the current playback position is very
  // close the next key frame's time.
  static constexpr TimeUnit sSkipToNextKeyFrameThreshold =
      TimeUnit::FromMicroseconds(5000);

  // If the media is playing, drop video until catch up playback position.
  media::TimeUnit GetSeekTarget() const override {
    auto target = mMaster->mMediaSink->IsStarted()
                      ? mMaster->GetClock()
                      : mSeekJob.mTarget->GetTime();
    mMaster->AdjustByLooping(target);
    return target;
  }

  media::TimeUnit GetNextKeyFrameTime() const {
    // We only call this method in RequestVideoData() and we only request video
    // data if we haven't done video seeking.
    MOZ_DIAGNOSTIC_ASSERT(!mDoneVideoSeeking);
    MOZ_DIAGNOSTIC_ASSERT(mMaster->VideoQueue().GetSize() == 0);

    if (mFirstVideoFrameAfterSeek) {
      return mFirstVideoFrameAfterSeek->NextKeyFrameTime();
    }

    return TimeUnit::Invalid();
  }
};

constexpr TimeUnit MediaDecoderStateMachine::VideoOnlySeekingState::
    sSkipToNextKeyFrameThreshold;

RefPtr<MediaDecoder::SeekPromise>
MediaDecoderStateMachine::DormantState::HandleSeek(const SeekTarget& aTarget) {
  if (aTarget.IsNextFrame()) {
    // NextFrameSeekingState doesn't reset the decoder unlike
    // AccurateSeekingState. So we first must come out of dormant by seeking to
    // mPendingSeek and continue later with the NextFrameSeek
    SLOG("Changed state to SEEKING (to {})",
         aTarget.GetTime().ToMicroseconds());
    SeekJob seekJob;
    seekJob.mTarget = Some(aTarget);
    return StateObject::SetState<NextFrameSeekingFromDormantState>(
        std::move(mPendingSeek), std::move(seekJob));
  }

  return StateObject::HandleSeek(aTarget);
}

/**
 * Purpose: stop playback until enough data is decoded to continue playback.
 *
 * Transition to:
 *   SEEKING if any seek request.
 *   SHUTDOWN if any decode error.
 *   COMPLETED when having decoded all audio/video data.
 *   DECODING/LOOPING_DECODING when having decoded enough data to continue
 * playback.
 */
class MediaDecoderStateMachine::BufferingState
    : public MediaDecoderStateMachine::StateObject {
 public:
  explicit BufferingState(Master* aPtr) : StateObject(aPtr) {}

  void Enter() {
    if (mMaster->IsPlaying()) {
      mMaster->StopPlayback();
    }

    mBufferingStart = TimeStamp::Now();
    // Playback is now stopped, so there is no need to connect to the queues'
    // PopFrontEvent()s, but frames may have been recently popped before the
    // transition from DECODING.
    if (mMaster->IsAudioDecoding() && !mMaster->HaveEnoughDecodedAudio() &&
        !mMaster->IsTrackingAudioData()) {
      mMaster->RequestAudioData();
    }
    if (mMaster->IsVideoDecoding() && !mMaster->HaveEnoughDecodedVideo() &&
        !mMaster->IsTrackingVideoData()) {
      mMaster->RequestVideoData(mMaster->GetMediaTime());
    }

    mMaster->ScheduleStateMachineIn(TimeUnit::FromMicroseconds(USECS_PER_S));
    mMaster->mOnNextFrameStatus.Notify(
        MediaDecoderOwner::NEXT_FRAME_UNAVAILABLE_BUFFERING);
  }

  void Step() override;

  State GetState() const override { return DECODER_STATE_BUFFERING; }

  void HandleAudioDecoded(AudioData* aAudio) override {
    mMaster->PushAudio(aAudio);
    if (!mMaster->HaveEnoughDecodedAudio()) {
      mMaster->RequestAudioData();
    }
    // This might be the sample we need to exit buffering.
    // Schedule Step() to check it.
    mMaster->ScheduleStateMachine();
  }

  void HandleVideoDecoded(VideoData* aVideo) override {
    mMaster->PushVideo(aVideo);
    if (!mMaster->HaveEnoughDecodedVideo()) {
      mMaster->RequestVideoData(mMaster->GetMediaTime());
    }
    // This might be the sample we need to exit buffering.
    // Schedule Step() to check it.
    mMaster->ScheduleStateMachine();
  }

  void HandleAudioCanceled() override { mMaster->RequestAudioData(); }

  void HandleVideoCanceled() override {
    mMaster->RequestVideoData(mMaster->GetMediaTime());
  }

  void HandleWaitingForAudio() override {
    mMaster->WaitForData(MediaData::Type::AUDIO_DATA);
  }

  void HandleWaitingForVideo() override {
    mMaster->WaitForData(MediaData::Type::VIDEO_DATA);
  }

  void HandleAudioWaited(MediaData::Type aType) override {
    mMaster->RequestAudioData();
  }

  void HandleVideoWaited(MediaData::Type aType) override {
    mMaster->RequestVideoData(mMaster->GetMediaTime());
  }

  void HandleEndOfAudio() override;
  void HandleEndOfVideo() override;

  void HandleVideoSuspendTimeout() override {
    // No video, so nothing to suspend.
    if (!mMaster->HasVideo()) {
      return;
    }

    mMaster->mVideoDecodeSuspended = true;
    mMaster->mOnPlaybackEvent.Notify(MediaPlaybackEvent::EnterVideoSuspend);
    Reader()->SetVideoBlankDecode(true);
  }

 private:
  TimeStamp mBufferingStart;

  // The maximum number of second we spend buffering when we are short on
  // unbuffered data.
  const uint32_t mBufferingWait = 15;
};

/**
 * Purpose: play all the decoded data and fire the 'ended' event.
 *
 * Transition to:
 *   SEEKING if any seek request.
 *   LOOPING_DECODING if MDSM enable looping.
 */
class MediaDecoderStateMachine::CompletedState
    : public MediaDecoderStateMachine::StateObject {
 public:
  explicit CompletedState(Master* aPtr) : StateObject(aPtr) {}

  void Enter() {
    bool hasNextFrame = (!mMaster->HasAudio() || !mMaster->mAudioCompleted) &&
                        (!mMaster->HasVideo() || !mMaster->mVideoCompleted);

    // Notify before dispatching to the reader, so that this reaches the main
    // thread in the same task as the resolution of a seek ending here (see
    // ConsistentOrdering in AbstractThread.h).
    mMaster->mOnNextFrameStatus.Notify(
        hasNextFrame ? MediaDecoderOwner::NEXT_FRAME_AVAILABLE
                     : MediaDecoderOwner::NEXT_FRAME_UNAVAILABLE);

    // On Android, the life cycle of graphic buffer is equal to Android's codec,
    // we couldn't release it if we still need to render the frame.
#ifndef MOZ_WIDGET_ANDROID
    if (!mMaster->mLooping) {
      // We've decoded all samples.
      // We don't need decoders anymore if not looping.
      Reader()->ReleaseResources();
    }
#endif

    Step();
  }

  void Exit() override { mSentPlaybackEndedEvent = false; }

  void Step() override {
    if (mMaster->mPlayState != MediaDecoder::PLAY_STATE_PLAYING &&
        mMaster->IsPlaying()) {
      mMaster->StopPlayback();
    }

    // Play the remaining media. We want to run AdvanceFrame() at least
    // once to ensure the current playback position is advanced to the
    // end of the media, and so that we update the readyState.
    if ((mMaster->HasVideo() && !mMaster->mVideoCompleted) ||
        (mMaster->HasAudio() && !mMaster->mAudioCompleted)) {
      // Start playback if necessary to play the remaining media.
      mMaster->MaybeStartPlayback();
      mMaster->UpdatePlaybackPositionPeriodically();
      MOZ_ASSERT(!mMaster->IsPlaying() || mMaster->IsStateMachineScheduled(),
                 "Must have timer scheduled");
      return;
    }

    // StopPlayback in order to reset the IsPlaying() state so audio
    // is restarted correctly.
    mMaster->StopPlayback();

    if (!mSentPlaybackEndedEvent) {
      auto clockTime =
          std::max(mMaster->AudioEndTime(), mMaster->VideoEndTime());
      // Correct the time over the end once looping was turned on.
      mMaster->AdjustByLooping(clockTime);
      if (mMaster->mDuration.Ref()->IsInfinite()) {
        // We have a finite duration when playback reaches the end.
        mMaster->mDuration = Some(clockTime);
        DDLOGEX(mMaster, DDLogCategory::Property, "duration_us",
                mMaster->mDuration.Ref()->ToMicroseconds());
      }
      mMaster->UpdatePlaybackPosition(clockTime);

      // Ensure readyState is updated before firing the 'ended' event.
      mMaster->mOnNextFrameStatus.Notify(
          MediaDecoderOwner::NEXT_FRAME_UNAVAILABLE);

      mMaster->mOnPlaybackEvent.Notify(MediaPlaybackEvent::PlaybackEnded);

      mSentPlaybackEndedEvent = true;

      // MediaSink::GetEndTime() must be called before stopping playback.
      mMaster->StopMediaSink();
    }
  }

  State GetState() const override { return DECODER_STATE_COMPLETED; }

  void HandleLoopingChanged() override {
    if (mMaster->mLooping) {
      SetDecodingState();
    }
  }

  void HandleAudioCaptured() override {
    // MediaSink is changed. Schedule Step() to check if we can start playback.
    mMaster->ScheduleStateMachine();
  }

  void HandleVideoSuspendTimeout() override {
    // Do nothing since no decoding is going on.
  }

  void HandleResumeVideoDecoding(const TimeUnit&) override {
    // Resume the video decoder and seek to the last video frame.
    // This triggers a video-only seek which won't update the playback position.
    auto target = mMaster->mDecodedVideoEndTime;
    mMaster->AdjustByLooping(target);
    StateObject::HandleResumeVideoDecoding(target);
  }

  void HandlePlayStateChanged(MediaDecoder::PlayState aPlayState) override {
    if (aPlayState == MediaDecoder::PLAY_STATE_PLAYING) {
      // Schedule Step() to check if we can start playback.
      mMaster->ScheduleStateMachine();
    }
  }

 private:
  bool mSentPlaybackEndedEvent = false;
};

/**
 * Purpose: release all resources allocated by MDSM.
 *
 * Transition to:
 *   None since this is the final state.
 *
 * Transition from:
 *   Any states other than SHUTDOWN.
 */
class MediaDecoderStateMachine::ShutdownState
    : public MediaDecoderStateMachine::StateObject {
 public:
  explicit ShutdownState(Master* aPtr) : StateObject(aPtr) {}

  RefPtr<ShutdownPromise> Enter();

  void Exit() override {
    MOZ_DIAGNOSTIC_CRASH("Shouldn't escape the SHUTDOWN state.");
  }

  State GetState() const override { return DECODER_STATE_SHUTDOWN; }

  RefPtr<MediaDecoder::SeekPromise> HandleSeek(
      const SeekTarget& aTarget) override {
    MOZ_DIAGNOSTIC_CRASH("Can't seek in shutdown state.");
    return MediaDecoder::SeekPromise::CreateAndReject(true, __func__);
  }

  RefPtr<ShutdownPromise> HandleShutdown() override {
    MOZ_DIAGNOSTIC_CRASH("Already shutting down.");
    return nullptr;
  }

  void HandleVideoSuspendTimeout() override {
    MOZ_DIAGNOSTIC_CRASH("Already shutting down.");
  }

  void HandleResumeVideoDecoding(const TimeUnit&) override {
    MOZ_DIAGNOSTIC_CRASH("Already shutting down.");
  }
};

RefPtr<MediaDecoder::SeekPromise>
MediaDecoderStateMachine::StateObject::HandleSeek(const SeekTarget& aTarget) {
  SLOG("Changed state to SEEKING (to {})", aTarget.GetTime().ToMicroseconds());
  SeekJob seekJob;
  seekJob.mTarget = Some(aTarget);
  return SetSeekingState(std::move(seekJob), EventVisibility::Observable);
}

RefPtr<ShutdownPromise>
MediaDecoderStateMachine::StateObject::HandleShutdown() {
  return SetState<ShutdownState>();
}

void MediaDecoderStateMachine::StateObject::HandleResumeVideoDecoding(
    const TimeUnit& aTarget) {
  MOZ_ASSERT(mMaster->mVideoDecodeSuspended);

  mMaster->mVideoDecodeSuspended = false;
  mMaster->mOnPlaybackEvent.Notify(MediaPlaybackEvent::ExitVideoSuspend);
  Reader()->SetVideoBlankDecode(false);

  // Start video-only seek to the current time.
  SeekJob seekJob;

  // We use fastseek to optimize the resuming time.
  // FastSeek is only used for video-only media since we don't need to worry
  // about A/V sync.
  // Don't use fastSeek if we want to seek to the end because it might seek to a
  // keyframe before the last frame (if the last frame itself is not a keyframe)
  // and we always want to present the final frame to the user when seeking to
  // the end.
  const auto type = mMaster->HasAudio() || aTarget == mMaster->Duration()
                        ? SeekTarget::Type::Accurate
                        : SeekTarget::Type::PrevSyncPoint;

  seekJob.mTarget.emplace(aTarget, type, SeekTarget::Track::VideoOnly);
  SLOG("video-only seek target={}, current time={}", aTarget.ToMicroseconds(),
       mMaster->GetMediaTime().ToMicroseconds());

  SetSeekingState(std::move(seekJob), EventVisibility::Suppressed);
}

RefPtr<MediaDecoder::SeekPromise>
MediaDecoderStateMachine::StateObject::SetSeekingState(
    SeekJob&& aSeekJob, EventVisibility aVisibility) {
  if (aSeekJob.mTarget->IsAccurate() || aSeekJob.mTarget->IsFast()) {
    if (aSeekJob.mTarget->IsVideoOnly()) {
      return SetState<VideoOnlySeekingState>(std::move(aSeekJob), aVisibility);
    }
    return SetState<AccurateSeekingState>(std::move(aSeekJob), aVisibility);
  }

  if (aSeekJob.mTarget->IsNextFrame()) {
    return SetState<NextFrameSeekingState>(std::move(aSeekJob), aVisibility);
  }

  MOZ_ASSERT_UNREACHABLE("Unknown SeekTarget::Type.");
  return nullptr;
}

void MediaDecoderStateMachine::StateObject::SetDecodingState() {
  if (mMaster->IsInSeamlessLooping()) {
    SetState<LoopingDecodingState>();
    return;
  }
  SetState<DecodingState>();
}

void MediaDecoderStateMachine::DecodeMetadataState::OnMetadataRead(
    MetadataHolder&& aMetadata) {
  mMetadataRequest.Complete();

  AUTO_PROFILER_LABEL("DecodeMetadataState::OnMetadataRead", MEDIA_PLAYBACK);
  mMaster->mInfo.emplace(*aMetadata.mInfo);
  mMaster->mMediaSeekable = Info().mMediaSeekable;
  mMaster->mMediaSeekableOnlyInBufferedRanges =
      Info().mMediaSeekableOnlyInBufferedRanges;

  if (Info().mMetadataDuration.isSome()) {
    mMaster->mDuration = Info().mMetadataDuration;
  } else if (Info().mUnadjustedMetadataEndTime.isSome()) {
    const TimeUnit unadjusted = Info().mUnadjustedMetadataEndTime.ref();
    const TimeUnit adjustment = Info().mStartTime;
    SLOG("No metadata duration, calculate one. unadjusted={}, adjustment={}",
         unadjusted.ToMicroseconds(), adjustment.ToMicroseconds());
    mMaster->mInfo->mMetadataDuration.emplace(unadjusted - adjustment);
    mMaster->mDuration = Info().mMetadataDuration;
  }

  // If we don't know the duration by this point, we assume infinity, per spec.
  if (mMaster->mDuration.Ref().isNothing()) {
    mMaster->mDuration = Some(TimeUnit::FromInfinity());
  }

  DDLOGEX(mMaster, DDLogCategory::Property, "duration_us",
          mMaster->mDuration.Ref()->ToMicroseconds());

  if (mMaster->HasVideo()) {
    SLOG("Video decode HWAccel={} videoQueueSize={}",
         Reader()->VideoIsHardwareAccelerated(),
         mMaster->GetAmpleVideoFrames());
  }

  MOZ_ASSERT(mMaster->mDuration.Ref().isSome());
  SLOG("OnMetadataRead, duration={}",
       mMaster->mDuration.Ref()->ToMicroseconds());

  mMaster->mMetadataLoadedEvent.Notify(std::move(aMetadata.mInfo),
                                       std::move(aMetadata.mTags),
                                       MediaDecoderEventVisibility::Observable);

  // Check whether the media satisfies the requirement of seamless looping.
  // TODO : after we ensure video seamless looping is stable enough, then we can
  // remove this to make the condition always true.
  mMaster->mSeamlessLoopingAllowed = StaticPrefs::media_seamless_looping();
  if (mMaster->HasVideo()) {
    mMaster->mSeamlessLoopingAllowed =
        StaticPrefs::media_seamless_looping_video();
  }

  SetState<DecodingFirstFrameState>();
}

void MediaDecoderStateMachine::DormantState::HandlePlayStateChanged(
    MediaDecoder::PlayState aPlayState) {
  if (aPlayState == MediaDecoder::PLAY_STATE_PLAYING) {
    // Exit dormant when the user wants to play.
    MOZ_ASSERT(mMaster->mSentFirstFrameLoadedEvent);
    SetSeekingState(std::move(mPendingSeek), EventVisibility::Suppressed);
  }
}

void MediaDecoderStateMachine::DecodingFirstFrameState::Enter() {
  // Transition to DECODING if we've decoded first frames.
  if (mMaster->mSentFirstFrameLoadedEvent) {
    SetDecodingState();
    return;
  }

  MOZ_ASSERT(!mMaster->mVideoDecodeSuspended);

  // Dispatch tasks to decode first frames.
  if (mMaster->HasAudio()) {
    mMaster->RequestAudioData();
  }
  if (mMaster->HasVideo()) {
    mMaster->RequestVideoData(media::TimeUnit());
  }
}

void MediaDecoderStateMachine::DecodingFirstFrameState::
    MaybeFinishDecodeFirstFrame() {
  MOZ_ASSERT(!mMaster->mSentFirstFrameLoadedEvent);

  if ((mMaster->IsAudioDecoding() && AudioQueue().GetSize() == 0) ||
      (mMaster->IsVideoDecoding() && VideoQueue().GetSize() == 0)) {
    return;
  }

  mMaster->FinishDecodeFirstFrame();
  if (mPendingSeek.Exists()) {
    SetSeekingState(std::move(mPendingSeek), EventVisibility::Observable);
  } else {
    SetDecodingState();
  }
}

void MediaDecoderStateMachine::DecodingState::Enter() {
  MOZ_ASSERT(mMaster->mSentFirstFrameLoadedEvent);

  if (mMaster->mVideoDecodeSuspended &&
      mMaster->mVideoDecodeMode == VideoDecodeMode::Normal) {
    StateObject::HandleResumeVideoDecoding(mMaster->GetMediaTime());
    return;
  }

  if (mMaster->mVideoDecodeMode == VideoDecodeMode::Suspend &&
      !mMaster->mVideoDecodeSuspendTimer.IsScheduled() &&
      !mMaster->mVideoDecodeSuspended) {
    // If the VideoDecodeMode is Suspend and the timer is not schedule, it means
    // the timer has timed out and we should suspend video decoding now if
    // necessary.
    HandleVideoSuspendTimeout();
  }

  // If we're in the normal decoding mode and the decoding has finished, then we
  // should go to `completed` state because we don't need to decode anything
  // later. However, if we're in the saemless decoding mode, we will restart
  // decoding ASAP so we can still stay in `decoding` state.
  if (!mMaster->IsVideoDecoding() && !mMaster->IsAudioDecoding() &&
      !mMaster->IsInSeamlessLooping()) {
    SetState<CompletedState>();
    return;
  }

  mOnAudioPopped =
      AudioQueue().PopFrontEvent().Connect(OwnerThread(), [this]() {
        AUTO_PROFILER_LABEL("MediaDecoderStateMachine::OnAudioPopped",
                            MEDIA_PLAYBACK);
        if (mMaster->IsAudioDecoding() && !mMaster->HaveEnoughDecodedAudio()) {
          EnsureAudioDecodeTaskQueued();
        }
      });
  mOnVideoPopped =
      VideoQueue().PopFrontEvent().Connect(OwnerThread(), [this]() {
        AUTO_PROFILER_LABEL("MediaDecoderStateMachine::OnVideoPopped",
                            MEDIA_PLAYBACK);
        if (mMaster->IsVideoDecoding() && !mMaster->HaveEnoughDecodedVideo()) {
          EnsureVideoDecodeTaskQueued();
        }
      });

  mMaster->mOnNextFrameStatus.Notify(MediaDecoderOwner::NEXT_FRAME_AVAILABLE);

  mDecodeStartTime = TimeStamp::Now();

  MaybeStopPrerolling();

  // Ensure that we've got tasks enqueued to decode data if we need to.
  DispatchDecodeTasksIfNeeded();

  mMaster->ScheduleStateMachine();

  // Will enter dormant when playback is paused for a while.
  if (mMaster->mPlayState == MediaDecoder::PLAY_STATE_PAUSED) {
    StartDormantTimer();
  }
}

void MediaDecoderStateMachine::DecodingState::Step() {
  if (mMaster->mPlayState != MediaDecoder::PLAY_STATE_PLAYING &&
      mMaster->IsPlaying()) {
    // We're playing, but the element/decoder is in paused state. Stop
    // playing!
    mMaster->StopPlayback();
  }

  // Start playback if necessary so that the clock can be properly queried.
  if (!mIsPrerolling) {
    mMaster->MaybeStartPlayback();
  }

  mMaster->UpdatePlaybackPositionPeriodically();
  MOZ_ASSERT(!mMaster->IsPlaying() || mMaster->IsStateMachineScheduled(),
             "Must have timer scheduled");
  if (IsBufferingAllowed()) {
    MaybeStartBuffering();
  }
}

void MediaDecoderStateMachine::DecodingState::HandleEndOfAudio() {
  AudioQueue().Finish();
  if (!mMaster->IsVideoDecoding()) {
    SetState<CompletedState>();
  } else {
    MaybeStopPrerolling();
  }
}

void MediaDecoderStateMachine::DecodingState::HandleEndOfVideo() {
  VideoQueue().Finish();
  if (!mMaster->IsAudioDecoding()) {
    SetState<CompletedState>();
  } else {
    MaybeStopPrerolling();
  }
}

void MediaDecoderStateMachine::DecodingState::DispatchDecodeTasksIfNeeded() {
  if (mMaster->IsAudioDecoding() && !mMaster->mMinimizePreroll &&
      !mMaster->HaveEnoughDecodedAudio()) {
    EnsureAudioDecodeTaskQueued();
  }

  if (mMaster->IsVideoDecoding() && !mMaster->mMinimizePreroll &&
      !mMaster->HaveEnoughDecodedVideo()) {
    EnsureVideoDecodeTaskQueued();
  }
}

void MediaDecoderStateMachine::DecodingState::EnsureAudioDecodeTaskQueued() {
  if (!mMaster->IsAudioDecoding() || mMaster->IsTrackingAudioData()) {
    return;
  }
  mMaster->RequestAudioData();
}

void MediaDecoderStateMachine::DecodingState::EnsureVideoDecodeTaskQueued() {
  if (!mMaster->IsVideoDecoding() || mMaster->IsTrackingVideoData()) {
    return;
  }
  mMaster->RequestVideoData(mMaster->GetMediaTime(),
                            ShouldRequestNextKeyFrame());
}

void MediaDecoderStateMachine::DecodingState::MaybeStartBuffering() {
  // Buffering makes senses only after decoding first frames.
  MOZ_ASSERT(mMaster->mSentFirstFrameLoadedEvent);

  // Don't enter buffering when MediaDecoder is not playing.
  if (mMaster->mPlayState != MediaDecoder::PLAY_STATE_PLAYING) {
    return;
  }

  // Don't enter buffering while prerolling so that the decoder has a chance to
  // enqueue some decoded data before we give up and start buffering.
  if (!mMaster->IsPlaying()) {
    return;
  }

  // Note we could have a wait promise pending when playing non-MSE EME.
  if (mMaster->OutOfDecodedAudio() && mMaster->IsWaitingAudioData()) {
    PROFILER_MARKER_TEXT("MDSM::StartBuffering", MEDIA_PLAYBACK, {},
                         "OutOfDecodedAudio");
    SLOG("Enter buffering due to out of decoded audio");
    SetState<BufferingState>();
    return;
  }
  if (mMaster->OutOfDecodedVideo() && mMaster->IsWaitingVideoData()) {
    PROFILER_MARKER_TEXT("MDSM::StartBuffering", MEDIA_PLAYBACK, {},
                         "OutOfDecodedVideo");
    SLOG("Enter buffering due to out of decoded video");
    SetState<BufferingState>();
    return;
  }

  if (Reader()->UseBufferingHeuristics() && mMaster->HasLowDecodedData() &&
      mMaster->HasLowBufferedData() && !mMaster->mCanPlayThrough) {
    PROFILER_MARKER_TEXT("MDSM::StartBuffering", MEDIA_PLAYBACK, {},
                         "BufferingHeuristics");
    SLOG("Enter buffering due to buffering heruistics");
    SetState<BufferingState>();
  }
}

void MediaDecoderStateMachine::LoopingDecodingState::HandleError(
    const MediaResult& aError, bool aIsAudio) {
  SLOG("{} looping failed, aError={}", aIsAudio ? "audio" : "video",
       aError.ErrorName().get());
  switch (aError.Code()) {
    case NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA:
      if (aIsAudio) {
        HandleWaitingForAudio();
      } else {
        HandleWaitingForVideo();
      }
      [[fallthrough]];
    case NS_ERROR_DOM_MEDIA_END_OF_STREAM:
      // This could happen after either the resource has been close, or the data
      // hasn't been appended in MSE, so that we won't be able to get any
      // sample and need to fallback to normal looping.
      if (mIsReachingAudioEOS && mIsReachingVideoEOS) {
        SetState<CompletedState>();
      }
      break;
    default:
      mMaster->DecodeError(aError);
      break;
  }
}

void MediaDecoderStateMachine::SeekingState::SeekCompleted() {
  MOZ_ASSERT(mMaster->OnTaskQueue());
  const auto newCurrentTime = CalculateNewCurrentTime();
  const bool seekingToEnd = (newCurrentTime == mMaster->Duration() ||
                             newCurrentTime.EqualsAtLowestResolution(
                                 mMaster->Duration().ToBase(USECS_PER_S))) &&
                            !mMaster->IsLiveStream();

  // A warm resume (a seek that interrupted active playback and did not land at
  // the end of the stream) re-creates the audio sink with the decode pipeline
  // already primed. Only then start the sink asynchronously (resume on the
  // system clock while cubeb inits off-thread) and use a minimal preroll,
  // resuming the clock on the target frame and refilling in the background. A
  // paused seek, or a seek to the end, starts cold or completes instead, so
  // leave this off so a later unrelated start does not inherit it.
  mMaster->mStartSinkAfterWarmSeek =
      !seekingToEnd && mMaster->mPlayState == MediaDecoder::PLAY_STATE_PLAYING;
  SLOG("SeekCompleted, startSinkAfterWarmSeek={}, seekingToEnd={}",
       mMaster->mStartSinkAfterWarmSeek, seekingToEnd);

  if (seekingToEnd) {
    SLOG("Seek completed, seeked to end: {}", newCurrentTime.ToString().get());
    // will transition to COMPLETED immediately. Note we don't do
    // this when playing a live stream, since the end of media will advance
    // once we download more data!
    AudioQueue().Finish();
    VideoQueue().Finish();

    // We won't start MediaSink when paused. m{Audio,Video}Completed will
    // remain false and 'playbackEnded' won't be notified. Therefore we
    // need to set these flags explicitly when seeking to the end.
    mMaster->mAudioCompleted = true;
    mMaster->mVideoCompleted = true;

    // There might still be a pending audio request when doing video-only or
    // next-frame seek. Discard it so we won't break the invariants of the
    // COMPLETED state by adding audio samples to a finished queue.
    mMaster->mAudioDataRequest.DisconnectIfExists();
  }

  // We want to resolve the seek request prior finishing the first frame
  // to ensure that the seeked event is fired prior loadeded.
  // Note: SeekJob.Resolve() resets SeekJob.mTarget. Don't use mSeekJob anymore
  //       hereafter.
  mSeekJob.Resolve(__func__);

  // Notify FirstFrameLoaded now if we haven't since we've decoded some data
  // for readyState to transition to HAVE_CURRENT_DATA and fire 'loadeddata'.
  if (!mMaster->mSentFirstFrameLoadedEvent) {
    mMaster->FinishDecodeFirstFrame();
  }

  // Ensure timestamps are up to date.
  // Suppressed visibility comes from two cases: (1) leaving dormant state,
  // and (2) resuming suspended video decoder. We want both cases to be
  // transparent to the user. So we only notify the change when the seek
  // request is from the user.
  if (mVisibility == EventVisibility::Observable) {
    // Don't update playback position for video-only seek.
    // Otherwise we might have |newCurrentTime > mMediaSink->GetPosition()|
    // and fail the assertion in GetClock() since we didn't stop MediaSink.
    mMaster->UpdatePlaybackPositionInternal(newCurrentTime);
  }

  // Try to decode another frame to detect if we're at the end...
  SLOG("Seek completed, mCurrentPosition={}",
       mMaster->mCurrentPosition.Ref().ToMicroseconds());

  if (mMaster->VideoQueue().PeekFront()) {
    mMaster->mMediaSink->Redraw(Info().mVideo);
    mMaster->mOnPlaybackEvent.Notify(MediaPlaybackEvent::Invalidate);
  }

  GoToNextState();
}

void MediaDecoderStateMachine::BufferingState::Step() {
  TimeStamp now = TimeStamp::Now();
  MOZ_ASSERT(!mBufferingStart.IsNull(), "Must know buffering start time.");

  if (Reader()->UseBufferingHeuristics()) {
    if (mMaster->IsWaitingAudioData() || mMaster->IsWaitingVideoData()) {
      // Can't exit buffering when we are still waiting for data.
      // Note we don't schedule next loop for we will do that when the wait
      // promise is resolved.
      return;
    }
    // With buffering heuristics, we exit buffering state when we:
    // 1. can play through or
    // 2. time out (specified by mBufferingWait) or
    // 3. have enough buffered data.
    TimeDuration elapsed = now - mBufferingStart;
    TimeDuration timeout =
        TimeDuration::FromSeconds(mBufferingWait * mMaster->mPlaybackRate);
    bool stopBuffering =
        mMaster->mCanPlayThrough || elapsed >= timeout ||
        !mMaster->HasLowBufferedData(TimeUnit::FromSeconds(mBufferingWait));
    if (!stopBuffering) {
      SLOG("Buffering: wait {}s, timeout in {:.3f}s", mBufferingWait,
           mBufferingWait - elapsed.ToSeconds());
      mMaster->ScheduleStateMachineIn(TimeUnit::FromMicroseconds(USECS_PER_S));
      return;
    }
  } else if (mMaster->OutOfDecodedAudio() || mMaster->OutOfDecodedVideo()) {
    MOZ_ASSERT(!mMaster->OutOfDecodedAudio() ||
               mMaster->IsTrackingAudioData() ||
               mMaster->HasNotifiedPlaybackError());
    MOZ_ASSERT(!mMaster->OutOfDecodedVideo() ||
               mMaster->IsTrackingVideoData() ||
               mMaster->HasNotifiedPlaybackError());
    SLOG(
        "In buffering mode, waiting to be notified: outOfAudio: {}, "
        "mAudioStatus: {}, outOfVideo: {}, mVideoStatus: {}",
        mMaster->OutOfDecodedAudio(), mMaster->AudioRequestStatus(),
        mMaster->OutOfDecodedVideo(), mMaster->VideoRequestStatus());
    return;
  }

  SLOG("Buffered for {:.3f}s", (now - mBufferingStart).ToSeconds());
  mMaster->mTotalBufferingDuration += (now - mBufferingStart);
  SetDecodingState();
}

void MediaDecoderStateMachine::BufferingState::HandleEndOfAudio() {
  AudioQueue().Finish();
  if (!mMaster->IsVideoDecoding()) {
    SetState<CompletedState>();
  } else {
    // Check if we can exit buffering.
    mMaster->ScheduleStateMachine();
  }
}

void MediaDecoderStateMachine::BufferingState::HandleEndOfVideo() {
  VideoQueue().Finish();
  if (!mMaster->IsAudioDecoding()) {
    SetState<CompletedState>();
  } else {
    // Check if we can exit buffering.
    mMaster->ScheduleStateMachine();
  }
}

RefPtr<ShutdownPromise> MediaDecoderStateMachine::ShutdownState::Enter() {
  auto* master = mMaster;

  master->mDelayedScheduler.Reset();

  // Shutdown happens while decode timer is active, we need to disconnect and
  // dispose of the timer.
  master->CancelSuspendTimer();

  if (master->IsPlaying()) {
    master->StopPlayback();
  }

  master->mAudioDataRequest.DisconnectIfExists();
  master->mVideoDataRequest.DisconnectIfExists();
  master->mAudioWaitRequest.DisconnectIfExists();
  master->mVideoWaitRequest.DisconnectIfExists();

  // Resetting decode should be called after stopping media sink, which can
  // ensure that we have an empty media queue before seeking the demuxer.
  master->StopMediaSink();
  master->ResetDecode();
  master->mMediaSink->Shutdown();

  // Prevent dangling pointers by disconnecting the listeners.
  master->mAudioQueueListener.Disconnect();
  master->mVideoQueueListener.Disconnect();
  master->mMetadataManager.Disconnect();
  master->mOnMediaNotSeekable.Disconnect();
  master->mAudibleListener.DisconnectIfExists();
  master->mPlaybackRateFallbackListener.DisconnectIfExists();

  // Disconnect canonicals and mirrors before shutting down our task queue.
  master->mStreamName.DisconnectIfConnected();
  master->mSinkDevice.DisconnectIfConnected();
  master->mOutputCaptureInfo.DisconnectIfConnected();
  master->mOutputTracks.DisconnectIfConnected();
  master->mOutputPrincipal.DisconnectIfConnected();

  master->mDuration.DisconnectAll();
  master->mCurrentPosition.DisconnectAll();
  master->mIsAudioDataAudible.DisconnectAll();

  // Shut down the watch manager to stop further notifications.
  master->mWatchManager.Shutdown();

  return Reader()->Shutdown()->Then(OwnerThread(), __func__, master,
                                    &MediaDecoderStateMachine::FinishShutdown,
                                    &MediaDecoderStateMachine::FinishShutdown);
}

#define INIT_WATCHABLE(name, val) name(val, "MediaDecoderStateMachine::" #name)
#define INIT_MIRROR(name, val) \
  name(mTaskQueue, val, "MediaDecoderStateMachine::" #name " (Mirror)")
#define INIT_CANONICAL(name, val) \
  name(mTaskQueue, val, "MediaDecoderStateMachine::" #name " (Canonical)")

MediaDecoderStateMachine::MediaDecoderStateMachine(MediaDecoder* aDecoder,
                                                   MediaFormatReader* aReader)
    : MediaDecoderStateMachineBase(aDecoder, aReader),
      mWatchManager(this, mTaskQueue),
      mDispatchedStateMachine(false),
      mDelayedScheduler(mTaskQueue, true /*aFuzzy*/),
      mCurrentFrameID(0),
      mAmpleAudioThreshold(detail::AMPLE_AUDIO_THRESHOLD),
      mVideoDecodeSuspended(false),
      mVideoDecodeSuspendTimer(mTaskQueue),
      mVideoDecodeMode(VideoDecodeMode::Normal),
      mIsMSE(aDecoder->IsMSE()),
      mShouldResistFingerprinting(aDecoder->ShouldResistFingerprinting()),
      mSeamlessLoopingAllowed(false),
      mTotalBufferingDuration(TimeDuration::Zero()),
      INIT_MIRROR(mStreamName, nsAutoString()),
      INIT_MIRROR(mSinkDevice, nullptr),
      INIT_MIRROR(mOutputCaptureInfo,
                  MediaDecoder::OutputCaptureInfo(
                      MediaDecoder::OutputCaptureState::None)),
      INIT_MIRROR(mOutputTracks, nsTArray<RefPtr<ProcessedMediaTrack>>()),
      INIT_MIRROR(mOutputPrincipal, PRINCIPAL_HANDLE_NONE),
      INIT_CANONICAL(mCanonicalOutputPrincipal, PRINCIPAL_HANDLE_NONE),
      mShuttingDown(false),
      mInitialized(false) {
  MOZ_COUNT_CTOR(MediaDecoderStateMachine);
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");

  DDLINKCHILD("reader", aReader);
}

#undef INIT_WATCHABLE
#undef INIT_MIRROR
#undef INIT_CANONICAL

MediaDecoderStateMachine::~MediaDecoderStateMachine() {
  MOZ_ASSERT(NS_IsMainThread(), "Should be on main thread.");
  MOZ_COUNT_DTOR(MediaDecoderStateMachine);
}

void MediaDecoderStateMachine::InitializationTask(MediaDecoder* aDecoder) {
  AUTO_PROFILER_LABEL("MediaDecoderStateMachine::InitializationTask",
                      MEDIA_PLAYBACK);
  MOZ_ASSERT(OnTaskQueue());

  MediaDecoderStateMachineBase::InitializationTask(aDecoder);

  // Initialize watchers.
  mWatchManager.Watch(mStreamName,
                      &MediaDecoderStateMachine::StreamNameChanged);
  mWatchManager.Watch(mOutputCaptureInfo,
                      &MediaDecoderStateMachine::UpdateOutputCaptured);
  mWatchManager.Watch(mOutputTracks,
                      &MediaDecoderStateMachine::UpdateOutputCaptured);
  mWatchManager.Watch(mOutputPrincipal,
                      &MediaDecoderStateMachine::OutputPrincipalChanged);

  mMediaSink = CreateMediaSink();
  mInitialized = true;

  MOZ_ASSERT(!mStateObj);
  auto* s = new DecodeMetadataState(this);
  mStateObj.reset(s);
  s->Enter();
}

void MediaDecoderStateMachine::AudioAudibleChanged(bool aAudible) {
  mIsAudioDataAudible = aAudible;
}

void MediaDecoderStateMachine::OnPlaybackRateFallback() {
  MOZ_ASSERT(OnTaskQueue());
  mOnPlaybackEvent.Notify(MediaPlaybackEvent::PlaybackRateFallback);
}

already_AddRefed<MediaSink> MediaDecoderStateMachine::CreateAudioSink() {
  if (mOutputCaptureInfo.Ref().mState !=
      MediaDecoder::OutputCaptureState::None) {
    const auto& outputCaptureInfo = mOutputCaptureInfo.Ref();
    RefPtr stream = MakeRefPtr<DecodedStream>(
        OwnerThread(),
        outputCaptureInfo.mState == MediaDecoder::OutputCaptureState::Capture
            ? outputCaptureInfo.mDummyTrack
            : nullptr,
        mOutputTracks, CanonicalOutputPrincipal(), mVolume, mPlaybackRate,
        mPreservesPitch, outputCaptureInfo.mShouldConfigAudioOutput,
        outputCaptureInfo.mDevice, mAudioQueue, mVideoQueue);
    mAudibleListener.DisconnectIfExists();
    mAudibleListener = stream->AudibleEvent().Connect(
        OwnerThread(), this, &MediaDecoderStateMachine::AudioAudibleChanged);
    mPlaybackRateFallbackListener.DisconnectIfExists();
    mPlaybackRateFallbackListener = stream->PlaybackRateFallbackEvent().Connect(
        OwnerThread(), this, &MediaDecoderStateMachine::OnPlaybackRateFallback);
    return stream.forget();
  }

  auto audioSinkCreator = [s = RefPtr<MediaDecoderStateMachine>(this), this]() {
    MOZ_ASSERT(OnTaskQueue());
    auto audioSink = std::make_unique<AudioSink>(
        mTaskQueue, mAudioQueue, Info().mAudio, mShouldResistFingerprinting);
    mAudibleListener.DisconnectIfExists();
    mAudibleListener = audioSink->AudibleEvent().Connect(
        mTaskQueue, this, &MediaDecoderStateMachine::AudioAudibleChanged);
    return audioSink;
  };
  return MakeAndAddRef<AudioSinkWrapper>(
      mTaskQueue, mAudioQueue, std::move(audioSinkCreator), mVolume,
      mPlaybackRate, mPreservesPitch, mSinkDevice.Ref());
}

already_AddRefed<MediaSink> MediaDecoderStateMachine::CreateMediaSink() {
  MOZ_ASSERT(OnTaskQueue());
  RefPtr<MediaSink> audioSink = CreateAudioSink();
  RefPtr<MediaSink> mediaSink = new VideoSink(
      mTaskQueue, audioSink, mVideoQueue, mVideoFrameContainer, *mFrameStats,
      StaticPrefs::media_video_queue_send_to_compositor_size());
  if (mSecondaryVideoContainer.Ref()) {
    mediaSink->SetSecondaryVideoContainer(mSecondaryVideoContainer.Ref());
  }
  return mediaSink.forget();
}

TimeUnit MediaDecoderStateMachine::GetDecodedAudioDuration() const {
  MOZ_ASSERT(OnTaskQueue());
  if (mMediaSink->IsStarted()) {
    return mMediaSink->UnplayedDuration(TrackInfo::kAudioTrack) +
           TimeUnit::FromMicroseconds(AudioQueue().Duration());
  }
  // MediaSink not started. All audio samples are in the queue.
  return TimeUnit::FromMicroseconds(AudioQueue().Duration());
}

bool MediaDecoderStateMachine::HaveEnoughDecodedAudio() const {
  MOZ_ASSERT(OnTaskQueue());
  auto ampleAudio = mAmpleAudioThreshold.MultDouble(mPlaybackRate);
  return AudioQueue().GetSize() > 0 && GetDecodedAudioDuration() >= ampleAudio;
}

bool MediaDecoderStateMachine::HaveEnoughDecodedVideo() const {
  MOZ_ASSERT(OnTaskQueue());
  return static_cast<double>(VideoQueue().GetSize()) >=
             GetAmpleVideoFrames() * mPlaybackRate + 1 &&
         IsVideoDataEnoughComparedWithAudio();
}

bool MediaDecoderStateMachine::IsVideoDataEnoughComparedWithAudio() const {
  // HW decoding is usually fast enough and we don't need to worry about its
  // speed.
  // TODO : we can consider whether we need to enable this on other HW decoding
  // except VAAPI. When enabling VAAPI on Linux, ffmpeg is not able to store too
  // many frames because it has a limitation of amount of stored video frames.
  // See bug1716638 and 1718309.
  if (mReader->VideoIsHardwareAccelerated()) {
    return true;
  }
  // In extreme situations (e.g. 4k+ video without hardware acceleration), the
  // video decoding will be much slower than audio. So for 4K+ video, we want to
  // consider audio decoding speed as well in order to reduce frame drops. This
  // check tries to keep the decoded video buffered as much as audio.
  if (HasAudio() && Info().mVideo.mImage.width >= 3840 &&
      Info().mVideo.mImage.height >= 2160) {
    return VideoQueue().Duration() >= AudioQueue().Duration();
  }
  // For non-4k video, the video decoding is usually really fast so we won't
  // need to consider audio decoding speed to store extra frames.
  return true;
}

void MediaDecoderStateMachine::PushAudio(AudioData* aSample) {
  MOZ_ASSERT(OnTaskQueue());
  MOZ_ASSERT(aSample);
  AudioQueue().Push(aSample);
  PROFILER_MARKER("MDSM::PushAudio", MEDIA_PLAYBACK, {}, MediaSampleMarker,
                  aSample->mTime.ToMicroseconds(),
                  aSample->GetEndTime().ToMicroseconds(),
                  AudioQueue().GetSize());
}

void MediaDecoderStateMachine::PushVideo(VideoData* aSample) {
  MOZ_ASSERT(OnTaskQueue());
  MOZ_ASSERT(aSample);
  aSample->mFrameID = ++mCurrentFrameID;
  VideoQueue().Push(aSample);
  PROFILER_MARKER("MDSM::PushVideo", MEDIA_PLAYBACK, {}, MediaSampleMarker,
                  aSample->mTime.ToMicroseconds(),
                  aSample->GetEndTime().ToMicroseconds(),
                  VideoQueue().GetSize());
}

void MediaDecoderStateMachine::OnAudioPopped(const RefPtr<AudioData>& aSample) {
  MOZ_ASSERT(OnTaskQueue());
  mPlaybackOffset = std::max(mPlaybackOffset, aSample->mOffset);
}

void MediaDecoderStateMachine::OnVideoPopped(const RefPtr<VideoData>& aSample) {
  MOZ_ASSERT(OnTaskQueue());
  mPlaybackOffset = std::max(mPlaybackOffset, aSample->mOffset);
}

bool MediaDecoderStateMachine::IsAudioDecoding() {
  MOZ_ASSERT(OnTaskQueue());
  return HasAudio() && !AudioQueue().IsFinished();
}

bool MediaDecoderStateMachine::IsVideoDecoding() {
  MOZ_ASSERT(OnTaskQueue());
  return HasVideo() && !VideoQueue().IsFinished();
}

bool MediaDecoderStateMachine::IsPlaying() const {
  MOZ_ASSERT(OnTaskQueue());
  return mMediaSink->IsPlaying();
}

void MediaDecoderStateMachine::SetMediaNotSeekable() { mMediaSeekable = false; }

nsresult MediaDecoderStateMachine::Init(MediaDecoder* aDecoder) {
  MOZ_ASSERT(NS_IsMainThread());

  nsresult rv = MediaDecoderStateMachineBase::Init(aDecoder);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // Connect mirrors.
  aDecoder->CanonicalStreamName().ConnectMirror(&mStreamName);
  aDecoder->CanonicalSinkDevice().ConnectMirror(&mSinkDevice);
  aDecoder->CanonicalOutputCaptureInfo().ConnectMirror(&mOutputCaptureInfo);
  aDecoder->CanonicalOutputTracks().ConnectMirror(&mOutputTracks);
  aDecoder->CanonicalOutputPrincipal().ConnectMirror(&mOutputPrincipal);

  mAudioQueueListener = AudioQueue().PopFrontEvent().Connect(
      mTaskQueue, this, &MediaDecoderStateMachine::OnAudioPopped);
  mVideoQueueListener = VideoQueue().PopFrontEvent().Connect(
      mTaskQueue, this, &MediaDecoderStateMachine::OnVideoPopped);
  mOnMediaNotSeekable = mReader->OnMediaNotSeekable().Connect(
      OwnerThread(), this, &MediaDecoderStateMachine::SetMediaNotSeekable);

  return NS_OK;
}

void MediaDecoderStateMachine::StopPlayback(MediaSink::StopReason aReason) {
  MOZ_ASSERT(OnTaskQueue());
  LOG("StopPlayback(reason={})", MediaSink::EnumValueToString(aReason));

  if (IsPlaying()) {
    mOnPlaybackEvent.Notify(MediaPlaybackEvent{
        MediaPlaybackEvent::PlaybackStopped, mPlaybackOffset});
    mMediaSink->SetPlaying(false, aReason);
    MOZ_ASSERT(!IsPlaying());
  }
}

void MediaDecoderStateMachine::MaybeStartPlayback() {
  MOZ_ASSERT(OnTaskQueue());
  // Should try to start playback only after decoding first frames.
  if (!mSentFirstFrameLoadedEvent) {
    LOG("MaybeStartPlayback: Not starting playback before loading first frame");
    return;
  }

  if (IsPlaying()) {
    // Logging this case is really spammy - don't do it.
    return;
  }

  if (mIsMediaSinkSuspended) {
    LOG("MaybeStartPlayback: Not starting playback when sink is suspended");
    return;
  }

  if (mPlayState != MediaDecoder::PLAY_STATE_PLAYING) {
    LOG("MaybeStartPlayback: Not starting playback [mPlayState={}]",
        static_cast<int>(mPlayState.Ref()));
    return;
  }

  LOG("MaybeStartPlayback() starting playback");
  StartMediaSink();

  if (!IsPlaying()) {
    mMediaSink->SetPlaying(true);
    MOZ_ASSERT(IsPlaying());
  }

  mOnPlaybackEvent.Notify(
      MediaPlaybackEvent{MediaPlaybackEvent::PlaybackStarted, mPlaybackOffset});
}

void MediaDecoderStateMachine::UpdatePlaybackPositionInternal(
    const TimeUnit& aTime) {
  MOZ_ASSERT(OnTaskQueue());
  LOGV("UpdatePlaybackPositionInternal({})", aTime.ToMicroseconds());

  // Ensure the position has a precision that matches other TimeUnit such as
  // buffering ranges and duration.
  mCurrentPosition = aTime.ToBase(1000000);
  NS_ASSERTION(mCurrentPosition.Ref() >= TimeUnit::Zero(),
               "CurrentTime should be positive!");
  if (mDuration.Ref().ref() < mCurrentPosition.Ref()) {
    mDuration = Some(mCurrentPosition.Ref());
    DDLOG(DDLogCategory::Property, "duration_us",
          mDuration.Ref()->ToMicroseconds());
  }
}

void MediaDecoderStateMachine::UpdatePlaybackPosition(const TimeUnit& aTime) {
  MOZ_ASSERT(OnTaskQueue());
  UpdatePlaybackPositionInternal(aTime);

  bool fragmentEnded =
      mFragmentEndTime.IsValid() && GetMediaTime() >= mFragmentEndTime;
  mMetadataManager.DispatchMetadataIfNeeded(aTime);

  if (fragmentEnded) {
    StopPlayback();
  }
}

/* static */ const char* MediaDecoderStateMachine::ToStateStr(State aState) {
  switch (aState) {
    case DECODER_STATE_DECODING_METADATA:
      return "DECODING_METADATA";
    case DECODER_STATE_DORMANT:
      return "DORMANT";
    case DECODER_STATE_DECODING_FIRSTFRAME:
      return "DECODING_FIRSTFRAME";
    case DECODER_STATE_DECODING:
      return "DECODING";
    case DECODER_STATE_SEEKING_ACCURATE:
      return "SEEKING_ACCURATE";
    case DECODER_STATE_SEEKING_FROMDORMANT:
      return "SEEKING_FROMDORMANT";
    case DECODER_STATE_SEEKING_NEXTFRAMESEEKING:
      return "DECODER_STATE_SEEKING_NEXTFRAMESEEKING";
    case DECODER_STATE_SEEKING_VIDEOONLY:
      return "SEEKING_VIDEOONLY";
    case DECODER_STATE_BUFFERING:
      return "BUFFERING";
    case DECODER_STATE_COMPLETED:
      return "COMPLETED";
    case DECODER_STATE_SHUTDOWN:
      return "SHUTDOWN";
    case DECODER_STATE_LOOPING_DECODING:
      return "LOOPING_DECODING";
    default:
      MOZ_ASSERT_UNREACHABLE("Invalid state.");
  }
  return "UNKNOWN";
}

const char* MediaDecoderStateMachine::ToStateStr() {
  MOZ_ASSERT(OnTaskQueue());
  return ToStateStr(mStateObj->GetState());
}

void MediaDecoderStateMachine::VolumeChanged() {
  AUTO_PROFILER_LABEL("MediaDecoderStateMachine::VolumeChanged",
                      MEDIA_PLAYBACK);
  PROFILER_MARKER_TEXT("MDSM::VolumeChanged", MEDIA_PLAYBACK, {},
                       nsPrintfCString("%f", mVolume.Ref()));
  MOZ_ASSERT(OnTaskQueue());
  mMediaSink->SetVolume(mVolume);
}

RefPtr<ShutdownPromise> MediaDecoderStateMachine::Shutdown() {
  AUTO_PROFILER_LABEL("MediaDecoderStateMachine::Shutdown", MEDIA_PLAYBACK);
  PROFILER_MARKER_UNTYPED("MDSM::Shutdown", MEDIA_PLAYBACK);
  MOZ_ASSERT(OnTaskQueue());
  mShuttingDown = true;
  return mStateObj->HandleShutdown();
}

void MediaDecoderStateMachine::PlayStateChanged() {
  AUTO_PROFILER_LABEL("MediaDecoderStateMachine::PlayStateChanged",
                      MEDIA_PLAYBACK);
  PROFILER_MARKER_TEXT(
      "MDSM::PlayStateChanged", MEDIA_PLAYBACK, {},
      nsPrintfCString("%s", MediaDecoder::EnumValueToString(mPlayState.Ref())));
  MOZ_ASSERT(OnTaskQueue());

  if (mPlayState != MediaDecoder::PLAY_STATE_PLAYING) {
    CancelSuspendTimer();
    // Leaving the playing state cancels any pending warm seek-resume, so a
    // later unrelated start does not inherit the reduced preroll.
    mStartSinkAfterWarmSeek = false;
  } else if (mMinimizePreroll) {
    // Once we start playing, we don't want to minimize our prerolling, as we
    // assume the user is likely to want to keep playing in future. This needs
    // to happen before we invoke StartDecoding().
    mMinimizePreroll = false;
  }

  mStateObj->HandlePlayStateChanged(mPlayState);
}

void MediaDecoderStateMachine::SetVideoDecodeMode(VideoDecodeMode aMode) {
  MOZ_ASSERT(NS_IsMainThread());
  nsCOMPtr<nsIRunnable> r = NewRunnableMethod<VideoDecodeMode>(
      "MediaDecoderStateMachine::SetVideoDecodeModeInternal", this,
      &MediaDecoderStateMachine::SetVideoDecodeModeInternal, aMode);
  OwnerThread()->DispatchStateChange(r.forget());
}

void MediaDecoderStateMachine::SetVideoDecodeModeInternal(
    VideoDecodeMode aMode) {
  MOZ_ASSERT(OnTaskQueue());

  LOG("SetVideoDecodeModeInternal(), VideoDecodeMode=({}->{}), "
      "mVideoDecodeSuspended={}",
      mVideoDecodeMode == VideoDecodeMode::Normal ? "Normal" : "Suspend",
      aMode == VideoDecodeMode::Normal ? "Normal" : "Suspend",
      mVideoDecodeSuspended ? 'T' : 'F');

  // Should not suspend decoding if we don't turn on the pref.
  if (!StaticPrefs::media_suspend_background_video_enabled() &&
      aMode == VideoDecodeMode::Suspend) {
    LOG("SetVideoDecodeModeInternal(), early return because preference off and "
        "set to Suspend");
    return;
  }

  if (aMode == mVideoDecodeMode) {
    LOG("SetVideoDecodeModeInternal(), early return because the mode does not "
        "change");
    return;
  }

  // Set new video decode mode.
  mVideoDecodeMode = aMode;

  // Start timer to trigger suspended video decoding.
  if (mVideoDecodeMode == VideoDecodeMode::Suspend) {
    TimeStamp target = TimeStamp::Now() + SuspendBackgroundVideoDelay();

    RefPtr<MediaDecoderStateMachine> self = this;
    mVideoDecodeSuspendTimer.Ensure(
        target, [=]() { self->OnSuspendTimerResolved(); },
        []() { MOZ_DIAGNOSTIC_CRASH("SetVideoDecodeModeInternal reject"); });
    mOnPlaybackEvent.Notify(MediaPlaybackEvent::StartVideoSuspendTimer);
    return;
  }

  // Resuming from suspended decoding

  // If suspend timer exists, destroy it.
  CancelSuspendTimer();

  if (mVideoDecodeSuspended) {
    auto target = mMediaSink->IsStarted() ? GetClock() : GetMediaTime();
    AdjustByLooping(target);
    mStateObj->HandleResumeVideoDecoding(target + detail::RESUME_VIDEO_PREMIUM);
  }
}

void MediaDecoderStateMachine::BufferedRangeUpdated() {
  AUTO_PROFILER_LABEL("MediaDecoderStateMachine::BufferedRangeUpdated",
                      MEDIA_PLAYBACK);
  MOZ_ASSERT(OnTaskQueue());

  // Buffered data doesn't establish a duration for an unbounded resource.
  if (!mIsMSE && IsLiveStream() &&
      (mDuration.Ref().isNothing() || mDuration.Ref()->IsInfinite())) {
    return;
  }

  // A duration estimate updated during playback can lag behind the end of
  // playable data when downloading is faster than playback. Update mDuration
  // as new data is downloaded so it includes buffered frames that haven't
  // been played yet.
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
  if ((mDuration.Ref().isNothing() || mDuration.Ref()->IsInfinite() ||
       end > mDuration.Ref().ref()) &&
      end.IsPositiveOrZero()) {
    nsPrintfCString msg{
        "duration:%" PRId64 "->%" PRId64,
        mDuration.Ref().isNothing() ? 0 : mDuration.Ref()->ToMicroseconds(),
        end.ToMicroseconds()};
    PROFILER_MARKER_TEXT("MDSM::BufferedRangeUpdated", MEDIA_PLAYBACK, {}, msg);
    LOG("{}", msg.get());
    mDuration = Some(end);
    DDLOG(DDLogCategory::Property, "duration_us",
          mDuration.Ref()->ToMicroseconds());
  }
}

RefPtr<MediaDecoder::SeekPromise> MediaDecoderStateMachine::Seek(
    const SeekTarget& aTarget) {
  AUTO_PROFILER_LABEL("MediaDecoderStateMachine::Seek", MEDIA_PLAYBACK);
  PROFILER_MARKER_UNTYPED("MDSM::Seek", MEDIA_PLAYBACK);
  MOZ_ASSERT(OnTaskQueue());

  // We need to be able to seek in some way
  if (!mMediaSeekable && !mMediaSeekableOnlyInBufferedRanges) {
    LOGW("Seek() should not be called on a non-seekable media");
    return MediaDecoder::SeekPromise::CreateAndReject(/* aRejectValue = */ true,
                                                      __func__);
  }

  if (aTarget.IsNextFrame() && !HasVideo()) {
    LOGW("Ignore a NextFrameSeekTask on a media file without video track.");
    return MediaDecoder::SeekPromise::CreateAndReject(/* aRejectValue = */ true,
                                                      __func__);
  }

  MOZ_ASSERT(mDuration.Ref().isSome(), "We should have got duration already");

  return mStateObj->HandleSeek(aTarget);
}

void MediaDecoderStateMachine::StopMediaSink(MediaSink::StopReason aReason) {
  MOZ_ASSERT(OnTaskQueue());
  if (mMediaSink->IsStarted()) {
    LOG("Stop MediaSink (reason={})", MediaSink::EnumValueToString(aReason));
    mMediaSink->Stop(aReason);
    mMediaSinkAudioEndedPromise.DisconnectIfExists();
    mMediaSinkVideoEndedPromise.DisconnectIfExists();
  }
}

void MediaDecoderStateMachine::RequestAudioData() {
  AUTO_PROFILER_LABEL("MediaDecoderStateMachine::RequestAudioData",
                      MEDIA_PLAYBACK);
  MOZ_ASSERT(OnTaskQueue());
  MOZ_ASSERT(IsAudioDecoding());
  MOZ_ASSERT(!IsRequestingAudioData());
  MOZ_ASSERT(!IsWaitingAudioData());
  LOGV("Queueing audio task - queued={}, decoder-queued={}",
       AudioQueue().GetSize(), mReader->SizeOfAudioQueueInFrames());

  PerformanceRecorder<PlaybackStage> perfRecorder(MediaStage::RequestData);
  RefPtr<MediaDecoderStateMachine> self = this;
  mReader->RequestAudioData()
      ->Then(
          OwnerThread(), __func__,
          [this, self, perfRecorder(std::move(perfRecorder))](
              const RefPtr<AudioData>& aAudio) mutable {
            perfRecorder.Record();
            AUTO_PROFILER_LABEL(
                "MediaDecoderStateMachine::RequestAudioData:Resolved",
                MEDIA_PLAYBACK);
            MOZ_ASSERT(aAudio);
            mAudioDataRequest.Complete();
            // audio->GetEndTime() is not always mono-increasing in chained
            // ogg.
            mDecodedAudioEndTime =
                std::max(aAudio->GetEndTime(), mDecodedAudioEndTime);
            LOGV("OnAudioDecoded [{},{}]", aAudio->mTime.ToMicroseconds(),
                 aAudio->GetEndTime().ToMicroseconds());
            mStateObj->HandleAudioDecoded(aAudio);
          },
          [this, self](const MediaResult& aError) {
            AUTO_PROFILER_LABEL(
                "MediaDecoderStateMachine::RequestAudioData:Rejected",
                MEDIA_PLAYBACK);
            LOGV("OnAudioNotDecoded ErrorName={} Message={}",
                 aError.ErrorName().get(), aError.Message().get());
            mAudioDataRequest.Complete();
            switch (aError.Code()) {
              case NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA:
                mStateObj->HandleWaitingForAudio();
                break;
              case NS_ERROR_DOM_MEDIA_CANCELED:
                mStateObj->HandleAudioCanceled();
                break;
              case NS_ERROR_DOM_MEDIA_END_OF_STREAM:
                mStateObj->HandleEndOfAudio();
                break;
              default:
                DecodeError(aError);
            }
          })
      ->Track(mAudioDataRequest);
}

void MediaDecoderStateMachine::RequestVideoData(
    const media::TimeUnit& aCurrentTime, bool aRequestNextKeyFrame) {
  AUTO_PROFILER_LABEL("MediaDecoderStateMachine::RequestVideoData",
                      MEDIA_PLAYBACK);
  MOZ_ASSERT(OnTaskQueue());
  MOZ_ASSERT(IsVideoDecoding());
  MOZ_ASSERT(!IsRequestingVideoData());
  MOZ_ASSERT(!IsWaitingVideoData());
  LOGV(
      "Queueing video task - queued={}, decoder-queued={}, stime={}, "
      "by-pass-skip={}",
      VideoQueue().GetSize(), mReader->SizeOfVideoQueueInFrames(),
      aCurrentTime.ToMicroseconds(), mBypassingSkipToNextKeyFrameCheck);

  PerformanceRecorder<PlaybackStage> perfRecorder(MediaStage::RequestData,
                                                  Info().mVideo.mImage.height);
  RefPtr<MediaDecoderStateMachine> self = this;
  mReader
      ->RequestVideoData(
          mBypassingSkipToNextKeyFrameCheck ? media::TimeUnit() : aCurrentTime,
          mBypassingSkipToNextKeyFrameCheck ? false : aRequestNextKeyFrame)
      ->Then(
          OwnerThread(), __func__,
          [this, self, perfRecorder(std::move(perfRecorder))](
              const RefPtr<VideoData>& aVideo) mutable {
            perfRecorder.Record();
            AUTO_PROFILER_LABEL(
                "MediaDecoderStateMachine::RequestVideoData:Resolved",
                MEDIA_PLAYBACK);
            MOZ_ASSERT(aVideo);
            mVideoDataRequest.Complete();
            // Handle abnormal or negative timestamps.
            mDecodedVideoEndTime =
                std::max(mDecodedVideoEndTime, aVideo->GetEndTime());
            LOGV("OnVideoDecoded [{},{}]", aVideo->mTime.ToMicroseconds(),
                 aVideo->GetEndTime().ToMicroseconds());
            mStateObj->HandleVideoDecoded(aVideo);
          },
          [this, self](const MediaResult& aError) {
            AUTO_PROFILER_LABEL(
                "MediaDecoderStateMachine::RequestVideoData:Rejected",
                MEDIA_PLAYBACK);
            LOGV("OnVideoNotDecoded ErrorName={} Message={}",
                 aError.ErrorName().get(), aError.Message().get());
            mVideoDataRequest.Complete();
            switch (aError.Code()) {
              case NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA:
                mStateObj->HandleWaitingForVideo();
                break;
              case NS_ERROR_DOM_MEDIA_CANCELED:
                mStateObj->HandleVideoCanceled();
                break;
              case NS_ERROR_DOM_MEDIA_END_OF_STREAM:
                mStateObj->HandleEndOfVideo();
                break;
              default:
                DecodeError(aError);
            }
          })
      ->Track(mVideoDataRequest);
}

void MediaDecoderStateMachine::WaitForData(MediaData::Type aType) {
  MOZ_ASSERT(OnTaskQueue());
  MOZ_ASSERT(aType == MediaData::Type::AUDIO_DATA ||
             aType == MediaData::Type::VIDEO_DATA);
  LOG("{}: {}", __func__, MediaData::EnumValueToString(aType));
  RefPtr<MediaDecoderStateMachine> self = this;
  if (aType == MediaData::Type::AUDIO_DATA) {
    mReader->WaitForData(MediaData::Type::AUDIO_DATA)
        ->Then(
            OwnerThread(), __func__,
            [self](MediaData::Type aType) {
              AUTO_PROFILER_LABEL(
                  "MediaDecoderStateMachine::WaitForData:AudioResolved",
                  MEDIA_PLAYBACK);
              self->mAudioWaitRequest.Complete();
              MOZ_ASSERT(aType == MediaData::Type::AUDIO_DATA);
              self->mStateObj->HandleAudioWaited(aType);
            },
            [self](const WaitForDataRejectValue& aRejection) {
              AUTO_PROFILER_LABEL(
                  "MediaDecoderStateMachine::WaitForData:AudioRejected",
                  MEDIA_PLAYBACK);
              self->mAudioWaitRequest.Complete();
              self->DecodeError(NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA);
            })
        ->Track(mAudioWaitRequest);
  } else {
    mReader->WaitForData(MediaData::Type::VIDEO_DATA)
        ->Then(
            OwnerThread(), __func__,
            [self, this](MediaData::Type aType) {
              AUTO_PROFILER_LABEL(
                  "MediaDecoderStateMachine::WaitForData:VideoResolved",
                  MEDIA_PLAYBACK);
              self->mVideoWaitRequest.Complete();
              MOZ_ASSERT(aType == MediaData::Type::VIDEO_DATA);
              LOG("WaitForData::VideoResolved");
              self->mStateObj->HandleVideoWaited(aType);
            },
            [self, this](const WaitForDataRejectValue& aRejection) {
              AUTO_PROFILER_LABEL(
                  "MediaDecoderStateMachine::WaitForData:VideoRejected",
                  MEDIA_PLAYBACK);
              self->mVideoWaitRequest.Complete();
              LOG("WaitForData::VideoRejected");
              self->DecodeError(NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA);
            })
        ->Track(mVideoWaitRequest);
  }
}

nsresult MediaDecoderStateMachine::StartMediaSink() {
  MOZ_ASSERT(OnTaskQueue());

  if (mMediaSink->IsStarted()) {
    return NS_OK;
  }

  mAudioCompleted = false;
  const auto startTime = GetMediaTime();
  const MediaSink::StartType startType = mStartSinkAfterWarmSeek
                                             ? MediaSink::StartType::SeekResume
                                             : MediaSink::StartType::Initial;
  mStartSinkAfterWarmSeek = false;
  LOG("StartMediaSink, mediaTime={}, startType={}", startTime.ToMicroseconds(),
      MediaSink::EnumValueToString(startType));
  nsresult rv = mMediaSink->Start(startTime, Info(), startType);
  StreamNameChanged();

  auto videoPromise = mMediaSink->OnEnded(TrackInfo::kVideoTrack);
  auto audioPromise = mMediaSink->OnEnded(TrackInfo::kAudioTrack);

  if (audioPromise) {
    audioPromise
        ->Then(OwnerThread(), __func__, this,
               &MediaDecoderStateMachine::OnMediaSinkAudioComplete,
               &MediaDecoderStateMachine::OnMediaSinkAudioError)
        ->Track(mMediaSinkAudioEndedPromise);
  }
  if (videoPromise) {
    videoPromise
        ->Then(OwnerThread(), __func__, this,
               &MediaDecoderStateMachine::OnMediaSinkVideoComplete,
               &MediaDecoderStateMachine::OnMediaSinkVideoError)
        ->Track(mMediaSinkVideoEndedPromise);
  }
  // Remember the initial offset when playback starts. This will be used
  // to calculate the rate at which bytes are consumed as playback moves on.
  RefPtr<MediaData> sample = mAudioQueue.PeekFront();
  mPlaybackOffset = sample ? sample->mOffset : 0;
  sample = mVideoQueue.PeekFront();
  if (sample && sample->mOffset > mPlaybackOffset) {
    mPlaybackOffset = sample->mOffset;
  }
  return rv;
}

bool MediaDecoderStateMachine::HasLowDecodedAudio() {
  MOZ_ASSERT(OnTaskQueue());
  return IsAudioDecoding() &&
         GetDecodedAudioDuration() <
             EXHAUSTED_DATA_MARGIN.MultDouble(mPlaybackRate);
}

bool MediaDecoderStateMachine::HasLowDecodedVideo() {
  MOZ_ASSERT(OnTaskQueue());
  return IsVideoDecoding() &&
         VideoQueue().GetSize() <
             static_cast<size_t>(floorl(LOW_VIDEO_FRAMES * mPlaybackRate));
}

bool MediaDecoderStateMachine::HasLowDecodedData() {
  MOZ_ASSERT(OnTaskQueue());
  MOZ_ASSERT(mReader->UseBufferingHeuristics());
  return HasLowDecodedAudio() || HasLowDecodedVideo();
}

bool MediaDecoderStateMachine::OutOfDecodedAudio() {
  MOZ_ASSERT(OnTaskQueue());
  return IsAudioDecoding() && !AudioQueue().IsFinished() &&
         AudioQueue().GetSize() == 0 &&
         !mMediaSink->HasUnplayedFrames(TrackInfo::kAudioTrack);
}

bool MediaDecoderStateMachine::HasLowBufferedData() {
  MOZ_ASSERT(OnTaskQueue());
  return HasLowBufferedData(detail::LOW_BUFFER_THRESHOLD);
}

bool MediaDecoderStateMachine::HasLowBufferedData(const TimeUnit& aThreshold) {
  MOZ_ASSERT(OnTaskQueue());

  // If we don't have a duration, mBuffered is probably not going to have
  // a useful buffered range. Return false here so that we don't get stuck in
  // buffering mode for live streams.
  if (Duration().IsInfinite()) {
    return false;
  }

  if (mBuffered.Ref().IsInvalid()) {
    return false;
  }

  // We are never low in decoded data when we don't have audio/video or have
  // decoded all audio/video samples.
  TimeUnit endOfDecodedVideo = (HasVideo() && !VideoQueue().IsFinished())
                                   ? mDecodedVideoEndTime
                                   : TimeUnit::FromNegativeInfinity();
  TimeUnit endOfDecodedAudio = (HasAudio() && !AudioQueue().IsFinished())
                                   ? mDecodedAudioEndTime
                                   : TimeUnit::FromNegativeInfinity();

  auto endOfDecodedData = std::max(endOfDecodedVideo, endOfDecodedAudio);
  if (Duration() < endOfDecodedData) {
    // Our duration is not up to date. No point buffering.
    return false;
  }

  if (endOfDecodedData.IsInfinite()) {
    // Have decoded all samples. No point buffering.
    return false;
  }

  auto start = endOfDecodedData;
  auto end = std::min(GetMediaTime() + aThreshold, Duration());
  if (start >= end) {
    // Duration of decoded samples is greater than our threshold.
    return false;
  }
  media::TimeInterval interval(start, end);
  return !mBuffered.Ref().Contains(interval);
}

void MediaDecoderStateMachine::EnqueueFirstFrameLoadedEvent() {
  MOZ_ASSERT(OnTaskQueue());
  // Track value of mSentFirstFrameLoadedEvent from before updating it
  bool firstFrameBeenLoaded = mSentFirstFrameLoadedEvent;
  mSentFirstFrameLoadedEvent = true;
  MediaDecoderEventVisibility visibility =
      firstFrameBeenLoaded ? MediaDecoderEventVisibility::Suppressed
                           : MediaDecoderEventVisibility::Observable;
  mFirstFrameLoadedEvent.Notify(std::make_unique<MediaInfo>(Info()),
                                visibility);
}

void MediaDecoderStateMachine::FinishDecodeFirstFrame() {
  MOZ_ASSERT(OnTaskQueue());
  MOZ_ASSERT(!mSentFirstFrameLoadedEvent);
  LOG("FinishDecodeFirstFrame");

  mMediaSink->Redraw(Info().mVideo);
  mReader->GetSendToCompositorSize().apply([self = RefPtr{this}](uint32_t x) {
    self->mMediaSink->SetVideoQueueSendToCompositorSize(x);
  });

  LOG("Media duration {}, mediaSeekable={}", Duration().ToMicroseconds(),
      mMediaSeekable);

  // Get potentially updated metadata
  mReader->ReadUpdatedMetadata(mInfo.ptr());

  EnqueueFirstFrameLoadedEvent();
}

RefPtr<ShutdownPromise> MediaDecoderStateMachine::FinishShutdown() {
  AUTO_PROFILER_LABEL("MediaDecoderStateMachine::FinishShutdown",
                      MEDIA_PLAYBACK);
  MOZ_ASSERT(OnTaskQueue());
  LOG("Shutting down state machine task queue");
  return OwnerThread()->BeginShutdown();
}

void MediaDecoderStateMachine::RunStateMachine() {
  MOZ_ASSERT(OnTaskQueue());
  AUTO_PROFILER_LABEL("MediaDecoderStateMachine::RunStateMachine",
                      MEDIA_PLAYBACK);
  mDelayedScheduler.Reset();  // Must happen on state machine task queue.
  mDispatchedStateMachine = false;
  mStateObj->Step();
}

void MediaDecoderStateMachine::ResetDecode(const TrackSet& aTracks) {
  MOZ_ASSERT(OnTaskQueue());
  LOG("MediaDecoderStateMachine::Reset");

  // Assert that aTracks specifies to reset the video track because we
  // don't currently support resetting just the audio track.
  MOZ_ASSERT(aTracks.contains(TrackInfo::kVideoTrack));

  if (aTracks.contains(TrackInfo::kVideoTrack)) {
    mDecodedVideoEndTime = TimeUnit::Zero();
    mVideoCompleted = false;
    VideoQueue().Reset();
    mVideoDataRequest.DisconnectIfExists();
    mVideoWaitRequest.DisconnectIfExists();
  }

  if (aTracks.contains(TrackInfo::kAudioTrack)) {
    mDecodedAudioEndTime = TimeUnit::Zero();
    mAudioCompleted = false;
    AudioQueue().Reset();
    mAudioDataRequest.DisconnectIfExists();
    mAudioWaitRequest.DisconnectIfExists();
  }

  mReader->ResetDecode(aTracks);
}

media::TimeUnit MediaDecoderStateMachine::GetClock(
    TimeStamp* aTimeStamp) const {
  MOZ_ASSERT(OnTaskQueue());
  auto clockTime = mMediaSink->GetPosition(aTimeStamp);
  // This fails on Windows some times, see 1765563
#if defined(XP_WIN)
  NS_ASSERTION(GetMediaTime() <= clockTime, "Clock should go forwards.");
#else
  MOZ_ASSERT(GetMediaTime() <= clockTime, "Clock should go forwards.");
#endif
  return clockTime;
}

void MediaDecoderStateMachine::UpdatePlaybackPositionPeriodically() {
  MOZ_ASSERT(OnTaskQueue());

  if (!IsPlaying()) {
    return;
  }

  // Cap the current time to the larger of the audio and video end time.
  // This ensures that if we're running off the system clock, we don't
  // advance the clock to after the media end time.
  if (VideoEndTime() > TimeUnit::Zero() || AudioEndTime() > TimeUnit::Zero()) {
    auto clockTime = GetClock();
    // Once looping was turned on, the time is probably larger than the duration
    // of the media track, so the time over the end should be corrected.
    AdjustByLooping(clockTime);
    bool loopback = clockTime < GetMediaTime() && mLooping;
    if (loopback && mBypassingSkipToNextKeyFrameCheck) {
      LOG("media has looped back, no longer bypassing skip-to-next-key-frame");
      mBypassingSkipToNextKeyFrameCheck = false;
    }

    // Skip frames up to the frame at the playback position, and figure out
    // the time remaining until it's time to display the next frame and drop
    // the current frame.
    NS_ASSERTION(clockTime >= TimeUnit::Zero(),
                 "Should have positive clock time.");

    // These will be non -1 if we've displayed a video frame, or played an audio
    // frame.
    auto maxEndTime = std::max(VideoEndTime(), AudioEndTime());
    auto t = std::min(clockTime, maxEndTime);
    // FIXME: Bug 1091422 - chained ogg files hit this assertion.
    // MOZ_ASSERT(t >= GetMediaTime());
    if (loopback || t > GetMediaTime()) {
      UpdatePlaybackPosition(t);
    }
  }
  // Note we have to update playback position before releasing the monitor.
  // Otherwise, MediaDecoder::AddOutputTrack could kick in when we are outside
  // the monitor and get a staled value from GetCurrentTimeUs() which hits the
  // assertion in GetClock().

  int64_t delay = std::max<int64_t>(
      1, static_cast<int64_t>(AUDIO_DURATION_USECS / mPlaybackRate));
  ScheduleStateMachineIn(TimeUnit::FromMicroseconds(delay));

  // Notify the listener as we progress in the playback offset. Note it would
  // be too intensive to send notifications for each popped audio/video sample.
  // It is good enough to send 'PlaybackProgressed' events every 40us (defined
  // by AUDIO_DURATION_USECS), and we ensure 'PlaybackProgressed' events are
  // always sent after 'PlaybackStarted' and before 'PlaybackStopped'.
  mOnPlaybackEvent.Notify(MediaPlaybackEvent{
      MediaPlaybackEvent::PlaybackProgressed, mPlaybackOffset});
}

void MediaDecoderStateMachine::ScheduleStateMachine() {
  MOZ_ASSERT(OnTaskQueue());
  if (mDispatchedStateMachine) {
    return;
  }
  mDispatchedStateMachine = true;

  nsresult rv = OwnerThread()->Dispatch(
      NewRunnableMethod("MediaDecoderStateMachine::RunStateMachine", this,
                        &MediaDecoderStateMachine::RunStateMachine));
  MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
  (void)rv;
}

void MediaDecoderStateMachine::ScheduleStateMachineIn(const TimeUnit& aTime) {
  AUTO_PROFILER_LABEL("MediaDecoderStateMachine::ScheduleStateMachineIn",
                      MEDIA_PLAYBACK);
  MOZ_ASSERT(OnTaskQueue());  // mDelayedScheduler.Ensure() may Disconnect()
                              // the promise, which must happen on the state
                              // machine task queue.
  MOZ_ASSERT(aTime > TimeUnit::Zero());
  if (mDispatchedStateMachine) {
    return;
  }

  TimeStamp target = TimeStamp::Now() + aTime.ToTimeDuration();

  // It is OK to capture 'this' without causing UAF because the callback
  // always happens before shutdown.
  RefPtr<MediaDecoderStateMachine> self = this;
  mDelayedScheduler.Ensure(
      target,
      [self]() {
        self->mDelayedScheduler.CompleteRequest();
        self->RunStateMachine();
      },
      []() { MOZ_DIAGNOSTIC_CRASH("ScheduleStateMachineIn reject"); });
}

bool MediaDecoderStateMachine::IsStateMachineScheduled() const {
  MOZ_ASSERT(OnTaskQueue());
  return mDispatchedStateMachine || mDelayedScheduler.IsScheduled();
}

void MediaDecoderStateMachine::SetPlaybackRate(double aPlaybackRate) {
  MOZ_ASSERT(OnTaskQueue());
  MOZ_ASSERT(aPlaybackRate != 0, "Should be handled by MediaDecoder::Pause()");
  PROFILER_MARKER_TEXT("MDSM::SetPlaybackRate", MEDIA_PLAYBACK, {},
                       nsPrintfCString("PlaybackRate:%f", aPlaybackRate));
  mPlaybackRate = aPlaybackRate;
  mMediaSink->SetPlaybackRate(mPlaybackRate);

  // Schedule next cycle to check if we can stop prerolling.
  ScheduleStateMachine();
}

void MediaDecoderStateMachine::PreservesPitchChanged() {
  AUTO_PROFILER_LABEL("MediaDecoderStateMachine::PreservesPitchChanged",
                      MEDIA_PLAYBACK);
  PROFILER_MARKER_TEXT(
      "MDSM::PreservesPitchChanged", MEDIA_PLAYBACK, {},
      nsPrintfCString("PreservesPitch:%d", mPreservesPitch.Ref()));
  MOZ_ASSERT(OnTaskQueue());
  mMediaSink->SetPreservesPitch(mPreservesPitch);
}

void MediaDecoderStateMachine::LoopingChanged() {
  AUTO_PROFILER_LABEL("MediaDecoderStateMachine::LoopingChanged",
                      MEDIA_PLAYBACK);
  MOZ_ASSERT(OnTaskQueue());
  LOGV("LoopingChanged, looping={}", mLooping.Ref());
  PROFILER_MARKER_TEXT("MDSM::LoopingChanged", MEDIA_PLAYBACK, {},
                       mLooping ? "true"_ns : "false"_ns);
  if (mSeamlessLoopingAllowed) {
    mStateObj->HandleLoopingChanged();
  }
}

void MediaDecoderStateMachine::StreamNameChanged() {
  AUTO_PROFILER_LABEL("MediaDecoderStateMachine::StreamNameChanged",
                      MEDIA_PLAYBACK);
  MOZ_ASSERT(OnTaskQueue());

  mMediaSink->SetStreamName(mStreamName);
}

void MediaDecoderStateMachine::UpdateOutputCaptured() {
  AUTO_PROFILER_LABEL("MediaDecoderStateMachine::UpdateOutputCaptured",
                      MEDIA_PLAYBACK);
  MOZ_ASSERT(OnTaskQueue());
  MOZ_ASSERT_IF(mOutputCaptureInfo.Ref().mState ==
                    MediaDecoder::OutputCaptureState::Capture,
                mOutputCaptureInfo.Ref().mDummyTrack);

  LOG("UpdateOutputCaptured, shouldConfigAudioOutput={}",
      mOutputCaptureInfo.Ref().mShouldConfigAudioOutput);

  // Reset these flags so they are consistent with the status of the sink.
  // TODO: Move these flags into MediaSink to improve cohesion so we don't need
  // to reset these flags when switching MediaSinks.
  mAudioCompleted = false;
  mVideoCompleted = false;

  // TODO: When it becomes necessary to remove audio output from a DecodedStream
  // that already has an audio output set, we should remove the output directly
  // instead of tearing down and recreating a new sink. See bug 2009488.
  // Don't create a new media sink if we're still suspending media sink.
  if (!mIsMediaSinkSuspended) {
    const bool wasPlaying = IsPlaying();
    // Stop and shut down the existing sink.
    StopMediaSink();
    mMediaSink->Shutdown();

    // Create a new sink according to whether output is captured.
    mMediaSink = CreateMediaSink();
    if (wasPlaying) {
      DebugOnly<nsresult> rv = StartMediaSink();
      MOZ_ASSERT(NS_SUCCEEDED(rv));
    }
  }

  // Don't buffer as much when audio is captured because we don't need to worry
  // about high latency audio devices.
  mAmpleAudioThreshold =
      mOutputCaptureInfo.Ref().mState != MediaDecoder::OutputCaptureState::None
          ? detail::AMPLE_AUDIO_THRESHOLD / 2
          : detail::AMPLE_AUDIO_THRESHOLD;

  mStateObj->HandleAudioCaptured();
}

void MediaDecoderStateMachine::OutputPrincipalChanged() {
  MOZ_ASSERT(OnTaskQueue());
  mCanonicalOutputPrincipal = mOutputPrincipal;
}

RefPtr<GenericPromise> MediaDecoderStateMachine::InvokeSetSink(
    const RefPtr<AudioDeviceInfo>& aSink) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aSink);

  return InvokeAsync(OwnerThread(), this, __func__,
                     &MediaDecoderStateMachine::SetSink, aSink);
}

RefPtr<GenericPromise> MediaDecoderStateMachine::SetSink(
    RefPtr<AudioDeviceInfo> aDevice) {
  MOZ_ASSERT(OnTaskQueue());
  if (mIsMediaSinkSuspended) {
    // Don't create a new media sink when suspended.
    return GenericPromise::CreateAndResolve(true, __func__);
  }

  return mMediaSink->SetAudioDevice(std::move(aDevice));
}

void MediaDecoderStateMachine::InvokeSuspendMediaSink() {
  MOZ_ASSERT(NS_IsMainThread());

  nsresult rv = OwnerThread()->Dispatch(
      NewRunnableMethod("MediaDecoderStateMachine::SuspendMediaSink", this,
                        &MediaDecoderStateMachine::SuspendMediaSink));
  MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
  (void)rv;
}

void MediaDecoderStateMachine::SuspendMediaSink() {
  AUTO_PROFILER_LABEL("MediaDecoderStateMachine::SuspendMediaSink",
                      MEDIA_PLAYBACK);
  MOZ_ASSERT(OnTaskQueue());
  if (mIsMediaSinkSuspended) {
    return;
  }
  LOG("SuspendMediaSink");
  mIsMediaSinkSuspended = true;
  StopMediaSink();
  mMediaSink->Shutdown();
}

void MediaDecoderStateMachine::InvokeResumeMediaSink() {
  MOZ_ASSERT(NS_IsMainThread());

  nsresult rv = OwnerThread()->Dispatch(
      NewRunnableMethod("MediaDecoderStateMachine::ResumeMediaSink", this,
                        &MediaDecoderStateMachine::ResumeMediaSink));
  MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
  (void)rv;
}

void MediaDecoderStateMachine::ResumeMediaSink() {
  AUTO_PROFILER_LABEL("MediaDecoderStateMachine::ResumeMediaSink",
                      MEDIA_PLAYBACK);
  MOZ_ASSERT(OnTaskQueue());
  if (!mIsMediaSinkSuspended) {
    return;
  }
  LOG("ResumeMediaSink");
  mIsMediaSinkSuspended = false;
  if (!mMediaSink->IsStarted()) {
    mMediaSink = CreateMediaSink();
    MaybeStartPlayback();
  }
}

void MediaDecoderStateMachine::UpdateSecondaryVideoContainer() {
  AUTO_PROFILER_LABEL("MediaDecoderStateMachine::UpdateSecondaryVideoContainer",
                      MEDIA_PLAYBACK);
  MOZ_ASSERT(OnTaskQueue());
  MOZ_DIAGNOSTIC_ASSERT(mMediaSink);
  mMediaSink->SetSecondaryVideoContainer(mSecondaryVideoContainer.Ref());
  mOnSecondaryVideoContainerInstalled.Notify(mSecondaryVideoContainer.Ref());
}

TimeUnit MediaDecoderStateMachine::AudioEndTime() const {
  MOZ_ASSERT(OnTaskQueue());
  if (mMediaSink->IsStarted()) {
    return mMediaSink->GetEndTime(TrackInfo::kAudioTrack);
  }
  return GetMediaTime();
}

TimeUnit MediaDecoderStateMachine::VideoEndTime() const {
  MOZ_ASSERT(OnTaskQueue());
  if (mMediaSink->IsStarted()) {
    return mMediaSink->GetEndTime(TrackInfo::kVideoTrack);
  }
  return GetMediaTime();
}

void MediaDecoderStateMachine::OnMediaSinkVideoComplete() {
  MOZ_ASSERT(OnTaskQueue());
  MOZ_ASSERT(HasVideo());
  AUTO_PROFILER_LABEL("MediaDecoderStateMachine::OnMediaSinkVideoComplete",
                      MEDIA_PLAYBACK);
  LOG("[{}]", __func__);

  mMediaSinkVideoEndedPromise.Complete();
  mVideoCompleted = true;
  ScheduleStateMachine();
}

void MediaDecoderStateMachine::OnMediaSinkVideoError() {
  MOZ_ASSERT(OnTaskQueue());
  MOZ_ASSERT(HasVideo());
  AUTO_PROFILER_LABEL("MediaDecoderStateMachine::OnMediaSinkVideoError",
                      MEDIA_PLAYBACK);
  LOGE("[%s]", __func__);

  mMediaSinkVideoEndedPromise.Complete();
  mVideoCompleted = true;
  if (HasAudio()) {
    return;
  }
  DecodeError(MediaResult(NS_ERROR_DOM_MEDIA_MEDIASINK_ERR, __func__));
}

void MediaDecoderStateMachine::OnMediaSinkAudioComplete() {
  MOZ_ASSERT(OnTaskQueue());
  MOZ_ASSERT(HasAudio());
  AUTO_PROFILER_LABEL("MediaDecoderStateMachine::OnMediaSinkAudioComplete",
                      MEDIA_PLAYBACK);
  LOG("[{}]", __func__);

  mMediaSinkAudioEndedPromise.Complete();
  mAudioCompleted = true;
  // To notify PlaybackEnded as soon as possible.
  ScheduleStateMachine();

  // Report OK to Decoder Doctor (to know if issue may have been resolved).
  mOnDecoderDoctorEvent.Notify(
      DecoderDoctorEvent{DecoderDoctorEvent::eAudioSinkStartup, NS_OK});
}

void MediaDecoderStateMachine::OnMediaSinkAudioError(nsresult aResult) {
  MOZ_ASSERT(OnTaskQueue());
  MOZ_ASSERT(HasAudio());
  AUTO_PROFILER_LABEL("MediaDecoderStateMachine::OnMediaSinkAudioError",
                      MEDIA_PLAYBACK);
  LOGE("[%s]", __func__);

  mMediaSinkAudioEndedPromise.Complete();
  mAudioCompleted = true;

  // Result should never be NS_OK in this *error* handler. Report to Dec-Doc.
  MOZ_ASSERT(NS_FAILED(aResult));
  mOnDecoderDoctorEvent.Notify(
      DecoderDoctorEvent{DecoderDoctorEvent::eAudioSinkStartup, aResult});

  // Make the best effort to continue playback when there is video.
  if (HasVideo()) {
    return;
  }

  // Otherwise notify media decoder/element about this error for it makes
  // no sense to play an audio-only file without sound output.
  DecodeError(MediaResult(NS_ERROR_DOM_MEDIA_MEDIASINK_ERR, __func__));
}

uint32_t MediaDecoderStateMachine::GetAmpleVideoFrames() const {
  MOZ_ASSERT(OnTaskQueue());
  if (mReader->VideoIsHardwareAccelerated()) {
    // HW decoding should be fast so queue size can be as small as possible
    // to lower frame latency.
    uint32_t hw = std::max<uint32_t>(
        StaticPrefs::media_video_queue_hw_accel_size(), MIN_VIDEO_QUEUE_SIZE);
    mReader->GetMinVideoQueueSize().apply(
        [&hw](const uint32_t& x) { hw = std::max(hw, x); });
    return hw;
  } else {
    // SW decoding is slower and queuing more frames in advance reduces the
    // chances of dropping late frames.
    uint32_t sw = std::max<uint32_t>(
        StaticPrefs::media_video_queue_default_size(), MIN_VIDEO_QUEUE_SIZE);
    mReader->GetMaxVideoQueueSize().apply(
        [&sw](const uint32_t& x) { sw = std::min(sw, x); });
    return sw;
  }
}

void MediaDecoderStateMachine::GetDebugInfo(
    dom::MediaDecoderStateMachineDebugInfo& aInfo) {
  MOZ_ASSERT(OnTaskQueue());
  aInfo.mDuration =
      mDuration.Ref() ? mDuration.Ref().ref().ToMicroseconds() : -1;
  aInfo.mMediaTime = GetMediaTime().ToMicroseconds();
  aInfo.mClock = mMediaSink->IsStarted() ? GetClock().ToMicroseconds() : -1;
  aInfo.mPlayState = int32_t(mPlayState.Ref());
  aInfo.mSentFirstFrameLoadedEvent = mSentFirstFrameLoadedEvent;
  aInfo.mIsPlaying = IsPlaying();
  CopyUTF8toUTF16(MakeStringSpan(AudioRequestStatus()),
                  aInfo.mAudioRequestStatus);
  CopyUTF8toUTF16(MakeStringSpan(VideoRequestStatus()),
                  aInfo.mVideoRequestStatus);
  aInfo.mDecodedAudioEndTime = mDecodedAudioEndTime.ToMicroseconds();
  aInfo.mDecodedVideoEndTime = mDecodedVideoEndTime.ToMicroseconds();
  aInfo.mAudioCompleted = mAudioCompleted;
  aInfo.mVideoCompleted = mVideoCompleted;
  mStateObj->GetDebugInfo(aInfo.mStateObj);
  mMediaSink->GetDebugInfo(aInfo.mMediaSink);
  aInfo.mTotalBufferingTimeMs = mTotalBufferingDuration.ToMilliseconds();
}

RefPtr<GenericPromise> MediaDecoderStateMachine::RequestDebugInfo(
    dom::MediaDecoderStateMachineDebugInfo& aInfo) {
  if (mShuttingDown) {
    return GenericPromise::CreateAndReject(NS_ERROR_FAILURE, __func__);
  }

  RefPtr<GenericPromise::Private> p = new GenericPromise::Private(__func__);
  RefPtr<MediaDecoderStateMachine> self = this;
  nsresult rv = OwnerThread()->Dispatch(
      NS_NewRunnableFunction("MediaDecoderStateMachine::RequestDebugInfo",
                             [self, p, &aInfo]() {
                               self->GetDebugInfo(aInfo);
                               p->Resolve(true, __func__);
                             }),
      AbstractThread::TailDispatch);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  (void)rv;
  return p;
}

class VideoQueueMemoryFunctor : public nsDequeFunctor<VideoData> {
 public:
  VideoQueueMemoryFunctor() : mSize(0) {}

  MOZ_DEFINE_MALLOC_SIZE_OF(MallocSizeOf);

  virtual void operator()(VideoData* aObject) override {
    mSize += aObject->SizeOfIncludingThis(MallocSizeOf);
  }

  size_t mSize;
};

class AudioQueueMemoryFunctor : public nsDequeFunctor<AudioData> {
 public:
  AudioQueueMemoryFunctor() : mSize(0) {}

  MOZ_DEFINE_MALLOC_SIZE_OF(MallocSizeOf);

  virtual void operator()(AudioData* aObject) override {
    mSize += aObject->SizeOfIncludingThis(MallocSizeOf);
  }

  size_t mSize;
};

size_t MediaDecoderStateMachine::SizeOfVideoQueue() const {
  VideoQueueMemoryFunctor functor;
  mVideoQueue.LockedForEach(functor);
  return functor.mSize;
}

size_t MediaDecoderStateMachine::SizeOfAudioQueue() const {
  AudioQueueMemoryFunctor functor;
  mAudioQueue.LockedForEach(functor);
  return functor.mSize;
}

const char* MediaDecoderStateMachine::AudioRequestStatus() const {
  MOZ_ASSERT(OnTaskQueue());
  if (IsRequestingAudioData()) {
    MOZ_DIAGNOSTIC_ASSERT(!IsWaitingAudioData());
    return "pending";
  }

  if (IsWaitingAudioData()) {
    return "waiting";
  }
  return "idle";
}

const char* MediaDecoderStateMachine::VideoRequestStatus() const {
  MOZ_ASSERT(OnTaskQueue());
  if (IsRequestingVideoData()) {
    MOZ_DIAGNOSTIC_ASSERT(!IsWaitingVideoData());
    return "pending";
  }

  if (IsWaitingVideoData()) {
    return "waiting";
  }
  return "idle";
}

void MediaDecoderStateMachine::OnSuspendTimerResolved() {
  LOG("OnSuspendTimerResolved");
  mVideoDecodeSuspendTimer.CompleteRequest();
  mStateObj->HandleVideoSuspendTimeout();
}

void MediaDecoderStateMachine::CancelSuspendTimer() {
  LOG("CancelSuspendTimer: State: {}, Timer.IsScheduled: {}",
      ToStateStr(mStateObj->GetState()),
      mVideoDecodeSuspendTimer.IsScheduled() ? 'T' : 'F');
  MOZ_ASSERT(OnTaskQueue());
  if (mVideoDecodeSuspendTimer.IsScheduled()) {
    mOnPlaybackEvent.Notify(MediaPlaybackEvent::CancelVideoSuspendTimer);
  }
  mVideoDecodeSuspendTimer.Reset();
}

void MediaDecoderStateMachine::AdjustByLooping(media::TimeUnit& aTime) const {
  MOZ_ASSERT(OnTaskQueue());

  // No need to adjust time.
  if (mOriginalDecodedDuration == media::TimeUnit::Zero()) {
    return;
  }

  // There are situations where we need to perform subtraction instead of modulo
  // to accurately adjust the clock. When we are not in a state of seamless
  // looping, it is usually necessary to normalize the clock time within the
  // range of [0, duration]. However, if the current clock time is greater than
  // the duration (i.e., duration+1) and not in looping, we should not adjust it
  // to 1 as we are not looping back to the starting position. Instead, we
  // should leave the clock time unchanged and trim it later to match the
  // maximum duration time.
  if (mStateObj->GetState() != DECODER_STATE_LOOPING_DECODING) {
    // Use the smaller offset rather than the larger one, as the larger offset
    // indicates the next round of looping. For example, if the duration is X
    // and the playback is currently in the third round of looping, both
    // queues will have an offset of 3X. However, if the audio decoding is
    // faster and the fourth round of data has already been added to the audio
    // queue, the audio offset will become 4X. Since playback is still in the
    // third round, we should use the smaller offset of 3X to adjust the time.
    TimeUnit offset = TimeUnit::FromInfinity();
    if (HasAudio()) {
      offset = std::min(AudioQueue().GetOffset(), offset);
    }
    if (HasVideo()) {
      offset = std::min(VideoQueue().GetOffset(), offset);
    }
    if (aTime > offset) {
      aTime -= offset;
      return;
    }
  }

  // When seamless looping happens at least once, it doesn't matter if we're
  // looping or not.
  aTime = aTime % mOriginalDecodedDuration;
}

bool MediaDecoderStateMachine::IsInSeamlessLooping() const {
  return mLooping && mSeamlessLoopingAllowed;
}

bool MediaDecoderStateMachine::HasLastDecodedData(MediaData::Type aType) {
  MOZ_DIAGNOSTIC_ASSERT(aType == MediaData::Type::AUDIO_DATA ||
                        aType == MediaData::Type::VIDEO_DATA);
  if (aType == MediaData::Type::AUDIO_DATA) {
    return mDecodedAudioEndTime != TimeUnit::Zero();
  }
  return mDecodedVideoEndTime != TimeUnit::Zero();
}

nsresult MediaDecoderStateMachine::IsCDMProxySupported(CDMProxy* aProxy) {
#ifdef MOZ_WMF_CDM
  MOZ_ASSERT(aProxy);
  // This proxy only works with the external state machine.
  if (aProxy->AsWMFCDMProxy()) {
    return NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR;
  }
#endif
  return NS_OK;
}

RefPtr<SetCDMPromise> MediaDecoderStateMachine::SetCDMProxy(CDMProxy* aProxy) {
  // Playback hasn't started yet.
  if (!mInitialized) {
    mReader->SetEncryptedCustomIdent();
  }
  return MediaDecoderStateMachineBase::SetCDMProxy(aProxy);
}

}  // namespace mozilla

// avoid redefined macro in unified build
#undef LOG
#undef LOGV
#undef LOGW
#undef LOGE
#undef SLOGW
#undef SLOGE
#undef NS_DispatchToMainThread
