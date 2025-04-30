/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaDecoder.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "AudioDeviceInfo.h"
#include "DOMMediaStream.h"
#include "ImageContainer.h"
#include "MediaDecoderStateMachineBase.h"
#include "MediaFormatReader.h"
#include "MediaResource.h"
#include "MediaShutdownManager.h"
#include "MediaTrackGraph.h"
#include "TelemetryProbesReporter.h"
#include "VideoFrameContainer.h"
#include "VideoUtils.h"
#include "WindowRenderer.h"
#include "mozilla/AbstractThread.h"
#include "mozilla/FloatingPoint.h"
#include "mozilla/MathAlgorithms.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/glean/DomMediaMetrics.h"
#include "mozilla/Unused.h"
#include "mozilla/dom/DOMTypes.h"
#include "mozilla/glean/DomMediaPlatformsWmfMetrics.h"
#include "nsComponentManagerUtils.h"
#include "nsContentUtils.h"
#include "nsError.h"
#include "nsIMemoryReporter.h"
#include "nsPrintfCString.h"
#include "nsServiceManagerUtils.h"
#include "nsTArray.h"

using namespace mozilla::dom;
using namespace mozilla::layers;
using namespace mozilla::media;

namespace mozilla {

// avoid redefined macro in unified build
#undef LOG
#undef DUMP

LazyLogModule gMediaDecoderLog("MediaDecoder");

#define LOG(x, ...) \
  DDMOZ_LOG(gMediaDecoderLog, LogLevel::Debug, x, ##__VA_ARGS__)

#define DUMP(x, ...) printf_stderr(x "\n", ##__VA_ARGS__)

#define NS_DispatchToMainThread(...) CompileError_UseAbstractMainThreadInstead

class MediaMemoryTracker : public nsIMemoryReporter {
  virtual ~MediaMemoryTracker();

  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIMEMORYREPORTER

  MOZ_DEFINE_MALLOC_SIZE_OF(MallocSizeOf);

  MediaMemoryTracker();
  void InitMemoryReporter();

  static StaticRefPtr<MediaMemoryTracker> sUniqueInstance;

  static MediaMemoryTracker* UniqueInstance() {
    if (!sUniqueInstance) {
      sUniqueInstance = new MediaMemoryTracker();
      sUniqueInstance->InitMemoryReporter();
    }
    return sUniqueInstance;
  }

  using DecodersArray = nsTArray<MediaDecoder*>;
  static DecodersArray& Decoders() { return UniqueInstance()->mDecoders; }

  DecodersArray mDecoders;

 public:
  static void AddMediaDecoder(MediaDecoder* aDecoder) {
    Decoders().AppendElement(aDecoder);
  }

  static void RemoveMediaDecoder(MediaDecoder* aDecoder) {
    DecodersArray& decoders = Decoders();
    decoders.RemoveElement(aDecoder);
    if (decoders.IsEmpty()) {
      sUniqueInstance = nullptr;
    }
  }
};

StaticRefPtr<MediaMemoryTracker> MediaMemoryTracker::sUniqueInstance;

LazyLogModule gMediaTimerLog("MediaTimer");

constexpr TimeUnit MediaDecoder::DEFAULT_NEXT_FRAME_AVAILABLE_BUFFERED;

void MediaDecoder::InitStatics() {
  MOZ_ASSERT(NS_IsMainThread());
  // Eagerly init gMediaDecoderLog to work around bug 1415441.
  MOZ_LOG(gMediaDecoderLog, LogLevel::Info, ("MediaDecoder::InitStatics"));

#if defined(NIGHTLY_BUILD)
  // Allow people to force a bit but try to warn them about filing bugs if audio
  // decoding does not work on utility
  static const bool allowLockPrefs =
      PR_GetEnv("MOZ_DONT_LOCK_UTILITY_PLZ_FILE_A_BUG") == nullptr;
  if (XRE_IsParentProcess() && allowLockPrefs) {
    // Lock Utility process preferences so that people cannot opt-out of
    // Utility process
    Preferences::Lock("media.utility-process.enabled");
#  if defined(MOZ_FFMPEG)
    Preferences::Lock("media.utility-ffmpeg.enabled");
#  endif  // defined(MOZ_FFMPEG)
    Preferences::Lock("media.utility-ffvpx.enabled");
#  if defined(MOZ_WMF)
    Preferences::Lock("media.utility-wmf.enabled");
#  endif  // defined(MOZ_WMF)
#  if defined(MOZ_APPLEMEDIA)
    Preferences::Lock("media.utility-applemedia.enabled");
#  endif  // defined(MOZ_APPLEMEDIA)
    Preferences::Lock("media.utility-vorbis.enabled");
    Preferences::Lock("media.utility-wav.enabled");
    Preferences::Lock("media.utility-opus.enabled");
  }
#endif  // defined(NIGHTLY_BUILD)
}

NS_IMPL_ISUPPORTS(MediaMemoryTracker, nsIMemoryReporter)

void MediaDecoder::NotifyOwnerActivityChanged(bool aIsOwnerInvisible,
                                              bool aIsOwnerConnected,
                                              bool aIsOwnerInBackground,
                                              bool aHasOwnerPendingCallbacks) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(!IsShutdown());
  SetElementVisibility(aIsOwnerInvisible, aIsOwnerConnected,
                       aIsOwnerInBackground, aHasOwnerPendingCallbacks);

  NotifyCompositor();
}

void MediaDecoder::Pause() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(!IsShutdown());
  LOG("Pause");
  if (mPlayState == PLAY_STATE_LOADING || IsEnded()) {
    mNextState = PLAY_STATE_PAUSED;
    return;
  }
  ChangeState(PLAY_STATE_PAUSED);
}

void MediaDecoder::SetVolume(double aVolume) {
  MOZ_ASSERT(NS_IsMainThread());
  mVolume = aVolume;
}

RefPtr<GenericPromise> MediaDecoder::SetSink(AudioDeviceInfo* aSinkDevice) {
  MOZ_ASSERT(NS_IsMainThread());
  mSinkDevice = aSinkDevice;
  return GetStateMachine()->InvokeSetSink(aSinkDevice);
}

void MediaDecoder::SetOutputCaptureState(OutputCaptureState aState,
                                         SharedDummyTrack* aDummyTrack) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mDecoderStateMachine, "Must be called after Load().");
  MOZ_ASSERT_IF(aState == OutputCaptureState::Capture, aDummyTrack);

  if (mOutputCaptureState.Ref() != aState) {
    LOG("Capture state change from %s to %s",
        EnumValueToString(mOutputCaptureState.Ref()),
        EnumValueToString(aState));
  }
  mOutputCaptureState = aState;
  if (mOutputDummyTrack.Ref().get() != aDummyTrack) {
    mOutputDummyTrack = nsMainThreadPtrHandle<SharedDummyTrack>(
        MakeAndAddRef<nsMainThreadPtrHolder<SharedDummyTrack>>(
            "MediaDecoder::mOutputDummyTrack", aDummyTrack));
  }
}

void MediaDecoder::AddOutputTrack(RefPtr<ProcessedMediaTrack> aTrack) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mDecoderStateMachine, "Must be called after Load().");
  CopyableTArray<RefPtr<ProcessedMediaTrack>> tracks = mOutputTracks;
  tracks.AppendElement(std::move(aTrack));
  mOutputTracks = tracks;
}

void MediaDecoder::RemoveOutputTrack(
    const RefPtr<ProcessedMediaTrack>& aTrack) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mDecoderStateMachine, "Must be called after Load().");
  CopyableTArray<RefPtr<ProcessedMediaTrack>> tracks = mOutputTracks;
  if (tracks.RemoveElement(aTrack)) {
    mOutputTracks = tracks;
  }
}

void MediaDecoder::SetOutputTracksPrincipal(
    const RefPtr<nsIPrincipal>& aPrincipal) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mDecoderStateMachine, "Must be called after Load().");
  mOutputPrincipal = MakePrincipalHandle(aPrincipal);
}

double MediaDecoder::GetDuration() {
  MOZ_ASSERT(NS_IsMainThread());
  return ToMicrosecondResolution(mDuration.match(DurationToDouble()));
}

bool MediaDecoder::IsInfinite() const {
  MOZ_ASSERT(NS_IsMainThread());
  return std::isinf(mDuration.match(DurationToDouble()));
}

