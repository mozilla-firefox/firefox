/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaEngineWebRTCAudio.h"

#include <algorithm>

#include "AudioConverter.h"
#include "MediaManager.h"
#include "MediaTrackConstraints.h"
#include "MediaTrackGraph.h"
#include "Tracing.h"
#include "api/audio/builtin_audio_processing_builder.h"
#include "api/audio/echo_canceller3_factory.h"
#include "api/environment/environment_factory.h"
#include "common_audio/include/audio_util.h"
#include "libwebrtcglue/WebrtcEnvironmentWrapper.h"
#include "modules/audio_processing/include/audio_processing.h"
#include "mozilla/Assertions.h"
#include "mozilla/ErrorNames.h"
#include "mozilla/Logging.h"
#include "mozilla/Sprintf.h"
#include "nsGlobalWindowInner.h"
#include "nsIDUtils.h"
#include "transport/runnable_utils.h"

using namespace webrtc;

// These are restrictions from the webrtc.org code
#define MAX_CHANNELS 2
#define MONO 1
#define MAX_SAMPLING_FREQ 48000  // Hz - multiple of 100

namespace mozilla {

using dom::MediaSourceEnum;

extern LazyLogModule gMediaManagerLog;
#define LOG(...) MOZ_LOG_FMT(gMediaManagerLog, LogLevel::Debug, __VA_ARGS__)
#define LOG_FRAME(...) \
  MOZ_LOG_FMT(gMediaManagerLog, LogLevel::Verbose, __VA_ARGS__)
#define LOG_ERROR(...) \
  MOZ_LOG_FMT(gMediaManagerLog, LogLevel::Error, __VA_ARGS__)

/**
 * WebRTC Microphone MediaEngineSource.
 */

MediaEngineWebRTCMicrophoneSource::MediaEngineWebRTCMicrophoneSource(
    const MediaDevice* aMediaDevice)
    : mPrincipal(PRINCIPAL_HANDLE_NONE),
      mDeviceInfo(aMediaDevice->mAudioDeviceInfo),
      mDeviceMaxChannelCount(mDeviceInfo->MaxChannels()),
      mSettings(new nsMainThreadPtrHolder<
                media::Refcountable<dom::MediaTrackSettings>>(
          "MediaEngineWebRTCMicrophoneSource::mSettings",
          new media::Refcountable<dom::MediaTrackSettings>(),
          // Non-strict means it won't assert main thread for us.
          // It would be great if it did but we're already on the media thread.
          /* aStrict = */ false)),
      mCapabilities(new nsMainThreadPtrHolder<
                    media::Refcountable<dom::MediaTrackCapabilities>>(
          "MediaEngineWebRTCMicrophoneSource::mCapabilities",
          new media::Refcountable<dom::MediaTrackCapabilities>(),
          // Non-strict means it won't assert main thread for us.
          // It would be great if it did but we're already on the media thread.
          /* aStrict = */ false)) {
  MOZ_ASSERT(aMediaDevice->mMediaSource == MediaSourceEnum::Microphone);
#ifndef ANDROID
  MOZ_ASSERT(mDeviceInfo->DeviceID());
#endif

  // We'll init lazily as needed
  mSettings->mEchoCancellation.Construct(0);
  mSettings->mAutoGainControl.Construct(0);
  mSettings->mNoiseSuppression.Construct(0);
  mSettings->mChannelCount.Construct(0);

  mState = kReleased;

  // Set mMaxChannelsCapablitiy on main thread.
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      __func__, [capabilities = mCapabilities,
                 deviceMaxChannelCount = mDeviceMaxChannelCount] {
        nsTArray<bool> echoCancellation;
        echoCancellation.AppendElement(true);
        echoCancellation.AppendElement(false);
        capabilities->mEchoCancellation.Reset();
        capabilities->mEchoCancellation.Construct(std::move(echoCancellation));

        nsTArray<bool> autoGainControl;
        autoGainControl.AppendElement(true);
        autoGainControl.AppendElement(false);
        capabilities->mAutoGainControl.Reset();
        capabilities->mAutoGainControl.Construct(std::move(autoGainControl));

        nsTArray<bool> noiseSuppression;
        noiseSuppression.AppendElement(true);
        noiseSuppression.AppendElement(false);
        capabilities->mNoiseSuppression.Reset();
        capabilities->mNoiseSuppression.Construct(std::move(noiseSuppression));

        if (deviceMaxChannelCount) {
          dom::ULongRange channelCountRange;
          channelCountRange.mMax.Construct(deviceMaxChannelCount);
          channelCountRange.mMin.Construct(1);
          capabilities->mChannelCount.Reset();
          capabilities->mChannelCount.Construct(channelCountRange);
        }
      }));
}

/*static*/ already_AddRefed<MediaEngineWebRTCMicrophoneSource>
MediaEngineWebRTCMicrophoneSource::CreateFrom(
    const MediaEngineWebRTCMicrophoneSource* aSource,
    const MediaDevice* aMediaDevice) {
  auto src = MakeRefPtr<MediaEngineWebRTCMicrophoneSource>(aMediaDevice);
  *static_cast<dom::MediaTrackSettings*>(src->mSettings) = *aSource->mSettings;
  *static_cast<dom::MediaTrackCapabilities*>(src->mCapabilities) =
      *aSource->mCapabilities;
  return src.forget();
}

nsresult MediaEngineWebRTCMicrophoneSource::EvaluateSettings(
    const NormalizedConstraints& aConstraintsUpdate,
    const MediaEnginePrefs& aInPrefs, MediaEnginePrefs* aOutPrefs,
    const char** aOutBadConstraint) {
  AssertIsOnOwningThread();

  FlattenedConstraints c(aConstraintsUpdate);
  MediaEnginePrefs prefs = aInPrefs;

  prefs.mAecOn = c.mEchoCancellation.Get(aInPrefs.mAecOn);
  prefs.mAgcOn = c.mAutoGainControl.Get(aInPrefs.mAgcOn && prefs.mAecOn);
  prefs.mNoiseOn = c.mNoiseSuppression.Get(aInPrefs.mNoiseOn && prefs.mAecOn);

  // Determine an actual channel count to use for this source. Three factors at
  // play here: the device capabilities, the constraints passed in by content,
  // and a pref that can force things (for testing)
  int32_t maxChannels = static_cast<int32_t>(mDeviceInfo->MaxChannels());

  // First, check channelCount violation wrt constraints. This fails in case of
  // error.
  if (c.mChannelCount.mMin > maxChannels) {
    *aOutBadConstraint = "channelCount";
    return NS_ERROR_FAILURE;
  }
  // A pref can force the channel count to use. If the pref has a value of zero
  // or lower, it has no effect.
  if (aInPrefs.mChannels <= 0) {
    prefs.mChannels = maxChannels;
  }

  // Get the number of channels asked for by content, and clamp it between the
  // pref and the maximum number of channels that the device supports.
  prefs.mChannels = c.mChannelCount.Get(std::min(prefs.mChannels, maxChannels));
  prefs.mChannels = std::clamp(prefs.mChannels, 1, maxChannels);

  LOG("Mic source {} Audio config: aec: {}, agc: {}, noise: {}, channels: {}",
      fmt::ptr(this), prefs.mAecOn ? "on" : "off", prefs.mAgcOn ? "on" : "off",
      prefs.mNoiseOn ? "on" : "off", prefs.mChannels);

  *aOutPrefs = prefs;

  return NS_OK;
}

nsresult MediaEngineWebRTCMicrophoneSource::Reconfigure(
    const dom::MediaTrackConstraints& aConstraints,
    const MediaEnginePrefs& aPrefs, const char** aOutBadConstraint) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mTrack);

  LOG("Mic source {} Reconfigure ", fmt::ptr(this));

  NormalizedConstraints constraints(aConstraints);
  MediaEnginePrefs outputPrefs;
  nsresult rv =
      EvaluateSettings(constraints, aPrefs, &outputPrefs, aOutBadConstraint);
  if (NS_FAILED(rv)) {
    if (aOutBadConstraint) {
      return NS_ERROR_INVALID_ARG;
    }

    nsAutoCString name;
    GetErrorName(rv, name);
    LOG("Mic source {} Reconfigure() failed unexpectedly. rv={}",
        fmt::ptr(this), name.get());
    Stop();
    return NS_ERROR_UNEXPECTED;
  }

  ApplySettings(outputPrefs);

  mCurrentPrefs = outputPrefs;

  return NS_OK;
}

