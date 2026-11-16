/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AudioGenerator.h"
#include "MediaEngineWebRTCAudio.h"
#include "MediaTrackGraphImpl.h"
#include "PrincipalHandle.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "libwebrtcglue/SystemTime.h"
#include "libwebrtcglue/WebrtcEnvironmentWrapper.h"
#include "mozilla/NullPrincipal.h"
#include "nsContentUtils.h"
#include "nsTArray.h"

using namespace mozilla;
using testing::NiceMock;
using testing::Return;

template <class T>
AudioChunk CreateAudioChunk(uint32_t aFrames, uint32_t aChannels,
                            AudioSampleFormat aSampleFormat);

class MockGraph : public MediaTrackGraphImpl {
 public:
  explicit MockGraph(TrackRate aRate)
      : MediaTrackGraphImpl(0, aRate, nullptr, AbstractThread::MainThread()) {
    ON_CALL(*this, OnGraphThread).WillByDefault(Return(true));
  }

  void Init(uint32_t aChannels) {
    MediaTrackGraphImpl::Init(OFFLINE_THREAD_DRIVER, DIRECT_DRIVER, aChannels);

    MonitorAutoLock lock(mMonitor);
    // We don't need a graph driver.  Advance to
    // LIFECYCLE_WAITING_FOR_TRACK_DESTRUCTION so that the driver never
    // starts.  Graph control messages run as in shutdown, synchronously.
    // This permits the main thread part of track initialization through
    // AudioProcessingTrack::Create().
    mLifecycleState = LIFECYCLE_WAITING_FOR_TRACK_DESTRUCTION;
#ifdef DEBUG
    mCanRunMessagesSynchronously = true;
#endif
    // Remove this graph's driver since it holds a ref. We are still kept
    // alive by the self-ref. Destroy() must be called to break that cycle if
    // no tracks are created and destroyed.
    mDriver = nullptr;
  }

  void ForceOutputDeviceForAEC(CubebUtils::AudioDeviceID aID) {
    mOutputDeviceForAEC = aID;
  }

  void ForceDefaultOutputDevice(CubebUtils::AudioDeviceID aID) {
    mDefaultOutputDeviceID = aID;
  }

  void UpdateEnumeratorDefaultDeviceTracking() override {}

  MOCK_CONST_METHOD0(OnGraphThread, bool());

 protected:
  ~MockGraph() = default;
};