#define INIT_MIRROR(name, val) \
  name(mOwner->AbstractMainThread(), val, "MediaDecoder::" #name " (Mirror)")
#define INIT_CANONICAL(name, val) \
  name(mOwner->AbstractMainThread(), val, "MediaDecoder::" #name " (Canonical)")

MediaDecoder::MediaDecoder(MediaDecoderInit& aInit)
    : mWatchManager(this, aInit.mOwner->AbstractMainThread()),
      mLogicalPosition(0.0),
      mDuration(TimeUnit::Invalid()),
      mOwner(aInit.mOwner),
      mAbstractMainThread(aInit.mOwner->AbstractMainThread()),
      mFrameStats(new FrameStatistics()),
      mVideoFrameContainer(aInit.mOwner->GetVideoFrameContainer()),
      mMinimizePreroll(aInit.mMinimizePreroll),
      mFiredMetadataLoaded(false),
      mIsOwnerInvisible(false),
      mIsOwnerConnected(false),
      mIsOwnerInBackground(false),
      mHasOwnerPendingCallbacks(false),
      mForcedHidden(false),
      mHasSuspendTaint(aInit.mHasSuspendTaint),
      mShouldResistFingerprinting(
          aInit.mOwner->ShouldResistFingerprinting(RFPTarget::AudioSampleRate)),
      mPlaybackRate(aInit.mPlaybackRate),
      mLogicallySeeking(false, "MediaDecoder::mLogicallySeeking"),
      INIT_MIRROR(mBuffered, TimeIntervals()),
      INIT_MIRROR(mCurrentPosition, TimeUnit::Zero()),
      INIT_MIRROR(mStateMachineDuration, NullableTimeUnit()),
      INIT_MIRROR(mIsAudioDataAudible, false),
      INIT_CANONICAL(mVolume, aInit.mVolume),
      INIT_CANONICAL(mPreservesPitch, aInit.mPreservesPitch),
      INIT_CANONICAL(mLooping, aInit.mLooping),
      INIT_CANONICAL(mStreamName, aInit.mStreamName),
      INIT_CANONICAL(mSinkDevice, nullptr),
      INIT_CANONICAL(mSecondaryVideoContainer, nullptr),
      INIT_CANONICAL(mOutputCaptureState, OutputCaptureState::None),
      INIT_CANONICAL(mOutputDummyTrack, nullptr),
      INIT_CANONICAL(mOutputTracks, nsTArray<RefPtr<ProcessedMediaTrack>>()),
      INIT_CANONICAL(mOutputPrincipal, PRINCIPAL_HANDLE_NONE),
      INIT_CANONICAL(mPlayState, PLAY_STATE_LOADING),
      mSameOriginMedia(false),
      mVideoDecodingOberver(
          new BackgroundVideoDecodingPermissionObserver(this)),
      mIsBackgroundVideoDecodingAllowed(false),
      mTelemetryReported(false),
      mContainerType(aInit.mContainerType),
      mTelemetryProbesReporter(
          new TelemetryProbesReporter(aInit.mReporterOwner)) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mAbstractMainThread);
  MediaMemoryTracker::AddMediaDecoder(this);

  //
  // Initialize watchers.
  //

  // mDuration
  mWatchManager.Watch(mStateMachineDuration, &MediaDecoder::DurationChanged);

  // readyState
  mWatchManager.Watch(mPlayState, &MediaDecoder::UpdateReadyState);
  // ReadyState computation depends on MediaDecoder::CanPlayThrough, which
  // depends on the download rate.
  mWatchManager.Watch(mBuffered, &MediaDecoder::UpdateReadyState);

  // mLogicalPosition
  mWatchManager.Watch(mCurrentPosition, &MediaDecoder::UpdateLogicalPosition);
  mWatchManager.Watch(mPlayState, &MediaDecoder::UpdateLogicalPosition);
  mWatchManager.Watch(mLogicallySeeking, &MediaDecoder::UpdateLogicalPosition);

  mWatchManager.Watch(mIsAudioDataAudible,
                      &MediaDecoder::NotifyAudibleStateChanged);

  mWatchManager.Watch(mVolume, &MediaDecoder::NotifyVolumeChanged);

  mVideoDecodingOberver->RegisterEvent();
}

#undef INIT_MIRROR
#undef INIT_CANONICAL

void MediaDecoder::Shutdown() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(!IsShutdown());

  // Unwatch all watch targets to prevent further notifications.
  mWatchManager.Shutdown();

  DiscardOngoingSeekIfExists();

  // This changes the decoder state to SHUTDOWN and does other things
  // necessary to unblock the state machine thread if it's blocked, so
  // the asynchronous shutdown in nsDestroyStateMachine won't deadlock.
  if (mDecoderStateMachine) {
    ShutdownStateMachine()->Then(mAbstractMainThread, __func__, this,
                                 &MediaDecoder::FinishShutdown,
                                 &MediaDecoder::FinishShutdown);
  } else {
    // Ensure we always unregister asynchronously in order not to disrupt
    // the hashtable iterating in MediaShutdownManager::Shutdown().
    RefPtr<MediaDecoder> self = this;
    nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(
        "MediaDecoder::Shutdown", [self]() { self->ShutdownInternal(); });
    mAbstractMainThread->Dispatch(r.forget());
  }

  ChangeState(PLAY_STATE_SHUTDOWN);
  mVideoDecodingOberver->UnregisterEvent();
  mVideoDecodingOberver = nullptr;
  mOwner = nullptr;
}

void MediaDecoder::NotifyXPCOMShutdown() {
  MOZ_ASSERT(NS_IsMainThread());
  // NotifyXPCOMShutdown will clear its reference to mDecoder. So we must ensure
  // that this MediaDecoder stays alive until completion.
  RefPtr<MediaDecoder> kungFuDeathGrip = this;
  if (auto* owner = GetOwner()) {
    owner->NotifyXPCOMShutdown();
  } else if (!IsShutdown()) {
    Shutdown();
  }
  MOZ_DIAGNOSTIC_ASSERT(IsShutdown());
}

MediaDecoder::~MediaDecoder() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(IsShutdown());
  MediaMemoryTracker::RemoveMediaDecoder(this);
}

void MediaDecoder::OnPlaybackEvent(MediaPlaybackEvent&& aEvent) {
  switch (aEvent.mType) {
    case MediaPlaybackEvent::PlaybackEnded:
      PlaybackEnded();
      break;
    case MediaPlaybackEvent::SeekStarted:
      SeekingStarted();
      break;
    case MediaPlaybackEvent::Invalidate:
      Invalidate();
      break;
    case MediaPlaybackEvent::EnterVideoSuspend:
      GetOwner()->QueueEvent(u"mozentervideosuspend"_ns);
      mIsVideoDecodingSuspended = true;
      break;
    case MediaPlaybackEvent::ExitVideoSuspend:
      GetOwner()->QueueEvent(u"mozexitvideosuspend"_ns);
      mIsVideoDecodingSuspended = false;
      break;
    case MediaPlaybackEvent::StartVideoSuspendTimer:
      GetOwner()->QueueEvent(u"mozstartvideosuspendtimer"_ns);
      break;
    case MediaPlaybackEvent::CancelVideoSuspendTimer:
      GetOwner()->QueueEvent(u"mozcancelvideosuspendtimer"_ns);
      break;
    case MediaPlaybackEvent::VideoOnlySeekBegin:
      GetOwner()->QueueEvent(u"mozvideoonlyseekbegin"_ns);
      break;
    case MediaPlaybackEvent::VideoOnlySeekCompleted:
      GetOwner()->QueueEvent(u"mozvideoonlyseekcompleted"_ns);
      break;
    default:
      break;
  }
}

bool MediaDecoder::IsVideoDecodingSuspended() const {
  return mIsVideoDecodingSuspended;
}

void MediaDecoder::OnPlaybackErrorEvent(const MediaResult& aError) {
  MOZ_ASSERT(NS_IsMainThread());
#ifdef MOZ_WMF_MEDIA_ENGINE
  if (aError == NS_ERROR_DOM_MEDIA_EXTERNAL_ENGINE_NOT_SUPPORTED_ERR ||
      aError == NS_ERROR_DOM_MEDIA_CDM_PROXY_NOT_SUPPORTED_ERR) {
    SwitchStateMachine(aError);
    return;
  }
#endif
  DecodeError(aError);
}

