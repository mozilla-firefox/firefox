/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_MEDIA_EXTERNALENGINESTATEMACHINE_H_
#define DOM_MEDIA_EXTERNALENGINESTATEMACHINE_H_

#include "MediaDecoderStateMachineBase.h"
#include "SeekJob.h"
#include "mozilla/Variant.h"

namespace mozilla {

/**
 * ExternalPlaybackEngine represents a media engine which is responsible for
 * decoding and playback, which are not controlled by Gecko.
 */
class ExternalPlaybackEngine;

enum class ExternalEngineEvent {
  LoadedMetaData,
  LoadedFirstFrame,
  LoadedData,
  Waiting,
  Playing,
  Seeked,
  BufferingStarted,
  BufferingEnded,
  Timeupdate,
  Ended,
  RequestForAudio,
  RequestForVideo,
  AudioEnough,
  VideoEnough,
};
const char* ExternalEngineEventToStr(ExternalEngineEvent aEvent);

/**
 * When using ExternalEngineStateMachine, that means we use an external engine
 * to control decoding and playback (including A/V sync). Eg. Media Foundation
 * Media Engine on Windows.
 *
 * The external engine does most of playback works, and uses ExternalEngineEvent
 * to tell us its internal state. Therefore, this state machine is responsible
 * to address those events from the engine and coordinate the format reader in
 * order to provide data to the engine correctly.
 */
DDLoggedTypeDeclName(ExternalEngineStateMachine);

class ExternalEngineStateMachine final
    : public MediaDecoderStateMachineBase,
      public DecoderDoctorLifeLogger<ExternalEngineStateMachine> {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(ExternalEngineStateMachine, override)

  ExternalEngineStateMachine(MediaDecoder* aDecoder,
                             MediaFormatReader* aReader);

  RefPtr<MediaDecoder::SeekPromise> InvokeSeek(
      const SeekTarget& aTarget) override;

  RefPtr<GenericPromise> InvokeSetSink(
      const RefPtr<AudioDeviceInfo>& aSink) override;

  // The media sample would be managed by the external engine so we won't store
  // any samples in our side.
  size_t SizeOfVideoQueue() const override { return 0; }
  size_t SizeOfAudioQueue() const override { return 0; }

  // Not supported.
  void SetVideoDecodeMode(VideoDecodeMode aMode) override {}
  void InvokeSuspendMediaSink() override {}
  void InvokeResumeMediaSink() override {}
  RefPtr<GenericPromise> RequestDebugInfo(
      dom::MediaDecoderStateMachineDebugInfo& aInfo) override {
    // This debug info doesn't fit in this scenario because most decoding
    // details are only visible inside the external engine.
    return GenericPromise::CreateAndResolve(true, __func__);
  }

  void NotifyEvent(ExternalEngineEvent aEvent) {
    // On the engine manager thread.
    (void)OwnerThread()->Dispatch(NS_NewRunnableFunction(
        "ExternalEngineStateMachine::NotifyEvent",
        [self = RefPtr{this}, aEvent] { self->NotifyEventInternal(aEvent); }));
  }
  void NotifyError(const MediaResult& aError) {
    // On the engine manager thread.
    (void)OwnerThread()->Dispatch(NS_NewRunnableFunction(
        "ExternalEngineStateMachine::NotifyError",
        [self = RefPtr{this}, aError] { self->NotifyErrorInternal(aError); }));
  }
  void NotifyResizing(uint32_t aWidth, uint32_t aHeight) {
    // On the engine manager thread.
    (void)OwnerThread()->Dispatch(
        NS_NewRunnableFunction("ExternalEngineStateMachine::NotifyResizing",
                               [self = RefPtr{this}, aWidth, aHeight] {
                                 self->NotifyResizingInternal(aWidth, aHeight);
                               }));
  }
  void NotifyHardwareReset(uint32_t aPlatformError) {
    // On the engine manager thread.
    (void)OwnerThread()->Dispatch(NS_NewRunnableFunction(
        "ExternalEngineStateMachine::NotifyHardwareReset",
        [self = RefPtr{this}, aPlatformError] {
          self->mHardwareResetError =
              Some(static_cast<int32_t>(aPlatformError));
          self->RecoverFromHardwareReset();
        }));
  }
#ifdef MOZ_WMF_CDM
  void NotifyWaitingForKey() {
    // On the engine manager thread.
    (void)OwnerThread()->Dispatch(NS_NewRunnableFunction(
        "ExternalEngineStateMachine::NotifyWaitingForKey",
        [self = RefPtr{this}] { self->NotifyWaitingForKeyInternal(); }));
  }
  // Frame server mode is used only for WMFClearKey (test-only CDM) — it
  // bypasses DComp surface delivery and drives rendering via OnVideoStreamTick.
  void NotifyFrameServerMode() {
    // On the engine manager thread.
    (void)OwnerThread()->Dispatch(NS_NewRunnableFunction(
        "ExternalEngineStateMachine::NotifyFrameServerMode",
        [self = RefPtr{this}] { self->NotifyFrameServerModeInternal(); }));
  }
#endif

  const char* GetStateStr() const;

  RefPtr<SetCDMPromise> SetCDMProxy(CDMProxy* aProxy) override;

  nsresult IsCDMProxySupported(CDMProxy* aProxy) override;

  bool IsExternalEngineStateMachine() const override { return true; }

 private:
  ~ExternalEngineStateMachine();

  void AssertOnTaskQueue() const { MOZ_ASSERT(OnTaskQueue()); }

  // A light-weight state object that helps to store some variables which would
  // only be used in a certain state. Also be able to do the cleaning for the
  // state transition. Only modify on the task queue.
  struct StateObject final {
    enum class State {
      InitEngine,
      ReadingMetadata,
      RunningEngine,
      SeekingData,
      ShutdownEngine,
      RecoverEngine,
    };
    struct InitEngine {
      InitEngine() = default;
      ~InitEngine() { mEngineInitRequest.DisconnectIfExists(); }
      MozPromiseRequestHolder<GenericNonExclusivePromise> mEngineInitRequest;
      RefPtr<GenericNonExclusivePromise> mInitPromise;
    };
    struct ReadingMetadata {
      ReadingMetadata() = default;
      ~ReadingMetadata() { mMetadataRequest.DisconnectIfExists(); }
      MozPromiseRequestHolder<MediaFormatReader::MetadataPromise>
          mMetadataRequest;
    };
    struct RunningEngine {};
    struct SeekingData {
      SeekingData() = default;
      SeekingData(SeekingData&&) = default;
      SeekingData(const SeekingData&) = delete;
      SeekingData& operator=(const SeekingData&) = delete;
      ~SeekingData() {
        mSeekJob.RejectIfExists(__func__);
        mSeekRequest.DisconnectIfExists();
      }
      void SetTarget(const SeekTarget& aTarget) {
        // If there is any promise for previous seeking, reject it first.
        mSeekJob.RejectIfExists(__func__);
        mSeekRequest.DisconnectIfExists();
        // Then create a new seek job.
        mSeekJob = SeekJob();
        mSeekJob.mTarget = Some(aTarget);
      }
      void Resolve(StaticString aCallSite) {
        MOZ_ASSERT(mSeekJob.Exists());
        mSeekJob.Resolve(aCallSite);
        mSeekJob = SeekJob();
      }
      void RejectIfExists(StaticString aCallSite) {
        mSeekJob.RejectIfExists(aCallSite);
      }
      bool IsSeeking() const { return mSeekRequest.Exists(); }
      media::TimeUnit GetTargetTime() const {
        return mSeekJob.mTarget ? mSeekJob.mTarget->GetTime()
                                : media::TimeUnit::Invalid();
      }
      // Set it to true when starting seeking, and would be set to false after
      // receiving engine's `seeked` event. Used on thhe task queue only.
      bool mWaitingEngineSeeked = false;
      bool mWaitingReaderSeeked = false;
      MozPromiseRequestHolder<MediaFormatReader::SeekPromise> mSeekRequest;
      SeekJob mSeekJob;
    };
    struct ShutdownEngine {
      RefPtr<ShutdownPromise> mShutdown;
    };
    // This state is used to recover the media engine after the MF CDM process
    // crashes.
    struct RecoverEngine : public InitEngine {};

    StateObject() : mData(ReadingMetadata()), mName(State::ReadingMetadata) {};
    explicit StateObject(InitEngine&& aArg)
        : mData(std::move(aArg)), mName(State::InitEngine) {};
    explicit StateObject(RunningEngine&& aArg)
        : mData(std::move(aArg)), mName(State::RunningEngine) {};
    explicit StateObject(SeekingData&& aArg)
        : mData(std::move(aArg)), mName(State::SeekingData) {};
    explicit StateObject(ShutdownEngine&& aArg)
        : mData(std::move(aArg)), mName(State::ShutdownEngine) {};
    explicit StateObject(RecoverEngine&& aArg)
        : mData(std::move(aArg)), mName(State::RecoverEngine) {};

    bool IsInitEngine() const { return mData.is<InitEngine>(); }
    bool IsReadingMetadata() const { return mData.is<ReadingMetadata>(); }
    bool IsRunningEngine() const { return mData.is<RunningEngine>(); }
    bool IsSeekingData() const { return mData.is<SeekingData>(); }
    bool IsShutdownEngine() const { return mData.is<ShutdownEngine>(); }
    bool IsRecoverEngine() const { return mData.is<RecoverEngine>(); }

    InitEngine* AsInitEngine() {
      if (IsInitEngine()) {
        return &mData.as<InitEngine>();
      }
      if (IsRecoverEngine()) {
        return &mData.as<RecoverEngine>();
      }
      return nullptr;
    }
    ReadingMetadata* AsReadingMetadata() {
      return IsReadingMetadata() ? &mData.as<ReadingMetadata>() : nullptr;
    }
    SeekingData* AsSeekingData() {
      return IsSeekingData() ? &mData.as<SeekingData>() : nullptr;
    }
    ShutdownEngine* AsShutdownEngine() {
      return IsShutdownEngine() ? &mData.as<ShutdownEngine>() : nullptr;
    }

    Variant<InitEngine, ReadingMetadata, RunningEngine, SeekingData,
            ShutdownEngine, RecoverEngine>
        mData;
    State mName;
  } mState;
  using State = StateObject::State;

  void NotifyEventInternal(ExternalEngineEvent aEvent);
  void NotifyErrorInternal(const MediaResult& aError);
  void NotifyResizingInternal(uint32_t aWidth, uint32_t aHeight);

  RefPtr<ShutdownPromise> Shutdown() override;

  void SetPlaybackRate(double aPlaybackRate) override;
  void BufferedRangeUpdated() override;
  void VolumeChanged() override;
  void PreservesPitchChanged() override;
  void PlayStateChanged() override;
  void LoopingChanged() override;
  void PlaybackRateChanged();

  // Not supported.
  void SetCanPlayThrough(bool aCanPlayThrough) override {}
  void SetFragmentEndTime(const media::TimeUnit& aFragmentEndTime) override {}

  void InitEngine();
  void OnEngineInitSuccess();
  void OnEngineInitFailure();

  void ReadMetadata();
  void OnMetadataRead(MetadataHolder&& aMetadata);
  void OnMetadataNotRead(const MediaResult& aError);
  bool IsFormatSupportedByExternalEngine(const MediaInfo& aInfo);

  // Functions for handling external engine event.
  void OnLoadedFirstFrame();
  void OnLoadedData();
  void OnWaiting();
  void OnPlaying();
  void OnSeeked();
  void OnBufferingStarted();
  void OnBufferingEnded();
  void OnTimeupdate();
  void OnEnded();
  void OnRequestAudio();
  void OnRequestVideo();

  void ResetDecode();

  void EndOfStream(MediaData::Type aType);
  void WaitForData(MediaData::Type aType);

  void StartRunningEngine();
  void RunningEngineUpdate(MediaData::Type aType);

  void ChangeStateTo(State aNextState);
  static const char* StateToStr(State aState);

  RefPtr<MediaDecoder::SeekPromise> Seek(const SeekTarget& aTarget) override;
  void SeekReader();
  void OnSeekResolved(const media::TimeUnit& aUnit);
  void OnSeekRejected(const SeekRejectValue& aReject);
  bool IsSeeking();
  void CheckIfSeekCompleted();

  void MaybeFinishWaitForData();

  void SetBlankVideoToVideoContainer();

  media::TimeUnit GetVideoThreshold();

  bool ShouldRunEngineUpdateForRequest();

  void UpdateSecondaryVideoContainer() override;

  // When the media engine enters an unrecoverable state, one of two recovery
  // paths is taken:
  // - CDM process crash : tear down reader resources and reinitialize entirely
  // - hardware context reset : GPU/DRM state invalidated but process still
  // alive, so reinitialize the engine only.
  void RecoverFromCDMProcessCrashIfNeeded();
  void RecoverFromHardwareReset();
#ifdef MOZ_WMF_CDM
  void NotifyWaitingForKeyInternal();
  void NotifyFrameServerModeInternal();
#endif

  void ReportTelemetry(const MediaResult& aError);
  void ReportRecoveryTelemetry(bool aRecovered);

  void DecodeError(const MediaResult& aError) override;

  void NotifyAudibleStateChangeIfNeeded();

  void ApplyPendingCDMProxy();

  UniquePtr<ExternalPlaybackEngine> mEngine;

  bool mHasEnoughAudio = false;
  bool mHasEnoughVideo = false;
  bool mSentPlaybackEndedEvent = false;
  bool mHasReceivedFirstDecodedVideoFrame = false;
#ifdef MOZ_WMF_CDM
  bool mIsFrameServerMode = false;
  // True once we have sent the video EOS notification to the WMF engine.
  // The WMF engine in frame server mode continues to fire TIMEUPDATE events
  // after EOS via OnVideoStreamTick; without this guard RunningEngineUpdate
  // would re-request video data after the stream has ended, causing spurious
  // decode failures.
  bool mVideoEOSSentToEngine = false;
#endif

  // Only used if setting CDM happens before the engine finishes initialization.
  RefPtr<CDMProxy> mPendingCDMProxy;
  MozPromiseRequestHolder<SetCDMPromise> mSetCDMProxyRequest;

  // If seek happens while the engine is still initializing, then we would
  // postpone the seek until the engine is ready.
  SeekJob mPendingSeek;
  MozPromiseRequestHolder<MediaDecoder::SeekPromise> mPendingSeekRequest;

  // It would be zero for audio-only playback.
  gfx::IntSize mVideoDisplay;

  // The display size of the last video frame sent to the video container.
  gfx::IntSize mLastFrameDisplay;

  // It would be set if playback is encrypted.
  nsCString mKeySystem;

  // This array stores the tasks which needs to be executed only after the
  // engine is ready but is called before that. It will be executed when
  // starting running the engine.
  nsTArray<RefPtr<nsIRunnable>> mPendingTasks;

  bool mHasFatalError = false;

  // Set when the engine has been fully initialized (in OnEngineInitSuccess)
  // and cleared on recovery (crash or hardware reset). Used to defer
  // operations that require a ready engine.
  bool mIsEngineReady = false;

  // Number of recovery attempts in the current recovery sequence, and the
  // platform error that triggered it. Reset once a recovery event is reported.
  uint32_t mRecoveryAttempts = 0;
  Maybe<int32_t> mHardwareResetError;
};

class ExternalPlaybackEngine {
 public:
  explicit ExternalPlaybackEngine(ExternalEngineStateMachine* aOwner)
      : mOwner(aOwner) {}