// AudioInputProcessing will put extra frames as pre-buffering data to avoid
// glitchs in non pass-through mode. The main goal of the test is to check how
// many frames left in the AudioInputProcessing's mSegment in various situations
// after input data has been processed.
TEST(TestAudioInputProcessing, Buffering)
{
  const TrackRate rate = 8000;  // So packet size is 80
  const uint32_t channels = 1;
  auto graph = MakeRefPtr<NiceMock<MockGraph>>(rate);
  graph->Init(channels);
  RefPtr track = AudioProcessingTrack::Create(graph);

  auto aip = MakeRefPtr<AudioInputProcessing>(channels);
  auto envWrapper = WebrtcEnvironmentWrapper::Create(
      mozilla::dom::RTCStatsTimestampMaker::Create());
  aip->SetEnvironmentWrapper(track, std::move(envWrapper));

  const size_t frames = 72;

  AudioGenerator<AudioDataValue> generator(channels, rate);
  GraphTime processedTime;
  GraphTime nextTime;
  AudioSegment output;
  MediaEnginePrefs settings;
  settings.mChannels = channels;
  // pref "media.getusermedia.agc2_forced" defaults to true.
  // mAgc would need to be set to something other than kAdaptiveAnalog
  // for mobile, as asserted in AudioInputProcessing::ConfigForPrefs,
  // if gain_controller1 were used.
  settings.mAgc2Forced = true;

  // Toggle pass-through mode without starting
  {
    EXPECT_EQ(aip->IsPassThrough(graph), true);
    EXPECT_EQ(aip->NumBufferedFrames(graph), 0);

    settings.mAgcOn = true;
    aip->ApplySettings(graph, nullptr, settings);
    EXPECT_EQ(aip->IsPassThrough(graph), false);
    EXPECT_EQ(aip->NumBufferedFrames(graph), 0);

    settings.mAgcOn = false;
    aip->ApplySettings(graph, nullptr, settings);
    EXPECT_EQ(aip->NumBufferedFrames(graph), 0);
  }

  {
    // Need (nextTime - processedTime) = 128 - 0 = 128 frames this round.
    // aip has not started and set to processing mode yet, so output will be
    // filled with silence data directly.
    processedTime = 0;
    nextTime = MediaTrackGraphImpl::RoundUpToEndOfAudioBlock(frames);

    AudioSegment input;
    generator.Generate(input, nextTime - processedTime);

    aip->Process(track, processedTime, nextTime, &input, &output);
    EXPECT_EQ(input.GetDuration(), nextTime - processedTime);
    EXPECT_EQ(output.GetDuration(), nextTime);
    EXPECT_EQ(aip->NumBufferedFrames(graph), 0);
  }

  // Set aip to processing/non-pass-through mode
  settings.mAgcOn = true;
  aip->ApplySettings(graph, nullptr, settings);
  {
    // Need (nextTime - processedTime) = 256 - 128 = 128 frames this round.
    // aip has not started yet, so output will be filled with silence data
    // directly.
    processedTime = nextTime;
    nextTime = MediaTrackGraphImpl::RoundUpToEndOfAudioBlock(2 * frames);

    AudioSegment input;
    generator.Generate(input, nextTime - processedTime);

    aip->Process(track, processedTime, nextTime, &input, &output);
    EXPECT_EQ(input.GetDuration(), nextTime - processedTime);
    EXPECT_EQ(output.GetDuration(), nextTime);
    EXPECT_EQ(aip->NumBufferedFrames(graph), 0);
  }

  // aip has been set to processing mode and is started.
  aip->Start(graph);
  {
    // Need (nextTime - processedTime) = 256 - 256 = 0 frames this round.
    // Process() will return early on 0 frames of input.
    // Pre-buffering is not triggered.
    processedTime = nextTime;
    nextTime = MediaTrackGraphImpl::RoundUpToEndOfAudioBlock(3 * frames);

    AudioSegment input;
    generator.Generate(input, nextTime - processedTime);

    aip->Process(track, processedTime, nextTime, &input, &output);
    EXPECT_EQ(input.GetDuration(), nextTime - processedTime);
    EXPECT_EQ(output.GetDuration(), nextTime);
    EXPECT_EQ(aip->NumBufferedFrames(graph), 0);
  }

  {
    // Need (nextTime - processedTime) = 384 - 256 = 128 frames this round.
    // On receipt of the these first frames, aip will insert 80 frames
    // into its internal buffer as pre-buffering.
    // Process() will take 128 frames from input, packetize and process
    // these frames into floor(128/80) = 1 80-frame packet (48 frames left in
    // packetizer), insert packets into aip's internal buffer, then move 128
    // frames the internal buffer to output, leaving 80 + 80 - 128 = 32 frames
    // in aip's internal buffer.
    processedTime = nextTime;
    nextTime = MediaTrackGraphImpl::RoundUpToEndOfAudioBlock(4 * frames);

    AudioSegment input;
    generator.Generate(input, nextTime - processedTime);

    aip->Process(track, processedTime, nextTime, &input, &output);
    EXPECT_EQ(input.GetDuration(), nextTime - processedTime);
    EXPECT_EQ(output.GetDuration(), nextTime);
    EXPECT_EQ(aip->NumBufferedFrames(graph), 32);
  }

  {
    // Need (nextTime - processedTime) = 384 - 384 = 0 frames this round.
    processedTime = nextTime;
    nextTime = MediaTrackGraphImpl::RoundUpToEndOfAudioBlock(5 * frames);

    AudioSegment input;
    generator.Generate(input, nextTime - processedTime);

    aip->Process(track, processedTime, nextTime, &input, &output);
    EXPECT_EQ(input.GetDuration(), nextTime - processedTime);
    EXPECT_EQ(output.GetDuration(), nextTime);
    EXPECT_EQ(aip->NumBufferedFrames(graph), 32);
  }

  {
    // Need (nextTime - processedTime) = 512 - 384 = 128 frames this round.
    // The Process() aip will take 128 frames from input, packetize and process
    // these frames into floor(128+48/80) = 2 80-frame packet (16 frames left in
    // packetizer), insert packets into aip's internal buffer, then move 128
    // frames the internal buffer to output, leaving 32 + 2*80 - 128 = 64 frames
    // in aip's internal buffer.
    processedTime = nextTime;
    nextTime = MediaTrackGraphImpl::RoundUpToEndOfAudioBlock(6 * frames);

    AudioSegment input;
    generator.Generate(input, nextTime - processedTime);

    aip->Process(track, processedTime, nextTime, &input, &output);
    EXPECT_EQ(input.GetDuration(), nextTime - processedTime);
    EXPECT_EQ(output.GetDuration(), nextTime);
    EXPECT_EQ(aip->NumBufferedFrames(graph), 64);
  }

  // Set aip to pass-through mode
  settings.mAgcOn = false;
  aip->ApplySettings(graph, nullptr, settings);
  {
    // Need (nextTime - processedTime) = 512 - 512 = 0 frames this round.
    // No buffering in pass-through mode
    processedTime = nextTime;
    nextTime = MediaTrackGraphImpl::RoundUpToEndOfAudioBlock(7 * frames);

    AudioSegment input;
    generator.Generate(input, nextTime - processedTime);

    aip->Process(track, processedTime, nextTime, &input, &output);
    EXPECT_EQ(input.GetDuration(), nextTime - processedTime);
    EXPECT_EQ(output.GetDuration(), processedTime);
    EXPECT_EQ(aip->NumBufferedFrames(graph), 0);
  }

  aip->Stop(graph);
  track->Destroy();
}