#ifdef MOZ_WMF_MEDIA_ENGINE
void MediaDecoder::SwitchStateMachine(const MediaResult& aError) {
  MOZ_ASSERT(aError == NS_ERROR_DOM_MEDIA_EXTERNAL_ENGINE_NOT_SUPPORTED_ERR ||
             aError == NS_ERROR_DOM_MEDIA_CDM_PROXY_NOT_SUPPORTED_ERR);
  // Already in shutting down decoder, no need to create another state machine.
  if (mPlayState == PLAY_STATE_SHUTDOWN) {
    return;
  }

  // External engine can't play the resource or we intentionally disable it, try
  // to use our own state machine again. Here we will create a new state machine
  // immediately and asynchrously shutdown the old one because we don't want to
  // dispatch any task to the old state machine. Therefore, we will disconnect
  // anything related with the old state machine, create a new state machine and
  // setup events/mirror/etc, then shutdown the old one and release its
  // reference once it finishes shutdown.
  RefPtr<MediaDecoderStateMachineBase> discardStateMachine =
      mDecoderStateMachine;

  // Disconnect mirror and events first.
  SetStateMachine(nullptr);
  DisconnectEvents();

  // Recreate a state machine and shutdown the old one.
  bool needExternalEngine = false;
  if (aError == NS_ERROR_DOM_MEDIA_CDM_PROXY_NOT_SUPPORTED_ERR) {
#  ifdef MOZ_WMF_CDM
    if (aError.GetCDMProxy()->AsWMFCDMProxy()) {
      needExternalEngine = true;
    }
#  endif
  }
  LOG("Need to create a new %s state machine",
      needExternalEngine ? "external engine" : "normal");

  nsresult rv = CreateAndInitStateMachine(
      discardStateMachine->IsLiveStream(),
      !needExternalEngine /* disable external engine */);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    LOG("Failed to create a new state machine!");
    glean::mfcdm::ErrorExtra extraData;
    extraData.errorName = Some("FAILED_TO_FALLBACK_TO_STATE_MACHINE"_ns);
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
    glean::mfcdm::error.Record(Some(extraData));
    if (MOZ_LOG_TEST(gMediaDecoderLog, LogLevel::Debug)) {
      nsPrintfCString logMessage{"MFCDM Error event, error=%s",
                                 extraData.errorName->get()};
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
      LOG("%s", logMessage.get());
    }
  }

  // Some attributes might have been set on the destroyed state machine, and
  // won't be reflected on the new MDSM by the state mirroring. We need to
  // update them manually later, after MDSM finished reading the
  // metadata because the MDSM might not be ready to perform the operations yet.
  mPendingStatusUpdateForNewlyCreatedStateMachine = true;

  // If there is ongoing seek performed on the old MDSM, cancel it because we
  // will perform seeking later again and don't want the old seeking affecting
  // us.
  DiscardOngoingSeekIfExists();

  discardStateMachine->BeginShutdown()->Then(
      AbstractThread::MainThread(), __func__, [discardStateMachine] {});
}
#endif

void MediaDecoder::OnDecoderDoctorEvent(DecoderDoctorEvent aEvent) {
  MOZ_ASSERT(NS_IsMainThread());
  // OnDecoderDoctorEvent is disconnected at shutdown time.
  MOZ_DIAGNOSTIC_ASSERT(!IsShutdown());
  Document* doc = GetOwner()->GetDocument();
  if (!doc) {
    return;
  }
  DecoderDoctorDiagnostics diags;
  diags.StoreEvent(doc, aEvent, __func__);
}

void MediaDecoder::OnNextFrameStatus(
    MediaDecoderOwner::NextFrameStatus aStatus) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(!IsShutdown());
  if (mNextFrameStatus != aStatus) {
    LOG("Changed mNextFrameStatus to %s",
        MediaDecoderOwner::EnumValueToString(aStatus));
    mNextFrameStatus = aStatus;
    UpdateReadyState();
  }
}

void MediaDecoder::OnTrackInfoUpdated(const VideoInfo& aVideoInfo,
                                      const AudioInfo& aAudioInfo) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(!IsShutdown());

  // Note that we don't check HasVideo() or HasAudio() here, because
  // those are checks for existing validity. If we always set the values
  // to what we receive, then we can go from not-video to video, for
  // example.
  mInfo->mVideo = aVideoInfo;
  mInfo->mAudio = aAudioInfo;

  Invalidate();

  EnsureTelemetryReported();
}

void MediaDecoder::OnSecondaryVideoContainerInstalled(
    const RefPtr<VideoFrameContainer>& aSecondaryVideoContainer) {
  MOZ_ASSERT(NS_IsMainThread());
  GetOwner()->OnSecondaryVideoContainerInstalled(aSecondaryVideoContainer);
}

void MediaDecoder::ShutdownInternal() {
  MOZ_ASSERT(NS_IsMainThread());
  mVideoFrameContainer = nullptr;
  mSecondaryVideoContainer = nullptr;
  MediaShutdownManager::Instance().Unregister(this);
}

void MediaDecoder::FinishShutdown() {
  MOZ_ASSERT(NS_IsMainThread());
  SetStateMachine(nullptr);
  ShutdownInternal();
}

nsresult MediaDecoder::CreateAndInitStateMachine(bool aIsLiveStream,
                                                 bool aDisableExternalEngine) {
  MOZ_ASSERT(NS_IsMainThread());
  SetStateMachine(CreateStateMachine(aDisableExternalEngine));

  NS_ENSURE_TRUE(GetStateMachine(), NS_ERROR_FAILURE);
  GetStateMachine()->DispatchIsLiveStream(aIsLiveStream);

  mMDSMCreationTime = Some(TimeStamp::Now());
  nsresult rv = mDecoderStateMachine->Init(this);
  NS_ENSURE_SUCCESS(rv, rv);

  // If some parameters got set before the state machine got created,
  // set them now
  SetStateMachineParameters();

  return NS_OK;
}

void MediaDecoder::SetStateMachineParameters() {
  MOZ_ASSERT(NS_IsMainThread());
  if (mPlaybackRate != 1 && mPlaybackRate != 0) {
    mDecoderStateMachine->DispatchSetPlaybackRate(mPlaybackRate);
  }
  mTimedMetadataListener = mDecoderStateMachine->TimedMetadataEvent().Connect(
      mAbstractMainThread, this, &MediaDecoder::OnMetadataUpdate);
  mMetadataLoadedListener = mDecoderStateMachine->MetadataLoadedEvent().Connect(
      mAbstractMainThread, this, &MediaDecoder::MetadataLoaded);
  mFirstFrameLoadedListener =
      mDecoderStateMachine->FirstFrameLoadedEvent().Connect(
          mAbstractMainThread, this, &MediaDecoder::FirstFrameLoaded);

  mOnPlaybackEvent = mDecoderStateMachine->OnPlaybackEvent().Connect(
      mAbstractMainThread, this, &MediaDecoder::OnPlaybackEvent);
  mOnPlaybackErrorEvent = mDecoderStateMachine->OnPlaybackErrorEvent().Connect(
      mAbstractMainThread, this, &MediaDecoder::OnPlaybackErrorEvent);
  mOnDecoderDoctorEvent = mDecoderStateMachine->OnDecoderDoctorEvent().Connect(
      mAbstractMainThread, this, &MediaDecoder::OnDecoderDoctorEvent);
  mOnMediaNotSeekable = mDecoderStateMachine->OnMediaNotSeekable().Connect(
      mAbstractMainThread, this, &MediaDecoder::OnMediaNotSeekable);
  mOnNextFrameStatus = mDecoderStateMachine->OnNextFrameStatus().Connect(
      mAbstractMainThread, this, &MediaDecoder::OnNextFrameStatus);
  mOnTrackInfoUpdated = mDecoderStateMachine->OnTrackInfoUpdatedEvent().Connect(
      mAbstractMainThread, this, &MediaDecoder::OnTrackInfoUpdated);
  mOnSecondaryVideoContainerInstalled =
      mDecoderStateMachine->OnSecondaryVideoContainerInstalled().Connect(
          mAbstractMainThread, this,
          &MediaDecoder::OnSecondaryVideoContainerInstalled);
  mOnEncrypted = mReader->OnEncrypted().Connect(
      mAbstractMainThread, GetOwner(), &MediaDecoderOwner::DispatchEncrypted);
  mOnWaitingForKey = mReader->OnWaitingForKey().Connect(
      mAbstractMainThread, GetOwner(), &MediaDecoderOwner::NotifyWaitingForKey);
  mOnDecodeWarning = mReader->OnDecodeWarning().Connect(
      mAbstractMainThread, GetOwner(), &MediaDecoderOwner::DecodeWarning);
}

void MediaDecoder::DisconnectEvents() {
  MOZ_ASSERT(NS_IsMainThread());
  mTimedMetadataListener.Disconnect();
  mMetadataLoadedListener.Disconnect();
  mFirstFrameLoadedListener.Disconnect();
  mOnPlaybackEvent.Disconnect();
  mOnPlaybackErrorEvent.Disconnect();
  mOnDecoderDoctorEvent.Disconnect();
  mOnMediaNotSeekable.Disconnect();
  mOnEncrypted.Disconnect();
  mOnWaitingForKey.Disconnect();
  mOnDecodeWarning.Disconnect();
  mOnNextFrameStatus.Disconnect();
  mOnTrackInfoUpdated.Disconnect();
  mOnSecondaryVideoContainerInstalled.Disconnect();
}

