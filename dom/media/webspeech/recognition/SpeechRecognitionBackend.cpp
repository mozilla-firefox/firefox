/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2  et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SpeechRecognitionBackend.h"

#include <speex/speex_resampler.h>

#include <algorithm>
#include <utility>

#include "AudibilityMonitor.h"
#include "AudioConfig.h"
#include "AudioConverter.h"
#include "MainThreadUtils.h"
#include "SpeechRecognition.h"
#include "SpeechTrackListener.h"
#include "mozilla/AbstractThread.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/Assertions.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/PodOperations.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/AudioStreamTrack.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/SpeechRecognitionBinding.h"
#include "mozilla/glean/DomMediaWebspeechMetrics.h"
#include "mozilla/hwinference/PSpeechRecognition.h"
#include "mozilla/hwinference/SpeechRecognitionChild.h"
#include "mozilla/ipc/MessageChannel.h"
#include "mozilla/ipc/ProtocolUtils.h"
#include "nsCOMPtr.h"
#include "nsProxyRelease.h"
#include "nsString.h"

namespace mozilla::dom {

using namespace mozilla::ipc;

StaticAutoPtr<mozilla::EventTargetCapability<nsISerialEventTarget>>
    SpeechRecognitionBackend::sIPCCapability;
int32_t SpeechRecognitionBackend::sIPCActorUsers = 0;
StaticRefPtr<nsITimer> SpeechRecognitionBackend::sIdleCloseTimer;

static LazyLogModule gSpeechRecognitionBackendLog("SpeechRecognitionBackend");

#define LOG(fmt, ...)                                                      \
  MOZ_LOG_FMT(gSpeechRecognitionBackendLog, mozilla::LogLevel::Debug, fmt, \
              ##__VA_ARGS__)
#define LOGV(fmt, ...)                                                       \
  MOZ_LOG_FMT(gSpeechRecognitionBackendLog, mozilla::LogLevel::Verbose, fmt, \
              ##__VA_ARGS__)
#define LOGE(fmt, ...)                                                     \
  MOZ_LOG_FMT(gSpeechRecognitionBackendLog, mozilla::LogLevel::Error, fmt, \
              ##__VA_ARGS__)

// How long the shared IPC thread stays idle before its backing OS thread is
// released. The serial event target itself lives for the process lifetime.
static constexpr uint32_t IPC_THREAD_IDLE_TIMEOUT_MS = 5000;

/* static */
void SpeechRecognitionBackend::CancelIdleCloseTimer() {
  if (sIdleCloseTimer) {
    sIdleCloseTimer->Cancel();
    sIdleCloseTimer = nullptr;
  }
}

/* static */
void SpeechRecognitionBackend::AcquireIPCActorUser() {
  AssertIsOnMainThread();
  bool connectionHeld = sIdleCloseTimer;
  // A new user within the grace period means the connection is wanted again,
  // so the idle close must not fire.
  CancelIdleCloseTimer();
  if (sIPCActorUsers++ || connectionHeld) {
    return;
  }

  ContentChild::GetSingleton()->SendAcquireHWInferenceProcess();
}

/* static */
void SpeechRecognitionBackend::ReleaseIPCActorUser() {
  AssertIsOnMainThread();
  MOZ_ASSERT(sIPCActorUsers > 0);
  if (--sIPCActorUsers) {
    return;
  }

  uint32_t graceMs =
      StaticPrefs::media_webspeech_recognition_idle_shutdown_grace_ms();
  // Past shutdown there is nothing left to keep the connection open for, and
  // no shutdown hook left to cancel a timer armed now: close immediately.
  if (!graceMs ||
      AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
    ContentChild::GetSingleton()->SendReleaseHWInferenceConnection();
    return;
  }

  // The idle timer must not outlive XPCOM: a still-armed timer released from a
  // static destructor crashes in nsTimerImpl::CancelImpl, once the timer
  // thread is gone. Registered here rather than alongside the IPC thread
  // because a keepalive can be acquired and dropped - arming the timer -
  // without any session ever creating that thread.
  static bool sRegisteredShutdownBlocker = false;
  if (!sRegisteredShutdownBlocker) {
    sRegisteredShutdownBlocker = true;
    RunOnShutdown([]() {
      AssertIsOnMainThread();
      CancelIdleCloseTimer();
    });
  }

  LOG("Last HWInference user gone, closing the connection in {}ms", graceMs);
  nsCOMPtr<nsITimer> timer;
  nsresult rv = NS_NewTimerWithCallback(
      getter_AddRefs(timer),
      [](nsITimer*) {
        AssertIsOnMainThread();
        // Only still armed if nothing acquired in the meantime, since
        // AcquireIPCActorUser() cancels the timer.
        sIdleCloseTimer = nullptr;
        ContentChild::GetSingleton()->SendReleaseHWInferenceConnection();
      },
      graceMs, nsITimer::TYPE_ONE_SHOT,
      "SpeechRecognitionBackend::IdleClose"_ns);

  if (NS_FAILED(rv)) {
    ContentChild::GetSingleton()->SendReleaseHWInferenceConnection();
    return;
  }
  sIdleCloseTimer = timer.forget();
}

SpeechRecognitionIPCActorUserGuard::SpeechRecognitionIPCActorUserGuard() {
  SpeechRecognitionBackend::AcquireIPCActorUser();
}

SpeechRecognitionIPCActorUserGuard::~SpeechRecognitionIPCActorUserGuard() {
  if (NS_IsMainThread()) {
    SpeechRecognitionBackend::ReleaseIPCActorUser();
  } else {
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "SpeechRecognitionIPCActorUserGuard::Release", [] {
          AssertIsOnMainThread();
          SpeechRecognitionBackend::ReleaseIPCActorUser();
        }));
  }
}

// Audio is streamed to the inference process in small blocks to keep end-to-end
// latency low. The block is well under the model's internal chunk (~80 ms), so
// the buffering delay is hidden behind the model's own latency rather than
// adding to it.
static constexpr double IPC_BLOCK_SIZE_S = 0.04;
// Depth costs no latency (the whole buffer is drained every poll), but having
// this significantly larger than the block size allows not dropping audio
// during long stalls under load -- some of the threads used to convey the audio
// aren't real-time, and can be starved.
static constexpr double RING_BUFFER_SIZE_S = 2.0;
static constexpr uint32_t STREAMING_POLL_MS = 20;
static constexpr int32_t SPEECH_RECOGNITION_TARGET_RATE = 16000;
static constexpr auto SPEECH_RECOGNITION_ENGINE_ID = "parakeet-cpp"_ns;
// DataCallback() downmixes into mMonoBuffer in slices of at most this many
// frames, so this is the only allocation the graph thread normally needs.
static constexpr uint32_t PER_CALLBACK_MONO_BUFFER_INITIAL_NUM_FRAMES = 512;

/* static */
already_AddRefed<SpeechRecognitionBackend> SpeechRecognitionBackend::Create(
    SpeechRecognition* aParent, uint32_t aGraphRate, const nsString& aLanguage,
    const nsTArray<nsString>& aPhrases) {
  AssertIsOnMainThread();

  // The resampling thread's whole lifetime is owned by the main thread:
  // created here, shut down by Shutdown(). It exists before the backend does
  // so that mResamplingCapability, which has no empty state, is bound to it
  // for the backend's whole lifetime.
  nsCOMPtr<nsIThread> resamplingThread;
  nsresult rv =
      NS_NewNamedThread("SpeechResampler", getter_AddRefs(resamplingThread));
  if (NS_FAILED(rv)) {
    LOGE("Failed to create the resampling thread: {:x}",
         static_cast<uint32_t>(rv));
    return nullptr;
  }

  return RefPtr<SpeechRecognitionBackend>(
             new SpeechRecognitionBackend(aParent, resamplingThread, aGraphRate,
                                          aLanguage, aPhrases))
      .forget();
}

SpeechRecognitionBackend::SpeechRecognitionBackend(
    SpeechRecognition* aParent, nsIThread* aResamplingThread,
    uint32_t aGraphRate, const nsString& aLanguage,
    const nsTArray<nsString>& aPhrases)
    : mParent(aParent),
      mLanguage(NS_ConvertUTF16toUTF8(aLanguage)),
      mPhrases(aPhrases.Clone()),
      mRingBuffer(std::make_unique<SPSCQueue<float>>(
          AssertedCast<int>(aGraphRate * RING_BUFFER_SIZE_S))),
      mResamplingThread(aResamplingThread),
      mResamplingCapability(aResamplingThread),
      mMonoBuffer(PER_CALLBACK_MONO_BUFFER_INITIAL_NUM_FRAMES),
      mGraphRate(aGraphRate) {}

SpeechRecognitionBackend::~SpeechRecognitionBackend() {
  AssertIsOnMainThread();
  MOZ_ASSERT(mStopped, "SpeechRecognition must Stop() or Abort() the backend");
}

void SpeechRecognitionBackend::Start() {
  AssertIsOnMainThread();
  LOG("SpeechRecognitionBackend::Start");

#ifdef DEBUG
  {
    auto session = mSession.Lock();
    MOZ_ASSERT(!session->mChild);
  }
#endif

  mAudibilityMonitor = std::make_unique<AudibilityMonitor>(mGraphRate, 0.5f);

  CreateSession(
      [self = RefPtr{this}](hwinference::SpeechRecognitionChild* aChild) {
        AssertOnIPCThread();
        if (!aChild) {
          LOGE("Failed to create speech recognition session");
          self->HandleRecognitionError(nsCString("network"));
          return;
        }
        self->StartSpeechRecognitionSession(self->mLanguage, aChild);
      });
}

void SpeechRecognitionBackend::Stop() {
  Shutdown(/* aWaitForFlush */ true, TrailingEvents::Fire);
}

void SpeechRecognitionBackend::Abort(TrailingEvents aTrailingEvents) {
  LOG("SpeechRecognitionBackend::Abort");
  // https://webaudio.github.io/web-speech-api/#dom-speechrecognition-abort
  // "stop listening and stop recognizing and do not return any information":
  // unlike stop(), no end-of-stream flush to wait for.
  Shutdown(/* aWaitForFlush */ false, aTrailingEvents);
}

// The whole shutdown sequence runs here, on the main thread, in this order:
//
// - disconnect the graph track feeding the ring buffer
// - publish mStopRequested so work already in flight on the other two threads
//   gives up
// - queue the trailing DOM events
// - hand the actor to the IPC thread to stop or abort
// - stop the resampling loop
// - and shut down its thread
//
// The events are queued before the last three steps, not after, so that
// everything they make another thread dispatch to the main thread - a result
// the end-of-stream flush produces, the flush's answer that ends the session -
// is queued behind them, leaving "end" last.
void SpeechRecognitionBackend::Shutdown(bool aWaitForFlush,
                                        TrailingEvents aTrailingEvents) {
  AssertIsOnMainThread();
  LOG("SpeechRecognitionBackend::Shutdown waitForFlush={}", aWaitForFlush);

  // Idempotent: JS can call stop() then abort()
  if (mStopped) {
    return;
  }
  mStopped = true;

  // SpeechTrackListener holds a strong RefPtr<SpeechRecognitionBackend> while
  // registered, detaching needs to happen first.
  DetachFromTrack();

  RefPtr<hwinference::SpeechRecognitionChild> childToStop;
  {
    auto session = mSession.Lock();
    session->mStopRequested = true;
    childToStop = std::move(session->mChild);
  }
  MOZ_ASSERT_IF(childToStop, sIPCCapability);
  const bool hadSession = !!childToStop;

  if (aTrailingEvents == TrailingEvents::Fire) {
    DispatchTrailingEvents();
  }

  if (hadSession && aWaitForFlush) {
    nsCOMPtr<nsIRunnable> stopSession = NS_NewRunnableFunction(
        "SpeechRecognitionBackend::StopSession",
        [self = RefPtr{this}, child = std::move(childToStop)]() {
          AssertOnIPCThread();
          LOG("Stopping HWInference speech recognition session");
          if (!child->CanSend()) {
            self->NotifySessionFinished(/* aProducedResult */ true,
                                        EnginePerfStats{});
            return;
          }
          child->SendStop()->Then(
              GetCurrentSerialEventTarget(), __func__,
              [self, child](hwinference::PSpeechRecognitionChild::StopPromise::
                                ResolveOrRejectValue&& aValue) {
                child->Close();
                if (aValue.IsReject()) {
                  // A dead channel means the engine never reported back, so
                  // don't claim a nomatch it never determined.
                  self->NotifySessionFinished(/* aProducedResult */ true,
                                              EnginePerfStats{});
                  return;
                }
                const auto& [any, fedAudioMs, inferenceMs] =
                    aValue.ResolveValue();
                self->NotifySessionFinished(
                    any, EnginePerfStats{fedAudioMs, inferenceMs});
              });
        });
    sIPCCapability->Dispatch(stopSession.forget());
  } else if (hadSession) {
    nsCOMPtr<nsIRunnable> abortSession = NS_NewRunnableFunction(
        "SpeechRecognitionBackend::AbortSession",
        [child = std::move(childToStop)]() {
          AssertOnIPCThread();
          LOG("Aborting HWInference speech recognition session");
          if (child->CanSend()) {
            child->SendStop();
            child->Close();
          }
        });
    sIPCCapability->Dispatch(abortSession.forget());
  } else if (aWaitForFlush) {
    // Nothing ever reached the engine, so there is no flush to wait for. Still
    // queued after the audioend above, so "end" stays last.
    NotifySessionFinished(/* aProducedResult */ true, EnginePerfStats{});
  }

  // Dispatch to the resampling thread to tell it to stop.
  nsCOMPtr<nsIRunnable> stop = NS_NewRunnableFunction(
      "SpeechRecognitionBackend::StopProcessingAudio", [self = RefPtr{this}]() {
        self->mResamplingCapability.AssertOnCurrentThread();
        self->mAudioProcessingStopped = true;
      });
  mResamplingThread->Dispatch(stop.forget());
  // nsIThread::Shutdown() blocks by spinning a nested event loop, which is
  // unsafe from e.g. global teardown; AsyncShutdown() just requests it.
  mResamplingThread->AsyncShutdown();
  mResamplingThread = nullptr;
}

void SpeechRecognitionBackend::DispatchTrailingEvents() {
  AssertIsOnMainThread();

  // Closes the pairs this session opened, then reports that capture is over.
  // https://webaudio.github.io/web-speech-api/#eventdef-speechrecognition-audioend
  // "Fired when the user agent has finished capturing audio."
  //
  // Not DispatchToParentIfAlive: abort() drops this backend before the task
  // runs, and these are not results the aborted session has to withhold. No
  // newer session can be current by then either, since start() throws until
  // "end" has fired.
  const bool speechDetected = std::exchange(mSpeechDetected, false);
  const bool audible = std::exchange(mCurrentlyAudible, false);

  NS_DispatchToMainThread(
      NS_NewRunnableFunction("SpeechRecognitionBackend::DispatchTrailingEvents",
                             [self = RefPtr{this}, speechDetected, audible]() {
                               AssertIsOnMainThread();
                               RefPtr<SpeechRecognition> parent(self->mParent);
                               if (!parent) {
                                 return;
                               }
                               if (speechDetected) {
                                 parent->DispatchTrustedEvent(u"speechend"_ns);
                               }
                               if (audible) {
                                 parent->DispatchTrustedEvent(u"soundend"_ns);
                               }
                               parent->DispatchTrustedEvent(u"audioend"_ns);
                             }));
}

void SpeechRecognitionBackend::NotifySessionFinished(bool aProducedResult,
                                                     EnginePerfStats aStats) {
  DispatchToParentIfAlive(
      "SpeechRecognitionBackend::NotifySessionFinished",
      [aProducedResult, aStats](SpeechRecognition* aParent) {
        aParent->OnSessionFinished(aProducedResult, aStats);
      });
}

void SpeechRecognitionBackend::AttachToTrack(AudioStreamTrack* aTrack) {
  AssertIsOnMainThread();
  MOZ_ASSERT(aTrack);
  MOZ_ASSERT(!mTrack, "Already attached to a track");
  MOZ_ASSERT(!mTrackListener);

  mTrack = aTrack;
  mTrackListener = SpeechTrackListener::Create(this);
  mTrack->AddListener(mTrackListener);

  LOG("SpeechRecognitionBackend::AttachToTrack");
}

void SpeechRecognitionBackend::SetEnabled(bool aEnabled) {
  AssertIsOnMainThread();

  if (!mTrack) {
    return;
  }

  mTrack->GetTrack()->QueueControlMessageWithNoShutdown(
      [self = RefPtr{this}, aEnabled] { self->mEnabled = aEnabled; });
}

void SpeechRecognitionBackend::DetachFromTrack() {
  AssertIsOnMainThread();

  if (!mTrack) {
    return;
  }

  LOG("SpeechRecognitionBackend::DetachFromTrack");

  if (mTrackListener) {
    mTrack->RemoveListener(mTrackListener);
    mTrackListener = nullptr;
  }

  mTrack = nullptr;
}

void SpeechRecognitionBackend::DataCallback(MediaTrackGraph* aGraph,
                                            TrackTime aTime,
                                            const AudioChunk& aChunk) {
  aGraph->AssertOnGraphThread();

  if (aChunk.mDuration == 0) {
    return;
  }

  // Real-time thread: lock- and allocation-free.
  double nowUs =
      (TimeStamp::Now() - TimeStamp::ProcessCreation()).ToMicroseconds();
  mLastTrackPositionRef.Write({aTime, int64_t(nowUs)});

  const size_t frameCount = static_cast<size_t>(aChunk.mDuration);
  // A null chunk is silence the graph did not bother to materialize, not an
  // absence of audio, so it is fed as zeros rather than dropped. Dropping it
  // would splice together the audio on either side of a silent gap, hiding the
  // silence that ends an utterance from the recognizer.
  const bool isSilence = aChunk.IsNull() || !mEnabled;

  // Downmix to mono into the fixed-size scratch buffer and enqueue. A single
  // graph chunk can be larger than the scratch buffer, so process it in slices
  // that fit, avoiding any allocation on the real-time graph thread.
  AudioDataValue* monoData = mMonoBuffer.Elements();
  Span<AudioDataValue* const> outputChannels(&monoData, 1);
  const size_t capacity = mMonoBuffer.Capacity();

  for (size_t offset = 0; offset < frameCount; offset += capacity) {
    const size_t sliceFrames = std::min(capacity, frameCount - offset);
    mMonoBuffer.SetLengthAndRetainStorage(sliceFrames);

    if (isSilence) {
      PodZero(mMonoBuffer.Elements(), sliceFrames);
    } else {
      AudioChunk slice = aChunk;
      slice.SliceTo(offset, offset + sliceFrames);
      slice.DownMixTo(outputChannels);
    }

    int written = mRingBuffer->Enqueue(mMonoBuffer.Elements(),
                                       AssertedCast<int>(sliceFrames));
    if (written < static_cast<int>(sliceFrames)) {
      mFramesDropped += sliceFrames - written;
      LOGE(
          "Capture ring buffer overflow: wrote {} of {} frames, {}s"
          " dropped total",
          written, sliceFrames, double(mFramesDropped) / mGraphRate);
    }
  }
}

TimeStamp SpeechRecognitionBackend::CaptureTimeForTrackPosition(
    TrackTime aPosition) {
  SampleTimeReference ref = mLastTrackPositionRef.Read();
  if (ref.mTimeUs == 0) {
    return TimeStamp();
  }
  TimeStamp refTimeStamp = TimeStamp::ProcessCreation() +
                           TimeDuration::FromMicroseconds(ref.mTimeUs);
  return EstimateSampleTimeStamp(ref.mPosition, refTimeStamp, aPosition,
                                 mGraphRate);
}

void SpeechRecognitionBackend::ProcessAudioChunk() {
  mResamplingCapability.AssertOnCurrentThread();
  // Both "teardown happened while the session was being initialized" and
  // "teardown happened mid-loop" end up here: this is the only thing that
  // stops the loop, and it never goes back to false.
  if (mAudioProcessingStopped) {
    LOG("Resampling loop stopped, not scheduling next audio chunk");
    return;
  }

  LOGV("ProcessAudioChunk, {} frames of graph-rate audio queued",
       mRingBuffer->AvailableRead());

  if (!mAudioConverter) {
    AudioConfig inputConfig(1, mGraphRate, AudioConfig::FORMAT_FLT);
    AudioConfig outputConfig(1, SPEECH_RECOGNITION_TARGET_RATE,
                             AudioConfig::FORMAT_FLT);
    mAudioConverter = std::make_unique<AudioConverter>(
        inputConfig, outputConfig, SPEEX_RESAMPLER_QUALITY_MIN);
  }

  int available = mRingBuffer->AvailableRead();
  double secondsAvailable = AssertedCast<double>(available) / mGraphRate;
  if (secondsAvailable > IPC_BLOCK_SIZE_S) {
    nsTArray<float> audioBuffer;
    audioBuffer.SetLength(available);
    int read = mRingBuffer->Dequeue(audioBuffer.Elements(), available);
    mFramesDequeuedTotal += read;
    TimeStamp captureEndTime =
        CaptureTimeForTrackPosition(mFramesDequeuedTotal);

    if (!mAudioStartDispatched) {
      mAudioStartDispatched = true;
      // Position 0 = capture start, not "now" (which would lag by the ring
      // buffer/IPC block delay).
      TimeStamp audioStartTs = CaptureTimeForTrackPosition(0);
      DispatchToParentIfAlive("SpeechRecognitionBackend::DispatchAudioStart",
                              [audioStartTs](SpeechRecognition* aParent) {
                                aParent->DispatchTrustedEventWithTimestamp(
                                    u"audiostart"_ns, audioStartTs);
                              });
    }

    if (mAudibilityMonitor) {
      const float* audioData = audioBuffer.Elements();
      mAudibilityMonitor->ProcessPlanar(Span<const float* const>(&audioData, 1),
                                        read);

      bool nowAudible = mAudibilityMonitor->RecentlyAudible();
      if (nowAudible != mAudible) {
        mAudible = nowAudible;

        // The soundstart/soundend pair is decided on the main thread, which is
        // also where the session ends, rather than here: a transition this
        // thread reports after Shutdown() has already closed the pair is a
        // straggler from a chunk that was in flight, and dropping it is what
        // keeps the two events paired and ahead of "audioend".
        DispatchToParentIfAlive(
            "SpeechRecognitionBackend::DispatchSoundEvent",
            [self = RefPtr{this}, nowAudible](SpeechRecognition* aParent) {
              AssertIsOnMainThread();
              if (self->mStopped || self->mCurrentlyAudible == nowAudible) {
                return;
              }
              self->mCurrentlyAudible = nowAudible;
              aParent->DispatchTrustedEvent(nowAudible ? u"soundstart"_ns
                                                       : u"soundend"_ns);
            });
      }
    }

    nsTArray<float> resampledBuffer;
    size_t frames =
        mAudioConverter->Process(resampledBuffer, audioBuffer.Elements(), read);
    if (!frames) {
      LOGE("AudioConverter::Process failed; dropping this audio chunk");
    } else {
      LOGV("Sending {}s of audio via IPC",
           static_cast<float>(frames) / SPEECH_RECOGNITION_TARGET_RATE);
      SendAudioDataViaIPC(std::move(resampledBuffer), captureEndTime);
    }
  } else {
    LOGV("Not enough data in ringbuffer ({}s), retrying in a bit",
         secondsAvailable);
  }

  nsCOMPtr<nsIRunnable> nextChunk = NS_NewRunnableFunction(
      "SpeechRecognitionBackend::ProcessAudioChunk", [self = RefPtr{this}]() {
        self->mResamplingCapability.AssertOnCurrentThread();
        self->ProcessAudioChunk();
      });

  // Rescheduled through the capability, which is this very thread, rather than
  // through mResamplingThread allows us to put a main-thread capability on the
  // thread handle.
  //
  // A failure here means Shutdown() has already asked the thread to go away, so
  // failing to redispatch is what we want.
  mResamplingCapability.GetEventTarget()->DelayedDispatch(nextChunk.forget(),
                                                          STREAMING_POLL_MS);
}

void SpeechRecognitionBackend::SendAudioDataViaIPC(nsTArray<float>&& aAudioData,
                                                   TimeStamp aCaptureEndTime) {
  mResamplingCapability.AssertOnCurrentThread();

  nsCOMPtr<nsIRunnable> sendAudio = NS_NewRunnableFunction(
      "SpeechRecognitionBackend::SendAudioData",
      [self = RefPtr{this}, audioData = std::move(aAudioData),
       aCaptureEndTime]() mutable {
        RefPtr<hwinference::SpeechRecognitionChild> child;
        {
          auto session = self->mSession.Lock();
          child = session->mChild;
        }
        if (child && child->CanSend()) {
          size_t sampleCount = audioData.Length();
          child->SendProcessAudioData(std::move(audioData), aCaptureEndTime);
          LOGV("Sent {} samples to HWInference", sampleCount);
        } else {
          LOGE("SpeechRecognitionChild not available, dropping {} samples",
               audioData.Length());
        }
      });
  sIPCCapability->Dispatch(sendAudio.forget());
}

void SpeechRecognitionBackend::StartSpeechRecognitionSession(
    const nsACString& aLanguage, hwinference::SpeechRecognitionChild* aChild) {
  AssertOnIPCThread();
  MOZ_ASSERT(aChild);

  {
    auto session = mSession.Lock();
    if (session->mStopRequested) {
      // Shutdown() publishes mStopRequested and takes mChild away under this
      // same lock, so it cannot have seen this actor: closing it here is what
      // ends the session in the utility process, and not doing so would leave
      // the process-wide session slot taken for good.
      LOG("Session init skipped, teardown already requested");
      aChild->Close();
      return;
    }
    session->mChild = aChild;
  }

  // The callbacks hold this instance with a strong ref, this instance holds the
  // callback with refs through mSession. This cycles is broken:
  // - from the IPC side in SpeechRecognitionChild::ActorDestroy
  // - from the main thread here in ::Shutdown()
  aChild->SetResultCallback(
      [self = RefPtr{this}](const nsCString& aTranscript, bool aIsFinal,
                            float aConfidence, TimeStamp aEventTime) {
        AssertOnIPCThread();
        LOG("Received recognition result: {} (final={}, confidence={})",
            aTranscript.get(), aIsFinal, aConfidence);

        self->HandleRecognitionResult(aTranscript, aIsFinal, aConfidence,
                                      aEventTime);
      });

  aChild->SetErrorCallback([self = RefPtr{this}](const nsCString& aError) {
    AssertOnIPCThread();
    LOGE("Recognition error: {}", aError.get());

    self->HandleRecognitionError(aError);
  });

  aChild->SetSpeechChangeCallback(
      [self = RefPtr{this}](bool aSpeechDetected, TimeStamp aEventTime) {
        AssertOnIPCThread();
        LOG("Speech change: {}", aSpeechDetected ? "started" : "ended");

        self->DispatchToParentIfAlive(
            "SpeechRecognitionBackend::HandleSpeechChange",
            [self, aSpeechDetected, aEventTime](SpeechRecognition* aParent) {
              AssertIsOnMainThread();
              // Like the soundstart/soundend pair, the main thread owns this
              // one, so that teardown is what closes it and a transition
              // reported after that is dropped.
              if (self->mStopped || self->mSpeechDetected == aSpeechDetected) {
                return;
              }
              self->mSpeechDetected = aSpeechDetected;
              aParent->DispatchTrustedEventWithTimestamp(
                  aSpeechDetected ? u"speechstart"_ns : u"speechend"_ns,
                  aEventTime);
            });
      });

  aChild->SetDestroyedCallback(
      [self = RefPtr{this}](hwinference::SpeechRecognitionChild* aDestroyed) {
        AssertOnIPCThread();
        auto session = self->mSession.Lock();
        if (session->mChild == aDestroyed) {
          session->mChild = nullptr;
        }
      });

  TimeStamp initStart = TimeStamp::Now();
  aChild->SendInit(SPEECH_RECOGNITION_ENGINE_ID, aLanguage, mPhrases)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self = RefPtr{this}, initStart](const nsCString& aError) {
            AssertOnIPCThread();
            if (!aError.IsEmpty()) {
              LOGE("Failed to initialize speech recognition session: {}",
                   aError.get());
              self->HandleRecognitionError(aError);
            } else {
              LOG("Speech recognition session initialized successfully");
              glean::media_speech_recognition::session_init_time
                  .AccumulateRawDuration(TimeStamp::Now() - initStart);
              self->DispatchToParentIfAlive(
                  "SpeechRecognitionBackend::NotifyBackendListening",
                  [](SpeechRecognition* aParent) {
                    aParent->NotifyBackendListening();
                  });
              // This needs to be a fallible dispatch because this promise can
              // resolve after e.g. `Stop()` has been called. In this case, it's
              // fine to fail to dispatch, it's what we want.
              nsCOMPtr<nsIRunnable> runnable = NS_NewRunnableFunction(
                  "SpeechRecognitionBackend::ProcessAudioOnBackgroundThread",
                  [self]() {
                    self->mResamplingCapability.AssertOnCurrentThread();
                    self->ProcessAudioChunk();
                  });
              self->mResamplingCapability.Dispatch(runnable.forget(),
                                                   NS_DISPATCH_FALLIBLE);
            }
          },
          [self = RefPtr{this}](ResponseRejectReason aReason) {
            LOGE("Init IPC call failed: {}", static_cast<int>(aReason));
            AssertOnIPCThread();
            // The session actor is gone: the HWInference process died or never
            // launched, e.g. we reached max restart count.
            self->HandleRecognitionError(nsCString("service-not-allowed"));
          });
}