TEST(TestAudioInputProcessing, ProcessDataWithDifferentPrincipals)
{
  const TrackRate rate = 48000;  // so # of output frames from packetizer is 480
  const uint32_t channels = 2;
  auto graph = MakeRefPtr<NiceMock<MockGraph>>(rate);
  graph->Init(channels);
  RefPtr track = AudioProcessingTrack::Create(graph);

  auto aip = MakeRefPtr<AudioInputProcessing>(channels);
  auto envWrapper = WebrtcEnvironmentWrapper::Create(
      mozilla::dom::RTCStatsTimestampMaker::Create());
  aip->SetEnvironmentWrapper(track, std::move(envWrapper));

  AudioGenerator<AudioDataValue> generator(channels, rate);

  RefPtr<nsIPrincipal> dummy_principal =
      NullPrincipal::CreateWithoutOriginAttributes();
  const PrincipalHandle principal1(dummy_principal.get());
  const PrincipalHandle principal2(nsContentUtils::GetSystemPrincipal());

  // Total 4800 frames. It's easier to test with frames of multiples of 480.
  nsTArray<std::pair<TrackTime, PrincipalHandle>> framesWithPrincipal = {
      {100, principal1},
      {200, PRINCIPAL_HANDLE_NONE},
      {300, principal2},
      {400, principal1},
      {440, PRINCIPAL_HANDLE_NONE},
      // 3 packet-size above.
      {480, principal1},
      {480, principal2},
      {480, PRINCIPAL_HANDLE_NONE},
      // 3 packet-size above.
      {500, principal2},
      {490, principal1},
      {600, principal1},
      {330, principal1}
      // 4 packet-size above.
  };

  // Generate 4800 frames of data with different principals.
  AudioSegment input;
  {
    for (const auto& [duration, principal] : framesWithPrincipal) {
      AudioSegment data;
      generator.Generate(data, duration);
      for (AudioSegment::ChunkIterator it(data); !it.IsEnded(); it.Next()) {
        it->mPrincipalHandle = principal;
      }

      input.AppendFrom(&data);
    }
  }

  auto verifyPrincipals = [&](const AudioSegment& data) {
    TrackTime start = 0;
    for (const auto& [duration, principal] : framesWithPrincipal) {
      const TrackTime end = start + duration;

      AudioSegment slice;
      slice.AppendSlice(data, start, end);
      start = end;

      for (AudioSegment::ChunkIterator it(slice); !it.IsEnded(); it.Next()) {
        EXPECT_EQ(it->mPrincipalHandle, principal);
      }
    }
  };

  // Check the principals in audio-processing mode.
  MediaEnginePrefs settings;
  settings.mChannels = channels;
  settings.mAgcOn = true;
  settings.mAgc2Forced = true;
  aip->ApplySettings(graph, nullptr, settings);
  EXPECT_EQ(aip->IsPassThrough(graph), false);
  aip->Start(graph);
  {
    AudioSegment output;
    {
      AudioSegment data;
      aip->Process(track, 0, 4800, &input, &data);
      EXPECT_EQ(input.GetDuration(), 4800);
      EXPECT_EQ(data.GetDuration(), 4800);

      // Extract another 480 frames to account for delay from pre-buffering.
      EXPECT_EQ(aip->NumBufferedFrames(graph), 480);
      AudioSegment dummy;
      dummy.AppendNullData(480);
      aip->Process(track, 0, 480, &dummy, &data);
      EXPECT_EQ(dummy.GetDuration(), 480);
      EXPECT_EQ(data.GetDuration(), 480 + 4800);

      // Ignore the pre-buffering silence.
      output.AppendSlice(data, 480, 480 + 4800);
    }

    verifyPrincipals(output);
  }

  // Check the principals in pass-through mode.
  settings.mAgcOn = false;
  aip->ApplySettings(graph, nullptr, settings);
  EXPECT_EQ(aip->IsPassThrough(graph), true);
  {
    AudioSegment output;
    aip->Process(track, 0, 4800, &input, &output);
    EXPECT_EQ(input.GetDuration(), 4800);
    EXPECT_EQ(output.GetDuration(), 4800);

    verifyPrincipals(output);
  }

  aip->Stop(graph);
  track->Destroy();
}