RefPtr<ShutdownPromise> MediaDecoder::ShutdownStateMachine() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(GetStateMachine());
  DisconnectEvents();
  return mDecoderStateMachine->BeginShutdown();
}

void MediaDecoder::Play() {
  MOZ_ASSERT(NS_IsMainThread());

  NS_ASSERTION(mDecoderStateMachine != nullptr, "Should have state machine.");
  LOG("Play");
  if (mPlaybackRate == 0) {
    return;
  }

  if (IsEnded()) {
    Seek(0, SeekTarget::PrevSyncPoint);
    return;
  }

  if (mPlayState == PLAY_STATE_LOADING) {
    mNextState = PLAY_STATE_PLAYING;
    return;
  }

  ChangeState(PLAY_STATE_PLAYING);
}

void MediaDecoder::Seek(double aTime, SeekTarget::Type aSeekType) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(!IsShutdown());

  LOG("Seek, target=%f", aTime);
  MOZ_ASSERT(aTime >= 0.0, "Cannot seek to a negative value.");

  auto time = TimeUnit::FromSeconds(aTime);

  mLogicalPosition = aTime;
  mLogicallySeeking = true;
  SeekTarget target = SeekTarget(time, aSeekType);
  CallSeek(target);

  if (mPlayState == PLAY_STATE_ENDED) {
    ChangeState(GetOwner()->GetPaused() ? PLAY_STATE_PAUSED
                                        : PLAY_STATE_PLAYING);
  }
}

void MediaDecoder::SetDelaySeekMode(bool aShouldDelaySeek) {
  MOZ_ASSERT(NS_IsMainThread());
  LOG("SetDelaySeekMode, shouldDelaySeek=%d", aShouldDelaySeek);
  if (mShouldDelaySeek == aShouldDelaySeek) {
    return;
  }
  mShouldDelaySeek = aShouldDelaySeek;
  if (!mShouldDelaySeek && mDelayedSeekTarget) {
    Seek(mDelayedSeekTarget->GetTime().ToSeconds(),
         mDelayedSeekTarget->GetType());
    mDelayedSeekTarget.reset();
  }
}

void MediaDecoder::DiscardOngoingSeekIfExists() {
  MOZ_ASSERT(NS_IsMainThread());
  mSeekRequest.DisconnectIfExists();
}

void MediaDecoder::CallSeek(const SeekTarget& aTarget) {
  MOZ_ASSERT(NS_IsMainThread());
  if (mShouldDelaySeek) {
    LOG("Delay seek to %f and store it to delayed seek target",
        mDelayedSeekTarget->GetTime().ToSeconds());
    mDelayedSeekTarget = Some(aTarget);
    return;
  }
  DiscardOngoingSeekIfExists();
  mDecoderStateMachine->InvokeSeek(aTarget)
      ->Then(mAbstractMainThread, __func__, this, &MediaDecoder::OnSeekResolved,
             &MediaDecoder::OnSeekRejected)
      ->Track(mSeekRequest);
}

double MediaDecoder::GetCurrentTime() {
  MOZ_ASSERT(NS_IsMainThread());
  return mLogicalPosition;
}

void MediaDecoder::OnMetadataUpdate(TimedMetadata&& aMetadata) {
  MOZ_ASSERT(NS_IsMainThread());
  MetadataLoaded(MakeUnique<MediaInfo>(*aMetadata.mInfo),
                 UniquePtr<MetadataTags>(std::move(aMetadata.mTags)),
                 MediaDecoderEventVisibility::Observable);
  FirstFrameLoaded(std::move(aMetadata.mInfo),
                   MediaDecoderEventVisibility::Observable);
}

void MediaDecoder::MetadataLoaded(
    UniquePtr<MediaInfo> aInfo, UniquePtr<MetadataTags> aTags,
    MediaDecoderEventVisibility aEventVisibility) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(!IsShutdown());

  LOG("MetadataLoaded, channels=%u rate=%u hasAudio=%d hasVideo=%d",
      aInfo->mAudio.mChannels, aInfo->mAudio.mRate, aInfo->HasAudio(),
      aInfo->HasVideo());

  mMediaSeekable = aInfo->mMediaSeekable;
  mMediaSeekableOnlyInBufferedRanges =
      aInfo->mMediaSeekableOnlyInBufferedRanges;
  mInfo = std::move(aInfo);

  mTelemetryProbesReporter->OnMediaContentChanged(
      TelemetryProbesReporter::MediaInfoToMediaContent(*mInfo));

  // Make sure the element and the frame (if any) are told about
  // our new size.
  if (aEventVisibility != MediaDecoderEventVisibility::Suppressed) {
    mFiredMetadataLoaded = true;
    GetOwner()->MetadataLoaded(mInfo.get(), std::move(aTags));
  }
  // Invalidate() will end up calling GetOwner()->UpdateMediaSize with the last
  // dimensions retrieved from the video frame container. The video frame
  // container contains more up to date dimensions than aInfo.
  // So we call Invalidate() after calling GetOwner()->MetadataLoaded to ensure
  // the media element has the latest dimensions.
  Invalidate();

#ifdef MOZ_WMF_MEDIA_ENGINE
  SetStatusUpdateForNewlyCreatedStateMachineIfNeeded();
#endif

  EnsureTelemetryReported();
}

#ifdef MOZ_WMF_MEDIA_ENGINE
void MediaDecoder::SetStatusUpdateForNewlyCreatedStateMachineIfNeeded() {
  if (!mPendingStatusUpdateForNewlyCreatedStateMachine) {
    return;
  }
  mPendingStatusUpdateForNewlyCreatedStateMachine = false;
  LOG("Set pending statuses if necessary (mLogicallySeeking=%d, "
      "mLogicalPosition=%f, mPlaybackRate=%f)",
      mLogicallySeeking.Ref(), mLogicalPosition, mPlaybackRate);
  if (mLogicallySeeking) {
    Seek(mLogicalPosition, SeekTarget::Accurate);
  }
  if (mPlaybackRate != 0 && mPlaybackRate != 1.0) {
    mDecoderStateMachine->DispatchSetPlaybackRate(mPlaybackRate);
  }
}
#endif

void MediaDecoder::EnsureTelemetryReported() {
  MOZ_ASSERT(NS_IsMainThread());

  if (mTelemetryReported || !mInfo) {
    // Note: sometimes we get multiple MetadataLoaded calls (for example
    // for chained ogg). So we ensure we don't report duplicate results for
    // these resources.
    return;
  }

  nsTArray<nsCString> codecs;
  if (mInfo->HasAudio() &&
      !mInfo->mAudio.GetAsAudioInfo()->mMimeType.IsEmpty()) {
    codecs.AppendElement(mInfo->mAudio.GetAsAudioInfo()->mMimeType);
  }
  if (mInfo->HasVideo() &&
      !mInfo->mVideo.GetAsVideoInfo()->mMimeType.IsEmpty()) {
    codecs.AppendElement(mInfo->mVideo.GetAsVideoInfo()->mMimeType);
  }
  if (codecs.IsEmpty()) {
    codecs.AppendElement(nsPrintfCString(
        "resource; %s", ContainerType().OriginalString().Data()));
  }
  for (const nsCString& codec : codecs) {
    LOG("Telemetry MEDIA_CODEC_USED= '%s'", codec.get());
    glean::media::codec_used.Get(codec).Add(1);
  }

  mTelemetryReported = true;
}