  virtual ~ExternalPlaybackEngine() = default;

  enum class InitFlag {
    None,
    ShouldPreload,
    EncryptedCustomIdent,
  };
  using InitFlagSet = EnumSet<InitFlag, uint8_t>;

  // Init the engine and specify the preload request.
  virtual RefPtr<GenericNonExclusivePromise> Init(
      const MediaInfo& aInfo, const InitFlagSet& aFlags) = 0;
  virtual void Shutdown() = 0;
  virtual uint64_t Id() const = 0;
  virtual bool IsInited() const = 0;

  // Following methods should only be called after successfully initialize the
  // external engine.
  virtual void Play() = 0;
  virtual void Pause() = 0;
  virtual void Seek(const media::TimeUnit& aTargetTime) = 0;
  virtual void SetPlaybackRate(double aPlaybackRate) = 0;
  virtual void SetVolume(double aVolume) = 0;
  virtual void SetLooping(bool aLooping) = 0;
  virtual void SetPreservesPitch(bool aPreservesPitch) = 0;
  virtual media::TimeUnit GetCurrentPosition() = 0;
  virtual void NotifyEndOfStream(TrackInfo::TrackType aType) = 0;
  virtual bool SetCDMProxy(CDMProxy* aProxy) = 0;
  virtual void NotifyResizing(uint32_t aWidth, uint32_t aHeight) = 0;

  ExternalEngineStateMachine* const MOZ_NON_OWNING_REF mOwner;
};

}  // namespace mozilla

#endif  // DOM_MEDIA_EXTERNALENGINESTATEMACHINE_H_