TEST(TestAudioInputProcessing, Downmixing)
{
  const TrackRate rate = 44100;
  const uint32_t channels = 4;
  auto graph = MakeRefPtr<NiceMock<MockGraph>>(rate);
  graph->Init(channels);
  RefPtr track = AudioProcessingTrack::Create(graph);

  auto aip = MakeRefPtr<AudioInputProcessing>(channels);
  auto envWrapper = WebrtcEnvironmentWrapper::Create(
      mozilla::dom::RTCStatsTimestampMaker::Create());
  aip->SetEnvironmentWrapper(track, std::move(envWrapper));

  const size_t frames = 44100;

  AudioGenerator<AudioDataValue> generator(channels, rate);
  GraphTime processedTime;
  GraphTime nextTime;

  MediaEnginePrefs settings;
  settings.mChannels = channels;
  settings.mAgcOn = true;
  settings.mAgc2Forced = true;
  aip->ApplySettings(graph, nullptr, settings);
  EXPECT_EQ(aip->IsPassThrough(graph), false);
  aip->Start(graph);

  processedTime = 0;
  nextTime = MediaTrackGraphImpl::RoundUpToEndOfAudioBlock(frames);

  {
    AudioSegment input;
    AudioSegment output;
    generator.Generate(input, nextTime - processedTime);

    // Intentionally reduce the amplitude of the generated sine wave so there's
    // no chance the max amplitude reaches 1.0, but not enough so that 4
    // channels summed together won't clip.
    input.ApplyVolume(0.9);

    // Process is going to see that it has 4 channels of input, and is going to
    // downmix to mono, scaling the input by 1/4 in the process.
    // We can't compare the input and output signal because the sine is going to
    // be mangledui
    aip->Process(track, processedTime, nextTime, &input, &output);
    EXPECT_EQ(input.GetDuration(), nextTime - processedTime);
    EXPECT_EQ(output.GetDuration(), nextTime);
    EXPECT_EQ(output.MaxChannelCount(), 1u);

    // Verify that it doesn't clip: the input signal has likely been mangled by
    // the various processing passes, but at least it shouldn't clip. We know we
    // always have floating point audio here, regardless of the sample-type used
    // by Gecko.
    for (AudioSegment::ChunkIterator iterOutput(output); !iterOutput.IsEnded();
         iterOutput.Next()) {
      const float* const output = iterOutput->ChannelData<float>()[0];
      for (uint32_t i = 0; i < iterOutput->GetDuration(); i++) {
        // Very conservative here, it's likely that the AGC lowers the volume a
        // lot.
        EXPECT_LE(std::abs(output[i]), 0.95);
      }
    }
  }

  // Now, repeat the test in pass-through mode, checking we get the unmodified
  // 4 channels.
  settings.mAgcOn = false;
  aip->ApplySettings(graph, nullptr, settings);
  EXPECT_EQ(aip->IsPassThrough(graph), true);

  AudioSegment input, output;
  processedTime = nextTime;
  nextTime += MediaTrackGraphImpl::RoundUpToEndOfAudioBlock(frames);
  generator.Generate(input, nextTime - processedTime);

  aip->Process(track, processedTime, nextTime, &input, &output);
  EXPECT_EQ(input.GetDuration(), nextTime - processedTime);
  EXPECT_EQ(output.GetDuration(), nextTime - processedTime);
  // This time, no downmix: 4 channels of input, 4 channels of output
  EXPECT_EQ(output.MaxChannelCount(), 4u);

  nsTArray<AudioDataValue> inputLinearized, outputLinearized;
  input.WriteToInterleavedBuffer(inputLinearized, input.MaxChannelCount());
  output.WriteToInterleavedBuffer(outputLinearized, output.MaxChannelCount());

  // The data should be passed through, and exactly equal.
  for (uint32_t i = 0; i < frames * channels; i++) {
    EXPECT_EQ(inputLinearized[i], outputLinearized[i]);
  }

  aip->Stop(graph);
  track->Destroy();
}