AudioProcessing::Config AudioInputProcessing::ConfigForPrefs(
    MediaTrackGraph* aGraph, const MediaEnginePrefs& aPrefs) const {
  AudioProcessing::Config config;

  config.pipeline.multi_channel_render = true;
  config.pipeline.multi_channel_capture = true;

  config.echo_canceller.enabled = aPrefs.mAecOn;

  if ((config.gain_controller1.enabled =
           aPrefs.mAgcOn && !aPrefs.mAgc2Forced)) {
    auto mode = static_cast<AudioProcessing::Config::GainController1::Mode>(
        aPrefs.mAgc);
    if (mode != AudioProcessing::Config::GainController1::kAdaptiveAnalog &&
        mode != AudioProcessing::Config::GainController1::kAdaptiveDigital &&
        mode != AudioProcessing::Config::GainController1::kFixedDigital) {
      LOG_ERROR("AudioInputProcessing {} Attempt to set invalid AGC mode {}",
                fmt::ptr(this), static_cast<int>(mode));
      mode = AudioProcessing::Config::GainController1::kAdaptiveDigital;
    }
#if defined(WEBRTC_IOS) || defined(ATA) || defined(WEBRTC_ANDROID)
    if (mode == AudioProcessing::Config::GainController1::kAdaptiveAnalog) {
      LOG_ERROR(
          "AudioInputProcessing {} Invalid AGC mode kAdaptiveAnalog on "
          "mobile",
          fmt::ptr(this));
      MOZ_ASSERT_UNREACHABLE(
          "Bad pref set in all.js or in about:config"
          " for the auto gain, on mobile.");
      mode = AudioProcessing::Config::GainController1::kFixedDigital;
    }
#endif
    config.gain_controller1.mode = mode;
  }
  config.gain_controller2.enabled =
      config.gain_controller2.adaptive_digital.enabled =
          aPrefs.mAgcOn && aPrefs.mAgc2Forced;

  if ((config.noise_suppression.enabled = aPrefs.mNoiseOn)) {
    auto level = static_cast<AudioProcessing::Config::NoiseSuppression::Level>(
        aPrefs.mNoise);
    if (level != AudioProcessing::Config::NoiseSuppression::kLow &&
        level != AudioProcessing::Config::NoiseSuppression::kModerate &&
        level != AudioProcessing::Config::NoiseSuppression::kHigh &&
        level != AudioProcessing::Config::NoiseSuppression::kVeryHigh) {
      LOG_ERROR(
          "AudioInputProcessing {} Attempt to set invalid noise suppression "
          "level {}",
          fmt::ptr(this), static_cast<int>(level));

      level = AudioProcessing::Config::NoiseSuppression::kModerate;
    }
    config.noise_suppression.level = level;
  }

  config.transient_suppression.enabled = aPrefs.mTransientOn;

  config.high_pass_filter.enabled = aPrefs.mHPFOn;

  if ((mPlatformProcessingSetParams &
       CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION)) {
    // Platform processing (VPIO on macOS) will cancel echo from the output
    // device used as the output stream. Leave it on here when rendering audio
    // to another output device.
    config.echo_canceller.enabled = !aGraph->OutputForAECIsPrimary();
  }
  if (mPlatformProcessingSetParams &
      CUBEB_INPUT_PROCESSING_PARAM_AUTOMATIC_GAIN_CONTROL) {
    config.gain_controller1.enabled = config.gain_controller2.enabled = false;
  }
  if (mPlatformProcessingSetParams &
      CUBEB_INPUT_PROCESSING_PARAM_NOISE_SUPPRESSION) {
    config.noise_suppression.enabled = false;
  }

  return config;
}

void MediaEngineWebRTCMicrophoneSource::ApplySettings(
    const MediaEnginePrefs& aPrefs) {
  AssertIsOnOwningThread();

  TRACE("ApplySettings");
  MOZ_ASSERT(
      mTrack,
      "ApplySetting is to be called only after SetTrack has been called");

  RefPtr<MediaEngineWebRTCMicrophoneSource> that = this;
  CubebUtils::AudioDeviceID deviceID = mDeviceInfo->DeviceID();
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      __func__, [this, that, deviceID, track = mTrack, prefs = aPrefs] {
        mSettings->mEchoCancellation.Value() = prefs.mAecOn;
        mSettings->mAutoGainControl.Value() = prefs.mAgcOn;
        mSettings->mNoiseSuppression.Value() = prefs.mNoiseOn;
        mSettings->mChannelCount.Value() = prefs.mChannels;

        if (track->IsDestroyed()) {
          return;
        }
        track->QueueControlMessageWithNoShutdown(
            [track, deviceID, prefs, inputProcessing = mInputProcessing] {
              inputProcessing->ApplySettings(track->Graph(), deviceID, prefs);
            });
      }));
}

nsresult MediaEngineWebRTCMicrophoneSource::Allocate(
    const dom::MediaTrackConstraints& aConstraints,
    const MediaEnginePrefs& aPrefs, uint64_t aWindowID,
    const char** aOutBadConstraint) {
  AssertIsOnOwningThread();

  mState = kAllocated;

  NormalizedConstraints normalized(aConstraints);
  MediaEnginePrefs outputPrefs;
  nsresult rv =
      EvaluateSettings(normalized, aPrefs, &outputPrefs, aOutBadConstraint);
  if (NS_FAILED(rv)) {
    return rv;
  }

  NS_DispatchToMainThread(NS_NewRunnableFunction(
      __func__, [settings = mSettings, prefs = outputPrefs] {
        settings->mEchoCancellation.Value() = prefs.mAecOn;
        settings->mAutoGainControl.Value() = prefs.mAgcOn;
        settings->mNoiseSuppression.Value() = prefs.mNoiseOn;
        settings->mChannelCount.Value() = prefs.mChannels;
      }));

  mCurrentPrefs = outputPrefs;

  return rv;
}

nsresult MediaEngineWebRTCMicrophoneSource::Deallocate() {
  AssertIsOnOwningThread();

  MOZ_ASSERT(mState == kStopped || mState == kAllocated);

  if (mTrack) {
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        __func__,
        [track = std::move(mTrack), inputProcessing = mInputProcessing] {
          if (track->IsDestroyed()) {
            // This track has already been destroyed on main thread by its
            // DOMMediaStream. No cleanup left to do.
            return;
          }
          track->QueueControlMessageWithNoShutdown([inputProcessing] {
            TRACE("mInputProcessing::End");
            inputProcessing->End();
          });
        }));
  }

  // Reset all state. This is not strictly necessary, this instance will get
  // destroyed soon.
  mTrack = nullptr;
  mPrincipal = PRINCIPAL_HANDLE_NONE;

  // If empty, no callbacks to deliver data should be occuring
  MOZ_ASSERT(mState != kReleased, "Source not allocated");
  MOZ_ASSERT(mState != kStarted, "Source not stopped");

  mState = kReleased;
  LOG("Mic source {} Audio device {} deallocated", fmt::ptr(this),
      NS_ConvertUTF16toUTF8(mDeviceInfo->Name()).get());
  return NS_OK;
}

void MediaEngineWebRTCMicrophoneSource::SetTrack(
    const RefPtr<MediaTrack>& aTrack, const PrincipalHandle& aPrincipal) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aTrack);
  MOZ_ASSERT(aTrack->AsAudioProcessingTrack());

  MOZ_ASSERT(!mTrack);
  MOZ_ASSERT(mPrincipal == PRINCIPAL_HANDLE_NONE);
  mTrack = aTrack->AsAudioProcessingTrack();
  mPrincipal = aPrincipal;

  mInputProcessing =
      MakeAndAddRef<AudioInputProcessing>(mDeviceMaxChannelCount);

  NS_DispatchToMainThread(NS_NewRunnableFunction(
      __func__, [track = mTrack, processing = mInputProcessing]() mutable {
        track->SetInputProcessing(std::move(processing));
        track->Resume();  // Suspended by MediaManager
      }));

  LOG("Mic source {} Track {} registered for microphone capture",
      fmt::ptr(this), fmt::ptr(aTrack.get()));
}