void SpeechRecognitionBackend::HandleRecognitionResult(
    const nsACString& aTranscript, bool aIsFinal, float aConfidence,
    TimeStamp aEventTime) {
  AssertOnIPCThread();
  LOG("HandleRecognitionResult: {} (final={}, conf={})",
      nsCString(aTranscript).get(), aIsFinal, aConfidence);

  DispatchToParentIfAlive(
      "SpeechRecognitionBackend::HandleRecognitionResult",
      [transcript = nsCString(aTranscript), aIsFinal, aConfidence,
       aEventTime](SpeechRecognition* aParent) {
        aParent->HandleRecognitionResultFromBackend(transcript, aIsFinal,
                                                    aConfidence, aEventTime);
      });
}

void SpeechRecognitionBackend::HandleRecognitionError(
    const nsACString& aError) {
  AssertOnIPCThread();
  LOGE("HandleRecognitionError: {}", nsCString(aError).get());

  // A stopped session ends through stop()'s own path, with "nomatch" and
  // "end". A failure the engine reports while it winds down - an init
  // abandoned because the session went away, say - is not the page's problem,
  // and firing "error" here would claim the session broke when it merely
  // ended.
  DispatchToParentIfAlive("SpeechRecognitionBackend::HandleRecognitionError",
                          [self = RefPtr{this}, error = nsCString(aError)](
                              SpeechRecognition* aParent) {
                            AssertIsOnMainThread();
                            if (self->mStopped) {
                              return;
                            }
                            aParent->HandleRecognitionErrorFromBackend(error);
                          });
}