TEST(TestAudioInputProcessing, DisabledPlatformProcessing)
{
  const TrackRate rate = 44100;
  const uint32_t channels = 1;
  auto graph = MakeRefPtr<NiceMock<MockGraph>>(rate);
  graph->Init(channels);

  auto aip = MakeRefPtr<AudioInputProcessing>(channels);

  MediaEnginePrefs settings;
  settings.mUsePlatformProcessing = false;
  settings.mAecOn = true;
  aip->ApplySettings(graph, nullptr, settings);
  aip->Start(graph);

  EXPECT_EQ(aip->RequestedInputProcessingParams(graph),
            CUBEB_INPUT_PROCESSING_PARAM_NONE);

  aip->Stop(graph);
  graph->Destroy();
}

TEST(TestAudioInputProcessing, EnabledPlatformProcessing)
{
  const TrackRate rate = 44100;
  const uint32_t channels = 1;
  auto graph = MakeRefPtr<NiceMock<MockGraph>>(rate);
  graph->Init(channels);

  auto aip = MakeRefPtr<AudioInputProcessing>(channels);

  MediaEnginePrefs settings;
  settings.mUsePlatformProcessing = true;
  settings.mAecOn = true;
  aip->ApplySettings(graph, nullptr, settings);
  aip->Start(graph);

  EXPECT_EQ(aip->RequestedInputProcessingParams(graph),
            CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION);

  aip->Stop(graph);
  graph->Destroy();
}

namespace webrtc {
bool operator==(const AudioProcessing::Config& aLhs,
                const AudioProcessing::Config& aRhs) {
  return aLhs.echo_canceller.enabled == aRhs.echo_canceller.enabled &&
         (aLhs.gain_controller1.enabled == aRhs.gain_controller1.enabled ||
          aLhs.gain_controller2.enabled == aRhs.gain_controller2.enabled) &&
         aLhs.noise_suppression.enabled == aRhs.noise_suppression.enabled;
}

static std::ostream& operator<<(
    std::ostream& aStream, const webrtc::AudioProcessing::Config& aConfig) {
  aStream << "webrtc::AudioProcessing::Config[";
  bool hadPrior = false;
  if (aConfig.echo_canceller.enabled) {
    aStream << "AEC";
    hadPrior = true;
  }
  if (aConfig.gain_controller1.enabled || aConfig.gain_controller2.enabled) {
    if (hadPrior) {
      aStream << ", ";
    }
    aStream << "AGC";
  }
  if (aConfig.noise_suppression.enabled) {
    if (hadPrior) {
      aStream << ", ";
    }
    aStream << "NS";
  }
  aStream << "]";
  return aStream;
}
}  // namespace webrtc