void MediaDecoder::FirstFrameLoaded(
    UniquePtr<MediaInfo> aInfo, MediaDecoderEventVisibility aEventVisibility) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(!IsShutdown());

  LOG("FirstFrameLoaded, channels=%u rate=%u hasAudio=%d hasVideo=%d "
      "mPlayState=%s transportSeekable=%d",
      aInfo->mAudio.mChannels, aInfo->mAudio.mRate, aInfo->HasAudio(),
      aInfo->HasVideo(), EnumValueToString(mPlayState), IsTransportSeekable());

  mInfo = std::move(aInfo);
  mTelemetryProbesReporter->OnMediaContentChanged(
      TelemetryProbesReporter::MediaInfoToMediaContent(*mInfo));

  Invalidate();

  // The element can run javascript via events
  // before reaching here, so only change the
  // state if we're still set to the original
  // loading state.
  if (mPlayState == PLAY_STATE_LOADING) {
    ChangeState(mNextState);
  }

  // We only care about video first frame.
  if (mInfo->HasVideo() && mMDSMCreationTime) {
    auto info = MakeUnique<dom::MediaDecoderDebugInfo>();
    RequestDebugInfo(*info)->Then(
        GetMainThreadSerialEventTarget(), __func__,
        [self = RefPtr<MediaDecoder>{this}, this, now = TimeStamp::Now(),
         creationTime = *mMDSMCreationTime, result = std::move(info)](
            GenericPromise::ResolveOrRejectValue&& aValue) mutable {
          if (IsShutdown()) {
            return;
          }
          if (aValue.IsReject()) {
            NS_WARNING("Failed to get debug info for the first frame probe!");
            return;
          }
          auto firstFrameLoadedTime = (now - creationTime).ToMilliseconds();
          MOZ_ASSERT(result->mReader.mTotalReadMetadataTimeMs >= 0.0);
          MOZ_ASSERT(result->mReader.mTotalWaitingForVideoDataTimeMs >= 0.0);
          MOZ_ASSERT(result->mStateMachine.mTotalBufferingTimeMs >= 0.0);

          using FirstFrameLoadedFlag =
              TelemetryProbesReporter::FirstFrameLoadedFlag;
          TelemetryProbesReporter::FirstFrameLoadedFlagSet flags;
          if (IsMSE()) {
            flags += FirstFrameLoadedFlag::IsMSE;
          }
          if (mDecoderStateMachine->IsExternalEngineStateMachine()) {
            flags += FirstFrameLoadedFlag::IsExternalEngineStateMachine;
          }
          if (IsHLSDecoder()) {
            flags += FirstFrameLoadedFlag::IsHLS;
          }
          if (result->mReader.mVideoHardwareAccelerated) {
            flags += FirstFrameLoadedFlag::IsHardwareDecoding;
          }
          mTelemetryProbesReporter->OnFirstFrameLoaded(
              firstFrameLoadedTime, result->mReader.mTotalReadMetadataTimeMs,
              result->mReader.mTotalWaitingForVideoDataTimeMs,
              result->mStateMachine.mTotalBufferingTimeMs, flags, *mInfo,
              NS_ConvertUTF16toUTF8(result->mReader.mVideoDecoderName));
        });
    mMDSMCreationTime.reset();
  }

  // GetOwner()->FirstFrameLoaded() might call us back. Put it at the bottom of
  // this function to avoid unexpected shutdown from reentrant calls.
  if (aEventVisibility != MediaDecoderEventVisibility::Suppressed) {
    GetOwner()->FirstFrameLoaded();
  }
}

void MediaDecoder::NetworkError(const MediaResult& aError) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(!IsShutdown());
  GetOwner()->NetworkError(aError);
}

void MediaDecoder::DecodeError(const MediaResult& aError) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(!IsShutdown());
  LOG("DecodeError, type=%s, error=%s", ContainerType().OriginalString().get(),
      aError.ErrorName().get());
  GetOwner()->DecodeError(aError);
}

void MediaDecoder::UpdateSameOriginStatus(bool aSameOrigin) {
  MOZ_ASSERT(NS_IsMainThread());
  mSameOriginMedia = aSameOrigin;
}

bool MediaDecoder::IsSeeking() const {
  MOZ_ASSERT(NS_IsMainThread());
  return mLogicallySeeking;
}

bool MediaDecoder::OwnerHasError() const {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(!IsShutdown());
  return GetOwner()->HasError();
}

bool MediaDecoder::IsEnded() const {
  MOZ_ASSERT(NS_IsMainThread());
  return mPlayState == PLAY_STATE_ENDED;
}

bool MediaDecoder::IsShutdown() const {
  MOZ_ASSERT(NS_IsMainThread());
  return mPlayState == PLAY_STATE_SHUTDOWN;
}

void MediaDecoder::PlaybackEnded() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(!IsShutdown());

  if (mLogicallySeeking || mPlayState == PLAY_STATE_LOADING ||
      mPlayState == PLAY_STATE_ENDED) {
    LOG("MediaDecoder::PlaybackEnded bailed out, "
        "mLogicallySeeking=%d mPlayState=%s",
        mLogicallySeeking.Ref(), EnumValueToString(mPlayState));
    return;
  }

  LOG("MediaDecoder::PlaybackEnded");

  ChangeState(PLAY_STATE_ENDED);
  InvalidateWithFlags(VideoFrameContainer::INVALIDATE_FORCE);
  GetOwner()->PlaybackEnded();
}

void MediaDecoder::NotifyPrincipalChanged() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(!IsShutdown());
  GetOwner()->NotifyDecoderPrincipalChanged();
}

void MediaDecoder::OnSeekResolved() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(!IsShutdown());
  LOG("MediaDecoder::OnSeekResolved");
  mLogicallySeeking = false;

  // Ensure logical position is updated after seek.
  UpdateLogicalPositionInternal();
  mSeekRequest.Complete();

  GetOwner()->SeekCompleted();
}

void MediaDecoder::OnSeekRejected() {
  MOZ_ASSERT(NS_IsMainThread());
  LOG("MediaDecoder::OnSeekRejected");
  mSeekRequest.Complete();
  mLogicallySeeking = false;

  GetOwner()->SeekAborted();
}

void MediaDecoder::SeekingStarted() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(!IsShutdown());
  GetOwner()->SeekStarted();
}

void MediaDecoder::ChangeState(PlayState aState) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!IsShutdown(), "SHUTDOWN is the final state.");

  if (mNextState == aState) {
    mNextState = PLAY_STATE_PAUSED;
  }

  if (mPlayState != aState) {
    DDLOG(DDLogCategory::Property, "play_state", EnumValueToString(aState));
    LOG("Play state changes from %s to %s", EnumValueToString(mPlayState),
        EnumValueToString(aState));
    mPlayState = aState;
    UpdateTelemetryHelperBasedOnPlayState(aState);
  }
}

TelemetryProbesReporter::Visibility MediaDecoder::OwnerVisibility() const {
  return GetOwner()->IsActuallyInvisible() || mForcedHidden
             ? TelemetryProbesReporter::Visibility::eInvisible
             : TelemetryProbesReporter::Visibility::eVisible;
}

void MediaDecoder::UpdateTelemetryHelperBasedOnPlayState(
    PlayState aState) const {
  if (aState == PlayState::PLAY_STATE_PLAYING) {
    mTelemetryProbesReporter->OnPlay(
        OwnerVisibility(),
        TelemetryProbesReporter::MediaInfoToMediaContent(*mInfo),
        mVolume == 0.f);
  } else if (aState == PlayState::PLAY_STATE_PAUSED ||
             aState == PlayState::PLAY_STATE_ENDED) {
    mTelemetryProbesReporter->OnPause(OwnerVisibility());
  } else if (aState == PLAY_STATE_SHUTDOWN) {
    mTelemetryProbesReporter->OnShutdown();
  }
}

MediaDecoder::PositionUpdate MediaDecoder::GetPositionUpdateReason(
    double aPrevPos, const TimeUnit& aCurPos) const {
  MOZ_ASSERT(NS_IsMainThread());
  // If current position is earlier than previous position and we didn't do
  // seek, that means we looped back to the start position.
  const bool notSeeking = !mSeekRequest.Exists();
  if (mLooping && notSeeking && aCurPos.ToSeconds() < aPrevPos) {
    return PositionUpdate::eSeamlessLoopingSeeking;
  }
  return aPrevPos != aCurPos.ToSeconds() && notSeeking
             ? PositionUpdate::ePeriodicUpdate
             : PositionUpdate::eOther;
}

void MediaDecoder::UpdateLogicalPositionInternal() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(!IsShutdown());

  TimeUnit currentPosition = CurrentPosition();
  if (mPlayState == PLAY_STATE_ENDED) {
    currentPosition =
        std::max(currentPosition, mDuration.match(DurationToTimeUnit()));
  }

  const PositionUpdate reason =
      GetPositionUpdateReason(mLogicalPosition, currentPosition);
  switch (reason) {
    case PositionUpdate::ePeriodicUpdate:
      SetLogicalPosition(currentPosition);
      // This is actually defined in `TimeMarchesOn`, but we do that in decoder.
      // https://html.spec.whatwg.org/multipage/media.html#playing-the-media-resource:event-media-timeupdate-7
      // TODO (bug 1688137): should we move it back to `TimeMarchesOn`?
      GetOwner()->MaybeQueueTimeupdateEvent();
      break;
    case PositionUpdate::eSeamlessLoopingSeeking:
      // When seamless seeking occurs, seeking was performed on the demuxer so
      // the decoder doesn't know. That means decoder still thinks it's in
      // playing. Therefore, we have to manually call those methods to notify
      // the owner about seeking.
      GetOwner()->SeekStarted();
      SetLogicalPosition(currentPosition);
      GetOwner()->SeekCompleted();
      break;
    default:
      MOZ_ASSERT(reason == PositionUpdate::eOther);
      SetLogicalPosition(currentPosition);
      break;
  }

  // Invalidate the frame so any video data is displayed.
  // Do this before the timeupdate event so that if that
  // event runs JavaScript that queries the media size, the
  // frame has reflowed and the size updated beforehand.
  Invalidate();
}