void SpeechRecognitionBackend::NotifyTrackEnded() {
  DispatchToParentIfAlive("SpeechRecognitionBackend::NotifyTrackEnded",
                          [](SpeechRecognition* aParent) { aParent->Stop(); });
}

/* static */
void SpeechRecognitionBackend::EnsureIPCThread() {
  AssertIsOnMainThread();

  if (!sIPCCapability) {
    // IPC actors are bound to the event target they were opened on, so the
    // target has to outlive them: a LazyIdleThread keeps a single one for the
    // process lifetime, and only releases its backing OS thread when idle.
    RefPtr<LazyIdleThread> thread =
        new LazyIdleThread(IPC_THREAD_IDLE_TIMEOUT_MS, "SpeechIPC");
    sIPCCapability = new EventTargetCapability<nsISerialEventTarget>(thread);
    LOG("Created shared IPC thread for speech recognition");
    ClearOnShutdown(&sIPCCapability);
  }
}

/* static */
void SpeechRecognitionBackend::AssertOnIPCThread() {
  sIPCCapability->AssertOnCurrentThread();
}

template <typename Func>
void SpeechRecognitionBackend::DispatchToParentIfAlive(const char* aName,
                                                       Func&& aFunc) {
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      aName,
      [self = RefPtr{this}, aFunc = std::forward<Func>(aFunc)]() mutable {
        AssertIsOnMainThread();
        RefPtr<SpeechRecognition> parent(self->mParent);
        // Also drop this if a newer backend has since replaced self on the
        // parent (e.g. stop() immediately followed by start()): self's
        // callback was already in flight when it was superseded, and its
        // notification belongs to the session it was created for, not
        // whichever one happens to be current by the time this runs.
        if (!parent || !parent->IsCurrentBackend(self.get())) {
          return;
        }
        aFunc(parent.get());
      }));
}