TEST(TestAudioInputProcessing, PlatformProcessing)
{
  const TrackRate rate = 44100;
  const uint32_t channels = 1;
  auto graph = MakeRefPtr<NiceMock<MockGraph>>(rate);
  graph->Init(channels);

  auto aip = MakeRefPtr<AudioInputProcessing>(channels);

  MediaEnginePrefs settings;
  settings.mUsePlatformProcessing = true;
  settings.mAecOn = true;
  aip->ApplySettings(graph, nullptr, settings);
  aip->Start(graph);

  webrtc::AudioProcessing::Config echoOnlyConfig;
  echoOnlyConfig.echo_canceller.enabled = true;
  webrtc::AudioProcessing::Config noiseOnlyConfig;
  noiseOnlyConfig.noise_suppression.enabled = true;
  webrtc::AudioProcessing::Config echoNoiseConfig = echoOnlyConfig;
  echoNoiseConfig.noise_suppression.enabled = true;

  // Config is applied, and platform processing requested.
  EXPECT_EQ(aip->RequestedInputProcessingParams(graph),
            CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION);
  EXPECT_EQ(aip->AppliedConfig(graph), echoOnlyConfig);
  EXPECT_FALSE(aip->IsPassThrough(graph));

  // No other constraint requests present.
  aip->NotifySetRequestedInputProcessingParams(
      graph, 1, CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION);
  EXPECT_EQ(aip->AppliedConfig(graph), echoOnlyConfig);
  EXPECT_FALSE(aip->IsPassThrough(graph));

  // Platform processing params successfully applied.
  aip->NotifySetRequestedInputProcessingParamsResult(
      graph, 1, CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION);
  // Turns off the equivalent APM config.
  EXPECT_EQ(aip->AppliedConfig(graph), webrtc::AudioProcessing::Config());
  EXPECT_TRUE(aip->IsPassThrough(graph));

  // Request for a response that comes back out-of-order later.
  aip->NotifySetRequestedInputProcessingParams(
      graph, 2, CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION);

  // Simulate an error after a driver switch.
  aip->NotifySetRequestedInputProcessingParams(
      graph, 3, CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION);
  // Requesting the same config that is already applied; does nothing.
  EXPECT_EQ(aip->AppliedConfig(graph), webrtc::AudioProcessing::Config());
  EXPECT_TRUE(aip->IsPassThrough(graph));
  // Error notification.
  aip->NotifySetRequestedInputProcessingParamsResult(graph, 3,
                                                     Err(CUBEB_ERROR));
  // The APM config is turned back on, and platform processing is requested to
  // be turned off.
  EXPECT_EQ(aip->RequestedInputProcessingParams(graph),
            CUBEB_INPUT_PROCESSING_PARAM_NONE);
  EXPECT_EQ(aip->AppliedConfig(graph), echoOnlyConfig);
  EXPECT_FALSE(aip->IsPassThrough(graph));

  // The request for turning platform processing off.
  aip->NotifySetRequestedInputProcessingParams(
      graph, 4, CUBEB_INPUT_PROCESSING_PARAM_NONE);
  EXPECT_EQ(aip->AppliedConfig(graph), echoOnlyConfig);
  EXPECT_FALSE(aip->IsPassThrough(graph));

  // Pretend there was a response for an old request.
  aip->NotifySetRequestedInputProcessingParamsResult(
      graph, 2, CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION);
  // It does nothing since we are requesting NONE now.
  EXPECT_EQ(aip->RequestedInputProcessingParams(graph),
            CUBEB_INPUT_PROCESSING_PARAM_NONE);
  EXPECT_EQ(aip->AppliedConfig(graph), echoOnlyConfig);
  EXPECT_FALSE(aip->IsPassThrough(graph));

  // Turn it off as requested.
  aip->NotifySetRequestedInputProcessingParamsResult(
      graph, 4, CUBEB_INPUT_PROCESSING_PARAM_NONE);
  EXPECT_EQ(aip->RequestedInputProcessingParams(graph),
            CUBEB_INPUT_PROCESSING_PARAM_NONE);
  EXPECT_EQ(aip->AppliedConfig(graph), echoOnlyConfig);
  EXPECT_FALSE(aip->IsPassThrough(graph));

  // Test partial support for the requested params.
  settings.mNoiseOn = true;
  aip->ApplySettings(graph, nullptr, settings);
  EXPECT_EQ(aip->RequestedInputProcessingParams(graph),
            CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION |
                CUBEB_INPUT_PROCESSING_PARAM_NOISE_SUPPRESSION);
  EXPECT_EQ(aip->AppliedConfig(graph), echoNoiseConfig);
  EXPECT_FALSE(aip->IsPassThrough(graph));
  // The request doesn't change anything.
  aip->NotifySetRequestedInputProcessingParams(
      graph, 5,
      CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION |
          CUBEB_INPUT_PROCESSING_PARAM_NOISE_SUPPRESSION);
  EXPECT_EQ(aip->AppliedConfig(graph), echoNoiseConfig);
  EXPECT_FALSE(aip->IsPassThrough(graph));
  // Only noise suppression was supported in the platform.
  aip->NotifySetRequestedInputProcessingParamsResult(
      graph, 5, CUBEB_INPUT_PROCESSING_PARAM_NOISE_SUPPRESSION);
  // In the APM only echo cancellation is applied.
  EXPECT_EQ(aip->AppliedConfig(graph), echoOnlyConfig);
  EXPECT_FALSE(aip->IsPassThrough(graph));

  // Test error for partial support.
  aip->NotifySetRequestedInputProcessingParams(
      graph, 6,
      CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION |
          CUBEB_INPUT_PROCESSING_PARAM_NOISE_SUPPRESSION);
  EXPECT_EQ(aip->AppliedConfig(graph), echoOnlyConfig);
  EXPECT_FALSE(aip->IsPassThrough(graph));
  aip->NotifySetRequestedInputProcessingParamsResult(graph, 6,
                                                     Err(CUBEB_ERROR));
  // The full config is applied in the APM, and NONE is requested.
  EXPECT_EQ(aip->RequestedInputProcessingParams(graph),
            CUBEB_INPUT_PROCESSING_PARAM_NONE);
  EXPECT_EQ(aip->AppliedConfig(graph), echoNoiseConfig);
  EXPECT_FALSE(aip->IsPassThrough(graph));

  // Enable platform processing again.
  aip->ApplySettings(graph, nullptr, settings);
  EXPECT_EQ(aip->RequestedInputProcessingParams(graph),
            CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION |
                CUBEB_INPUT_PROCESSING_PARAM_NOISE_SUPPRESSION);
  EXPECT_EQ(aip->AppliedConfig(graph), echoNoiseConfig);
  EXPECT_FALSE(aip->IsPassThrough(graph));
  // Request.
  aip->NotifySetRequestedInputProcessingParams(
      graph, 7,
      CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION |
          CUBEB_INPUT_PROCESSING_PARAM_NOISE_SUPPRESSION);
  EXPECT_EQ(aip->AppliedConfig(graph), echoNoiseConfig);
  EXPECT_FALSE(aip->IsPassThrough(graph));
  // It succeeded.
  aip->NotifySetRequestedInputProcessingParamsResult(
      graph, 7,
      static_cast<cubeb_input_processing_params>(
          CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION |
          CUBEB_INPUT_PROCESSING_PARAM_NOISE_SUPPRESSION));
  // No config is applied in the APM, and the full set is requested.
  EXPECT_EQ(aip->RequestedInputProcessingParams(graph),
            CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION |
                CUBEB_INPUT_PROCESSING_PARAM_NOISE_SUPPRESSION);
  EXPECT_EQ(aip->AppliedConfig(graph), webrtc::AudioProcessing::Config());
  EXPECT_TRUE(aip->IsPassThrough(graph));

  // Simulate that another concurrent request was made, i.e. two tracks are
  // using the same device with different processing params, where the
  // intersection of processing params is NONE.
  aip->NotifySetRequestedInputProcessingParams(
      graph, 8, CUBEB_INPUT_PROCESSING_PARAM_NONE);
  // The full config is applied in the APM.
  EXPECT_EQ(aip->AppliedConfig(graph), echoNoiseConfig);
  EXPECT_FALSE(aip->IsPassThrough(graph));
  // The result succeeds, leading to no change since sw processing is already
  // applied.
  aip->NotifySetRequestedInputProcessingParamsResult(
      graph, 8, CUBEB_INPUT_PROCESSING_PARAM_NONE);
  EXPECT_EQ(aip->RequestedInputProcessingParams(graph),
            CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION |
                CUBEB_INPUT_PROCESSING_PARAM_NOISE_SUPPRESSION);
  EXPECT_EQ(aip->AppliedConfig(graph), echoNoiseConfig);
  EXPECT_FALSE(aip->IsPassThrough(graph));

  // The other concurrent request goes away.
  aip->NotifySetRequestedInputProcessingParams(
      graph, 9,
      CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION |
          CUBEB_INPUT_PROCESSING_PARAM_NOISE_SUPPRESSION);
  // The full config is still applied in the APM.
  EXPECT_EQ(aip->AppliedConfig(graph), echoNoiseConfig);
  EXPECT_FALSE(aip->IsPassThrough(graph));
  // The result succeeds, leading to no change since sw processing is already
  // applied.
  aip->NotifySetRequestedInputProcessingParamsResult(
      graph, 9,
      static_cast<cubeb_input_processing_params>(
          CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION |
          CUBEB_INPUT_PROCESSING_PARAM_NOISE_SUPPRESSION));
  EXPECT_EQ(aip->RequestedInputProcessingParams(graph),
            CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION |
                CUBEB_INPUT_PROCESSING_PARAM_NOISE_SUPPRESSION);
  EXPECT_EQ(aip->AppliedConfig(graph), webrtc::AudioProcessing::Config());
  EXPECT_TRUE(aip->IsPassThrough(graph));

  // Changing input track resets the processing params generation. The applied
  // config (AEC, NS) is adapted to the subset applied in the platform (AEC).
  aip->Disconnect(graph);
  aip->NotifySetRequestedInputProcessingParams(
      graph, 1, CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION);
  EXPECT_EQ(aip->AppliedConfig(graph), echoNoiseConfig);
  EXPECT_FALSE(aip->IsPassThrough(graph));
  aip->NotifySetRequestedInputProcessingParamsResult(
      graph, 1, CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION);
  EXPECT_EQ(aip->AppliedConfig(graph), noiseOnlyConfig);
  EXPECT_FALSE(aip->IsPassThrough(graph));

  aip->Stop(graph);
  graph->Destroy();
}