void MediaDecoder::SetLogicalPosition(const TimeUnit& aNewPosition) {
  MOZ_ASSERT(NS_IsMainThread());
  if (TimeUnit::FromSeconds(mLogicalPosition) == aNewPosition ||
      mLogicalPosition == aNewPosition.ToSeconds()) {
    return;
  }
  mLogicalPosition = aNewPosition.ToSeconds();
  DDLOG(DDLogCategory::Property, "currentTime", mLogicalPosition);
}

void MediaDecoder::DurationChanged() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(!IsShutdown());

  Variant<TimeUnit, double> oldDuration = mDuration;

  // Use the explicit duration if we have one.
  // Otherwise use the duration mirrored from MDSM.
  if (mExplicitDuration.isSome()) {
    mDuration.emplace<double>(mExplicitDuration.ref());
  } else if (mStateMachineDuration.Ref().isSome()) {
    MOZ_ASSERT(mStateMachineDuration.Ref().ref().IsValid());
    mDuration.emplace<TimeUnit>(mStateMachineDuration.Ref().ref());
  }

  LOG("New duration: %s",
      mDuration.match(DurationToTimeUnit()).ToString().get());
  if (oldDuration.is<TimeUnit>() && oldDuration.as<TimeUnit>().IsValid()) {
    LOG("Old Duration %s",
        oldDuration.match(DurationToTimeUnit()).ToString().get());
  }

  if ((oldDuration.is<double>() || oldDuration.as<TimeUnit>().IsValid())) {
    if (mDuration.match(DurationToDouble()) ==
        oldDuration.match(DurationToDouble())) {
      return;
    }
  }

  LOG("Duration changed to %s",
      mDuration.match(DurationToTimeUnit()).ToString().get());

  // See https://www.w3.org/Bugs/Public/show_bug.cgi?id=28822 for a discussion
  // of whether we should fire durationchange on explicit infinity.
  if (mFiredMetadataLoaded &&
      (!std::isinf(mDuration.match(DurationToDouble())) ||
       mExplicitDuration.isSome())) {
    GetOwner()->QueueEvent(u"durationchange"_ns);
  }

  if (CurrentPosition().ToSeconds() > mDuration.match(DurationToDouble())) {
    Seek(mDuration.match(DurationToDouble()), SeekTarget::Accurate);
  }
}

already_AddRefed<KnowsCompositor> MediaDecoder::GetCompositor() {
  MediaDecoderOwner* owner = GetOwner();
  Document* ownerDoc = owner ? owner->GetDocument() : nullptr;
  WindowRenderer* renderer =
      ownerDoc ? nsContentUtils::WindowRendererForDocument(ownerDoc) : nullptr;
  RefPtr<KnowsCompositor> knows =
      renderer ? renderer->AsKnowsCompositor() : nullptr;
  return knows ? knows->GetForMedia().forget() : nullptr;
}

void MediaDecoder::NotifyCompositor() {
  RefPtr<KnowsCompositor> knowsCompositor = GetCompositor();
  if (knowsCompositor) {
    nsCOMPtr<nsIRunnable> r =
        NewRunnableMethod<already_AddRefed<KnowsCompositor>&&>(
            "MediaFormatReader::UpdateCompositor", mReader,
            &MediaFormatReader::UpdateCompositor, knowsCompositor.forget());
    Unused << mReader->OwnerThread()->Dispatch(r.forget());
  }
}

void MediaDecoder::SetElementVisibility(bool aIsOwnerInvisible,
                                        bool aIsOwnerConnected,
                                        bool aIsOwnerInBackground,
                                        bool aHasOwnerPendingCallbacks) {
  MOZ_ASSERT(NS_IsMainThread());
  mIsOwnerInvisible = aIsOwnerInvisible;
  mIsOwnerConnected = aIsOwnerConnected;
  mIsOwnerInBackground = aIsOwnerInBackground;
  mHasOwnerPendingCallbacks = aHasOwnerPendingCallbacks;
  mTelemetryProbesReporter->OnVisibilityChanged(OwnerVisibility());
  UpdateVideoDecodeMode();
}

void MediaDecoder::SetForcedHidden(bool aForcedHidden) {
  MOZ_ASSERT(NS_IsMainThread());
  mForcedHidden = aForcedHidden;
  mTelemetryProbesReporter->OnVisibilityChanged(OwnerVisibility());
  UpdateVideoDecodeMode();
}

void MediaDecoder::SetSuspendTaint(bool aTainted) {
  MOZ_ASSERT(NS_IsMainThread());
  mHasSuspendTaint = aTainted;
  UpdateVideoDecodeMode();
}

void MediaDecoder::UpdateVideoDecodeMode() {
  MOZ_ASSERT(NS_IsMainThread());

  // The MDSM may yet be set.
  if (!mDecoderStateMachine) {
    LOG("UpdateVideoDecodeMode(), early return because we don't have MDSM.");
    return;
  }

  // Seeking is required when leaving suspend mode.
  if (!mMediaSeekable) {
    LOG("UpdateVideoDecodeMode(), set Normal because the media is not "
        "seekable");
    mDecoderStateMachine->SetVideoDecodeMode(VideoDecodeMode::Normal);
    return;
  }

  // If mHasSuspendTaint is set, never suspend the video decoder.
  if (mHasSuspendTaint) {
    LOG("UpdateVideoDecodeMode(), set Normal because the element has been "
        "tainted.");
    mDecoderStateMachine->SetVideoDecodeMode(VideoDecodeMode::Normal);
    return;
  }

  // If mSecondaryVideoContainer is set, never suspend the video decoder.
  if (mSecondaryVideoContainer.Ref()) {
    LOG("UpdateVideoDecodeMode(), set Normal because the element is cloning "
        "itself visually to another video container.");
    mDecoderStateMachine->SetVideoDecodeMode(VideoDecodeMode::Normal);
    return;
  }

  // Don't suspend elements that is not in a connected tree.
  if (!mIsOwnerConnected) {
    LOG("UpdateVideoDecodeMode(), set Normal because the element is not in "
        "tree.");
    mDecoderStateMachine->SetVideoDecodeMode(VideoDecodeMode::Normal);
    return;
  }

  // Don't suspend elements that have pending rVFC callbacks.
  if (mHasOwnerPendingCallbacks && !mIsOwnerInBackground) {
    LOG("UpdateVideoDecodeMode(), set Normal because the element has pending "
        "callbacks while in foreground.");
    mDecoderStateMachine->SetVideoDecodeMode(VideoDecodeMode::Normal);
    return;
  }

  // If mForcedHidden is set, suspend the video decoder anyway.
  if (mForcedHidden) {
    LOG("UpdateVideoDecodeMode(), set Suspend because the element is forced to "
        "be suspended.");
    mDecoderStateMachine->SetVideoDecodeMode(VideoDecodeMode::Suspend);
    return;
  }

  // Resume decoding in the advance, even the element is in the background.
  if (mIsBackgroundVideoDecodingAllowed) {
    LOG("UpdateVideoDecodeMode(), set Normal because the tab is in background "
        "and hovered.");
    mDecoderStateMachine->SetVideoDecodeMode(VideoDecodeMode::Normal);
    return;
  }

  if (mIsOwnerInvisible) {
    LOG("UpdateVideoDecodeMode(), set Suspend because of invisible element.");
    mDecoderStateMachine->SetVideoDecodeMode(VideoDecodeMode::Suspend);
  } else {
    LOG("UpdateVideoDecodeMode(), set Normal because of visible element.");
    mDecoderStateMachine->SetVideoDecodeMode(VideoDecodeMode::Normal);
  }
}

void MediaDecoder::SetIsBackgroundVideoDecodingAllowed(bool aAllowed) {
  mIsBackgroundVideoDecodingAllowed = aAllowed;
  UpdateVideoDecodeMode();
}

bool MediaDecoder::HasSuspendTaint() const {
  MOZ_ASSERT(NS_IsMainThread());
  return mHasSuspendTaint;
}

void MediaDecoder::SetSecondaryVideoContainer(
    const RefPtr<VideoFrameContainer>& aSecondaryVideoContainer) {
  MOZ_ASSERT(NS_IsMainThread());
  if (mSecondaryVideoContainer.Ref() == aSecondaryVideoContainer) {
    return;
  }
  mSecondaryVideoContainer = aSecondaryVideoContainer;
  UpdateVideoDecodeMode();
}

bool MediaDecoder::IsMediaSeekable() {
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_TRUE(GetStateMachine(), false);
  return mMediaSeekable;
}