/* static */
template <typename SendFunc>
auto SpeechRecognitionBackend::RunWithTransientSession(SendFunc&& aSendFunc) {
  AssertIsOnMainThread();

  using SendPromise = typename decltype(aSendFunc(
      std::declval<hwinference::SpeechRecognitionChild*>()))::element_type;
  using ResolveValueType = typename SendPromise::ResolveValueType;
  using OperationPromise = MozPromise<ResolveValueType, nsresult, true>;

  MozPromiseHolder<OperationPromise> holder;
  RefPtr<OperationPromise> operation = holder.Ensure(__func__);
  CreateSession([holder = std::move(holder),
                 aSendFunc = std::forward<SendFunc>(aSendFunc)](
                    hwinference::SpeechRecognitionChild* aChild) mutable {
    AssertOnIPCThread();
    if (!aChild) {
      holder.Reject(NS_ERROR_FAILURE, __func__);
      return;
    }
    RefPtr child = aChild;
    aSendFunc(aChild)->Then(
        GetCurrentSerialEventTarget(), __func__,
        [holder = std::move(holder),
         child](typename SendPromise::ResolveOrRejectValue&& aValue) mutable {
          AssertOnIPCThread();
          child->Close();
          if (aValue.IsReject()) {
            holder.Reject(NS_ERROR_FAILURE, __func__);
            return;
          }
          holder.Resolve(std::move(aValue.ResolveValue()), __func__);
        });
  });
  return operation;
}