nsresult MediaEngineWebRTCMicrophoneSource::Start() {
  AssertIsOnOwningThread();

  // This spans setting both the enabled state and mState.
  if (mState == kStarted) {
    return NS_OK;
  }

  MOZ_ASSERT(mState == kAllocated || mState == kStopped);

  ApplySettings(mCurrentPrefs);

  CubebUtils::AudioDeviceID deviceID = mDeviceInfo->DeviceID();
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      __func__, [inputProcessing = mInputProcessing, deviceID, track = mTrack,
                 principal = mPrincipal] {
        if (track->IsDestroyed()) {
          return;
        }

        track->QueueControlMessageWithNoShutdown([track, inputProcessing] {
          TRACE("mInputProcessing::Start");
          inputProcessing->Start(track->Graph());
        });
        track->ConnectDeviceInput(deviceID, inputProcessing.get(), principal);
      }));

  MOZ_ASSERT(mState != kReleased);
  mState = kStarted;

  return NS_OK;
}

nsresult MediaEngineWebRTCMicrophoneSource::Stop() {
  AssertIsOnOwningThread();

  LOG("Mic source {} Stop()", fmt::ptr(this));
  MOZ_ASSERT(mTrack, "SetTrack must have been called before ::Stop");

  if (mState == kStopped) {
    // Already stopped - this is allowed
    return NS_OK;
  }

  NS_DispatchToMainThread(NS_NewRunnableFunction(
      __func__, [inputProcessing = mInputProcessing, deviceInfo = mDeviceInfo,
                 track = mTrack] {
        if (track->IsDestroyed()) {
          return;
        }

        MOZ_ASSERT(track->DeviceId().value() == deviceInfo->DeviceID());
        track->DisconnectDeviceInput();
        track->QueueControlMessageWithNoShutdown([track, inputProcessing] {
          TRACE("mInputProcessing::Stop");
          inputProcessing->Stop(track->Graph());
        });
      }));

  MOZ_ASSERT(mState == kStarted, "Should be started when stopping");
  mState = kStopped;

  return NS_OK;
}

void MediaEngineWebRTCMicrophoneSource::GetSettings(
    dom::MediaTrackSettings& aOutSettings) const {
  MOZ_ASSERT(NS_IsMainThread());
  aOutSettings = *mSettings;
}

void MediaEngineWebRTCMicrophoneSource::GetCapabilities(
    dom::MediaTrackCapabilities& aOutCapabilities) const {
  MOZ_ASSERT(NS_IsMainThread());
  aOutCapabilities = *mCapabilities;
}

AudioInputProcessing::AudioInputProcessing(uint32_t aMaxChannelCount)
    : mInputDownmixBuffer(MAX_SAMPLING_FREQ * MAX_CHANNELS / 100),
      mEnabled(false),
      mEnded(false),
      mPacketCount(0) {
  mSettings.mChannels = static_cast<int32_t>(std::min<uint32_t>(
      std::numeric_limits<int32_t>::max(), aMaxChannelCount));
}

void AudioInputProcessing::Disconnect(MediaTrackGraph* aGraph) {
  aGraph->AssertOnGraphThread();
  mPlatformProcessingSetGeneration = 0;
  mPlatformProcessingSetParams = CUBEB_INPUT_PROCESSING_PARAM_NONE;
  ApplySettingsInternal(aGraph, mSettings);
}

void AudioInputProcessing::NotifySetRequestedInputProcessingParams(
    MediaTrackGraph* aGraph, int aGeneration,
    cubeb_input_processing_params aRequestedParams) {
  aGraph->AssertOnGraphThread();
  MOZ_ASSERT(aGeneration >= mPlatformProcessingSetGeneration);
  if (aGeneration <= mPlatformProcessingSetGeneration) {
    return;
  }
  mPlatformProcessingSetGeneration = aGeneration;
  cubeb_input_processing_params intersection =
      mPlatformProcessingSetParams & aRequestedParams;
  LOG("AudioInputProcessing {} platform processing params being applied are "
      "now {} (Gen {}). Assuming {} while waiting for the result.",
      fmt::ptr(this),
      CubebUtils::ProcessingParamsToString(aRequestedParams).get(), aGeneration,
      CubebUtils::ProcessingParamsToString(intersection).get());
  if (mPlatformProcessingSetParams == intersection) {
    LOG("AudioInputProcessing {} intersection {} of platform processing params "
        "already applied. Doing nothing.",
        fmt::ptr(this),
        CubebUtils::ProcessingParamsToString(intersection).get());
    return;
  }
  mPlatformProcessingSetParams = intersection;
  ApplySettingsInternal(aGraph, mSettings);
}

void AudioInputProcessing::NotifySetRequestedInputProcessingParamsResult(
    MediaTrackGraph* aGraph, int aGeneration,
    const Result<cubeb_input_processing_params, int>& aResult) {
  aGraph->AssertOnGraphThread();
  if (aGeneration != mPlatformProcessingSetGeneration) {
    // This is a result from an old request, wait for a more recent one.
    return;
  }
  if (aResult.isOk()) {
    if (mPlatformProcessingSetParams == aResult.inspect()) {
      // No change.
      return;
    }
    mPlatformProcessingSetError = Nothing();
    mPlatformProcessingSetParams = aResult.inspect();
    LOG("AudioInputProcessing {} platform processing params are now {}.",
        fmt::ptr(this),
        CubebUtils::ProcessingParamsToString(mPlatformProcessingSetParams)
            .get());
  } else {
    mPlatformProcessingSetError = Some(aResult.inspectErr());
    mPlatformProcessingSetParams = CUBEB_INPUT_PROCESSING_PARAM_NONE;
    LOG("AudioInputProcessing {} platform processing params failed to apply. "
        "Applying input processing config in libwebrtc.",
        fmt::ptr(this));
  }
  ApplySettingsInternal(aGraph, mSettings);
}

bool AudioInputProcessing::IsPassThrough(MediaTrackGraph* aGraph) const {
  aGraph->AssertOnGraphThread();
  // The high-pass filter is not taken into account when activating the
  // pass through, since it's not controllable from content.
  auto config = AppliedConfig(aGraph);
  auto aec = [](const auto& config) { return config.echo_canceller.enabled; };
  auto agc = [](const auto& config) {
    return config.gain_controller1.enabled || config.gain_controller2.enabled;
  };
  auto ns = [](const auto& config) { return config.noise_suppression.enabled; };
  return !(aec(config) || agc(config) || ns(config));
}

void AudioInputProcessing::PassThroughChanged(MediaTrackGraph* aGraph) {
  aGraph->AssertOnGraphThread();

  if (!mEnabled) {
    MOZ_ASSERT(!mPacketizerInput);
    return;
  }

  if (IsPassThrough(aGraph)) {
    // Switching to pass-through.  Clear state so that it doesn't affect any
    // future processing, if re-enabled.
    ResetAudioProcessing(aGraph);
  } else {
    // No longer pass-through.  Processing will not use old state.
    // Packetizer setup is deferred until needed.
    MOZ_ASSERT(!mPacketizerInput);
  }
}

uint32_t AudioInputProcessing::GetRequestedInputChannelCount() const {
  return mSettings.mChannels;
}

void AudioInputProcessing::RequestedInputChannelCountChanged(
    MediaTrackGraph* aGraph, CubebUtils::AudioDeviceID aDeviceId) {
  aGraph->ReevaluateInputDevice(aDeviceId);
}

void AudioInputProcessing::Start(MediaTrackGraph* aGraph) {
  aGraph->AssertOnGraphThread();

  if (mEnabled) {
    return;
  }
  mEnabled = true;

  MOZ_ASSERT(!mPacketizerInput);
}