namespace {

// Returns zero, either as a TimeUnit or as a double.
template <typename T>
constexpr T Zero() {
  if constexpr (std::is_same<T, double>::value) {
    return 0.0;
  } else if constexpr (std::is_same<T, TimeUnit>::value) {
    return TimeUnit::Zero();
  }
  MOZ_RELEASE_ASSERT(false);
};

// Returns Infinity either as a TimeUnit or as a double.
template <typename T>
constexpr T Infinity() {
  if constexpr (std::is_same<T, double>::value) {
    return std::numeric_limits<double>::infinity();
  } else if constexpr (std::is_same<T, TimeUnit>::value) {
    return TimeUnit::FromInfinity();
  }
  MOZ_RELEASE_ASSERT(false);
};

};  // namespace

// This method can be made to return either TimeIntervals, that is a set of
// interval that are delimited with TimeUnit, or TimeRanges, that is a set of
// intervals that are delimited by seconds, as doubles.
// seekable often depends on the duration of a media, in the very common case
// where the seekable range is [0, duration]. When playing a MediaSource, the
// duration of a media element can be set as an arbitrary number, that are
// 64-bits floating point values.
// This allows returning an interval that is [0, duration], with duration being
// a double that cannot be represented as a TimeUnit, either because it has too
// many significant digits, or because it's outside of the int64_t range that
// TimeUnit internally uses.
template <typename IntervalType>
IntervalType MediaDecoder::GetSeekableImpl() {
  MOZ_ASSERT(NS_IsMainThread());
  if (std::isnan(GetDuration())) {
    // We do not have a duration yet, we can't determine the seekable range.
    return IntervalType();
  }

  // Compute [0, duration] -- When dealing with doubles, use ::GetDuration to
  // avoid rounding the value differently. When dealing with TimeUnit, it's
  // returned directly.
  typename IntervalType::InnerType duration;
  if constexpr (std::is_same<typename IntervalType::InnerType, double>::value) {
    duration = GetDuration();
  } else {
    duration = mDuration.as<TimeUnit>();
  }
  typename IntervalType::ElemType zeroToDuration =
      typename IntervalType::ElemType(
          Zero<typename IntervalType::InnerType>(),
          IsInfinite() ? Infinity<typename IntervalType::InnerType>()
                       : duration);
  auto buffered = IntervalType(GetBuffered());
  // Remove any negative range in the interval -- seeking to a non-positive
  // position isn't possible.
  auto positiveBuffered = buffered.Intersection(zeroToDuration);

  // We can seek in buffered range if the media is seekable. Also, we can seek
  // in unbuffered ranges if the transport level is seekable (local file or the
  // server supports range requests, etc.) or in cue-less WebMs
  if (mMediaSeekableOnlyInBufferedRanges) {
    return IntervalType(positiveBuffered);
  }
  if (!IsMediaSeekable()) {
    return IntervalType();
  }
  if (!IsTransportSeekable()) {
    return IntervalType(positiveBuffered);
  }

  // Common case: seeking is possible at any point of the stream.
  return IntervalType(zeroToDuration);
}

media::TimeIntervals MediaDecoder::GetSeekable() {
  return GetSeekableImpl<media::TimeIntervals>();
}

media::TimeRanges MediaDecoder::GetSeekableTimeRanges() {
  return GetSeekableImpl<media::TimeRanges>();
}

void MediaDecoder::SetFragmentEndTime(double aTime) {
  MOZ_ASSERT(NS_IsMainThread());
  if (mDecoderStateMachine) {
    mDecoderStateMachine->DispatchSetFragmentEndTime(
        TimeUnit::FromSeconds(aTime));
  }
}

void MediaDecoder::SetPlaybackRate(double aPlaybackRate) {
  MOZ_ASSERT(NS_IsMainThread());

  double oldRate = mPlaybackRate;
  mPlaybackRate = aPlaybackRate;
  if (aPlaybackRate == 0) {
    Pause();
    return;
  }

  if (oldRate == 0 && !GetOwner()->GetPaused()) {
    // PlaybackRate is no longer null.
    // Restart the playback if the media was playing.
    Play();
  }

  if (mDecoderStateMachine) {
    mDecoderStateMachine->DispatchSetPlaybackRate(aPlaybackRate);
  }
}

void MediaDecoder::SetPreservesPitch(bool aPreservesPitch) {
  MOZ_ASSERT(NS_IsMainThread());
  mPreservesPitch = aPreservesPitch;
}

void MediaDecoder::SetLooping(bool aLooping) {
  MOZ_ASSERT(NS_IsMainThread());
  mLooping = aLooping;
}

void MediaDecoder::SetStreamName(const nsAutoString& aStreamName) {
  MOZ_ASSERT(NS_IsMainThread());
  mStreamName = aStreamName;
}

void MediaDecoder::ConnectMirrors(MediaDecoderStateMachineBase* aObject) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aObject);
  mStateMachineDuration.Connect(aObject->CanonicalDuration());
  mBuffered.Connect(aObject->CanonicalBuffered());
  mCurrentPosition.Connect(aObject->CanonicalCurrentPosition());
  mIsAudioDataAudible.Connect(aObject->CanonicalIsAudioDataAudible());
}

void MediaDecoder::DisconnectMirrors() {
  MOZ_ASSERT(NS_IsMainThread());
  mStateMachineDuration.DisconnectIfConnected();
  mBuffered.DisconnectIfConnected();
  mCurrentPosition.DisconnectIfConnected();
  mIsAudioDataAudible.DisconnectIfConnected();
}

void MediaDecoder::SetStateMachine(
    MediaDecoderStateMachineBase* aStateMachine) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT_IF(aStateMachine, !mDecoderStateMachine);
  if (aStateMachine) {
    mDecoderStateMachine = aStateMachine;
    LOG("set state machine %p", mDecoderStateMachine.get());
    ConnectMirrors(aStateMachine);
    UpdateVideoDecodeMode();
  } else if (mDecoderStateMachine) {
    LOG("null out state machine %p", mDecoderStateMachine.get());
    mDecoderStateMachine = nullptr;
    DisconnectMirrors();
  }
}

ImageContainer* MediaDecoder::GetImageContainer() {
  return mVideoFrameContainer ? mVideoFrameContainer->GetImageContainer()
                              : nullptr;
}

void MediaDecoder::InvalidateWithFlags(uint32_t aFlags) {
  if (mVideoFrameContainer) {
    mVideoFrameContainer->InvalidateWithFlags(aFlags);
  }
}

void MediaDecoder::Invalidate() {
  if (mVideoFrameContainer) {
    mVideoFrameContainer->Invalidate();
  }
}

void MediaDecoder::Suspend() {
  MOZ_ASSERT(NS_IsMainThread());
  GetStateMachine()->InvokeSuspendMediaSink();
}

void MediaDecoder::Resume() {
  MOZ_ASSERT(NS_IsMainThread());
  GetStateMachine()->InvokeResumeMediaSink();
}

// Constructs the time ranges representing what segments of the media
// are buffered and playable.
media::TimeIntervals MediaDecoder::GetBuffered() {
  MOZ_ASSERT(NS_IsMainThread());
  return mBuffered.Ref();
}

size_t MediaDecoder::SizeOfVideoQueue() {
  MOZ_ASSERT(NS_IsMainThread());
  if (mDecoderStateMachine) {
    return mDecoderStateMachine->SizeOfVideoQueue();
  }
  return 0;
}

size_t MediaDecoder::SizeOfAudioQueue() {
  MOZ_ASSERT(NS_IsMainThread());
  if (mDecoderStateMachine) {
    return mDecoderStateMachine->SizeOfAudioQueue();
  }
  return 0;
}

void MediaDecoder::NotifyReaderDataArrived() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(!IsShutdown());

  nsresult rv = mReader->OwnerThread()->Dispatch(
      NewRunnableMethod("MediaFormatReader::NotifyDataArrived", mReader.get(),
                        &MediaFormatReader::NotifyDataArrived));
  MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
  Unused << rv;
}

// Provide access to the state machine object
MediaDecoderStateMachineBase* MediaDecoder::GetStateMachine() const {
  MOZ_ASSERT(NS_IsMainThread());
  return mDecoderStateMachine;
}

bool MediaDecoder::CanPlayThrough() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(!IsShutdown());
  return CanPlayThroughImpl();
}