/* static */
void SpeechRecognitionBackend::ResolveAvailability(Promise* aPromise,
                                                   AvailabilityStatus aStatus) {
  AssertIsOnMainThread();
  using Label = glean::media_speech_recognition::AvailabilityLabel;
  Label label;
  switch (aStatus) {
    case AvailabilityStatus::Unavailable:
      label = Label::eUnavailable;
      break;
    case AvailabilityStatus::Downloadable:
      label = Label::eDownloadable;
      break;
    case AvailabilityStatus::Downloading:
      label = Label::eDownloading;
      break;
    case AvailabilityStatus::Available:
      label = Label::eAvailable;
      break;
    default:
      MOZ_ASSERT_UNREACHABLE(
          "Unhandled AvailabilityStatus, add a label for it in metrics.yaml");
      label = Label::e__Other__;
      break;
  }
  glean::media_speech_recognition::availability.EnumGet(label).Add(1);
  aPromise->MaybeResolve(aStatus);
}

/* static */
already_AddRefed<Promise> SpeechRecognitionBackend::Available(
    nsIGlobalObject* aGlobal, const nsTArray<nsCString>& aLanguages) {
  AssertIsOnMainThread();

  if (!aGlobal) {
    return nullptr;
  }

  ErrorResult rv;
  RefPtr<Promise> promise = Promise::Create(aGlobal, rv);
  if (rv.Failed()) {
    return nullptr;
  }

  nsTArray<nsCString> languages = aLanguages.Clone();
  if (languages.IsEmpty()) {
    languages.AppendElement("en-US"_ns);
  }

  LOG("SpeechRecognitionBackend::Available - Starting availability check for "
      "{} languages",
      languages.Length());

  if (MOZ_LOG_TEST(gSpeechRecognitionBackendLog, LogLevel::Debug)) {
    for (const auto& lang : languages) {
      LOG("SpeechRecognitionBackend::Available - Language requested: {}",
          lang.get());
    }
  }

  // https://webaudio.github.io/web-speech-api/#availability-algorithm
  // step 5.2.2: "available" if installed, "downloadable" if supported by
  // the user agent but not yet installed, "unavailable" if not supported.
  // IsModelAvailable alone can't tell "installed" from "not installed but
  // fetchable" apart (ModelHub.isModelAvailable returns true for both), so
  // check IsModelInstalled first for "available"; IsModelAvailable then
  // distinguishes "downloadable" from "unavailable".
  using IsModelInstalledPromise =
      hwinference::PSpeechRecognitionChild::IsModelInstalledPromise;
  using AvailabilityPromise = MozPromise<AvailabilityStatus, nsresult, true>;
  // RunWithTransientSession turns any rejection into NS_ERROR_FAILURE, so the
  // IPC reject reason is forwarded verbatim rather than converted at each step.
  using SendAvailabilityPromise =
      MozPromise<AvailabilityStatus, ResponseRejectReason, true>;
  RunWithTransientSession(
      [languages = std::move(languages)](
          hwinference::SpeechRecognitionChild* aChild) mutable
          -> RefPtr<SendAvailabilityPromise> {
        RefPtr<hwinference::SpeechRecognitionChild> child = aChild;
        return IsModelInstalledNative(child, languages)
            ->Then(GetCurrentSerialEventTarget(), __func__,
                   [child, languages = std::move(languages)](
                       IsModelInstalledPromise::ResolveOrRejectValue&& aValue)
                       -> RefPtr<SendAvailabilityPromise> {
                     if (aValue.IsReject()) {
                       return SendAvailabilityPromise::CreateAndReject(
                           aValue.RejectValue(), __func__);
                     }
                     if (aValue.ResolveValue()) {
                       return SendAvailabilityPromise::CreateAndResolve(
                           AvailabilityStatus::Available, __func__);
                     }
                     return child->SendIsModelAvailable(languages)->Map(
                         GetCurrentSerialEventTarget(), __func__,
                         [](bool aAvailable) {
                           return aAvailable ? AvailabilityStatus::Downloadable
                                             : AvailabilityStatus::Unavailable;
                         });
                   });
      })
      ->Then(GetMainThreadSerialEventTarget(), __func__,
             [promise](AvailabilityPromise::ResolveOrRejectValue&& aValue) {
               ResolveAvailability(promise,
                                   aValue.IsResolve()
                                       ? aValue.ResolveValue()
                                       : AvailabilityStatus::Unavailable);
             });

  return promise.forget();
}