void AudioInputProcessing::Stop(MediaTrackGraph* aGraph) {
  aGraph->AssertOnGraphThread();

  if (!mEnabled) {
    return;
  }

  mEnabled = false;

  if (IsPassThrough(aGraph)) {
    return;
  }

  // Packetizer is active and we were just stopped. Stop the packetizer and
  // processing.
  ResetAudioProcessing(aGraph);
}

// The following is how how Process() works in pass-through and non-pass-through
// mode. In both mode, Process() outputs the same amount of the frames as its
// input data.
//
// I. In non-pass-through mode:
//
// We will use webrtc::AudioProcessing to process the input audio data in this
// mode. The data input in webrtc::AudioProcessing needs to be a 10ms chunk,
// while the input data passed to Process() is not necessary to have times of
// 10ms-chunk length. To divide the input data into 10ms chunks,
// mPacketizerInput is introduced.
//
// We will add one 10ms-chunk silence into the internal buffer before Process()
// works. Those extra frames is called pre-buffering. It aims to avoid glitches
// we may have when producing data in mPacketizerInput. Without pre-buffering,
// when the input data length is not 10ms-times, we could end up having no
// enough output needs since mPacketizerInput would keep some input data, which
// is the remainder of the 10ms-chunk length. To force processing those data
// left in mPacketizerInput, we would need to add some extra frames to make
// mPacketizerInput produce a 10ms-chunk. For example, if the sample rate is
// 44100 Hz, then the packet-size is 441 frames. When we only have 384 input
// frames, we would need to put additional 57 frames to mPacketizerInput to
// produce a packet. However, those extra 57 frames result in a glitch sound.
//
// By adding one 10ms-chunk silence in advance to the internal buffer, we won't
// need to add extra frames between the input data no matter what data length it
// is. The only drawback is the input data won't be processed and send to output
// immediately. Process() will consume pre-buffering data for its output first.
// The below describes how it works:
//
//
//                          Process()
//               +-----------------------------+
//   input D(N)  |   +--------+   +--------+   |  output D(N)
// --------------|-->|  P(N)  |-->|  S(N)  |---|-------------->
//               |   +--------+   +--------+   |
//               |   packetizer    mSegment    |
//               +-----------------------------+
//               <------ internal buffer ------>
//
//
//   D(N): number of frames from the input and the output needs in the N round
//      Z: number of frames of a 10ms chunk(packet) in mPacketizerInput, Z >= 1
//         (if Z = 1, packetizer has no effect)
//   P(N): number of frames left in mPacketizerInput after the N round. Once the
//         frames in packetizer >= Z, packetizer will produce a packet to
//         mSegment, so P(N) = (P(N-1) + D(N)) % Z, 0 <= P(N) <= Z-1
//   S(N): number of frames left in mSegment after the N round. The input D(N)
//         frames will be passed to mPacketizerInput first, and then
//         mPacketizerInput may append some packets to mSegment, so
//         S(N) = S(N-1) + Z * floor((P(N-1) + D(N)) / Z) - D(N)
//
// At the first, we set P(0) = 0, S(0) = X, where X >= Z-1. X is the
// pre-buffering put in the internal buffer. With this settings, P(K) + S(K) = X
// always holds.
//
// Intuitively, this seems true: We put X frames in the internal buffer at
// first. If the data won't be blocked in packetizer, after the Process(), the
// internal buffer should still hold X frames since the number of frames coming
// from input is the same as the output needs. The key of having enough data for
// output needs, while the input data is piled up in packetizer, is by putting
// at least Z-1 frames as pre-buffering, since the maximum number of frames
// stuck in the packetizer before it can emit a packet is packet-size - 1.
// Otherwise, we don't have enough data for output if the new input data plus
// the data left in packetizer produces a smaller-than-10ms chunk, which will be
// left in packetizer. Thus we must have some pre-buffering frames in the
// mSegment to make up the length of the left chunk we need for output. This can
// also be told by by induction:
//   (1) This holds when K = 0
//   (2) Assume this holds when K = N: so P(N) + S(N) = X
//       => P(N) + S(N) = X >= Z-1 => S(N) >= Z-1-P(N)
//   (3) When K = N+1, we have D(N+1) input frames comes
//     a. if P(N) + D(N+1) < Z, then packetizer has no enough data for one
//        packet. No data produced by packertizer, so the mSegment now has
//        S(N) >= Z-1-P(N) frames. Output needs D(N+1) < Z-P(N) frames. So it
//        needs at most Z-P(N)-1 frames, and mSegment has enough frames for
//        output, Then, P(N+1) = P(N) + D(N+1) and S(N+1) = S(N) - D(N+1)
//        => P(N+1) + S(N+1) = P(N) + S(N) = X
//     b. if P(N) + D(N+1) = Z, then packetizer will produce one packet for
//        mSegment, so mSegment now has S(N) + Z frames. Output needs D(N+1)
//        = Z-P(N) frames. S(N) has at least Z-1-P(N)+Z >= Z-P(N) frames, since
//        Z >= 1. So mSegment has enough frames for output. Then, P(N+1) = 0 and
//        S(N+1) = S(N) + Z - D(N+1) = S(N) + P(N)
//        => P(N+1) + S(N+1) = P(N) + S(N) = X
//     c. if P(N) + D(N+1) > Z, and let P(N) + D(N+1) = q * Z + r, where q >= 1
//        and 0 <= r <= Z-1, then packetizer will produce can produce q packets
//        for mSegment. Output needs D(N+1) = q * Z - P(N) + r frames and
//        mSegment has S(N) + q * z >= q * z - P(N) + Z-1 >= q*z -P(N) + r,
//        since r <= Z-1. So mSegment has enough frames for output. Then,
//        P(N+1) = r and S(N+1) = S(N) + q * Z - D(N+1)
//         => P(N+1) + S(N+1) = S(N) + (q * Z + r - D(N+1)) =  S(N) + P(N) = X
//   => P(K) + S(K) = X always holds
//
// Since P(K) + S(K) = X and P(K) is in [0, Z-1], the S(K) is in [X-Z+1, X]
// range. In our implementation, X is set to Z so S(K) is in [1, Z].
// By the above workflow, we always have enough data for output and no extra
// frames put into packetizer. It means we don't have any glitch!
//
// II. In pass-through mode:
//
//                Process()
//               +--------+
//   input D(N)  |        |  output D(N)
// -------------->-------->--------------->
//               |        |
//               +--------+
//
// The D(N) frames of data are just forwarded from input to output without any
// processing
void AudioInputProcessing::Process(AudioProcessingTrack* aTrack,
                                   GraphTime aFrom, GraphTime aTo,
                                   AudioSegment* aInput,
                                   AudioSegment* aOutput) {
  aTrack->AssertOnGraphThread();
  MOZ_ASSERT(aFrom <= aTo);
  MOZ_ASSERT(!mEnded);

  TrackTime need = aTo - aFrom;
  if (need == 0) {
    return;
  }

  MediaTrackGraph* graph = aTrack->Graph();
  if (!mEnabled) {
    LOG_FRAME(
        "(Graph {}, Driver {}) AudioInputProcessing {} Filling {} frames of "
        "silence to output (disabled)",
        fmt::ptr(graph), fmt::ptr(graph->CurrentDriver()), fmt::ptr(this),
        need);
    aOutput->AppendNullData(need);
    return;
  }

  MOZ_ASSERT(aInput->GetDuration() == need,
             "Wrong data length from input port source");

  if (mSettings.mAecOn &&
      (mPlatformProcessingSetParams &
       CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION) &&
      mAppliedConfig.echo_canceller.enabled ==
          aTrack->Graph()->OutputForAECIsPrimary()) {
    ApplySettingsInternal(aTrack->Graph(), mSettings);
  }

  if (IsPassThrough(graph)) {
    LOG_FRAME(
        "(Graph {}, Driver {}) AudioInputProcessing {} Forwarding {} frames of "
        "input data to output directly (PassThrough)",
        fmt::ptr(graph), fmt::ptr(graph->CurrentDriver()), fmt::ptr(this),
        aInput->GetDuration());
    aOutput->AppendSegment(aInput);
    return;
  }

  // If the requested input channel count is updated, create a new
  // packetizer. No need to change the pre-buffering since the rate is always
  // the same. The frames left in the packetizer would be replaced by null
  // data and then transferred to mSegment.
  EnsurePacketizer(aTrack);

  // Preconditions of the audio-processing logic.
  MOZ_ASSERT(static_cast<uint32_t>(mSegment.GetDuration()) +
                 mPacketizerInput->FramesAvailable() ==
             mPacketizerInput->mPacketSize);
  // We pre-buffer mPacketSize frames, but the maximum number of frames stuck in
  // the packetizer before it can emit a packet is mPacketSize-1. Thus that
  // remaining 1 frame will always be present in mSegment.
  MOZ_ASSERT(mSegment.GetDuration() >= 1);
  MOZ_ASSERT(mSegment.GetDuration() <= mPacketizerInput->mPacketSize);

  PacketizeAndProcess(aTrack, *aInput);
  LOG_FRAME(
      "(Graph {}, Driver {}) AudioInputProcessing {} Buffer has {} frames of "
      "data now, after packetizing and processing",
      fmt::ptr(graph), fmt::ptr(graph->CurrentDriver()), fmt::ptr(this),
      mSegment.GetDuration());

  // By setting pre-buffering to the number of frames of one packet, and
  // because the maximum number of frames stuck in the packetizer before
  // it can emit a packet is the mPacketSize-1, we always have at least
  // one more frame than output needs.
  MOZ_ASSERT(mSegment.GetDuration() > need);
  aOutput->AppendSlice(mSegment, 0, need);
  mSegment.RemoveLeading(need);
  LOG_FRAME(
      "(Graph {}, Driver {}) AudioInputProcessing {} moving {} frames of data "
      "to output, leaving {} frames in buffer",
      fmt::ptr(graph), fmt::ptr(graph->CurrentDriver()), fmt::ptr(this), need,
      mSegment.GetDuration());

  // Postconditions of the audio-processing logic.
  MOZ_ASSERT(static_cast<uint32_t>(mSegment.GetDuration()) +
                 mPacketizerInput->FramesAvailable() ==
             mPacketizerInput->mPacketSize);
  MOZ_ASSERT(mSegment.GetDuration() >= 1);
  MOZ_ASSERT(mSegment.GetDuration() <= mPacketizerInput->mPacketSize);
}