TEST(TestAudioInputProcessing, PlatformProcessingSetSinkId)
{
  const TrackRate rate = 44100;
  const uint32_t channels = 1;
  auto graph = MakeRefPtr<NiceMock<MockGraph>>(rate);
  graph->ForceDefaultOutputDevice(CubebUtils::AudioDeviceID(1));
  graph->ForceOutputDeviceForAEC(CubebUtils::AudioDeviceID(2));
  graph->Init(channels);
  ASSERT_EQ(graph->PrimaryOutputDeviceID(), nullptr);

  RefPtr track = AudioProcessingTrack::Create(graph);
  auto aip = MakeRefPtr<AudioInputProcessing>(channels);
  aip->SetEnvironmentWrapper(track, WebrtcEnvironmentWrapper::Create(
                                        dom::RTCStatsTimestampMaker::Create()));
  track->ConnectDeviceInput(nullptr, aip, PRINCIPAL_HANDLE_NONE);

  MediaEnginePrefs settings;
  settings.mUsePlatformProcessing = true;
  settings.mAecOn = true;
  settings.mAgcOn = true;
  settings.mAgc2Forced = true;
  settings.mNoiseOn = true;
  settings.mChannels = channels;
  aip->ApplySettings(graph, nullptr, settings);
  aip->Start(graph);

  webrtc::AudioProcessing::Config echoOnlyConfig;
  echoOnlyConfig.echo_canceller.enabled = true;
  webrtc::AudioProcessing::Config allConfig;
  allConfig.echo_canceller.enabled = allConfig.noise_suppression.enabled =
      allConfig.gain_controller2.enabled = true;

  constexpr cubeb_input_processing_params PROCESSING_PARAM_ALL =
      CUBEB_INPUT_PROCESSING_PARAM_ECHO_CANCELLATION |
      CUBEB_INPUT_PROCESSING_PARAM_AUTOMATIC_GAIN_CONTROL |
      CUBEB_INPUT_PROCESSING_PARAM_NOISE_SUPPRESSION;

  // Config is applied, and platform processing requested.
  EXPECT_EQ(aip->RequestedInputProcessingParams(graph), PROCESSING_PARAM_ALL);
  EXPECT_EQ(aip->AppliedConfig(graph), allConfig);
  EXPECT_FALSE(aip->IsPassThrough(graph));

  // No other constraint requests present.
  aip->NotifySetRequestedInputProcessingParams(graph, 1, PROCESSING_PARAM_ALL);
  EXPECT_EQ(aip->AppliedConfig(graph), allConfig);
  EXPECT_FALSE(aip->IsPassThrough(graph));

  // Platform processing params successfully applied.
  aip->NotifySetRequestedInputProcessingParamsResult(graph, 1,
                                                     PROCESSING_PARAM_ALL);
  // Because setSinkId is used, AEC (only) is still applied.
  EXPECT_EQ(aip->AppliedConfig(graph), echoOnlyConfig);
  EXPECT_FALSE(aip->IsPassThrough(graph));

  // Changing to primary device for AEC should enable passthrough.
  const GraphTime frames = MediaTrackGraphImpl::RoundUpToEndOfAudioBlock(100);
  AudioGenerator<AudioDataValue> generator(channels, rate);

  graph->ForceOutputDeviceForAEC(CubebUtils::AudioDeviceID(1));
  GraphTime processedTime = 0, nextTime = 0;
  {
    AudioSegment input, output;
    processedTime = nextTime;
    nextTime += frames;
    generator.Generate(input, frames);
    aip->Process(track, processedTime, nextTime, &input, &output);
    for (AudioSegment::ConstChunkIterator it(output); !it.IsEnded();
         it.Next()) {
      aip->ProcessOutputData(track, *it);
    }
  }
  EXPECT_EQ(aip->RequestedInputProcessingParams(graph), PROCESSING_PARAM_ALL);
  EXPECT_EQ(aip->AppliedConfig(graph), webrtc::AudioProcessing::Config());
  EXPECT_TRUE(aip->IsPassThrough(graph));

  // Changing to non-primary device for AEC should turn on AEC again.
  graph->ForceOutputDeviceForAEC(CubebUtils::AudioDeviceID(2));
  {
    AudioSegment input, output;
    processedTime = nextTime;
    nextTime += frames;
    generator.Generate(input, frames);
    aip->Process(track, processedTime, nextTime, &input, &output);
    for (AudioSegment::ConstChunkIterator it(output); !it.IsEnded();
         it.Next()) {
      aip->ProcessOutputData(track, *it);
    }
  }
  EXPECT_EQ(aip->RequestedInputProcessingParams(graph), PROCESSING_PARAM_ALL);
  EXPECT_EQ(aip->AppliedConfig(graph), echoOnlyConfig);
  EXPECT_FALSE(aip->IsPassThrough(graph));

  aip->Stop(graph);
  track->DisconnectDeviceInput();
  track->Destroy();
  graph->Destroy();
}