/* static */
RefPtr<hwinference::PSpeechRecognitionChild::IsModelInstalledPromise>
SpeechRecognitionBackend::IsModelInstalledNative(
    hwinference::SpeechRecognitionChild* aChild,
    const nsTArray<nsCString>& aLanguages) {
  AssertOnIPCThread();

  LOG("SpeechRecognitionBackend::IsModelInstalledNative - Starting installed "
      "check for {} languages",
      aLanguages.Length());

  return aChild->SendIsModelInstalled(aLanguages);
}

/* static */
void SpeechRecognitionBackend::CreateSession(
    MoveOnlyFunction<void(hwinference::SpeechRecognitionChild*)> aCallback) {
  AssertIsOnMainThread();

  RefPtr<SpeechRecognitionIPCActorUserGuard> guard =
      MakeRefPtr<SpeechRecognitionIPCActorUserGuard>();
  EnsureIPCThread();

  Endpoint<hwinference::PSpeechRecognitionParent> parentEndpoint;
  Endpoint<hwinference::PSpeechRecognitionChild> childEndpoint;
  MOZ_ALWAYS_SUCCEEDS(hwinference::PSpeechRecognition::CreateEndpoints(
      &parentEndpoint, &childEndpoint));
  ContentChild::GetSingleton()->SendCreateSpeechRecognition(
      std::move(parentEndpoint));

  sIPCCapability->Dispatch(NS_NewRunnableFunction(
      "SpeechRecognitionBackend::CreateSession",
      [guard = std::move(guard), endpoint = std::move(childEndpoint),
       callback = std::move(aCallback)]() mutable {
        AssertOnIPCThread();
        RefPtr child = new hwinference::SpeechRecognitionChild(guard.forget());
        if (!endpoint.Bind(child)) {
          callback(nullptr);
          return;
        }
        callback(child);
      }));
}