void AudioInputProcessing::ProcessOutputData(AudioProcessingTrack* aTrack,
                                             const AudioChunk& aChunk) {
  MOZ_ASSERT(aChunk.ChannelCount() > 0);
  aTrack->AssertOnGraphThread();

  if (!mEnabled) {
    return;
  }

  if (IsPassThrough(aTrack->Graph())) {
    return;
  }

  if (aChunk.mDuration == 0) {
    return;
  }

  TrackRate sampleRate = aTrack->mSampleRate;
  uint32_t framesPerPacket = GetPacketSize(sampleRate);  // in frames
  // Downmix from aChannels to MAX_CHANNELS if needed.
  uint32_t channelCount =
      std::min<uint32_t>(aChunk.ChannelCount(), MAX_CHANNELS);
  if (channelCount != mOutputBufferChannelCount ||
      channelCount * framesPerPacket != mOutputBuffer.Length()) {
    MOZ_RELEASE_ASSERT(mOutputBuffer.SetLength(channelCount * framesPerPacket));
    mOutputBufferChannelCount = channelCount;
    // It's ok to drop the audio still in the packetizer here: if this changes,
    // we changed devices or something.
    mOutputBufferFrameCount = 0;
  }

  TrackTime chunkOffset = 0;
  AutoTArray<float*, MAX_CHANNELS> channelPtrs;
  channelPtrs.SetLength(channelCount);
  do {
    MOZ_ASSERT(mOutputBufferFrameCount < framesPerPacket);
    uint32_t packetRemainder = framesPerPacket - mOutputBufferFrameCount;
    mSubChunk = aChunk;
    mSubChunk.SliceTo(
        chunkOffset, std::min(chunkOffset + packetRemainder, aChunk.mDuration));
    MOZ_ASSERT(mSubChunk.mDuration <= packetRemainder);

    for (uint32_t channel = 0; channel < channelCount; channel++) {
      channelPtrs[channel] =
          &mOutputBuffer[channel * framesPerPacket + mOutputBufferFrameCount];
    }
    mSubChunk.DownMixTo(channelPtrs);

    chunkOffset += mSubChunk.mDuration;
    MOZ_ASSERT(chunkOffset <= aChunk.mDuration);
    mOutputBufferFrameCount += mSubChunk.mDuration;
    MOZ_ASSERT(mOutputBufferFrameCount <= framesPerPacket);

    if (mOutputBufferFrameCount == framesPerPacket) {
      // Have a complete packet.  Analyze it.
      EnsureAudioProcessing(aTrack);
      for (uint32_t channel = 0; channel < channelCount; channel++) {
        channelPtrs[channel] = &mOutputBuffer[channel * framesPerPacket];
      }
      StreamConfig reverseConfig(sampleRate, channelCount);
      DebugOnly<int> err = mAudioProcessing->AnalyzeReverseStream(
          channelPtrs.Elements(), reverseConfig);
      MOZ_ASSERT(!err, "Could not process the reverse stream.");

      mOutputBufferFrameCount = 0;
    }
  } while (chunkOffset < aChunk.mDuration);

  mSubChunk.SetNull(0);
}