RefPtr<SetCDMPromise> MediaDecoder::SetCDMProxy(CDMProxy* aProxy) {
  MOZ_ASSERT(NS_IsMainThread());
#ifdef MOZ_WMF_CDM
  if (aProxy) {
    nsresult rv = GetStateMachine()->IsCDMProxySupported(aProxy);
    if (rv == NS_ERROR_DOM_MEDIA_NOT_ALLOWED_ERR) {
      // We can't switch to another state machine because this CDM proxy type is
      // disabled by pref.
      LOG("CDM proxy %s not allowed!",
          NS_ConvertUTF16toUTF8(aProxy->KeySystem()).get());
      return SetCDMPromise::CreateAndReject(rv, __func__);
    }
    if (rv == NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR) {
      // Switch to another state machine if the current one doesn't support the
      // given CDM proxy.
      LOG("CDM proxy %s not supported! Switch to another state machine.",
          NS_ConvertUTF16toUTF8(aProxy->KeySystem()).get());
      SwitchStateMachine(
          MediaResult{NS_ERROR_DOM_MEDIA_CDM_PROXY_NOT_SUPPORTED_ERR, aProxy});
      rv = GetStateMachine()->IsCDMProxySupported(aProxy);
      if (NS_FAILED(rv)) {
        MOZ_DIAGNOSTIC_CRASH("CDM proxy not supported after switch!");
        LOG("CDM proxy not supported after switch!");
        return SetCDMPromise::CreateAndReject(rv, __func__);
      }
    }
    MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv), "CDM proxy not supported!");
  }
#endif
  return GetStateMachine()->SetCDMProxy(aProxy);
}

bool MediaDecoder::IsOpusEnabled() { return StaticPrefs::media_opus_enabled(); }

bool MediaDecoder::IsOggEnabled() { return StaticPrefs::media_ogg_enabled(); }

bool MediaDecoder::IsWaveEnabled() { return StaticPrefs::media_wave_enabled(); }

bool MediaDecoder::IsWebMEnabled() { return StaticPrefs::media_webm_enabled(); }

NS_IMETHODIMP
MediaMemoryTracker::CollectReports(nsIHandleReportCallback* aHandleReport,
                                   nsISupports* aData, bool aAnonymize) {
  // NB: When resourceSizes' ref count goes to 0 the promise will report the
  //     resources memory and finish the asynchronous memory report.
  RefPtr<MediaDecoder::ResourceSizes> resourceSizes =
      new MediaDecoder::ResourceSizes(MediaMemoryTracker::MallocSizeOf);

  nsCOMPtr<nsIHandleReportCallback> handleReport = aHandleReport;
  nsCOMPtr<nsISupports> data = aData;

  resourceSizes->Promise()->Then(
      AbstractThread::MainThread(), __func__,
      [handleReport, data](size_t size) {
        handleReport->Callback(
            ""_ns, "explicit/media/resources"_ns, KIND_HEAP, UNITS_BYTES,
            static_cast<int64_t>(size),
            nsLiteralCString("Memory used by media resources including "
                             "streaming buffers, caches, etc."),
            data);

        nsCOMPtr<nsIMemoryReporterManager> imgr =
            do_GetService("@mozilla.org/memory-reporter-manager;1");

        if (imgr) {
          imgr->EndReport();
        }
      },
      [](size_t) { /* unused reject function */ });

  int64_t video = 0;
  int64_t audio = 0;
  DecodersArray& decoders = Decoders();
  for (size_t i = 0; i < decoders.Length(); ++i) {
    MediaDecoder* decoder = decoders[i];
    video += static_cast<int64_t>(decoder->SizeOfVideoQueue());
    audio += static_cast<int64_t>(decoder->SizeOfAudioQueue());
    decoder->AddSizeOfResources(resourceSizes);
  }

  MOZ_COLLECT_REPORT("explicit/media/decoded/video", KIND_HEAP, UNITS_BYTES,
                     video, "Memory used by decoded video frames.");

  MOZ_COLLECT_REPORT("explicit/media/decoded/audio", KIND_HEAP, UNITS_BYTES,
                     audio, "Memory used by decoded audio chunks.");

  return NS_OK;
}

MediaDecoderOwner* MediaDecoder::GetOwner() const {
  MOZ_ASSERT(NS_IsMainThread());
  // mOwner is valid until shutdown.
  return mOwner;
}

MediaDecoderOwner::NextFrameStatus MediaDecoder::NextFrameBufferedStatus() {
  MOZ_ASSERT(NS_IsMainThread());
  // Next frame hasn't been decoded yet.
  // Use the buffered range to consider if we have the next frame available.
  auto currentPosition = CurrentPosition();
  media::TimeInterval interval(
      currentPosition, currentPosition + DEFAULT_NEXT_FRAME_AVAILABLE_BUFFERED);
  return GetBuffered().Contains(interval)
             ? MediaDecoderOwner::NEXT_FRAME_AVAILABLE
             : MediaDecoderOwner::NEXT_FRAME_UNAVAILABLE;
}

void MediaDecoder::GetDebugInfo(dom::MediaDecoderDebugInfo& aInfo) {
  MOZ_ASSERT(NS_IsMainThread());
  CopyUTF8toUTF16(nsPrintfCString("%p", this), aInfo.mInstance);
  aInfo.mChannels = mInfo ? mInfo->mAudio.mChannels : 0;
  aInfo.mRate = mInfo ? mInfo->mAudio.mRate : 0;
  aInfo.mHasAudio = mInfo ? mInfo->HasAudio() : false;
  aInfo.mHasVideo = mInfo ? mInfo->HasVideo() : false;
  CopyUTF8toUTF16(MakeStringSpan(EnumValueToString(mPlayState)),
                  aInfo.mPlayState);
  aInfo.mContainerType =
      NS_ConvertUTF8toUTF16(ContainerType().Type().AsString());
}

RefPtr<GenericPromise> MediaDecoder::RequestDebugInfo(
    MediaDecoderDebugInfo& aInfo) {
  MOZ_DIAGNOSTIC_ASSERT(!IsShutdown());
  if (!NS_IsMainThread()) {
    // Run the request on the main thread if it's not already.
    return InvokeAsync(AbstractThread::MainThread(), __func__,
                       [this, self = RefPtr{this}, &aInfo]() {
                         return RequestDebugInfo(aInfo);
                       });
  }
  GetDebugInfo(aInfo);

  return mReader->RequestDebugInfo(aInfo.mReader)
      ->Then(AbstractThread::MainThread(), __func__,
             [this, self = RefPtr{this}, &aInfo] {
               if (!GetStateMachine()) {
                 return GenericPromise::CreateAndResolve(true, __func__);
               }
               return GetStateMachine()->RequestDebugInfo(aInfo.mStateMachine);
             });
}

void MediaDecoder::NotifyAudibleStateChanged() {
  MOZ_DIAGNOSTIC_ASSERT(!IsShutdown());
  GetOwner()->SetAudibleState(mIsAudioDataAudible);
  mTelemetryProbesReporter->OnAudibleChanged(
      mIsAudioDataAudible ? TelemetryProbesReporter::AudibleState::eAudible
                          : TelemetryProbesReporter::AudibleState::eNotAudible);
}

void MediaDecoder::NotifyVolumeChanged() {
  MOZ_DIAGNOSTIC_ASSERT(!IsShutdown());
  mTelemetryProbesReporter->OnMutedChanged(mVolume == 0.f);
}

double MediaDecoder::GetTotalVideoPlayTimeInSeconds() const {
  return mTelemetryProbesReporter->GetTotalVideoPlayTimeInSeconds();
}

double MediaDecoder::GetTotalVideoHDRPlayTimeInSeconds() const {
  return mTelemetryProbesReporter->GetTotalVideoHDRPlayTimeInSeconds();
}

double MediaDecoder::GetVisibleVideoPlayTimeInSeconds() const {
  return mTelemetryProbesReporter->GetVisibleVideoPlayTimeInSeconds();
}

double MediaDecoder::GetInvisibleVideoPlayTimeInSeconds() const {
  return mTelemetryProbesReporter->GetInvisibleVideoPlayTimeInSeconds();
}

double MediaDecoder::GetTotalAudioPlayTimeInSeconds() const {
  return mTelemetryProbesReporter->GetTotalAudioPlayTimeInSeconds();
}

double MediaDecoder::GetAudiblePlayTimeInSeconds() const {
  return mTelemetryProbesReporter->GetAudiblePlayTimeInSeconds();
}

double MediaDecoder::GetInaudiblePlayTimeInSeconds() const {
  return mTelemetryProbesReporter->GetInaudiblePlayTimeInSeconds();
}

double MediaDecoder::GetMutedPlayTimeInSeconds() const {
  return mTelemetryProbesReporter->GetMutedPlayTimeInSeconds();
}

MediaMemoryTracker::MediaMemoryTracker() = default;

void MediaMemoryTracker::InitMemoryReporter() {
  RegisterWeakAsyncMemoryReporter(this);
}

MediaMemoryTracker::~MediaMemoryTracker() {
  UnregisterWeakMemoryReporter(this);
}

}  // namespace mozilla

// avoid redefined macro in unified build
#undef DUMP
#undef LOG
#undef NS_DispatchToMainThread