/* static */
already_AddRefed<Promise> SpeechRecognitionBackend::Install(
    nsIGlobalObject* aGlobal, const nsTArray<nsCString>& aLanguages,
    uint64_t aInnerWindowId) {
  AssertIsOnMainThread();

  if (!aGlobal) {
    return nullptr;
  }

  ErrorResult rv;
  RefPtr<Promise> promise = Promise::Create(aGlobal, rv);
  if (rv.Failed()) {
    return nullptr;
  }

  if (aLanguages.IsEmpty()) {
    promise->MaybeResolve(false);
    return promise.forget();
  }

  LOG("SpeechRecognitionBackend::Install - Starting install for {} languages",
      aLanguages.Length());

  InstallModels(aLanguages, aInnerWindowId)
      ->Then(GetMainThreadSerialEventTarget(), __func__,
             [promise](ModelInstallPromise::ResolveOrRejectValue&& aValue) {
               bool success = aValue.IsResolve() &&
                              aValue.ResolveValue() ==
                                  hwinference::ModelInstallResult::Installed;
               LOG("SpeechRecognitionBackend::Install - Install completed: {}",
                   success ? "success" : "failed");
               promise->MaybeResolve(success);
             });

  return promise.forget();
}

/* static */
RefPtr<SpeechRecognitionBackend::ModelInstallPromise>
SpeechRecognitionBackend::InstallModels(const nsTArray<nsCString>& aLanguages,
                                        uint64_t aInnerWindowId) {
  AssertIsOnMainThread();
  MOZ_ASSERT(!aLanguages.IsEmpty());

  LOG("SpeechRecognitionBackend::InstallModels - Starting install for {} "
      "languages",
      aLanguages.Length());

  return RunWithTransientSession(
      [languages = aLanguages.Clone(),
       aInnerWindowId](hwinference::SpeechRecognitionChild* aChild) mutable {
        return aChild->SendInstallModels(std::move(languages), aInnerWindowId);
      });
}

/* static */
RefPtr<SpeechRecognitionBackend::ModelInstallPromise>
SpeechRecognitionBackend::EnsureModelsInstalled(
    const nsTArray<nsCString>& aLanguages, uint64_t aInnerWindowId) {
  AssertIsOnMainThread();
  MOZ_ASSERT(!aLanguages.IsEmpty());

  return RunWithTransientSession(
             [languages = aLanguages.Clone()](
                 hwinference::SpeechRecognitionChild* aChild) mutable {
               return IsModelInstalledNative(aChild, languages);
             })
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [languages = aLanguages.Clone(), aInnerWindowId](
              MozPromise<bool, nsresult, true>::ResolveOrRejectValue&& aValue)
              -> RefPtr<ModelInstallPromise> {
            AssertIsOnMainThread();
            if (aValue.IsResolve() && aValue.ResolveValue()) {
              return ModelInstallPromise::CreateAndResolve(
                  hwinference::ModelInstallResult::Installed, __func__);
            }
            // Not installed, or the query failed, in which case the
            // install is what reports why.
            return InstallModels(languages, aInnerWindowId);
          });
}

}  // namespace mozilla::dom

#undef LOG
#undef LOGV
#undef LOGE