// Only called if we're not in passthrough mode
void AudioInputProcessing::PacketizeAndProcess(AudioProcessingTrack* aTrack,
                                               const AudioSegment& aSegment) {
  MediaTrackGraph* graph = aTrack->Graph();
  MOZ_ASSERT(!IsPassThrough(graph),
             "This should be bypassed when in PassThrough mode.");
  MOZ_ASSERT(mEnabled);
  MOZ_ASSERT(mPacketizerInput);
  MOZ_ASSERT(mPacketizerInput->mPacketSize ==
             GetPacketSize(aTrack->mSampleRate));

  // Calculate number of the pending frames in mChunksInPacketizer.
  auto pendingFrames = [&]() {
    TrackTime frames = 0;
    for (const auto& p : mChunksInPacketizer) {
      frames += p.first;
    }
    return frames;
  };

  // Precondition of the Principal-labelling logic below.
  MOZ_ASSERT(mPacketizerInput->FramesAvailable() ==
             static_cast<uint32_t>(pendingFrames()));

  // The WriteToInterleavedBuffer will do upmix or downmix if the channel-count
  // in aSegment's chunks is different from mPacketizerInput->mChannels
  // WriteToInterleavedBuffer could be avoided once Bug 1729041 is done.
  size_t sampleCount = aSegment.WriteToInterleavedBuffer(
      mInterleavedBuffer, mPacketizerInput->mChannels);
  size_t frameCount =
      sampleCount / static_cast<size_t>(mPacketizerInput->mChannels);

  // Packetize our input data into 10ms chunks, deinterleave into planar channel
  // buffers, process, and append to the right MediaStreamTrack.
  // frameCount is one graph iteration's worth and the packetizer is drained to
  // under one packet each call, so the queued sample count stays small and
  // Input always succeeds here.
  MOZ_ALWAYS_TRUE(NS_SUCCEEDED(mPacketizerInput->Input(
      mInterleavedBuffer.Elements(), static_cast<uint32_t>(frameCount))));

  // Update mChunksInPacketizer and make sure the precondition for the
  // Principal-labelling logic still holds.
  for (AudioSegment::ConstChunkIterator iter(aSegment); !iter.IsEnded();
       iter.Next()) {
    MOZ_ASSERT(iter->mDuration > 0);
    mChunksInPacketizer.emplace_back(
        std::make_pair(iter->mDuration, iter->mPrincipalHandle));
  }
  MOZ_ASSERT(mPacketizerInput->FramesAvailable() ==
             static_cast<uint32_t>(pendingFrames()));

  LOG_FRAME(
      "(Graph {}, Driver {}) AudioInputProcessing {} Packetizing {} frames. "
      "Packetizer has {} frames (enough for {} packets) now",
      fmt::ptr(graph), fmt::ptr(graph->CurrentDriver()), fmt::ptr(this),
      frameCount, mPacketizerInput->FramesAvailable(),
      mPacketizerInput->PacketsAvailable());

  size_t offset = 0;

  while (mPacketizerInput->PacketsAvailable()) {
    mPacketCount++;
    MOZ_ASSERT(mInputBuffer.Length() ==
               mPacketizerInput->mPacketSize * mPacketizerInput->mChannels);
    MOZ_ASSERT(mDeinterleavedBuffer.Length() ==
               mPacketizerInput->mPacketSize * mPacketizerInput->mChannels);
    float* packet = mInputBuffer.Data();
    mPacketizerInput->Output(packet);
    mInputDump->Write(packet, mInputBuffer.Length());

    // Downmix from mPacketizerInput->mChannels to mono if needed. We always
    // have floats here, the packetizer performed the conversion.
    AutoTArray<float*, 8> deinterleavedPacketizedInputDataChannelPointers;
    uint32_t channelCountInput = 0;
    if (mPacketizerInput->mChannels > MAX_CHANNELS) {
      channelCountInput = MONO;
      deinterleavedPacketizedInputDataChannelPointers.SetLength(
          channelCountInput);
      deinterleavedPacketizedInputDataChannelPointers[0] =
          mDeinterleavedBuffer.Data();
      // Downmix to mono (and effectively have a planar buffer) by summing all
      // channels in the first channel, and scaling by the number of channels to
      // avoid clipping.
      float gain = 1.f / mPacketizerInput->mChannels;
      size_t readIndex = 0;
      for (size_t i = 0; i < mPacketizerInput->mPacketSize; i++) {
        mDeinterleavedBuffer.Data()[i] = 0.;
        for (size_t j = 0; j < mPacketizerInput->mChannels; j++) {
          mDeinterleavedBuffer.Data()[i] += gain * packet[readIndex++];
        }
      }
    } else {
      channelCountInput = mPacketizerInput->mChannels;
      webrtc::InterleavedView<const float> interleaved(
          packet, mPacketizerInput->mPacketSize, channelCountInput);
      webrtc::DeinterleavedView<float> deinterleaved(
          mDeinterleavedBuffer.Data(), mPacketizerInput->mPacketSize,
          channelCountInput);

      Deinterleave(interleaved, deinterleaved);

      // Set up pointers into the deinterleaved data for code below
      deinterleavedPacketizedInputDataChannelPointers.SetLength(
          channelCountInput);
      for (size_t i = 0;
           i < deinterleavedPacketizedInputDataChannelPointers.Length(); ++i) {
        deinterleavedPacketizedInputDataChannelPointers[i] =
            deinterleaved[i].data();
      }
    }

    StreamConfig inputConfig(aTrack->mSampleRate, channelCountInput);
    StreamConfig outputConfig = inputConfig;

    EnsureAudioProcessing(aTrack);
    // Bug 1404965: Get the right delay here, it saves some work down the line.
    mAudioProcessing->set_stream_delay_ms(0);

    // Bug 1414837: find a way to not allocate here.
    CheckedInt<size_t> bufferSize(sizeof(float));
    bufferSize *= mPacketizerInput->mPacketSize;
    bufferSize *= channelCountInput;
    RefPtr<SharedBuffer> buffer = SharedBuffer::Create(bufferSize);

    // Prepare channel pointers to the SharedBuffer created above.
    AutoTArray<float*, 8> processedOutputChannelPointers;
    AutoTArray<const float*, 8> processedOutputChannelPointersConst;
    processedOutputChannelPointers.SetLength(channelCountInput);
    processedOutputChannelPointersConst.SetLength(channelCountInput);

    offset = 0;
    for (size_t i = 0; i < processedOutputChannelPointers.Length(); ++i) {
      processedOutputChannelPointers[i] =
          static_cast<float*>(buffer->Data()) + offset;
      processedOutputChannelPointersConst[i] =
          static_cast<float*>(buffer->Data()) + offset;
      offset += mPacketizerInput->mPacketSize;
    }

    mAudioProcessing->ProcessStream(
        deinterleavedPacketizedInputDataChannelPointers.Elements(), inputConfig,
        outputConfig, processedOutputChannelPointers.Elements());

    if (mOutputDump.isNothing()) {
      mOutputDump.emplace();
      mOutputDump->Open("AudioProcessingOutput", channelCountInput,
                        aTrack->mSampleRate);
    }
    for (uint32_t f = 0; f < mPacketizerInput->mPacketSize; ++f) {
      for (uint32_t c = 0; c < channelCountInput; ++c) {
        packet[f * channelCountInput + c] =
            processedOutputChannelPointers[c][f];
      }
    }
    mOutputDump->Write(packet,
                       mPacketizerInput->mPacketSize * channelCountInput);

    // If logging is enabled, dump the audio processing stats twice a second
    if (MOZ_LOG_TEST(gMediaManagerLog, LogLevel::Debug) &&
        !(mPacketCount % 50)) {
      AudioProcessingStats stats = mAudioProcessing->GetStatistics();
      char msg[1024];
      msg[0] = '\0';
      size_t offset = 0;
#define AddIfValue(format, member)                                       \
  if (stats.member.has_value()) {                                        \
    offset += SprintfBuf(msg + offset, sizeof(msg) - offset,             \
                         #member ":" format ", ", stats.member.value()); \
  }
      AddIfValue("%d", voice_detected);
      AddIfValue("%lf", echo_return_loss);
      AddIfValue("%lf", echo_return_loss_enhancement);
      AddIfValue("%lf", divergent_filter_fraction);
      AddIfValue("%d", delay_median_ms);
      AddIfValue("%d", delay_standard_deviation_ms);
      AddIfValue("%d", delay_ms);
#undef AddIfValue
      LOG("AudioProcessing statistics: {}", msg);
    }

    if (mEnded) {
      continue;
    }

    // We already have planar audio data of the right format. Insert into the
    // MTG.
    MOZ_ASSERT(processedOutputChannelPointers.Length() == channelCountInput);

    // Insert the processed data chunk by chunk to mSegment with the paired
    // PrincipalHandle value. The chunks are tracked in mChunksInPacketizer.

    auto getAudioChunk = [&](TrackTime aStart, TrackTime aEnd,
                             const PrincipalHandle& aPrincipalHandle) {
      if (aStart == aEnd) {
        return AudioChunk();
      }
      RefPtr<SharedBuffer> other = buffer;
      AudioChunk c =
          AudioChunk(other.forget(), processedOutputChannelPointersConst,
                     static_cast<TrackTime>(mPacketizerInput->mPacketSize),
                     aPrincipalHandle);
      c.SliceTo(aStart, aEnd);
      return c;
    };

    // The number of frames of data that needs to be labelled with Principal
    // values.
    TrackTime len = static_cast<TrackTime>(mPacketizerInput->mPacketSize);
    // The start offset of the unlabelled chunk.
    TrackTime start = 0;
    // By mChunksInPacketizer's information, we can keep labelling the
    // unlabelled frames chunk by chunk.
    while (!mChunksInPacketizer.empty()) {
      auto& [frames, principal] = mChunksInPacketizer.front();
      const TrackTime end = start + frames;
      if (end > len) {
        // If the left unlabelled frames are part of this chunk, then we need to
        // adjust the number of frames in the chunk.
        if (len > start) {
          mSegment.AppendAndConsumeChunk(getAudioChunk(start, len, principal));
          frames -= len - start;
        }
        break;
      }
      // Otherwise, the number of unlabelled frames is larger than or equal to
      // this chunk. We can label the whole chunk directly.
      mSegment.AppendAndConsumeChunk(getAudioChunk(start, end, principal));
      start = end;
      mChunksInPacketizer.pop_front();
    }

    LOG_FRAME(
        "(Graph {}, Driver {}) AudioInputProcessing {} Appending {} frames of "
        "packetized audio, leaving {} frames in packetizer ({} frames in "
        "mChunksInPacketizer)",
        fmt::ptr(graph), fmt::ptr(graph->CurrentDriver()), fmt::ptr(this),
        mPacketizerInput->mPacketSize, mPacketizerInput->FramesAvailable(),
        pendingFrames());

    // Postcondition of the Principal-labelling logic.
    MOZ_ASSERT(mPacketizerInput->FramesAvailable() ==
               static_cast<uint32_t>(pendingFrames()));
  }
}

void AudioInputProcessing::DeviceChanged(MediaTrackGraph* aGraph) {
  aGraph->AssertOnGraphThread();

  // Reset some processing
  if (mAudioProcessing) {
    mAudioProcessing->Initialize();
  }
  LOG_FRAME(
      "(Graph {}, Driver {}) AudioInputProcessing {} Reinitializing audio "
      "processing",
      fmt::ptr(aGraph), fmt::ptr(aGraph->CurrentDriver()), fmt::ptr(this));
}

cubeb_input_processing_params
AudioInputProcessing::RequestedInputProcessingParams(
    MediaTrackGraph* aGraph) const {
  aGraph->AssertOnGraphThread();
  if (!mPlatformProcessingEnabled) {
    return CUBEB_INPUT_PROCESSING_PARAM_NONE;
  }
  if (mPlatformProcessingSetError) {
    return CUBEB_INPUT_PROCESSING_PARAM_NONE;
  }
  cubeb_input_processing_params params = CUBEB_INPUT_PROCESSING_PARAM_NONE;
  if (mSettings.mAecOn) {
    params |= CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION;
  }
  if (mSettings.mAgcOn) {
    params |= CUBEB_INPUT_PROCESSING_PARAM_AUTOMATIC_GAIN_CONTROL;
  }
  if (mSettings.mNoiseOn) {
    params |= CUBEB_INPUT_PROCESSING_PARAM_NOISE_SUPPRESSION;
  }
  return params;
}

void AudioInputProcessing::ApplySettings(MediaTrackGraph* aGraph,
                                         CubebUtils::AudioDeviceID aDeviceID,
                                         const MediaEnginePrefs& aSettings) {
  TRACE("AudioInputProcessing::ApplySettings");
  aGraph->AssertOnGraphThread();

  // CUBEB_ERROR_NOT_SUPPORTED means the backend does not support platform
  // processing. In that case, leave the error in place so we don't request
  // processing anew.
  if (mPlatformProcessingSetError.valueOr(CUBEB_OK) !=
      CUBEB_ERROR_NOT_SUPPORTED) {
    mPlatformProcessingSetError = Nothing();
  }

  // Read previous state from mSettings.
  uint32_t oldChannelCount = GetRequestedInputChannelCount();

  ApplySettingsInternal(aGraph, aSettings);

  if (oldChannelCount != GetRequestedInputChannelCount()) {
    RequestedInputChannelCountChanged(aGraph, aDeviceID);
  }
}

void AudioInputProcessing::ApplySettingsInternal(
    MediaTrackGraph* aGraph, const MediaEnginePrefs& aSettings) {
  TRACE("AudioInputProcessing::ApplySettingsInternal");
  aGraph->AssertOnGraphThread();

  mPlatformProcessingEnabled = aSettings.mUsePlatformProcessing;

  // Read previous state from the applied config.
  bool wasPassThrough = IsPassThrough(aGraph);

  mSettings = aSettings;
  mAppliedConfig = ConfigForPrefs(aGraph, aSettings);
  if (mAudioProcessing) {
    mAudioProcessing->ApplyConfig(mAppliedConfig);
  }

  if (wasPassThrough != IsPassThrough(aGraph)) {
    PassThroughChanged(aGraph);
  }
}

const webrtc::AudioProcessing::Config& AudioInputProcessing::AppliedConfig(
    MediaTrackGraph* aGraph) const {
  aGraph->AssertOnGraphThread();
  return mAppliedConfig;
}

void AudioInputProcessing::End() {
  mEnded = true;
  mSegment.Clear();
}

TrackTime AudioInputProcessing::NumBufferedFrames(
    MediaTrackGraph* aGraph) const {
  aGraph->AssertOnGraphThread();
  return mSegment.GetDuration();
}

void AudioInputProcessing::SetEnvironmentWrapper(
    AudioProcessingTrack* aTrack,
    RefPtr<WebrtcEnvironmentWrapper> aEnvWrapper) {
  aTrack->AssertOnGraphThread();
  mEnvWrapper = std::move(aEnvWrapper);
}

void AudioInputProcessing::EnsurePacketizer(AudioProcessingTrack* aTrack) {
  aTrack->AssertOnGraphThread();
  MOZ_ASSERT(mEnabled);
  MediaTrackGraph* graph = aTrack->Graph();
  MOZ_ASSERT(!IsPassThrough(graph));

  uint32_t channelCount = GetRequestedInputChannelCount();
  MOZ_ASSERT(channelCount > 0);
  if (mPacketizerInput && mPacketizerInput->mChannels == channelCount) {
    return;
  }

  // If mPacketizerInput exists but with different channel-count, there is no
  // need to change pre-buffering since the packet size is the same as the old
  // one, since the rate is a constant.
  MOZ_ASSERT_IF(mPacketizerInput, mPacketizerInput->mPacketSize ==
                                      GetPacketSize(aTrack->mSampleRate));
  bool needPreBuffering = !mPacketizerInput;
  if (mPacketizerInput) {
    const TrackTime numBufferedFrames =
        static_cast<TrackTime>(mPacketizerInput->FramesAvailable());
    mSegment.AppendNullData(numBufferedFrames);
    mPacketizerInput = Nothing();
    mChunksInPacketizer.clear();
  }

  mInputDump.reset();
  mOutputDump.reset();
  mPacketizerInput.emplace(GetPacketSize(aTrack->mSampleRate), channelCount);
  MOZ_RELEASE_ASSERT(
      mInputBuffer.SetLength(mPacketizerInput->mPacketSize * channelCount));
  MOZ_RELEASE_ASSERT(mDeinterleavedBuffer.SetLength(
      mPacketizerInput->mPacketSize * channelCount));
  mInputDump.emplace();
  mInputDump->Open("AudioProcessingInput", channelCount, aTrack->mSampleRate);

  if (needPreBuffering) {
    LOG_FRAME(
        "(Graph {}, Driver {}) AudioInputProcessing {}: Adding {} frames of "
        "silence as pre-buffering",
        fmt::ptr(graph), fmt::ptr(graph->CurrentDriver()), fmt::ptr(this),
        mPacketizerInput->mPacketSize);

    AudioSegment buffering;
    buffering.AppendNullData(
        static_cast<TrackTime>(mPacketizerInput->mPacketSize));
    PacketizeAndProcess(aTrack, buffering);
  }
}

void AudioInputProcessing::EnsureAudioProcessing(AudioProcessingTrack* aTrack) {
  aTrack->AssertOnGraphThread();

  MediaTrackGraph* graph = aTrack->Graph();
  // If the AEC might need to deal with drift then inform it of this and it
  // will be less conservative about echo suppression.  This can lead to some
  // suppression of non-echo signal, so do this only when drift is expected.
  // https://bugs.chromium.org/p/webrtc/issues/detail?id=11985#c2
  bool haveAECAndDrift = mSettings.mAecOn;
  if (haveAECAndDrift) {
    if (mSettings.mExpectDrift < 0) {
      haveAECAndDrift =
          graph->OutputForAECMightDrift() ||
          aTrack->GetDeviceInputTrackGraphThread()->AsNonNativeInputTrack();
    } else {
      haveAECAndDrift = mSettings.mExpectDrift > 0;
    }
  }
  if (!mAudioProcessing || haveAECAndDrift != mHadAECAndDrift) {
    TRACE("AudioProcessing creation");
    LOG("Track {} AudioInputProcessing {} creating AudioProcessing. "
        "aec+drift: {}",
        fmt::ptr(aTrack), fmt::ptr(this), haveAECAndDrift ? "Y" : "N");
    MOZ_ASSERT(mEnvWrapper);
    mHadAECAndDrift = haveAECAndDrift;
    BuiltinAudioProcessingBuilder builder;
    builder.SetConfig(AppliedConfig(graph));
    if (haveAECAndDrift) {
      // Setting an EchoControlFactory always enables AEC, overriding
      // Config::echo_canceller.enabled, so do this only when AEC is enabled.
      EchoCanceller3Config aec3Config;
      aec3Config.echo_removal_control.has_clock_drift = true;
      builder.SetEchoControlFactory(
          std::make_unique<EchoCanceller3Factory>(aec3Config));
    }
    mAudioProcessing.reset(builder.Build(mEnvWrapper->Environment()).release());
  }
}

void AudioInputProcessing::ResetAudioProcessing(MediaTrackGraph* aGraph) {
  aGraph->AssertOnGraphThread();
  MOZ_ASSERT(IsPassThrough(aGraph) || !mEnabled);

  LOG_FRAME(
      "(Graph {}, Driver {}) AudioInputProcessing {} Resetting audio "
      "processing",
      fmt::ptr(aGraph), fmt::ptr(aGraph->CurrentDriver()), fmt::ptr(this));

  // Reset AudioProcessing so that if we resume processing in the future it
  // doesn't depend on old state.
  if (mAudioProcessing) {
    mAudioProcessing->Initialize();
  }

  MOZ_ASSERT_IF(mPacketizerInput,
                static_cast<uint32_t>(mSegment.GetDuration()) +
                        mPacketizerInput->FramesAvailable() ==
                    mPacketizerInput->mPacketSize);

  // It's ok to clear all the internal buffer here since we won't use mSegment
  // in pass-through mode or when audio processing is disabled.
  LOG_FRAME(
      "(Graph {}, Driver {}) AudioInputProcessing {} Emptying out {} frames of "
      "data",
      fmt::ptr(aGraph), fmt::ptr(aGraph->CurrentDriver()), fmt::ptr(this),
      mSegment.GetDuration());
  mSegment.Clear();

  mPacketizerInput = Nothing();
  mChunksInPacketizer.clear();
}

void AudioProcessingTrack::Destroy() {
  MOZ_ASSERT(NS_IsMainThread());
  DisconnectDeviceInput();

  MediaTrack::Destroy();
}

void AudioProcessingTrack::SetInputProcessing(
    RefPtr<AudioInputProcessing> aInputProcessing) {
  if (IsDestroyed()) {
    return;
  }

  RefPtr<WebrtcEnvironmentWrapper> envWrapper =
      WebrtcEnvironmentWrapper::Create(dom::RTCStatsTimestampMaker::Create(
          nsGlobalWindowInner::GetInnerWindowWithId(GetWindowId())));

  QueueControlMessageWithNoShutdown(
      [self = RefPtr{this}, this, inputProcessing = std::move(aInputProcessing),
       envWrapper = std::move(envWrapper)]() mutable {
        TRACE("AudioProcessingTrack::SetInputProcessingImpl");
        inputProcessing->SetEnvironmentWrapper(self, std::move(envWrapper));
        SetInputProcessingImpl(std::move(inputProcessing));
      });
}

AudioProcessingTrack* AudioProcessingTrack::Create(MediaTrackGraph* aGraph) {
  MOZ_ASSERT(NS_IsMainThread());
  AudioProcessingTrack* track = new AudioProcessingTrack(aGraph->GraphRate());
  aGraph->AddTrack(track, {});
  return track;
}

void AudioProcessingTrack::DestroyImpl() {
  ProcessedMediaTrack::DestroyImpl();
  if (mInputProcessing) {
    mInputProcessing->End();
  }
}

void AudioProcessingTrack::ProcessInput(GraphTime aFrom, GraphTime aTo,
                                        uint32_t aFlags) {
  TRACE_COMMENT("AudioProcessingTrack::ProcessInput", "AudioProcessingTrack %p",
                this);
  MOZ_ASSERT(mInputProcessing);
  MOZ_ASSERT(aFrom < aTo);

  LOG_FRAME(
      "(Graph {}, Driver {}) AudioProcessingTrack {} ProcessInput from {} to "
      "{}, needs {} frames",
      fmt::ptr(mGraph), fmt::ptr(mGraph->CurrentDriver()), fmt::ptr(this),
      aFrom, aTo, aTo - aFrom);

  if (!mInputProcessing->IsEnded()) {
    MOZ_ASSERT(TrackTimeToGraphTime(GetEnd()) == aFrom);
    if (mInputs.IsEmpty()) {
      GetData<AudioSegment>()->AppendNullData(aTo - aFrom);
      LOG_FRAME(
          "(Graph {}, Driver {}) AudioProcessingTrack {} Filling {} frames of "
          "null data (no input source)",
          fmt::ptr(mGraph), fmt::ptr(mGraph->CurrentDriver()), fmt::ptr(this),
          aTo - aFrom);
    } else {
      MOZ_ASSERT(mInputs.Length() == 1);
      AudioSegment data;
      DeviceInputConsumerTrack::GetInputSourceData(data, aFrom, aTo);
      mInputProcessing->Process(this, aFrom, aTo, &data,
                                GetData<AudioSegment>());
    }
    MOZ_ASSERT(TrackTimeToGraphTime(GetEnd()) == aTo);

    ApplyTrackDisabling(mSegment.get());
  } else if (aFlags & ALLOW_END) {
    mEnded = true;
  }
}

void AudioProcessingTrack::NotifyOutputData(MediaTrackGraph* aGraph,
                                            const AudioChunk& aChunk) {
  MOZ_ASSERT(mGraph == aGraph, "Cannot feed audio output to another graph");
  AssertOnGraphThread();
  if (mInputProcessing) {
    mInputProcessing->ProcessOutputData(this, aChunk);
  }
}

void AudioProcessingTrack::SetInputProcessingImpl(
    RefPtr<AudioInputProcessing> aInputProcessing) {
  AssertOnGraphThread();
  mInputProcessing = std::move(aInputProcessing);
}

MediaEngineWebRTCAudioCaptureSource::MediaEngineWebRTCAudioCaptureSource(
    const MediaDevice* aMediaDevice) {
  MOZ_ASSERT(aMediaDevice->mMediaSource == MediaSourceEnum::AudioCapture);
}

/* static */
nsString MediaEngineWebRTCAudioCaptureSource::GetUUID() {
  nsID uuid{};
  char uuidBuffer[NSID_LENGTH];
  nsCString asciiString;
  ErrorResult rv;

  rv = nsID::GenerateUUIDInPlace(uuid);
  if (rv.Failed()) {
    return u""_ns;
  }

  uuid.ToProvidedString(uuidBuffer);
  asciiString.AssignASCII(uuidBuffer);

  // Remove {} and the null terminator
  return NS_ConvertASCIItoUTF16(Substring(asciiString, 1, NSID_LENGTH - 3));
}

/* static */
nsString MediaEngineWebRTCAudioCaptureSource::GetGroupId() {
  return u"AudioCaptureGroup"_ns;
}

void MediaEngineWebRTCAudioCaptureSource::SetTrack(
    const RefPtr<MediaTrack>& aTrack, const PrincipalHandle& aPrincipalHandle) {
  AssertIsOnOwningThread();
  // Nothing to do here. aTrack is a placeholder dummy and not exposed.
}

nsresult MediaEngineWebRTCAudioCaptureSource::Start() {
  AssertIsOnOwningThread();
  return NS_OK;
}

nsresult MediaEngineWebRTCAudioCaptureSource::Stop() {
  AssertIsOnOwningThread();
  return NS_OK;
}

nsresult MediaEngineWebRTCAudioCaptureSource::Reconfigure(
    const dom::MediaTrackConstraints& aConstraints,
    const MediaEnginePrefs& aPrefs, const char** aOutBadConstraint) {
  return NS_OK;
}

void MediaEngineWebRTCAudioCaptureSource::GetSettings(
    dom::MediaTrackSettings& aOutSettings) const {
  aOutSettings.mAutoGainControl.Construct(false);
  aOutSettings.mEchoCancellation.Construct(false);
  aOutSettings.mNoiseSuppression.Construct(false);
  aOutSettings.mChannelCount.Construct(1);
}

}  // namespace mozilla

// Don't allow our macros to leak into other cpps in our unified build unit.
#undef MAX_CHANNELS
#undef MONO
#undef MAX_SAMPLING_FREQ
